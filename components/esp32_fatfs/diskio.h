/*-----------------------------------------------------------------------/
/  Low level disk interface modlue include file   (C)ChaN, 2025          /
/-----------------------------------------------------------------------*/

#ifndef _DISKIO_DEFINED
#define _DISKIO_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

#include "ff.h"

/* Status & Result types จาก FatFS */
typedef BYTE    DSTATUS;

typedef enum {
    RES_OK = 0,
    RES_ERROR,
    RES_WRPRT,
    RES_NOTRDY,
    RES_PARERR
} DRESULT;

/* Disk Status Bits (DSTATUS) */

#define STA_NOINIT		0x01	/* Drive not initialized */
#define STA_NODISK		0x02	/* No medium in the drive */
#define STA_PROTECT		0x04	/* Write protected */

/* Command code for disk_ioctrl fucntion */

/* Generic command (Used by FatFs) */
#define CTRL_SYNC			0	/* Complete pending write process (needed at FF_FS_READONLY == 0) */
#define GET_SECTOR_COUNT	1	/* Get media size (needed at FF_USE_MKFS == 1) */
#define GET_SECTOR_SIZE		2	/* Get sector size (needed at FF_MAX_SS != FF_MIN_SS) */
#define GET_BLOCK_SIZE		3	/* Get erase block size (needed at FF_USE_MKFS == 1) */
#define CTRL_TRIM			4	/* Inform device that the data on the block of sectors is no longer used (needed at FF_USE_TRIM == 1) */


/* ฟังก์ชันมาตรฐานที่ FatFS จะมาเรียก */
DSTATUS disk_initialize (BYTE pdrv);
DSTATUS disk_status (BYTE pdrv);
DRESULT disk_read (BYTE pdrv, BYTE* buff, LBA_t sector, UINT count);
DRESULT disk_write (BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count);
DRESULT disk_ioctl (BYTE pdrv, BYTE cmd, void* buff);

/* -------------------------------------------------------------
   โครงสร้างไดรเวอร์แบบยืดหยุ่น (Function Pointer + Context)
   ------------------------------------------------------------- */
typedef struct {
    DSTATUS (*init)(void *ctx);
    DSTATUS (*status)(void *ctx);
    DRESULT (*read)(void *ctx, BYTE *buff, LBA_t sector, UINT count);
    DRESULT (*write)(void *ctx, const BYTE *buff, LBA_t sector, UINT count);
    DRESULT (*ioctl)(void *ctx, BYTE cmd, void *buff);
} diskio_ops_t;

/**
 * @brief ลงทะเบียนไดรฟ์ใหม่เข้าสู่ระบบ FatFS
 * @param ops ตารางฟังก์ชันของไดรฟ์นั้นๆ
 * @param ctx พอยน์เตอร์อ้างอิงฮาร์ดแวร์ (เช่น sdmmc_card_t* หรือ struct usbh_msc*)
 * @param out_pdrv คืนค่าหมายเลข pdrv ที่ระบบจัดสรรให้ (0, 1, 2, ...)
 * @return DRESULT คืน RES_OK หากลงทะเบียนสำเร็จ
 */
DRESULT diskio_register_driver(const diskio_ops_t *ops, void *ctx, BYTE *out_pdrv);

/**
 * @brief ยกเลิกการลงทะเบียนไดรฟ์ (เช่น เมื่อถอด USB ออก)
 * @param pdrv หมายเลขไดรฟ์ที่ต้องการถอด
 */
void diskio_unregister_driver(BYTE pdrv);

#ifdef __cplusplus
}
#endif

#endif