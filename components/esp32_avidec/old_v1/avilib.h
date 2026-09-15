/*
 *  avilib.h — AVI (MJPEG + MP3) splitter adapted for ESP32 / FatFS / ESP-IDF
 *
 *  Based on the classic "avilib" by Rainer Johanni (1999) and
 *  Thomas Östreich (2001/2002), which is part of transcode and released
 *  under the GNU GPL v2 (or later). See avilib.c for the full history.
 *
 *  Changes for embedded use (ESP-IDF + FatFS):
 *    - POSIX open/read/lseek/close  ->  FatFS  f_open/f_read/f_lseek/f_close
 *    - the write path (AVI_open_output_file, AVI_write_frame, ...) is removed
 *      to save flash and RAM; this header is read-only playback oriented
 *    - off_t  ->  uint32_t offsets (files up to 4 GiB, 4 bytes/offset instead
 *      of 8 on 64-bit hosts)
 *    - the full 'hdrl' list is parsed streaming (no big malloc)
 *    - the raw 'idx1' block is no longer kept in RAM: a compact
 *      video_index/audio_index is built directly (12 bytes per entry)
 *    - added a read-ahead buffer + optional PSRAM placement (see avilib_config_t)
 *    - added a zero-copy read variant: AVI_read_frame_ref()
 *    - codec info headers are stored inline inside avi_t (no heap fragmentation)
 *
 *  The classic read-side API (AVI_open_input_file, AVI_read_frame,
 *  AVI_read_audio, ...) is kept source-compatible so existing code keeps
 *  working after a rebuild.
 */

#ifndef AVILIB_H
#define AVILIB_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ff.h"     /* FatFS */

#ifdef __cplusplus
extern "C" {
#endif

#define AVI_MAX_TRACKS 8

/* Absolute file offset. 64-bit to support OpenDML files > 4 GiB */
typedef uint64_t avi_off_t;

/* ------------------------------------------------------------------ */
/* index entries (same meaning as the original avilib)                */
/* ------------------------------------------------------------------ */
typedef struct {
    avi_off_t key;      /* chunk flags, 0x10 = keyframe */
    avi_off_t pos;      /* absolute offset of the chunk *payload* */
    avi_off_t len;      /* payload length in bytes */
} video_index_entry;

typedef struct {
    avi_off_t pos;      /* absolute offset of the chunk payload */
    avi_off_t len;      /* payload length in bytes */
    avi_off_t tot;      /* total audio bytes before this chunk */
} audio_index_entry;

typedef struct track_s {
    long   a_fmt;             /* audio format, see WAVE_FORMAT_* below */
    long   a_chans;           /* channels, 0 for no audio */
    long   a_rate;            /* sample rate in Hz */
    long   a_bits;            /* bits per sample */
    long   mp3rate;           /* MP3 bitrate, kbit/s */

    long   audio_strn;        /* audio stream number */
    avi_off_t audio_bytes;    /* total bytes of audio data */
    long   audio_chunks;      /* number of audio chunks in the file */

    char   audio_tag[4];      /* tag of audio data, e.g. "01wb" */
    long   audio_posc;        /* audio position: chunk */
    long   audio_posb;        /* audio position: byte within chunk */

    avi_off_t a_codech_off;   /* absolute offset of audio strh */
    avi_off_t a_codecf_off;   /* absolute offset of audio strf */

    audio_index_entry *audio_index;
    long   audio_index_cap;   /* allocated capacity of audio_index */
} track_t;

typedef struct {
    uint32_t bi_size;
    uint32_t bi_width;
    uint32_t bi_height;
    uint16_t bi_planes;
    uint16_t bi_bit_count;
    uint32_t bi_compression;
    uint32_t bi_size_image;
    uint32_t bi_x_pels_per_meter;
    uint32_t bi_y_pels_per_meter;
    uint32_t bi_clr_used;
    uint32_t bi_clr_important;
} BITMAPINFOHEADER_avilib;

typedef struct {
    uint16_t w_format_tag;
    uint16_t n_channels;
    uint32_t n_samples_per_sec;
    uint32_t n_avg_bytes_per_sec;
    uint16_t n_block_align;
    uint16_t w_bits_per_sample;
    uint16_t cb_size;
} WAVEFORMATEX_avilib;

/* ------------------------------------------------------------------ */
/* configuration                                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    size_t buffer_size;   /* read-ahead buffer size; 0 = default (16 KiB) */
    void  *buffer;        /* optional user-provided buffer (>= buffer_size) */
    bool   use_psram;     /* place internal buffers in PSRAM when available */
} avilib_config_t;

