// ════════════════════════════════════════════════════════════════════════════
//  esp32_http_req.c  —  HTTP(S) Request Library for ESP32 / ESP-IDF v6.0
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
                ESP_LOGE(TAG, "OOM: free=%u needed=%u max=%u",
                         esp_get_free_heap_size(), needed, HTTP_REQ_DEFAULT_MAX_BODY);
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
//  Standard API
// ════════════════════════════════════════════════════════════════════════════

void http_open(http_req_t *req, const char *url, esp_http_client_method_t method) {
    if (!req || !url) {
        ESP_LOGE(TAG, "http_open: invalid args");
        return;
    }

    _free_resp_body(req);
    req->_resp_hdr_count = 0;
    req->_status_code    = 0;
    req->_mem_error      = false;

    if (req->_client == NULL) {
        esp_http_client_config_t cfg = {
            .url               = url,
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
        }
    } else {
        _clear_req_hdrs(req);
        esp_http_client_set_url(req->_client, url);
        esp_http_client_set_method(req->_client, method);
    }
}

void http_set_header(http_req_t *req, const char *key, const char *value) {
    if (!req || !req->_client || !key || !value) return;
    esp_http_client_set_header(req->_client, key, value);
    if (req->_req_hdr_count < HTTP_REQ_MAX_REQ_HDRS) {
        req->_req_hdr_keys[req->_req_hdr_count++] = strdup(key);
    } else {
        ESP_LOGW(TAG, "req header tracking full, '%s' will not be auto-cleared", key);
    }
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

char *http_send(http_req_t *req) {
    if (!req || !req->_client) {
        ESP_LOGE(TAG, "http_send: call http_open() first");
        return NULL;
    }

    req->_active_recv_hdr  = req->_opts_set ? req->_recv_header : false;
    req->_active_recv_body = req->_opts_set ? req->_recv_body   : true;

    esp_err_t err = esp_http_client_perform(req->_client);
    req->_status_code = esp_http_client_get_status_code(req->_client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "perform failed: %s", esp_err_to_name(err));
        _free_resp_body(req);
        _clear_req_hdrs(req);
        esp_http_client_cleanup(req->_client);
        req->_client = NULL;
        return NULL;
    }

    if (req->_mem_error) {
        ESP_LOGE(TAG, "OOM during body receive");
        _free_resp_body(req);
        return NULL;
    }

    return req->_resp_body;
}

char *http_get_header(http_req_t *req, const char *key) {
    if (!req || !key) return NULL;
    for (int i = 0; i < req->_resp_hdr_count; i++) {
        if (strcasecmp(req->_resp_hdrs[i].key, key) == 0) {
            return req->_resp_hdrs[i].val;
        }
    }
    return NULL;
}

uint16_t http_get_code(http_req_t *req) {
    if (!req) return 0;
    return (uint16_t)req->_status_code;
}

void http_close(http_req_t *req) {
    if (!req) return;
    _clear_req_hdrs(req);
    _free_resp_body(req);
    if (req->_client) {
        esp_http_client_cleanup(req->_client);
        req->_client = NULL;
    }
    memset(req, 0, sizeof(http_req_t));
}

char *http_fetch_json(http_req_t *req, const char *url, esp_http_client_method_t method) {
    http_open(req, url, method);
    http_set_header(req, "Accept", "application/json");
    http_set_header(req, "User-Agent", "ESP32 Client");
    return http_send(req);
}

// ════════════════════════════════════════════════════════════════════════════
//  Long-poll Implementation
// ════════════════════════════════════════════════════════════════════════════

static void _poll_task_fn(void *arg) {
    http_req_t *req = (http_req_t *)arg;

    while (!req->_poll_stop) {

        // ── เปิด connection ───────────────────────────────────────────────────
        // ไม่เรียก http_open() เพราะ timeout ถูกตั้งตอน init แล้ว
        // ใช้ esp_http_client ที่ init ไว้ใน http_long_poll_start() โดยตรง
        _free_resp_body(req);
        req->_resp_hdr_count = 0;
        req->_status_code    = 0;
        req->_mem_error      = false;

        if (req->_client) {
            _clear_req_hdrs(req);
            esp_http_client_set_url(req->_client, req->_poll_url);
            esp_http_client_set_method(req->_client, req->_poll_method);
        }

        // ── ใส่ headers ────────────────────────────────────────────────────────
        if (req->_poll_headers && req->_client) {
            for (int i = 0; req->_poll_headers[i][0] != NULL; i++) {
                http_set_header(req,
                                req->_poll_headers[i][0],
                                req->_poll_headers[i][1]);
            }
        }

        // ── ใส่ body (POST/PUT) ────────────────────────────────────────────────
        if (req->_poll_body && req->_client) {
            http_set_body(req, req->_poll_body);
        }

        // ── ตั้ง active flags ก่อน perform ────────────────────────────────────
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
                .timeout_ms        = req->_poll_timeout_ms,
            };
            req->_client = esp_http_client_init(&cfg);
            if (!req->_client) {
                ESP_LOGE(TAG, "poll recreate client failed, stopping");
                break;
            }
        }

        // ── รอก่อน reconnect (interruptible โดย http_long_poll_stop) ──────────
        if (!req->_poll_stop && req->_poll_reconnect_ms > 0) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(req->_poll_reconnect_ms));
        }
    }

    ESP_LOGI(TAG, "poll task stopped");
    http_close(req);
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

    // ── บันทึก config ────────────────────────────────────────────────────────
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
        .timeout_ms        = req->_poll_timeout_ms,
    };
    req->_client = esp_http_client_init(&cfg);
    if (!req->_client) {
        return ESP_FAIL;
    }

    // ── Spawn task ───────────────────────────────────────────────────────────
    uint32_t    stack = config->task_stack_size
                        ? config->task_stack_size
                        : HTTP_REQ_POLL_DEFAULT_STACK;
    UBaseType_t prio  = config->task_priority
                        ? config->task_priority
                        : HTTP_REQ_POLL_DEFAULT_PRIO;
    const char *name  = config->task_name ? config->task_name : "http_poll";

    BaseType_t ret = xTaskCreate(_poll_task_fn, name, stack, req, prio, &req->_poll_task);
    if (ret != pdPASS) {
        esp_http_client_cleanup(req->_client);
        req->_client    = NULL;
        req->_poll_task = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "poll started → %s (timeout=%ums reconnect=%ums)",
             config->url, req->_poll_timeout_ms, config->reconnect_ms);
    return ESP_OK;
}

void http_long_poll_stop(http_req_t *req) {
    if (!req || !req->_poll_task) return;
    req->_poll_stop = true;
    xTaskNotifyGive(req->_poll_task);  // ตื่นจาก reconnect delay ทันที
    ESP_LOGI(TAG, "poll stop requested");
}

bool http_long_poll_is_running(const http_req_t *req) {
    return req && req->_poll_task != NULL;
}