#ifndef ESP32_WIFI_H
#define ESP32_WIFI_H

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

/* ── WiFi Status Codes ──────────────────────────────────────────── */
typedef enum {
    WIFI_STATUS_IDLE = 0,          /* ยังไม่ได้ connect */
    WIFI_STATUS_CONNECTING,        /* กำลังเชื่อมต่อ */
    WIFI_STATUS_CONNECTED,         /* เชื่อมต่ออยู่ */
    WIFI_STATUS_DISCONNECTED,      /* หลุดแล้ว (กำลังจะ reconnect) */
    WIFI_STATUS_FAIL,              /* ล้มเหลวถาวร (เช่น รหัสผ่านผิด) */
} wifi_status_t;

/* ── Callback สำหรับแจ้งเมื่อสถานะ WiFi เปลี่ยน ──────────────────── */
typedef void (*wifi_status_cb_t)(wifi_status_t status, void *user_data);

/* ── Public API ─────────────────────────────────────────────────── */

/**
 * @brief  กำหนดค่าคงที่ของ wifi_config_t สำหรับ ssid/password
 *         เรียกก่อน wifi_init() เพื่อส่งค่าเข้ามา
 *         ใช้ตัวแปร static เก็บไว้ reconnect จะได้ใช้ค่าเดิม
 */
void wifi_set_credentials(const char *ssid, const char *pwd);

/**
 * @brief  Init WiFi subsystem (เรียกครั้งเดียวตอน boot)
 *         init NVS, netif, event loop, WiFi driver
 *         หลัง init แล้วจะเริ่ม auto-connect เอง (สำหรับ WiFi ออกเน็ตและ ESP-NOW)
 */
esp_err_t wifi_init(void);

#ifndef CONFIG_IDF_TARGET_ESP32P4

/**
 * @brief  สั่ง Start WiFi สำหรับ ESP-NOW เท่านั้น
 */
esp_err_t wifi_espnow_init(bool long_range);

#endif

/**
 * @brief  ตรวจสอบสถานะการเชื่อมต่อปัจจุบัน
 */
wifi_status_t wifi_get_status(void);

/**
 * @brief  เช็คว่า WiFi เชื่อมต่ออยู่ไหม
 */
bool wifi_is_connected(void);

/**
 * @brief  ดึง RSSI (dBm) ยิ่งใกล้ 0 ยิ่งแรง
 * @retval RSSI หรือ 0 ถ้ายังไม่เชื่อมต่อ
 */
int8_t wifi_get_rssi(void);

/**
 * @brief  ดึง IP address เป็น string
 * @retval true ถ้ามี IP, false ถ้ายังไม่ได้รับ IP
 */
bool wifi_get_ip_str(char *buf, size_t buf_len);

/**
 * @brief  ตั้งค่า callback รับการแจ้งเตือนเมื่อสถานะเปลี่ยน
 */
void wifi_on_status_change(wifi_status_cb_t cb, void *user_data);

/* ── Reconnect Config ──────────────────────────────────────────── */

/**
 * @brief  กำหนดจำนวนครั้ง reconnect สูงสุด (default: 0 = ไม่จำกัด)
 */
void wifi_set_max_retry(uint32_t max);

/**
 * @brief  กำหนด reconnect base interval (default: 3000 ms)
 *         ระบบใช้ exponential backoff: base * 2^attempt, cap ที่ 60 วิ
 */
void wifi_set_reconnect_interval(uint32_t ms);

/* ── Power Save ────────────────────────────────────────────────── */

/**
 * @brief  ตั้ง Power Save Mode (default: WIFI_PS_MIN_MODEM)
 */
void wifi_set_ps(wifi_ps_type_t ps);

/* ── SNTP ──────────────────────────────────────────────────────── */

void wifi_sntp_sync(void);
uint8_t wifi_sntp_is_sync(void);

#endif // ESP32_WIFI_H