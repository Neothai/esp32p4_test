/*
 *  avilib.c — AVI (MJPEG + MP3) splitter adapted for ESP32 / FatFS / ESP-IDF
 *
 *  Original code (GPL v2 or later):
 *    Copyright (C) 1999 Rainer Johanni <Rainer@Johanni.de>
 *    Copyright (C) 2001 Thomas Östreich
 *    multiple audio track support Copyright (C) 2002 Thomas Östreich
 *    part of transcode — a linux video stream processing tool
 *
 *  ESP32 / FatFS adaptation and performance tuning:
 *    - POSIX I/O replaced with FatFS (f_open/f_read/f_lseek/f_close/f_size)
 *    - read-ahead window buffer: chunk headers are parsed straight from the
 *      buffer instead of one 8-byte f_read() per chunk; frame payloads are
 *      memcpy'd from the buffer (or read directly into the caller's buffer)
 *      with no per-frame f_lseek in the sequential case
 *    - 'hdrl' is parsed in place (the original malloc'd the whole list)
 *    - the raw idx1 block is no longer kept in RAM; a compact
 *      video_index/audio_index (12 bytes/entry, uint32_t offsets) is built
 *    - optional PSRAM placement for buffer + index (heap_caps_malloc)
 *    - zero-copy frame access via AVI_read_frame_ref()
 *    - Support >1GB file with OpenDML checking
 *
 *  Buffering invariant:
 *    buf[] covers absolute offsets [buf_off, buf_off + buf_len) and the FIL
 *    read pointer always sits at buf_off + buf_len. ensure() compacts or
 *    repositions the window; read_at() restores the pointer after a stray
 *    seek so the invariant always holds.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "avilib.h"

#ifndef AVILIB_HOST_TEST
  #include "esp_heap_caps.h"
#endif

/* ------------------------------------------------------------------ */
/* configuration defaults                                             */
/* ------------------------------------------------------------------ */
#ifndef AVILIB_DEFAULT_BUFFER_SIZE
  #ifdef CONFIG_AVILIB_BUFFER_SIZE
    #define AVILIB_DEFAULT_BUFFER_SIZE CONFIG_AVILIB_BUFFER_SIZE
  #else
    #define AVILIB_DEFAULT_BUFFER_SIZE 16384
  #endif
#endif

long AVI_errno = 0;

#define SETERR(x) do { AVI_errno = (x); } while (0)

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */
#define FCC4(a,b,c,d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
                       ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define FCC2(a,b)     ((uint32_t)(a) | ((uint32_t)(b) << 8))

#define TAG_RIFF FCC4('R','I','F','F')
#define TAG_AVI  FCC4('A','V','I',' ')
#define TAG_LIST FCC4('L','I','S','T')
#define TAG_hdrl FCC4('h','d','r','l')
#define TAG_avih FCC4('a','v','i','h')
#define TAG_strl FCC4('s','t','r','l')
#define TAG_strh FCC4('s','t','r','h')
#define TAG_strf FCC4('s','t','r','f')
#define TAG_movi FCC4('m','o','v','i')
#define TAG_idx1 FCC4('i','d','x','1')
#define TAG_vids FCC4('v','i','d','s')
#define TAG_auds FCC4('a','u','d','s')
#define TAG_MJPG FCC4('M','J','P','G')
#define TAG_DIB  FCC4('D','I','B',' ')

#define HI_DC FCC2('d','c')
#define HI_DB FCC2('d','b')
#define HI_WB FCC2('w','b')

static inline uint32_t rd32(const uint8_t *p)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
#else
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
#endif
}

static inline uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t pad2(uint32_t v) { return (v + 1u) & ~1u; }

static inline bool is_video_chunk(avi_t *a, uint32_t id)
{
    uint32_t hi = id >> 16;
    if ((id & 0xFFFFu) != (a->video_fcc & 0xFFFFu))
        return false;
    //return hi == HI_DC || hi == HI_DB;
    return hi == HI_DC || hi == HI_DB || 
           hi == FCC2('D','C') || hi == FCC2('D','B');
}

/* ------------------------------------------------------------------ */
/* allocation (PSRAM-aware on ESP-IDF)                                */
/* ------------------------------------------------------------------ */
static void *avilib_malloc(avi_t *a, size_t size, bool *psram)
{
    void *p = NULL;
    if (psram) *psram = false;
#ifndef AVILIB_HOST_TEST
    if (a && a->cfg.use_psram) {
        p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        if (p) {
            if (psram) *psram = true;
            return p;
        }
    }
#else
    (void)a;
#endif
    p = malloc(size);
    return p;
}

static void avilib_free(void *p) { if (p) free(p); }

/* ------------------------------------------------------------------ */
/* buffered reader                                                    */
/* ------------------------------------------------------------------ */
static bool ensure(avi_t *avi, uint32_t need)
{
    avi_off_t pos = avi->pos;

    if (pos >= avi->buf_off && pos + need <= avi->buf_off + avi->buf_len)
        return true;

    if (pos >= avi->buf_off && pos <= avi->buf_off + avi->buf_len) {
        uint32_t keep = avi->buf_off + avi->buf_len - pos;
        memmove(avi->buf, avi->buf + (pos - avi->buf_off), keep);
        avi->buf_off = pos;
        avi->buf_len = keep;
    } else {
        avi->buf_off = pos;
        avi->buf_len = 0;
        if (f_lseek(&avi->fil, (FSIZE_t)pos) != FR_OK)
            return false;
    }

    while (avi->buf_len < need) {
        UINT want = avi->buf_size - avi->buf_len;
        UINT rd = 0;
        if (f_read(&avi->fil, avi->buf + avi->buf_len, want, &rd) != FR_OK)
            return false;
        if (rd == 0)
            return false;
        avi->buf_len += rd;
    }
    return true;
}

/* Copy len bytes from absolute offset off into dst. In-window memcpy when
 * possible; otherwise seek+read, always restoring the FIL pointer. */
