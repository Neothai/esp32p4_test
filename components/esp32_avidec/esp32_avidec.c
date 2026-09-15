#include "esp32_avidec.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "esp_rom_sys.h"

#define TAG "ESP32_AVIDEC"

// สแกนผัง OpenDML แบบ O(1) กระโดดข้ามตามขนาด Header (ใช้เวลา < 2 ms ไม่ค้างแน่นอน)
static void _build_block_map(esp32_avidec_t *a) {
    a->block_cnt = 0;
    if (!a->avi || a->avi->file_size < 12) return;

    if (a->avi->movi_start > 0) {
        a->blocks[0].movi_start = a->avi->movi_start;
        a->blocks[0].movi_end   = a->avi->movi_end;
        a->block_cnt = 1;
    } else {
        ESP_LOGE(TAG, "Base AVI header not parsed properly!");
        return;
    }

    avi_off_t riff_pos = 0;

    while (a->block_cnt < ESP32_AVIDEC_MAX_BLOCKS) {
        uint8_t h[12];
        UINT br = 0;
        if (f_lseek(&a->avi->fil, (FSIZE_t)riff_pos) != FR_OK) break;
        if (f_read(&a->avi->fil, h, 12, &br) != FR_OK || br < 12) break;

        uint32_t fcc_riff = (uint32_t)h[0] | ((uint32_t)h[1] << 8) | ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24);
        uint32_t riff_sz  = (uint32_t)h[4] | ((uint32_t)h[5] << 8) | ((uint32_t)h[6] << 16) | ((uint32_t)h[7] << 24);
        uint32_t fcc_type = (uint32_t)h[8] | ((uint32_t)h[9] << 8) | ((uint32_t)h[10] << 16) | ((uint32_t)h[11] << 24);

        if (fcc_riff != 0x46464952) break; // ไม่ใช่ 'RIFF' จบการค้นหา

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

        // กระโดดข้ามก้อนตาม riff_sz โดยตรง (O(1)) ไม่วนสแกนทีละ Sector
        avi_off_t next_riff = riff_pos + 8 + ((riff_sz + 1u) & ~1u);
        if (next_riff + 12 > a->avi->file_size) break;

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
                break;
            }
        }
    }

    // [แก้] ลงทะเบียนผังบล็อกให้ avilib รู้จัก เพื่อให้ "ตัวอ่าน" ใช้ผังนี้
    //       แทนการเดาขอบเขตบล็อกจากฟิลด์ขนาด LIST (สาเหตุของอาการค้างกลางเรื่อง)
    avi_off_t last_end = 0;
    for (int i = 0; i < a->block_cnt; i++) {
        AVI_add_movi_block(a->avi, a->blocks[i].movi_start, a->blocks[i].movi_end);
        last_end = a->blocks[i].movi_end;
    }
    AVI_seek_start(a->avi);      /* ย้อนไปบล็อกแรกจริง (ไม่ใช่บล็อกสุดท้าย) */
    a->avi->video_pos = 0;
    a->next_frame_time = 0;
    a->dropped_frames  = 0;

    if (last_end && a->avi->file_size > last_end + (8 * 1024 * 1024)) {
        ESP_LOGW(TAG, "block map incomplete: %llu bytes unaccounted after last block",
                 (unsigned long long)(a->avi->file_size - last_end));
    }

    ESP_LOGI(TAG, "OpenDML block map: %d block(s), movi=[%llu,%llu) file=%llu",
             a->block_cnt, (unsigned long long)a->avi->movi_start,
             (unsigned long long)a->avi->movi_end,
             (unsigned long long)a->avi->file_size);
}

