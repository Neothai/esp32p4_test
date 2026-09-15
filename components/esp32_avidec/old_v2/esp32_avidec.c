#include "esp32_avidec.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <string.h>

#define TAG "ESP32_AVIDEC"

static void _build_block_map(esp32_avidec_t *a) {
    a->block_cnt = 0;
    if (!a->avi || a->avi->file_size < 12) return;

    // 1. บล็อก 0 ดึงจาก avilib ที่สแกนไว้ถูกต้องแล้วแน่นอน 100%
    if (a->avi->movi_start > 0) {
        a->blocks[0].movi_start = a->avi->movi_start;
        a->blocks[0].movi_end   = a->avi->movi_end;
        a->block_cnt = 1;
    } else {
        ESP_LOGE(TAG, "Base AVI header not parsed properly!");
        return;
    }

    // 2. เริ่มต้นจาก RIFF ก้อนแรกที่ตำแหน่ง 0 ของไฟล์
    avi_off_t riff_pos = 0;

    while (a->block_cnt < ESP32_AVIDEC_MAX_BLOCKS) {
        uint8_t h[12];
        UINT br = 0;
        if (f_lseek(&a->avi->fil, (FSIZE_t)riff_pos) != FR_OK) break;
        if (f_read(&a->avi->fil, h, 12, &br) != FR_OK || br < 12) break;

        uint32_t fcc_riff = (uint32_t)h[0] | ((uint32_t)h[1] << 8) | ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24);
        uint32_t riff_sz  = (uint32_t)h[4] | ((uint32_t)h[5] << 8) | ((uint32_t)h[6] << 16) | ((uint32_t)h[7] << 24);
        uint32_t fcc_type = (uint32_t)h[8] | ((uint32_t)h[9] << 8) | ((uint32_t)h[10] << 16) | ((uint32_t)h[11] << 24);

        if (fcc_riff != 0x46464952) { // ไม่ใช่ 'RIFF' จบการค้นหาทันที
            break;
        }

        // ถ้าเป็นก้อนขยาย OpenDML (RIFF AVIX) ก้อนที่ 2, 3, 4...
        if (fcc_type == 0x58495641 && a->block_cnt > 0) { // 'AVIX'
            uint8_t lh[12];
            if (f_lseek(&a->avi->fil, (FSIZE_t)(riff_pos + 12)) == FR_OK &&
                f_read(&a->avi->fil, lh, 12, &br) == FR_OK && br == 12) {
                uint32_t lid   = (uint32_t)lh[0] | ((uint32_t)lh[1] << 8) | ((uint32_t)lh[2] << 16) | ((uint32_t)lh[3] << 24);
                uint32_t lsz   = (uint32_t)lh[4] | ((uint32_t)lh[5] << 8) | ((uint32_t)lh[6] << 16) | ((uint32_t)lh[7] << 24);
                uint32_t ltype = (uint32_t)lh[8] | ((uint32_t)lh[9] << 8) | ((uint32_t)lh[10] << 16) | ((uint32_t)lh[11] << 24);

                if (lid == 0x5453494C && ltype == 0x69766F6D) { // 'LIST' && 'movi'
                    a->blocks[a->block_cnt].movi_start = riff_pos + 24;
                    a->blocks[a->block_cnt].movi_end   = riff_pos + 20 + lsz;
                    if (a->blocks[a->block_cnt].movi_end > a->avi->file_size) {
                        a->blocks[a->block_cnt].movi_end = a->avi->file_size;
                    }
                    a->block_cnt++;
                }
            }
        }

        // คำนวณตำแหน่ง RIFF ก้อนถัดไป โดยกระโดดข้ามตาม riff_sz ทันที (O(1) ไม่ต้องสแกน)
        avi_off_t next_riff = riff_pos + 8 + ((riff_sz + 1u) & ~1u);
        if (next_riff + 12 > a->avi->file_size) {
            break;
        }

        // ตรวจสอบว่าที่ next_riff เป็น 'RIFF' หรือไม่
        if (f_lseek(&a->avi->fil, (FSIZE_t)next_riff) == FR_OK &&
            f_read(&a->avi->fil, h, 4, &br) == FR_OK && br == 4 &&
            ((uint32_t)h[0] | ((uint32_t)h[1] << 8) | ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24)) == 0x46464952) {
            riff_pos = next_riff;
        } else {
            // สำรองกรณี Muxer จัด Sector Align 512 ไบต์
            avi_off_t aligned_512 = (next_riff + 511ULL) & ~511ULL;
            if (aligned_512 + 12 <= a->avi->file_size &&
                f_lseek(&a->avi->fil, (FSIZE_t)aligned_512) == FR_OK &&
                f_read(&a->avi->fil, h, 4, &br) == FR_OK && br == 4 &&
                ((uint32_t)h[0] | ((uint32_t)h[1] << 8) | ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24)) == 0x46464952) {
                riff_pos = aligned_512;
            } else {
                break; // ไม่มีก้อน RIFF อื่นแล้ว จบการทำงาน
            }
        }
    }

    // 3. รีเซ็ตตำแหน่งหัวอ่านของ FatFS และ avilib กลับมาจุดเริ่มต้นของวิดีโอ
    f_lseek(&a->avi->fil, (FSIZE_t)a->avi->movi_start);
    a->avi->pos       = a->avi->movi_start;
    a->avi->buf_off   = a->avi->movi_start;
    a->avi->buf_len   = 0;
    a->avi->video_pos = 0;

    ESP_LOGI(TAG, "OpenDML Block Map ready: %d block(s) registered", a->block_cnt);
}

