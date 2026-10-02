# esp32_usb — ไลบรารี USB สำหรับ ESP32-P4 + CherryUSB v1.6.1

ไลบรารีทางการสำหรับโปรเจกต์นี้ ครอบ CherryUSB ไว้ทั้งก้อน แล้วเปิดให้ใช้ด้วย API
ชุดเดียวที่ **thread-safe ทุกฟังก์ชัน**, **callback ไม่เคยถูกเรียกจาก ISR**,
และ **รองรับ hot-plug เต็มรูปแบบ**

```
components/esp32_usb/
├── CMakeLists.txt
├── README.md
├── include/                      ← ผู้ใช้ include ได้
│   ├── esp32_usb.h               หัวไฟล์รวม (include ตัวเดียวพอ)
│   ├── esp32_usb_config.h        ⚙️ ค่าคงที่ PUBLIC — แก้ได้
│   ├── esp32_usbh_main.h         แกนกลาง
│   ├── esp32_usbh_msc.h          แฟลชไดรฟ์ / ฮาร์ดดิสก์
│   └── esp32_usbh_hid.h          เมาส์ / คีย์บอร์ด
├── private/
│   └── esp32_usb_private.h       🔒 ค่าคงที่ PRIVATE — ห้ามแก้ ห้าม include
├── esp32_usbh_main.c
├── esp32_usbh_msc.c
└── esp32_usbh_hid.c
```

---

## 1. กฎการตั้งชื่อ

| รูปแบบ | ความหมาย |
|---|---|
| `esp32_usbh_...` | ฝั่ง **host** (h = host) |
| `esp32_usbd_...` | ฝั่ง **device** (d = device) — เว้นที่ไว้สำหรับอนาคต |
| `esp32_usbh_init()` / `esp32_usbh_deinit()` | เปิด/ปิด **ทั้งระบบ** ด้วยคำสั่งเดียว |
| `esp32_usbh_set_xxx()` / `esp32_usbh_get_xxx()` | ตั้งค่า / อ่านค่า |
| `esp32_usbh_<module>_set_xxx()` | ของเฉพาะโมดูล เช่น `esp32_usbh_msc_set_auto_mount()` |
| `priv_...` | ภายในไลบรารี ไม่ถูก export |

`esp32_usbh_init()` อยู่ใน `usbh_main` และเป็นคนเรียก `priv_msc_start()` /
`priv_hid_start()` ให้เอง — เรียกครั้งเดียวได้ครบทุกโมดูล

---

## 2. เริ่มใช้งาน 10 บรรทัด

```c
#include "esp32_usb.h"

static void on_usb(const esp32_usbh_event_t *ev, void *ctx)
{
    /* ⚠️ ฟังก์ชันนี้รันบน event task ของไลบรารี ไม่ใช่ ISR ไม่ใช่เธรด hub
     *    บล็อกได้ พิมพ์ log ได้ เรียก f_open() ได้ตามสบาย */
    switch (ev->id) {
    case ESP32_USBH_EV_MSC_MOUNTED:
        ESP_LOGI("app", "ไดรฟ์ %s พร้อม (%llu MB) — %s",
                 ev->msc.path, ev->msc.capacity_bytes >> 20, ev->dev.product);
        break;
    case ESP32_USBH_EV_HID_REPORT:
        if (ev->hid.kind == ESP32_USBH_HID_KIND_MOUSE)
            ESP_LOGI("app", "เมาส์ dx=%d dy=%d btn=%02X",
                     ev->hid.mouse.dx, ev->hid.mouse.dy, ev->hid.mouse.buttons);
        break;
    default:
        ESP_LOGI("app", "%s", esp32_usbh_event_str(ev->id));
        break;
    }
}

void app_main(void)
{
    esp32_usbh_cfg_t cfg = ESP32_USBH_CFG_DEFAULT();
    cfg.event_cb = on_usb;
    ESP_ERROR_CHECK(esp32_usbh_init(&cfg));
}
```