static bool read_at(avi_t *avi, avi_off_t off, void *dst, uint32_t len)
{
    if (len == 0)
        return true;

    if (off >= avi->buf_off && off + len <= avi->buf_off + avi->buf_len) {
        memcpy(dst, avi->buf + (off - avi->buf_off), len);
        return true;
    }

    // อ่านข้อมูลเดินหน้าตรง ๆ โดยไม่ต้องสั่ง f_lseek ย้อนกลับ
    UINT rd = 0;
    if (f_lseek(&avi->fil, (FSIZE_t)off) != FR_OK)
        return false;
    if (f_read(&avi->fil, dst, len, &rd) != FR_OK || rd != len)
        return false;

    // อัปเดตตำแหน่งบัฟเฟอร์ให้เดินหน้าตามจริง ตัด f_lseek(restore) ทิ้งเด็ดขาด
    avi->buf_off = off;
    avi->buf_len = 0;

    return true;
}

/*
static bool read_at(avi_t *avi, avi_off_t off, void *dst, uint32_t len)
{
    if (len == 0)
        return true;

    if (off >= avi->buf_off && off + len <= avi->buf_off + avi->buf_len) {
        memcpy(dst, avi->buf + (off - avi->buf_off), len);
        return true;
    }

    // อ่านข้อมูลตรงๆ โดยไม่ต้อง Seek ถอยหลัง
    UINT rd = 0;
    if (f_lseek(&avi->fil, (FSIZE_t)off) != FR_OK)
        return false;
    if (f_read(&avi->fil, dst, len, &rd) != FR_OK || rd != len)
        return false;

    // อัปเดตตำแหน่งบัฟเฟอร์ให้เดินหน้าตามจริง ตัด f_lseek(restore) ทิ้ง
    avi->buf_off = off;
    avi->buf_len = 0;

    return true;
}*/

/* Read the chunk header (id, size) at the current streaming cursor. */
static bool peek_hdr(avi_t *avi, uint32_t *id, uint32_t *sz)
{
    const uint8_t *c;
    if (!ensure(avi, 8))
        return false;
    c = avi->buf + (avi->pos - avi->buf_off);
    *id = rd32(c);
    *sz = rd32(c + 4);
    return true;
}

/* ------------------------------------------------------------------ */
/* index building (compact, growable arrays)                          */
/* ------------------------------------------------------------------ */
static bool index_video_reserve(avi_t *a, uint32_t n)
{
    uint32_t cap;
    if (n <= a->max_idx)
        return true;
    cap = a->max_idx ? a->max_idx : 256;
    while (cap < n)
        cap *= 2;
    {
        video_index_entry *np = avilib_malloc(a, cap * sizeof(video_index_entry), NULL);
        if (!np)
            return false;
        if (a->video_index && a->video_frames)
            memcpy(np, a->video_index, (size_t)a->video_frames * sizeof(video_index_entry));
        avilib_free(a->video_index);
        a->video_index = np;
        a->max_idx = (long)cap;
    }
    return true;
}

static bool index_audio_reserve(avi_t *a, int t, uint32_t n)
{
    uint32_t cap;
    track_t *tr = &a->track[t];

    if (n <= (uint32_t)tr->audio_index_cap)
        return true;

    cap = tr->audio_index_cap > 0 ? (uint32_t)tr->audio_index_cap : 256;
    while (cap < n)
        cap *= 2;
    {
        audio_index_entry *np = avilib_malloc(a, cap * sizeof(audio_index_entry), NULL);
        if (!np)
            return false;
        if (tr->audio_index && tr->audio_chunks)
            memcpy(np, tr->audio_index,
                   (size_t)tr->audio_chunks * sizeof(audio_index_entry));
        avilib_free(tr->audio_index);
        tr->audio_index = np;
        tr->audio_index_cap = (long)cap;
    }
    return true;
}

static void index_clear(avi_t *a)
{
    int t;
    avilib_free(a->video_index);
    a->video_index = NULL;
    a->video_frames = 0;
    a->max_idx = 0;
    a->n_idx = 0;
    for (t = 0; t < AVI_MAX_TRACKS; t++) {
        avilib_free(a->track[t].audio_index);
        a->track[t].audio_index = NULL;
        a->track[t].audio_index_cap = 0;
        a->track[t].audio_chunks = 0;
        a->track[t].audio_bytes = 0;
    }
}

/* Scan the movi list and build video/audio indexes (single pass). */
static int build_index_scan(avi_t *a)
{
    uint32_t id, sz;
    avi_off_t tot[AVI_MAX_TRACKS];
    int t;

    for (t = 0; t < AVI_MAX_TRACKS; t++)
        tot[t] = 0;

    a->pos = a->movi_start;

    for (;;) {
        avi_off_t payload;
        if (a->pos + 8 > a->movi_end)
            break;
        if (!peek_hdr(a, &id, &sz))
            break;

        if (id == TAG_LIST) { a->pos += 12; continue; }
        if (id == FCC4('r','e','c',' ')) { a->pos += 8; continue; }

        payload = a->pos + 8;
        a->pos += 8 + pad2(sz);

        if (is_video_chunk(a, id)) {
            if (!index_video_reserve(a, (uint32_t)a->video_frames + 1))
                return -1;
            a->video_index[a->video_frames].key = 0x10; /* MJPEG: all keyframes */
            a->video_index[a->video_frames].pos = payload;
            a->video_index[a->video_frames].len = sz;
            a->video_frames++;
            a->n_idx++;
            if (sz > a->max_len)
                a->max_len = sz;
            continue;
        }

        for (t = 0; t < a->anum; t++) {
            uint32_t tag = FCC4((uint8_t)a->track[t].audio_tag[0],
                                (uint8_t)a->track[t].audio_tag[1],
                                (uint8_t)a->track[t].audio_tag[2],
                                (uint8_t)a->track[t].audio_tag[3]);
            if (id == tag) {
                if (!index_audio_reserve(a, t, (uint32_t)a->track[t].audio_chunks + 1))
                    return -1;
                a->track[t].audio_index[a->track[t].audio_chunks].pos = payload;
                a->track[t].audio_index[a->track[t].audio_chunks].len = sz;
                a->track[t].audio_index[a->track[t].audio_chunks].tot = tot[t];
                tot[t] += sz;
                a->track[t].audio_chunks++;
                a->n_idx++;
                break;
            }
        }
    }

    for (t = 0; t < a->anum; t++)
        a->track[t].audio_bytes = tot[t];

    a->pos = a->movi_start;
    a->video_pos = 0;
    for (t = 0; t < a->anum; t++) {
        a->track[t].audio_posc = 0;
        a->track[t].audio_posb = 0;
    }
    return 0;
}