static bool _exec_fast_seek(esp32_avidec_t *a, uint32_t target_sec) {
    if (!a->avi || a->block_cnt == 0 || a->fps <= 0) return false;

    double total_sec = (double)a->total_frames / a->fps;
    if (total_sec > 0 && target_sec >= (uint32_t)total_sec) {
        target_sec = (uint32_t)total_sec - 1;
    }

    uint64_t total_payload_bytes = 0;
    for (int i = 0; i < a->block_cnt; i++) {
        total_payload_bytes += (a->blocks[i].movi_end - a->blocks[i].movi_start);
    }
    if (total_payload_bytes == 0) return false;

    uint64_t target_stream_byte = (uint64_t)(((double)target_sec / total_sec) * (double)total_payload_bytes);

    int blk_idx = 0;
    uint64_t accum = 0;
    for (int i = 0; i < a->block_cnt; i++) {
        uint64_t blk_size = a->blocks[i].movi_end - a->blocks[i].movi_start;
        if (target_stream_byte <= accum + blk_size || i == a->block_cnt - 1) {
            blk_idx = i;
            break;
        }
        accum += blk_size;
    }

    uint64_t approx_pos = (a->blocks[blk_idx].movi_start + (target_stream_byte - accum)) & ~511ULL;
    if (approx_pos < a->blocks[blk_idx].movi_start) {
        approx_pos = a->blocks[blk_idx].movi_start;
    }

    // กวาดหา Signature "00dc" + JPEG SOI (0xFF 0xD8)
    int match_offset = -1;
    if (f_lseek(&a->avi->fil, (FSIZE_t)approx_pos) == FR_OK) {
        UINT br = 0;
        if (f_read(&a->avi->fil, a->vbuf, a->cfg.video_buf_size, &br) == FR_OK && br > 16) {
            for (size_t i = 0; i + 10 < br; i++) {
                if (a->vbuf[i] == '0' && a->vbuf[i+1] == '0' &&
                    (a->vbuf[i+2] == 'd' || a->vbuf[i+2] == 'D') &&
                    (a->vbuf[i+3] == 'c' || a->vbuf[i+3] == 'C' || a->vbuf[i+3] == 'b' || a->vbuf[i+3] == 'B')) {
                    if (a->vbuf[i + 8] == 0xFF && a->vbuf[i + 9] == 0xD8) {
                        match_offset = (int)i;
                        break;
                    }
                }
            }
        }
    }

    uint64_t exact_pos = (match_offset >= 0) ? (approx_pos + match_offset) : a->blocks[blk_idx].movi_start;

    // ซิงก์ตำแหน่งให้ avilib ทันที
    a->avi->movi_start = a->blocks[blk_idx].movi_start;
    a->avi->movi_end   = a->blocks[blk_idx].movi_end;
    a->avi->pos        = exact_pos;
    a->avi->buf_off    = exact_pos;
    a->avi->buf_len    = 0;
    f_lseek(&a->avi->fil, (FSIZE_t)exact_pos);

    a->avi->video_pos  = (long)(target_sec * a->fps);
    return true;
}

