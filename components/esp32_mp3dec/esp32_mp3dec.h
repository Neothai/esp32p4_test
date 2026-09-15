#ifndef ESP32_MP3DEC_H
#define ESP32_MP3DEC_H

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "helix/pub/mp3dec.h"
#include "audio_eq.h"
#include "ff.h"

#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ESP32_MP3DEC_TAG "ESP_MP3DEC"

#define DEFAULT_I2S_BCLK_IO     GPIO_NUM_21
#define DEFAULT_I2S_WS_IO       GPIO_NUM_22
#define DEFAULT_I2S_DOUT_IO     GPIO_NUM_23

#define IN_BUF_SIZE             (4096)
#define MIN_REFILL_SIZE         (2048)
#define MAX_PCM_SAMPLES         (1152 * 2)

typedef struct esp32_mp3dec_t {
  i2s_chan_handle_t i2s_hnd;
  i2s_chan_config_t chan_cfg;
  BaseType_t mp3_task;
  HMP3Decoder mp3_dec;
  MP3FrameInfo mp3_frame_info;
  
  const uint8_t *orig_mp3_buff;
  const uint8_t *mp3_buff;
  
  FIL file;
  bool is_file_open;
  uint8_t *file_in_buf;
  uint32_t file_audio_offset;
  bool is_file_source;
  bool eof_reached;

  size_t mp3_size;
  size_t bytes_left;
  int16_t *pcm_buff;
  int32_t *pcm32_buff;
  
  uint32_t frame_cnt;
  bool hw_is_init;
  bool start_dec;
  bool is_paused;
  int32_t seek_target_sec;

  uint8_t volume;
  audio_eq_3band_t eq;
} esp32_mp3dec_t;

void     esp32_mp3dec_init(esp32_mp3dec_t *mp3);
void     esp32_mp3dec_hw_init(esp32_mp3dec_t *mp3, gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din);
void     esp32_mp3dec_set_data(esp32_mp3dec_t *mp3, const uint8_t *data, size_t data_size);
void     esp32_mp3dec_set_file(esp32_mp3dec_t *mp3, const char *filepath);
void     esp32_mp3dec_start(esp32_mp3dec_t *mp3);
void     esp32_mp3dec_stop(esp32_mp3dec_t *mp3);

void     esp32_mp3dec_pause(esp32_mp3dec_t *mp3);
void     esp32_mp3dec_resume(esp32_mp3dec_t *mp3);
bool     esp32_mp3dec_is_paused(esp32_mp3dec_t *mp3);
uint32_t esp32_mp3dec_get_current_sec(esp32_mp3dec_t *mp3);
uint32_t esp32_mp3dec_get_total_sec(esp32_mp3dec_t *mp3);
void     esp32_mp3dec_seek(esp32_mp3dec_t *mp3, int32_t target_sec);

void     esp32_mp3dec_set_volume(esp32_mp3dec_t *mp3, uint8_t volume);
uint8_t  esp32_mp3dec_get_volume(esp32_mp3dec_t *mp3);

/**
 * @brief  ตั้งค่า EQ gain (-12 ถึง +12 dB)
 */
void     esp32_mp3dec_set_eq(esp32_mp3dec_t *mp3, int8_t bass_db, int8_t mid_db, int8_t treble_db);

/**
 * @brief  ตั้งค่าความถี่เสียงทุ้ม (20-500 Hz) เช่น 50, 80, 100, 150
 */
void     esp32_mp3dec_set_eq_bass_freq(esp32_mp3dec_t *mp3, uint16_t freq_hz);

#endif // ESP32_MP3DEC_H
