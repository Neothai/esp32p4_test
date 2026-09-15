#include "esp32_wifi.h"
#include "esp_netif_sntp.h"
#include "nvs_flash.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "string.h"

#define TAG "ESP32_WIFI"

/* ═══════════════════════════════════════════════════════════════════
 *  Reconnect Config
 * ═══════════════════════════════════════════════════════════════════ */
#define RECONNECT_BASE_MS   3000    /* เริ่มจาก 3 วิ */
#define RECONNECT_CAP_MS    60000   /* สูงสุด 60 วิ */
#define DEFAULT_MAX_RETRY   0       /* 0 = ไม่จำกัด */

/* ═══════════════════════════════════════════════════════════════════
 *  Internal State
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Credentials (เก็บไว้ reconnect ใช้) ───────────────────────── */
static char _sta_ssid[32]  = {0};
static char _sta_pass[64]  = {0};

/* ── Status ─────────────────────────────────────────────────────── */
static wifi_status_t       _status        = WIFI_STATUS_IDLE;
static esp_netif_ip_info_t _ip_info;
static int                 _rssi          = 0;
static bool                _initialized   = false;

/* ── Reconnect ──────────────────────────────────────────────────── */
static uint32_t    _retry_count     = 0;
static uint32_t    _max_retry       = DEFAULT_MAX_RETRY;
static uint32_t    _base_interval   = RECONNECT_BASE_MS;
static TimerHandle_t _reconn_timer  = NULL;
static bool        _stop_reconnect  = false;  /* true = user หยุดแล้ว */

/* ── Sync ───────────────────────────────────────────────────────── */
static SemaphoreHandle_t _mutex      = NULL;
static EventGroupHandle_t _evg       = NULL;

#define EVT_CONNECTED BIT0
#define EVT_FAIL      BIT1

/* ── Callback ───────────────────────────────────────────────────── */
static wifi_status_cb_t _status_cb = NULL;
static void            *_status_cb_ctx = NULL;

/* ═══════════════════════════════════════════════════════════════════
 *  Internal Helpers
 * ═══════════════════════════════════════════════════════════════════ */

static void _set_status(wifi_status_t s) {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    _status = s;
    if (_mutex) xSemaphoreGive(_mutex);
    if (_status_cb) _status_cb(s, _status_cb_ctx);
}

static uint32_t _backoff_ms(void) {
    if (_retry_count == 0) return _base_interval;
    /* base * 2^attempt, cap ที่ RECONNECT_CAP_MS */
    uint32_t ms = _base_interval;
    for (uint32_t i = 0; i < _retry_count && ms < RECONNECT_CAP_MS; i++) {
        uint32_t next = ms * 2;
        if (next < ms) { ms = RECONNECT_CAP_MS; break; }  /* overflow */
        ms = next;
        if (ms > RECONNECT_CAP_MS) ms = RECONNECT_CAP_MS;
    }
    return ms;
}

/* ── แปลง disconnect reason เป็น string ──────────────────────── */
static const char *_disc_reason_str(uint8_t r) {
    switch (r) {
        case WIFI_REASON_BEACON_TIMEOUT:           return "BEACON_TIMEOUT (AP unreachable)";
        case WIFI_REASON_NO_AP_FOUND:              return "NO_AP_FOUND";
        case WIFI_REASON_AUTH_FAIL:                return "AUTH_FAIL (wrong password)";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:   return "4WAY_HANDSHAKE_TIMEOUT";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:        return "HANDSHAKE_TIMEOUT";
        case WIFI_REASON_CONNECTION_FAIL:          return "CONNECTION_FAIL";
        case WIFI_REASON_ASSOC_FAIL:               return "ASSOC_FAIL";
        case WIFI_REASON_MIC_FAILURE:              return "MIC_FAILURE";
        default: {
            static char buf[16];
            snprintf(buf, sizeof(buf), "reason_%d", r);
            return buf;
        }
    }
}