static bool _exec_fast_seek(esp32_avidec_t *a, uint32_t target_sec) {
    if (!a->avi || a->block_cnt == 0 || a->fps <= 0) return false;
    /* dwLength ของกล้องหลายรุ่นเป็น 0 -> ห้ามหารด้วยศูนย์ (เดิมได้ NaN/inf) */
    if (a->total_frames <= 0) {
        ESP_LOGW(TAG, "seek: file reports 0 frames, time-based seek unavailable");
        return false;
    }

    double total_sec = (double)a->total_frames / a->fps;
    if (target_sec >= (uint32_t)total_sec) {
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

    int match_offset = -1;
    /* [แก้] ใช้ tag จริงของไฟล์ (เดิม hardcode "00dc") */
    const uint8_t t0 = (uint8_t)a->avi->video_tag[0];
    const uint8_t t1 = (uint8_t)a->avi->video_tag[1];
    const uint8_t t2 = (uint8_t)a->avi->video_tag[2];
    const uint8_t t3 = (uint8_t)a->avi->video_tag[3];
    const uint8_t t3alt = (t3 == 'c') ? 'b' : ((t3 == 'b') ? 'c' : t3);
    /* [แก้] สแกนเท่าขนาดเฟรมที่ใหญ่สุดที่พบ (เดิมอ่าน 128 KB ทุกครั้งที่ seek) */
    uint32_t scan_len = (a->max_frame_seen ? a->max_frame_seen + 64 : (32 * 1024));
    if (scan_len > a->cfg.video_buf_size) scan_len = a->cfg.video_buf_size;
    if (f_lseek(&a->avi->fil, (FSIZE_t)approx_pos) == FR_OK) {
        UINT br = 0;
        if (f_read(&a->avi->fil, a->vbuf, scan_len, &br) == FR_OK && br > 16) {
            for (size_t i = 0; i + 10 < br; i++) {
                if (a->vbuf[i] == t0 && a->vbuf[i+1] == t1 && a->vbuf[i+2] == t2 &&
                    (a->vbuf[i+3] == t3 || a->vbuf[i+3] == t3alt)) {
                    uint32_t clen = (uint32_t)a->vbuf[i+4] | ((uint32_t)a->vbuf[i+5] << 8) |
                                    ((uint32_t)a->vbuf[i+6] << 16) | ((uint32_t)a->vbuf[i+7] << 24);
                    if (clen > a->max_frame_seen) a->max_frame_seen = clen;
                    if (a->vbuf[i + 8] == 0xFF && a->vbuf[i + 9] == 0xD8) {
                        match_offset = (int)i;
                        break;
                    }
                }
            }
        }
    }

    uint64_t exact_pos = (match_offset >= 0) ? (approx_pos + match_offset) : a->blocks[blk_idx].movi_start;

    /* [แก้] ผ่าน API ของ avilib เพื่อรักษา invariant ของบัฟเฟอร์
             (FIL ptr ต้องเท่ากับ buf_off + buf_len เสมอ) */
    AVI_set_movi_region(a->avi, a->blocks[blk_idx].movi_start,
                                 a->blocks[blk_idx].movi_end);
    a->avi->pos     = exact_pos;
    a->avi->buf_off = exact_pos;
    a->avi->buf_len = 0;
    f_lseek(&a->avi->fil, (FSIZE_t)exact_pos);

    a->avi->video_pos  = (long)(target_sec * a->fps);
    a->next_frame_time = 0;     /* เริ่มนับจังหวะใหม่หลัง seek */
    a->dropped_frames  = 0;
    return true;
}

/* ------------------------------------------------------------------ */
/* ขยายบัฟเฟอร์ "ตามขนาดจริงของ chunk ที่เจอ" (ไม่ต้องเดาขนาดล่วงหน้า)  */
/* - ใช้ heap_caps_aligned_alloc ใหม่ (คง alignment 64 ไบต์สำหรับ DMA)   */
/* - คัดลอกข้อมูลเดิมไปด้วย (ปลอดภัยกับ pointer ที่ถูกอ้างอยู่)         */
/* ------------------------------------------------------------------ */
static bool _grow_buf(uint8_t **pbuf, size_t *pcap, size_t want)
{
    if (want <= *pcap)
        return true;
    want = (want + 63u) & ~(size_t)63u;         /* คงแนว 64 ไบต์ */

    uint8_t *np = (uint8_t *)heap_caps_aligned_alloc(64, want, MALLOC_CAP_SPIRAM);
    if (!np)
        np = (uint8_t *)heap_caps_aligned_alloc(64, want, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!np)
        return false;                            /* จองไม่ได้ -> ผู้เรียกจะแจ้ง ERROR */

    memcpy(np, *pbuf, *pcap);                    /* เก็บข้อมูลเดิมไว้ (chunk ที่อ่านค้าง) */
    free(*pbuf);
    *pbuf = np;
    *pcap = want;
    return true;
}

static void _avidec_worker_task(void *pvParam) {
    esp32_avidec_t *a = (esp32_avidec_t *)pvParam;
    long chunk_len = 0;

    while (a->task_run) {
        // ประมวลผล Seek ทันที
        // [แก้] อ่าน-และ-ล้างค่าภายใต้ critical section (กัน lost seek / torn read)
        int64_t want;
        portENTER_CRITICAL(&a->seek_mux);
        want = a->seek_target_sec;
        a->seek_target_sec = -1;
        portEXIT_CRITICAL(&a->seek_mux);

        if (want >= 0) {
            xSemaphoreTake(a->lock, portMAX_DELAY);
            uint32_t target = (uint32_t)want;
            if (_exec_fast_seek(a, target)) {
                if (a->event_cb) {
                    a->event_cb(ESP32_AVIDEC_EVENT_SEEK_DONE, (void *)(uintptr_t)target, a->cfg.user_ctx);
                }
            }
            xSemaphoreGive(a->lock);
        }

        if (a->state != ESP32_AVIDEC_STATE_PLAYING) {
            a->next_frame_time = 0;     /* เริ่มนับจังหวะใหม่เมื่อกลับมาเล่น */
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        xSemaphoreTake(a->lock, portMAX_DELAY);

        int ret = AVI_read_data(a->avi, (char *)a->vbuf, a->cfg.video_buf_size,
                                (char *)a->abuf, a->cfg.audio_buf_size, &chunk_len);

        // จัดการกรณีบัฟเฟอร์เล็กเกินไป
        if (ret < 0) {
            /* [แก้ v4] รู้ขนาดจริงจาก chunk_len แล้ว -> ขยายบัฟเฟอร์เท่าที่จำเป็น
               แล้วอ่าน chunk เดิมซ้ำ (AVI_retry_last_chunk) โดยไม่ทิ้งเฟรม
               ทำให้ไม่ต้อง "เดา" ขนาดจาก suggested อย่างเดียว และไม่เปลือง RAM */
            size_t need = (size_t)chunk_len + 64u;      /* +64 กันขอบ header/padding */
            size_t cap  = (ret == -1) ? a->cfg.video_buf_size : a->cfg.audio_buf_size;
            size_t lim  = (ret == -1) ? ESP32_AVIDEC_VBUF_MAX : ESP32_AVIDEC_ABUF_MAX;

            if (need <= lim && need > cap && _grow_buf(ret == -1 ? &a->vbuf : &a->abuf, &cap, need)) {
                if (ret == -1) a->cfg.video_buf_size = cap;
                else           a->cfg.audio_buf_size = cap;
                if (AVI_retry_last_chunk(a->avi) == 0) {
                    a->buf_grow_count++;
                    if (a->buf_grow_count <= 4 || (a->buf_grow_count % 20) == 0)
                        ESP_LOGW(TAG, "buffer grown -> %u B for chunk %ld (grow #%u)",
                                 (unsigned)cap, chunk_len, (unsigned)a->buf_grow_count);
                    xSemaphoreGive(a->lock);
                    continue;                        /* อ่าน chunk เดิมซ้ำด้วยบัฟเฟอร์ใหม่ */
                }
            }

            /* ขยายไม่ได้/เกินเพดาน: แจ้ง event + จำกัด log + ไม่ปล่อยให้วนร้อนๆ */
            a->buf_small_frames++;
            if (a->buf_small_frames < 4 || (a->buf_small_frames % 100) == 0) {
                ESP_LOGE(TAG, "buffer too small (ret=%d, chunk=%ld, vbuf=%u) - frame dropped",
                         ret, chunk_len, (unsigned)a->cfg.video_buf_size);
            }
            if (a->event_cb)
                a->event_cb(ESP32_AVIDEC_EVENT_ERROR, (void *)(intptr_t)ret, a->cfg.user_ctx);
            xSemaphoreGive(a->lock);
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        if (ret == 0) { // EOF
            /* [แก้] รายงานความครบถ้วน: ถ้าแสดงไม่ครบตามที่ส่วนหัวบอก = มีช่วงที่ถูก
               "ข้าม" ไป (LIST movi ประกาศสั้น / gap / chunk เสีย) ไม่ใช่แค่จบปกติ */
            if (a->total_frames > 0 && (long)a->frames_shown != a->total_frames) {
                ESP_LOGW(TAG, "EOF: เล่นไป %u จาก %ld เฟรม (หาย %ld เฟรม) - ตรวจโครงสร้างไฟล์",
                         (unsigned)a->frames_shown, a->total_frames,
                         a->total_frames - (long)a->frames_shown);
            }
            a->state = ESP32_AVIDEC_STATE_STOPPED;
            AVI_seek_start(a->avi);   /* [แก้] กรอกลับไปบล็อกแรกจริง (OpenDML-safe) */
            a->next_frame_time = 0;
            if (a->event_cb) {
                a->event_cb(ESP32_AVIDEC_EVENT_EOF, NULL, a->cfg.user_ctx);
                a->event_cb(ESP32_AVIDEC_EVENT_STATE_CHANGED, (void *)(uintptr_t)a->state, a->cfg.user_ctx);
            }
            xSemaphoreGive(a->lock);
            continue;
        }

        if (ret == 1 && chunk_len > 4) { // Video Chunk
            if (a->next_frame_time == 0)
                a->next_frame_time = esp_timer_get_time();
            a->next_frame_time += a->frame_interval_us;
            int64_t now  = esp_timer_get_time();
            int64_t late = now - a->next_frame_time;   /* >0 = ช้ากว่ากำหนด */

            /* [แก้] ถ้าสายเกิน 1.5 เฟรม ให้ "ทิ้งเฟรมนี้ก่อน decode"
               - ประหยัด CPU/ไฟล์งานจอ และไม่เกิด burst ไล่เก็บยาว ๆ
               - คงเส้นเวลาเดิมไว้ (ไม่รีเซ็ต) เพื่อให้ A/V กลับมาตรงกันได้ */
            if (late > (a->frame_interval_us * 3) / 2) {
                a->dropped_frames++;
                if ((a->dropped_frames % 50) == 1) {
                    ESP_LOGW(TAG, "pacing: %.0f ms late, dropped %lu frames",
                             (double)late / 1000.0, (unsigned long)a->dropped_frames);
                }
                xSemaphoreGive(a->lock);
                continue;
            }

            size_t offset = 0;
            if (!(chunk_len >= 2 && a->vbuf[0] == 0xFF && a->vbuf[1] == 0xD8)) {
                while (offset + 1 < (size_t)chunk_len) {
                    if (a->vbuf[offset] == 0xFF && a->vbuf[offset + 1] == 0xD8) break;
                    offset++;
                }
                if (offset + 1 >= (size_t)chunk_len)
                    offset = 0;
            }

            if (a->video_cb) {
                a->frames_shown++;
                a->video_cb(a->vbuf + offset, (size_t)chunk_len - offset,
                            (uint32_t)(a->avi->video_pos - 1), a->cfg.user_ctx);
            }

            int64_t diff = a->next_frame_time - now;
            if (diff > 1000) {
                xSemaphoreGive(a->lock);
                vTaskDelay(pdMS_TO_TICKS(diff / 1000));
                continue;
            }
            if (diff > 0) {
                xSemaphoreGive(a->lock);
                esp_rom_delay_us((uint32_t)diff);
                continue;
            }
            /* ช้ากว่ากำหนดแบบสะสมเกิน 1.5 วินาที (เช่น SD หลุด) -> ยอม resync */
            if (late > 1500000) {
                ESP_LOGW(TAG, "pacing resync: %.1f s behind", (double)late / 1e6);
                a->next_frame_time = now;
                a->dropped_frames = 0;
            }
        } 
        else if (ret == 2 && chunk_len > 0) { // Audio Chunk
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

    avi->seek_mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    avi->seek_target_sec = -1;
    avi->next_frame_time = 0;
    avi->dropped_frames = 0;
    avi->buf_small_frames = 0;
    avi->buf_grow_count = 0;
    avi->frames_shown = 0;
    avi->max_frame_seen = 0;
    avi->state = ESP32_AVIDEC_STATE_IDLE;
    avi->task_run = true;

    BaseType_t res = xTaskCreatePinnedToCore(
        _avidec_worker_task, "avi_worker", avi->cfg.task_stack, (void *)avi,
        avi->cfg.task_priority, &avi->task_hdl, avi->cfg.task_core_id
    );

    if (res != pdPASS) {
        vSemaphoreDelete(avi->lock);   /* [แก้] เดิม leak mutex ตรงนี้ */
        avi->lock = NULL;
        return ESP32_AVIDEC_ERR_FAIL;
    }
    return ESP32_AVIDEC_OK;
}

esp32_avidec_err_t esp32_avidec_set_file(esp32_avidec_t *avi, const char *filepath) {
    if (!avi || !filepath) return ESP32_AVIDEC_ERR_INVALID_ARG;
    xSemaphoreTake(avi->lock, portMAX_DELAY);

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

    /* นโยบายบัฟเฟอร์ (v4) — ไม่เดาขนาด:
     *   เริ่มต้น = max(dwSuggestedBufferSize + margin, cfg ของผู้ใช้) โดยมีพื้น 32 KB
     *   ถ้าจริง ๆ เจอ chunk ใหญ่กว่านั้น AVI_read_data() จะคืน -1/-2 พร้อมขนาดจริง
     *   แล้ว worker จะ "ขยาย + อ่านซ้ำ" ให้เอง (ดู _grow_buf / AVI_retry_last_chunk)
     *   => จองเท่าที่ไฟล์บอก และไม่มีทาง "จองน้อยเกินจนเฟรมหาย" อีก */
    uint32_t v_suggested = AVI_video_suggested_buffer_size(avi->avi);
    size_t actual_vbuf_sz = (size_t)v_suggested + 4096u;
    if (actual_vbuf_sz < avi->cfg.video_buf_size)
        actual_vbuf_sz = avi->cfg.video_buf_size;          /* cfg = พื้นล่าง */
    if (actual_vbuf_sz < 32768) actual_vbuf_sz = 32768;    /* กันค่าตั้งต้นจิ๋ว ๆ */
    if (actual_vbuf_sz > ESP32_AVIDEC_VBUF_MAX) actual_vbuf_sz = ESP32_AVIDEC_VBUF_MAX;
    ESP_LOGI(TAG, "video buffer: cfg=%u suggested=%u -> %u bytes",
             (unsigned)(avi->cfg.video_buf_size), (unsigned)v_suggested, (unsigned)actual_vbuf_sz);
    avi->cfg.video_buf_size = actual_vbuf_sz;

    uint32_t a_suggested = AVI_audio_suggested_buffer_size(avi->avi);
    size_t actual_abuf_sz = (a_suggested > 0) ? (size_t)(a_suggested + 1024) : 4096;
    if (actual_abuf_sz < avi->cfg.audio_buf_size) actual_abuf_sz = avi->cfg.audio_buf_size;
    if (actual_abuf_sz < 4096) actual_abuf_sz = 4096;
    if (actual_abuf_sz > ESP32_AVIDEC_ABUF_MAX) actual_abuf_sz = ESP32_AVIDEC_ABUF_MAX;
    avi->cfg.audio_buf_size = actual_abuf_sz;

    /* [แก้] เฟรมใหญ่ไม่น่าอยู่ใน DRAM ภายในได้ -> ลอง PSRAM ก่อน ไม่ให้ heap ภายในแตก */
    if (avi->cfg.video_buf_size > 32768) {
        avi->vbuf = (uint8_t *)heap_caps_aligned_alloc(64, avi->cfg.video_buf_size, MALLOC_CAP_SPIRAM);
    }
    if (!avi->vbuf) {
        avi->vbuf = (uint8_t *)heap_caps_aligned_alloc(64, avi->cfg.video_buf_size,
                                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    }
    if (!avi->vbuf) {
        avi->vbuf = (uint8_t *)heap_caps_aligned_alloc(64, avi->cfg.video_buf_size, MALLOC_CAP_SPIRAM);
    }
    avi->abuf = (uint8_t *)malloc(avi->cfg.audio_buf_size);

    if (!avi->vbuf || !avi->abuf) {
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
    if (!avi || !avi->avi || !avi->lock) return ESP32_AVIDEC_ERR_INVALID_STATE;
    if (target_sec > (uint32_t)INT32_MAX) return ESP32_AVIDEC_ERR_INVALID_ARG;
    /* [แก้] เขียน 64 บิตจาก task อื่น -> ต้องอยู่ใน critical section */
    portENTER_CRITICAL(&avi->seek_mux);
    avi->seek_target_sec = (int64_t)target_sec;
    portEXIT_CRITICAL(&avi->seek_mux);
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
    avi->seek_target_sec = -1;
    avi->next_frame_time = 0;
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