/*
static void _avidec_worker_task(void *pvParam) {
    esp32_avidec_t *a = (esp32_avidec_t *)pvParam;
    int64_t next_frame_time = 0;
    long chunk_len = 0;

    while (a->task_run) {
        // ประมวลผล Seek ทันที แม้อยู่ในสถานะ PAUSED
        if (a->seek_target_sec >= 0) {
            xSemaphoreTake(a->lock, portMAX_DELAY);
            uint32_t target = (uint32_t)a->seek_target_sec;
            a->seek_target_sec = -1;
            if (_exec_fast_seek(a, target)) {
                next_frame_time = esp_timer_get_time();
                if (a->event_cb) {
                    a->event_cb(ESP32_AVIDEC_EVENT_SEEK_DONE, (void *)(uintptr_t)target, a->cfg.user_ctx);
                }
            }
            xSemaphoreGive(a->lock);
        }

        if (a->state != ESP32_AVIDEC_STATE_PLAYING) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        xSemaphoreTake(a->lock, portMAX_DELAY);

        int ret = AVI_read_data(a->avi, (char *)a->vbuf, a->cfg.video_buf_size,
                                (char *)a->abuf, a->cfg.audio_buf_size, &chunk_len);

        if (ret == 0) { // จบไฟล์ (EOF)
            a->state = ESP32_AVIDEC_STATE_STOPPED;
            if (a->event_cb) {
                a->event_cb(ESP32_AVIDEC_EVENT_EOF, NULL, a->cfg.user_ctx);
                a->event_cb(ESP32_AVIDEC_EVENT_STATE_CHANGED, (void *)(uintptr_t)a->state, a->cfg.user_ctx);
            }
            xSemaphoreGive(a->lock);
            continue;
        }

        if (ret == 1 && chunk_len > 4) { // Video Chunk
            // สแกนหาจุดเริ่ม JPEG SOI (0xFFD8)
            size_t offset = 0;
            while (offset + 1 < (size_t)chunk_len) {
                if (a->vbuf[offset] == 0xFF && a->vbuf[offset + 1] == 0xD8) break;
                offset++;
            }

            // ถ้าเจอมาร์กเกอร์ ส่งเฉพาะก้อนภาพจริงเข้า Callback
            if (offset + 1 < (size_t)chunk_len && a->video_cb) {
                a->video_cb(a->vbuf + offset, (size_t)chunk_len - offset, 
                            (uint32_t)a->avi->video_pos, a->cfg.user_ctx);
            }

            if (a->video_cb) {
                a->video_cb(a->vbuf, (size_t)chunk_len, (uint32_t)a->avi->video_pos, a->cfg.user_ctx);
            }

            // จัดการเวลา Frame Pacing
            next_frame_time += a->frame_interval_us;
            int64_t now = esp_timer_get_time();
            if (next_frame_time > now) {
                int64_t delay_ms = (next_frame_time - now) / 1000;
                if (delay_ms > 0) {
                    xSemaphoreGive(a->lock);
                    vTaskDelay(pdMS_TO_TICKS(delay_ms));
                    continue;
                }
            } else {
                next_frame_time = now;
            }
        } else if (ret == 2 && chunk_len > 0) { // Audio Chunk
            if (a->audio_cb) {
                a->audio_cb(a->abuf, (size_t)chunk_len, a->cfg.user_ctx);
            }
        }

        xSemaphoreGive(a->lock);
    }

    a->task_hdl = NULL;
    vTaskDelete(NULL);
}*/

