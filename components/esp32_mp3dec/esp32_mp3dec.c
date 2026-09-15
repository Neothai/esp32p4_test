#include "esp32_mp3dec.h"

/*
static void _hw_i2s_init(esp32_mp3dec_t *mp3, gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din) {
  mp3->chan_cfg = (i2s_chan_config_t)I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&mp3->chan_cfg, &mp3->i2s_hnd, NULL));

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
        .mclk = mclk,
        .bclk = bclk,
        .ws   = ws,
        .dout = dout,
        .din  = din,
        .invert_flags = {
          .mclk_inv = false,
          .bclk_inv = false,
          .ws_inv   = false,
        },
      },
  };

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(mp3->i2s_hnd, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(mp3->i2s_hnd));
}
*/

static void _hw_i2s_init(esp32_mp3dec_t *mp3, gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din) {
    mp3->chan_cfg = (i2s_chan_config_t)I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    // บังคับให้ DMA ส่งความเงียบ (0x00) ทันทีที่ข้อมูลหมด ป้องกันเสียงค้างวนลูป
    mp3->chan_cfg.auto_clear = true;
    
    ESP_ERROR_CHECK(i2s_new_channel(&mp3->chan_cfg, &mp3->i2s_hnd, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = 44100,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256, // ES8311 ต้องการ 256 * Fs
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = mclk, // ต้องระบุขา MCLK (เช่น GPIO_NUM_13)
            .bclk = bclk,
            .ws   = ws,
            .dout = dout,
            .din  = din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(mp3->i2s_hnd, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(mp3->i2s_hnd));
}

static void i2s_reconfig_sample_rate(esp32_mp3dec_t *mp3, uint32_t sample_rate){
  i2s_std_clk_config_t conf = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
  ESP_ERROR_CHECK(i2s_channel_disable(mp3->i2s_hnd));
  ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(mp3->i2s_hnd, &conf));
  ESP_ERROR_CHECK(i2s_channel_enable(mp3->i2s_hnd));
}

void _esp32_mp3dec_task(void *param){
  esp32_mp3dec_t *mp3 = (esp32_mp3dec_t*)param;
  while(1){
    if(!mp3->start_dec){
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    bool mp3_details_display = false;
    uint8_t skip_frame_on_seek_count = 0;
    
    while (mp3->start_dec) {
      if (mp3->is_paused) {
        vTaskDelay(pdMS_TO_TICKS(20));
        continue;
      }

      if (mp3->seek_target_sec >= 0) {
        uint32_t target_sec = (uint32_t)mp3->seek_target_sec;
        mp3->seek_target_sec = -1;

        if (mp3->mp3_frame_info.bitrate > 0) {
          size_t target_byte = (size_t)(((uint64_t)target_sec * mp3->mp3_frame_info.bitrate) / 8);

          if (mp3->is_file_source && mp3->is_file_open) {
            f_lseek(&mp3->file, mp3->file_audio_offset + target_byte);
            mp3->bytes_left = 0;
            mp3->eof_reached = false;
          } else {
            if (target_byte >= mp3->mp3_size) {
              target_byte = (mp3->mp3_size > 4096) ? (mp3->mp3_size - 4096) : 0;
            }
            mp3->mp3_buff   = mp3->orig_mp3_buff + target_byte;
            mp3->bytes_left = mp3->mp3_size - target_byte;
          }

          if (mp3->mp3_frame_info.samprate > 0) {
            mp3->frame_cnt = (target_sec * mp3->mp3_frame_info.samprate) / 1152;
          }

          ESP_LOGI(ESP32_MP3DEC_TAG, "Seeked to %lu sec", (unsigned long)target_sec);

          audio_eq_reset_state(&mp3->eq);
          skip_frame_on_seek_count = 4;
        }
      }

      if (mp3->is_file_source && mp3->is_file_open) {
        if (mp3->bytes_left < MIN_REFILL_SIZE && !mp3->eof_reached) {
          if (mp3->bytes_left > 0 && mp3->mp3_buff != mp3->file_in_buf) {
            memmove(mp3->file_in_buf, mp3->mp3_buff, mp3->bytes_left);
          }
          mp3->mp3_buff = mp3->file_in_buf;

          size_t to_read = IN_BUF_SIZE - mp3->bytes_left;
          UINT read_bytes = 0;
          FRESULT fr = f_read(&mp3->file, mp3->file_in_buf + mp3->bytes_left, (UINT)to_read, &read_bytes);
          
          if (fr == FR_OK) {
            mp3->bytes_left += read_bytes;
            if (read_bytes < to_read) {
              mp3->eof_reached = true;
            }
          } else {
            ESP_LOGE(ESP32_MP3DEC_TAG, "FatFs Read Error: %d", fr);
            mp3->eof_reached = true;
          }
        }
        if (mp3->bytes_left <= 0 && mp3->eof_reached) break;
      } else {
        if (mp3->bytes_left <= 0) break;
      }

      int offset = MP3FindSyncWord((unsigned char *)mp3->mp3_buff, mp3->bytes_left);
      if (offset < 0) {
        if (mp3->is_file_source && !mp3->eof_reached) {
          mp3->bytes_left = 0;
          continue;
        }
        break;
      }

      mp3->mp3_buff += offset;
      mp3->bytes_left -= offset;
      if (mp3->bytes_left <= 0) continue;

      if (!mp3->mp3_dec || !mp3->pcm_buff || !mp3->pcm32_buff) break;

      int mp3_err = MP3Decode(mp3->mp3_dec, (unsigned char **)&mp3->mp3_buff, (int*)&mp3->bytes_left, mp3->pcm_buff, 0);

      if (mp3_err == ERR_MP3_NONE) {
        MP3GetLastFrameInfo(mp3->mp3_dec, &mp3->mp3_frame_info);
        mp3->frame_cnt++;

        if (!mp3_details_display) {
          mp3_details_display = true;
          uint32_t sr = (uint32_t)mp3->mp3_frame_info.samprate;
          i2s_reconfig_sample_rate(mp3, sr);
          audio_eq_set_sample_rate(&mp3->eq, sr);  /* ← NEW: update EQ for actual sample rate */
          ESP_LOGI(ESP32_MP3DEC_TAG, "Track Info: %lu Hz | %ld kbps | %d Channels", 
                   (unsigned long)sr,
                   (long)mp3->mp3_frame_info.bitrate / 1000, 
                   mp3->mp3_frame_info.nChans);
        }

        if(skip_frame_on_seek_count > 0){
          skip_frame_on_seek_count--;
          continue;
        }

        size_t num_samples = (size_t)mp3->mp3_frame_info.outputSamps;

        /*
         * ─── FIX #1: Headroom-safe int16 → int32 conversion ───
         *
         * เดิม: << 16 (signal ใกล้ INT32_MAX → ไม่มี headroom สำหรับ EQ)
         * ใหม่: << 8  (signal ~25% ของ INT32_MAX → มี headroom 48 dB)
         *
         * จะ shift กลับ << 8 หลัง EQ + Volume ก่อนส่ง I2S
         */
        if (mp3->mp3_frame_info.nChans == 1 && num_samples <= MAX_PCM_SAMPLES) {
          /* Mono: Helix อาจ output mono → แปลงเป็น stereo */
          for (int i = (int)num_samples - 1; i >= 0; i--) {
            int32_t val = ((int32_t)mp3->pcm_buff[i]) << 8;
            mp3->pcm32_buff[i * 2]     = val;
            mp3->pcm32_buff[i * 2 + 1] = val;
          }
          num_samples *= 2;
        } else {
          /* Stereo: (หรือ Helix ที่ output stereo อยู่แล้ว) */
          for (size_t i = 0; i < num_samples; i++) {
            mp3->pcm32_buff[i] = ((int32_t)mp3->pcm_buff[i]) << 8;
          }
        }

        /* ─── EQ Processing ─── */
        if (mp3->eq.enabled) {
          audio_eq_process_stereo(&mp3->eq, mp3->pcm32_buff, num_samples);
        }

        /* ─── Volume ─── */
        uint8_t vol = mp3->volume;
        if (vol == 0) {
          memset(mp3->pcm32_buff, 0, num_samples * sizeof(int32_t));
        } else if (vol < 100) {
          for (size_t i = 0; i < num_samples; i++) {
            mp3->pcm32_buff[i] = (int32_t)(((int64_t)mp3->pcm32_buff[i] * vol) / 100);
          }
        }

        /*
         * ─── FIX #2: Scale up + Clamp for I2S (32-bit MSB-aligned) ───
         *
         * Shift left 8 เพื่อย้ายข้อมูลไปอยู่ที่ MSB สำหรับ DAC
         * Clamp เพื่อป้องกัน overflow (เฉพาะ peak ที่หายาก)
         */
        for (size_t i = 0; i < num_samples; i++) {
          int64_t val = (int64_t)mp3->pcm32_buff[i] << 8;
          if (val > INT32_MAX) val = INT32_MAX;
          if (val < INT32_MIN) val = INT32_MIN;
          mp3->pcm32_buff[i] = (int32_t)val;
        }

        /* ─── I2S Output ─── */
        size_t bytes_written = 0;
        size_t pcm32_bytes = num_samples * sizeof(int32_t);

        esp_err_t ret = i2s_channel_write(mp3->i2s_hnd, mp3->pcm32_buff, pcm32_bytes, &bytes_written, portMAX_DELAY);
        if (ret != ESP_OK) {
          ESP_LOGE(ESP32_MP3DEC_TAG, "I2S Write Error: %s", esp_err_to_name(ret));
        }
      } else if (mp3_err == ERR_MP3_INDATA_UNDERFLOW) {
        if (mp3->is_file_source && !mp3->eof_reached) continue;
        break;
      } else {
        if (mp3->bytes_left > 0) {
          mp3->mp3_buff += 1;
          mp3->bytes_left -= 1;
        } else {
          break;
        }
      }
    }

    ESP_LOGI(ESP32_MP3DEC_TAG, "Playback completed! Total frames: %lu", (unsigned long)mp3->frame_cnt);

    if (mp3->is_file_open) {
      f_close(&mp3->file);
      mp3->is_file_open = false;
    }
    if (mp3->file_in_buf) {
      free(mp3->file_in_buf);
      mp3->file_in_buf = NULL;
    }
    if (mp3->mp3_dec) {
      MP3FreeDecoder(mp3->mp3_dec);
      mp3->mp3_dec = NULL;
    }
    if (mp3->pcm_buff) {
      free(mp3->pcm_buff);
      mp3->pcm_buff = NULL;
    }
    if (mp3->pcm32_buff) {
      free(mp3->pcm32_buff);
      mp3->pcm32_buff = NULL;
    }
    
    mp3->start_dec = false;
  }
}

void esp32_mp3dec_init(esp32_mp3dec_t *mp3){
  memset(mp3, 0, sizeof(esp32_mp3dec_t));
  mp3->volume = 100;
  audio_eq_init(&mp3->eq);

  esp32_mp3dec_hw_init(mp3, I2S_GPIO_UNUSED, DEFAULT_I2S_BCLK_IO, DEFAULT_I2S_WS_IO, DEFAULT_I2S_DOUT_IO, I2S_GPIO_UNUSED);
  if(!mp3->mp3_task) {
    xTaskCreate(_esp32_mp3dec_task, "mp3_dec", 2048, (void*)mp3, 5, NULL);
    mp3->mp3_task = pdPASS;
  }
}

void esp32_mp3dec_hw_init(esp32_mp3dec_t *mp3, gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din){
  if(mp3->hw_is_init) return;
  _hw_i2s_init(mp3, mclk, bclk, ws, dout, din);
  mp3->hw_is_init = true;
}

void esp32_mp3dec_set_data(esp32_mp3dec_t *mp3, const uint8_t *data, size_t data_size){
  if(!mp3 || !data) return;
  mp3->is_file_source  = false;
  mp3->orig_mp3_buff   = data;
  mp3->mp3_buff        = data;
  mp3->mp3_size        = data_size;
  mp3->bytes_left      = data_size;
  mp3->seek_target_sec = -1;
  mp3->is_paused       = false;
}

void esp32_mp3dec_set_file(esp32_mp3dec_t *mp3, const char *filepath){
  if(!mp3 || !filepath) return;

  if (mp3->is_file_open) {
    f_close(&mp3->file);
    mp3->is_file_open = false;
  }

  FRESULT fr = f_open(&mp3->file, filepath, FA_READ);
  if (fr != FR_OK) {
    ESP_LOGE(ESP32_MP3DEC_TAG, "Cannot open file: %s (FatFs Err: %d)", filepath, fr);
    return;
  }
  mp3->is_file_open = true;
  mp3->mp3_size = (size_t)f_size(&mp3->file);

  uint8_t id3_head[10];
  UINT br = 0;
  uint32_t offset = 0;
  fr = f_read(&mp3->file, id3_head, 10, &br);
  if (fr == FR_OK && br == 10) {
    if (id3_head[0] == 'I' && id3_head[1] == 'D' && id3_head[2] == '3') {
      uint32_t tag_size = ((uint32_t)id3_head[6] << 21) |
                          ((uint32_t)id3_head[7] << 14) |
                          ((uint32_t)id3_head[8] << 7)  |
                          ((uint32_t)id3_head[9]);
      offset = tag_size + 10;
    }
  }
  f_lseek(&mp3->file, offset);

  mp3->file_audio_offset = offset;
  mp3->is_file_source    = true;
  mp3->eof_reached       = false;
  mp3->bytes_left        = 0;
  mp3->seek_target_sec   = -1;
  mp3->is_paused         = false;
}

void esp32_mp3dec_start(esp32_mp3dec_t *mp3){
  if(!mp3) return;

  if (mp3->is_file_source) {
    if (!mp3->is_file_open) return;
    if (!mp3->file_in_buf) {
      mp3->file_in_buf = (uint8_t *)malloc(IN_BUF_SIZE);
    }
    mp3->mp3_buff = mp3->file_in_buf;
  }

  if (!mp3->pcm_buff) {
    mp3->pcm_buff = (int16_t *)malloc(MAX_PCM_SAMPLES * sizeof(int16_t));
  }
  if (!mp3->pcm32_buff) {
    mp3->pcm32_buff = (int32_t *)malloc(MAX_PCM_SAMPLES * sizeof(int32_t));
  }
  if (!mp3->mp3_dec) {
    mp3->mp3_dec = MP3InitDecoder();
  }

  if ((mp3->is_file_source && !mp3->file_in_buf) || !mp3->pcm_buff || !mp3->pcm32_buff || !mp3->mp3_dec) {
    ESP_LOGE(ESP32_MP3DEC_TAG, "Memory allocation failed");
    return;
  }

  mp3->frame_cnt       = 0;
  mp3->is_paused       = false;
  mp3->seek_target_sec = -1;
  mp3->start_dec       = true;
}

void esp32_mp3dec_stop(esp32_mp3dec_t *mp3){
  if (!mp3) return;
  mp3->start_dec = false;
  mp3->is_paused = false;
}

void esp32_mp3dec_pause(esp32_mp3dec_t *mp3){
  if (!mp3) return;
  mp3->is_paused = true;
  ESP_ERROR_CHECK(i2s_channel_disable(mp3->i2s_hnd));
}

void esp32_mp3dec_resume(esp32_mp3dec_t *mp3){
  if (!mp3) return;
  mp3->is_paused = false;
  ESP_ERROR_CHECK(i2s_channel_enable(mp3->i2s_hnd));
}

bool esp32_mp3dec_is_paused(esp32_mp3dec_t *mp3){
  return mp3 ? mp3->is_paused : false;
}

uint32_t esp32_mp3dec_get_current_sec(esp32_mp3dec_t *mp3){
  if (!mp3 || mp3->mp3_frame_info.samprate == 0) return 0;
  return (uint32_t)(((uint64_t)mp3->frame_cnt * 1152) / mp3->mp3_frame_info.samprate);
}

uint32_t esp32_mp3dec_get_total_sec(esp32_mp3dec_t *mp3){
  if (!mp3 || mp3->mp3_frame_info.bitrate == 0) return 0;
  return (uint32_t)(((uint64_t)mp3->mp3_size * 8) / mp3->mp3_frame_info.bitrate);
}

void esp32_mp3dec_seek(esp32_mp3dec_t *mp3, int32_t target_sec){
  if (mp3) {
    uint32_t max_time = esp32_mp3dec_get_total_sec(mp3);
    mp3->seek_target_sec = target_sec < 0 ? 0 : (target_sec > (int32_t)max_time ? (int32_t)max_time : target_sec);
  }
}

void esp32_mp3dec_set_volume(esp32_mp3dec_t *mp3, uint8_t volume){
  if (!mp3) return;
  if (volume > 100) volume = 100;
  mp3->volume = volume;
  ESP_LOGI(ESP32_MP3DEC_TAG, "Volume set to %u%%", volume);
}

uint8_t esp32_mp3dec_get_volume(esp32_mp3dec_t *mp3){
  return mp3 ? mp3->volume : 0;
}

void esp32_mp3dec_set_eq(esp32_mp3dec_t *mp3, int8_t bass_db, int8_t mid_db, int8_t treble_db){
  if (!mp3) return;
  audio_eq_set_params(&mp3->eq, bass_db, mid_db, treble_db);
}

void esp32_mp3dec_set_eq_bass_freq(esp32_mp3dec_t *mp3, uint16_t freq_hz){
  if (!mp3) return;
  audio_eq_set_bass_freq(&mp3->eq, freq_hz);
  ESP_LOGI(ESP32_MP3DEC_TAG, "EQ Bass frequency set to %u Hz", freq_hz);
}
