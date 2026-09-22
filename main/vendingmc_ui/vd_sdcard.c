#include "vd_sdcard.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

#include "sdmmc_cmd.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#define TAG "VD_SD"

static sd_pwr_ctrl_handle_t _vd_sd_pwr_ctrl_handle = NULL;
static sdmmc_host_t _sd_mmc_host   = SDMMC_HOST_DEFAULT();
static sdmmc_card_t _sd_mmc_card;
static FATFS _sd_mmc_fatfs;
static bool _internal_sd_host_init = false;

static const _vd_sd_profile_t _sd_mmc_profiles[] = {
  //{"UHS-I SDR50 (100MHz 4-bit)", SDMMC_FREQ_SDR50,     1.8f, SDMMC_SLOT_FLAG_UHS1, 4},
  {"UHS-I DDR50 (50MHz 4-bit)",  SDMMC_FREQ_DDR50,     1.8f, SDMMC_SLOT_FLAG_UHS1, 4},
  {"High-Speed (40MHz 4-bit)",   SDMMC_FREQ_HIGHSPEED, 3.3f, 0,                    4},
  {"Standard (20MHz 4-bit)",     SDMMC_FREQ_DEFAULT,   3.3f, 0,                    4},
  {"Fail-Safe (20MHz 1-bit)",    SDMMC_FREQ_DEFAULT,   3.3f, 0,                    1},
};

#define _VD_SD_MMC_PROFILE_COUNT (sizeof(_sd_mmc_profiles) / sizeof(_sd_mmc_profiles[0]))

void _vd_sd_delay(uint32_t ms){
  vTaskDelay(pdMS_TO_TICKS(ms));
}

esp_err_t _vd_sd_mmc_init(sdmmc_host_t *host, sdmmc_card_t *out_card_info) {
  esp_err_t err = ESP_FAIL;

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.clk    = _VD_SD_MMC_CLK;
  slot_config.cmd    = _VD_SD_MMC_CMD;
  slot_config.d0     = _VD_SD_MMC_D0;
  slot_config.d1     = _VD_SD_MMC_D1;
  slot_config.d2     = _VD_SD_MMC_D2;
  slot_config.d3     = _VD_SD_MMC_D3;
  slot_config.cd     = SDMMC_SLOT_NO_CD;
  slot_config.wp     = SDMMC_SLOT_NO_WP;
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  for (uint8_t i = 0; i < _VD_SD_MMC_PROFILE_COUNT; i++) {
    for (int j = 0; j < _VD_SD_PROFILE_TRY_COUNT; j++) {
      _vd_sd_delay(850);
      const _vd_sd_profile_t *prof = &_sd_mmc_profiles[i];
      ESP_LOGW(TAG, ">> Trying Profile [%d/%d]: %s...", (int)i + 1,
               (int)_VD_SD_MMC_PROFILE_COUNT, prof->name);

      host->max_freq_khz = prof->freq_khz;
      host->io_voltage = prof->io_voltage;

      slot_config.width = prof->bus_width;
      slot_config.flags |= prof->slot_flags;

      err = sdmmc_host_init_slot(host->slot, &slot_config);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init slot for %s", prof->name);
        sdmmc_host_deinit_slot(host->slot);
        continue;
      }

      _vd_sd_delay(250);

      err = sdmmc_card_init(host, out_card_info);
      if (err == ESP_OK) {
        ESP_LOGI(TAG, "SUCCESS! SD Card locked at: %s", prof->name);
        sdmmc_card_print_info(stdout, out_card_info);
        return ESP_OK;
      }

      ESP_LOGW(TAG, "Profile [%s] failed with err=0x%x (%s), stepping down...",
               prof->name, err, esp_err_to_name(err));
    }
  }

  return err; // ล้มเหลวครบทุกระดับ
}

