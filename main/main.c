// เจอบั๊ก (001) เมื่อเล่นวิดีโอ 40 นาที ไปประมาณ 10 นาทีกว่าๆ ก็เกิดอาการค้างทั้งภาพและเสียง

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp32_wifi.h"
#include "sd_protocol_types.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "ff.h"
#include "diskio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_ldo_regulator.h"
#include "esp_dma_utils.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_ek79007.h"
#include "esp_idf_version.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "demos/lv_demos.h"

#include "driver/jpeg_decode.h"
#include "avilib.h"
#include "freertos/ringbuf.h"
#include "esp32_mp3dec.h"
#include "helix/pub/mp3dec.h"
#include "esp32_avidec.h"
#include "es8311.c"

#include "driver/i2c_master.h"
#include "esp_lcd_touch_gt911.h"

#include "vendingmc_ui/lvgl_thai_kb.h"
#include "vendingmc_ui/vd_home_page.h"
#include "vendingmc_ui/vd_image_dec.h"
#include "vendingmc_ui/vd_payment_ui.h"
#include "vendingmc_ui/vd_toast_notify.h"
#include "vendingmc_ui/vd_task_mgr.h"
#include "vendingmc_ui/vd_sdcard.h"

const char *qr_b64data = "data:image/png;base64,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";
lv_image_dsc_t qr_img_dsc;
uint8_t *raw_qr_img;

LV_FONT_DECLARE(anuphan_14);
LV_FONT_DECLARE(anuphan_16);
LV_FONT_DECLARE(anuphan_bold_16);
LV_FONT_DECLARE(anuphan_semi_bold_16);
//LV_FONT_DECLARE(anuphan_18);
extern lv_font_t anuphan_18;
LV_FONT_DECLARE(anuphan_med_18);
LV_FONT_DECLARE(anuphan_bold_18);
LV_FONT_DECLARE(anuphan_semi_bold_18);
LV_FONT_DECLARE(anuphan_20);
LV_FONT_DECLARE(anuphan_bold_20);
LV_FONT_DECLARE(anuphan_22);
LV_FONT_DECLARE(anuphan_med_22);
LV_FONT_DECLARE(anuphan_bold_22);
LV_FONT_DECLARE(anuphan_bold_24);
LV_FONT_DECLARE(anuphan_bold_26);
LV_FONT_DECLARE(font_awesome_20);

LV_IMG_DECLARE(greentea);
LV_IMG_DECLARE(nvdm200_51);
LV_IMG_DECLARE(cart250_219);
LV_IMG_DECLARE(qr120_120);
LV_IMG_DECLARE(cash167_120);
LV_IMG_DECLARE(prompt_pay200_67);
LV_IMG_DECLARE(IMG_20260820_123647);

#define PIN_NUM_CLK    GPIO_NUM_43
#define PIN_NUM_CMD    GPIO_NUM_44
#define PIN_NUM_D0     GPIO_NUM_39
#define PIN_NUM_D1     GPIO_NUM_40
#define PIN_NUM_D2     GPIO_NUM_41
#define PIN_NUM_D3     GPIO_NUM_42

// ขา I2S สำหรับ ES8311
#define I2S_MCLK_IO       GPIO_NUM_13
#define I2S_BCLK_IO       GPIO_NUM_12
#define I2S_WS_IO         GPIO_NUM_10
#define I2S_DOUT_IO       GPIO_NUM_9

// ขาเปิดเพาเวอร์แอมป์ลำโพง (ขึ้นอยู่กับแบบวงจรของบอร์ด เช่น GPIO 26, 53 หรือ 45)
#define PA_ENABLE_GPIO    GPIO_NUM_53

#define BENCH_FILE_PATH   "0:/bench.tmp"
#define BENCH_TOTAL_BYTES (64 * 1024 * 1024)
#define BENCH_CHUNK_SIZE  (64 * 1024)

#define TAG_FS "SD_FILES"
#define MAX_DEPTH    6     // จำกัดความลึกสูงสุดไม่เกิน 6 ระดับชั้น
#define MAX_PATH_LEN 512

// ── กำหนดขา GPIO ของ Touch IC ตามบอร์ดของคุณ ──
#define TOUCH_I2C_SDA         GPIO_NUM_7
#define TOUCH_I2C_SCL         GPIO_NUM_8
#define TOUCH_PIN_INT         GPIO_NUM_NC
#define TOUCH_PIN_RST         GPIO_NUM_23

#define TAG "LVGL_P4"

#define LCD_H_RES              1024
#define LCD_V_RES              600
#define PIN_NUM_BK_LIGHT       GPIO_NUM_32
#define PIN_NUM_LCD_RST        GPIO_NUM_33

i2c_master_bus_handle_t i2c_bus_handle;
lv_display_t *display;
vd_home_page_t *vd_home_page;
vd_payment_opt_dialog_t *dialog;
bool __start_qr = false;
bool __sd_ok = true;

static esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
static esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;

typedef enum payment_type {
  qr_pay,
  cash_pay
} payment_type;

typedef struct payment_frame_usr_data {
  lv_obj_t *obj_main;
  payment_type p_type;
} payment_frame_usr_data;

void log_system_memory(void) {
    // 1. ดึงค่าหน่วยความจำจากระดับฮาร์ดแวร์ด้วย ESP-IDF API
    size_t free_heap      = esp_get_free_heap_size();
    size_t min_free_heap  = esp_get_minimum_free_heap_size();
    size_t internal_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t psram_free     = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    size_t total_internal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t total_psram    = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t total_heap     = total_internal + total_psram;

    // คำนวณเปอร์เซ็นต์การใช้งาน (Usage %)
    // ป้องกันการหารด้วย 0 (Division by Zero) ในกรณีที่ไม่มี PSRAM หรือระบบยังไม่พร้อม
    float internal_used_pct = (total_internal > 0) ? (((float)(total_internal - internal_free) / total_internal) * 100.0f) : 0.0f;
    float psram_used_pct    = (total_psram > 0)    ? (((float)(total_psram - psram_free) / total_psram) * 100.0f) : 0.0f;
    float heap_used_pct     = (total_heap > 0)     ? (((float)(total_heap - free_heap) / total_heap) * 100.0f) : 0.0f;

    // 2. เตรียมบัฟเฟอร์แปลงข้อมูลตัวเลขให้เป็นข้อความที่อ่านง่าย (ขยายขนาดบัฟเฟอร์เพื่อรองรับข้อความ % ที่เพิ่มเข้ามา)
    char str_free[60];
    char str_min[60];
    char str_internal[60];
    char str_psram[60];

    // แปลงหน่วยเป็น KB และใส่ % การใช้งานเข้าไปด้วย
    snprintf(str_free, sizeof(str_free), "%zu bytes (%.1f KB) | Used: %.1f%%", free_heap, free_heap / 1024.0, heap_used_pct);
    snprintf(str_min, sizeof(str_min), "%zu bytes (%.1f KB)", min_free_heap, min_free_heap / 1024.0);
    snprintf(str_internal, sizeof(str_internal), "%zu bytes (%.1f KB) | Used: %.1f%%", internal_free, internal_free / 1024.0, internal_used_pct);

    if (total_psram > 0) {
        snprintf(str_psram, sizeof(str_psram), "%zu bytes (%.1f KB) | Used: %.1f%%", psram_free, psram_free / 1024.0, psram_used_pct);
    } else {
        snprintf(str_psram, sizeof(str_psram), "0 bytes (Not Available)");
    }

    // 3. พ่นตาราง Unicode ออกทาง Serial Monitor
    // ขยายความกว้างคอลัมน์ขวาจาก 40 เป็น 50 ตัวอักษร เพื่อให้รองรับข้อความเปอร์เซ็นต์ที่เพิ่มขึ้นโดยไม่ล้นบรรทัด
    ESP_LOGI(TAG,
             "\r\n"
             "┌──────────────────────┬──────────────────────────────────────────────────┐\r\n"
             "│ %-20s │ %-48s │\r\n" // ปรับสเปซเผื่อเส้นขอบตาราง
             "├──────────────────────┼──────────────────────────────────────────────────┤\r\n"
             "│ %-20s │ %-48s │\r\n"
             "│ %-20s │ %-48s │\r\n"
             "│ %-20s │ %-48s │\r\n"
             "│ %-20s │ %-48s │\r\n"
             "└──────────────────────┴──────────────────────────────────────────────────┘",
             "Memory Parameter", "Available Status",
             "Current Free Heap", str_free,
             "Min Ever Free Heap", str_min,
             "Internal DRAM Free", str_internal,
             "PSRAM (SPIRAM) Free", str_psram);
}

/**
 * @brief พิมพ์ค่า Stack คงเหลือต่ำสุดที่เคยเกิดขึ้น (High Water Mark)
 * @param tag ข้อความระบุจุดที่เรียก เช่น "APP_MAIN", "LVGL" (ใส่ NULL ได้)
 * @param target_task TaskHandle_t ที่ต้องการเช็ค (ส่ง NULL เพื่อเช็ค Task ตัวเอง)
 */
void check_task_stack(const char *tag, TaskHandle_t target_task) {
    TaskHandle_t hdl = target_task ? target_task : xTaskGetCurrentTaskHandle();
    const char *task_name = pcTaskGetName(hdl);

    // ใน ESP-IDF ฟังก์ชันนี้จะคืนค่าเป็น "ไบต์ (Bytes)" โดยตรง
    UBaseType_t min_free_bytes = uxTaskGetStackHighWaterMark(hdl);

    ESP_LOGI(tag ? tag : "STACK", "[%s] Min Free Stack: %u bytes", 
             task_name, (unsigned int)min_free_bytes);
}

/* Thai Upper Vowels (สระบนและเครื่องหมายบน) */
#define THAI_MAI_HAN_AKAT     0x0E31  // ไม้หันอากาศ
#define THAI_SARA_I           0x0E34  // สระอิ
#define THAI_SARA_II          0x0E35  // สระอี
#define THAI_SARA_UE          0x0E36  // สระอึ
#define THAI_SARA_UEE         0x0E37  // สระอือ
#define THAI_MAI_TAIKHU       0x0E47  // ไม้ไต่คู้
#define THAI_NIKHAHIT         0x0E4D  // นิคหิต (หยาดน้ำค้าง)
#define THAI_YAMAKKAN         0x0E4E  // ยามักการ