เท่านี้: เสียบแฟลชไดรฟ์ → mount อัตโนมัติเป็น `"0:"`, `"1:"`, …
เสียบเมาส์/คีย์บอร์ด → report ไหลเข้ามาที่ callback เดียวกัน
ถอดสายกลางคัน → unmount + ปล่อยทรัพยากรเอง เสียบใหม่ได้ทันที

---

## 3. ความปลอดภัยของ callback (ข้อกำหนดข้อ 2)

CherryUSB เรียกเราจาก 3 บริบทที่อันตราย:

| บริบท | ตัวอย่าง | ข้อห้าม |
|---|---|---|
| ISR | complete callback ของ URB แบบ async | ห้ามบล็อก ห้ามจองหน่วยความจำ |
| เธรด hub (prio 24) | `usbh_event_handler`, `usbh_msc_run/stop`, `usbh_hid_run/stop` | ห้ามบล็อกนาน มิฉะนั้น enumerate ค้างทั้งบัส |
| เธรด DWC2 | — | เหมือนกัน |

ไลบรารีจัดการให้ทั้งหมด:

```
ISR / เธรด hub  ──► คัดลอกข้อมูลใส่ esp32_usbh_event_t (ไม่มีพอยน์เตอร์ค้าง)
                 └► xQueueSend / xQueueSendFromISR  (ไม่บล็อก คิวเต็มก็ทิ้งแล้วนับสถิติ)
                         │
                         ▼
                  event task (prio 6)  ──► เรียก callback ของผู้ใช้
```

* โครงสร้าง `esp32_usbh_event_t` เป็น **ค่าสำเนาทั้งก้อน** (ชื่อรุ่น, raw report,
  ข้อมูลไดรฟ์) จึงไม่มีทาง dangling แม้อุปกรณ์จะถูกถอดไปแล้วตอน callback ทำงาน
* งานที่บล็อกนาน (restart host, cycle VBUS, `usbh_deinitialize`) อยู่บน
  **supervisor task** แยกอีกตัว (prio 7) ไม่ไปขวาง event task

---

## 4. ค่าคงที่ PUBLIC / PRIVATE (ข้อกำหนดข้อ 4)

### PUBLIC → `include/esp32_usb_config.h`

แก้ได้ 2 ทาง: แก้ไฟล์ตรง ๆ หรือ `-D` / `add_compile_definitions()` จาก CMake
(ทุกค่าครอบด้วย `#ifndef`) ทุกค่ามี `_Static_assert` ตรวจช่วงตอนคอมไพล์

ตัวอย่างค่าที่ใช้บ่อย:

| มาโคร | ค่าเริ่มต้น | ช่วงที่ยอมรับ | หมายเหตุ |
|---|---|---|---|
| `ESP32_USBH_MAX_DEVICES` | 8 | 1–32 | ขนาดตารางอุปกรณ์ |
| `ESP32_USBH_EVENT_QUEUE_LEN` | 24 | 4–128 | คิว event |
| `ESP32_USBH_STR_LEN` | 40 | 8–128 | ความยาวชื่อรุ่น/ผู้ผลิต |
| `ESP32_USBH_TASK_CORE` | -1 | -1,0,1 | -1 = ไม่ปักคอร์ |
| `ESP32_USBH_VBUS_GPIO` | -1 | -1..48 | -1 = ไม่ควบคุม VBUS |
| `ESP32_USBH_MSC_MAX_SEC_PER_CMD` | **64** | 1–127 | ⚠️ ดูข้อ 8 |
| `ESP32_USBH_MSC_BOUNCE_SECTORS` | 64 | 8–128 | bounce buffer **ต่อไดรฟ์** |
| `ESP32_USBH_MSC_MAX_DRIVES` | `CONFIG_USBHOST_MAX_MSC_CLASS` | 1–8 | ผูกกับ `usb_config.h` |
| `ESP32_USBH_MSC_AUTO_MOUNT` | 1 | 0/1 | |
| `ESP32_USBH_HID_MAX_DEVICES` | `CONFIG_USBHOST_MAX_HID_CLASS` | 1–16 | นับเป็น *interface* |
| `ESP32_USBH_HID_FORCE_BOOT` | 1 | 0/1 | บังคับ boot protocol |

