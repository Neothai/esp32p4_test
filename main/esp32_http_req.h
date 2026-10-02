// ════════════════════════════════════════════════════════════════════════════
//  esp32_http_req.h  —  HTTP(S) Request Library for ESP32 / ESP-IDF v6.0
// ════════════════════════════════════════════════════════════════════════════
//
//  ════════════════════════════════════════════════════════════════════════
//  Ownership Contract — อ่านก่อนใช้
//  ════════════════════════════════════════════════════════════════════════
//
//  [O1] ทุก http_req_t ถูกสร้างขึ้นโดย lib เท่านั้น (calloc ใน http_open)
//       - ห้ามผู้ใช้สร้าง struct เเอง (stack/static/global)
//       - ห้ามผู้ใช้ free()/malloc()/realloc() กับ req หรือสมาชิกใน req
//
//  [O2] http_close(req) คือทางออกเดียวของการคืนหน่วยความจำทั้งหมด
//       (esp_http_client, response body, request header keys, poll semaphores, ตัว req)
//       - เรียก "หนึ่งครั้ง" ต่อ req แล้วตั้ง pointer ของผู้ใช้ = NULL ทันที
//       - ถ้า http_close() คืน ESP_FAIL (poll task ไม่ยอมจบ) = "ยังไม่ได้ free"
//         ห้ามใช้ pointer ต่อ ให้ retry http_long_poll_stop()/http_close() เมื่อพร้อม
//
//  [O3] ค่าที่คืนจาก http_send() / http_get_header() / http_fetch_json()
//       เป็น borrowed pointer ชี้ข้อมูล "ภายใน" req:
//       - ห้าม free()
//       - อ่านได้เฉพาะระหว่าง req ยังไม่ถูก close
//
//  [O4] ตัวอักษรที่ส่งเข้า lib เป็น borrowed — ต้องอยู่ได้ครบช่วงเวลาที่ระบุ:
//       - http_open(url):            url อยู่ถึง http_close(req)
//       - http_set_body(body):       body อยู่จนจบ http_send() นั้น
//       - poll: config->url/body/headers/user_ctx อยู่จน task หยุดสมบูรณ์
//
//  [O5] http_send() ล้มเหลว = req เสียสภาพ (client ถูก cleanup ไปแล้ว)
//       ให้ http_close(req) แล้ว http_open() ใหม่ — ห้าม retry บน req เดิม
//
//  [O6] long-poll:
//       - poll task ไม่เคย free req เเอง (แก้จากเวอร์ชันก่อน)
//       - ปิด req ขณะที่ poll ยังทำงาน: เรียก http_close() ได้เลย
//         (lib จะ stop + รอ task จบเองก่อน free)
//         หรือ http_long_poll_stop() ก่อนแล้วค่อย http_close()
//       - ห้ามเรียก http_* ใดๆ (รวม stop/close) จาก "ภายใน" on_data callback
//         เพราะ callback ทำงานอยู่บน poll task เเอง → deadlock
//
//  [O7] req ตัวหนึ่งถูกใช้ทีละหนึ่ง thread (standard mode)
//       poll mode มี poll task เป็นผู้ใช้คนเดียว — ผู้ใช้ห้าม send/close พลาดซ้ำซ้อน
// ════════════════════════════════════════════════════════════════════════════
#pragma once

#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
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
// margin เติมให้ poll task มีเวลาออกจาก perform()/callback
// (stop/close รอ task นานสุด = request_timeout_ms + margin นี้)
#define HTTP_REQ_POLL_STOP_MARGIN_MS  5000

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
//  ทำงานบน poll task — ไม่ต้องรอให้ connection ปิด
//
//  body     = chunk ที่ได้รับ (null-terminated, reuse buffer เดิม)
//  body_len = ความยาว chunk นี้ (ไม่ใช่ accumulated)
//
//  ⚠ ข้อจำกัด:
//    - ห้ามเรียก http_* ใดๆ จาก callback (open/send/close/stop) → deadlock
//    - ถ้าต้องการหยุด ให้เรียก http_long_poll_stop() จาก task "อื่น"
//    - user_ctx: lib ไม่เคย free — ผู้ใช้รับผิดชอบให้มันอยู่ได้จน task จบ
// ────────────────────────────────────────────────────────────────────────────
typedef void (*http_poll_cb_t)(uint16_t status_code,
                                const char *body,
                                size_t body_len,
                                void *user_ctx);