/* Thai Tone Marks & Diacritics (วรรณยุกต์และเครื่องหมายพิเศษบน) */
#define THAI_MAI_EK           0x0E48  // ไม้เอก
#define THAI_MAI_THO          0x0E49  // ไม้โท
#define THAI_MAI_TRI          0x0E4A  // ไม้ตรี
#define THAI_MAI_CHATTAWA     0x0E4B  // ไม้จัตวา
#define THAI_THANTHAKHAT      0x0E4C  // ทัณฑฆาต (ตัวการันต์)

/* Thai Lower Vowels (สระล่างและเครื่องหมายล่าง) */
#define THAI_SARA_U           0x0E38  // สระอุ
#define THAI_SARA_UU          0x0E39  // สระอู
#define THAI_PHINTHU          0x0E3A  // พินทุ (จุดล่างคำบาลี)

/* Thai Base Level Vowels & Symbols (สระระดับบรรทัดและเครื่องหมาย) */
#define THAI_SARA_A           0x0E30  // สระอะ
#define THAI_SARA_AA          0x0E32  // สระอา
#define THAI_SARA_AM          0x0E33  // สระอำ
#define THAI_LAKKHANGYAO      0x0E45  // ลากข้างยาว
#define THAI_MAIYAMOK         0x0E46  // ไม้ยมก
#define THAI_SARA_E           0x0E40  // สระเอ
#define THAI_SARA_AE          0x0E41  // สระแอ
#define THAI_SARA_O           0x0E42  // สระโอ
#define THAI_SARA_AI_MAIMUAN  0x0E43  // สระใอไม้ม้วน
#define THAI_SARA_AI_MAIMALAI 0x0E44  // สระไอไม้มลาย

#define THAI_CHAR_LLVL        0x00    // ตัวล่าง
#define THAI_CHAR_LVL0        0x01    // ตัวระดับทั่วไป
#define THAI_CHAR_LVL1        0x02    // ตัวสูงระดับ 1
#define THAI_CHAR_LVL1_OR_2   0x03    // ตัวสูงระดับ 1 หรือ 2 ก็ได้ ขึ้นอยู่กับตัวก่อนหน้า
#define THAI_CHAR_NONE        0x04    // ไม่ใช่อักขระไทย

uint8_t get_thai_char_level(uint32_t unicode){
  if(!(unicode >= 0x0E00 && unicode <= 0x0E7F)) return THAI_CHAR_NONE;
  if(unicode >= THAI_SARA_U && unicode <= THAI_PHINTHU) return THAI_CHAR_LLVL;
  if((unicode >= THAI_MAI_HAN_AKAT && unicode <= THAI_SARA_UEE) || unicode == THAI_MAI_TAIKHU
   || unicode == THAI_NIKHAHIT || unicode == THAI_YAMAKKAN || unicode == THAI_MAIYAMOK) return THAI_CHAR_LVL1;
  if(unicode >= THAI_MAI_EK && unicode <= THAI_THANTHAKHAT) return THAI_CHAR_LVL1_OR_2;

  return THAI_CHAR_LVL0;
}

bool thai_font_get_glyph_dsc_fmt_txt(const lv_font_t * font, lv_font_glyph_dsc_t * dsc_out, uint32_t unicode_letter, uint32_t unicode_letter_next) {
  bool res = lv_font_get_glyph_dsc_fmt_txt(font, dsc_out, unicode_letter, unicode_letter_next);
  uint8_t ctype = get_thai_char_level(unicode_letter);
  if (!res) return false;
  if (ctype == THAI_CHAR_NONE) return true;

  uint8_t next_ctype = get_thai_char_level(unicode_letter_next);

  if (ctype == THAI_CHAR_LVL1_OR_2) {
    dsc_out->adv_w = 0;
    dsc_out->ofs_y += (next_ctype != THAI_CHAR_LVL1_OR_2) ? dsc_out->box_h : 0;


  }

  return true;
}

// ฟังก์ชันเปิดใช้งาน Hook ให้กับฟอนต์
void apply_thai_font_hook(lv_font_t * font) {
    if (font && font->get_glyph_dsc != thai_font_get_glyph_dsc_fmt_txt) {
      //thai_font = font;
      font->get_glyph_dsc = thai_font_get_glyph_dsc_fmt_txt;
    }
}

/* ตาราง Lookup Table 256 ค่า (0xFF = ตัวอักษรที่ไม่ถูกต้อง) */
static const uint8_t b64_lut[256] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3E, 0xFF, 0xFF, 0xFF, 0x3F, // 0x2B='+', 0x2F='/'
    0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, // 0x30-0x39='0'-'9', 0x3D='='
    0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, // 0x41-0x4F='A'-'O'
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 0x50-0x5A='P'-'Z'
    0xFF, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, // 0x61-0x6F='a'-'o'
    0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 0x70-0x7A='p'-'z'
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/**
 * @brief ถอดรหัส Base64 Data URI เป็น Binary Buffer
 * @param input สตริง Base64 หรือ Data URI
 * @param out_len พอยน์เตอร์สำหรับรับขนาดไบต์จริงของไฟล์ที่ถอดรหัสได้
 * @return uint8_t* บัฟเฟอร์ข้อมูลไบนารี (ต้อง free/lv_free เมื่อใช้งานเสร็จ) หรือ NULL หากล้มเหลว
 */
uint8_t *base64_decode(const char * input, size_t * out_len) {
    if(!input || !out_len) return NULL;

    ESP_LOGI("BASE64", "Start!");
    uint64_t t0 = esp_timer_get_time();

    // 1. ตัด Prefix "data:image/...;base64," ออกอัตโนมัติ
    const char * data_ptr = strchr(input, ',');
    if(data_ptr) {
        data_ptr++; // ขยับข้ามเครื่องหมายจุลภาคไปที่เนื้อ Base64 แท้ๆ
    } else {
        data_ptr = input;
    }

    size_t in_len = strlen(data_ptr);
    if(in_len == 0) return NULL;

    // 2. คำนวณขนาดเอาต์พุตสูงสุดที่ต้องใช้ (4 Base64 chars -> 3 Binary bytes)
    size_t max_out_len = (in_len / 4) * 3 + 3;
    uint8_t * out_buf = (uint8_t *)malloc(max_out_len);
    if(!out_buf) return NULL;

    uint8_t * out = out_buf;
    const uint8_t * src = (const uint8_t *)data_ptr;
    const uint8_t * end = src + in_len;

    // 3. Loop ถอดรหัสทีละ 4 อักขระ -> 3 ไบต์
    while(src + 4 <= end) {
        // ข้ามช่องว่างหรือ Newline ที่อาจปนมา
        if(*src <= 0x20) { src++; continue; }

        uint8_t c0 = b64_lut[src[0]];
        uint8_t c1 = b64_lut[src[1]];
        uint8_t c2 = b64_lut[src[2]];
        uint8_t c3 = b64_lut[src[3]];

        // รวมค่า Bitwise เข้า 32-bit Register
        uint32_t triple = (c0 << 18) | (c1 << 12) | (c2 << 6) | c3;

        // ถอดรหัส 2 ไบต์แรกเสมอ
        *out++ = (triple >> 16) & 0xFF;

        // ตรวจสอบ Padding '='
        if(src[2] == '=') {
            src += 4;
            break;
        }
        *out++ = (triple >> 8) & 0xFF;

        if(src[3] == '=') {
            src += 4;
            break;
        }
        *out++ = triple & 0xFF;

        src += 4;
    }

    *out_len = (size_t)(out - out_buf);

    uint64_t t1 = esp_timer_get_time();
    ESP_LOGI("BASE64", "End! take %ul ms", (t1-t0)/1000.0);

    return out_buf;
}

lv_obj_t *keyboard;
lv_obj_t *tile_view, *sale_page;
lv_obj_t *product_cart_item_wrapper;
lv_obj_t *payment_opt_frame, *qr_pay_opt_frame, *cash_pay_opt_frame, *payment_topic;
lv_obj_t *loading_bar, *payment_close_label;
payment_type pay_type = qr_pay;

static void drag_event_handler(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    lv_indev_t * indev = lv_indev_active();
    if(indev == NULL) return;

    lv_point_t vect;
    lv_indev_get_vect(indev, &vect);

    int32_t x = lv_obj_get_x_aligned(obj) + vect.x;
    int32_t y = lv_obj_get_y_aligned(obj) + vect.y;

    lv_obj_set_pos(obj, x, y);
}

static void close_btn_handler(lv_event_t * e) {
  lv_obj_t *dia = (lv_obj_t*)lv_event_get_user_data(e);
  lv_obj_delete_async(dia);
  dia = NULL;
}


lv_obj_t *vd_dialog_create(lv_obj_t *parant, const char *topic);

static void new_dia_event_handler(lv_event_t * e) {
  vd_dialog_create(lv_screen_active(), "Test");
}


lv_obj_t *vd_dialog_create(lv_obj_t *parant, const char *topic){
  lv_obj_t *dia = lv_obj_create(parant);
  lv_obj_set_clickable(dia, true);
  lv_obj_add_event_cb(dia, drag_event_handler, LV_EVENT_PRESSING, NULL);
  lv_obj_set_style_pad_all(dia, 5, 0);
  lv_obj_set_size(dia, 200, 100);

  lv_obj_invalidate(dia);

  //lv_obj_t *header = lv_obj_create(dia);
  //lv_obj_set_size(header, lv_obj_get_width(dia), (int)(lv_obj_get_height(dia) / 10.0));

  lv_obj_t *close_btn = lv_btn_create(dia);
  lv_obj_set_align(close_btn, LV_ALIGN_TOP_RIGHT);
  lv_obj_set_style_bg_color(close_btn, lv_color_make(255, 0, 0), 0);
  lv_obj_add_event_cb(close_btn, close_btn_handler, LV_EVENT_RELEASED, dia);

  lv_obj_t *close_btn_label = lv_label_create(close_btn);
  lv_label_set_text(close_btn_label, "X");

  return dia;
}

