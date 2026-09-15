// ════════════════════════════════════════════════════════════════════════════
//  esp32_http_req.h  —  HTTP(S) Request Library for ESP32 / ESP-IDF v6.0
// ════════════════════════════════════════════════════════════════════════════
#pragma once

#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ────────────────────────────────────────────────────────────────────────────
//  Constants
// ────────────────────────────────────────────────────────────────────────────
#define HTTP_REQ_TIMEOUT_MS           10000
#define HTTP_REQ_DEFAULT_MAX_BODY     (64 * 1024)
#define HTTP_REQ_MIN_FREE_HEAP        (32 * 1024)
#define HTTP_REQ_MAX_RESP_HDRS        16
#define HTTP_REQ_RESP_KEY_LEN         64
#define HTTP_REQ_RESP_VAL_LEN         256
#define HTTP_REQ_MAX_REQ_HDRS         16
#define HTTP_REQ_POLL_DEFAULT_STACK   8192
#define HTTP_REQ_POLL_DEFAULT_PRIO    5

// ────────────────────────────────────────────────────────────────────────────
//  Internal types
// ────────────────────────────────────────────────────────────────────────────
typedef struct {
    char key[HTTP_REQ_RESP_KEY_LEN];
    char val[HTTP_REQ_RESP_VAL_LEN];
} _http_resp_hdr_entry_t;

// ────────────────────────────────────────────────────────────────────────────
//  Long-poll callback
//
//  ถูกเรียกจาก HTTP_EVENT_ON_DATA ทุกครั้งที่ได้รับ chunk จาก server
//  ไม่ต้องรอให้ connection ปิด — ทำให้รองรับ SSE และ long-poll ได้
//
//  body = chunk ที่ได้รับ (null-terminated, reuse buffer เดิม)
//  body_len = ความยาว chunk นี้ (ไม่ใช่ accumulated)
//
//  ⚠ callback ทำงานใน poll task
//     ห้ามเรียก http_open/send/close จาก callback
//     ถ้าต้องการหยุด ให้เรียก http_long_poll_stop() จาก task อื่น
// ────────────────────────────────────────────────────────────────────────────
typedef void (*http_poll_cb_t)(uint16_t status_code,
                                const char *body,
                                size_t body_len,
                                void *user_ctx);

// ────────────────────────────────────────────────────────────────────────────
//  Long-poll configuration
// ────────────────────────────────────────────────────────────────────────────
typedef struct {
    // ── Request ───────────────────────────────────────────────────────────
    const char              *url;
    esp_http_client_method_t method;

    // headers[][2] = {{"Key","Val"}, ..., {NULL,NULL}}
    const char *(*headers)[2];

    // body สำหรับ POST/PUT — pointer ต้องอยู่ได้ตลอด lifetime ของ poll task
    const char *body;

    // ── Response ──────────────────────────────────────────────────────────
    bool recv_body;   // default: true

    // ── Callback ──────────────────────────────────────────────────────────
    http_poll_cb_t on_data;    // เรียกต่อ chunk ที่ได้รับ (ทั้ง success/error)
    void          *user_ctx;

    // ── Timing ────────────────────────────────────────────────────────────
    // timeout ของแต่ละ request (ms)
    // long-poll จริง: ตั้งสูงๆ เช่น 60000
    // server จะ hold connection ไว้จนมีข้อมูลหรือ timeout
    uint32_t request_timeout_ms;

    // หน่วงเวลาก่อน reconnect หลัง connection ปิด (ms)
    // long-poll: 0 (reconnect ทันที)
    // polling ธรรมดา: เช่น 5000
    uint32_t reconnect_ms;

    // ── FreeRTOS Task ─────────────────────────────────────────────────────
    uint32_t    task_stack_size;  // 0 = HTTP_REQ_POLL_DEFAULT_STACK (8192)
    UBaseType_t task_priority;    // 0 = HTTP_REQ_POLL_DEFAULT_PRIO (5)
    const char *task_name;        // NULL = "http_poll"
} http_poll_config_t;