ถ้าใส่ค่าเกินช่วง จะ **คอมไพล์ไม่ผ่าน** พร้อมข้อความบอกช่วงที่ถูกต้อง
ส่วนค่าที่ตั้งตอนรันไทม์ (`esp32_usbh_msc_set_max_sectors_per_cmd()`) จะ **clamp**
ให้อยู่ในช่วงแล้วคืน `ESP_ERR_INVALID_ARG` เพื่อบอกว่าโดนปรับ

### PRIVATE → `private/esp32_usb_private.h`

อยู่ใน `PRIV_INCLUDE_DIRS` โค้ดผู้ใช้ `#include` ไม่ได้เลย (compile error)
เก็บพวก `PRIV_HARD_MAX_SEC_PER_CMD 127`, `PRIV_EP0_MUTEX_WAIT_MS`,
`PRIV_BOT_RESET_REQUEST 0xFF`, `PRIV_STOP_JOIN_TIMEOUT_MS`, …

---

## 5. ข้อมูลระบบที่อ่าน/เขียนได้ (ข้อกำหนดข้อ 3)

```c
/* อ่านรายการอุปกรณ์ทั้งหมด */
for (int i = 0; i < esp32_usbh_get_device_count(); i++) {
    esp32_usbh_devinfo_t d;
    esp32_usbh_get_device_info(i, &d);
    printf("%04X:%04X  %s / %s  S/N %s  %s  tier %u  hub %u port %u  %s\n",
           d.vid, d.pid, d.manufacturer, d.product, d.serial,
           esp32_usbh_class_str(d.dev_class), d.tier, d.hub_index, d.hub_port,
           esp32_usbh_speed_str(d.speed));
}

esp32_usbh_devinfo_t d;
esp32_usbh_get_device_by_vidpid(0x0bda, 0x9210, &d);

esp32_usbh_stats_t st;  esp32_usbh_get_stats(&st);   /* attach/detach/enum_fail/restart/drop */
esp32_usbh_get_state();                              /* RUNNING / RESTARTING / ... */
esp32_usbh_get_hw_max_sectors();                     /* อ่านจาก GHWCFG3 จริง */

/* เขียน */
esp32_usbh_set_event_cb(cb, ctx);
esp32_usbh_set_log_level(ESP_LOG_WARN);
esp32_usbh_set_vbus(false);          /* ตัดไฟพอร์ต */
esp32_usbh_set_fetch_strings(false); /* ปิดการดึงชื่อรุ่น ให้ enumerate เร็วขึ้น */
esp32_usbh_restart();                /* soft reset ทั้ง stack โดยไม่รีบูต */
```

**MSC**

```c
esp32_usbh_msc_info_t mi;
esp32_usbh_msc_get_info(0, &mi);   /* path, pdrv, product, block_count/size, free/total, mounted */
esp32_usbh_msc_get_count();  esp32_usbh_msc_get_mounted_count();
esp32_usbh_msc_get_index_by_path("1:");
esp32_usbh_msc_get_stats(0, &ms);  /* read/write bytes, retries, BOT reset, timeout */
esp32_usbh_msc_set_max_sectors_per_cmd(64);
esp32_usbh_msc_set_auto_mount(false);
esp32_usbh_msc_mount(0);  esp32_usbh_msc_unmount(0);
esp32_usbh_msc_read(0, lba, buf, nsec);   /* ข้าม FatFS ไปอ่าน sector ตรง ๆ */
```

**HID**

