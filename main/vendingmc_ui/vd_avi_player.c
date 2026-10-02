#include "vd_avi_player.h"
#include "core/lv_obj_event_private.h"
#include "src/draw/lv_draw_private.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/jpeg_decode.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#define TAG "VD_AVI"

// คิวพัก Raw JPEG Chunks บน PSRAM (512 KB เก็บได้ประมาณ 4-6 เฟรม)
#define RAW_VIDEO_RB_SIZE (512 * 1024)

struct vd_avi_player_t {
    lv_obj_t               *img_obj;
    lv_image_dsc_t          img_dsc;

    // Ping-Pong Framebuffer บน PSRAM (จัดเรียง 64-byte aligned สำหรับ P4)
    uint8_t                *fb[2];
    uint8_t                 cur_fb_idx;
    size_t                  fb_size;
    uint32_t                aligned_w;
    uint32_t                aligned_h;

    // ระบบถอดรหัสวิดีโอและฮาร์ดแวร์ JPEG
    esp32_avidec_t          avidec;
    bool                    avidec_inited;
    jpeg_decoder_handle_t   jpeg_dec;
    bool                    jpeg_dec_inited;

    // คิวและ Worker Task แยกอิสระสำหรับถอดรหัสภาพ (Decoupled Pipeline)
    RingbufHandle_t         raw_video_rb;
    TaskHandle_t            decode_task_hdl;
    volatile bool           decode_task_run;

    // ตัวจัดการเสียง
    esp32_avidec_audio_cb_t audio_cb;
    void                   *audio_ctx;
};

/* ล้างเฟรมภาพดิบค้างในคิว (ใช้ตอน Seek หรือ Stop) */
static void _flush_raw_video_queue(vd_avi_player_t *player) {
    if (!player || !player->raw_video_rb) return;
    size_t sz = 0;
    void *item = NULL;
    while ((item = xRingbufferReceive(player->raw_video_rb, &sz, 0)) != NULL) {
        vRingbufferReturnItem(player->raw_video_rb, item);
    }
}

/* --- Task แยกสำหรับถอดรหัส JPEG และส่งเข้า LVGL (ไม่ดึงเวลาของ Demuxer) --- */
static void _avi_video_decode_task(void *pvParam) {
    vd_avi_player_t *player = (vd_avi_player_t *)pvParam;
    ESP_LOGI(TAG, "Video decode worker task started on Core %d", xPortGetCoreID());

    jpeg_decode_cfg_t dec_cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
    };

    while (player->decode_task_run) {
        size_t item_size = 0;
        // รอรับก้อน Raw JPEG จากคิว สูงสุด 50 ms
        uint8_t *raw_jpeg = (uint8_t *)xRingbufferReceive(player->raw_video_rb, &item_size, pdMS_TO_TICKS(50));
        if (!raw_jpeg) {
            continue;
        }

        if (player->jpeg_dec_inited && player->fb[0] && player->fb[1]) {
            uint8_t next_fb_idx = player->cur_fb_idx ^ 1;
            uint8_t *target_fb = player->fb[next_fb_idx];

            uint32_t actual_out_bytes = 0;
            esp_err_t err = jpeg_decoder_process(player->jpeg_dec, &dec_cfg,
                                                 raw_jpeg, item_size,
                                                 target_fb, player->fb_size,
                                                 &actual_out_bytes);
            if (err == ESP_OK) {
                // อัปเดต Framebuffer เข้า LVGL
                if (lvgl_port_lock(0)) {
                    player->cur_fb_idx = next_fb_idx;
                    player->img_dsc.data = target_fb;

                    if (player->img_obj && lv_obj_is_valid(player->img_obj)) {
                        lv_obj_invalidate(player->img_obj);
                    }
                    lvgl_port_unlock();
                }
            }
        }

        vRingbufferReturnItem(player->raw_video_rb, (void *)raw_jpeg);
    }

    ESP_LOGI(TAG, "Video decode worker task exited");
    player->decode_task_hdl = NULL;
    vTaskDelete(NULL);
}

/* --- Callback รับภาพจาก avi_worker: ทำงานแบบ Non-blocking คืนสิทธิ์ทันที --- */
static void _avi_video_frame_cb(const uint8_t *frame_data, size_t len, uint32_t frame_idx, void *user_ctx) {
    vd_avi_player_t *player = (vd_avi_player_t *)user_ctx;
    if (!player || !player->raw_video_rb || !frame_data || len == 0) return;

    // ส่ง Raw JPEG เข้าคิวล่วงหน้า (รอสูงสุด 10 ms ถ้าคิวเต็มให้ดรอปเฟรมภาพ เพื่อไม่บล็อกเสียง)
    BaseType_t res = xRingbufferSend(player->raw_video_rb, frame_data, len, pdMS_TO_TICKS(10));
    if (res != pdTRUE) {
        ESP_LOGD(TAG, "Video decode queue full, dropped 1 raw frame");
    }
}