/* ------------------------------------------------------------------ */
/* main object                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    /* ---- public read-side info (same field names as classic avilib) ---- */
    long   width;             /* video frame width  */
    long   height;            /* video frame height */
    double fps;               /* frames per second */
    char   compressor[8];     /* compressor from strh, 4 bytes + NUL */
    char   compressor2[8];    /* compression from strf, 4 bytes + NUL */
    long   video_strn;        /* video stream number */
    long   video_frames;      /* number of video frames */
    char   video_tag[4];      /* tag of video data, e.g. "00dc" */
    long   video_pos;         /* index of the next frame to read */
    unsigned long max_len;    /* maximum video chunk payload seen */

    track_t track[AVI_MAX_TRACKS];

    long   n_idx;             /* total chunks indexed (video + audio) */
    long   max_idx;           /* same, kept for API compatibility */

    avi_off_t v_codech_off;   /* absolute offset of video strh */
    avi_off_t v_codecf_off;   /* absolute offset of video strf */

    video_index_entry *video_index;

    int    anum;              /* total number of audio tracks */
    int    aptr;              /* current audio working track */

    /* codec info: pointers into the inline storage below (no heap alloc) */
    BITMAPINFOHEADER_avilib *bitmap_info_header;
    WAVEFORMATEX_avilib     *wave_format_ex[AVI_MAX_TRACKS];

    avi_off_t movi_start;     /* offset of first chunk inside 'movi' */
    avi_off_t movi_end;       /* one past the 'movi' contents */
    avi_off_t idx1_off;       /* offset of idx1 payload (0 = none) */
    uint32_t  idx1_len;       /* length of idx1 payload */

    /* ---- internal state (opaque) ---- */
    FIL     fil;              /* FatFS file object */
    bool    is_open;          /* file is open (and owned) by this handle */
    avi_off_t file_size;
    avi_off_t pos;            /* streaming cursor (absolute offset) */
    uint32_t video_fcc;       /* '00dc' (MJPEG) or '00db' (raw) */

    /* read-ahead window: covers [buf_off, buf_off + buf_len) */
    uint8_t *buf;
    uint32_t buf_size;
    avi_off_t buf_off, buf_len;
    bool    buf_external, buf_psram;

    /* reusable scratch for oversized frames (zero-copy ref API) */
    uint8_t *scratch;
    uint32_t scratch_size;
    bool    scratch_psram;

    avilib_config_t cfg;

    /* inline codec storage (see pointers above) */
    BITMAPINFOHEADER_avilib _bih;
    WAVEFORMATEX_avilib     _wfe[AVI_MAX_TRACKS];
} avi_t;

/* ------------------------------------------------------------------ */
/* error codes (same numeric values as the original avilib)           */
/* ------------------------------------------------------------------ */
#define AVI_ERR_SIZELIM      1
#define AVI_ERR_OPEN         2
#define AVI_ERR_READ         3
#define AVI_ERR_WRITE        4
#define AVI_ERR_WRITE_INDEX  5
#define AVI_ERR_CLOSE        6
#define AVI_ERR_NOT_PERM     7
#define AVI_ERR_NO_MEM       8
#define AVI_ERR_NO_AVI       9
#define AVI_ERR_NO_HDRL     10
#define AVI_ERR_NO_MOVI     11
#define AVI_ERR_NO_VIDS     12
#define AVI_ERR_NO_IDX      13

/* audio formats */
#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_UNKNOWN      (0x0000)
#define WAVE_FORMAT_PCM          (0x0001)
#define WAVE_FORMAT_ADPCM        (0x0002)
#define WAVE_FORMAT_IBM_CVSD     (0x0005)
#define WAVE_FORMAT_ALAW         (0x0006)
#define WAVE_FORMAT_MULAW        (0x0007)
#define WAVE_FORMAT_OKI_ADPCM    (0x0010)
#define WAVE_FORMAT_DVI_ADPCM    (0x0011)
#define WAVE_FORMAT_DIGISTD      (0x0015)
#define WAVE_FORMAT_DIGIFIX      (0x0016)
#define WAVE_FORMAT_YAMAHA_ADPCM (0x0020)
#define WAVE_FORMAT_DSP_TRUESPEECH (0x0022)
#define WAVE_FORMAT_GSM610       (0x0031)
#define WAVE_FORMAT_MPEGLAYER3   (0x0055)
#define IBM_FORMAT_MULAW         (0x0101)
#define IBM_FORMAT_ALAW          (0x0102)
#define IBM_FORMAT_ADPCM         (0x0103)
#endif

extern long AVI_errno;

/* ------------------------------------------------------------------ */
/* opening / closing                                                  */
/* ------------------------------------------------------------------ */

/* Open by FatFS path. getIndex != 0 builds the frame index at open time
 * (needed for AVI_read_frame / seeking). Returns NULL on error (AVI_errno). */