static void spdb_decr_value_cb(lv_event_t * e){
  lv_obj_t *val_label = (lv_obj_t*)lv_event_get_user_data(e);
  int val = atoi(lv_label_get_text(val_label));
  lv_label_set_text_fmt(val_label, "%d", --val);
}

static void spdb_incr_value_cb(lv_event_t * e){
  lv_obj_t *val_label = (lv_obj_t*)lv_event_get_user_data(e);
  int val = atoi(lv_label_get_text(val_label));
  lv_label_set_text_fmt(val_label, "%d", ++val);
}


/*
lv_obj_t *vd_spinbox_create(lv_obj_t *parant, const uint16_t width, const uint16_t height){
  lv_obj_t * base = lv_obj_create(parant);
  lv_obj_set_size(base, width, height);
  lv_obj_set_style_bg_opa(base, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(base, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(base, 0, 0);
  lv_obj_set_scrollable(base, true);

  static lv_style_t btn_style;
  lv_style_init(&btn_style);
  lv_style_set_bg_color(&btn_style, lv_color_hex(0xF0F1F7));
  lv_style_set_border_opa(&btn_style, LV_OPA_TRANSP);
  lv_style_set_shadow_opa(&btn_style, LV_OPA_TRANSP);
  lv_style_set_radius(&btn_style, 10);
  lv_style_set_pad_all(&btn_style, 0);
  lv_style_set_size(&btn_style, 30, 30);

  lv_obj_t * value_label = lv_label_create(base);

  lv_obj_t *down_btn = lv_button_create(base);
  lv_obj_t *down_btn_sign = lv_label_create(down_btn);

  lv_obj_add_style(down_btn, &btn_style, 0);
  lv_obj_set_align(down_btn, LV_ALIGN_LEFT_MID);
  lv_obj_set_scrollable(down_btn, false);
  lv_obj_add_event_cb(down_btn, spdb_decr_value_cb, LV_EVENT_RELEASED, value_label);

  lv_label_set_text(down_btn_sign, LV_SYMBOL_MINUS);
  lv_obj_set_align(down_btn_sign, LV_ALIGN_CENTER);
  lv_obj_set_style_text_color(down_btn_sign, lv_color_make(0, 0, 0), 0);
  lv_obj_set_style_text_font(down_btn_sign, &lv_font_montserrat_12, 0);

  lv_obj_t *up_btn = lv_button_create(base);
  lv_obj_t *up_btn_sign = lv_label_create(up_btn);
  lv_obj_add_style(up_btn, &btn_style, 0);
  lv_obj_set_align(up_btn, LV_ALIGN_RIGHT_MID);
  lv_obj_set_scrollable(up_btn, false);
  lv_obj_add_event_cb(up_btn, spdb_incr_value_cb, LV_EVENT_RELEASED, value_label);

  lv_label_set_text(up_btn_sign, LV_SYMBOL_PLUS);
  lv_obj_set_align(up_btn_sign, LV_ALIGN_CENTER);
  lv_obj_set_style_text_color(up_btn_sign, lv_color_make(0, 0, 0), 0);
  lv_obj_set_style_text_font(up_btn_sign, &lv_font_montserrat_12, 0);

  lv_obj_set_align(value_label, LV_ALIGN_CENTER);
  lv_label_set_text(value_label, "0");

  return base;
}*/

/* =========================================================================
 * 1. Animation Setters
 * ========================================================================= */

 
static void anim_set_translate_x(void * var, int32_t v) {
    lv_obj_set_style_translate_x((lv_obj_t *)var, v, 0);
}

static void anim_set_translate_y(void * var, int32_t v) {
    lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void anim_set_circle_size(void * var, int32_t v) {
    lv_obj_t * obj = (lv_obj_t *)var;
    lv_obj_set_size(obj, v, v);
}

static void anim_set_x(void * var, int32_t v) {
    lv_obj_set_x((lv_obj_t *)var, v);
}

static void anim_set_opa(void * var, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void anim_set_bg_opa(void * var, int32_t v) {
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void anim_set_border_opa(void * var, int32_t v) {
    lv_obj_set_style_border_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void anim_set_text_opa(void * var, int32_t v) {
    lv_obj_set_style_text_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void anim_set_height(void * var, int32_t v) {
    lv_obj_set_height((lv_obj_t *)var, v);
}

static void anim_set_width(void * var, int32_t v) {
    lv_obj_set_width((lv_obj_t *)var, v);
}

/* =========================================================================
 * 2. ลบ Wrapper และรายการสินค้าทิ้งหลังยุบความสูงเสร็จ
 * ========================================================================= */
static void cart_item_final_delete_cb(lv_anim_t * a) {
    lv_obj_t * wrapper = (lv_obj_t *)a->var;
    lv_obj_delete_async(wrapper); // ลบทั้ง Wrapper (รวมทั้งแถบแดงและตัวสินค้า)
    wrapper = NULL;
}

static void start_collapse_and_delete(lv_obj_t * wrapper) {
    lv_anim_t a_h;
    lv_anim_init(&a_h);
    lv_anim_set_var(&a_h, wrapper);
    lv_anim_set_values(&a_h, lv_obj_get_height(wrapper), 0);
    lv_anim_set_duration(&a_h, 180); // ยุบความสูงใน 180ms
    lv_anim_set_path_cb(&a_h, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a_h, (lv_anim_exec_xcb_t)anim_set_height);
    lv_anim_set_completed_cb(&a_h, cart_item_final_delete_cb);
    lv_anim_start(&a_h);
}

static void cart_item_slide_out_completed_cb(lv_anim_t * a) {
    lv_obj_t * fg_card = (lv_obj_t *)a->var;
    lv_obj_t * wrapper = lv_obj_get_parent(fg_card);
    start_collapse_and_delete(wrapper);
}

/* =========================================================================
 * 3. Swipe Event Handler สำหรับ Foreground Card
 * ========================================================================= */
static void cart_item_swipe_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * fg_card = lv_event_get_target(e);
    lv_indev_t * indev = lv_indev_active();

    static int32_t total_drag_x = 0;

    if(code == LV_EVENT_PRESSED) {
        total_drag_x = lv_obj_get_style_translate_x(fg_card, 0);
    }
    else if(code == LV_EVENT_PRESSING) {
        if(!indev) return;
        lv_point_t vect;
        lv_indev_get_vect(indev, &vect);

        total_drag_x += vect.x;

        // อนุญาตให้ปัดเฉพาะไปทางซ้าย
        if(total_drag_x > 0) total_drag_x = 0;

        // เลื่อนกล่องสินค้าไปตามนิ้วในแนวตรง
        lv_obj_set_style_translate_x(fg_card, total_drag_x, 0);
    }
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        int32_t cur_x = lv_obj_get_style_translate_x(fg_card, 0);
        int32_t w = lv_obj_get_width(fg_card);
        int32_t threshold = -(w / 3); // เกณฑ์ 33%

        if(cur_x < threshold) {
            /* -------------------------------------------------------------
             * [กรณีลบ]: ปัดหลุดเกณฑ์ -> สไลด์ออกซ้ายให้พ้นจอ แล้วยุบแถว
             * ------------------------------------------------------------- */
            lv_anim_t a_x;
            lv_anim_init(&a_x);
            lv_anim_set_var(&a_x, fg_card);
            lv_anim_set_values(&a_x, cur_x, -w - 30);
            lv_anim_set_duration(&a_x, 180);
            lv_anim_set_path_cb(&a_x, lv_anim_path_ease_in);
            lv_anim_set_exec_cb(&a_x, (lv_anim_exec_xcb_t)anim_set_translate_x);
            lv_anim_set_completed_cb(&a_x, cart_item_slide_out_completed_cb);
            lv_anim_start(&a_x);

            // ค่อยๆ ปรับจางหายไปพร้อมกัน
            lv_anim_t a_opa;
            lv_anim_init(&a_opa);
            lv_anim_set_var(&a_opa, fg_card);
            lv_anim_set_values(&a_opa, lv_obj_get_style_opa(fg_card, 0), 0);
            lv_anim_set_duration(&a_opa, 180);
            lv_anim_set_exec_cb(&a_opa, (lv_anim_exec_xcb_t)anim_set_opa);
            lv_anim_start(&a_opa);
        }
        else {
            /* -------------------------------------------------------------
             * [กรณีคืนค่า]: เด้งสปริง Snap-back กลับมาปิดแถบแดง
             * ------------------------------------------------------------- */
            lv_anim_t a_x;
            lv_anim_init(&a_x);
            lv_anim_set_var(&a_x, fg_card);
            lv_anim_set_values(&a_x, cur_x, 0);
            lv_anim_set_duration(&a_x, 220);
            lv_anim_set_path_cb(&a_x, lv_anim_path_overshoot);
            lv_anim_set_exec_cb(&a_x, (lv_anim_exec_xcb_t)anim_set_translate_x);
            lv_anim_start(&a_x);
        }
    }
}

/* 1. Callback สเกลต้องอยู่บนสุด */
static void product_card_scale_anim_cb(void * var, int32_t v) {
    lv_obj_t * obj = (lv_obj_t *)var;
    lv_obj_set_style_transform_scale_x(obj, v, 0);
    lv_obj_set_style_transform_scale_y(obj, v, 0);
}

/* 2. ฟังก์ชันคืนค่าขนาดการ์ด (เรียกใช้ product_card_scale_anim_cb) */
static void restore_card_scale(lv_obj_t * card) {
    lv_anim_delete(card, (lv_anim_exec_xcb_t)product_card_scale_anim_cb);

    int32_t cur_scale = lv_obj_get_style_transform_scale_x(card, 0);
    if(cur_scale <= 0) cur_scale = 240;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, card);
    lv_anim_set_values(&a, cur_scale, 256);
    lv_anim_set_duration(&a, 180);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)product_card_scale_anim_cb);
    lv_anim_start(&a);

    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
}