static void _avidec_worker_task(void *pvParam) {
    esp32_avidec_t *a = (esp32_avidec_t *)pvParam;
    int64_t next_frame_time = 0;
    long chunk_len = 0;

    while (a->task_run) {
        // ประมวลผล Seek ทันที แม้อยู่ในสถานะ PAUSED
        if (a->seek_target_sec >= 0) {
            xSemaphoreTake(a->lock, portMAX_DELAY);
            uint32_t target = (uint32_t)a->seek_target_sec;
            a->seek_target_sec = -1;
            if (_exec_fast_seek(a, target)) {
                next_frame_time = esp_timer_get_time();
                if (a->event_cb) {
                    a->event_cb(ESP32_AVIDEC_EVENT_SEEK_DONE, (void *)(uintptr_t)target, a->cfg.user_ctx);
                }
            }
            xSemaphoreGive(a->lock);
        }

        if (a->state != ESP32_AVIDEC_STATE_PLAYING) {
            next_frame_time = 0; // [แก้ไข] รีเซ็ตฐานเวลาเมื่อพัก/หยุดเล่น
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        xSemaphoreTake(a->lock, portMAX_DELAY);

        int ret = AVI_read_data(a->avi, (char *)a->vbuf, a->cfg.video_buf_size,
                                (char *)a->abuf, a->cfg.audio_buf_size, &chunk_len);

        if (ret == 0) { // จบไฟล์ (EOF)
            a->state = ESP32_AVIDEC_STATE_STOPPED;
            if (a->event_cb) {
                a->event_cb(ESP32_AVIDEC_EVENT_EOF, NULL, a->cfg.user_ctx);
                a->event_cb(ESP32_AVIDEC_EVENT_STATE_CHANGED, (void *)(uintptr_t)a->state, a->cfg.user_ctx);
            }
            xSemaphoreGive(a->lock);
            continue;
        }

        if (ret == 1 && chunk_len > 4) { // Video Chunk
            // [แก้ไข] สแกนหาจุดเริ่ม JPEG SOI (0xFFD8) ภายในไลบรารี ให้ Callback สะอาด
            size_t offset = 0;
            while (offset + 1 < (size_t)chunk_len) {
                if (a->vbuf[offset] == 0xFF && a->vbuf[offset + 1] == 0xD8) break;
                offset++;
            }

            if (offset + 1 < (size_t)chunk_len && a->video_cb) {
                a->video_cb(a->vbuf + offset, (size_t)chunk_len - offset, (uint32_t)a->avi->video_pos, a->cfg.user_ctx);
            }

            // ── [แก้ไขจุดสำคัญ] Frame Pacing รักษาเส้นเวลาสะสม ──
            if (next_frame_time == 0) {
                next_frame_time = esp_timer_get_time();
            }
            next_frame_time += a->frame_interval_us;
            int64_t now = esp_timer_get_time();
            int64_t diff = next_frame_time - now;

            if (diff > 0) {
                int64_t delay_ms = diff / 1000;
                if (delay_ms > 0) {
                    xSemaphoreGive(a->lock);
                    vTaskDelay(pdMS_TO_TICKS(delay_ms));
                    continue;
                }
            } else if (diff < -100000) {
                // หลุดจังหวะสะสมเกิน 100ms (3 เฟรม) เช่น ตอน Seek หรือ SD ชะงักหนัก ค่อย Resync ใหม่
                next_frame_time = now;
            }
            // ถ้า diff ติดลบเล็กน้อย (-1 ถึง -99ms) ระบบจะไม่สั่ง Delay
            // เพื่อให้อ่านก้อนถัดไปทันที เป็นการจ่ายหนี้เวลาและป้อนก้อนเสียงชดเชยทันที
        } else if (ret == 2 && chunk_len > 0) { // Audio Chunk
            if (a->audio_cb) {
                a->audio_cb(a->abuf, (size_t)chunk_len, a->cfg.user_ctx);
            }
        }

        xSemaphoreGive(a->lock);
    }

    a->task_hdl = NULL;
    vTaskDelete(NULL);
}

esp32_avidec_err_t esp32_avidec_init(esp32_avidec_t *avi, const esp32_avidec_cfg_t *cfg) {
    if (!avi) return ESP32_AVIDEC_ERR_INVALID_ARG;
    memset(avi, 0, sizeof(esp32_avidec_t));

    if (cfg) avi->cfg = *cfg;
    else {
        esp32_avidec_cfg_t def = ESP32_AVIDEC_CONFIG_DEFAULT();
        avi->cfg = def;
    }

    avi->lock = xSemaphoreCreateMutex();
    if (!avi->lock) return ESP32_AVIDEC_ERR_NO_MEM;

    avi->seek_target_sec = -1;
    avi->state = ESP32_AVIDEC_STATE_IDLE;
    avi->task_run = true;

    BaseType_t res = xTaskCreatePinnedToCore(
        _avidec_worker_task, "avi_worker", avi->cfg.task_stack, (void *)avi,
        avi->cfg.task_priority, &avi->task_hdl, avi->cfg.task_core_id
    );

    return (res == pdPASS) ? ESP32_AVIDEC_OK : ESP32_AVIDEC_ERR_FAIL;
}

esp32_avidec_err_t esp32_avidec_set_file(esp32_avidec_t *avi, const char *filepath) {
    if (!avi || !filepath) return ESP32_AVIDEC_ERR_INVALID_ARG;
    xSemaphoreTake(avi->lock, portMAX_DELAY);

    // 1. คืนทรัพยากรเดิมหากเคยเปิดไฟล์ค้างไว้
    if (avi->avi) {
        AVI_close(avi->avi);
        avi->avi = NULL;
    }
    if (avi->vbuf) { free(avi->vbuf); avi->vbuf = NULL; }
    if (avi->abuf) { free(avi->abuf); avi->abuf = NULL; }

    avilib_config_t acfg = {
        .buffer_size = avi->cfg.read_cache_size,
        .use_psram   = true
    };

    avi->avi = AVI_open_input_file_ex(filepath, 0, &acfg);
    if (!avi->avi) {
        xSemaphoreGive(avi->lock);
        return ESP32_AVIDEC_ERR_FAIL;
    }

    avi->width        = AVI_video_width(avi->avi);
    avi->height       = AVI_video_height(avi->avi);
    avi->fps          = AVI_frame_rate(avi->avi);
    avi->total_frames = AVI_video_frames(avi->avi);
    avi->frame_interval_us = (avi->fps > 0) ? (int64_t)(1000000.0 / avi->fps) : 33333;

    // 2. คำนวณขนาด Video Buffer จาก suggestedBufferSize พร้อม Headroom
    uint32_t v_suggested = AVI_video_suggested_buffer_size(avi->avi);
    ESP_LOGI(TAG, "Video Suggested Buf Size: %lu Bytes", v_suggested);

    size_t actual_vbuf_sz = 0;

    if (v_suggested > 0) {
        actual_vbuf_sz = (size_t)v_suggested + 4096; // เพิ่ม Margin 4 KB ป้องกัน Overflow
    } else {
        // Fallback: หากไฟล์ไม่ได้ใส่ค่ามา ให้ใช้ค่า Default จาก Config (เช่น 128 KB)
        actual_vbuf_sz = avi->cfg.video_buf_size ? avi->cfg.video_buf_size : (128 * 1024);
    }
    avi->cfg.video_buf_size = actual_vbuf_sz;

    ESP_LOGI(TAG, "Actual allocated video Buf Size: %lu Bytes", actual_vbuf_sz);

    // 3. คำนวณขนาด Audio Buffer จาก suggestedBufferSize
    uint32_t a_suggested = AVI_audio_suggested_buffer_size(avi->avi);
    size_t actual_abuf_sz = (a_suggested > 0) ? (size_t)(a_suggested + 512) : avi->cfg.audio_buf_size;
    if (actual_abuf_sz < 4096) actual_abuf_sz = 4096; // ค่าต่ำสุดสำหรับ 1 MP3 Frame
    avi->cfg.audio_buf_size = actual_abuf_sz;

    ESP_LOGI(TAG, "Audio Suggested Buf Size: %lu Bytes", a_suggested);
    ESP_LOGI(TAG, "Actual allocated audio Buf Size: %lu Bytes", actual_abuf_sz);

    // 4. ทำการจัดสรรหน่วยความจำจริงตามขนาดที่คำนวณได้
    avi->vbuf = (uint8_t *)heap_caps_aligned_alloc(64, avi->cfg.video_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!avi->vbuf) {
        // หาก Internal RAM เต็ม ให้ขยับไปจองบน PSRAM แทน
        avi->vbuf = (uint8_t *)heap_caps_aligned_alloc(64, avi->cfg.video_buf_size, MALLOC_CAP_SPIRAM);
    }
    avi->abuf = (uint8_t *)malloc(avi->cfg.audio_buf_size);

    if (!avi->vbuf || !avi->abuf) {
        ESP_LOGI(TAG, "Fail to allocate memory for video or audio; terminate!");
        AVI_close(avi->avi);
        avi->avi = NULL;
        if (avi->vbuf) { free(avi->vbuf); avi->vbuf = NULL; }
        if (avi->abuf) { free(avi->abuf); avi->abuf = NULL; }
        xSemaphoreGive(avi->lock);
        return ESP32_AVIDEC_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Allocated optimal buffers -> Video: %u KB, Audio: %u B", 
             (unsigned)(avi->cfg.video_buf_size / 1024), (unsigned)avi->cfg.audio_buf_size);

    _build_block_map(avi);

    avi->state = ESP32_AVIDEC_STATE_LOADED;
    if (avi->event_cb) {
        avi->event_cb(ESP32_AVIDEC_EVENT_STATE_CHANGED, (void *)(uintptr_t)avi->state, avi->cfg.user_ctx);
    }

    xSemaphoreGive(avi->lock);
    return ESP32_AVIDEC_OK;
}

esp32_avidec_err_t esp32_avidec_set_video_cb(esp32_avidec_t *avi, esp32_avidec_video_cb_t cb) {
    if (!avi) return ESP32_AVIDEC_ERR_INVALID_ARG;
    avi->video_cb = cb;
    return ESP32_AVIDEC_OK;
}

esp32_avidec_err_t esp32_avidec_set_audio_cb(esp32_avidec_t *avi, esp32_avidec_audio_cb_t cb) {
    if (!avi) return ESP32_AVIDEC_ERR_INVALID_ARG;
    avi->audio_cb = cb;
    return ESP32_AVIDEC_OK;
}

esp32_avidec_err_t esp32_avidec_set_event_cb(esp32_avidec_t *avi, esp32_avidec_event_cb_t cb) {
    if (!avi) return ESP32_AVIDEC_ERR_INVALID_ARG;
    avi->event_cb = cb;
    return ESP32_AVIDEC_OK;
}

esp32_avidec_err_t esp32_avidec_play(esp32_avidec_t *avi) {
    if (!avi || !avi->avi) return ESP32_AVIDEC_ERR_INVALID_STATE;
    if (avi->state == ESP32_AVIDEC_STATE_PLAYING) return ESP32_AVIDEC_OK;

    xSemaphoreTake(avi->lock, portMAX_DELAY);
    avi->state = ESP32_AVIDEC_STATE_PLAYING;
    if (avi->event_cb) {
        avi->event_cb(ESP32_AVIDEC_EVENT_STATE_CHANGED, (void *)(uintptr_t)avi->state, avi->cfg.user_ctx);
    }
    xSemaphoreGive(avi->lock);
    return ESP32_AVIDEC_OK;
}

esp32_avidec_err_t esp32_avidec_pause(esp32_avidec_t *avi) {
    if (!avi) return ESP32_AVIDEC_ERR_INVALID_STATE;
    if (avi->state == ESP32_AVIDEC_STATE_PAUSED) return ESP32_AVIDEC_OK;

    xSemaphoreTake(avi->lock, portMAX_DELAY);
    avi->state = ESP32_AVIDEC_STATE_PAUSED;
    if (avi->event_cb) {
        avi->event_cb(ESP32_AVIDEC_EVENT_STATE_CHANGED, (void *)(uintptr_t)avi->state, avi->cfg.user_ctx);
    }
    xSemaphoreGive(avi->lock);
    return ESP32_AVIDEC_OK;
}

esp32_avidec_err_t esp32_avidec_resume(esp32_avidec_t *avi) {
    return esp32_avidec_play(avi);
}

esp32_avidec_err_t esp32_avidec_stop(esp32_avidec_t *avi) {
    if (!avi || !avi->avi) return ESP32_AVIDEC_ERR_INVALID_STATE;
    if (avi->state == ESP32_AVIDEC_STATE_STOPPED) return ESP32_AVIDEC_OK;

    xSemaphoreTake(avi->lock, portMAX_DELAY);
    avi->state = ESP32_AVIDEC_STATE_STOPPED;
    AVI_seek_start(avi->avi);
    if (avi->event_cb) {
        avi->event_cb(ESP32_AVIDEC_EVENT_STATE_CHANGED, (void *)(uintptr_t)avi->state, avi->cfg.user_ctx);
    }
    xSemaphoreGive(avi->lock);
    return ESP32_AVIDEC_OK;
}

esp32_avidec_err_t esp32_avidec_seek(esp32_avidec_t *avi, uint32_t target_sec) {
    if (!avi || !avi->avi) return ESP32_AVIDEC_ERR_INVALID_STATE;
    avi->seek_target_sec = target_sec;
    return ESP32_AVIDEC_OK;
}

esp32_avidec_err_t esp32_avidec_destroy(esp32_avidec_t *avi) {
    if (!avi) return ESP32_AVIDEC_ERR_INVALID_ARG;
    esp32_avidec_stop(avi);

    xSemaphoreTake(avi->lock, portMAX_DELAY);
    if (avi->avi) {
        AVI_close(avi->avi);
        avi->avi = NULL;
    }
    avi->block_cnt = 0;
    avi->state = ESP32_AVIDEC_STATE_IDLE;
    if (avi->event_cb) {
        avi->event_cb(ESP32_AVIDEC_EVENT_STATE_CHANGED, (void *)(uintptr_t)avi->state, avi->cfg.user_ctx);
    }
    xSemaphoreGive(avi->lock);

    return ESP32_AVIDEC_OK;
}

esp32_avidec_err_t esp32_avidec_deinit(esp32_avidec_t *avi) {
    if (!avi) return ESP32_AVIDEC_ERR_INVALID_ARG;
    esp32_avidec_destroy(avi);

    avi->task_run = false;
    int timeout_ms = 400;
    while (avi->task_hdl != NULL && timeout_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout_ms -= 10;
    }
    if (avi->task_hdl != NULL) {
        vTaskDelete(avi->task_hdl);
        avi->task_hdl = NULL;
    }

    if (avi->vbuf) { free(avi->vbuf); avi->vbuf = NULL; }
    if (avi->abuf) { free(avi->abuf); avi->abuf = NULL; }
    if (avi->lock) { vSemaphoreDelete(avi->lock); avi->lock = NULL; }

    return ESP32_AVIDEC_OK;
}

uint32_t esp32_avidec_get_current_sec(esp32_avidec_t *avi) {
    if (!avi || !avi->avi || avi->fps <= 0) return 0;
    return (uint32_t)(avi->avi->video_pos / avi->fps);
}

uint32_t esp32_avidec_get_total_sec(esp32_avidec_t *avi) {
    if (!avi || avi->fps <= 0) return 0;
    return (uint32_t)(avi->total_frames / avi->fps);
}

esp32_avidec_state_t esp32_avidec_get_state(esp32_avidec_t *avi) {
    return avi ? avi->state : ESP32_AVIDEC_STATE_IDLE;
}