```c
int kb = esp32_usbh_hid_get_index_by_kind(ESP32_USBH_HID_KIND_KEYBOARD);
esp32_usbh_hid_set_leds(kb, ESP32_USBH_KBD_LED_CAPSLOCK);
esp32_usbh_hid_set_protocol(kb, false);      /* กลับไป report protocol */
esp32_usbh_hid_set_poll_interval(kb, 8);     /* บังคับคาบ 8 ms */
esp32_usbh_hid_get_report_descriptor(kb, buf, sizeof(buf));
char c = esp32_usbh_hid_keycode_to_char(ev->hid.kbd.keys[0], shift);
```

---

## 6. Thread safety (ข้อกำหนดข้อ 5)

| ทรัพยากร | กลไก |
|---|---|
| ตารางอุปกรณ์ | `dev_mtx` (mutex) |
| สถิติ | `portMUX` (spinlock สั้น ๆ ปลอดภัยกับ ISR) |
| **EP0 / control transfer** | `priv_ep0_lock()` — mutex ระดับไลบรารี |
| ไดรฟ์ MSC แต่ละตัว | recursive mutex + bounce buffer + spinlock สถิติ **ของใครของมัน** |
| HID แต่ละตัว | buffer + mutex ของตัวเอง |
| FatFS | `FF_FS_REENTRANT` — ดูข้อ 7 |

### ทำไมต้องมี `priv_ep0_lock()`

`usbh_control_transfer()` ล็อกแค่ `hport->mutex` ซึ่งเป็น **ต่ออุปกรณ์**
แต่ `usbh_core.c:21` ใช้ `ep0_request_buffer[busid]` ซึ่งเป็น buffer **ต่อบัส**
ที่ทุกอุปกรณ์ใช้ร่วมกัน → ถ้าเราอ่าน string descriptor ขณะที่เธรด hub กำลัง
enumerate อุปกรณ์อื่น ข้อมูลจะปนกัน
**control transfer ทุกครั้งที่ไลบรารีนี้เป็นคนเริ่ม จึงถือ mutex ตัวนี้เสมอ**

### การ "join" ตอนถอดสาย (จุดที่ crash ง่ายที่สุด)

`usbh_msc_disconnect()` เรียก `usbh_msc_stop()` แล้ว **`usbh_msc_class_free()` ทันที**
(HID ก็เหมือนกัน `usbh_hid.c:283`) ถ้า task ของเรายังถือพอยน์เตอร์อยู่ = use-after-free
ไลบรารีจึงทำ 2 ขั้น:

1. `d->valid = false` (volatile) → ตัด I/O ใหม่ทันที
2. วนรอจน `io_busy == 0` (สูงสุด `PRIV_STOP_JOIN_TIMEOUT_MS` = 800 ms) ค่อย return

---

## 7. ⚠️ ต้องแก้ `components/esp32_fatfs/` ด้วย

ตอบคำถามที่ถามมาตรง ๆ: **ใช่ ต้องแก้ `ffconf.h`** ไม่งั้นเขียน 2 ไดรฟ์พร้อมกัน
FatFS จะพังที่ตัวมันเอง (ไลบรารีนี้ป้องกันได้แค่ชั้น block device)

### 7.1 `ffconf.h`

```c
#define FF_FS_REENTRANT   1      /* เดิม 0 — ⭐ ตัวสำคัญที่สุด */
#define FF_FS_TIMEOUT     5000   /* tick ที่รอ mutex (ควร >= USB timeout) */
#define FF_VOLUMES        4      /* ต้อง >= ESP32_USBH_MSC_MAX_DRIVES และ >= MAX_MSC_CLASS */
#define FF_FS_LOCK        8      /* กันไฟล์เดียวกันถูกเปิดซ้ำซ้อน; 0 = ปิด */
```

`FF_FS_REENTRANT 1` ให้ **mutex ต่อ volume** ผลคือ

* คนละไดรฟ์ → **ทำงานขนานกันได้จริง** ตามที่ต้องการ
* ไดรฟ์เดียวกัน → เข้าคิวทีละคน (ซึ่งถูกต้อง FAT ใช้ FAT table ร่วมกัน)

### 7.2 `ffsystem.c` — ต้องมี 4 ฟังก์ชันนี้