/* Try to build the index from an idx1 chunk. Returns 1 on success, 0 if the
 * idx1 offsets do not validate (caller falls back to a scan), -1 on error. */
static int build_index_idx1(avi_t *a, avi_off_t idx1_off, uint32_t idx1_len)
{
    uint8_t e[16];
    avi_off_t base = 0;
    uint32_t i, n = idx1_len / 16;
    avi_off_t tot[AVI_MAX_TRACKS];
    int t, found = 0;

    for (t = 0; t < AVI_MAX_TRACKS; t++)
        tot[t] = 0;

    /*=========================================================================================================================*/
    /* ตรวจสอบหา Base Offset */
    for (i = 0; i < n; i++) {
        if (!read_at(a, idx1_off + i * 16, e, 16))
            return 0;
        {
            uint32_t fcc = rd32(e);
            if (is_video_chunk(a, fcc)) { // [แก้ไข] ใช้ is_video_chunk แทนการเช็คแค่ HI_DC
                uint32_t off = rd32(e + 8);
                uint32_t len = rd32(e + 12);
                uint8_t h[8];

                /* Candidate A: Offset สัมพันธ์กับต้นไฟล์ */
                if (off + 8 <= a->file_size &&
                    read_at(a, off, h, 8) && rd32(h) == fcc && rd32(h + 4) == len) {
                    base = 8;
                    found = 1;
                    break;
                }
                /* Candidate B: Offset สัมพันธ์กับ 'movi' FourCC (movi_start - 4) */
                if (a->movi_start >= 4 && (a->movi_start - 4 + off + 8) <= a->file_size &&
                    read_at(a, a->movi_start - 4 + off, h, 8) &&
                    rd32(h) == fcc && rd32(h + 4) == len) {
                    base = a->movi_start + 4;
                    found = 1;
                    break;
                }
                /* [เพิ่ม] Candidate C: Offset สัมพันธ์กับ 'LIST' movi (movi_start - 12) */
                if (a->movi_start >= 12 && (a->movi_start - 12 + off + 8) <= a->file_size &&
                    read_at(a, a->movi_start - 12 + off, h, 8) &&
                    rd32(h) == fcc && rd32(h + 4) == len) {
                    base = a->movi_start - 4;
                    found = 1;
                    break;
                }
                /* [เพิ่ม] Candidate D: Offset สัมพันธ์กับ First Payload ใน movi */
                if ((a->movi_start + off + 8) <= a->file_size &&
                    read_at(a, a->movi_start + off, h, 8) &&
                    rd32(h) == fcc && rd32(h + 4) == len) {
                    base = a->movi_start + 8;
                    found = 1;
                    break;
                }
            }
        }
    }
    if (!found)
        return 0; // หากไม่ตรงจะ Fallback ไปใช้ build_index_scan() อัตโนมัติ

    /*=========================================================================================================================*/

    /* find the base offset of idx1 entries: file-relative or movi-relative */
    for (i = 0; i < n; i++) {
        if (!read_at(a, idx1_off + i * 16, e, 16))
            return 0;
        {
            uint32_t fcc = rd32(e);
            if ((fcc >> 16) == HI_DC || (fcc >> 16) == HI_DB) {
                uint32_t off = rd32(e + 8);
                uint32_t len = rd32(e + 12);
                uint8_t h[8];

                /* candidate A: offset is relative to the file start */
                if (off + 8 <= a->file_size &&
                    read_at(a, off, h, 8) && rd32(h) == fcc && rd32(h + 4) == len) {
                    base = 8; /* payload = offset + 8 */
                    found = 1;
                    break;
                }
                /* candidate B: offset is relative to the movi LIST start */
                if (a->movi_start >= 4 && off + a->movi_start <= a->file_size &&
                    read_at(a, a->movi_start - 4 + off, h, 8) &&
                    rd32(h) == fcc && rd32(h + 4) == len) {
                    base = a->movi_start + 4;
                    found = 1;
                    break;
                }
            }
        }
    }
    if (!found)
        return 0;

    /* build compact indexes from the idx1 entries */
    for (i = 0; i < n; i++) {
        if (!read_at(a, idx1_off + i * 16, e, 16))
            return 0;
        {
            uint32_t fcc = rd32(e);
            uint32_t flags = rd32(e + 4);
            uint32_t off = rd32(e + 8);
            uint32_t len = rd32(e + 12);
            avi_off_t payload = base + off;

            if (payload + len > a->file_size)
                continue;

            if ((fcc >> 16) == HI_DC || (fcc >> 16) == HI_DB) {
                if (!index_video_reserve(a, (uint32_t)a->video_frames + 1))
                    return -1;
                a->video_index[a->video_frames].key = flags;
                a->video_index[a->video_frames].pos = payload;
                a->video_index[a->video_frames].len = len;
                a->video_frames++;
                a->n_idx++;
                if (len > a->max_len)
                    a->max_len = len;
                continue;
            }

            for (t = 0; t < a->anum; t++) {
                uint32_t tag = FCC4((uint8_t)a->track[t].audio_tag[0],
                                    (uint8_t)a->track[t].audio_tag[1],
                                    (uint8_t)a->track[t].audio_tag[2],
                                    (uint8_t)a->track[t].audio_tag[3]);
                if (fcc == tag) {
                    if (!index_audio_reserve(a, t, (uint32_t)a->track[t].audio_chunks + 1))
                        return -1;
                    a->track[t].audio_index[a->track[t].audio_chunks].pos = payload;
                    a->track[t].audio_index[a->track[t].audio_chunks].len = len;
                    a->track[t].audio_index[a->track[t].audio_chunks].tot = tot[t];
                    tot[t] += len;
                    a->track[t].audio_chunks++;
                    a->n_idx++;
                    break;
                }
            }
        }
    }

    for (t = 0; t < a->anum; t++)
        a->track[t].audio_bytes = tot[t];

    return 1;
}

