#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define TAG_ES8311 "ES8311"
#define ES8311_ADDR 0x18  // หากเสียงไม่ออก ให้ลองสลับเป็น 0x19

static i2c_master_dev_handle_t s_es8311_dev = NULL;

static esp_err_t es8311_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    esp_err_t ret = i2c_master_transmit(s_es8311_dev, buf, sizeof(buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_ES8311, "I2C Write fail [Reg 0x%02X = 0x%02X]: %s", reg, val, esp_err_to_name(ret));
    }
    return ret;
}

void es8311_codec_init(i2c_master_bus_handle_t bus_handle) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES8311_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_es8311_dev));

    ESP_LOGI(TAG_ES8311, "Configuring ES8311 Full Register Pipeline...");

    // 1. Reset ชิป
    es8311_write_reg(0x00, 0x1F);
    vTaskDelay(pdMS_TO_TICKS(15));
    es8311_write_reg(0x00, 0x00);

    // 2. ปลุกระบบจ่ายไฟ Analog Bias, VMID และ Reference Voltage
    es8311_write_reg(0x0D, 0x00); // Power-up Analog (0 = Normal, 1 = PowerDown)
    es8311_write_reg(0x0E, 0x02); // Enable Vref & low noise bias
    es8311_write_reg(0x0F, 0x44); // Power-up VMID divider
    es8311_write_reg(0x10, 0x1F); // Set bias generator
    es8311_write_reg(0x11, 0x7F); // Internal bias resistor
    es8311_write_reg(0x12, 0x00); // Power-up DAC
    es8311_write_reg(0x13, 0x10); // Power-up ADC
    es8311_write_reg(0x14, 0x1A); // Output driver stage enable

    // 3. กำหนดความถี่สัญญาณนาฬิกา (MCLK 256*Fs จาก I2S)
    es8311_write_reg(0x01, 0x3F); // Enable MCLK, BCLK, DCLK
    es8311_write_reg(0x02, 0x00); // MCLK Pre-divider = 1
    es8311_write_reg(0x03, 0x10); // ADC clock divider
    es8311_write_reg(0x04, 0x10); // DAC clock divider
    es8311_write_reg(0x05, 0x00); // BCLK divider
    es8311_write_reg(0x06, 0x00);
    es8311_write_reg(0x07, 0x00);
    es8311_write_reg(0x08, 0xFF);

    // 4. ตั้งค่าฟอร์แมตสัญญาณ I2S (Standard Philips, 16-bit)
    es8311_write_reg(0x09, 0x0C); // SDP IN: 16-bit I2S
    es8311_write_reg(0x0A, 0x0C); // SDP OUT: 16-bit I2S

    // 5. สลับเส้นทางเสียงจาก DAC ไปยังขา Output (จุดสำคัญ!)
    es8311_write_reg(0x37, 0x08); // Route DAC output to Line-Out/Speaker Driver

    // 6. ปรับระดับความดัง และ Unmute DAC
    es8311_write_reg(0x32, 0xC0); // Volume: 0 dB (100% Clean)
    es8311_write_reg(0x31, 0x00); // Unmute DAC

    // 7. สั่งสตาร์ต Clock State Machine (CSM) ทำงานจริง
    es8311_write_reg(0x00, 0x80); // CSM_ON = 1

    ESP_LOGI(TAG_ES8311, "ES8311 Active & Unmuted successfully!");
}