// ────────────────────────────────────────────────────────────────────────────
//  http_req_t — Object หลัก (http_req_t req = {0})
//
//  ⚠ req ที่ใช้กับ long-poll ต้องเป็น static หรือ global
//     เพราะ poll task อ้างอิง pointer ของมันตลอดเวลา
// ────────────────────────────────────────────────────────────────────────────
typedef struct {
    // ── Internal ──────────────────────────────────────────────────────────
    esp_http_client_handle_t _client;

    bool _recv_header;
    bool _recv_body;
    bool _opts_set;

    bool _active_recv_hdr;
    bool _active_recv_body;

    char  *_resp_body;
    size_t _resp_body_len;
    bool   _mem_error;

    int _status_code;

    _http_resp_hdr_entry_t _resp_hdrs[HTTP_REQ_MAX_RESP_HDRS];
    int                    _resp_hdr_count;

    char *_req_hdr_keys[HTTP_REQ_MAX_REQ_HDRS];
    int   _req_hdr_count;

    // ── Long-poll state ───────────────────────────────────────────────────
    TaskHandle_t    _poll_task;
    volatile bool   _poll_stop;

    http_poll_cb_t  _poll_cb;
    void           *_poll_ctx;

    const char              *_poll_url;
    esp_http_client_method_t _poll_method;
    const char *(*_poll_headers)[2];
    const char              *_poll_body;
    uint32_t                 _poll_reconnect_ms;
    uint32_t                 _poll_timeout_ms;   // ← เก็บ timeout ที่ใช้ init client

} http_req_t;

// ────────────────────────────────────────────────────────────────────────────
//  Standard API
// ────────────────────────────────────────────────────────────────────────────
void     http_open(http_req_t *req, const char *url, esp_http_client_method_t method);
void     http_set_header(http_req_t *req, const char *key, const char *value);
void     http_set_body(http_req_t *req, const char *body);
void     http_set_resp_opt(http_req_t *req, bool recv_header, bool recv_body);
char    *http_send(http_req_t *req);
char    *http_get_header(http_req_t *req, const char *key);
uint16_t http_get_code(http_req_t *req);
void     http_close(http_req_t *req);
char    *http_fetch_json(http_req_t *req, const char *url, esp_http_client_method_t method);

// ────────────────────────────────────────────────────────────────────────────
//  Long-poll API
// ────────────────────────────────────────────────────────────────────────────

/**
 * @brief  เริ่ม long-poll ใน FreeRTOS task แยก
 *
 * flow ของ poll task:
 *   loop:
 *     perform() ← block อยู่จนกว่า server ส่งข้อมูลหรือ timeout
 *       ├─ ต่อ chunk: HTTP_EVENT_ON_DATA → _event_handler → on_data callback
 *       └─ connection ปิด/timeout → perform() return
 *     รอ reconnect_ms (interruptible)
 *     loop อีกครั้ง
 *
 * @return ESP_OK หรือ ESP_ERR_INVALID_STATE ถ้า poll กำลังทำงานอยู่
 */
esp_err_t http_long_poll_start(http_req_t *req, const http_poll_config_t *config);

/**
 * @brief  หยุด long-poll (non-blocking)
 *
 * set stop flag + notify task ให้ตื่นจาก reconnect delay ทันที
 * ถ้ากำลัง block ใน perform() อยู่ จะรอ request timeout ของ round นั้น
 *
 * ตรวจสอบว่าหยุดสมบูรณ์ด้วย http_long_poll_is_running()
 */
void http_long_poll_stop(http_req_t *req);

/**
 * @brief  ตรวจสอบว่า poll task ทำงานอยู่ไหม
 */
bool http_long_poll_is_running(const http_req_t *req);

#ifdef __cplusplus
}
#endif