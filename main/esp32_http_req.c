// ════════════════════════════════════════════════════════════════════════════
//  esp32_http_req.c  —  HTTP(S) Request Library for ESP32 / ESP-IDF v6.0
// ════════════════════════════════════════════════════════════════════════════
//
//  v2 — แก้ memory safety / ownership (contract ครบใน esp32_http_req.h)
//
//  สิ่งที่เปลี่ยนจาก v1:
//   1. http_open(): init client fail → คืนหน่วยความจำ + return NULL
//      (v1 คืน handle ที่ _client=NULL ทำให้ผู้ใช้สับสนว่าเปิดสำเร็จ)
//   2. http_fetch_json(): signature ใหม่ http_fetch_json(http_req_t **out_req, ...)
//      (v1 เขียนทับ parameter req ด้วย http_open() แล้วคืนแต่ body
//      → handle หาย = leak req + client + header keys ทุกครั้งที่เรียก)
//   3. poll task: ไม่ free req เเองตอนจบ
//      (v1 เรียก http_close(req) ใน task → use-after-free / double-free
//      ถ้าผู้เรียกยังถือ pointer อยู่ แล้วเรียก close/is_running อีก)
//   4. http_long_poll_stop(): บล็อกจน task จบ (done semaphore)
//      (v1 non-blocking → http_close() จาก task อื่นอาจ free req
//      ขณะที่ poll task ยัง perform() อยู่)
//   5. http_close(): หยุด + รอ poll task ก่อน free เสมอ, คืน poll semaphores
//   6. http_set_header(): strdup สำเร็จ + ยังมีที่ว่าง → ค่อยตั้งใน client,
//      คืน esp_err_t
//      (v1 ตั้งใน client ก่อน track → header ที่ track ไม่ได้
//      จะค้างอยู่ใน client delete ไม่ได้ โดยเฉพาะ poll ที่ reuse client)
//   7. http_send(): reset body/header/state ทุกครั้งก่อน perform
//      (v1 send ซ้ำบน req เดิม → body เก่าค้างต้น buffer)
//   8. pointer แบบ borrowed ใช้ const char * (http_send/get_header/fetch_json)
//   9. ตัด "reuse req struct" ออก — ทุก req มาจาก http_open() เท่านั้น
//      (v1 มี else branch ให้ผู้ใช้สร้าง struct เเอง แล้ว http_close() จะ
//      free pointer นั้น → free pointer ที่ไม่ใช่ malloc)
//
//  note: จุด free() โดยตรงที่เหลือในไฟล์นี้คือ
//    - free(req) ใน http_close()        ← ตาม contract: free ทุกจุดผ่าน http_close
//    - free(dup) ใน http_set_header()   ← allocation ที่ไม่สำเร็จ ยังไม่ออกจาก
//                                            function ถ้าไม่ free จะรั่ว
// ════════════════════════════════════════════════════════════════════════════
#include "esp32_http_req.h"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "HTTP_REQ";

// ════════════════════════════════════════════════════════════════════════════
//  Private helpers
// ════════════════════════════════════════════════════════════════════════════

static inline bool _heap_ok(size_t needed) {
    return esp_get_free_heap_size() > needed + HTTP_REQ_MIN_FREE_HEAP;
}

static void _free_resp_body(http_req_t *req) {
    free(req->_resp_body);
    req->_resp_body     = NULL;
    req->_resp_body_len = 0;
}

static void _clear_req_hdrs(http_req_t *req) {
    for (int i = 0; i < req->_req_hdr_count; i++) {
        if (req->_req_hdr_keys[i]) {
            if (req->_client) {
                esp_http_client_delete_header(req->_client, req->_req_hdr_keys[i]);
            }
            free(req->_req_hdr_keys[i]);
            req->_req_hdr_keys[i] = NULL;
        }
    }
    req->_req_hdr_count = 0;
}