เมื่อเปิด `FF_FS_REENTRANT` FatFS จะเรียก `ff_mutex_*` ถ้าไม่มีจะลิงก์ไม่ผ่าน

```c
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_ff_mutex[FF_VOLUMES + 1];   /* +1 = volume lock ของระบบ */

int ff_mutex_create(int vol)
{
    s_ff_mutex[vol] = xSemaphoreCreateMutex();
    return (s_ff_mutex[vol] != NULL);
}

void ff_mutex_delete(int vol)
{
    if (s_ff_mutex[vol]) { vSemaphoreDelete(s_ff_mutex[vol]); s_ff_mutex[vol] = NULL; }
}

int ff_mutex_take(int vol)
{
    return (xSemaphoreTake(s_ff_mutex[vol], pdMS_TO_TICKS(FF_FS_TIMEOUT)) == pdTRUE);
}

void ff_mutex_give(int vol)
{
    xSemaphoreGive(s_ff_mutex[vol]);
}
```

> FatFS รุ่นเก่ากว่า R0.15 ใช้ชื่อ `ff_cre_syncobj` / `ff_del_syncobj` /
> `ff_req_grant` / `ff_rel_grant` และส่ง `FF_SYNC_t*` เข้ามา — เช็กที่ `ff.c`
> ว่าเรียกชื่อไหน แล้วใช้ชื่อนั้น ตรรกะข้างในเหมือนกันทุกอย่าง

### 7.3 ย้ำเรื่องความเร็ว (ไม่เกี่ยวกับ thread safety แต่เจ็บกว่า)

`ff.c` ตัดทุก `disk_read`/`disk_write` ที่ **ขอบ cluster** (บรรทัด 4083 และ 4198)
วัดได้ว่าแต่ละคำสั่งมีค่าคงที่ 491 µs → ถ้า cluster 4 KB จะเหลือไม่ถึง 8 MB/s
ไม่ว่า USB จะเร็วแค่ไหน **ฟอร์แมตแฟลชไดรฟ์เป็น cluster 64–128 KB**
ไลบรารีจะเตือนใน log ให้เองถ้าเจอ cluster < 32 KB

---

## 8. สิ่งที่ต้องแก้นอกโฟลเดอร์นี้

### 8.1 `CMakeLists.txt` ที่ราก (กัน `f_mount` ชนกัน)

```cmake
set(EXCLUDE_COMPONENTS fatfs)          # ⬅ ต้องอยู่ "ก่อน" บรรทัดถัดไป
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(your_app)
```

แล้ว `idf.py fullclean` หนึ่งครั้ง ไม่งั้นจะเจอ
`multiple definition of 'f_mount'` เพราะ `fatfs` ของ IDF ชนกับ `esp32_fatfs`

### 8.2 `main.c` — ลบของเก่าทิ้ง

ไลบรารีนี้ทำแทนทั้งหมดแล้ว ให้ลบออกจาก `main.c`:

* `usbh_initialize(...)` ที่บรรทัด ~3664 (ตัวที่ยังส่ง handler เป็น `NULL`)
* glue ของ diskio บรรทัด ~3247–3365 และ `diskio_register_driver()` บรรทัด ~3510
* `usbh_msc_run()` / `usbh_msc_stop()` เดิมที่นิยามไว้เอง

เหลือแค่ `esp32_usbh_init(&cfg);`

### 8.3 `usb_config.h` (ที่ก๊อปมาไว้ใน `components/` แล้ว)

```c
#define CONFIG_USBHOST_MAX_EXTHUBS        3     /* CH334 ซ้อน 2 ชั้น */
#define CONFIG_USBHOST_MAX_EHPORTS        4
#define CONFIG_USBHOST_MAX_MSC_CLASS      4     /* ต้อง = FF_VOLUMES */
#define CONFIG_USBHOST_MAX_HID_CLASS      4     /* ต้อง >= ESP32_USBH_HID_MAX_DEVICES */
#define CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT 1000   /* เดิม 500 */
#define CONFIG_USBHOST_MSC_TIMEOUT       10000  /* เดิม 5000 */
#define CONFIG_USBHOST_PSC_STACKSIZE      4096
/* อย่าเปิด CONFIG_USBHOST_GET_STRING_DESC — ถ้าสตริงพัง enumerate จะล้มทั้งตัว
   ไลบรารีนี้ดึงสตริงเองแบบทนพลาด */
```