/* --- Callback ส่งผ่านข้อมูลเสียง --- */
static void _avi_audio_chunk_cb(const uint8_t *audio_data, size_t len, void *user_ctx) {
    vd_avi_player_t *player = (vd_avi_player_t *)user_ctx;
    if (player && player->audio_cb) {
        player->audio_cb(audio_data, len, player->audio_ctx);
    }
}

/* --- ล้างทรัพยากรเมื่อ Widget ถูกทำลาย --- */
static void _avi_delete_event_cb(lv_event_t *e) {
    vd_avi_player_t *player = (vd_avi_player_t *)lv_event_get_user_data(e);
    if (!player) return;

    // 1. ปิด Demuxer หยุดการอ่านไฟล์
    if (player->avidec_inited) {
        esp32_avidec_deinit(&player->avidec);
        player->avidec_inited = false;
    }

    // 2. หยุด Video Decode Task และรอให้จบการทำงาน
    player->decode_task_run = false;
    int wait_cnt = 0;
    while (player->decode_task_hdl && wait_cnt++ < 25) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // 3. ทำลาย RingBuffer ภาพดิบ
    if (player->raw_video_rb) {
        vRingbufferDelete(player->raw_video_rb);
        player->raw_video_rb = NULL;
    }

    // 4. คืนทรัพยากรฮาร์ดแวร์ JPEG
    if (player->jpeg_dec_inited) {
        jpeg_del_decoder_engine(player->jpeg_dec);
        player->jpeg_dec_inited = false;
    }

    // 5. ปล่อย Framebuffers
    if (player->fb[0]) {
        heap_caps_free(player->fb[0]);
        player->fb[0] = NULL;
    }
    if (player->fb[1]) {
        heap_caps_free(player->fb[1]);
        player->fb[1] = NULL;
    }

    free(player);
}

static void _avi_cover_check_cb(lv_event_t *e) {
    lv_cover_check_info_t *info = (lv_cover_check_info_t *)lv_event_get_param(e);
    if (info) {
        info->res = LV_COVER_RES_COVER; // ข้ามการวาด Background/Widget ด้านหลังทั้งหมด
    }
}

static void _avi_draw_task_filter_cb(lv_event_t *e) {
    lv_draw_task_t *task = lv_event_get_draw_task(e);
    if (!task) return;

    if (lv_draw_task_get_type(task) == LV_DRAW_TASK_TYPE_IMAGE) {
        lv_draw_image_dsc_t *draw_dsc = (lv_draw_image_dsc_t *)lv_draw_task_get_draw_dsc(task);
        const lv_image_dsc_t *src_img = (const lv_image_dsc_t *)draw_dsc->src;
        lv_layer_t *layer = task->target_layer;

        if (src_img && src_img->data && layer && layer->draw_buf) {
            int32_t x1 = task->area.x1;
            int32_t y1 = task->area.y1;
            int32_t x2 = task->area.x2;
            int32_t y2 = task->area.y2;

            int32_t draw_w = x2 - x1 + 1;
            int32_t draw_h = y2 - y1 + 1;

            if (draw_w > 0 && draw_h > 0) {
                uint8_t *dst_base = (uint8_t *)layer->draw_buf->data;
                uint8_t *src_base = (uint8_t *)src_img->data;

                uint32_t dst_stride = layer->draw_buf->header.stride;
                uint32_t src_stride = src_img->header.stride;

                int32_t dst_offset_x = x1 - layer->buf_area.x1;
                int32_t dst_offset_y = y1 - layer->buf_area.y1;

                // Copy พิกเซลแถวต่อแถวแบบตรงไปตรงมา ไม่ผ่านการ Blend สี
                for (int32_t y = 0; y < draw_h; y++) {
                    uint8_t *dst_row = dst_base + ((dst_offset_y + y) * dst_stride) + (dst_offset_x * 2);
                    uint8_t *src_row = src_base + (y * src_stride);
                    memcpy(dst_row, src_row, draw_w * 2); // 2 ไบต์ต่อพิกเซล (RGB565)
                }

                // บอก LVGL ว่างานนี้วาดเสร็จสมบูรณ์แล้ว ไม่ต้องรัน lv_draw_sw ต่อ
                task->state = LV_DRAW_TASK_STATE_FINISHED;
            }
        }
    }
}

