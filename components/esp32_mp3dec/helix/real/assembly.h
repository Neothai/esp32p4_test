#ifndef _ASSEMBLY_H
#define _ASSEMBLY_H

#include <stdint.h>

#ifndef Word64
typedef long long Word64;
#endif

// Implement ฟังก์ชันคำนวณคณิตศาสตร์ 32/64-bit สำหรับ RISC-V GCC
static inline int xmp3_MULSHIFT32(int x, int y) {
    return (int)(((Word64)x * (Word64)y) >> 32);
}

static inline int xmp3_FASTABS(int x) {
    return (x >= 0) ? x : -x;
}

static inline int xmp3_CLZ(int x) {
    return (x == 0) ? 32 : __builtin_clz(x);
}

static inline Word64 xmp3_MADD64(Word64 sum, int x, int y) {
    return sum + ((Word64)x * (Word64)y);
}

static inline Word64 xmp3_SAR64(Word64 x, int n) {
    return x >> n;
}

static inline Word64 xmp3_SHL64(Word64 x, int n) {
    return x << n;
}

// ผูก Macro ให้เรียกฟังก์ชันข้างต้น
#define MULSHIFT32(x, y)    xmp3_MULSHIFT32(x, y)
#define FASTABS(x)          xmp3_FASTABS(x)
#define CLZ(x)              xmp3_CLZ(x)
#define MADD64(sum, x, y)   xmp3_MADD64(sum, x, y)
#define SAR64(x, n)         xmp3_SAR64(x, n)
#define SHL64(x, n)         xmp3_SHL64(x, n)

#endif /* _ASSEMBLY_H */