/* 3. Event Handler การ์ดสินค้า (เรียกใช้ restore_card_scale) */
static void product_card_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * card = lv_event_get_target(e);

    if(code == LV_EVENT_PRESSED) {
        lv_anim_delete(card, (lv_anim_exec_xcb_t)product_card_scale_anim_cb);

        int32_t cur_scale = lv_obj_get_style_transform_scale_x(card, 0);
        if(cur_scale <= 0) cur_scale = 256;

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, card);
        lv_anim_set_values(&a, cur_scale, 240);
        lv_anim_set_duration(&a, 80);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)product_card_scale_anim_cb);
        lv_anim_start(&a);

        lv_obj_set_style_bg_color(card, lv_color_hex(0xF0F2F5), 0);
    }
    else if(code == LV_EVENT_RELEASED ||
            code == LV_EVENT_PRESS_LOST ||
            code == LV_EVENT_SCROLL_BEGIN ||
            code == LV_EVENT_DEFOCUSED) {
        restore_card_scale(card);
    }
    else if(code == LV_EVENT_CLICKED) {
        // แตะเลือกสินค้าสำเร็จ
        LV_LOG_WARN("ADD NEW ITEM");
        //lv_profiler_builtin_flush();
    }
}

static void payment_opt_frame_popout_complete_cb(lv_anim_t * a){
    lv_obj_delete_async(a->var);
    payment_opt_frame = NULL;
}

void obj_pop_animation(lv_obj_t *obj, bool pop_up, const int32_t w, const int32_t h, lv_anim_completed_cb_t on_popout_complete){
  lv_obj_set_size(obj, pop_up ? 0 : lv_obj_get_width(obj), pop_up ? 0 : lv_obj_get_height(obj));

  lv_anim_t ah, aw;
  lv_anim_init(&ah); lv_anim_init(&aw);
  lv_anim_set_var(&ah, obj); lv_anim_set_var(&aw, obj);
  lv_anim_set_values(&ah, pop_up ? 0 : lv_obj_get_height(obj), pop_up ? h : 0); lv_anim_set_values(&aw, pop_up ? 0 : lv_obj_get_width(obj), pop_up ? w : 0);
  lv_anim_set_duration(&ah, pop_up ? 220 : 170); lv_anim_set_duration(&aw, pop_up ? 220 : 170);
  lv_anim_set_exec_cb(&ah, anim_set_height); lv_anim_set_exec_cb(&aw, anim_set_width);
  lv_anim_set_path_cb(&ah, lv_anim_path_overshoot); lv_anim_set_path_cb(&aw, lv_anim_path_overshoot);

  if(!pop_up && on_popout_complete){
    lv_anim_set_completed_cb(&ah, on_popout_complete);
  }

  lv_anim_start(&ah); lv_anim_start(&aw);
}

static void on_checkout_box_close(lv_event_t * e){
  obj_pop_animation(payment_opt_frame, false, 0, 0, payment_opt_frame_popout_complete_cb);

  lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_TRANSP, 0);
  lv_obj_set_clickable(lv_layer_top(), false);
  //lv_profiler_builtin_flush();
}
/*
lv_obj_t * vd_badge_create(lv_obj_t * parent, const char * text, const lv_font_t * font, lv_color_t bg_color, lv_color_t text_color) {
    // 1. ตัวกล่อง Badge ขยายตามข้อความอัตโนมัติ (LV_SIZE_CONTENT)
    lv_obj_t * badge = lv_obj_create(parent);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(badge, bg_color, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(badge, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0); // ทรง Pill ปลายมน
    lv_obj_set_style_pad_hor(badge, 8, 0);               // Padding ซ้าย-ขวา 8px
    lv_obj_set_style_pad_ver(badge, 3, 0);               // Padding บน-ล่าง 3px
    lv_obj_set_scrollable(badge, false);

    // 2. ข้อความภายใน Badge
    lv_obj_t * label = lv_label_create(badge);
    if(font) lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, text_color, 0);
    lv_label_set_text(label, text);
    lv_obj_set_align(label, LV_ALIGN_CENTER);

    return badge;
}*/

/*
static void loading_bar_anim_cb(void * var, int32_t t) {
    lv_obj_t * indicator = (lv_obj_t *)var;
    lv_obj_t * track = lv_obj_get_parent(indicator);
    int32_t w = lv_obj_get_width(track);

    // คำนวณ Cubic Easing (0 - 1200ms)
    int64_t t3 = (int64_t)t * t * t;
    int64_t inv_t = 1200 - t;
    int64_t inv_t3 = inv_t * inv_t * inv_t;

    // คำนวณตำแหน่งหัว (head) และหาง (tail)
    int32_t tail_x = (int32_t)((t3 * w) / 1728000000LL);
    int32_t head_x = (int32_t)(((1728000000LL - inv_t3) * w) / 1728000000LL);

    int32_t bar_w = head_x - tail_x;
    if(bar_w < 0) bar_w = 0;

    // อัปเดตพิกัดและความกว้างให้แท่งแสงใน LVGL
    lv_obj_set_x(indicator, tail_x);
    lv_obj_set_width(indicator, bar_w);
}

lv_obj_t * vd_loading_bar_create(lv_obj_t * parent, int32_t w, int32_t h, lv_color_t bar_color, lv_color_t bg_color) {
    // 1. กล่องลู่ทางวิ่ง (Track Background)
    lv_obj_t * track = lv_obj_create(parent);
    lv_obj_set_size(track, w, h);
    lv_obj_set_style_bg_color(track, bg_color, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0); // ทำขอบมนทรง Pill
    lv_obj_set_style_clip_corner(track, true, 0);        // บังคับตัดขอบมนแท่งแสงด้านใน
    lv_obj_set_style_border_opa(track, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(track, 0, 0);
    lv_obj_set_scrollable(track, false);

    lv_obj_t * indicator = lv_obj_create(track);
    lv_obj_set_size(indicator, 0, lv_pct(100)); // สูงเต็ม 100% ของ track
    lv_obj_set_style_bg_color(indicator, bar_color, 0);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_opa(indicator, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(indicator, 0, 0);
    lv_obj_set_scrollable(indicator, true);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, indicator);
    lv_anim_set_values(&a, 0, 1200); // ส่งค่าเวลา t (0 ถึง 1200ms)
    lv_anim_set_duration(&a, 1200);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); // เล่นวนไม่รู้จบ
    lv_anim_set_path_cb(&a, lv_anim_path_linear);          // Linear time (เพราะเราคำนวณ Easing เองใน cb)
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)loading_bar_anim_cb);
    lv_anim_start(&a);

    return track;
}
*/

/* =========================================================================
 * Animation Callback: วาด Checkmark เชื่อมมุมเนียนสนิท ไม่แหว่งและไม่ปูด
 * ========================================================================= */
static void anim_draw_checkmark_cb(void *var, int32_t t) {
  lv_obj_t *canvas = (lv_obj_t *)var;

  // 1. ล้าง Canvas ให้โปร่งใส
  lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_TRANSP);

  // 2. พิกัดของเครื่องหมายถูก (Canvas 60x60 px)
  const int32_t ax = 15, ay = 30; // จุดเริ่มต้น A (ซ้าย)
  const int32_t bx = 26, by = 42; // จุดหักมุม B (ล่าง)
  const int32_t cx = 46, cy = 18; // จุดปลาย C (ขวาบน)
  const int32_t line_w = 6;       // ความหนาเส้น 6px

  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);

  if (t <= 35) {
    // --- จังหวะที่ 1: ลากเส้นจาก A -> B ---
    int32_t cur_x = ax + ((bx - ax) * t) / 35;
    int32_t cur_y = ay + ((by - ay) * t) / 35;

    lv_draw_line_dsc_t line1_dsc;
    lv_draw_line_dsc_init(&line1_dsc);
    line1_dsc.color = lv_color_hex(0x10B981);
    line1_dsc.width = line_w;
    line1_dsc.round_start = true; // มนที่จุด A
    line1_dsc.round_end = true;   // มนที่หัววิ่ง
    line1_dsc.p1 = (lv_point_precise_t){ax, ay};
    line1_dsc.p2 = (lv_point_precise_t){cur_x, cur_y};
    lv_draw_line(&layer, &line1_dsc);
  } else {
    // --- จังหวะที่ 2: เส้น A->B (มีหัวมนที่ B ทำหน้าที่เป็น Joint) + ลากเส้น B->C ---
    // 1. เส้นที่ 1 (A -> B): เปิด round_end เพื่อทำหน้าที่เป็นข้อต่อมนที่ B
    lv_draw_line_dsc_t line1_dsc;
    lv_draw_line_dsc_init(&line1_dsc);
    line1_dsc.color = lv_color_hex(0x10B981);
    line1_dsc.width = line_w;
    line1_dsc.round_start = true;
    line1_dsc.round_end = true; // ข้อต่อมนของเส้นที่ 1 จะปิดรอยแหว่งพอดี
    line1_dsc.p1 = (lv_point_precise_t){ax, ay};
    line1_dsc.p2 = (lv_point_precise_t){bx, by};
    lv_draw_line(&layer, &line1_dsc);

    // 2. เส้นที่ 2 (B -> C): ตัดตรงที่ B เพื่อรับกับข้อต่อ และมนที่ปลาย C
    int32_t t2 = t - 35;
    int32_t cur_x = bx + ((cx - bx) * t2) / 65;
    int32_t cur_y = by + ((cy - by) * t2) / 65;

    lv_draw_line_dsc_t line2_dsc;
    lv_draw_line_dsc_init(&line2_dsc);
    line2_dsc.color = lv_color_hex(0x10B981);
    line2_dsc.width = line_w;
    line2_dsc.round_start = false; // ปิดมนที่ B ป้องกันการซ้อนทับจนปูด
    line2_dsc.round_end = true;    // มนที่ปลายทาง C
    line2_dsc.p1 = (lv_point_precise_t){bx, by};
    line2_dsc.p2 = (lv_point_precise_t){cur_x, cur_y};
    lv_draw_line(&layer, &line2_dsc);
  }

  lv_canvas_finish_layer(canvas, &layer);
}

