#ifndef ESP32_AVIDEC_H
#define ESP32_AVIDEC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/FreeRTOS.h"
#include "avilib.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP32_AVIDEC_MAX_BLOCKS 64

/* เพดานบัฟเฟอร์กันไฟล์เสีย/ค่า size ขยะ: ถ้า chunk ที่ต้องใช้ใหญ่กว่านี้ถือว่า
   ผิดปกติ -> ทิ้ง chunk นั้น + แจ้ง EVENT_ERROR (ไม่พยายามจองตาม) */
#define ESP32_AVIDEC_VBUF_MAX  (4u * 1024u * 1024u)
#define ESP32_AVIDEC_ABUF_MAX  (256u * 1024u)

typedef enum {
    ESP32_AVIDEC_OK                =  0,
    ESP32_AVIDEC_ERR_FAIL          = -1,
    ESP32_AVIDEC_ERR_NO_MEM        = -2,
    ESP32_AVIDEC_ERR_INVALID_ARG   = -3,
    ESP32_AVIDEC_ERR_INVALID_STATE = -4,
} esp32_avidec_err_t;

typedef enum {
    ESP32_AVIDEC_STATE_IDLE = 0,   // หลัง init หรือหลัง destroy
    ESP32_AVIDEC_STATE_LOADED,     // เปิดไฟล์สำเร็จ พร้อมเล่น
    ESP32_AVIDEC_STATE_PLAYING,    // กำลังเล่น
    ESP32_AVIDEC_STATE_PAUSED,     // หยุดชั่วคราว
    ESP32_AVIDEC_STATE_STOPPED,    // หยุดเล่นและกรอกลับไปจุดเริ่มต้น
} esp32_avidec_state_t;

typedef enum {
    ESP32_AVIDEC_EVENT_EOF = 0,
    ESP32_AVIDEC_EVENT_STATE_CHANGED,
    ESP32_AVIDEC_EVENT_SEEK_DONE,
    ESP32_AVIDEC_EVENT_ERROR,
} esp32_avidec_event_t;

// Signature สำหรับ Callback ต่างๆ
typedef void (*esp32_avidec_video_cb_t)(const uint8_t *frame_data, size_t len, uint32_t frame_idx, void *user_ctx);
typedef void (*esp32_avidec_audio_cb_t)(const uint8_t *audio_data, size_t len, void *user_ctx);
typedef void (*esp32_avidec_event_cb_t)(esp32_avidec_event_t event, void *event_data, void *user_ctx);

typedef struct {
    size_t      video_buf_size;   // บัฟเฟอร์เฟรมภาพ (แนะนำ 128 * 1024)
    size_t      audio_buf_size;   // บัฟเฟอร์เสียง (แนะนำ 4096)
    size_t      read_cache_size;  // บัฟเฟอร์ FatFS cache ของ avilib (แนะนำ 128 * 1024)
    UBaseType_t task_priority;    // FreeRTOS Task Priority (แนะนำ 5)
    BaseType_t  task_core_id;     // Core ID (แนะนำ 1 หรือ tskNO_AFFINITY)
    uint32_t    task_stack;       // Stack size (แนะนำ 8192)
    void       *user_ctx;         // Custom context สำหรับส่งคืนใน Callbacks
} esp32_avidec_cfg_t;

typedef struct {
    uint64_t movi_start;
    uint64_t movi_end;
} esp32_avidec_block_t;