/* ── ตรวจว่า reason นี้ควร retry ไหม ─────────────────────────── */
static bool _should_retry(uint8_t reason) {
    /* พวกนี้ retry ไม่ได้ หยุดทันที */
    if (reason == WIFI_REASON_AUTH_FAIL) return false;
    /* พวกนี้ retry ได้ หรือไม่แน่ใจ */
    return true;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Reconnect Timer
 * ═══════════════════════════════════════════════════════════════════ */

static void IRAM_ATTR _reconn_timer_cb(TimerHandle_t t) {
    if (_stop_reconnect) return;

    if (_max_retry > 0 && _retry_count >= _max_retry) {
        ESP_LOGW(TAG, "Reconnect gave up after %lu attempts", (unsigned long)_retry_count);
        _set_status(WIFI_STATUS_FAIL);
        return;
    }

    ESP_LOGI(TAG, "Reconnecting... (attempt #%lu)", (unsigned long)(_retry_count + 1));
    _retry_count++;
    _set_status(WIFI_STATUS_CONNECTING);

    /* ⚠️ ใช้ esp_err_t return แทน ESP_ERROR_CHECK!
     * ถ้ามี race condition จะได้ไม่ crash */
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "esp_wifi_connect returned: %s", esp_err_to_name(ret));
        /* ถ้า error ก็ไม่ abort — ตั้ง timer ลองอีกครั้ง */
    }
}

static void _start_reconn_timer(void) {
    if (_stop_reconnect) return;
    uint32_t ms = _backoff_ms();
    ESP_LOGI(TAG, "Next reconnect in %lu ms", (unsigned long)ms);
    xTimerChangePeriod(_reconn_timer, pdMS_TO_TICKS(ms), 0);
    xTimerStart(_reconn_timer, 0);
}