static void auto_close_payment_model_cb(lv_anim_t * a){
  on_checkout_box_close(NULL);
}

#define CHECK_CANVAS_W 64
#define CHECK_CANVAS_H 64
uint8_t check_canvas_buf[CHECK_CANVAS_W * CHECK_CANVAS_H * 4] __attribute__((aligned(64)));

void show_payment_success_animation(lv_obj_t * modal_frame, const char * paid_amount_str, uint32_t timeout_ms) {
    if(modal_frame == NULL || !lv_obj_is_in_widget_tree(modal_frame)) return;

    // อัปเดตพิกัดและขนาดเพื่ออ่านความกว้างจริงของ Dialog
    lv_obj_update_layout(modal_frame);
    int32_t modal_w = lv_obj_get_width(modal_frame);
    if(modal_w <= 0) modal_w = 600;

    // 1. [Green Flood Layer] เลเยอร์พื้นหลังสีเขียว Fade-In ทับหน้าต่างเดิม
    lv_obj_t * green_layer = lv_obj_create(modal_frame);
    lv_obj_set_size(green_layer, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(green_layer, 0, 0);
    lv_obj_set_style_bg_color(green_layer, lv_color_hex(0x10B981), 0); // เขียว Emerald
    lv_obj_set_style_bg_opa(green_layer, 0, 0);                       // เริ่มที่โปร่งใส 0
    lv_obj_set_style_border_opa(green_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(green_layer, 0, 0);
    lv_obj_set_style_clip_corner(green_layer, true, 0); // ตัดมุมโค้งมนด้านบนไม่ให้แถบเวลาล้นกรอบ
    lv_obj_set_style_pad_all(green_layer, 0, 0);
    lv_obj_set_scrollable(green_layer, false);
    lv_obj_set_style_radius(green_layer, 20, 0);

    lv_anim_t a_green;
    lv_anim_init(&a_green);
    lv_anim_set_var(&a_green, green_layer);
    lv_anim_set_values(&a_green, 0, LV_OPA_COVER);
    lv_anim_set_duration(&a_green, 250);
    lv_anim_set_path_cb(&a_green, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a_green, (lv_anim_exec_xcb_t)anim_set_bg_opa);
    lv_anim_start(&a_green);

    // =========================================================================
    // [Countdown Bar]: แถบเวลานับถอยหลังชิดขอบบนสุดของกล่อง
    // =========================================================================
    // 1.1 ลู่ทางวิ่งด้านหลัง (สีขาวโปร่งแสงบางๆ)
    lv_obj_t * progress_track = lv_obj_create(green_layer);
    lv_obj_set_size(progress_track, lv_pct(100), 5);
    lv_obj_align(progress_track, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(progress_track, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(progress_track, 0, 0);
    lv_obj_set_style_pad_all(progress_track, 0, 0);
    lv_obj_set_style_radius(progress_track, 0, 0);
    lv_obj_set_scrollable(progress_track, false);

    // 1.2 แถบสีขาวนับถอยหลัง
    lv_obj_t * progress_bar = lv_obj_create(progress_track);
    lv_obj_set_size(progress_bar, modal_w, lv_pct(100));
    lv_obj_align(progress_bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(progress_bar, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(progress_bar, LV_OPA_90, 0);
    lv_obj_set_style_border_width(progress_bar, 0, 0);
    lv_obj_set_style_pad_all(progress_bar, 0, 0);
    lv_obj_set_style_radius(progress_bar, 4, 0);
    lv_obj_set_scrollable(progress_bar, false);

    // แอนิเมชันลดขนาดความกว้าง (modal_w -> 0)
    lv_anim_t a_progress;
    lv_anim_init(&a_progress);
    lv_anim_set_var(&a_progress, progress_bar);
    lv_anim_set_values(&a_progress, modal_w, 0);
    lv_anim_set_duration(&a_progress, timeout_ms); // ระยะเวลา 4.0 วินาที
    lv_anim_set_delay(&a_progress, 250);     // เริ่มนับถอยหลังหลังจากพื้นเขียวเปิดเต็มที่
    lv_anim_set_path_cb(&a_progress, lv_anim_path_linear);
    lv_anim_set_exec_cb(&a_progress, (lv_anim_exec_xcb_t)anim_set_width);
    lv_anim_set_completed_cb(&a_progress, auto_close_payment_model_cb);
    lv_anim_start(&a_progress);

    // 2. [Success Content Box] กล่องรวมเนื้อหากึ่งกลาง
    lv_obj_t * content_box = lv_obj_create(green_layer);
    lv_obj_set_size(content_box, lv_pct(100), lv_pct(100));
    lv_obj_set_align(content_box, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(content_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(content_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(content_box, 0, 0);
    lv_obj_set_scrollable(content_box, false);

    lv_obj_t * icon_circle = lv_obj_create(content_box);
    lv_obj_set_size(icon_circle, 0, 0); // ตั้งขนาดเริ่มต้นเป็น 0 (แทนการสเกล)
    lv_obj_set_style_radius(icon_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(icon_circle, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(icon_circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(icon_circle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(icon_circle, 20, 0);
    lv_obj_set_style_shadow_color(icon_circle, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(icon_circle, (lv_opa_t)LV_OPA_20, 0);
    lv_obj_align(icon_circle, LV_ALIGN_CENTER, 0, -55);
    lv_obj_set_scrollable(icon_circle, false);

    // =========================================================================
    // สร้าง Canvas ซ้อนตรงกลางวงกลม
    // =========================================================================
    lv_obj_t * check_canvas = lv_canvas_create(icon_circle);
    lv_canvas_set_buffer(check_canvas, check_canvas_buf, CHECK_CANVAS_W, CHECK_CANVAS_H, LV_COLOR_FORMAT_ARGB8888);
    lv_canvas_fill_bg(check_canvas, lv_color_white(), LV_OPA_TRANSP);
    lv_obj_align(check_canvas, LV_ALIGN_CENTER, 0, 0);

    // [แก้ไข]: เปลี่ยนจาก Scale 0->256 มาเป็นเปลี่ยนขนาด Size 0->80 px
    lv_anim_t a_icon;
    lv_anim_init(&a_icon);
    lv_anim_set_var(&a_icon, icon_circle);
    lv_anim_set_values(&a_icon, 0, 80); // ขนาดเส้นผ่านศูนย์กลาง 0 -> 80px
    lv_anim_set_duration(&a_icon, 300);
    lv_anim_set_delay(&a_icon, 200);
    lv_anim_set_path_cb(&a_icon, lv_anim_path_overshoot);
    lv_anim_set_exec_cb(&a_icon, (lv_anim_exec_xcb_t)anim_set_circle_size);
    lv_anim_start(&a_icon);

    // แอนิเมชันเริ่มวาดเส้น Checkmark ตามปกติ
    lv_anim_t a_draw;
    lv_anim_init(&a_draw);
    lv_anim_set_var(&a_draw, check_canvas);
    lv_anim_set_values(&a_draw, 0, 100);
    lv_anim_set_duration(&a_draw, 350);
    lv_anim_set_delay(&a_draw, 400); // เริ่มวาดตอนวงกลมขยายตัวเต็มที่
    lv_anim_set_path_cb(&a_draw, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a_draw, (lv_anim_exec_xcb_t)anim_draw_checkmark_cb);
    lv_anim_start(&a_draw);

    // 4. ข้อความหัวข้อ "ชำระเงินสำเร็จ"
    lv_obj_t * title = lv_label_create(content_box);
    lv_obj_set_style_text_font(title, &anuphan_bold_26, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "ชำระเงินสำเร็จ");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 15);
    lv_obj_set_style_opa(title, 0, 0);
    lv_obj_set_style_translate_y(title, 15, 0);

    // 5. ข้อความรอง "กรุณารับสินค้าที่ช่องรับด้านล่าง"
    lv_obj_t * subtitle = lv_label_create(content_box);
    lv_obj_set_style_text_font(subtitle, &anuphan_med_18, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xD1FAE5), 0);
    lv_label_set_text(subtitle, "กรุณารับสินค้าที่ช่องรับด้านล่าง");
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 45);
    lv_obj_set_style_opa(subtitle, 0, 0);
    lv_obj_set_style_translate_y(subtitle, 15, 0);

    // 6. ป้ายกำกับยอดเงิน
    //char badge_txt[64];
    //snprintf(badge_txt, sizeof(badge_txt), "ชำระแล้ว %s", paid_amount_str);
    //lv_obj_t * badge = vd_badge_create(content_box, badge_txt, &anuphan_14, lv_color_hex(0x047857), lv_color_white());
    //lv_obj_align(badge, LV_ALIGN_CENTER, 0, 85);
    //lv_obj_set_style_opa(badge, 0, 0);
    //lv_obj_set_style_translate_y(badge, 15, 0);

    // Slide Up + Fade-in สำหรับ Title
    lv_anim_t a_title_opa, a_title_pos;
    lv_anim_init(&a_title_opa); lv_anim_init(&a_title_pos);
    lv_anim_set_var(&a_title_opa, title); lv_anim_set_var(&a_title_pos, title);
    lv_anim_set_values(&a_title_opa, 0, LV_OPA_COVER);
    lv_anim_set_values(&a_title_pos, 15, 0);
    lv_anim_set_duration(&a_title_opa, 250); lv_anim_set_duration(&a_title_pos, 250);
    lv_anim_set_delay(&a_title_opa, 300); lv_anim_set_delay(&a_title_pos, 300);
    lv_anim_set_path_cb(&a_title_pos, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a_title_opa, (lv_anim_exec_xcb_t)anim_set_opa);
    lv_anim_set_exec_cb(&a_title_pos, (lv_anim_exec_xcb_t)anim_set_translate_y);
    lv_anim_start(&a_title_opa); lv_anim_start(&a_title_pos);

    // Slide Up + Fade-in สำหรับ Subtitle
    lv_anim_t a_sub_opa, a_sub_pos;
    lv_anim_init(&a_sub_opa); lv_anim_init(&a_sub_pos);
    lv_anim_set_var(&a_sub_opa, subtitle); lv_anim_set_var(&a_sub_pos, subtitle);
    lv_anim_set_values(&a_sub_opa, 0, LV_OPA_COVER);
    lv_anim_set_values(&a_sub_pos, 15, 0);
    lv_anim_set_duration(&a_sub_opa, 250); lv_anim_set_duration(&a_sub_pos, 250);
    lv_anim_set_delay(&a_sub_opa, 380); lv_anim_set_delay(&a_sub_pos, 380);
    lv_anim_set_path_cb(&a_sub_pos, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a_sub_opa, (lv_anim_exec_xcb_t)anim_set_opa);
    lv_anim_set_exec_cb(&a_sub_pos, (lv_anim_exec_xcb_t)anim_set_translate_y);
    lv_anim_start(&a_sub_opa); lv_anim_start(&a_sub_pos);

    // Slide Up + Fade-in สำหรับ Badge
    //lv_anim_t a_badge_opa, a_badge_pos;
    //lv_anim_init(&a_badge_opa); lv_anim_init(&a_badge_pos);
    //lv_anim_set_var(&a_badge_opa, badge); lv_anim_set_var(&a_badge_pos, badge);
    //lv_anim_set_values(&a_badge_opa, 0, LV_OPA_COVER);
    //lv_anim_set_values(&a_badge_pos, 15, 0);
    //lv_anim_set_duration(&a_badge_opa, 250); lv_anim_set_duration(&a_badge_pos, 250);
    //lv_anim_set_delay(&a_badge_opa, 450); lv_anim_set_delay(&a_badge_pos, 450);
    //lv_anim_set_path_cb(&a_badge_pos, lv_anim_path_ease_out);
    //lv_anim_set_exec_cb(&a_badge_opa, (lv_anim_exec_xcb_t)anim_set_opa);
    //lv_anim_set_exec_cb(&a_badge_pos, (lv_anim_exec_xcb_t)anim_set_translate_y);
    //lv_anim_start(&a_badge_opa); lv_anim_start(&a_badge_pos);
    //lv_profiler_builtin_flush();
}

static void qr_load_ok_test(lv_event_t * e){
  lv_obj_delete_async(loading_bar);
  loading_bar = NULL;

  lv_anim_t paym_label_fadein_anim;
  lv_anim_init(&paym_label_fadein_anim);
  lv_anim_set_var(&paym_label_fadein_anim, payment_topic);
  lv_anim_set_values(&paym_label_fadein_anim, 0, 255);
  lv_anim_set_duration(&paym_label_fadein_anim, 220);
  lv_anim_set_path_cb(&paym_label_fadein_anim, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&paym_label_fadein_anim, (lv_anim_exec_xcb_t)anim_set_text_opa);
  lv_anim_start(&paym_label_fadein_anim);

  int32_t paym_frame_height = lv_obj_get_height(payment_opt_frame);

    lv_anim_t paym_frame_h_anim;
    lv_anim_init(&paym_frame_h_anim);
    lv_anim_set_var(&paym_frame_h_anim, payment_opt_frame);
    lv_anim_set_values(&paym_frame_h_anim, paym_frame_height, paym_frame_height + 180);
    lv_anim_set_duration(&paym_frame_h_anim, 400);
    lv_anim_set_path_cb(&paym_frame_h_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&paym_frame_h_anim, (lv_anim_exec_xcb_t)anim_set_height);
    lv_anim_start(&paym_frame_h_anim);

    // ทดสอบ
    lv_obj_t * promptpay_logo = lv_image_create(payment_opt_frame);
    lv_image_set_src(promptpay_logo, &prompt_pay200_67);
    lv_obj_align(promptpay_logo, LV_ALIGN_TOP_MID, 0, 50);
/*
    size_t png_data_size = 0;
    raw_qr_img = base64_decode(qr_b64data, &png_data_size);

    if(raw_qr_img){
      qr_img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
      qr_img_dsc.header.cf = LV_COLOR_FORMAT_RAW;
      qr_img_dsc.header.w = 0;
      qr_img_dsc.header.h = 0;
      qr_img_dsc.data_size = png_data_size;
      qr_img_dsc.data = raw_qr_img;

      lv_obj_t * qr_img = lv_image_create(payment_opt_frame);
      lv_image_set_src(qr_img, &qr_img_dsc);
      lv_image_set_scale(qr_img, 200);
      lv_obj_align(qr_img, LV_ALIGN_CENTER, 0, -10);
      lv_obj_update_layout(qr_img);

      lv_obj_t *price_label = lv_label_create(payment_opt_frame);
      lv_obj_set_style_text_font(price_label, &anuphan_18, 0);
      lv_label_set_text(price_label, "ยอดต้องชำระรวม 200 บาท");
      lv_obj_align_to(price_label, qr_img, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    }*/
    //lv_profiler_builtin_flush();
}

static void paym_ok_test(lv_event_t * e){
  lv_obj_update_layout(payment_opt_frame);
  int32_t paym_frame_height = lv_obj_get_height(payment_opt_frame);

  lv_anim_t paym_frame_h_anim;
  lv_anim_init(&paym_frame_h_anim);
  lv_anim_set_var(&paym_frame_h_anim, payment_opt_frame);
  lv_anim_set_values(&paym_frame_h_anim, paym_frame_height, paym_frame_height - 180);
  lv_anim_set_duration(&paym_frame_h_anim, 350);
  lv_anim_set_path_cb(&paym_frame_h_anim, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&paym_frame_h_anim, (lv_anim_exec_xcb_t)anim_set_height);
  lv_anim_start(&paym_frame_h_anim);

  show_payment_success_animation(payment_opt_frame, "฿200", 4000);
  //lv_profiler_builtin_flush();
 }

 /*
static void paym_label_fadeout_comp(lv_anim_t * a){
  lv_obj_t * topic = (lv_obj_t *)a->var;

    LV_LOG_WARN("pay_type = %s", pay_type == qr_pay ? "QR พร้อมเพย์" : "จ่ายเงินสด");
    lv_label_set_text(topic, pay_type == qr_pay ? "QR พร้อมเพย์" : "จ่ายเงินสด");

    loading_bar = vd_loading_bar_create(payment_opt_frame, 280, 16, lv_color_hex(0x5B6EF5), lv_color_hex(0xEEF2FF));
    lv_obj_center(loading_bar);

    lv_obj_t *tmp_btn_paym_ok = lv_button_create(payment_opt_frame);
    lv_obj_set_align(tmp_btn_paym_ok, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_add_event_cb(tmp_btn_paym_ok, qr_load_ok_test, LV_EVENT_CLICKED, NULL);

    lv_obj_t *tmp_btn_label = lv_label_create(tmp_btn_paym_ok);
    lv_obj_set_style_text_font(tmp_btn_label, &anuphan_16, 0);
    lv_label_set_text(tmp_btn_label, "โหลด QR สำเร็จ");
    lv_obj_center(tmp_btn_label);

    lv_obj_t *tmp2_btn_paym_ok = lv_button_create(payment_opt_frame);
    lv_obj_set_align(tmp2_btn_paym_ok, LV_ALIGN_BOTTOM_RIGHT);
    lv_obj_add_event_cb(tmp2_btn_paym_ok, paym_ok_test, LV_EVENT_CLICKED, NULL);

    lv_obj_t *tmp2_btn_label = lv_label_create(tmp2_btn_paym_ok);
    lv_obj_set_style_text_font(tmp2_btn_label, &anuphan_16, 0);
    lv_label_set_text(tmp2_btn_label, "จ่ายสำเร็จ");

    //lv_profiler_builtin_flush();
}*/

static void qr_pay_selected_cb(lv_event_t *e) {
  vd_payment_opt_dialog_t *dialog = (vd_payment_opt_dialog_t*)lv_event_get_user_data(e);
  vd_payment_opt_scroll_to_loading_page(dialog, e, QR);
  LV_LOG_USER("QR Selected");

  __start_qr = true; // ทดสอบ
}

static void cash_pay_selected_cb(lv_event_t *e) {
  vd_payment_opt_dialog_t *dialog = (vd_payment_opt_dialog_t*)lv_event_get_user_data(e);
  vd_payment_opt_scroll_to_loading_page(dialog, e, CASH);
  LV_LOG_USER("CASH Selected");
}

void request_qr_cb(vd_payment_opt_dialog_t *dialog){
  __start_qr = true; // ทดสอบ
}

void on_dialog_delete_cb(vd_payment_opt_dialog_t *p_dialog){
  dialog = NULL;
  LV_LOG_USER("dialog deleted!");
}

static void pay_ok_cb(lv_event_t *e){
  vd_payment_opt_send_payment_ok_signal(dialog);
}

static void item_checkout_cb(lv_event_t *e) {
  dialog = vd_payment_opt_dialog_create(lv_layer_top(), qr_pay_selected_cb, cash_pay_selected_cb, vd_cart_get_total_items(vd_home_page));

  vd_payment_opt_card_set_badge(dialog->qr_pay_card->badge, "แนะนำ", true);
  vd_payment_opt_card_set_badge(dialog->cash_pay_card->badge, "", false);

  vd_payment_opt_set_total_amounts(dialog, vd_cart_get_total_amounts(vd_home_page));
  vd_payment_opt_set_request_new_qr_cb(dialog, request_qr_cb);
  vd_payment_opt_set_on_delete_cb(dialog, on_dialog_delete_cb);

  lv_obj_t *pay_ok_btn = lv_button_create(dialog->main);
  lv_obj_set_align(pay_ok_btn, LV_ALIGN_BOTTOM_RIGHT);
  lv_obj_add_event_cb(pay_ok_btn, pay_ok_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *pay_ok_txt = lv_label_create(pay_ok_btn);
  lv_label_set_text(pay_ok_txt, "Payok");
  lv_obj_center(pay_ok_txt);
}

lv_obj_t *video_frame, *slider, *slider_time, *play_pause_btn, *play_pause_btn_txt;
lv_image_dsc_t lvid = {};

esp_err_t display_jpeg_direct(esp_lcd_panel_handle_t panel, const char *file_path);

/* ── ฟังก์ชันเปิดไฟ Backlight ───────────────────────────────────── */
static void bsp_enable_backlight(void) {
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT
    };
    gpio_config(&bk_gpio_config);
    gpio_set_level(PIN_NUM_BK_LIGHT, 0);
}

static esp_lcd_touch_handle_t tp_handle = NULL;

static void setup_touchscreen(lv_display_t *display) {
    ESP_LOGI(TAG, "Initializing Touchscreen (GT911)...");

    vTaskDelay(pdMS_TO_TICKS(50));

    // 2. ผูก I2C IO เข้ากับ Touch Panel
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_handle, &tp_io_config, &tp_io_handle));

    // 3. กำหนดค่าคอนฟิกของ GT911
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = TOUCH_PIN_RST,
        .int_gpio_num = TOUCH_PIN_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            // หากคุณกลับหัวจอ 180 องศา ให้สลับแกนพิกัดสัมผัสตรงนี้:
            .swap_xy = 0,
            .mirror_x = 1, // กลับแกน X ให้ตรงกับภาพที่หมุน 180°
            .mirror_y = 1, // กลับแกน Y ให้ตรงกับภาพที่หมุน 180°
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp_handle));

    // 4. ลงทะเบียนเข้ากับ esp_lvgl_port
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = display,
        .handle = tp_handle,
    };
    lv_indev_t *touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (touch_indev == NULL) {
        ESP_LOGE(TAG, "Failed to register touch indev to LVGL");
    }

    lv_indev_set_long_press_repeat_time(touch_indev, 10);
}

FIL file;
static FATFS g_fatfs;
static sdmmc_card_t s_card_info;


#define LCD_H_RES   1024
#define LCD_V_RES   600

// บัฟเฟอร์เก็บภาพพิกเซล RGB565 ขนาด 1024 x 600 x 2 ไบต์ (~1.23 MB)
static uint8_t *s_fb_rgb565 = NULL;

esp_err_t display_jpeg_direct(esp_lcd_panel_handle_t panel, const char *file_path)
{
    static FIL f;
    FRESULT fr = f_open(&f, file_path, FA_READ);
    if (fr != FR_OK) {
        ESP_LOGE(TAG, "Open file failed: %s (err=%d)", file_path, fr);
        return ESP_ERR_NOT_FOUND;
    }

    size_t file_size = f_size(&f);

    // 1. อ่านไฟล์เข้า Internal DMA RAM
    int64_t t0 = esp_timer_get_time();
    uint8_t *jpeg_raw_buf = (uint8_t *)heap_caps_aligned_alloc(64, file_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!jpeg_raw_buf) {
        jpeg_raw_buf = (uint8_t *)heap_caps_aligned_alloc(64, file_size, MALLOC_CAP_SPIRAM);
        if (!jpeg_raw_buf) {
            f_close(&f);
            ESP_LOGE(TAG, "Alloc JPEG raw buffer failed");
            return ESP_ERR_NO_MEM;
        }
    }

    UINT br = 0;
    fr = f_read(&f, jpeg_raw_buf, file_size, &br);
    f_close(&f);
    if (fr != FR_OK || br != file_size) {
        free(jpeg_raw_buf);
        ESP_LOGE(TAG, "Read JPEG file incomplete");
        return ESP_FAIL;
    }
    int64_t t_read = esp_timer_get_time() - t0;

    // 2. ตรวจสอบข้อมูล Header ของภาพ
    jpeg_decoder_handle_t jpeg_dec = NULL;
    jpeg_decode_engine_cfg_t eng_cfg = {
        .intr_priority = 0,
        .timeout_ms = 1000,
    };
    ESP_ERROR_CHECK(jpeg_new_decoder_engine(&eng_cfg, &jpeg_dec));

    jpeg_decode_picture_info_t pic_info;
    esp_err_t err = jpeg_decoder_get_info(jpeg_raw_buf, file_size, &pic_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Invalid JPEG Header: %s", file_path);
        jpeg_del_decoder_engine(jpeg_dec);
        free(jpeg_raw_buf);
        return err;
    }

    // ดักข้อจำกัดที่ 1: ความกว้างและความสูงต้องหารด้วย 8 ลงตัว
    if ((pic_info.width * pic_info.height % 8) != 0) {
        ESP_LOGW(TAG, "Skip %s: Dimension (%ux%u) not divisible by 8!", 
                 file_path, pic_info.width, pic_info.height);
        jpeg_del_decoder_engine(jpeg_dec);
        free(jpeg_raw_buf);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // ดักข้อจำกัดที่ 2: คำนวณขนาดหลัง 16-byte alignment
    uint32_t aligned_w = (pic_info.width + 15) & ~15;
    uint32_t aligned_h = (pic_info.height + 15) & ~15;
    size_t required_out_bytes = aligned_w * aligned_h * 2; // RGB565

    // หากภาพใหญ่เกินกว่าหน้าจอ 1024x600 ให้ข้ามภาพนี้ไปก่อน
    if (aligned_w > LCD_H_RES || aligned_h > LCD_V_RES) {
        ESP_LOGW(TAG, "Skip %s: Resolution (%ux%u) exceeds display limit (1024x600)!", 
                 file_path, pic_info.width, pic_info.height);
        jpeg_del_decoder_engine(jpeg_dec);
        free(jpeg_raw_buf);
        return ESP_ERR_INVALID_SIZE;
    }

    // 3. เริ่มถอดรหัสด้วยฮาร์ดแวร์
    int64_t t1 = esp_timer_get_time();
    jpeg_decode_cfg_t dec_cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
    };

    if (!s_fb_rgb565) {
        s_fb_rgb565 = (uint8_t *)heap_caps_aligned_alloc(64, LCD_H_RES * LCD_V_RES * 2, MALLOC_CAP_SPIRAM);
        if (!s_fb_rgb565) {
            ESP_LOGE(TAG, "Critical: Cannot allocate display framebuffer in PSRAM!");
            jpeg_del_decoder_engine(jpeg_dec);
            free(jpeg_raw_buf);
            return ESP_ERR_NO_MEM;
        }
    }

    uint32_t out_bytes = 0;
    // ส่งขนาดบัฟเฟอร์ที่แท้จริงตามที่ฮาร์ดแวร์ต้องการ (required_out_bytes)
    err = jpeg_decoder_process(jpeg_dec, &dec_cfg, jpeg_raw_buf, file_size,
                               s_fb_rgb565, required_out_bytes, &out_bytes);
    int64_t t_decode = esp_timer_get_time() - t1;

    jpeg_del_decoder_engine(jpeg_dec);
    free(jpeg_raw_buf);

    if (err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Skip %s: Progressive JPEG not supported by hardware (Baseline only)", file_path);
        return err;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Hardware decode failed: %s", esp_err_to_name(err));
        return err;
    }

    int64_t t2 = esp_timer_get_time();

    // 2. คำนวณตำแหน่งกึ่งกลางจอโดยอิงจากขนาด Aligned
    int x_start = (LCD_H_RES > aligned_w) ? (LCD_H_RES - aligned_w) / 2 : 0;
    int y_start = (LCD_V_RES > pic_info.height) ? (LCD_V_RES - pic_info.height) / 2 : 0;

    // 3. ส่งค่า aligned_w ให้ตัวขับจอ
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 
                                              x_start, 
                                              y_start, 
                                              x_start + aligned_w, 
                                              y_start + pic_info.height, 
                                              s_fb_rgb565));
    int64_t t_draw = esp_timer_get_time() - t2;

    int64_t total_time = esp_timer_get_time() - t0;

    ESP_LOGI(TAG, "================ Direct Render Performance ================");
    ESP_LOGI(TAG, "Image Size       : %u bytes (%ux%u)", (unsigned)file_size, pic_info.width, pic_info.height);
    ESP_LOGI(TAG, "1. SD Read       : %.2f ms (Throughput: %.2f MB/s)", 
             (float)t_read / 1000.0f, ((float)file_size / 1048576.0f) / ((float)t_read / 1000000.0f));
    ESP_LOGI(TAG, "2. HW JPEG Decode: %.2f ms", (float)t_decode / 1000.0f);
    ESP_LOGI(TAG, "3. MIPI Flush    : %.2f ms", (float)t_draw / 1000.0f);
    ESP_LOGI(TAG, "Total Frame Time : %.2f ms (Potential FPS: %.1f)", 
             (float)total_time / 1000.0f, 1000000.0f / (float)total_time);
    ESP_LOGI(TAG, "===========================================================");

    return ESP_OK;
}
/*
void console_task(void *pvParameters) {
    char line[64];
    int idx = 0;

    printf("\n========================================\n");
    printf("   ESP32-P4 Video CLI Console Ready     \n");
    printf("========================================\n");

    while (1) {
        int c = getchar();
        if (c == EOF || c < 0) {
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        if (c == '\r' || c == '\n') {
            if (idx == 0) {
                printf("\n>> ");
                fflush(stdout);
                continue;
            }
            line[idx] = '\0';
            printf("\n");

            int sec = 0, min = 0;
            if (sscanf(line, "seek %d:%d", &min, &sec) == 2) {
                esp32_avidec_seek(&s_player, min * 60 + sec);
            } else if (sscanf(line, "seek %d", &sec) == 1) {
                esp32_avidec_seek(&s_player, sec);
            } else if (strcmp(line, "status") == 0) {
                uint32_t cur = esp32_avidec_get_current_sec(&s_player);
                uint32_t tot = esp32_avidec_get_total_sec(&s_player);
                printf("[STATUS] %02lu:%02lu / %02lu:%02lu (State: %d)\n",
                       (unsigned long)(cur / 60), (unsigned long)(cur % 60),
                       (unsigned long)(tot / 60), (unsigned long)(tot % 60),
                       esp32_avidec_get_state(&s_player));
            } else if (strcmp(line, "pause") == 0) {
                esp32_avidec_pause(&s_player);
            } else if (strcmp(line, "resume") == 0) {
                esp32_avidec_resume(&s_player);
            } else {
                printf("Unknown command: %s\n", line);
            }

            idx = 0;
            printf(">> ");
            fflush(stdout);
        } else if (c == '\b' || c == 127) {
            if (idx > 0) {
                idx--;
                printf("\b \b");
                fflush(stdout);
            }
        } else if (idx < sizeof(line) - 1) {
            line[idx++] = (char)c;
            putchar(c);
            fflush(stdout);
        }
    }
}*/

void prod_card_clicked_cb(vd_prod_card_t *card, lv_event_t *e){
  vd_cart_item_create(vd_home_page, card);
  vd_toast_notify_create(NULL, "เพิ่มสินค้าลงตะกร้าแล้ว", 1500);
}

uint8_t *prod_img_buf[10];
uint8_t *prod_raw_img_buf[10];
uint8_t prod_img_cnt;

lv_image_dsc_t prod_img_dsc[10];
bool __read_all_prod_img_ok = false;

void lvgl_main(void *pv_param) {
  check_task_stack("LVGL_START", NULL);
  if (lvgl_port_lock(0)) {
    tile_view = lv_tileview_create(lv_screen_active());
    sale_page = lv_tileview_add_tile(tile_view, 0, 0, LV_DIR_LEFT);
    lv_obj_set_style_pad_left(sale_page, 0, 0);

    apply_thai_font_hook((lv_font_t *)&anuphan_14);
    apply_thai_font_hook((lv_font_t *)&anuphan_16);
    apply_thai_font_hook((lv_font_t *)&anuphan_semi_bold_16);
    apply_thai_font_hook((lv_font_t *)&anuphan_bold_16);
    anuphan_18.fallback = &font_awesome_20;
    apply_thai_font_hook((lv_font_t *)&anuphan_18);
    apply_thai_font_hook((lv_font_t *)&anuphan_med_18);
    apply_thai_font_hook((lv_font_t *)&anuphan_semi_bold_18);
    apply_thai_font_hook((lv_font_t *)&anuphan_20);
    apply_thai_font_hook((lv_font_t *)&anuphan_bold_20);
    apply_thai_font_hook((lv_font_t *)&anuphan_med_22);
    apply_thai_font_hook((lv_font_t *)&anuphan_bold_18);
    apply_thai_font_hook((lv_font_t *)&anuphan_22);
    apply_thai_font_hook((lv_font_t *)&anuphan_bold_22);
    apply_thai_font_hook((lv_font_t *)&anuphan_bold_24);
    apply_thai_font_hook((lv_font_t *)&anuphan_bold_26);

    keyboard = lvgl_thai_kb_create(lv_screen_active(), &anuphan_18);

    vd_home_page = vd_home_page_create(sale_page, LCD_H_RES, LCD_V_RES, item_checkout_cb);
    lv_obj_set_state(vd_home_page->prod_search_ta, LV_STATE_FOCUSED, false);

    vd_task_mgr_register_task(xTaskGetCurrentTaskHandle());
    vd_task_manager_create(lv_layer_top());

    lvgl_port_unlock();

    log_system_memory();
    check_task_stack("LVGL_END", NULL);
  }

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10));

    if(__read_all_prod_img_ok){
      if (lvgl_port_lock(1000)) {
      __read_all_prod_img_ok = false;
      for (int i = 0; i < prod_img_cnt; i++) {
        bool is_promotion = i % 2 == 0;
        vd_prod_card_create(vd_home_page, "ข้าวผัดกะเพรา", 60.0, is_promotion ? 50.0 : 0, &prod_img_dsc[i], prod_card_clicked_cb);
      }
      lvgl_port_unlock();
    }
    }

    if (__start_qr) {
      __start_qr = false;
      vTaskDelay(pdMS_TO_TICKS(4000)); // รอจำลองการสร้าง QR

      // 1. ถือรหัสความปลอดภัยก่อนเข้ายุ่งกับ LVGL (รอจนกว่าจะได้สิทธิ์)
      if (lvgl_port_lock(1000)) { // รอคิวได้สูงสุด 1 วินาที
        size_t png_data_size = 0;

        if(raw_qr_img) {
          free(raw_qr_img);
          raw_qr_img = NULL;
        }

        raw_qr_img = vd_base64_decode(qr_b64data, &png_data_size);

        if (raw_qr_img && png_data_size > 0) {
          qr_img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
          qr_img_dsc.header.cf = LV_COLOR_FORMAT_RAW;
          qr_img_dsc.header.w = 0;
          qr_img_dsc.header.h = 0;
          qr_img_dsc.data_size = png_data_size;
          qr_img_dsc.data = raw_qr_img;

          // ตรวจสอบความถูกต้องของ Pointer ก่อนเรียกใช้
          if (dialog && dialog->qr_img) {
            vd_payment_opt_set_qr_data(dialog, &qr_img_dsc, 12000);
            vd_payment_opt_send_loading_ok_signal(dialog);
          } else {
            ESP_LOGE("PAYMENT", "Invalid dialog or qr_img pointer!");
          }
        } else {
          ESP_LOGE("PAYMENT", "Base64 decode failed!");
        }

        lvgl_port_unlock(); // ปล่อยคืนสิทธิ์ทุกครั้ง
      }
    }
  }
}

bool img_cb(const char *file_path, const char *file_name, size_t file_size, void *user_data) {
  if (prod_img_cnt == 10) {
    __read_all_prod_img_ok = false;
    return false;
  }

  esp_err_t err = vd_sd_read_file(file_path, &prod_img_buf[prod_img_cnt], &file_size, true);
  if (err != ESP_OK) return false;

  vd_img_info_t img_info = {};
  vd_img_type_t img_type = vd_img_check_type(prod_img_buf[prod_img_cnt], file_size, &img_info);

  if(img_type == VD_IMG_TYPE_JPG) {
    uint32_t out_w = 0, out_h = 0;
    size_t out_size = 0;

    // บรรทัดเดียวจบ: จองแรม Aligned + ถอดรหัสให้เสร็จสรรพ
    err = vd_img_jpg_dec(prod_img_buf[prod_img_cnt], file_size, 
                         &prod_raw_img_buf[prod_img_cnt], &out_size, &out_w, &out_h);

    if (err == ESP_OK) {
      // สร้าง LVGL Descriptor
      img_info.width  = out_w;
      img_info.height = out_h;
      vd_img_jpg_to_lv_dsc(prod_raw_img_buf[prod_img_cnt], out_size, &img_info, &prod_img_dsc[prod_img_cnt]);
    }
  }

  free(prod_img_buf[prod_img_cnt]);
  prod_img_cnt++;

  return true;
}

void sd_main(void *p){
  vd_task_mgr_register_task(xTaskGetCurrentTaskHandle());
  esp_err_t sd_err = vd_sd_mount();
    if(sd_err != ESP_OK) {
      ESP_LOGE(TAG, "SD Error!");
      __sd_ok = false;
    }

    if(__sd_ok){
      vd_sd_scan_dir("0:/prod_img", ".jpg", img_cb, NULL);
      __read_all_prod_img_ok = true;

    vd_sd_unmount();
    }

  while(1){
   vTaskDelay(pdMS_TO_TICKS(100)); 
  }
}

void app_main(void) {  
  check_task_stack("MAIN_START", NULL);

  rv_utils_enable_fpu();

    ESP_LOGI(TAG, "Initializing MIPI-DSI Host...");

    ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = 3,//TEST_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = 2500,//TEST_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));

    ESP_LOGI(TAG, "Initialize MIPI DSI bus");
    esp_lcd_dsi_bus_config_t bus_config = EK79007_PANEL_BUS_DSI_2CH_CONFIG();
    bus_config.lane_bit_rate_mbps = 1200;
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_dbi_io_config_t dbi_config = EK79007_PANEL_IO_DBI_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));

    ESP_LOGI(TAG, "Install LCD driver of ek79007");
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    esp_lcd_dpi_panel_config_t dpi_config = EK79007_1024_600_PANEL_60HZ_CONFIG_CF(LCD_COLOR_FMT_RGB565);
    dpi_config.dpi_clock_freq_mhz = 70;
    dpi_config.num_fbs = 2;