avi_t *AVI_open_input_file(const char *filename, int getIndex);

/* Same, with an explicit configuration (buffer size / PSRAM / external buffer). */
avi_t *AVI_open_input_file_ex(const char *filename, int getIndex, const avilib_config_t *cfg);

/* Wrap an already-open FatFS FIL (you open/close it, e.g. to keep the SD
 * mounted between clips). The handle does NOT close fp on AVI_close(). */
avi_t *AVI_open_fatfs(FIL *fp, int getIndex, const avilib_config_t *cfg);

int AVI_close(avi_t *AVI);

/* (Re)parse the header + optionally build the index. AVI_open_* already
 * call this; useful after AVI_open_fatfs if you need to re-open the index. */
int AVI_parse(avi_t *AVI, int getIndex);

/* Rebuild the frame index by scanning the movi chunk headers. O(1) seeks
 * afterwards. Returns 0 on success, -1 on error. */
int AVI_build_index(avi_t *AVI);

/* ------------------------------------------------------------------ */
/* info / accessors                                                   */
/* ------------------------------------------------------------------ */
long   AVI_video_frames(avi_t *AVI);
int    AVI_video_width(avi_t *AVI);
int    AVI_video_height(avi_t *AVI);
double AVI_frame_rate(avi_t *AVI);
char  *AVI_video_compressor(avi_t *AVI);

int    AVI_audio_tracks(avi_t *AVI);
int    AVI_set_audio_track(avi_t *AVI, int track);
int    AVI_get_audio_track(avi_t *AVI);
int    AVI_audio_channels(avi_t *AVI);
int    AVI_audio_bits(avi_t *AVI);
int    AVI_audio_format(avi_t *AVI);
long   AVI_audio_rate(avi_t *AVI);
long   AVI_audio_mp3rate(avi_t *AVI);
long   AVI_audio_bytes(avi_t *AVI);
long   AVI_audio_chunks(avi_t *AVI);

long   AVI_max_video_chunk(avi_t *AVI);
long   AVI_frame_size(avi_t *AVI, long frame);
long   AVI_audio_size(avi_t *AVI, long frame);
long   AVI_get_video_position(avi_t *AVI, avi_off_t frame);

long   AVI_audio_codech_offset(avi_t *AVI);
long   AVI_audio_codecf_offset(avi_t *AVI);
long   AVI_video_codech_offset(avi_t *AVI);
long   AVI_video_codecf_offset(avi_t *AVI);

/* ------------------------------------------------------------------ */
/* reading                                                            */
/* ------------------------------------------------------------------ */

/* Copy the next video frame into vidbuf (must hold the frame size, see
 * AVI_frame_size / AVI_max_video_chunk). Returns bytes read, -1 on error. */
long AVI_read_frame(avi_t *AVI, char *vidbuf, int *keyframe);

/* Zero-copy variant: returns a pointer to the frame data (valid until the
 * next avilib call on this handle). Does NOT copy into a user buffer. */
long AVI_read_frame_ref(avi_t *AVI, const uint8_t **data, int *keyframe);

/* Read up to `bytes` bytes of audio from the current position. Returns the
 * number of bytes actually read (0 = end of audio), -1 on error. */
long AVI_read_audio(avi_t *AVI, char *audbuf, long bytes);

/* Read one whole audio chunk (one MP3 frame for typical files). audbuf may be
 * NULL to only query the chunk size. Returns bytes read, 0 at end, -1 error. */
long AVI_read_audio_chunk(avi_t *AVI, char *audbuf);

/* Streaming reader that needs NO index. Returns:
 *    1 = video frame in vidbuf, 2 = audio chunk in audbuf, 0 = EOF,
 *   -1 = video buffer too small, -2 = audio buffer too small.
 * `len` receives the payload size. This is the fastest sequential path. */
int AVI_read_data(avi_t *AVI, char *vidbuf, long max_vidbuf,
                              char *audbuf, long max_audbuf, long *len);

/* ------------------------------------------------------------------ */
/* seeking                                                            */
/* ------------------------------------------------------------------ */
int AVI_seek_start(avi_t *AVI);              /* back to the first frame */
int AVI_set_video_position(avi_t *AVI, avi_off_t frame);
int AVI_set_audio_position(avi_t *AVI, avi_off_t byte);
int AVI_set_audio_bitrate(avi_t *AVI, long bitrate); /* kept for compat */

/* ------------------------------------------------------------------ */
/* errors                                                             */
/* ------------------------------------------------------------------ */
void  AVI_print_error(char *str);
char *AVI_strerror(void);
char *AVI_syserror(void);

#ifdef __cplusplus
}
#endif

#endif /* AVILIB_H */