typedef struct esp32_avidec_t {
    avi_t                 *avi;
    esp32_avidec_state_t   state;
    esp32_avidec_cfg_t     cfg;

    // Callbacks
    esp32_avidec_video_cb_t video_cb;
    esp32_avidec_audio_cb_t audio_cb;
    esp32_avidec_event_cb_t event_cb;

    // Working Buffers
    uint8_t               *vbuf;
    uint8_t               *abuf;

    // Synchronization & Task Control
    TaskHandle_t           task_hdl;
    SemaphoreHandle_t      lock;
    portMUX_TYPE           seek_mux;         /* protects seek_target_sec */
    volatile bool          task_run;
    volatile int64_t       seek_target_sec;
    int64_t                next_frame_time;  /* pacing clock (us) */
    uint32_t               dropped_frames;   /* frames dropped by pacing */
    uint32_t               buf_small_frames; /* AVI_read_data() == -1/-2 events */
    uint32_t               buf_grow_count;   /* ขยายบัฟเฟอร์อัตโนมัติสำเร็จกี่ครั้ง */
    uint32_t               frames_shown;     /* เฟรมภาพที่ส่งเข้า callback จริง (ตรวจความครบถ้วน) */
    uint32_t               max_frame_seen;   /* largest video chunk seen */

    // Video Properties
    int                    width;
    int                    height;
    double                 fps;
    long                   total_frames;
    int64_t                frame_interval_us;

    // OpenDML Block Mapping (เพื่อการ Seek แบบ O(1) ไร้ดีเลย์)
    esp32_avidec_block_t   blocks[ESP32_AVIDEC_MAX_BLOCKS];
    int                    block_cnt;
} esp32_avidec_t;

#define ESP32_AVIDEC_CONFIG_DEFAULT() { \
    .video_buf_size  = 128 * 1024,      \
    .audio_buf_size  = 4096,            \
    .read_cache_size = 128 * 1024,      \
    .task_priority   = 5,               \
    .task_core_id    = 1,               \
    .task_stack      = 8192,            \
    .user_ctx        = NULL             \
}

// Lifecycle APIs
esp32_avidec_err_t esp32_avidec_init(esp32_avidec_t *avi, const esp32_avidec_cfg_t *cfg);
esp32_avidec_err_t esp32_avidec_set_file(esp32_avidec_t *avi, const char *filepath);
esp32_avidec_err_t esp32_avidec_set_video_cb(esp32_avidec_t *avi, esp32_avidec_video_cb_t cb);
esp32_avidec_err_t esp32_avidec_set_audio_cb(esp32_avidec_t *avi, esp32_avidec_audio_cb_t cb);
esp32_avidec_err_t esp32_avidec_set_event_cb(esp32_avidec_t *avi, esp32_avidec_event_cb_t cb);

// Control APIs
esp32_avidec_err_t esp32_avidec_play(esp32_avidec_t *avi); // เริ่มเล่นวิดีโอ
esp32_avidec_err_t esp32_avidec_pause(esp32_avidec_t *avi); // หยุดวิดีโอชั่วคราว (ยังไม่ทำลายพวก buf ของระบบที่จองไว้)
esp32_avidec_err_t esp32_avidec_resume(esp32_avidec_t *avi); // เล่นวิดีโอต่อจากที่พักไว้
esp32_avidec_err_t esp32_avidec_stop(esp32_avidec_t *avi); // หยุดเล่นวิดีโอ แล้วเลื่อนทุกอย่างไปอยู่ที่จุดเริ่มต้น แต่ยังคงไม่ทำลาย buf ที่ระบบจองไว้เช่นกัน
esp32_avidec_err_t esp32_avidec_seek(esp32_avidec_t *avi, uint32_t target_sec);

// Cleanup APIs
esp32_avidec_err_t esp32_avidec_destroy(esp32_avidec_t *avi); // ทำลายระบบเล่นวิดีโอทั้งหมดอย่างปลอดภัยเป็นลำดับ เสมือนยังไม่ได้ esp32_avidec_set_file
esp32_avidec_err_t esp32_avidec_deinit(esp32_avidec_t *avi); // ทำลายระบบเล่นวิดีโอทั้งหมดอย่างปลอดภัยเป็นลำดับ (หาก esp32_avidec_stop, esp32_avidec_destroy ยังไม่ถูกผู้ใช้เรียกตามลำดับ) และทำลายหน่วยความจำที่จองไว้สำหรับ lib ทุกอย่าง เสมือนยังไม่ได้ esp32_avidec_init 

// Info Getters
uint32_t             esp32_avidec_get_current_sec(esp32_avidec_t *avi);
uint32_t             esp32_avidec_get_total_sec(esp32_avidec_t *avi);
esp32_avidec_state_t esp32_avidec_get_state(esp32_avidec_t *avi);

#ifdef __cplusplus
}
#endif

#endif // ESP32_AVIDEC_H