lv_obj_t *vd_avi_create(lv_obj_t *parent) {
    if (!parent) parent = lv_screen_active();

    vd_avi_player_t *player = (vd_avi_player_t *)calloc(1, sizeof(vd_avi_player_t));
    if (!player) {
        ESP_LOGE(TAG, "Alloc vd_avi_player_t failed");
        return NULL;
    }

    // 1. ติดตั้ง Hardware JPEG Decoder Engine ของ ESP32-P4
    jpeg_decode_engine_cfg_t eng_cfg = {
        .intr_priority = 0,
        .timeout_ms = 500,
    };
    if (jpeg_new_decoder_engine(&eng_cfg, &player->jpeg_dec) == ESP_OK) {
        player->jpeg_dec_inited = true;
    } else {
        ESP_LOGE(TAG, "Init HW JPEG Decoder Engine failed");
        free(player);
        return NULL;
    }

    // 2. สร้าง RingBuffer สำหรับ Raw Video Chunks บน PSRAM (512 KB)
    player->raw_video_rb = xRingbufferCreateWithCaps(RAW_VIDEO_RB_SIZE, RINGBUF_TYPE_NOSPLIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!player->raw_video_rb) {
        player->raw_video_rb = xRingbufferCreate(RAW_VIDEO_RB_SIZE, RINGBUF_TYPE_NOSPLIT);
    }
    if (!player->raw_video_rb) {
        ESP_LOGE(TAG, "Create raw video ringbuffer failed");
        jpeg_del_decoder_engine(player->jpeg_dec);
        free(player);
        return NULL;
    }

    // 3. สตาร์ต Worker Task แยกสำหรับถอดรหัสภาพ
    player->decode_task_run = true;
    BaseType_t task_ret = xTaskCreatePinnedToCore(_avi_video_decode_task, "avi_vdec", 8192, player, 4, &player->decode_task_hdl, 0);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Create video decode task failed");
        vRingbufferDelete(player->raw_video_rb);
        jpeg_del_decoder_engine(player->jpeg_dec);
        free(player);
        return NULL;
    }

    // 4. เริ่มต้นตัวอ่านไฟล์ AVI
    esp32_avidec_cfg_t avi_cfg = ESP32_AVIDEC_CONFIG_DEFAULT();
    avi_cfg.user_ctx = player;
    if (esp32_avidec_init(&player->avidec, &avi_cfg) == ESP32_AVIDEC_OK) {
        player->avidec_inited = true;
        esp32_avidec_set_video_cb(&player->avidec, _avi_video_frame_cb);
        esp32_avidec_set_audio_cb(&player->avidec, _avi_audio_chunk_cb);
    } else {
        ESP_LOGE(TAG, "Init esp32_avidec failed");
        player->decode_task_run = false;
        vRingbufferDelete(player->raw_video_rb);
        jpeg_del_decoder_engine(player->jpeg_dec);
        free(player);
        return NULL;
    }

    // 5. สร้าง LVGL Image Widget เป็นตัวแทนแสดงผล
    player->img_obj = lv_image_create(parent);
    lv_obj_set_user_data(player->img_obj, player);
    lv_obj_add_event_cb(player->img_obj, _avi_delete_event_cb, LV_EVENT_DELETE, player);
    lv_obj_add_event_cb(player->img_obj, _avi_cover_check_cb, LV_EVENT_COVER_CHECK, NULL);
    lv_obj_add_event_cb(player->img_obj, _avi_draw_task_filter_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);

    // ปรับสไตล์ให้เป็น Fast-Path
    lv_obj_set_scrollable(player->img_obj, false);
    lv_obj_set_clickable(player->img_obj, false);
    lv_obj_set_style_bg_opa(player->img_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_image_opa(player->img_obj, LV_OPA_COVER, 0);
    lv_image_set_antialias(player->img_obj, false);

    lv_obj_set_style_radius(player->img_obj, 0, 0);
    lv_obj_set_style_border_width(player->img_obj, 0, 0);
    lv_obj_set_style_pad_all(player->img_obj, 0, 0);
    lv_obj_set_style_shadow_width(player->img_obj, 0, 0);

    return player->img_obj;
}