// ════════════════════════════════════════════════════════════════════════════
//  Event Handler
//
//  HTTP_EVENT_ON_DATA มีสองโหมด:
//
//  โหมด Normal (_poll_cb == NULL):
//    สะสม body ทั้งหมดไว้ใน _resp_body แล้ว http_send() คืนให้หลังจบ
//
//  โหมด Poll/Streaming (_poll_cb != NULL):
//    เรียก callback ต่อ chunk ทันที จาก event handler โดยตรง
//    (ไม่รอให้ http_send() return เพราะ server อาจ hold connection ไว้นาน)
//    หลังเรียก callback จะ reset body_len = 0 เพื่อให้ chunk ถัดไปเริ่มใหม่
//    โดย reuse buffer เดิม ไม่มี malloc เพิ่มต่อ chunk
// ════════════════════════════════════════════════════════════════════════════
static esp_err_t _event_handler(esp_http_client_event_t *evt) {
    http_req_t *req = (http_req_t *)evt->user_data;

    switch (evt->event_id) {

    case HTTP_EVENT_ON_HEADER:
        if (!req->_active_recv_hdr) break;
        if (req->_resp_hdr_count >= HTTP_REQ_MAX_RESP_HDRS) {
            ESP_LOGW(TAG, "resp header table full, dropping '%s'", evt->header_key);
            break;
        }
        strlcpy(req->_resp_hdrs[req->_resp_hdr_count].key,
                evt->header_key,   HTTP_REQ_RESP_KEY_LEN);
        strlcpy(req->_resp_hdrs[req->_resp_hdr_count].val,
                evt->header_value, HTTP_REQ_RESP_VAL_LEN);
        req->_resp_hdr_count++;
        break;

    case HTTP_EVENT_ON_DATA:
        if (!req->_active_recv_body) break;
        if (req->_mem_error) return ESP_FAIL;

        {
            size_t needed = req->_resp_body_len + (size_t)evt->data_len + 1;

            if (!_heap_ok((size_t)evt->data_len) || needed > HTTP_REQ_DEFAULT_MAX_BODY) {
                ESP_LOGE(TAG, "OOM: free=%zu needed=%zu max=%zu",
                         (size_t)esp_get_free_heap_size(), needed,
                         (size_t)HTTP_REQ_DEFAULT_MAX_BODY);
                req->_mem_error = true;
                return ESP_FAIL;
            }

            char *tmp = realloc(req->_resp_body, needed);
            if (!tmp) {
                req->_mem_error = true;
                return ESP_FAIL;
            }

            req->_resp_body = tmp;
            memcpy(req->_resp_body + req->_resp_body_len, evt->data, (size_t)evt->data_len);
            req->_resp_body_len += (size_t)evt->data_len;
            req->_resp_body[req->_resp_body_len] = '\0';

            // ── Poll/Streaming mode: เรียก callback ต่อ chunk ทันที ──────────
            //
            //  ทำงานภายใน event handler โดยตรง ไม่ต้องรอให้ http_send() return
            //  ทำให้รองรับ server ที่ hold connection หรือ SSE ได้
            //
            //  หลัง callback: reset len = 0 แต่ไม่ free buffer
            //  chunk ถัดไปจะ overwrite ตั้งแต่ต้น — ประหยัด malloc ต่อ chunk
            //
            if (req->_poll_cb) {
                uint16_t code = (uint16_t)esp_http_client_get_status_code(evt->client);
                req->_poll_cb(code, req->_resp_body, req->_resp_body_len, req->_poll_ctx);
                req->_resp_body_len = 0;
                req->_resp_body[0]  = '\0';
            }
        }
        break;

    default:
        break;
    }

    return ESP_OK;
}

