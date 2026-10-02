#include "vd_image_dec.h"
#include <string.h>

#define TAG "VD_IMAGE"

/* Helper อ่านค่า Big-Endian 16/32 bit */
static inline uint16_t read_be16(const uint8_t *b) {
    return (uint16_t)((b[0] << 8) | b[1]);
}

static inline uint32_t read_be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

/* Helper อ่านค่า Little-Endian 16/32 bit */
static inline uint16_t read_le16(const uint8_t *b) {
    return (uint16_t)(b[0] | (b[1] << 8));
}

static inline uint32_t read_le32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

/**
 * @brief ตรวจสอบ Marker ของ JPEG ทีละ Segment เพื่อระบุว่าเป็น Baseline หรือ Progressive
 */
static vd_jpg_mode_t parse_jpeg_markers(const uint8_t *data, size_t len, uint32_t *w, uint32_t *h) {
    if (len < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        return VD_JPG_MODE_NONE;
    }

    size_t i = 2; // ข้าม SOI (0xFFD8)
    while (i + 1 < len) {
        // ค้นหา Marker 0xFF
        if (data[i] != 0xFF) {
            i++;
            continue;
        }

        // ข้าม 0xFF ซ้ำซ้อน (Padding)
        while (i < len && data[i] == 0xFF) {
            i++;
        }
        if (i >= len) break;

        uint8_t marker = data[i++];

        // Marker พิเศษที่ไม่มี Payload Length
        if (marker == 0xD8 || marker == 0xD9 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x00) {
            if (marker == 0xD9) break; // EOI (End of Image)
            continue;
        }

        // หากถึง SOS (0xDA: Start of Scan) แสดงว่าส่วน Header หมดแล้ว
        if (marker == 0xDA) break;

        // ต้องการ 2 ไบต์สำหรับขนาดของ Payload
        if (i + 1 >= len) break;
        uint16_t segment_len = read_be16(&data[i]);
        if (segment_len < 2) break; // ป้องกัน Marker เสียหาย

        /* 
         * ตรวจสอบ SOF (Start of Frame) Markers:
         * 0xC0: SOF0  (Baseline DCT)
         * 0xC1: SOF1  (Extended Sequential DCT)
         * 0xC2: SOF2  (Progressive DCT)
         * 0xC9: SOF9  (Extended Sequential, Arithmetic coding)
         * 0xCA: SOF10 (Progressive, Arithmetic coding)
         */
        if ((marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
            (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF)) {
            
            // โครงสร้าง SOF: [Length 2B] [Precision 1B] [Height 2B] [Width 2B] [Components 1B]
            if (i + 7 <= len) {
                if (h) *h = read_be16(&data[i + 3]);
                if (w) *w = read_be16(&data[i + 5]);
            }

            if (marker == 0xC0) return VD_JPG_MODE_BASELINE;
            if (marker == 0xC2 || marker == 0xCA) return VD_JPG_MODE_PROGRESSIVE;
            if (marker == 0xC1 || marker == 0xC9) return VD_JPG_MODE_EXTENDED_SEQ;
            return VD_JPG_MODE_OTHER;
        }

        // ข้ามไปยัง Segment ถัดไป
        i += segment_len;
    }

    return VD_JPG_MODE_OTHER;
}

vd_img_type_t vd_img_check_type(const uint8_t *data, size_t len, vd_img_info_t *out_info) {
    vd_img_info_t info = {
        .type = VD_IMG_TYPE_UNKNOWN,
        .jpg_mode = VD_JPG_MODE_NONE,
        .width = 0,
        .height = 0,
        .type_name = "UNKNOWN"
    };

    if (!data || len < 4) {
        if (out_info) *out_info = info;
        return VD_IMG_TYPE_UNKNOWN;
    }

    /* 1. ตรวจสอบ PNG: 89 50 4E 47 0D 0A 1A 0A */
    if (len >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
        info.type = VD_IMG_TYPE_PNG;
        info.type_name = "PNG";
        // อ่าน IHDR chunk (Offset 16 = Width, 20 = Height)
        if (len >= 24 && memcmp(&data[12], "IHDR", 4) == 0) {
            info.width  = read_be32(&data[16]);
            info.height = read_be32(&data[20]);
        }
    }
    /* 2. ตรวจสอบ JPEG / JPG: FF D8 FF */
    else if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        info.type = VD_IMG_TYPE_JPG;
        info.jpg_mode = parse_jpeg_markers(data, len, &info.width, &info.height);

        switch (info.jpg_mode) {
            case VD_JPG_MODE_BASELINE:     info.type_name = "JPG (Baseline)"; break;
            case VD_JPG_MODE_PROGRESSIVE:  info.type_name = "JPG (Progressive)"; break;
            case VD_JPG_MODE_EXTENDED_SEQ: info.type_name = "JPG (Extended Seq)"; break;
            default:                       info.type_name = "JPG (Other)"; break;
        }
    }
    /* 3. ตรวจสอบ WEBP: "RIFF" .... "WEBP" */
    else if (len >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(&data[8], "WEBP", 4) == 0) {
        info.type = VD_IMG_TYPE_WEBP;
        info.type_name = "WEBP";
        
        // VP8 Simple Lossy
        if (len >= 30 && memcmp(&data[12], "VP8 ", 4) == 0) {
            info.width  = read_le16(&data[26]) & 0x3FFF;
            info.height = read_le16(&data[28]) & 0x3FFF;
        }
        // VP8L Lossless
        else if (len >= 25 && memcmp(&data[12], "VP8L", 4) == 0 && data[20] == 0x2F) {
            uint32_t b0 = data[21], b1 = data[22], b2 = data[23], b3 = data[24];
            info.width  = 1 + (((b1 & 0x3F) << 8) | b0);
            info.height = 1 + (((b3 & 0xF) << 10) | (b2 << 2) | ((b1 & 0xC0) >> 6));
        }
        // VP8X Extended
        else if (len >= 30 && memcmp(&data[12], "VP8X", 4) == 0) {
            info.width  = 1 + (data[24] | (data[25] << 8) | (data[26] << 16));
            info.height = 1 + (data[27] | (data[28] << 8) | (data[29] << 16));
        }
    }
    /* 4. ตรวจสอบ BMP: "BM" (0x42, 0x4D) */
    else if (data[0] == 0x42 && data[1] == 0x4D) {
        info.type = VD_IMG_TYPE_BMP;
        info.type_name = "BMP";
        if (len >= 26) {
            info.width  = read_le32(&data[18]);
            int32_t h   = (int32_t)read_le32(&data[22]);
            info.height = (h < 0) ? (uint32_t)(-h) : (uint32_t)h; // BMP height ติดลบได้ (Top-down)
        }
    }
    /* 5. ตรวจสอบ GIF: "GIF87a" หรือ "GIF89a" */
    else if (len >= 10 && (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0)) {
        info.type = VD_IMG_TYPE_GIF;
        info.type_name = "GIF";
        info.width  = read_le16(&data[6]);
        info.height = read_le16(&data[8]);
    }
    /* 6. ตรวจสอบ TIFF: "II*\0" (Little-Endian) หรือ "MM\0*" (Big-Endian) */
    else if (len >= 4 && ((memcmp(data, "II\x2A\x00", 4) == 0) || (memcmp(data, "MM\x00\x2A", 4) == 0))) {
        info.type = VD_IMG_TYPE_TIFF;
        info.type_name = "TIFF";
    }
    /* 7. ตรวจสอบ ICO: 00 00 01 00 */
    else if (len >= 6 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01 && data[3] == 0x00) {
        info.type = VD_IMG_TYPE_ICO;
        info.type_name = "ICO";
        info.width  = data[6] == 0 ? 256 : data[6];
        info.height = data[7] == 0 ? 256 : data[7];
    }
    /* 8. ตรวจสอบ SVG (Text / XML) */
    else {
        // กวาดตรวจช่วง 256 ไบต์แรกเพื่อหาแท็ก <svg
        size_t scan_len = (len > 256) ? 256 : len;
        for (size_t k = 0; k + 4 < scan_len; k++) {
            if (data[k] == '<' && (memcmp(&data[k], "<svg", 4) == 0 || memcmp(&data[k], "<SVG", 4) == 0)) {
                info.type = VD_IMG_TYPE_SVG;
                info.type_name = "SVG";
                break;
            }
        }
    }

    if (out_info) {
        *out_info = info;
    }
    return info.type;
}

