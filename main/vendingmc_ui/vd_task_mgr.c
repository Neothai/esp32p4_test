#include "vd_task_mgr.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MONITOR_TASKS 12

static TaskHandle_t s_registered_tasks[MAX_MONITOR_TASKS];
static size_t s_task_count = 0;

void vd_task_mgr_register_task(TaskHandle_t handle) {
    if (!handle || s_task_count >= MAX_MONITOR_TASKS) return;
    for (size_t i = 0; i < s_task_count; i++) {
        if (s_registered_tasks[i] == handle) return; // ป้องกันลงทะเบียนซ้ำ
    }
    s_registered_tasks[s_task_count++] = handle;
}

typedef struct {
    lv_obj_t   *dialog;
    lv_obj_t   *lbl_mem;
    lv_obj_t   *lbl_cpu;
    lv_obj_t   *bar_cpu;
    lv_obj_t   *table;
    lv_timer_t *update_timer;
} vd_task_mgr_t;

/* แดร็กหน้าต่างผ่านแถบ Header */
static void _dialog_drag_event_cb(lv_event_t *e) {
    lv_obj_t *dia = (lv_obj_t *)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (!dia || !indev) return;

    lv_point_t vect;
    lv_indev_get_vect(indev, &vect);
    lv_obj_set_pos(dia, lv_obj_get_x_aligned(dia) + vect.x, lv_obj_get_y_aligned(dia) + vect.y);
}

static void _task_mgr_delete_cb(lv_event_t *e) {
    vd_task_mgr_t *mgr = (vd_task_mgr_t *)lv_event_get_user_data(e);
    if (!mgr) return;
    if (mgr->update_timer) {
        lv_timer_delete(mgr->update_timer);
        mgr->update_timer = NULL;
    }
    free(mgr);
}

static void _close_btn_cb(lv_event_t *e) {
    lv_obj_t *dia = (lv_obj_t *)lv_event_get_user_data(e);
    if (dia) lv_obj_delete_async(dia);
}

/* 1. แยกฟังก์ชันคำนวณและวาดข้อมูลออกมาต่างหาก */
static void _task_mgr_update(vd_task_mgr_t *mgr) {
    if (!mgr || !mgr->dialog) return;

    /* 1.1 อัปเดตข้อมูล Memory */
    size_t free_heap  = esp_get_free_heap_size();
    size_t free_dram  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    char mem_buf[64];
    snprintf(mem_buf, sizeof(mem_buf),
        "DRAM: %u KB | PSRAM: %.1f MB | Heap: %u KB",
        (unsigned)(free_dram / 1024),
        (float)free_psram / (1024.0f * 1024.0f),
        (unsigned)(free_heap / 1024));
    
    lv_label_set_text(mgr->lbl_mem, mem_buf);

    /* 1.2 คำนวณ % CPU โหลดจาก LVGL Idle Timer */
    uint32_t idle_pct = lv_timer_get_idle();
    int cpu_usage = (int)(100 - idle_pct);
    if (cpu_usage < 0) cpu_usage = 0;
    if (cpu_usage > 100) cpu_usage = 100;

    lv_label_set_text_fmt(mgr->lbl_cpu, "UI Load: %3d%%", cpu_usage);
    lv_bar_set_value(mgr->bar_cpu, cpu_usage, LV_ANIM_ON);

    /* 1.3 อัปเดตตาราง Task จาก Handle ที่ลงทะเบียนไว้ */
    lv_table_set_row_count(mgr->table, s_task_count + 1);

    for (size_t i = 0; i < s_task_count; i++) {
        TaskHandle_t h = s_registered_tasks[i];
        uint32_t row = i + 1;

        if (!h) continue;

        // ชื่อ Task
        lv_table_set_cell_value(mgr->table, row, 0, pcTaskGetName(h));

        // Priority ปัจจุบัน
        char prio_buf[8];
        snprintf(prio_buf, sizeof(prio_buf), "%u", (unsigned)uxTaskPriorityGet(h));
        lv_table_set_cell_value(mgr->table, row, 1, prio_buf);

        // Stack ขั้นต่ำที่เคยเหลือ
        UBaseType_t min_stack = uxTaskGetStackHighWaterMark(h);
        char stack_buf[16];
        snprintf(stack_buf, sizeof(stack_buf), "%u B", (unsigned)min_stack);
        lv_table_set_cell_value(mgr->table, row, 2, stack_buf);

        // สถานะของ Task
        eTaskState state = eTaskGetState(h);
        const char *state_str = "?";
        switch (state) {
            case eRunning:   state_str = "RUN"; break;
            case eReady:     state_str = "RDY"; break;
            case eBlocked:   state_str = "BLK"; break;
            case eSuspended: state_str = "SUS"; break;
            case eDeleted:   state_str = "DEL"; break;
            default: break;
        }
        lv_table_set_cell_value(mgr->table, row, 3, state_str);
    }
}

/* 2. ตัว Callback สำหรับ Timer (ตรวจสอบพอยน์เตอร์ก่อนเสมอ) */
static void _task_mgr_timer_cb(lv_timer_t *t) {
    if (!t) return;
    vd_task_mgr_t *mgr = (vd_task_mgr_t *)lv_timer_get_user_data(t);
    _task_mgr_update(mgr);
}