และปิดสีใน log (`CONFIG_USB_PRINTF_COLOR_ENABLE`) + ตั้ง `USB_DBG_WARNING`
เพราะ `esp_rom_printf()` busy-wait บน UART จากเธรด prio 24 เสีย ~40 ms ต่อการ enumerate

---

## 9. การกู้คืนอัตโนมัติ (hot-plug + error recovery)

ไล่ระดับจากเบาไปหนัก:

| ชั้น | เงื่อนไข | การกระทำ |
|---|---|---|
| L0 | `-USB_ERR_NAK` | หน่วงตาม `bInterval` แล้วลองใหม่ |
| L1 | I/O error ชั่วคราว | ลองซ้ำ `ESP32_USBH_MSC_IO_RETRY` (3) ครั้ง |
| L2 | `-12 IO` / `-8 STALL` | **BOT reset** = class request `0xFF` + CLEAR_FEATURE ทั้ง 2 bulk EP + เคลียร์ `data_toggle` ด้วยมือ (CherryUSB ไม่มี `usbh_clear_feature` สาธารณะ) |
| L3 | BOT reset ล้ม และ EP0 ก็ตาย | `esp32_usbh_restart()` — deinit → 200 ms → init ใหม่ (วัดได้ 564 ms ไม่ต้องรีบูต) |
| L4 | `ESP32_USBH_VBUS_GPIO` ตั้งไว้ | ตัดไฟ 1 s → จ่ายไฟ → รอ 3 s → restart |
| L5 | `usbh_hub_deinitialize` ค้าง (รอ `hub_sem` ไม่มี timeout) | `esp_timer` watchdog รีบูตที่ `ESP32_USBH_PANIC_REBOOT_MS` (0 = ปิด) |

เพิ่มเติม: CherryUSB **ไม่มี event แจ้ง enumerate ล้มเหลว** (`usbh_hub.c:650-652`
แค่ log) ไลบรารีจึงเฝ้าเองว่า `CONNECTED` แล้วไม่ตามด้วย `CONFIGURED` ภายในเวลาที่กำหนด
→ ยิง `ESP32_USBH_EV_ENUM_FAILED` และถ้าเกิน `ESP32_USBH_ENUM_FAIL_LIMIT` (3) ครั้ง
ก็ restart host ให้เอง (มี cooldown `ESP32_USBH_RESTART_COOLDOWN_MS` = 10 s กันลูป)

---

## 10. ⚠️ ค่า `MSC_MAX_SEC_PER_CMD` — อย่าเพิ่มเกิน 64

`GHWCFG3 = 0x03805eb5` ของ ESP32-P4 แปลว่า

* `XFERSIZEWIDTH = 5` → ช่อง xfer size สูงสุด 65535 ไบต์
* `PKTSIZEWIDTH  = 3` → ช่อง packet count สูงสุด **127 แพ็กเก็ต**

แต่ `usb_hc_dwc2.c:512` ใส่ mask ตายตัวเป็น `0x3FF` (1023) → ถ้าสั่งโอน 64 KB
(128 แพ็กเก็ต × 512 B) ทั้งสองช่องจะล้นกลายเป็น **0** แชนเนลไม่เริ่มทำงานเลย
→ timeout 5 วินาที → คืน `-14`

`esp32_usbh_get_hw_max_sectors()` อ่าน GHWCFG3 จริงตอนบูตแล้วคำนวณให้
ค่า 64 คือเพดานที่เหลือ margin และวัดได้ **36 / 39 MB/s** กับ RTL9210 แล้ว