#else
    esp_lcd_dpi_panel_config_t dpi_config = EK79007_1024_600_PANEL_60HZ_CONFIG(TEST_MIPI_DPI_PX_FORMAT);
#endif
    ek79007_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = 2,//TEST_MIPI_DSI_LANE_NUM,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = 33,//TEST_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,//TEST_LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_ek79007(mipi_dbi_io, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_enable_dma2d(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    bsp_enable_backlight();

    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = TOUCH_I2C_SCL,
        .sda_io_num = TOUCH_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle));

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel_handle,
        .buffer_size = LCD_H_RES * LCD_V_RES,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .swap_bytes = false,
            .buff_dma = true,
            .buff_spiram = true,
            .direct_mode = true,
            .sw_rotate = true,
        }
    };

    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
            .avoid_tearing = true, // เปิดสร้าง Semaphore รอ VSYNC เพื่อภาพไร้รอยฉีก
        }
    };

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack      = 16 * 1024;
    lvgl_cfg.timer_period_ms = 2;
    esp_err_t err = lvgl_port_init(&lvgl_cfg);

    display = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);

    extern lvgl_port_ctx_t lvgl_port_ctx;

    if(lvgl_port_ctx.lvgl_task) vd_task_mgr_register_task(lvgl_port_ctx.lvgl_task);

    if(lvgl_port_lock(100)){
      setup_touchscreen(display);
      lvgl_port_unlock();
    }

    xTaskCreate(sd_main, "sd_main", 16 * 1024, NULL, 2, NULL);
    xTaskCreate(lvgl_main, "lv_main", 8 * 1024, NULL, 3, NULL);
    check_task_stack("MAIN_END", NULL);
    return;
}