lv_obj_t *vd_task_manager_create(lv_obj_t *parent) {
    if (!parent) parent = lv_screen_active();

    vd_task_mgr_t *mgr = (vd_task_mgr_t *)calloc(1, sizeof(vd_task_mgr_t));
    if (!mgr) return NULL;

    /* กล่องหน้าต่างหลัก (500x320 พิกเซล) */
    mgr->dialog = lv_obj_create(parent);
    lv_obj_set_size(mgr->dialog, 500, 320);
    lv_obj_center(mgr->dialog);
    lv_obj_set_style_bg_color(mgr->dialog, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_bg_opa(mgr->dialog, LV_OPA_80, 0);
    lv_obj_set_style_border_color(mgr->dialog, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(mgr->dialog, 1, 0);
    lv_obj_set_style_radius(mgr->dialog, 12, 0);
    lv_obj_set_style_pad_all(mgr->dialog, 0, 0);
    lv_obj_set_style_clip_corner(mgr->dialog, true, 0);
    lv_obj_set_scrollable(mgr->dialog, false);

    lv_obj_add_event_cb(mgr->dialog, _task_mgr_delete_cb, LV_EVENT_DELETE, mgr);

    /* Header Bar สำหรับลากย้าย */
    lv_obj_t *header = lv_obj_create(mgr->dialog);
    lv_obj_set_size(header, lv_pct(100), 38);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_hor(header, 12, 0);
    lv_obj_set_style_pad_ver(header, 0, 0);
    lv_obj_set_scrollable(header, false);
    lv_obj_set_clickable(header, true);
    lv_obj_add_event_cb(header, _dialog_drag_event_cb, LV_EVENT_PRESSING, mgr->dialog);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " Task Manager");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    /* ปุ่มปิด (X) */
    lv_obj_t *close_btn = lv_button_create(header);
    lv_obj_set_size(close_btn, 24, 24);
    lv_obj_set_style_radius(close_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xEF4444), 0);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(close_btn, _close_btn_cb, LV_EVENT_CLICKED, mgr->dialog);

    lv_obj_t *close_txt = lv_label_create(close_btn);
    lv_label_set_text(close_txt, LV_SYMBOL_CLOSE);
    lv_obj_center(close_txt);

    /* แถบสรุป Performance */
    lv_obj_t *perf_box = lv_obj_create(mgr->dialog);
    lv_obj_set_size(perf_box, lv_pct(100), 52);
    lv_obj_align(perf_box, LV_ALIGN_TOP_MID, 0, 38);
    lv_obj_set_style_bg_color(perf_box, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_color(perf_box, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(perf_box, 1, 0);
    lv_obj_set_style_pad_hor(perf_box, 12, 0);
    lv_obj_set_style_pad_ver(perf_box, 5, 0);
    lv_obj_set_scrollable(perf_box, false);

    mgr->lbl_cpu = lv_label_create(perf_box);
    lv_label_set_text(mgr->lbl_cpu, "UI Load:   0%");
    lv_obj_set_style_text_color(mgr->lbl_cpu, lv_color_hex(0x38BDF8), 0);
    lv_obj_align(mgr->lbl_cpu, LV_ALIGN_TOP_LEFT, 0, 0);

    mgr->bar_cpu = lv_bar_create(perf_box);
    lv_obj_set_size(mgr->bar_cpu, 110, 7);
    lv_obj_align(mgr->bar_cpu, LV_ALIGN_TOP_LEFT, 95, 5);
    lv_bar_set_range(mgr->bar_cpu, 0, 100);
    lv_obj_set_style_bg_color(mgr->bar_cpu, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_bg_color(mgr->bar_cpu, lv_color_hex(0x38BDF8), LV_PART_INDICATOR);

    mgr->lbl_mem = lv_label_create(perf_box);
    lv_label_set_text(mgr->lbl_mem, "DRAM: -- KB | PSRAM: -- MB | Heap: -- KB");
    lv_obj_set_style_text_color(mgr->lbl_mem, lv_color_hex(0x94A3B8), 0);
    lv_obj_align(mgr->lbl_mem, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* ตารางรายการ Tasks */
    mgr->table = lv_table_create(mgr->dialog);
    lv_obj_set_size(mgr->table, lv_pct(100), 230);
    lv_obj_align(mgr->table, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(mgr->table, 0, 0);
    lv_obj_set_style_radius(mgr->table, 0, 0);
    lv_obj_set_style_bg_color(mgr->table, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_pad_all(mgr->table, 0, 0);

    lv_obj_set_style_text_color(mgr->table, lv_color_black(), LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(mgr->table, 4, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(mgr->table, 6, LV_PART_ITEMS);

    lv_table_set_column_count(mgr->table, 4);
    lv_table_set_column_width(mgr->table, 0, 170); // Task Name
    lv_table_set_column_width(mgr->table, 1, 65);  // Priority
    lv_table_set_column_width(mgr->table, 2, 110); // Min Stack
    lv_table_set_column_width(mgr->table, 3, 85);  // State

    lv_table_set_cell_value(mgr->table, 0, 0, "Task Name");
    lv_table_set_cell_value(mgr->table, 0, 1, "Prio");
    lv_table_set_cell_value(mgr->table, 0, 2, "Min Stack");
    lv_table_set_cell_value(mgr->table, 0, 3, "State");

    _task_mgr_update(mgr);

    // สร้าง Timer อัปเดตต่อเนื่องทุก 1 วินาที
    mgr->update_timer = lv_timer_create(_task_mgr_timer_cb, 1000, mgr);

    return mgr->dialog;
}