static void _stop_reconn_timer(void) {
    if (_reconn_timer) xTimerStop(_reconn_timer, 0);
    _retry_count = 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  WiFi Event Handler
 * ═══════════════════════════════════════════════════════════════════ */

static void _wifi_event_handler(void *arg, esp_event_base_t ev, int32_t id, void *data) {

    /* ── STA Started → เริ่ม connect ───────────────────────────── */
    if (ev == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA started, connecting to \"%s\"...", _sta_ssid);
        _retry_count = 0;
        _set_status(WIFI_STATUS_CONNECTING);
        esp_wifi_connect();
    }

    /* ── STA Disconnected ───────────────────────────────────────── */
    else if (ev == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "Disconnected: %s", _disc_reason_str(d->reason));

        /* ตรวจ reason: ถ้าเป็น auth_fail หยุดเลย */
        if (!_should_retry(d->reason)) {
            ESP_LOGE(TAG, "Permanent failure — will NOT retry. Check SSID/password.");
            _stop_reconn_timer();
            _set_status(WIFI_STATUS_FAIL);
            return;
        }

        _set_status(WIFI_STATUS_DISCONNECTED);

        /* เริ่ม reconnect timer (ไม่ connect ทันที!) */
        _start_reconn_timer();
    }

    /* ── STA Connected (link level, ยังไม่มี IP) ──────────────── */
    else if (ev == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WiFi link established");
    }

    /* ── Got IP ─────────────────────────────────────────────────── */
    else if (ev == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        memcpy(&_ip_info, &e->ip_info, sizeof(_ip_info));
        _stop_reconn_timer();
        _set_status(WIFI_STATUS_CONNECTED);
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&_ip_info.ip));
        if (_evg) xEventGroupSetBits(_evg, EVT_CONNECTED);
    }

    /* ── Debug log ──────────────────────────────────────────────── */
    if (ev == WIFI_EVENT) {
        ESP_LOGD(TAG, "WIFI_EVENT id=%ld", id);
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  SNTP Callback
 * ═══════════════════════════════════════════════════════════════════ */

static void _sntp_sync_cb(struct timeval *tv) {
  ESP_LOGI(TAG, "Time has been synchronized!");

  // ตั้งค่า Timezone ทันทีที่ซิงค์เสร็จ
  setenv("TZ", "ICT-7", 1);
  tzset();

  time_t now = tv->tv_sec;
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char strftime_buf[64];
  strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
  ESP_LOGI(TAG, "Current time: %s", strftime_buf);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════ */

void wifi_set_credentials(const char *ssid, const char *pwd) {
    if (ssid) {
        strncpy(_sta_ssid, ssid, sizeof(_sta_ssid) - 1);
        _sta_ssid[sizeof(_sta_ssid) - 1] = '\0';
    }
    if (pwd) {
        strncpy(_sta_pass, pwd, sizeof(_sta_pass) - 1);
        _sta_pass[sizeof(_sta_pass) - 1] = '\0';
    }
    ESP_LOGI(TAG, "Credentials set: SSID=\"%s\"", _sta_ssid);
}


esp_err_t wifi_init(void) {
    esp_err_t ret;

    // ── NVS ──────────────────────────────────────────────────────
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // ── Netif + Event Loop ───────────────────────────────────────
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_netif_create_default_wifi_sta();

    // เพิ่มการรอ/หน่วงเวลาเล็กน้อยเพื่อให้ Hosted Link กับ C6 พร้อมสมบูรณ์ 
    vTaskDelay(pdMS_TO_TICKS(500));

    // ── WiFi Driver ──────────────────────────────────────────────
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // ── Event Handlers ───────────────────────────────────────────
    esp_event_handler_instance_t h1, h2;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, _wifi_event_handler, NULL, &h1);
    esp_event_handler_instance_register(IP_EVENT,  IP_EVENT_STA_GOT_IP, _wifi_event_handler, NULL, &h2);

    // ── WiFi Config (ใช้ credentials ที่ set ไว้) ───────────────
    if (_sta_ssid[0] == '\0') {
        ESP_LOGE(TAG, "No SSID set! Call wifi_set_credentials() first.");
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t wifi_config = {0};
    strcpy((char *)wifi_config.sta.ssid, _sta_ssid);
    strcpy((char *)wifi_config.sta.password, _sta_pass);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    wifi_config.sta.bssid_set = false;  

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    //esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_wifi_set_ps(WIFI_PS_NONE);

    // ── Internal Resources ─────────────────────────────────────── 
    _evg = xEventGroupCreate();
    _mutex = xSemaphoreCreateMutex();
    _stop_reconnect = false;

    _reconn_timer = xTimerCreate("wifi_rc", pdMS_TO_TICKS(_base_interval),
                                  pdFALSE, NULL, _reconn_timer_cb);

    // ── Start WiFi ─────────────────────────────────────────────── 
    esp_wifi_start();

    _initialized = true;
    ESP_LOGI(TAG, "WiFi initialized — auto-reconnect enabled");
    return ESP_OK;
}


#ifndef CONFIG_IDF_TARGET_ESP32P4

esp_err_t wifi_espnow_init(bool long_range) {
  /* ── NVS ────────────────────────────────────────────────────── */
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    ret = nvs_flash_init();
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

  if (long_range)
    ESP_ERROR_CHECK(esp_wifi_set_protocol(
        WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N |
                         WIFI_PROTOCOL_LR));

  return ESP_OK;
}

#endif

wifi_status_t wifi_get_status(void) {
    wifi_status_t s;
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    s = _status;
    if (_mutex) xSemaphoreGive(_mutex);
    return s;
}

bool wifi_is_connected(void) {
    return wifi_get_status() == WIFI_STATUS_CONNECTED;
}

int8_t wifi_get_rssi(void) {
    esp_wifi_sta_get_rssi(&_rssi);
    return _rssi;
}

bool wifi_get_ip_str(char *buf, size_t len) {
    if (!wifi_is_connected() || !buf || len < 16) return false;
    snprintf(buf, len, IPSTR, IP2STR(&_ip_info.ip));
    return true;
}

void wifi_on_status_change(wifi_status_cb_t cb, void *ctx) {
    _status_cb = cb;
    _status_cb_ctx = ctx;
}

void wifi_set_max_retry(uint32_t max) {
    _max_retry = max;
    ESP_LOGI(TAG, "Max retry set to %lu (0=unlimited)", (unsigned long)max);
}

void wifi_set_reconnect_interval(uint32_t ms) {
    _base_interval = ms;
    ESP_LOGI(TAG, "Reconnect base interval set to %lu ms", (unsigned long)ms);
}

void wifi_set_ps(wifi_ps_type_t ps) {
    esp_wifi_set_ps(ps);
}

/* ═══════════════════════════════════════════════════════════════════
 *  SNTP
 * ═══════════════════════════════════════════════════════════════════ */

void wifi_sntp_sync() {
  ESP_LOGI(TAG, "Initializing SNTP...");

  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("th.pool.ntp.org");
  config.server_from_dhcp = false;  // ไม่ใช้จาก DHCP (ถ้าอยากระบุเอง)
  config.index_of_first_server = 0; // เริ่มที่ตัวแรก
  config.sync_cb = _sntp_sync_cb;

  esp_netif_sntp_init(&config);
  esp_sntp_setservername(1, "time.google.com");
  esp_sntp_setservername(2, "time.windows.com");
  esp_sntp_setservername(3, "asia.pool.ntp.org");

  ESP_LOGI(TAG, "SNTP Service started.");
}

uint8_t wifi_sntp_is_sync(void) {
    return sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
}