esp_err_t vd_img_jpg_dec(const uint8_t *jpg_in, size_t len, uint8_t **out_raw, size_t *out_size, uint32_t *out_w, uint32_t *out_h) {
  if (!jpg_in || !len || !out_raw) return ESP_ERR_INVALID_ARG;

  jpeg_decoder_handle_t jpeg_dec = NULL;
  jpeg_decode_engine_cfg_t eng_cfg = {
      .intr_priority = 0,
      .timeout_ms = 1000,
  };
  
  esp_err_t err = jpeg_new_decoder_engine(&eng_cfg, &jpeg_dec);
  if (err != ESP_OK) return err;

  jpeg_decode_picture_info_t pic_info;
  err = jpeg_decoder_get_info(jpg_in, len, &pic_info);
  if (err != ESP_OK) {
    jpeg_del_decoder_engine(jpeg_dec);
    return err;
  }

  // 1. คำนวณ 16-byte Alignment อัตโนมัติในนี้
  uint32_t aligned_w = (pic_info.width + 15) & ~15;
  uint32_t aligned_h = (pic_info.height + 15) & ~15;
  size_t required_bytes = aligned_w * aligned_h * 2; // RGB565

  // 2. จองแรมที่ถูกวิธีและขนาดถูกต้องตรงนี้เลย
  size_t allocated_size = 0;
  jpeg_decode_memory_alloc_cfg_t alloc_cfg = {
    .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER
  };

  uint8_t *raw_buf = (uint8_t *)jpeg_alloc_decoder_mem(required_bytes, &alloc_cfg, &allocated_size);
  if (!raw_buf) {
    jpeg_del_decoder_engine(jpeg_dec);
    return ESP_ERR_NO_MEM;
  }

  // 3. เริ่มถอดรหัส
  jpeg_decode_cfg_t dec_cfg = {
      .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
      .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
  };

  uint32_t actual_out_bytes = 0;
  err = jpeg_decoder_process(jpeg_dec, &dec_cfg, jpg_in, len, raw_buf, allocated_size, &actual_out_bytes);
  jpeg_del_decoder_engine(jpeg_dec);

  if (err != ESP_OK) {
    free(raw_buf);
    return err;
  }

  // คืนค่าผลลัพธ์ออกไป
  *out_raw = raw_buf;
  if (out_size) *out_size = required_bytes;
  if (out_w)    *out_w = aligned_w;
  if (out_h)    *out_h = aligned_h;

  return ESP_OK;
}

esp_err_t vd_img_jpg_to_lv_dsc(const uint8_t *raw_in, size_t size, vd_img_info_t *info, lv_image_dsc_t *img_dsc) {
  if (!raw_in || !size || !info || !img_dsc) return ESP_ERR_INVALID_ARG;

  uint32_t aligned_w = (info->width + 15) & ~15;
  uint32_t aligned_h = (info->height + 15) & ~15;

  img_dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
  img_dsc->header.cf    = LV_COLOR_FORMAT_RGB565; // เปลี่ยนจาก RAW เป็น RGB565 เพราะถอดรหัสเสร็จแล้ว
  img_dsc->header.w     = aligned_w;              // ใช้ความกว้างที่ตรงกับบัฟเฟอร์ฮาร์ดแวร์
  img_dsc->header.h     = aligned_h;
  img_dsc->header.stride= aligned_w * 2;
  img_dsc->data_size    = size;
  img_dsc->data         = raw_in;

  return ESP_OK;
}