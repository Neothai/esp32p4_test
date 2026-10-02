/*
 * app_fs_vfs.c — ส่วนที่ใช้ POSIX (opendir/readdir/stat)
 *
 * ⚠️ ทำไมต้องแยกไฟล์?
 *   ff.h ประกาศ  typedef struct { ... } DIR;   (หรือ #define DIR FF_DIR)
 *   <dirent.h> ก็ประกาศ  typedef struct { ... } DIR;  เหมือนกัน
 *   include ทั้งคู่ในไฟล์เดียว = redefinition error แก้ไม่ได้ด้วย #undef
 *   -> จึงต้องแยก translation unit  ไฟล์นี้ "ห้าม" include "ff.h" เด็ดขาด
 *
 * ถ้าโปรเจกต์ไม่มีไดรฟ์แบบ VFS (ใช้แต่ FatFS) ยังต้องคอมไพล์ไฟล์นี้อยู่
 * เพราะ app_fs_bridge.c อ้างถึงสัญลักษณ์เหล่านี้ — แค่จะไม่ถูกเรียกเท่านั้น
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "vendingmc_ui/vd_menu.h"   /* ← ปรับ path ให้ตรงกับโปรเจกต์ */

/* มาจาก app_fs_bridge.c */
vd_file_type_t app_fs_type_by_name(const char *name, bool is_dir);

bool app_fs_vfs_list(const char *real_dir, vd_file_item_t *items,
                     uint32_t cap, uint32_t *out_n)
{
    *out_n = 0;

    DIR *dir = opendir(real_dir);
    if (!dir) return false;

    uint32_t n = 0;
    struct dirent *de;
    while (n < cap && (de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue;

        bool is_dir = (de->d_type == DT_DIR);
        uint32_t kb = 0;

        if (!is_dir) {
            char fp[640];
            snprintf(fp, sizeof(fp), "%s/%s", real_dir, de->d_name);
            struct stat st;
            if (stat(fp, &st) == 0) {
                kb = (uint32_t)((st.st_size + 1023) / 1024);
                is_dir = S_ISDIR(st.st_mode);     /* เผื่อ d_type ไม่รองรับ */
            }
        }

        items[n].name = strdup(de->d_name);
        if (!items[n].name) break;
        items[n].type    = app_fs_type_by_name(de->d_name, is_dir);
        items[n].size_kb = kb;
        n++;
    }
    closedir(dir);

    *out_n = n;
    return true;
}

int app_fs_vfs_delete(const char *real, bool is_dir)
{
    return is_dir ? rmdir(real) : unlink(real);
}

int app_fs_vfs_rename(const char *from, const char *to)
{
    return rename(from, to);
}

int app_fs_vfs_mkdir(const char *real)
{
    return mkdir(real, 0777);
}
