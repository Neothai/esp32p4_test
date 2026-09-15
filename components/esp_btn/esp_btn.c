#include "esp_btn.h"
#include "esp_timer.h"

// ฟังก์ชันภายในระบบสำหรับอ่านค่าเวลาเป็นมิลลิวินาที (ทดแทน millis)
static uint32_t sys_millis(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void esp_btn_init(esp_btn_t *btn, gpio_num_t pin) {
    btn->pin = pin;
    
    // ตั้งค่าโครงสร้างอินพุต GPIO ของ ESP-IDF
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    for (int i = 0; i < 3; i++) {
        btn->valbtnPrA[i] = 1;
        btn->valbtnReA[i] = 1;
    }
    btn->longPressDuration = 1000;
    btn->longPressDetected[0] = 0;
    btn->longPressDetected[1] = 0;
    btn->longPress_btn_isPressed[0] = 0;
    btn->longPress_btn_isPressed[1] = 0;
}

void esp_btn_set_long_pr(esp_btn_t *btn, uint16_t long_ms) {
    btn->longPressDuration = long_ms;
}

uint8_t esp_btn_get_pr(esp_btn_t *btn, uint8_t edge) {
    uint8_t level = false;
    uint8_t btn_status = gpio_get_level(btn->pin); // อัปเดตผ่านฮาร์ดแวร์โดยตรงตามขาที่ผูกไว้

    if (!edge) { // falling edge
        if (btn->valbtnPrA[0] == 1 && btn_status == 0) { level = true; }
        btn->valbtnPrA[0] = btn_status;
    } else { // rising edge
        if (btn->valbtnReA[0] == 0 && btn_status == 1) { level = true; }
        btn->valbtnReA[0] = btn_status;
    }

    return level;
}

uint8_t esp_btn_get_long_pr_once(esp_btn_t *btn) {
    uint8_t longPressOnce_send = false;
    uint32_t now = sys_millis();
    uint8_t btn_status = gpio_get_level(btn->pin);

    if (btn->valbtnPrA[1] == 1 && btn_status == 0) {
        btn->longPress_btn_isPressed[0] = true;
        btn->btnPressedLastTime[0] = now;
        btn->longPressDetected[0] = false;
    }

    if (btn->valbtnPrA[1] == 0 && btn_status == 1) {
        btn->longPressDetected[0] = btn->longPress_btn_isPressed[0] = false;
    }

    btn->valbtnPrA[1] = btn_status;

    if (btn->longPress_btn_isPressed[0] && !btn->longPressDetected[0]) {
        if ((now - btn->btnPressedLastTime[0]) > btn->longPressDuration) {
            longPressOnce_send = btn->longPressDetected[0] = true;
        }
    }

    return longPressOnce_send;
}

uint8_t esp_btn_get_long_pr_re(esp_btn_t *btn) {
    uint32_t now = sys_millis();
    uint8_t btn_status = gpio_get_level(btn->pin);

    if (btn->valbtnPrA[2] == 1 && btn_status == 0) {
        btn->longPress_btn_isPressed[1] = true;
        btn->btnPressedLastTime[1] = now;
        btn->longPressDetected[1] = false;
    }

    if (btn->valbtnPrA[2] == 0 && btn_status == 1) {
        btn->longPressDetected[1] = btn->longPress_btn_isPressed[1] = false;
    }

    btn->valbtnPrA[2] = btn_status;

    if (btn->longPress_btn_isPressed[1] && !btn->longPressDetected[1]) {
        if ((now - btn->btnPressedLastTime[1]) > btn->longPressDuration) {
            btn->longPressDetected[1] = true;
        }
    }

    return btn->longPressDetected[1];
}

uint8_t esp_btn_get_pr_long_pr_re(esp_btn_t *btn) {
    if (esp_btn_get_pr(btn, falling) || esp_btn_get_long_pr_re(btn)) {
        return true;
    }
    return false;
}

// ====================================================================================================== //

void esp_btncb_init(esp_btncb_t *button, gpio_num_t pin, btnEvent cb) {
    button->btnPin = pin;
    esp_btn_init(&button->btnStateObj, pin);
    button->btnEventCb = cb;
    button->btnDebounceTimeMs = 10;
    button->btnLastStateForDebounce = gpio_get_level(pin);
}

void esp_btncb_set_long_pr(esp_btncb_t *button, uint16_t long_ms) {
    esp_btn_set_long_pr(&button->btnStateObj, long_ms);
}

void esp_btncb_set_db(esp_btncb_t *button, uint16_t db_ms) {
    button->btnDebounceTimeMs = db_ms;
}

void esp_btncb_task(esp_btncb_t *button) {
    if (!button->btnEventCb) { return; }

    uint32_t now = sys_millis();
    uint8_t raw_state = gpio_get_level(button->btnPin);

    if (raw_state != button->btnLastStateForDebounce) {
        button->btnLastDebounceTime = now;
        button->btnLastStateForDebounce = raw_state;
    }

    if ((now - button->btnLastDebounceTime) > button->btnDebounceTimeMs) {
        // ประมวลผลจากระดับล่างตามลำดับสัญญาณที่เสถียรแล้ว
        if (esp_btn_get_pr(&button->btnStateObj, falling))        { button->btnEventCb(BTN_EVENT_PRESSED); }
        if (esp_btn_get_pr(&button->btnStateObj, rising))         { button->btnEventCb(BTN_EVENT_RELEASED); }
        if (esp_btn_get_long_pr_once(&button->btnStateObj))        { button->btnEventCb(BTN_EVENT_LONG_PRESSED_ONCE); }
        if (esp_btn_get_long_pr_re(&button->btnStateObj)) { button->btnEventCb(BTN_EVENT_LONG_PRESSED_UNTIL_RELEASE); }
    }
}