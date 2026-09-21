#ifndef VD_TASK_MGR_H
#define VD_TASK_MGR_H

#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

// ฟังก์ชันลงทะเบียน Task ที่ต้องการให้มอนิเตอร์
void vd_task_mgr_register_task(TaskHandle_t handle);

// ฟังก์ชันสร้างหน้าต่าง Task Manager
lv_obj_t *vd_task_manager_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif

#endif /* VD_TASK_MGR_H */