esp_err_t vd_sd_mount(void) {
  esp_err_t err = ESP_FAIL;

  if(!_internal_sd_host_init) {
    _sd_mmc_host.slot = SDMMC_HOST_SLOT_0;

    err = sdmmc_host_init();
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "SDMMC Host Controller initialized by app_main");
    } else if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_STATE) {
      ESP_LOGI(TAG, "SDMMC Host Controller already initialized");
    } else {
      return err;
    }

    sd_pwr_ctrl_ldo_config_t ldo_config = { .ldo_chan_id = 4 };

    err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &_vd_sd_pwr_ctrl_handle);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to create a new on-chip LDO power control driver");
      return err;
    }

    _sd_mmc_host.pwr_ctrl_handle = _vd_sd_pwr_ctrl_handle;
    _sd_mmc_host.current_limit   = SDMMC_CURRENT_LIMIT_800MA;

    _internal_sd_host_init       = true;
  }

  err = _vd_sd_mmc_init(&_sd_mmc_host, &_sd_mmc_card);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize SD card in all profiles! (Card missing or broken)");
    return err;
  }

  diskio_register_sd_card(&_sd_mmc_card);

  FRESULT fr = f_mount(&_sd_mmc_fatfs, _VD_SD_MOUNT_POINT, 1);
  if (fr != FR_OK) {
    ESP_LOGI(TAG, "Mount SD filesystem fail, error: %d", fr);
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t vd_sd_unmount(void) {
  f_unmount(_VD_SD_MOUNT_POINT);
  return sdmmc_host_deinit_slot(_sd_mmc_host.slot);
}

esp_err_t vd_sd_read_file(const char *path, uint8_t **out_buf, size_t *out_size, bool prefer_psram) {
    if (!path || !out_buf) return ESP_ERR_INVALID_ARG;

    FIL f;
    FRESULT fr = f_open(&f, path, FA_READ);
    if (fr != FR_OK) {
        ESP_LOGE(TAG, "Open failed: %s (err: %d)", path, fr);
        return ESP_ERR_NOT_FOUND;
    }

    size_t size = f_size(&f);
    if (out_size) *out_size = size;

    // เผื่อ 64-byte alignment สำหรับฮาร์ดแวร์ ESP32-P4 และแถม 1 ไบต์สำหรับ Null-terminator
    uint32_t caps = prefer_psram ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    uint8_t *buf = (uint8_t *)heap_caps_aligned_alloc(64, size + 1, caps);
    
    if (!buf && prefer_psram) {
        // Fallback หาแรมภายในกรณี PSRAM จัดสรรไม่ผ่าน
        buf = (uint8_t *)heap_caps_aligned_alloc(64, size + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    }

    if (!buf) {
        f_close(&f);
        ESP_LOGE(TAG, "No memory for file read (%zu bytes)", size);
        return ESP_ERR_NO_MEM;
    }

    UINT br = 0;
    fr = f_read(&f, buf, size, &br);
    f_close(&f);

    if (fr != FR_OK || br != size) {
        heap_caps_free(buf);
        return ESP_FAIL;
    }

    buf[size] = '\0'; // เผื่ออ่านไฟล์ข้อความ
    *out_buf = buf;
    return ESP_OK;
}

esp_err_t vd_sd_write_file(const char *path, const void *data, size_t size) {
    if (!path || (!data && size > 0)) return ESP_ERR_INVALID_ARG;

    FIL f;
    FRESULT fr = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) return ESP_FAIL;

    UINT bw = 0;
    fr = f_write(&f, data, size, &bw);
    f_close(&f);

    return (fr == FR_OK && bw == size) ? ESP_OK : ESP_FAIL;
}

esp_err_t vd_sd_append_file(const char *path, const void *data, size_t size) {
    if (!path || (!data && size > 0)) return ESP_ERR_INVALID_ARG;

    FIL f;
    FRESULT fr = f_open(&f, path, FA_OPEN_APPEND | FA_WRITE);
    if (fr != FR_OK) return ESP_FAIL;

    UINT bw = 0;
    fr = f_write(&f, data, size, &bw);
    f_close(&f);

    return (fr == FR_OK && bw == size) ? ESP_OK : ESP_FAIL;
}

bool vd_sd_file_exists(const char *path) {
    FILINFO fno;
    return (f_stat(path, &fno) == FR_OK);
}

esp_err_t vd_sd_delete(const char *path) {
    FRESULT fr = f_unlink(path);
    return (fr == FR_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t vd_sd_move(const char *src_path, const char *dst_path) {
    FRESULT fr = f_rename(src_path, dst_path);
    return (fr == FR_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t vd_sd_copy(const char *src_path, const char *dst_path) {
    FIL f_src, f_dst;
    FRESULT fr = f_open(&f_src, src_path, FA_READ);
    if (fr != FR_OK) return ESP_ERR_NOT_FOUND;

    fr = f_open(&f_dst, dst_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        f_close(&f_src);
        return ESP_FAIL;
    }

    // ใช้ Chunk Buffer 16KB จองใน Internal DMA เพื่อให้ก๊อปปี้ได้เร็วที่สุด
    const size_t chunk_size = 16 * 1024;
    uint8_t *buf = (uint8_t *)heap_caps_malloc(chunk_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!buf) {
        buf = (uint8_t *)malloc(4096);
    }

    UINT br = 0, bw = 0;
    esp_err_t ret = ESP_OK;

    while (f_read(&f_src, buf, chunk_size, &br) == FR_OK && br > 0) {
        if (f_write(&f_dst, buf, br, &bw) != FR_OK || bw != br) {
            ret = ESP_FAIL;
            break;
        }
    }

    free(buf);
    f_close(&f_src);
    f_close(&f_dst);
    return ret;
}

esp_err_t vd_sd_scan_dir(const char *dir_path, const char *ext_filter, vd_sd_file_cb_t cb, void *user_data) {
    if (!dir_path || !cb) return ESP_ERR_INVALID_ARG;

    DIR dir;
    FILINFO fno;
    FRESULT fr = f_opendir(&dir, dir_path);
    if (fr != FR_OK) return ESP_FAIL;

    char full_path[256];
    size_t ext_len = ext_filter ? strlen(ext_filter) : 0;

    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
        // ข้ามโฟลเดอร์ย่อย
        if (fno.fattrib & AM_DIR) continue;

        // ตรวจสอบตัวกรองนามสกุล
        if (ext_len > 0) {
            size_t name_len = strlen(fno.fname);
            if (name_len < ext_len) continue;
            // เช็คเคสตัวพิมพ์เล็ก-ใหญ่ไม่สน
            if (strcasecmp(&fno.fname[name_len - ext_len], ext_filter) != 0) {
                continue;
            }
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, fno.fname);
        
        // ส่งเข้า callback หาก callback คืนค่า false ให้หยุดสแกน
        if (!cb(full_path, fno.fname, fno.fsize, user_data)) {
            break;
        }
    }

    f_closedir(&dir);
    return ESP_OK;
}