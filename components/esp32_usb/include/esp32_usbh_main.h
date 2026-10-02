/*
 * esp32_usbh_main.h — แกนกลางของ USB Host
 *
 *  esp32_usbh_init()   เปิดทุกโมดูล host ด้วยคำสั่งเดียว
 *  esp32_usbh_deinit() ปิดทั้งหมด
 *  esp32_usbh_set_*()  / esp32_usbh_get_*()
 *
 * 🔒 ทุกฟังก์ชันในไฟล์นี้ thread-safe
 * 🔔 callback ของผู้ใช้ "ไม่เคย" ถูกเรียกจาก ISR หรือจากเธรด hub
 *    ไลบรารีคัดลอกข้อมูลใส่คิวแล้วเรียกจาก event task ของตัวเอง
 */
#ifndef ESP32_USBH_MAIN_H
#define ESP32_USBH_MAIN_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp32_usb_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 *  ชนิดข้อมูล
 * ======================================================================== */

typedef enum {
    ESP32_USBH_STATE_STOPPED = 0,
    ESP32_USBH_STATE_STARTING,
    ESP32_USBH_STATE_RUNNING,
    ESP32_USBH_STATE_RESTARTING,
    ESP32_USBH_STATE_STOPPING,
} esp32_usbh_state_t;

typedef enum {
    ESP32_USBH_CLASS_UNKNOWN = 0,
    ESP32_USBH_CLASS_MSC,
    ESP32_USBH_CLASS_HID,
    ESP32_USBH_CLASS_HUB,
} esp32_usbh_class_t;

typedef enum {
    ESP32_USBH_SPEED_UNKNOWN = 0,
    ESP32_USBH_SPEED_LOW,
    ESP32_USBH_SPEED_FULL,
    ESP32_USBH_SPEED_HIGH,
} esp32_usbh_speed_t;

/** ข้อมูลอุปกรณ์หนึ่งตัว — เป็นสำเนา ปลอดภัยต่อการถอดสายกลางคัน */
typedef struct {
    bool               valid;
    esp32_usbh_class_t dev_class;
    esp32_usbh_speed_t speed;

    uint16_t vid;
    uint16_t pid;
    uint16_t bcd_device;
    uint8_t  dev_addr;
    uint8_t  hub_index;      /* 1 = root hub */
    uint8_t  hub_port;
    uint8_t  interface;
    uint8_t  tier;           /* ความลึกในต้นไม้ USB */

    char manufacturer[ESP32_USBH_STR_LEN];
    char product[ESP32_USBH_STR_LEN];
    char serial[ESP32_USBH_STR_LEN];
    char devname[CONFIG_USBHOST_DEV_NAMELEN];  /* /dev/sda, /dev/input0 ... */
} esp32_usbh_devinfo_t;

/* ---------------- เหตุการณ์ ---------------- */

typedef enum {
    ESP32_USBH_EV_DEVICE_ATTACHED = 0,
    ESP32_USBH_EV_DEVICE_DETACHED,
    ESP32_USBH_EV_ENUM_FAILED,
    ESP32_USBH_EV_HOST_RESTARTED,

    ESP32_USBH_EV_MSC_MOUNTED,
    ESP32_USBH_EV_MSC_UNMOUNTED,
    ESP32_USBH_EV_MSC_IO_ERROR,

    ESP32_USBH_EV_HID_ATTACHED,
    ESP32_USBH_EV_HID_DETACHED,
    ESP32_USBH_EV_HID_REPORT,
} esp32_usbh_event_id_t;

/* --- payload: MSC --- */
typedef struct {
    uint8_t  index;            /* 0..ESP32_USBH_MSC_MAX_DRIVES-1 */
    uint8_t  pdrv;             /* เลขไดรฟ์ของ FatFS */
    char     path[4];          /* "0:" "1:" ... */
    uint64_t capacity_bytes;
    uint32_t block_count;
    uint16_t block_size;
    int      error_code;       /* ใช้กับ EV_MSC_IO_ERROR */
} esp32_usbh_msc_evt_t;

/* --- payload: HID --- */
typedef enum {
    ESP32_USBH_HID_KIND_OTHER = 0,
    ESP32_USBH_HID_KIND_KEYBOARD,
    ESP32_USBH_HID_KIND_MOUSE,
} esp32_usbh_hid_kind_t;

typedef struct {
    int16_t dx, dy;            /* เมาส์ report-protocol ใช้ 12/16 บิต ต้องรองรับ */
    int8_t  wheel;
    uint8_t buttons;           /* bit0 L, bit1 R, bit2 M, bit3/4 ปุ่มข้าง */
} esp32_usbh_mouse_t;

typedef struct {
    uint8_t modifier;          /* bit0 LCtrl .. bit7 RGui */
    uint8_t keys[6];           /* HID usage id, 0 = ว่าง */
} esp32_usbh_kbd_t;

typedef struct {
    uint8_t               index;
    esp32_usbh_hid_kind_t kind;
    bool                  boot_protocol;
    uint8_t               raw[ESP32_USBH_HID_REPORT_SIZE];
    uint8_t               raw_len;
    union {
        esp32_usbh_mouse_t mouse;
        esp32_usbh_kbd_t   kbd;
    };
} esp32_usbh_hid_evt_t;