// ════════════════════════════════════════════════════════════════════════════
//  Poll: stop + wait
//
//  set stop flag + signal wake semaphore (ปลุกจาก reconnect delay)
//  แล้วรอ done semaphore ที่ poll task give "ครั้งเดียว" ตอนจบ
// ════════════════════════════════════════════════════════════════════════════
static esp_err_t _poll_stop_and_wait(http_req_t *req) {
    req->_poll_stop = true;
    if (req->_poll_wake_sem) {
        xSemaphoreGive(req->_poll_wake_sem);
    }

    uint32_t wait_ms = req->_poll_timeout_ms + HTTP_REQ_POLL_STOP_MARGIN_MS;
    if (req->_poll_done_sem) {
        if (xSemaphoreTake(req->_poll_done_sem, pdMS_TO_TICKS(wait_ms)) == pdTRUE) {
            return ESP_OK;
        }
    }

    // fallback (ปกติไม่ควรมาถึง: ถ้า task ทำงานอยู่ sem ต้องถูกสร้างแล้ว)
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(wait_ms);
    while (req->_poll_task && xTaskGetTickCount() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return req->_poll_task ? ESP_FAIL : ESP_OK;
}

// ════════════════════════════════════════════════════════════════════════════
//  Standard API
// ════════════════════════════════════════════════════════════════════════════

http_req_t *http_open(const char *url, esp_http_client_method_t method) {
    if (!url) {
        ESP_LOGE(TAG, "http_open: invalid args");
        return NULL;
    }

    http_req_t *req = (http_req_t *)calloc(1, sizeof(http_req_t));
    if (!req) {
        ESP_LOGE(TAG, "http_open: out of memory");
        return NULL;
    }

    esp_http_client_config_t cfg = {
        .url               = url,   // borrowed: esp_http_client เก็บ pointer นี้ไว้
        .method            = method,
        .event_handler     = _event_handler,
        .user_data         = req,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
        .buffer_size       = 4096,
        .buffer_size_tx    = 2048,
        .timeout_ms        = HTTP_REQ_TIMEOUT_MS,
    };
    req->_client = esp_http_client_init(&cfg);
    if (!req->_client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        http_close(req);   // fail → คืนทุกอย่างแล้วคืน NULL (ไม่ปล่อย handle เสียสภาพออก)
        return NULL;
    }

    return req;
}

esp_err_t http_set_header(http_req_t *req, const char *key, const char *value) {
    if (!req || !req->_client || !key || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    if (req->_req_hdr_count >= HTTP_REQ_MAX_REQ_HDRS) {
        ESP_LOGE(TAG, "req header table full (%d)", HTTP_REQ_MAX_REQ_HDRS);
        return ESP_ERR_NO_MEM;
    }

    char *dup = strdup(key);
    if (!dup) {
        ESP_LOGE(TAG, "strdup header key failed");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_set_header(req->_client, key, value);
    if (err != ESP_OK) {
        free(dup);   // ยังไม่ได้ register tracking — คืน dup ทันที
        return err;
    }

    req->_req_hdr_keys[req->_req_hdr_count++] = dup;
    return ESP_OK;
}

void http_set_body(http_req_t *req, const char *body) {
    if (!req || !req->_client || !body) return;
    esp_http_client_set_post_field(req->_client, body, (int)strlen(body));
}

void http_set_resp_opt(http_req_t *req, bool recv_header, bool recv_body) {
    if (!req) return;
    req->_recv_header = recv_header;
    req->_recv_body   = recv_body;
    req->_opts_set    = true;
}

const char *http_send(http_req_t *req) {
    if (!req || !req->_client) {
        ESP_LOGE(TAG, "http_send: no client — call http_open() first (หรือ req นี้ถูกปิดแล้ว)");
        return NULL;
    }

    req->_active_recv_hdr  = req->_opts_set ? req->_recv_header : false;
    req->_active_recv_body = req->_opts_set ? req->_recv_body   : true;

    // reset จาก round ก่อน → ทำให้ http_send() เรียกซ้ำบน req เดิมปลอดภัย
    _free_resp_body(req);
    req->_resp_hdr_count = 0;
    req->_status_code    = 0;
    req->_mem_error      = false;

    esp_err_t err = esp_http_client_perform(req->_client);
    req->_status_code = esp_http_client_get_status_code(req->_client);

    // perform fail / OOM → client อาจมี state ค้าง (TLS/socket)
    //   ทิ้ง client ในนี้ แล้วคืน NULL → ผู้ใช้ http_close(req) + http_open() ใหม่ [O5]
    //   (ตัว req ยังไม่ free — ผู้ใช้ต้อง http_close() ตาม contract)
    if (err != ESP_OK || req->_mem_error) {
        if (req->_mem_error) {
            ESP_LOGE(TAG, "OOM during body receive");
        } else {
            ESP_LOGE(TAG, "perform failed: %s", esp_err_to_name(err));
        }
        _free_resp_body(req);
        _clear_req_hdrs(req);
        esp_http_client_cleanup(req->_client);
        req->_client = NULL;
        return NULL;
    }

    return req->_resp_body;   // borrowed pointer — อ่านได้จน http_close(req) [O3]
}

const char *http_get_header(const http_req_t *req, const char *key) {
    if (!req || !key) return NULL;
    for (int i = 0; i < req->_resp_hdr_count; i++) {
        if (strcasecmp(req->_resp_hdrs[i].key, key) == 0) {
            return req->_resp_hdrs[i].val;
        }
    }
    return NULL;
}

uint16_t http_get_code(const http_req_t *req) {
    if (!req) return 0;
    return (uint16_t)req->_status_code;
}

size_t http_get_body_len(const http_req_t *req) {
    if (!req) return 0;
    return req->_resp_body_len;
}

esp_err_t http_close(http_req_t *req) {
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    // ถ้า poll task ยังทำงานอยู่ → stop + รอจนจบก่อน
    // (ห้าม free req/client ขณะที่ poll task ยังอาจใช้งานมันอยู่) [O6]
    if (req->_poll_task) {
        esp_err_t r = _poll_stop_and_wait(req);
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "poll task did not finish in time — req NOT freed, retry later");
            return ESP_FAIL;
        }
    }

    _clear_req_hdrs(req);
    _free_resp_body(req);
    if (req->_client) {
        esp_http_client_cleanup(req->_client);
        req->_client = NULL;
    }
    if (req->_poll_done_sem) {
        vSemaphoreDelete(req->_poll_done_sem);
        req->_poll_done_sem = NULL;
    }
    if (req->_poll_wake_sem) {
        vSemaphoreDelete(req->_poll_wake_sem);
        req->_poll_wake_sem = NULL;
    }

    memset(req, 0, sizeof(http_req_t));
    free(req);
    return ESP_OK;
}

const char *http_fetch_json(http_req_t **out_req, const char *url,
                            esp_http_client_method_t method) {
    if (!out_req) {
        return NULL;
    }
    *out_req = NULL;

    http_req_t *req = http_open(url, method);
    if (!req) {
        return NULL;
    }

    http_set_header(req, "Accept", "application/json");
    http_set_header(req, "User-Agent", "ESP32 Client");

    const char *body = http_send(req);
    if (!body) {
        // fail → lib คืนทุกอย่างให้เอง ผู้เรียกไม่ต้องจัดการอะไรเพิ่ม
        http_close(req);
        return NULL;
    }

    // สำเร็จ → คืน handle ให้ผู้เรียก (ผู้เรียกต้อง http_close(req) หลังใช้เสร็จ)
    *out_req = req;
    return body;   // borrowed pointer — มีอายุถึง http_close(req) [O3]
}

// ════════════════════════════════════════════════════════════════════════════
//  Long-poll Implementation
// ════════════════════════════════════════════════════════════════════════════

static void _poll_task_fn(void *arg) {
    http_req_t *req = (http_req_t *)arg;

    while (!req->_poll_stop) {

        // ── เตรียม state สำหรับรอบใหม่ ─────────────────────────────────────
        _free_resp_body(req);
        req->_resp_hdr_count = 0;
        req->_status_code    = 0;
        req->_mem_error      = false;

        // ── เปิด connection ────────────────────────────────────────────────
        // ใช้ esp_http_client ที่ init ไว้ใน http_long_poll_start()
        if (req->_client) {
            _clear_req_hdrs(req);
            esp_http_client_set_url(req->_client, req->_poll_url);
            esp_http_client_set_method(req->_client, req->_poll_method);
        }

        // ── headers (ใส่ใหม่ทุก round เพราะ _clear_req_hdrs ทิ้งไปแล้ว) ─────
        if (req->_poll_headers && req->_client) {
            for (int i = 0; req->_poll_headers[i][0] != NULL; i++) {
                esp_err_t h = http_set_header(req,
                                              req->_poll_headers[i][0],
                                              req->_poll_headers[i][1]);
                if (h != ESP_OK) {
                    ESP_LOGW(TAG, "poll: set header '%s' failed (%s)",
                             req->_poll_headers[i][0], esp_err_to_name(h));
                }
            }
        }

        // ── body (POST/PUT) ────────────────────────────────────────────────
        if (req->_poll_body && req->_client) {
            http_set_body(req, req->_poll_body);
        }

        // ── ตั้ง active flags ก่อน perform ──────────────────────────────────
        req->_active_recv_hdr  = false;
        req->_active_recv_body = true;   // ต้องเปิด เพื่อให้ event handler ทำงาน

        // ── Perform: block จนกว่า server ปิด connection หรือ timeout ──────────
        //
        //  ระหว่าง block: HTTP_EVENT_ON_DATA จะ fire ทุก chunk
        //  → _event_handler เรียก _poll_cb ต่อ chunk ทันที
        //  → perform() return เมื่อ connection ปิดหรือ timeout
        //
        if (req->_client) {
            esp_err_t err = esp_http_client_perform(req->_client);
            req->_status_code = esp_http_client_get_status_code(req->_client);

            if (err != ESP_OK && !req->_poll_stop) {
                ESP_LOGW(TAG, "poll perform: %s (will reconnect)", esp_err_to_name(err));
                // cleanup แล้ว recreate ใน round ถัดไปเพื่อป้องกัน TLS state leak
                esp_http_client_cleanup(req->_client);
                req->_client = NULL;
            }
        }

        // ── ถ้า client ถูก cleanup ต้อง recreate ก่อน round ถัดไป ─────────────
        if (!req->_poll_stop && !req->_client) {
            esp_http_client_config_t cfg = {
                .url               = req->_poll_url,
                .method            = req->_poll_method,
                .event_handler     = _event_handler,
                .user_data         = req,
                .crt_bundle_attach = esp_crt_bundle_attach,
                .keep_alive_enable = true,
                .buffer_size       = 4096,
                .buffer_size_tx    = 2048,
                .timeout_ms        = (int)req->_poll_timeout_ms,
            };
            req->_client = esp_http_client_init(&cfg);
            if (!req->_client) {
                ESP_LOGE(TAG, "poll recreate client failed, stopping");
                break;
            }
        }

        // ── รอก่อน reconnect (ตื่นได้ทันทีเมื่อ http_long_poll_stop) ─────────
        if (!req->_poll_stop && req->_poll_reconnect_ms > 0) {
            if (req->_poll_wake_sem) {
                xSemaphoreTake(req->_poll_wake_sem,
                               pdMS_TO_TICKS(req->_poll_reconnect_ms));
            } else {
                vTaskDelay(pdMS_TO_TICKS(req->_poll_reconnect_ms));
            }
        }
    }

    // ── task จบ ───────────────────────────────────────────────────────────
    // ⚠ ห้าม free req ใน task (v1 เรียก http_close(req) ที่นี่
    //    → use-after-free / double-free ถ้าผู้เรียกยังถือ pointer อยู่)
    //    req เป็นของ caller — caller คืนผ่าน http_close() [O2]
    //
    //    ลำดับสำคัญ: give done "ก่อน" ตั้ง _poll_task = NULL
    //    → ใครก็ตามที่เห็น _poll_task == NULL (stop/close/is_running)
    //      ย่อมเห็น token done เรียบร้อยแล้ว — ไม่เกิด race ตอน free semaphore
    ESP_LOGI(TAG, "poll task stopped");
    if (req->_poll_done_sem) {
        xSemaphoreGive(req->_poll_done_sem);
    }
    req->_poll_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t http_long_poll_start(http_req_t *req, const http_poll_config_t *config) {
    if (!req || !config || !config->url || !config->on_data) {
        return ESP_ERR_INVALID_ARG;
    }
    if (req->_poll_task) {
        ESP_LOGE(TAG, "poll already running");
        return ESP_ERR_INVALID_STATE;
    }

    // ── บันทึก config (borrowed pointers — อยู่ได้ถึง task จบ [O4]) ─────────
    req->_poll_cb           = config->on_data;
    req->_poll_ctx          = config->user_ctx;
    req->_poll_url          = config->url;
    req->_poll_method       = config->method;
    req->_poll_headers      = config->headers;
    req->_poll_body         = config->body;
    req->_poll_reconnect_ms = config->reconnect_ms;
    req->_poll_stop         = false;
    req->_poll_timeout_ms   = config->request_timeout_ms
                              ? config->request_timeout_ms
                              : HTTP_REQ_TIMEOUT_MS;

    // ── semaphores (สร้างครั้งเดียว, reuse ทุกครั้ง, delete ใน http_close) ───────
    if (req->_poll_done_sem) {
        xSemaphoreTake(req->_poll_done_sem, 0);   // drain token เก่าจากรอบก่อน
    } else {
        req->_poll_done_sem = xSemaphoreCreateBinary();
    }
    if (req->_poll_wake_sem) {
        xSemaphoreTake(req->_poll_wake_sem, 0);   // drain wake token เก่า
    } else {
        req->_poll_wake_sem = xSemaphoreCreateBinary();
    }
    if (!req->_poll_done_sem || !req->_poll_wake_sem) {
        ESP_LOGE(TAG, "poll: OOM creating semaphores");
        return ESP_ERR_NO_MEM;
    }

    // ── Init client ด้วย timeout ที่ถูกต้อง ───────────────────────────────────
    if (req->_client) {
        esp_http_client_cleanup(req->_client);
        req->_client = NULL;
    }

    esp_http_client_config_t cfg = {
        .url               = config->url,
        .method            = config->method,
        .event_handler     = _event_handler,
        .user_data         = req,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
        .buffer_size       = 4096,
        .buffer_size_tx    = 2048,
        .timeout_ms        = (int)req->_poll_timeout_ms,
    };
    req->_client = esp_http_client_init(&cfg);
    if (!req->_client) {
        ESP_LOGE(TAG, "poll: esp_http_client_init failed");
        return ESP_FAIL;
    }

    // ── Spawn task ──────────────────────────────────────────────────────────
    uint32_t    stack = config->task_stack_size
                        ? config->task_stack_size
                        : HTTP_REQ_POLL_DEFAULT_STACK;
    UBaseType_t prio  = config->task_priority
                        ? config->task_priority
                        : HTTP_REQ_POLL_DEFAULT_PRIO;
    const char *name  = config->task_name ? config->task_name : "http_poll";

    BaseType_t ret = xTaskCreate(_poll_task_fn, name, stack, req, prio, &req->_poll_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "poll: xTaskCreate failed");
        esp_http_client_cleanup(req->_client);
        req->_client    = NULL;
        req->_poll_task = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "poll started → %s (timeout=%ums reconnect=%ums)",
             config->url, (unsigned)req->_poll_timeout_ms, (unsigned)config->reconnect_ms);
    return ESP_OK;
}

esp_err_t http_long_poll_stop(http_req_t *req) {
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!req->_poll_task) {
        ESP_LOGW(TAG, "poll not running");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = _poll_stop_and_wait(req);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "poll stopped");
    } else {
        ESP_LOGE(TAG, "poll stop timed out — task may still be running, do NOT http_close() yet");
    }
    return err;
}

bool http_long_poll_is_running(const http_req_t *req) {
    return req && req->_poll_task != NULL;
}