esp_err_t vd_avi_set_src(lv_obj_t *obj, const char *path) {
    vd_avi_player_t *player = vd_avi_get_player(obj);
    if (!player || !path) return ESP_ERR_INVALID_ARG;

    // หยุดไฟล์เดิมและล้างคิวภาพเก่า
    esp32_avidec_stop(&player->avidec);
    _flush_raw_video_queue(player);

    // เปิดไฟล์ AVI
    if (esp32_avidec_set_file(&player->avidec, path) != ESP32_AVIDEC_OK) {
        ESP_LOGE(TAG, "Open AVI failed: %s", path);
        return ESP_FAIL;
    }

    // คำนวณขนาด 16-pixel alignment สำหรับฮาร์ดแวร์ ESP32-P4
    uint32_t w = player->avidec.width;
    uint32_t h = player->avidec.height;
    player->aligned_w = (w + 15) & ~15;
    player->aligned_h = (h + 15) & ~15;
    size_t new_fb_size = player->aligned_w * player->aligned_h * 2; // RGB565 = 2 bytes/px

    // จัดสรร Framebuffer แบบ Ping-Pong ใน PSRAM (aligned 64 bytes)
    if (player->fb_size != new_fb_size) {
        if (player->fb[0]) heap_caps_free(player->fb[0]);
        if (player->fb[1]) heap_caps_free(player->fb[1]);

        player->fb_size = new_fb_size;
        player->fb[0] = (uint8_t *)heap_caps_aligned_alloc(64, player->fb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        player->fb[1] = (uint8_t *)heap_caps_aligned_alloc(64, player->fb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

        if (!player->fb[0] || !player->fb[1]) {
            ESP_LOGE(TAG, "Alloc PSRAM video framebuffers failed (%u bytes each)", (unsigned)player->fb_size);
            return ESP_ERR_NO_MEM;
        }

        memset(player->fb[0], 0, player->fb_size);
        memset(player->fb[1], 0, player->fb_size);
    }

    // กำหนดค่า Descriptor ให้กับ LVGL
    player->cur_fb_idx = 0;
    player->img_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    player->img_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    player->img_dsc.header.w      = player->aligned_w;
    player->img_dsc.header.h      = player->aligned_h;
    player->img_dsc.header.stride = player->aligned_w * 2;
    player->img_dsc.data_size     = player->fb_size;
    player->img_dsc.data          = player->fb[0];

    if (lvgl_port_lock(100)) {
        lv_image_set_src(player->img_obj, &player->img_dsc);
        lv_obj_set_size(player->img_obj, player->aligned_w, player->aligned_h);
        lvgl_port_unlock();
    }

    return ESP_OK;
}

esp_err_t vd_avi_play(lv_obj_t *obj) {
    vd_avi_player_t *player = vd_avi_get_player(obj);
    if (!player) return ESP_ERR_INVALID_ARG;
    return (esp32_avidec_play(&player->avidec) == ESP32_AVIDEC_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t vd_avi_pause(lv_obj_t *obj) {
    vd_avi_player_t *player = vd_avi_get_player(obj);
    if (!player) return ESP_ERR_INVALID_ARG;
    return (esp32_avidec_pause(&player->avidec) == ESP32_AVIDEC_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t vd_avi_stop(lv_obj_t *obj) {
    vd_avi_player_t *player = vd_avi_get_player(obj);
    if (!player) return ESP_ERR_INVALID_ARG;
    _flush_raw_video_queue(player);
    return (esp32_avidec_stop(&player->avidec) == ESP32_AVIDEC_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t vd_avi_seek(lv_obj_t *obj, uint64_t sec) {
    vd_avi_player_t *player = vd_avi_get_player(obj);
    if (!player) return ESP_ERR_INVALID_ARG;
    _flush_raw_video_queue(player);
    return (esp32_avidec_seek(&player->avidec, (uint32_t)sec) == ESP32_AVIDEC_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t vd_avi_seek_start(lv_obj_t *obj) {
    return vd_avi_seek(obj, 0);
}

esp_err_t vd_avi_delete(lv_obj_t *obj) {
    if (!obj) return ESP_OK;
    lv_obj_delete_async(obj);
    return ESP_OK;
}

esp_err_t vd_avi_set_audio_cb(lv_obj_t *obj, esp32_avidec_audio_cb_t cb, void *user_ctx) {
    vd_avi_player_t *player = vd_avi_get_player(obj);
    if (!player) return ESP_ERR_INVALID_ARG;
    player->audio_cb = cb;
    player->audio_ctx = user_ctx;
    return ESP_OK;
}

vd_avi_player_t *vd_avi_get_player(lv_obj_t *obj) {
    if (!obj || !lv_obj_is_valid(obj)) return NULL;
    return (vd_avi_player_t *)lv_obj_get_user_data(obj);
}

uint32_t vd_avi_get_current_sec(lv_obj_t *obj) {
    vd_avi_player_t *player = vd_avi_get_player(obj);
    if (!player || !player->avidec_inited) return 0;
    return esp32_avidec_get_current_sec(&player->avidec);
}

uint32_t vd_avi_get_total_sec(lv_obj_t *obj) {
    vd_avi_player_t *player = vd_avi_get_player(obj);
    if (!player || !player->avidec_inited) return 0;
    return esp32_avidec_get_total_sec(&player->avidec);
}

bool vd_avi_is_playing(lv_obj_t *obj) {
    vd_avi_player_t *player = vd_avi_get_player(obj);
    if (!player || !player->avidec_inited) return false;
    return esp32_avidec_get_state(&player->avidec) == ESP32_AVIDEC_STATE_PLAYING;
}