/** โครงสร้างเหตุการณ์ — ส่งเป็นค่าสำเนาทั้งก้อน ไม่มีพอยน์เตอร์ค้าง */
typedef struct {
    esp32_usbh_event_id_t id;
    esp32_usbh_devinfo_t  dev;
    union {
        esp32_usbh_msc_evt_t msc;
        esp32_usbh_hid_evt_t hid;
    };
} esp32_usbh_event_t;

/**
 * callback เหตุการณ์
 * @note ถูกเรียกจาก event task ของไลบรารี — "ไม่ใช่" ISR และ "ไม่ใช่" เธรด hub
 *       จึงบล็อก, พิมพ์ log, เรียก FatFS หรือจองหน่วยความจำได้ตามปกติ
 *       แต่ถ้าใช้เวลานานมากจะทำให้เหตุการณ์ถัดไปล่าช้า (คิวลึก
 *       ESP32_USBH_EVENT_QUEUE_LEN) ควรย้ายงานหนักไป task ของตัวเอง
 */
typedef void (*esp32_usbh_event_cb_t)(const esp32_usbh_event_t *ev, void *ctx);

/* ---------------- สถิติ ---------------- */
typedef struct {
    uint32_t attach_count;
    uint32_t detach_count;
    uint32_t enum_fail_count;
    uint32_t host_restart_count;
    uint32_t vbus_cycle_count;
    uint32_t event_drop_count;   /* คิวเต็ม */
} esp32_usbh_stats_t;

/* ---------------- ค่าเริ่มต้นตอน init ---------------- */
typedef struct {
    esp32_usbh_event_cb_t event_cb;   /* ใส่ NULL ได้ แล้วค่อย set ทีหลัง */
    void                 *event_ctx;
    bool                  auto_mount; /* override ESP32_USBH_MSC_AUTO_MOUNT */
    bool                  fetch_strings;
} esp32_usbh_cfg_t;

#define ESP32_USBH_CFG_DEFAULT()                       \
    (esp32_usbh_cfg_t){                                \
        .event_cb      = NULL,                         \
        .event_ctx     = NULL,                         \
        .auto_mount    = (ESP32_USBH_MSC_AUTO_MOUNT),  \
        .fetch_strings = (ESP32_USBH_FETCH_STRINGS),   \
    }

/* ========================================================================
 *  API
 * ======================================================================== */

/**
 * เปิด USB host ทั้งระบบ (core + MSC + HID ตามที่คอมไพล์ไว้)
 * @param cfg  NULL = ใช้ค่า default ทั้งหมด
 * @return ESP_OK, ESP_ERR_INVALID_STATE (เปิดอยู่แล้ว), ESP_ERR_NO_MEM, ESP_FAIL
 */
esp_err_t esp32_usbh_init(const esp32_usbh_cfg_t *cfg);

/** ปิดทั้งระบบ unmount ทุกไดรฟ์ หยุดทุก task และ deinit คอนโทรลเลอร์ */
esp_err_t esp32_usbh_deinit(void);

/** รีสตาร์ท host stack (DWC2 soft reset) โดยไม่รีบูตเครื่อง */
esp_err_t esp32_usbh_restart(void);

/* ---------------- set ---------------- */

esp_err_t esp32_usbh_set_event_cb(esp32_usbh_event_cb_t cb, void *ctx);
esp_err_t esp32_usbh_set_log_level(esp_log_level_t level);
/** เปิด/ปิดไฟ VBUS (ต้องตั้ง ESP32_USBH_VBUS_GPIO ไว้) */
esp_err_t esp32_usbh_set_vbus(bool on);
/** เปิด/ปิดการดึง string descriptor ตอนอุปกรณ์เสียบ */
esp_err_t esp32_usbh_set_fetch_strings(bool enable);

/* ---------------- get ---------------- */

esp32_usbh_state_t esp32_usbh_get_state(void);
/** จำนวนอุปกรณ์ที่อยู่ในตารางตอนนี้ */
int  esp32_usbh_get_device_count(void);
/** ดึงข้อมูลอุปกรณ์ลำดับที่ index (0-based) — คืนสำเนา ปลอดภัยเสมอ */
esp_err_t esp32_usbh_get_device_info(int index, esp32_usbh_devinfo_t *out);
/** ค้นหาด้วย VID/PID (คืนตัวแรกที่เจอ) */
esp_err_t esp32_usbh_get_device_by_vidpid(uint16_t vid, uint16_t pid,
                                          esp32_usbh_devinfo_t *out);
esp_err_t esp32_usbh_get_stats(esp32_usbh_stats_t *out);
/** เพดาน sector/คำสั่งที่ฮาร์ดแวร์รองรับจริง (อ่านจาก GHWCFG3) */
uint32_t  esp32_usbh_get_hw_max_sectors(void);
const char *esp32_usbh_speed_str(esp32_usbh_speed_t s);
const char *esp32_usbh_class_str(esp32_usbh_class_t c);
const char *esp32_usbh_event_str(esp32_usbh_event_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* ESP32_USBH_MAIN_H */
