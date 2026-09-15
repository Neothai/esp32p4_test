#ifndef ESP_BTN_H_
#define ESP_BTN_H_

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

// โครงสร้างปุ่มกดระดับล่าง (Low-level Button Type)
typedef struct {
    gpio_num_t pin;                 // ขา GPIO ที่ผูกกับปุ่มนี้
    uint8_t valbtnPrA[3];           // สถานะก่อนหน้าสำหรับตรวจจับขากดลง (Falling)
    uint8_t valbtnReA[3];           // สถานะก่อนหน้าสำหรับตรวจจับขาปล่อย (Rising)
    uint32_t btnPressedLastTime[2]; // บันทึกเวลาล่าสุดที่ปุ่มถูกกด (หน่วย ms)
    uint16_t longPressDuration;    // ระยะเวลาที่ใช้พิจารณาการกดค้าง
    uint8_t longPressDetected[2];     // แฟล็กแจ้งเตือนเมื่อตรวจพบการกดค้างสำเร็จ
    uint8_t longPress_btn_isPressed[2]; // แฟล็กสถานะว่าปัจจุบันปุ่มยังถูกกดอยู่หรือไม่
} esp_btn_t;

#define falling 0
#define rising  1

// Functions สำหรับการใช้งานแบบแมนนวล (ดึงค่าเอง)
void esp_btn_init(esp_btn_t *btn, gpio_num_t pin);
void esp_btn_set_long_pr(esp_btn_t *btn, uint16_t long_ms);
uint8_t esp_btn_get_pr(esp_btn_t *btn, uint8_t edge);
uint8_t esp_btn_get_long_pr_once(esp_btn_t *btn);
uint8_t esp_btn_get_long_pr_re(esp_btn_t *btn);
uint8_t esp_btn_get_pr_long_pr_re(esp_btn_t *btn);

// ========================================= Button Library With Callback Function ========================================= //

typedef void (*btnEvent)(uint8_t eventId);

// โครงสร้างปุ่มกดระบบคอลแบ็ก (Callback Button Type)
typedef struct {
    uint32_t btnLastDebounceTime;
    uint16_t btnDebounceTimeMs;
    uint8_t btnCurrStateForDebounce;
    uint8_t btnLastStateForDebounce;
    gpio_num_t btnPin;
    esp_btn_t btnStateObj;
    btnEvent btnEventCb;    
} esp_btncb_t;

#define BTN_EVENT_PRESSED                    1
#define BTN_EVENT_RELEASED                   2
#define BTN_EVENT_LONG_PRESSED_ONCE          3
#define BTN_EVENT_LONG_PRESSED_UNTIL_RELEASE 4

// Functions สำหรับการใช้งานระบบ Callback Event
void esp_btncb_init(esp_btncb_t *button, gpio_num_t pin, btnEvent cb);
void esp_btncb_set_long_pr(esp_btncb_t *button, uint16_t long_ms);
void esp_btncb_set_db(esp_btncb_t *button, uint16_t db_ms);
void esp_btncb_task(esp_btncb_t *button);

#endif /* ESP_BTN_H_ */