// ────────────────────────────────────────────────────────────────────────────
//  Long-poll configuration
//
//  ⚠ url / headers / body / user_ctx เป็น borrowed pointer:
//    ต้องยัง valid ตลอดจน poll task หยุดสมบูรณ์
//    (http_long_poll_stop() คืน ESP_OK แล้ว หรือ http_close(req) สำเร็จแล้ว)
// ────────────────────────────────────────────────────────────────────────────
typedef struct {
    // ── Request ───────────────────────────────────────────────────────────
    const char              *url;
    esp_http_client_method_t method;

    // headers[][2] = {{"Key","Val"}, ..., {NULL,NULL}}
    const char *(*headers)[2];

    // body สำหรับ POST/PUT
    const char *body;

    // ── Response ──────────────────────────────────────────────────────────
    // reserved — poll mode รับ body เสมอ (on_data คือจุดประสงค์หลัก)
    bool recv_body;

    // ── Callback ──────────────────────────────────────────────────────────
    http_poll_cb_t on_data;    // เรียกต่อ chunk ที่ได้รับ
    void          *user_ctx;   // opaque — lib ไม่ free

    // ── Timing ────────────────────────────────────────────────────────────
    // timeout ของแต่ละ request (ms) — 0 = HTTP_REQ_TIMEOUT_MS
    // long-poll จริง: ตั้งสูงๆ เช่น 60000
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
//  http_req_t — Object หลัก
//
//  ⚠ ตาม ownership contract [O1]:
//    ทุกตัวมาจากการ calloc ของ lib ผ่าน http_open() เท่านั้น
//    ผู้ใช้ห้ามสร้าง struct เเอง ห้าม free เเอง — คืนผ่าน http_close() เท่านั้น
// ────────────────────────────────────────────────────────────────────────────
typedef struct {
    // ── Internal (lib owns — ห้ามผู้ใช้เข้าถึง/free) ─────────────────────────
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
    TaskHandle_t    _poll_task;      // NULL = task ไม่ทำงาน
    volatile bool   _poll_stop;

    http_poll_cb_t  _poll_cb;
    void           *_poll_ctx;

    const char              *_poll_url;
    esp_http_client_method_t _poll_method;
    const char *(*_poll_headers)[2];
    const char              *_poll_body;
    uint32_t                 _poll_reconnect_ms;
    uint32_t                 _poll_timeout_ms;

    // one-shot: task give ครั้งเดียวตอนจบสมบูรณ์ (stop/close รอจาก token นี้)
    SemaphoreHandle_t _poll_done_sem;
    // ใช้ตื่น poll task จาก reconnect delay / พาสัญญาณ stop
    SemaphoreHandle_t _poll_wake_sem;
} http_req_t;

// ────────────────────────────────────────────────────────────────────────────
//  Standard API
// ────────────────────────────────────────────────────────────────────────────

/**
 * @brief สร้าง req + init esp_http_client
 *
 * @return handle หรือ NULL ถ้า allocate/init ไม่สำเร็จ
 *         (กรณี fail lib คืนหน่วยความจำหมดแล้ว — ผู้ใช้ไม่ต้องทำอะไรเพิ่ม)
 *
 * @note url เป็น borrowed pointer — ต้องอยู่ได้จน http_close(req)
 *       (esp_http_client เก็บ pointer ของ url ไว้)
 */
http_req_t *http_open(const char *url, esp_http_client_method_t method);

/**
 * @brief ตั้ง request header
 *
 * lib ทำสำเนา key ไว้ track เพื่อ delete จาก client ตอน cleanup
 *
 * @return ESP_OK หรือ ESP_ERR_INVALID_ARG / ESP_ERR_NO_MEM
 *         ถ้า fail — header นั้น "ไม่ถูกตั้ง" (ต่างจากเวอร์ชันก่อนที่ตั้งก่อน track
 *         ทำให้ header ค้างอยู่ใน client ตลอด)
 */
esp_err_t http_set_header(http_req_t *req, const char *key, const char *value);

/**
 * @brief ตั้ง request body (POST/PUT)
 *
 * @note body เป็น borrowed pointer — ต้องอยู่ได้จนจบ http_send()
 */
void http_set_body(http_req_t *req, const char *body);

/**
 * @brief เปิด/ปิดการเก็บ response header/body — เรียกก่อน http_send()
 *
 * ไม่เรียก = เก็บ body แต่ไม่เก็บ header (ค่า default เดิม)
 */
void http_set_resp_opt(http_req_t *req, bool recv_header, bool recv_body);

/**
 * @brief ส่ง request (block จนจบ)
 *
 * @return borrowed pointer ชี้ response body (null-terminated)
 *         — ห้าม free, อ่านได้จน http_close(req)     [O3]
 *         หรือ NULL ถ้า perform fail / OOM / ยังไม่มี client
 *
 * @note perform fail → req เสียสภาพ: http_close(req) แล้ว http_open() ใหม่ [O5]
 */
const char *http_send(http_req_t *req);

/**
 * @brief ค้นหา response header (เฉพาะ case ที่เก็บด้วย http_set_resp_opt(true, ...))
 *
 * @return borrowed pointer ชี้ค่า header หรือ NULL
 */
const char *http_get_header(const http_req_t *req, const char *key);

/**
 * @return HTTP status code หรือ 0
 */
uint16_t http_get_code(const http_req_t *req);

/**
 * @brief ความยาวของ response body (ไบต์) ของ request ล่าสุด
 *
 * คือจำนวนไบต์ "ทั้งหมด" ที่สะสมจากทุก HTTP_EVENT_ON_DATA ของ request นี้
 * (ไม่รวม terminator '\0') — ใช้คู่กับ http_send() ได้โดยตรง:
 *
 *   const char *body = http_send(req);
 *   size_t      len  = http_get_body_len(req);
 *   cJSON_ParseWithLength(body, len);   // ปลอดภัยแม้ body มี byte 0 ข้างใน
 *
 * • ถ้า body เป็น textล้วน strlen(body) จะได้ค่าเดียวกัน
 * • จำกัดที่ HTTP_REQ_DEFAULT_MAX_BODY (64 KB) — เกินไป http_send คืน NULL
 * • ในโหมด long-poll ตัว callback ได้รับ body_len ต่อ chunk อยู่แล้ว
 *   (ค่าตัวนี้ถูก reset เป็น 0 หลังแต่ละ callback) — getter นี้ใช้กับ
 *   โหมด normal เท่านั้น
 * • หลัง http_close() แล้ว ตัวนี้ใช้ไม่ได้
 */
size_t http_get_body_len(const http_req_t *req);

/**
 * @brief คืนหน่วยความจำทั้งหมดของ req — ทางออกเดียว [O2]
 *
 * ถ้า poll task ยังทำงานอยู่ จะ stop + รอ task จบเองก่อน
 * (block นานสุด ≈ request_timeout_ms + HTTP_REQ_POLL_STOP_MARGIN_MS)
 *
 * ห้ามเรียกจากภายใน on_data callback [O6]
 *
 * @return ESP_OK  = คืนหน่วยความจำเสร็จ — ตั้ง pointer ของผู้ใช้ = NULL
 *         ESP_FAIL = poll task ไม่ยอมจบ (เช่น ติดอยู่ใน on_data callback)
 *                    — "ยังไม่ได้ free" ห้ามใช้ pointer ต่อ
 *                    ให้ retry stop/close เมื่อ task ยุติ
 */
esp_err_t http_close(http_req_t *req);

/**
 * @brief Shortcut: http_open + Accept: application/json + User-Agent + http_send
 *
 * แก้ leak จากเวอร์ชันก่อน (v1 เขียนทับ handle ของผู้เรียกแล้วไม่คืน
 * → ไม่มีทาง http_close ได้)
 *
 * @param out_req ออก: handle ของ req เมื่อสำเร็จ — ผู้ใช้ต้อง http_close()
 *                หลังใช้ body/header เสร็จ (body เป็น borrowed pointer [O3])
 *
 * @return borrowed pointer ชี้ JSON body หรือ NULL ถ้า fail
 *         กรณี fail: lib คืนทุกอย่างให้เองแล้ว, *out_req = NULL
 *         ผู้ใช้ไม่ต้องจัดการอะไรเพิ่ม
 */
const char *http_fetch_json(http_req_t **out_req, const char *url,
                            esp_http_client_method_t method);

// ────────────────────────────────────────────────────────────────────────────
//  Long-poll API
// ────────────────────────────────────────────────────────────────────────────

/**
 * @brief เริ่ม long-poll ใน FreeRTOS task แยก
 *
 * flow ของ poll task:
 *   loop:
 *     perform() ← block จนกว่า server ส่งข้อมูล/ปิด connection หรือ timeout
 *       ├─ ต่อ chunk: HTTP_EVENT_ON_DATA → on_data callback
 *       └─ connection ปิด/timeout → perform() return
 *     [ถ้า fail] cleanup + re-init client เพื่อตัด TLS state ค้าง
 *     รอ reconnect_ms (ตื่นได้ทันทีเมื่อมีการ stop)
 *
 * @return ESP_OK / ESP_ERR_INVALID_ARG /
 *         ESP_ERR_INVALID_STATE (poll กำลังทำงานอยู่) /
 *         ESP_ERR_NO_MEM / ESP_FAIL
 */
esp_err_t http_long_poll_start(http_req_t *req, const http_poll_config_t *config);

/**
 * @brief หยุด long-poll — "บล็อก" จนกว่า poll task จะจบสมบูรณ์
 *
 * (ต่างจากเวอร์ชันก่อนที่เป็น non-blocking)
 * set stop flag + signal task แล้วรอ done signal
 * timeout = request_timeout_ms + HTTP_REQ_POLL_STOP_MARGIN_MS
 *
 * ปลอดภัยเรียกจาก task ใดก็ได้ "ยกเว้น" poll task เอง [O6]
 *
 * @return ESP_OK  = task จบแล้ว — ปลอดภัยเรียก http_close(req)
 *         ESP_FAIL = task ยังไม่จบ (เช่น ติดใน on_data callback ที่ทำงานนาน)
 *                    — ห้าม http_close(req) จนกว่า
 *                      http_long_poll_is_running() == false
 */
esp_err_t http_long_poll_stop(http_req_t *req);

/**
 * @brief ตรวจว่า poll task ทำงานอยู่ไหม
 *
 * @return false = task จบแล้ว ปลอดภัย close ได้
 */
bool http_long_poll_is_running(const http_req_t *req);

#ifdef __cplusplus
}
#endif