int AVI_build_index(avi_t *AVI)
{
    if (!AVI)
        return -1;

    index_clear(AVI);

    if (AVI->idx1_off && AVI->idx1_len >= 16) {
        int r = build_index_idx1(AVI, AVI->idx1_off, AVI->idx1_len);
        if (r == 1)
            return 0;
        /* fall through to a scan */
        index_clear(AVI);
    }

    if (build_index_scan(AVI) != 0)
        return -1;

    return (AVI->video_frames > 0) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* header parsing                                                     */
/* ------------------------------------------------------------------ */
static int parse_strh(avi_t *a, avi_off_t off, int strno, int *last_kind, int *last_track)
{
    uint8_t s[56];
    uint32_t fcc, handler, scale, rate, length, suggested_buf;

    if (!read_at(a, off, s, 56))
        return -1;

    fcc           = rd32(s + 0);
    handler       = rd32(s + 4);
    scale         = rd32(s + 20);
    rate          = rd32(s + 24);
    length        = rd32(s + 32);
    suggested_buf = rd32(s + 36);

    *last_kind = 0;

    if (fcc == TAG_vids && a->video_strn < 0) {
        a->video_strn = (long)strno;
        memcpy(a->compressor, s + 4, 4);
        a->compressor[4] = 0;
        if (scale)
            a->fps = (double)rate / (double)scale;
        a->video_frames = (long)length;
        a->v_codech_off = off;
        a->video_fcc = (handler == TAG_DIB) ? HI_DB : HI_DC;
        a->video_suggested_buf = suggested_buf;
        *last_kind = 1;
        *last_track = 0;
    }/* else if (fcc == TAG_auds && a->anum < AVI_MAX_TRACKS) {
        int t = a->anum;
        memset(&a->track[t], 0, sizeof(a->track[t]));
        a->track[t].audio_strn = (long)strno;
        a->track[t].a_codech_off = off;
        a->track[t].audio_tag[0] = (char)('0' + (t + 1) / 10);
        a->track[t].audio_tag[1] = (char)('0' + (t + 1) % 10);
        a->track[t].audio_tag[2] = 'w';
        a->track[t].audio_tag[3] = 'b';
        a->anum++;
        *last_kind = 2;
        *last_track = t;
    }*/
    else if (fcc == TAG_auds && a->anum < AVI_MAX_TRACKS) {
        int t = a->anum;
        memset(&a->track[t], 0, sizeof(a->track[t]));
        a->track[t].audio_strn = (long)strno;
        a->track[t].a_codech_off = off;
        
        // [แก้ไข] ใช้ strno แทน (t + 1)
        a->track[t].audio_tag[0] = (char)('0' + strno / 10);
        a->track[t].audio_tag[1] = (char)('0' + strno % 10);
        a->track[t].audio_tag[2] = 'w';
        a->track[t].audio_tag[3] = 'b';

        a->track[t].audio_suggested_buf = suggested_buf;
        
        a->anum++;
        *last_kind = 2;
        *last_track = t;
    }
    return 0;
}

static int parse_strf(avi_t *a, avi_off_t off, uint32_t sz, int last_kind, int last_track)
{
    if (last_kind == 1) {
        uint8_t b[40];
        uint32_t n = sz < sizeof(b) ? sz : sizeof(b);
        if (n < 40)
            return 0;
        if (!read_at(a, off, b, 40))
            return -1;
        a->_bih.bi_size = rd32(b + 0);
        a->_bih.bi_width = rd32(b + 4);
        a->_bih.bi_height = rd32(b + 8);
        a->_bih.bi_planes = rd16(b + 12);
        a->_bih.bi_bit_count = rd16(b + 14);
        a->_bih.bi_compression = rd32(b + 16);
        a->_bih.bi_size_image = rd32(b + 20);
        a->bitmap_info_header = &a->_bih;
        a->width = (long)rd32(b + 4);
        a->height = (long)rd32(b + 8);
        memcpy(a->compressor2, b + 16, 4);
        a->compressor2[4] = 0;
        a->v_codecf_off = off;

        memcpy(a->compressor2, b + 16, 4);
        a->compressor2[4] = 0;
        
        // [เพิ่ม] หาก compressor หลักว่าง ให้ใช้จาก compressor2
        if (a->compressor[0] == 0 || a->compressor[0] == ' ') {
            memcpy(a->compressor, a->compressor2, 5);
        }
        
        a->v_codecf_off = off;
    } else if (last_kind == 2 && last_track >= 0 && last_track < AVI_MAX_TRACKS) {
        uint8_t w[16];
        uint32_t n = sz < sizeof(w) ? sz : sizeof(w);
        track_t *t = &a->track[last_track];
        if (n < 16)
            return 0;
        if (!read_at(a, off, w, 16))
            return -1;
        t->a_fmt = rd16(w + 0);
        t->a_chans = rd16(w + 2);
        t->a_rate = rd32(w + 4);
        t->mp3rate = (long)(8 * (uint64_t)rd32(w + 8) / 1000);
        t->a_bits = rd16(w + 14);
        a->wave_format_ex[last_track] = &a->_wfe[last_track];
        a->_wfe[last_track].w_format_tag = rd16(w + 0);
        a->_wfe[last_track].n_channels = rd16(w + 2);
        a->_wfe[last_track].n_samples_per_sec = rd32(w + 4);
        a->_wfe[last_track].n_avg_bytes_per_sec = rd32(w + 8);
        a->_wfe[last_track].n_block_align = rd16(w + 12);
        a->_wfe[last_track].w_bits_per_sample = rd16(w + 14);
        t->a_codecf_off = off;
    }
    return 0;
}

static int parse_strl(avi_t *a, avi_off_t start, avi_off_t end, int strno)
{
    avi_off_t p = start;
    int last_kind = 0, last_track = -1;

    // [แก้ไข] ลบ int strno = 0; ออก
    // int strno = 0;

    while (p + 8 <= end) {
        uint8_t h[8];
        uint32_t id, sz;
        if (!read_at(a, p, h, 8))
            break;
        id = rd32(h);
        sz = rd32(h + 4);

        if (id == TAG_strh && sz >= 56) {
          //if (parse_strh(a, p + 8, strno++, &last_kind, &last_track) != 0)
            if (parse_strh(a, p + 8, strno, &last_kind, &last_track) != 0)
                break;
        } else if (id == TAG_strf) {
            if (parse_strf(a, p + 8, sz, last_kind, last_track) != 0)
                break;
            last_kind = 0;
        }
        p += 8 + pad2(sz);
    }
    return 0;
}

static int parse_hdrl(avi_t *a, avi_off_t start, avi_off_t end)
{
    avi_off_t p = start;
    int strno = 0; // [แก้ไข] เริ่มนับหมายเลขสตรีมตรงนี้

    while (p + 8 <= end) {
        uint8_t h[8];
        uint32_t id, sz;
        if (!read_at(a, p, h, 8))
            break;
        id = rd32(h);
        sz = rd32(h + 4);

        if (id == TAG_LIST) {
            uint8_t t[4];
            if (read_at(a, p + 8, t, 4)) {
                uint32_t ltype = rd32(t);
                //if (ltype == TAG_strl)
                //    parse_strl(a, p + 12, p + 8 + sz);
                if (ltype == TAG_strl) {
                    parse_strl(a, p + 12, p + 8 + sz, strno);
                    strno++; // [แก้ไข] เพิ่มหมายเลขสตรีมเมื่อจบแต่ละ strl
                }
            }
            p += 8 + pad2(sz);
            continue;
        }

        if (id == TAG_avih && sz >= 40) {
            uint8_t b[40];
            if (read_at(a, p + 8, b, 40)) {
                a->width = (long)rd32(b + 32);
                a->height = (long)rd32(b + 36);
                /* dwTotalFrames at +16, streams at +24 */
            }
        }
        p += 8 + pad2(sz);
    }
    return 0;
}

int AVI_parse(avi_t *AVI, int getIndex)
{
    uint8_t head[12];
    avi_off_t pos;
    int t;

    if (!AVI)
        return -1;

    /* reset parsed state */
    AVI->width = AVI->height = 0;
    AVI->fps = 0.0;
    AVI->compressor[0] = AVI->compressor[4] = 0;
    AVI->compressor2[0] = AVI->compressor2[4] = 0;
    AVI->video_strn = -1;
    AVI->video_frames = 0;
    AVI->video_pos = 0;
    AVI->max_len = 0;
    AVI->v_codech_off = AVI->v_codecf_off = 0;
    AVI->movi_start = AVI->movi_end = 0;
    AVI->idx1_off = 0;
    AVI->idx1_len = 0;
    AVI->anum = 0;
    AVI->aptr = 0;
    for (t = 0; t < AVI_MAX_TRACKS; t++) {
        memset(&AVI->track[t], 0, sizeof(AVI->track[t]));
        AVI->wave_format_ex[t] = NULL;
    }
    AVI->bitmap_info_header = NULL;
    index_clear(AVI);

    /* RIFF / AVI  */
    if (!read_at(AVI, 0, head, 12)) { SETERR(AVI_ERR_READ); return -1; }
    if (rd32(head) != TAG_RIFF || rd32(head + 8) != TAG_AVI) { SETERR(AVI_ERR_NO_AVI); return -1; }

    /* walk top-level chunks: hdrl -> parse, movi -> record bounds,
     * idx1 -> remember for index building */
    pos = 12;
    while (pos + 8 <= AVI->file_size) {
        uint8_t h[8];
        uint32_t id, sz;
        if (!read_at(AVI, pos, h, 8))
            break;
        id = rd32(h);
        sz = rd32(h + 4);

        if (id == TAG_LIST) {
            uint8_t t[4];
            uint32_t ltype = 0;
            if (read_at(AVI, pos + 8, t, 4))
                ltype = rd32(t);

            if (ltype == TAG_hdrl)
                parse_hdrl(AVI, pos + 12, pos + 8 + sz);
            else if (ltype == TAG_movi) {
                AVI->movi_start = pos + 12;
                AVI->movi_end = pos + 8 + sz;
                if (AVI->movi_end > AVI->file_size)
                    AVI->movi_end = AVI->file_size;
            }
            pos += 8 + pad2(sz);
            continue;
        }

        if (id == TAG_idx1) {
            AVI->idx1_off = pos + 8;
            AVI->idx1_len = sz;
        }
        pos += 8 + pad2(sz);
    }

    if (!AVI->movi_start) { SETERR(AVI_ERR_NO_MOVI); return -1; }
    if (AVI->video_strn < 0 || AVI->width <= 0) { SETERR(AVI_ERR_NO_VIDS); return -1; }

    /* video tag "NNdc"/"NNdb" and fcc */
    {
        uint32_t lo = (uint32_t)AVI->video_strn;
        AVI->video_tag[0] = (char)('0' + lo / 10);
        AVI->video_tag[1] = (char)('0' + lo % 10);
        AVI->video_tag[2] = 'd';
        AVI->video_tag[3] = (AVI->video_fcc == HI_DB) ? 'b' : 'c';
        AVI->video_fcc = FCC4((uint8_t)AVI->video_tag[0], (uint8_t)AVI->video_tag[1],
                              (uint8_t)AVI->video_tag[2], (uint8_t)AVI->video_tag[3]);
    }

    AVI->pos = AVI->movi_start;
    AVI->video_pos = 0;

    if (getIndex) {
        if (AVI_build_index(AVI) != 0)
            { SETERR(AVI_ERR_NO_VIDS); return -1; }
    }

    SETERR(0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* open / close                                                       */
/* ------------------------------------------------------------------ */
static avi_t *avi_alloc(const avilib_config_t *cfg)
{
    avi_t *a = (avi_t *)malloc(sizeof(avi_t));
    if (!a) {
        SETERR(AVI_ERR_NO_MEM);
        return NULL;
    }
    memset(a, 0, sizeof(*a));
    a->video_strn = -1;
    a->aptr = 0;
    a->cfg.buffer_size = AVILIB_DEFAULT_BUFFER_SIZE;
    if (cfg)
        a->cfg = *cfg;
    if (a->cfg.buffer_size < 2048)
        a->cfg.buffer_size = 2048;
    return a;
}

static int avi_init_buffer(avi_t *a)
{
    if (a->cfg.buffer) {
        a->buf = a->cfg.buffer;
        a->buf_external = true;
    } else {
        a->buf = avilib_malloc(a, a->cfg.buffer_size, &a->buf_psram);
        if (!a->buf)
            return -1;
        a->buf_external = false;
    }
    a->buf_size = a->cfg.buffer_size;
    a->buf_off = a->buf_len = 0;
    return 0;
}

avi_t *AVI_open_input_file_ex(const char *filename, int getIndex, const avilib_config_t *cfg)
{
    avi_t *a = avi_alloc(cfg);
    if (!a)
        return NULL;

    if (f_open(&a->fil, filename, FA_READ) != FR_OK) {
        SETERR(AVI_ERR_OPEN);
        free(a);
        return NULL;
    }

    printf("Open AVI File: %s\r\n", filename);

    a->is_open = true;
    a->file_size = (avi_off_t)f_size(&a->fil);

    if (avi_init_buffer(a) != 0) {
        SETERR(AVI_ERR_NO_MEM);
        AVI_close(a);
        return NULL;
    }

    printf("Init AVI buffer: %s\r\n", filename);

    if (AVI_parse(a, getIndex) != 0) {
        AVI_close(a);
        return NULL;
    }

    printf("Parse AVI File: %s\r\n", filename);

    return a;
}

avi_t *AVI_open_input_file(const char *filename, int getIndex)
{
    return AVI_open_input_file_ex(filename, getIndex, NULL);
}

avi_t *AVI_open_fatfs(FIL *fp, int getIndex, const avilib_config_t *cfg)
{
    avi_t *a;
    if (!fp)
        return NULL;

    a = avi_alloc(cfg);
    if (!a)
        return NULL;

    a->fil = *fp;
    a->is_open = false;      /* caller owns the FIL */
    a->file_size = (avi_off_t)f_size(&a->fil);

    if (avi_init_buffer(a) != 0) {
        SETERR(AVI_ERR_NO_MEM);
        AVI_close(a);
        return NULL;
    }

    if (AVI_parse(a, getIndex) != 0) {
        AVI_close(a);
        return NULL;
    }
    return a;
}

int AVI_close(avi_t *AVI)
{
    if (!AVI)
        return 0;

    if (AVI->is_open)
        f_close(&AVI->fil);
    if (AVI->buf && !AVI->buf_external)
        avilib_free(AVI->buf);
    if (AVI->scratch)
        avilib_free(AVI->scratch);
    index_clear(AVI);
    free(AVI);
    return 0;
}

/* ------------------------------------------------------------------ */
/* accessors                                                          */
/* ------------------------------------------------------------------ */
long AVI_video_frames(avi_t *AVI)   { return AVI->video_frames; }
int  AVI_video_width(avi_t *AVI)    { return (int)AVI->width; }
int  AVI_video_height(avi_t *AVI)   { return (int)AVI->height; }
double AVI_frame_rate(avi_t *AVI)   { return AVI->fps; }
char *AVI_video_compressor(avi_t *AVI) { return AVI->compressor2; }

int AVI_audio_tracks(avi_t *AVI)    { return AVI->anum; }
int AVI_get_audio_track(avi_t *AVI) { return AVI->aptr; }

int AVI_set_audio_track(avi_t *AVI, int track)
{
    if (track < 0 || track + 1 > AVI->anum)
        return -1;
    AVI->aptr = track;
    return 0;
}

int AVI_audio_channels(avi_t *AVI)  { return (int)AVI->track[AVI->aptr].a_chans; }
int AVI_audio_bits(avi_t *AVI)      { return (int)AVI->track[AVI->aptr].a_bits; }
int AVI_audio_format(avi_t *AVI)    { return (int)AVI->track[AVI->aptr].a_fmt; }
long AVI_audio_rate(avi_t *AVI)     { return AVI->track[AVI->aptr].a_rate; }
long AVI_audio_mp3rate(avi_t *AVI)  { return AVI->track[AVI->aptr].mp3rate; }
avi_off_t AVI_audio_bytes(avi_t *AVI)    { return (long)AVI->track[AVI->aptr].audio_bytes; }
long AVI_audio_chunks(avi_t *AVI)   { return AVI->track[AVI->aptr].audio_chunks; }

long AVI_max_video_chunk(avi_t *AVI) { return (long)AVI->max_len; }

long AVI_frame_size(avi_t *AVI, long frame)
{
    if (!AVI->video_index) { SETERR(AVI_ERR_NO_IDX); return -1; }
    if (frame < 0 || frame >= AVI->video_frames) return 0;
    return (long)AVI->video_index[frame].len;
}

long AVI_audio_size(avi_t *AVI, long frame)
{
    if (!AVI->track[AVI->aptr].audio_index) { SETERR(AVI_ERR_NO_IDX); return -1; }
    if (frame < 0 || frame >= AVI->track[AVI->aptr].audio_chunks) return 0;
    return (long)AVI->track[AVI->aptr].audio_index[frame].len;
}

avi_off_t AVI_get_video_position(avi_t *AVI, avi_off_t frame)
{
    if (!AVI->video_index) { SETERR(AVI_ERR_NO_IDX); return -1; }
    if (frame >= AVI->video_frames) return 0;
    return (long)AVI->video_index[frame].pos;
}

long AVI_audio_codech_offset(avi_t *AVI) { return (long)AVI->track[AVI->aptr].a_codech_off; }
long AVI_audio_codecf_offset(avi_t *AVI) { return (long)AVI->track[AVI->aptr].a_codecf_off; }
long AVI_video_codech_offset(avi_t *AVI) { return (long)AVI->v_codech_off; }
long AVI_video_codecf_offset(avi_t *AVI) { return (long)AVI->v_codecf_off; }

/* ------------------------------------------------------------------ */
/* reading                                                            */
/* ------------------------------------------------------------------ */

/* grow the reusable scratch buffer to hold at least `need` bytes */
static bool ensure_scratch(avi_t *a, uint32_t need)
{
    uint8_t *np;
    if (need <= a->scratch_size)
        return true;
    np = avilib_malloc(a, need, &a->scratch_psram);
    if (!np)
        return false;
    if (a->scratch)
        avilib_free(a->scratch);
    a->scratch = np;
    a->scratch_size = need;
    return true;
}

long AVI_read_frame(avi_t *AVI, char *vidbuf, int *keyframe)
{
    uint32_t n;
    avi_off_t payload;

    if (!AVI->video_index) {
        if (AVI_build_index(AVI) != 0) { SETERR(AVI_ERR_NO_IDX); return -1; }
    }
    if (AVI->video_pos < 0 || AVI->video_pos >= AVI->video_frames)
        return -1;

    n = AVI->video_index[AVI->video_pos].len;
    payload = AVI->video_index[AVI->video_pos].pos;
    if (keyframe)
        *keyframe = (AVI->video_index[AVI->video_pos].key == 0x10) ? 1 : 0;

    if (!read_at(AVI, payload, vidbuf, n)) {
        SETERR(AVI_ERR_READ);
        return -1;
    }

    AVI->pos = payload + pad2(n);
    AVI->video_pos++;
    return (long)n;
}

long AVI_read_frame_ref(avi_t *AVI, const uint8_t **data, int *keyframe)
{
    uint32_t n;
    avi_off_t payload;

    if (!AVI->video_index) {
        if (AVI_build_index(AVI) != 0) { SETERR(AVI_ERR_NO_IDX); return -1; }
    }
    if (AVI->video_pos < 0 || AVI->video_pos >= AVI->video_frames)
        return -1;

    n = AVI->video_index[AVI->video_pos].len;
    payload = AVI->video_index[AVI->video_pos].pos;
    if (keyframe)
        *keyframe = (AVI->video_index[AVI->video_pos].key == 0x10) ? 1 : 0;

    if (payload >= AVI->buf_off && payload + n <= AVI->buf_off + AVI->buf_len) {
        *data = AVI->buf + (payload - AVI->buf_off);   /* zero-copy */
    } else {
        if (!ensure_scratch(AVI, n)) { SETERR(AVI_ERR_NO_MEM); return -1; }
        if (!read_at(AVI, payload, AVI->scratch, n)) { SETERR(AVI_ERR_READ); return -1; }
        *data = AVI->scratch;
    }

    AVI->pos = payload + pad2(n);
    AVI->video_pos++;
    return (long)n;
}

long AVI_read_audio(avi_t *AVI, char *audbuf, long bytes)
{
    track_t *t;
    long nr = 0;

    if (!AVI->track[AVI->aptr].audio_index) {
        if (AVI_build_index(AVI) != 0) { SETERR(AVI_ERR_NO_IDX); return -1; }
    }
    t = &AVI->track[AVI->aptr];

    while (bytes > 0) {
        uint32_t left, todo;
        avi_off_t pos;

        left = t->audio_index[t->audio_posc].len - (avi_off_t)t->audio_posb;
        if (left == 0) {
            if (t->audio_posc >= t->audio_chunks - 1)
                return nr;
            t->audio_posc++;
            t->audio_posb = 0;
            continue;
        }
        todo = (uint32_t)(bytes < (long)left ? bytes : (long)left);
        pos = t->audio_index[t->audio_posc].pos + (avi_off_t)t->audio_posb;
        if (!read_at(AVI, pos, audbuf + nr, todo)) {
            SETERR(AVI_ERR_READ);
            return -1;
        }
        bytes -= (long)todo;
        nr += (long)todo;
        t->audio_posb += (long)todo;
    }
    return nr;
}

long AVI_read_audio_chunk(avi_t *AVI, char *audbuf)
{
    track_t *t;
    uint32_t left;
    avi_off_t pos;

    if (!AVI->track[AVI->aptr].audio_index) {
        if (AVI_build_index(AVI) != 0) { SETERR(AVI_ERR_NO_IDX); return -1; }
    }
    t = &AVI->track[AVI->aptr];

    if (t->audio_posc >= t->audio_chunks)
        return 0;

    left = t->audio_index[t->audio_posc].len - (avi_off_t)t->audio_posb;
    if (audbuf == NULL)
        return (long)left;
    if (left == 0)
        return 0;

    pos = t->audio_index[t->audio_posc].pos + (avi_off_t)t->audio_posb;
    if (!read_at(AVI, pos, audbuf, left)) {
        SETERR(AVI_ERR_READ);
        return -1;
    }
    t->audio_posc++;
    t->audio_posb = 0;
    return (long)left;
}

/*
int AVI_read_data(avi_t *AVI, char *vidbuf, long max_vidbuf,
                              char *audbuf, long max_audbuf, long *len)
{
    //
    // Return codes:
    //    1 = video data read,  2 = audio data read,  0 = EOF,
    //   -1 = video buffer too small, -2 = audio buffer too small
    //
    for (;;) {
        uint32_t id, sz, payload;

        if (AVI->pos + 8 > AVI->movi_end)
            return 0;
        if (!peek_hdr(AVI, &id, &sz))
            return 0;

        if (id == TAG_LIST) { AVI->pos += 12; continue; }
        if (id == FCC4('r','e','c',' ')) { AVI->pos += 8; continue; }

        payload = AVI->pos + 8;
        AVI->pos += 8 + pad2(sz);

        if (is_video_chunk(AVI, id)) {
            if (len) *len = (long)sz;
            AVI->video_pos++;
            if (sz > (uint32_t)max_vidbuf)
                return -1;
            if (!read_at(AVI, payload, vidbuf, sz))
                return 0;
            return 1;
        }

        if ((id >> 16) == HI_WB && AVI->anum > 0) {
            int t;
            for (t = 0; t < AVI->anum; t++) {
                uint32_t tag = FCC4((uint8_t)AVI->track[t].audio_tag[0],
                                    (uint8_t)AVI->track[t].audio_tag[1],
                                    (uint8_t)AVI->track[t].audio_tag[2],
                                    (uint8_t)AVI->track[t].audio_tag[3]);
                if (id == tag) {
                    if (len) *len = (long)sz;
                    if (sz > (uint32_t)max_audbuf)
                        return -2;
                    if (!read_at(AVI, payload, audbuf, sz))
                        return 0;
                    return 2;
                }
            }
        }
        // unknown chunk: skip 
    }
}*/

int AVI_read_data(avi_t *AVI, char *vidbuf, long max_vidbuf,
                              char *audbuf, long max_audbuf, long *len)
{
    /*
     * Return codes:
     *    1 = video data read,  2 = audio data read,  0 = EOF,
     *   -1 = video buffer too small, -2 = audio buffer too small
     */
    for (;;) {
        uint32_t id, sz;
        avi_off_t payload;

        // ── ตรวจสอบเมื่ออ่านจนสุดก้อน movi ปัจจุบัน (เช่น แตะเพดาน 1 GB) ──
        if (AVI->pos + 8 > AVI->movi_end) {
            bool found_next_movi = false;

            // วนลูปกวาดหาบล็อก OpenDML (RIFF AVIX -> LIST movi) ก้อนถัดไป
            while (AVI->pos + 8 <= AVI->file_size) {
                uint32_t cid, csz;
                if (!peek_hdr(AVI, &cid, &csz)) break;

                if (cid == TAG_RIFF) {
                    uint8_t t[4];
                    // ตรวจสอบ Signature ว่าเป็นก้อนต่อขยาย 'AVIX' หรือไม่
                    if (read_at(AVI, AVI->pos + 8, t, 4) && rd32(t) == FCC4('A','V','I','X')) {
                        AVI->pos += 12; // ข้าม Header 'RIFF' <sz> 'AVIX'
                        continue;
                    }
                } else if (cid == TAG_LIST) {
                    uint8_t t[4];
                    // ตรวจสอบว่าเป็นจุดเริ่มต้นข้อมูลภาพ/เสียง 'movi' ก้อนใหม่หรือไม่
                    if (read_at(AVI, AVI->pos + 8, t, 4) && rd32(t) == TAG_movi) {
                        AVI->movi_start = AVI->pos + 12;
                        AVI->movi_end   = AVI->pos + 8 + csz;
                        if (AVI->movi_end > AVI->file_size) {
                            AVI->movi_end = AVI->file_size;
                        }
                        AVI->pos = AVI->movi_start;
                        found_next_movi = true;
                        break;
                    }
                }

                // หากเป็น Chunk อื่น (เช่น idx1 เก่า หรือ Junk) ให้ข้ามตามขนาดข้อมูล
                AVI->pos += 8 + pad2(csz);
            }

            if (!found_next_movi) {
                return 0; // จบไฟล์วิดีโอทั้งหมดจริง ๆ (EOF)
            }

            // พบก้อน movi ใหม่แล้ว -> วนลูปกลับไปอ่าน Chunk แรกของก้อนถัดไปทันที
            continue;
        }

        if (!peek_hdr(AVI, &id, &sz))
            return 0;

        if (id == TAG_LIST) { AVI->pos += 12; continue; }
        if (id == FCC4('r','e','c',' ')) { AVI->pos += 8; continue; }

        payload = AVI->pos + 8;
        AVI->pos += 8 + pad2(sz);

        if (is_video_chunk(AVI, id)) {
            if (len) *len = (long)sz;
            AVI->video_pos++;
            if (sz > (uint32_t)max_vidbuf)
                return -1;
            if (!read_at(AVI, payload, vidbuf, sz))
                return 0;
            return 1;
        }

        if ((id >> 16) == HI_WB && AVI->anum > 0) {
            int t;
            for (t = 0; t < AVI->anum; t++) {
                uint32_t tag = FCC4((uint8_t)AVI->track[t].audio_tag[0],
                                    (uint8_t)AVI->track[t].audio_tag[1],
                                    (uint8_t)AVI->track[t].audio_tag[2],
                                    (uint8_t)AVI->track[t].audio_tag[3]);
                if (id == tag) {
                    if (len) *len = (long)sz;
                    if (sz > (uint32_t)max_audbuf)
                        return -2;
                    if (!read_at(AVI, payload, audbuf, sz))
                        return 0;
                    return 2;
                }
            }
        }
        // unknown chunk: skip
    }
}


/* ------------------------------------------------------------------ */
/* seeking                                                            */
/* ------------------------------------------------------------------ */
int AVI_seek_start(avi_t *AVI)
{
    int t;
    AVI->pos = AVI->movi_start;
    AVI->video_pos = 0;
    for (t = 0; t < AVI_MAX_TRACKS; t++) {
        AVI->track[t].audio_posc = 0;
        AVI->track[t].audio_posb = 0;
    }
    return 0;
}

int AVI_set_video_position(avi_t *AVI, avi_off_t frame)
{
    if (!AVI->video_index) {
        if (AVI_build_index(AVI) != 0) { SETERR(AVI_ERR_NO_IDX); return -1; }
    }
    if (frame > AVI->video_frames)
        frame = AVI->video_frames;
    AVI->video_pos = frame;
    return 0;
}

int AVI_set_audio_position(avi_t *AVI, avi_off_t byte)
{
    track_t *t = &AVI->track[AVI->aptr];
    long n0, n1, n;

    if (!t->audio_index) {
        if (AVI_build_index(AVI) != 0) { SETERR(AVI_ERR_NO_IDX); return -1; }
    }

    n0 = 0;
    n1 = t->audio_chunks;
    while (n0 < n1 - 1) {
        n = (n0 + n1) / 2;
        if ((long)t->audio_index[n].tot > byte)
            n1 = n;
        else
            n0 = n;
    }
    t->audio_posc = n0;
    t->audio_posb = byte - (long)t->audio_index[n0].tot;
    return 0;
}

int AVI_set_audio_bitrate(avi_t *AVI, long bitrate)
{
    AVI->track[AVI->aptr].mp3rate = bitrate;
    return 0;
}

/* ------------------------------------------------------------------ */
/* errors                                                             */
/* ------------------------------------------------------------------ */
static const char *avi_errors[] = {
    /*  0 */ "avilib - No Error",
    /*  1 */ "avilib - AVI file size limit reached",
    /*  2 */ "avilib - Error opening AVI file",
    /*  3 */ "avilib - Error reading from AVI file",
    /*  4 */ "avilib - Error writing to AVI file",
    /*  5 */ "avilib - Error writing index (file may still be usable)",
    /*  6 */ "avilib - Error closing AVI file",
    /*  7 */ "avilib - Operation (read/write) not permitted",
    /*  8 */ "avilib - Out of memory (malloc failed)",
    /*  9 */ "avilib - Not an AVI file",
    /* 10 */ "avilib - AVI file has no header list (corrupted?)",
    /* 11 */ "avilib - AVI file has no MOVI list (corrupted?)",
    /* 12 */ "avilib - AVI file has no video data",
    /* 13 */ "avilib - operation needs an index",
    /* 14 */ "avilib - Unknown Error"
};
static int num_avi_errors = (int)(sizeof(avi_errors) / sizeof(char *));

static char error_string[256];

char *AVI_strerror(void)
{
    int e = (AVI_errno >= 0 && AVI_errno < num_avi_errors) ? (int)AVI_errno : num_avi_errors - 1;
    return (char *)avi_errors[e];
}

char *AVI_syserror(void)
{
    error_string[0] = 0;
    return error_string;
}

void AVI_print_error(char *str)
{
    fprintf(stderr, "%s: %s\n", str, AVI_strerror());
}

uint32_t AVI_video_suggested_buffer_size(avi_t *AVI) {
    return AVI ? AVI->video_suggested_buf : 0;
}

uint32_t AVI_audio_suggested_buffer_size(avi_t *AVI) {
    if (!AVI || AVI->anum <= 0) return 0;
    return AVI->track[AVI->aptr].audio_suggested_buf;
}