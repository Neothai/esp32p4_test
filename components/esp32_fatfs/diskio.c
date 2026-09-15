/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2025        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Basic definitions of FatFs */
#include "diskio.h"		/* Declarations FatFs MAI */

#include "sdmmc_cmd.h"
#include "esp_log.h"
#include <time.h>

#define TAG "DISKIO_SD"

// ตัวแปร Card Handle จาก ESP-IDF
static sdmmc_card_t *s_card = NULL;

/* Example: Mapping of physical drive number for each drive */
#define DEV_FLASH	0	/* Map FTL to physical drive 0 */
#define DEV_MMC		1	/* Map MMC/SD card to physical drive 1 */
#define DEV_USB		2	/* Map USB MSD to physical drive 2 */

void diskio_register_sd_card(sdmmc_card_t *card) {
    s_card = card;
}

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	if (pdrv != 0 || !s_card) {
        return STA_NOINIT;
    }
	
	return 0;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	if (pdrv != 0 || !s_card) {
        return STA_NOINIT;
    }
	
    return 0;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	if (pdrv != 0 || !s_card || !buff || count == 0) {
        return RES_PARERR;
    }

    // สั่งอ่าน Sector (Block ละ 512 bytes) ผ่าน DMA SPI
    esp_err_t err = sdmmc_read_sectors(s_card, buff, (size_t)sector, (size_t)count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_read_sectors failed (sector: %llu, count: %u, err: %s)", 
                 (unsigned long long)sector, count, esp_err_to_name(err));
        return RES_ERROR;
    }

    return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	if (pdrv != 0 || !s_card || !buff || count == 0) {
        return RES_PARERR;
    }

    // สั่งเขียน Sector ลง SD Card
    esp_err_t err = sdmmc_write_sectors(s_card, buff, (size_t)sector, (size_t)count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_write_sectors failed (sector: %llu, count: %u, err: %s)", 
                 (unsigned long long)sector, count, esp_err_to_name(err));
        return RES_ERROR;
    }

    return RES_OK;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
    BYTE pdrv,
    BYTE cmd,
    void *buff
)
{
    if (pdrv != 0 || !s_card) {
        return RES_NOTRDY;
    }

    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;

        case GET_SECTOR_COUNT:
            if (!buff) return RES_PARERR;
            *(LBA_t *)buff = (LBA_t)s_card->csd.capacity;
            return RES_OK;

        case GET_SECTOR_SIZE:
            if (!buff) return RES_PARERR;
            // ค่ามาตรฐานของการ์ด SD ทั่วไปคือ 512 ไบต์ ป้องกันกรณีอ่านได้ 0
            WORD ss = (WORD)s_card->csd.sector_size;
            *(WORD *)buff = (ss >= 512 && ss <= 4096) ? ss : 512;
            return RES_OK;

        case GET_BLOCK_SIZE:
            if (!buff) return RES_PARERR;
            // กำหนดเป็น 128 เซกเตอร์ (64 KB) ให้ตรงกับขอบเขต Flash Erase Block ทั่วไป
            *(DWORD *)buff = 128; 
            return RES_OK;

        case CTRL_TRIM:
            return RES_OK;

        default:
            return RES_PARERR;
    }
}

DWORD get_fattime(void) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year < 80) {
        // กรณีเวลายังไม่ซิงก์ คืนค่าเริ่มต้น: 2026/01/01 00:00:00
        return ((DWORD)(2026 - 1980) << 25) | (1 << 21) | (1 << 16);
    }

    return ((DWORD)(timeinfo.tm_year - 80) << 25)
         | ((DWORD)(timeinfo.tm_mon + 1) << 21)
         | ((DWORD)timeinfo.tm_mday << 16)
         | ((DWORD)timeinfo.tm_hour << 11)
         | ((DWORD)timeinfo.tm_min << 5)
         | ((DWORD)timeinfo.tm_sec >> 1);
}