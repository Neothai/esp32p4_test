#ifndef VD_UTILS_H
#define VD_UTILS_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vd_time_t {
  uint16_t ms;
  uint8_t s;
  uint8_t m;
  uint32_t h;
} vd_time_t;

uint8_t *vd_base64_decode(const char *input, size_t *out_len);

void vd_format_price_raw(float value, char *buffer, bool need_fraction);

/**
 * @brief จัดรูปแบบตัวเลขราคา ใส่เครื่องหมายคอมม่าคั่นหลักพัน และตัดทศนิยม .00 ทิ้ง
 * @param value ค่าเงินทศนิยม (float)
 * @param buffer บัฟเฟอร์ปลายทางสำหรับรับสตริง (ควรมีขนาดอย่างน้อย 24-32 ไบต์)
 */
void vd_format_price(float value, char *buffer);

void vd_ms_to_time(uint64_t ms, vd_time_t *time);

#ifdef __cplusplus
}
#endif

#endif /* VD_UTILS_H */