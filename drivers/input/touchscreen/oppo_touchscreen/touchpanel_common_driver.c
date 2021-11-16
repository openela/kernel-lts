// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/uaccess.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/task_work.h>
#include <linux/rtc.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <linux/of_gpio.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>

#ifdef CONFIG_OPLUS_SYSTEM_SEC_DEBUG
#include <soc/oplus/system/sec_debug.h>
#endif

#ifdef CONFIG_TOUCHIRQ_UPDATE_QOS
#include <linux/pm_qos.h>
#endif

#ifndef TPD_USE_EINT
#include <linux/hrtimer.h>
#endif

#ifdef CONFIG_FB
#include <linux/fb.h>
#include <linux/notifier.h>
#endif

#ifdef CONFIG_DRM_MSM
#include <linux/msm_drm_notify.h>
#endif

#include "touchpanel_common.h"
#include "touchpanel_healthinfo.h"
#include "util_interface/touch_interfaces.h"

#if GESTURE_RATE_MODE
#include "gesture_recon_rate.h"
#endif
/*******Part0:LOG TAG Declear************************/
#define TPD_PRINT_POINT_NUM 150
#define TPD_DEVICE "touchpanel"
#define TPD_INFO(a, arg...)  pr_err("[TP]"TPD_DEVICE ": " a, ##arg)
#define TPD_DEBUG(a, arg...)\
    do{\
        if (LEVEL_DEBUG == tp_debug)\
            pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
    }while(0)

#define TPD_DETAIL(a, arg...)\
    do{\
        if (LEVEL_BASIC != tp_debug)\
            pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
    }while(0)

#define TPD_SPECIFIC_PRINT(count, a, arg...)\
    do{\
        if (count++ == TPD_PRINT_POINT_NUM || LEVEL_DEBUG == tp_debug) {\
            TPD_INFO(TPD_DEVICE ": " a, ##arg);\
            count = 0;\
        }\
    }while(0)

/*******Part1:Global variables Area********************/
unsigned int tp_debug = 0;
unsigned int tp_register_times = 0;
struct touchpanel_data *g_tp = NULL;
static DECLARE_WAIT_QUEUE_HEAD(waiter);

#ifdef CONFIG_TOUCHIRQ_UPDATE_QOS
static struct pm_qos_request pm_qos_req;
static int pm_qos_value = PM_QOS_DEFAULT_VALUE;
static int pm_qos_state = 0;
#define PM_QOS_TOUCH_WAKEUP_VALUE 400
#endif

/*******Part2:declear Area********************************/
static void speedup_resume(struct work_struct *work);
static void lcd_trigger_load_tp_fw(struct work_struct *work);
static void lcd_tp_refresh_work(struct work_struct *work);

void esd_handle_switch(struct esd_information *esd_info, bool flag);

#ifdef TPD_USE_EINT
static irqreturn_t tp_irq_thread_fn(int irq, void *dev_id);
#endif

#if defined(CONFIG_FB) || defined(CONFIG_DRM_MSM)
static int fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data);
#endif

static void tp_touch_release(struct touchpanel_data *ts);
static void tp_btnkey_release(struct touchpanel_data *ts);
static void tp_fw_update_work(struct work_struct *work);
static int init_debug_info_proc(struct touchpanel_data *ts);
static void tp_work_func(struct touchpanel_data *ts);
__attribute__((weak)) int register_devinfo(char *name, struct manufacture_info *info)
{
    return 1;
}
__attribute__((weak)) int preconfig_power_control(struct touchpanel_data *ts)
{
    return 0;
}
__attribute__((weak)) int reconfig_power_control(struct touchpanel_data *ts)
{
    return 0;
}
__attribute__((weak)) int notify_prevention_handle(struct kernel_grip_info *grip_info, int obj_attention, struct point_info *points)
{
    return obj_attention;
}

#ifdef CONFIG_TOUCHPANEL_ALGORITHM
__attribute__((weak)) void switch_kalman_fun(struct touchpanel_data *ts, bool game_switch)
{
    return;
}

__attribute__((weak)) int touch_algorithm_handle(struct touchpanel_data *ts, int obj_attention, struct point_info *points)
{
    return obj_attention;
}
__attribute__((weak)) void oppo_touch_algorithm_init(struct touchpanel_data *ts)
{
    return;
}

__attribute__((weak)) void set_algorithm_direction(struct touchpanel_data *ts, int dir)
{
    return;
}

__attribute__((weak)) void release_algorithm_points(struct touchpanel_data *ts)
{
    return;
}
#endif




/*******Part3:Function  Area********************************/
/**
 * operate_mode_switch - switch work mode based on current params
 * @ts: touchpanel_data struct using for common driver
 *
 * switch work mode based on current params(gesture_enable, limit_enable, glove_enable)
 * Do not care the result: Return void type
 */
void operate_mode_switch(struct touchpanel_data *ts)
{
    struct kernel_grip_info *grip_info = ts->grip_info;

    if (!ts->ts_ops->mode_switch) {
        TPD_INFO("not support ts_ops->mode_switch callback\n");
        return;
    }

    if (ts->is_suspended) {
        if (ts->black_gesture_support || ts->fingerprint_underscreen_support) {
            if ((ts->gesture_enable & 0x01) == 1 || ts->fp_enable) {
                if (ts->single_tap_support && ts->ts_ops->enable_single_tap) {
                    if (ts->gesture_enable == 3) {
                        ts->ts_ops->enable_single_tap(ts->chip_data, true);
                    } else {
                        ts->ts_ops->enable_single_tap(ts->chip_data, false);
                    }
                }
                ts->ts_ops->mode_switch(ts->chip_data, MODE_GESTURE, true);
                if (ts->mode_switch_type == SEQUENCE)
                    ts->ts_ops->mode_switch(ts->chip_data, MODE_NORMAL, true);
                if (ts->fingerprint_underscreen_support)
                    ts->ts_ops->enable_fingerprint(ts->chip_data, !!ts->fp_enable);
                if (((ts->gesture_enable & 0x01) != 1) && ts->ts_ops->enable_gesture_mask)
                    ts->ts_ops->enable_gesture_mask(ts->chip_data, 0);
            } else {
                ts->ts_ops->mode_switch(ts->chip_data, MODE_GESTURE, false);
                if (ts->mode_switch_type == SEQUENCE)
                    ts->ts_ops->mode_switch(ts->chip_data, MODE_SLEEP, true);
            }
        } else
            ts->ts_ops->mode_switch(ts->chip_data, MODE_SLEEP, true);
    } else {
        if (ts->ear_sense_support) {
            ts->ts_ops->mode_switch(ts->chip_data, MODE_EARSENSE, ts->es_enable == 1);
            ts->ts_ops->mode_switch(ts->chip_data, MODE_PALM_REJECTION, ts->palm_enable);
        }

        if (ts->face_detect_support) {
            if (ts->fd_enable) {
                input_event(ts->ps_input_dev, EV_MSC, MSC_RAW, 0);
                input_sync(ts->ps_input_dev);
            }
            ts->ts_ops->mode_switch(ts->chip_data, MODE_FACE_DETECT, ts->fd_enable == 1);
        }

        if (ts->fingerprint_underscreen_support) {
            ts->ts_ops->enable_fingerprint(ts->chip_data, !!ts->fp_enable);
        }

        if (ts->mode_switch_type == SEQUENCE) {
            if (ts->black_gesture_support)
                ts->ts_ops->mode_switch(ts->chip_data, MODE_GESTURE, false);
        }
        if (ts->edge_limit_support || ts->fw_edge_limit_support)
            ts->ts_ops->mode_switch(ts->chip_data, MODE_EDGE, ts->limit_edge);

        if (ts->glove_mode_support)
            ts->ts_ops->mode_switch(ts->chip_data, MODE_GLOVE, ts->glove_enable);

        if (ts->charger_pump_support)
            ts->ts_ops->mode_switch(ts->chip_data, MODE_CHARGE, ts->is_usb_checked);

        if (ts->wireless_charger_support)
            ts->ts_ops->mode_switch(ts->chip_data, MODE_WIRELESS_CHARGE, ts->is_wireless_checked);

        if (ts->headset_pump_support)
            ts->ts_ops->mode_switch(ts->chip_data, MODE_HEADSET, ts->is_headset_checked);

        if (ts->kernel_grip_support && ts->ts_ops->enable_kernel_grip) {
            ts->ts_ops->enable_kernel_grip(ts->chip_data, grip_info);
        }

        if (ts->smooth_level_support && ts->ts_ops->smooth_lv_set) {
            ts->ts_ops->smooth_lv_set(ts->chip_data, ts->smooth_level);
        }

        if (ts->smooth_level_array_support && ts->ts_ops->smooth_lv_set) {
            if (ts->smooth_level_charging_array_support && ts->charger_pump_support && ts->is_usb_checked) {
                ts->ts_ops->smooth_lv_set(ts->chip_data, ts->smooth_level_charging_array[ts->smooth_level_chosen]);
            } else {
                ts->ts_ops->smooth_lv_set(ts->chip_data, ts->smooth_level_array[ts->smooth_level_chosen]);
            }
        }

        if (ts->sensitive_level_array_support && ts->ts_ops->sensitive_lv_set) {
            ts->ts_ops->sensitive_lv_set(ts->chip_data, ts->sensitive_level_array[ts->sensitive_level_chosen]);
        }

        if (ts->lcd_tp_refresh_support && ts->ts_ops->tp_refresh_switch) {
            ts->ts_ops->tp_refresh_switch(ts->chip_data, ts->lcd_fps);
        }

        if (ts->fw_grip_support && ts->ts_ops->tp_set_grip_level) {
            ts->ts_ops->tp_set_grip_level(ts->chip_data, ts->grip_level);
        }

        ts->ts_ops->mode_switch(ts->chip_data, MODE_NORMAL, true);
    }
}

static void tp_touch_down(struct touchpanel_data *ts, struct point_info points, int touch_report_num, int id)
{
    static int last_width_major;
    static int point_num = 0;

    if (ts->input_dev == NULL)
        return;

    input_report_key(ts->input_dev, BTN_TOUCH, 1);
    input_report_key(ts->input_dev, BTN_TOOL_FINGER, 1);
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    if (ts->boot_mode == RECOVERY_BOOT)
#else
    if (ts->boot_mode == MSM_BOOT_MODE__RECOVERY)
#endif
    {
        input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, points.z);
    } else {
        if (touch_report_num == 1) {
            input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, points.width_major);
            last_width_major = points.width_major;
        } else if (!(touch_report_num & 0x7f) || touch_report_num == 30) {
            //if touch_report_num == 127, every 127 points, change width_major
            //down and keep long time, auto repeat per 5 seconds, for weixing
            //report move event after down event, for weixing voice delay problem, 30 -> 300ms in order to avoid the intercept by shortcut
            if (last_width_major == points.width_major) {
                last_width_major = points.width_major + 1;
            } else {
                last_width_major = points.width_major;
            }
            input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, last_width_major);
        }
        if ((points.x > ts->touch_major_limit.width_range) && (points.x < ts->resolution_info.max_x - ts->touch_major_limit.width_range) && \
            (points.y > ts->touch_major_limit.height_range) && (points.y < ts->resolution_info.max_y - ts->touch_major_limit.height_range)) {
            if (ts->smart_gesture_support) {
                if (points.touch_major > SMART_GESTURE_THRESHOLD) {
                    input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, points.touch_major);
                } else {
                    input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, SMART_GESTURE_LOW_VALUE);
                }
            }

            if (ts->pressure_report_support) {
                input_report_abs(ts->input_dev, ABS_MT_PRESSURE, points.touch_major);   //add for fixing gripview tap no function issue
            }
        }
        if(!CHK_BIT(ts->irq_slot, (1 << id))) {
            TPD_DETAIL("first touch point id %d [%4d %4d %4d]\n", id, points.x, points.y, points.z);
        }
    }

    input_report_abs(ts->input_dev, ABS_MT_POSITION_X, points.x);
    input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, points.y);

    TPD_SPECIFIC_PRINT(point_num, "Touchpanel id %d :Down[%4d %4d %4d]\n", id, points.x, points.y, points.z);

#ifndef TYPE_B_PROTOCOL
    input_mt_sync(ts->input_dev);
#endif
}

//called by other report device
void external_report_touch(int id, bool down_status, int x, int y)
{

    if (g_tp == NULL)
        return;

    mutex_lock(&g_tp->mutex);
    g_tp->external_touch_status = down_status;
    if (down_status) {
        input_mt_slot(g_tp->input_dev, id);
        input_mt_report_slot_state(g_tp->input_dev, MT_TOOL_FINGER, 1);
        input_report_key(g_tp->input_dev, BTN_TOUCH, 1);
        input_report_key(g_tp->input_dev, BTN_TOOL_FINGER, 1);
        input_report_abs(g_tp->input_dev, ABS_MT_POSITION_X, x);
        input_report_abs(g_tp->input_dev, ABS_MT_POSITION_Y, y);
    } else {
        input_mt_slot(g_tp->input_dev, id);
        input_mt_report_slot_state(g_tp->input_dev, MT_TOOL_FINGER, 0);
        if (!g_tp->touch_count) {
            input_report_key(g_tp->input_dev, BTN_TOUCH, 0);
            input_report_key(g_tp->input_dev, BTN_TOOL_FINGER, 0);
        }
    }
    input_sync(g_tp->input_dev);
    mutex_unlock(&g_tp->mutex);
}
EXPORT_SYMBOL(external_report_touch);

static void tp_touch_up(struct touchpanel_data *ts)
{
    if (ts->input_dev == NULL)
        return;

    if (ts->external_touch_support && (true == ts->external_touch_status)) {
        TPD_DETAIL("external touch device is down, skip this touch up.\n");
        return;
    }

    input_report_key(ts->input_dev, BTN_TOUCH, 0);
    input_report_key(ts->input_dev, BTN_TOOL_FINGER, 0);
#ifndef TYPE_B_PROTOCOL
    input_mt_sync(ts->input_dev);
#endif
}

static void tp_exception_handle(struct touchpanel_data *ts)
{
    if (!ts->ts_ops->reset) {
        TPD_INFO("not support ts->ts_ops->reset callback\n");
        return;
    }

    ts->ts_ops->reset(ts->chip_data);    // after reset, all registers set to default
    operate_mode_switch(ts);

    tp_btnkey_release(ts);
    tp_touch_release(ts);
    if (ts->fingerprint_underscreen_support) {
        ts->fp_info.touch_state = 0;
        opticalfp_irq_handler(&ts->fp_info);
    }
}

static void tp_fw_auto_reset_handle(struct touchpanel_data *ts)
{
    TPD_INFO("%s\n", __func__);

    if(ts->ts_ops->write_ps_status) {
        ts->ts_ops->write_ps_status(ts->chip_data, ts->ps_status);

        if (!ts->ps_status) {
            if (ts->ts_ops->exit_esd_mode) {
                ts->ts_ops->exit_esd_mode(ts->chip_data);
            }
        }
    }

    operate_mode_switch(ts);

    tp_btnkey_release(ts);
    tp_touch_release(ts);
}

static void tp_geture_info_transform(struct gesture_info *gesture, struct resolution_info *resolution_info)
{
    gesture->Point_start.x = gesture->Point_start.x * resolution_info->LCD_WIDTH  / (resolution_info->max_x);
    gesture->Point_start.y = gesture->Point_start.y * resolution_info->LCD_HEIGHT / (resolution_info->max_y);
    gesture->Point_end.x   = gesture->Point_end.x   * resolution_info->LCD_WIDTH  / (resolution_info->max_x);
    gesture->Point_end.y   = gesture->Point_end.y   * resolution_info->LCD_HEIGHT / (resolution_info->max_y);
    gesture->Point_1st.x   = gesture->Point_1st.x   * resolution_info->LCD_WIDTH  / (resolution_info->max_x);
    gesture->Point_1st.y   = gesture->Point_1st.y   * resolution_info->LCD_HEIGHT / (resolution_info->max_y);
    gesture->Point_2nd.x   = gesture->Point_2nd.x   * resolution_info->LCD_WIDTH  / (resolution_info->max_x);
    gesture->Point_2nd.y   = gesture->Point_2nd.y   * resolution_info->LCD_HEIGHT / (resolution_info->max_y);
    gesture->Point_3rd.x   = gesture->Point_3rd.x   * resolution_info->LCD_WIDTH  / (resolution_info->max_x);
    gesture->Point_3rd.y   = gesture->Point_3rd.y   * resolution_info->LCD_HEIGHT / (resolution_info->max_y);
    gesture->Point_4th.x   = gesture->Point_4th.x   * resolution_info->LCD_WIDTH  / (resolution_info->max_x);
    gesture->Point_4th.y   = gesture->Point_4th.y   * resolution_info->LCD_HEIGHT / (resolution_info->max_y);
}

static void tp_gesture_handle(struct touchpanel_data *ts)
{
    struct gesture_info gesture_info_temp;

    if (!ts->ts_ops->get_gesture_info) {
        TPD_INFO("not support ts->ts_ops->get_gesture_info callback\n");
        return;
    }

    memset(&gesture_info_temp, 0, sizeof(struct gesture_info));
    ts->ts_ops->get_gesture_info(ts->chip_data, &gesture_info_temp);
    tp_geture_info_transform(&gesture_info_temp, &ts->resolution_info);

    TPD_INFO("detect %s gesture\n", gesture_info_temp.gesture_type == DouTap ? "double tap" :
             gesture_info_temp.gesture_type == UpVee ? "up vee" :
             gesture_info_temp.gesture_type == DownVee ? "down vee" :
             gesture_info_temp.gesture_type == LeftVee ? "(>)" :
             gesture_info_temp.gesture_type == RightVee ? "(<)" :
             gesture_info_temp.gesture_type == Circle ? "circle" :
             gesture_info_temp.gesture_type == DouSwip ? "(||)" :
             gesture_info_temp.gesture_type == Left2RightSwip ? "(-->)" :
             gesture_info_temp.gesture_type == Right2LeftSwip ? "(<--)" :
             gesture_info_temp.gesture_type == Up2DownSwip ? "up to down |" :
             gesture_info_temp.gesture_type == Down2UpSwip ? "down to up |" :
             gesture_info_temp.gesture_type == Mgestrue ? "(M)" :
             gesture_info_temp.gesture_type == Wgestrue ? "(W)" :
             gesture_info_temp.gesture_type == FingerprintDown ? "(fingerprintdown)" :
             gesture_info_temp.gesture_type == FingerprintUp ? "(fingerprintup)" :
             gesture_info_temp.gesture_type == SingleTap ? "single tap" :
             gesture_info_temp.gesture_type == Heart ? "heart" : "unknown");
#if GESTURE_COORD_GET
    if (ts->ts_ops->get_gesture_coord) {
        ts->ts_ops->get_gesture_coord(ts->chip_data, gesture_info_temp.gesture_type);
    }
#endif
    if (ts->health_monitor_v2_support) {
        tp_healthinfo_report(&ts->monitor_data_v2, HEALTH_GESTURE, &gesture_info_temp.gesture_type);
    }
#ifdef CONFIG_OPPO_TP_APK
    if (ts->gesture_debug_sta) {
        input_report_key(ts->input_dev, KEY_POWER, 1);
        input_sync(ts->input_dev);
        input_report_key(ts->input_dev, KEY_POWER, 0);
        input_sync(ts->input_dev);
        return;
    }
#endif // end of CONFIG_OPPO_TP_APK

    if (gesture_info_temp.gesture_type != UnkownGesture && gesture_info_temp.gesture_type != FingerprintDown && gesture_info_temp.gesture_type != FingerprintUp) {
        memcpy(&ts->gesture, &gesture_info_temp, sizeof(struct gesture_info));
#if GESTURE_RATE_MODE
        if(ts->geature_ignore)
            return;
#endif
        input_report_key(ts->input_dev, KEY_F4, 1);
        input_sync(ts->input_dev);
        input_report_key(ts->input_dev, KEY_F4, 0);
        input_sync(ts->input_dev);
    } else if (gesture_info_temp.gesture_type == FingerprintDown) {
        ts->fp_info.touch_state = 1;
        if (ts->screenoff_fingerprint_info_support) {
            ts->fp_info.x = gesture_info_temp.Point_start.x;
            ts->fp_info.y = gesture_info_temp.Point_start.y;
        }
        opticalfp_irq_handler(&ts->fp_info);
        notify_display_fpd(true);
    } else if (gesture_info_temp.gesture_type == FingerprintUp) {
        ts->fp_info.touch_state = 0;
        if (ts->screenoff_fingerprint_info_support) {
            ts->fp_info.x = gesture_info_temp.Point_start.x;
            ts->fp_info.y = gesture_info_temp.Point_start.y;
        }
        opticalfp_irq_handler(&ts->fp_info);
        notify_display_fpd(false);
    }
}

void tp_touch_btnkey_release(void)
{
    struct touchpanel_data *ts = g_tp;

    if (!ts) {
        TPD_INFO("ts is NULL\n");
        return ;
    }

    tp_touch_release(ts);
    tp_btnkey_release(ts);
}

static void tp_touch_release(struct touchpanel_data *ts)
{
#ifdef TYPE_B_PROTOCOL

    int i = 0;

    if (ts->report_flow_unlock_support) {
        mutex_lock(&ts->report_mutex);
    }
    for (i = 0; i < ts->max_num; i++) {
        input_mt_slot(ts->input_dev, i);
        input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 0);
    }
    input_report_key(ts->input_dev, BTN_TOUCH, 0);
    input_report_key(ts->input_dev, BTN_TOOL_FINGER, 0);
    input_sync(ts->input_dev);
    if (ts->report_flow_unlock_support) {
        mutex_unlock(&ts->report_mutex);
    }
#else
    input_report_key(ts->input_dev, BTN_TOUCH, 0);
    input_report_key(ts->input_dev, BTN_TOOL_FINGER, 0);
    input_mt_sync(ts->input_dev);
    input_sync(ts->input_dev);
#endif
    TPD_INFO("release all touch point and key, clear tp touch down flag\n");
    ts->view_area_touched = 0; //realse all touch point,must clear this flag
    ts->touch_count = 0;
    ts->irq_slot = 0;
    if (ts->health_monitor_v2_support && ts->kernel_grip_support) {
        reset_healthinfo_grip_time_record(&ts->monitor_data_v2, ts->grip_info);
    }
#ifdef CONFIG_TOUCHPANEL_ALGORITHM
    release_algorithm_points(ts);
#endif
}

static bool edge_point_process(struct touchpanel_data *ts, struct point_info points)
{
    if (ts->limit_edge) {
        if (points.x > ts->edge_limit.left_x2 && points.x < ts->edge_limit.right_x2) {
            if (ts->edge_limit.in_which_area == AREA_EDGE)
                tp_touch_release(ts);
            ts->edge_limit.in_which_area = AREA_NORMAL;
        } else if ((points.x > ts->edge_limit.left_x1 && points.x < ts->edge_limit.left_x2) || (points.x > ts->edge_limit.right_x2 && points.x < ts->edge_limit.right_x1)) { //area2
            if (ts->edge_limit.in_which_area == AREA_EDGE) {
                ts->edge_limit.in_which_area = AREA_CRITICAL;
            }
        } else if (points.x < ts->edge_limit.left_x1 || points.x > ts->edge_limit.right_x1) {        //area 1
            if (ts->edge_limit.in_which_area == AREA_CRITICAL) {
                ts->edge_limit.in_which_area = AREA_EDGE;
                return true;
            }
            if (ts->edge_limit.in_which_area ==  AREA_NORMAL)
                return true;

            ts->edge_limit.in_which_area = AREA_EDGE;
        }
    }

    return false;
}

static bool corner_point_process(struct touchpanel_data *ts, struct corner_info *corner, struct point_info *points, int i)
{
    int j;
    if (ts->limit_corner) {
        if ((ts->limit_corner & (1 << CORNER_TOPLEFT)) && (points[i].x < ts->edge_limit.left_x3 && points[i].y < ts->edge_limit.left_y1)) {
            points[i].type  = AREA_CORNER;
            if (ts->edge_limit.in_which_area == AREA_NORMAL)
                return true;

            corner[CORNER_TOPLEFT].id = i;
            corner[CORNER_TOPLEFT].point = points[i];
            corner[CORNER_TOPLEFT].flag = true;

            ts->edge_limit.in_which_area = points[i].type;
        }
        if ((ts->limit_corner & (1 << CORNER_TOPRIGHT))  && (points[i].x < ts->edge_limit.left_x3 && points[i].y > ts->edge_limit.right_y1)) {
            points[i].type  = AREA_CORNER;
            if (ts->edge_limit.in_which_area == AREA_NORMAL)
                return true;

            corner[CORNER_TOPRIGHT].id = i;
            corner[CORNER_TOPRIGHT].point = points[i];
            corner[CORNER_TOPRIGHT].flag = true;

            ts->edge_limit.in_which_area = points[i].type;
        }
        if ((ts->limit_corner & (1 << CORNER_BOTTOMLEFT))  && (points[i].x > ts->edge_limit.right_x3 && points[i].y < ts->edge_limit.left_y1)) {
            points[i].type  = AREA_CORNER;
            if (ts->edge_limit.in_which_area == AREA_NORMAL)
                return true;

            corner[CORNER_BOTTOMLEFT].id = i;
            corner[CORNER_BOTTOMLEFT].point = points[i];
            corner[CORNER_BOTTOMLEFT].flag = true;

            ts->edge_limit.in_which_area = points[i].type;
        }
        if ((ts->limit_corner & (1 << CORNER_BOTTOMRIGHT))  && (points[i].x > ts->edge_limit.right_x3 && points[i].y > ts->edge_limit.right_y1)) {
            points[i].type  = AREA_CORNER;
            if (ts->edge_limit.in_which_area == AREA_NORMAL)
                return true;

            corner[CORNER_BOTTOMRIGHT].id = i;
            corner[CORNER_BOTTOMRIGHT].point = points[i];
            corner[CORNER_BOTTOMRIGHT].flag = true;

            ts->edge_limit.in_which_area = points[i].type;
        }

        if (points[i].type != AREA_CORNER) {
            if (ts->edge_limit.in_which_area == AREA_CORNER) {
                for (j = 0; j < 4; j++) {
                    if (corner[j].flag) {
#ifdef TYPE_B_PROTOCOL
                        input_mt_slot(ts->input_dev, corner[j].id);
                        input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 0);
#endif
                    }
                }
            }
            points[i].type = AREA_NORMAL;
            ts->edge_limit.in_which_area = points[i].type;
        }

    }

    return false;
}

static int touch_elimination_caculate(struct touchpanel_data *ts, int obj_attention, struct point_info *points)
{
    int i = 0, total_cnt = 0, unit = 0, max_array_x = 0, max_array_y = 0;
    uint16_t left_edge_bit = 0, right_edge_bit = 0;
    int left_edge_cnt = 0, right_edge_cnt = 0, left_center_cnt = 0, right_center_cnt = 0;
    static int left_trigger_status = 0, right_trigger_status = 0;       //store trigger status
    static int cross_status[10] = {0};                                  //store cross range status of each id
    static int reject_status[10] = {0};                                 //show reject status if each id

    for (i = 0; i < ts->max_num; i++) {
        if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01) {
            total_cnt++;

            if (ts->limit_edge) {
                if (points[i].x <= ts->monitor_data.eli_ver_range) {
                    left_edge_cnt++;
                    left_edge_bit = left_edge_bit | (1 << i);
                } else if ((points[i].x > ts->monitor_data.eli_ver_range) && (points[i].x < ts->resolution_info.max_x - ts->monitor_data.eli_ver_range)) {
                    cross_status[i] = 1;    //set cross status of i
                    left_center_cnt++;
                } else {
                    right_edge_cnt++;
                    right_edge_bit = right_edge_bit | (1 << i);
                }
            } else {
                if (points[i].y <= ts->monitor_data.eli_hor_range) {
                    left_edge_cnt++;
                    left_edge_bit = left_edge_bit | (1 << i);
                } else if ((points[i].y > ts->monitor_data.eli_hor_range) && (points[i].y < ts->resolution_info.max_y / 2)) {
                    cross_status[i] = 1;    //set cross status of i
                    left_center_cnt++;
                } else if ((points[i].y >= ts->resolution_info.max_y / 2) && (points[i].y < ts->resolution_info.max_y - ts->monitor_data.eli_hor_range)) {
                    cross_status[i] = 1;    //set cross status of i
                    right_center_cnt++;
                } else {
                    right_edge_cnt++;
                    right_edge_bit = right_edge_bit | (1 << i);
                }
            }
        } else {
            cross_status[i] = 0;     //recovery this id status
            reject_status[i] = 0;    //recovery this id status
        }
    }

    if (0 == total_cnt) {
        left_trigger_status = 0;     //recovery trigger status while all touch up
        right_trigger_status = 0;
        return obj_attention;
    }

    unit = ts->monitor_data.eli_size;
    max_array_x = ts->resolution_info.max_x / unit;
    max_array_y = ts->resolution_info.max_y / unit;
    if (ts->limit_edge) {      //in vertical mode
        if ((left_edge_cnt || right_edge_cnt) && left_center_cnt) {     //need to judge which point shoud be eliminated
            left_edge_bit = left_edge_bit | right_edge_bit;
            for (i = 0; i < ts->max_num; i++) {
                if ((((left_edge_bit & TOUCH_BIT_CHECK) >> i) & 0x01) && !cross_status[i]) {
                    reject_status[i] = 1;
                    obj_attention = obj_attention & (~(1 << i));

                    //count of each grip
                    if (!left_trigger_status && ts->monitor_data.eli_ver_pos) {
                        if (points[i].x <= ts->monitor_data.eli_ver_range)
                            ts->monitor_data.eli_ver_pos[points[i].x / unit * max_array_y + points[i].y / unit]++;
                        else
                            ts->monitor_data.eli_ver_pos[(ts->resolution_info.max_x - points[i].x) / unit * max_array_y + points[i].y / unit]++;
                    }
                }
            }

            left_trigger_status = 1;     //set trigger status
        } else if (left_edge_cnt || right_edge_cnt) {
            left_edge_bit = left_edge_bit | right_edge_bit;
            for (i = 0; i < ts->max_num; i++) {
                if ((((left_edge_bit & TOUCH_BIT_CHECK) >> i) & 0x01) && !cross_status[i] && reject_status[i]) {
                    obj_attention = obj_attention & (~(1 << i));
                }
            }
        } else {
            left_trigger_status = 0;
        }
    } else {
        //left part of panel
        if (left_edge_cnt && left_center_cnt) {
            for (i = 0; i < ts->max_num; i++) {
                if ((((left_edge_bit & TOUCH_BIT_CHECK) >> i) & 0x01) && !cross_status[i]) {
                    reject_status[i] = 1;
                    obj_attention = obj_attention & (~(1 << i));

                    //count of each grip
                    if (!left_trigger_status && ts->monitor_data.eli_hor_pos) {
                        ts->monitor_data.eli_hor_pos[points[i].y / unit * max_array_x + (ts->resolution_info.max_x - points[i].x) / unit]++;
                    }
                }
            }

            left_trigger_status = 1;     //set trigger status
        } else if (left_edge_cnt) {
            for (i = 0; i < ts->max_num; i++) {
                if ((((left_edge_bit & TOUCH_BIT_CHECK) >> i) & 0x01) && !cross_status[i] && reject_status[i]) {
                    obj_attention = obj_attention & (~(1 << i));
                }
            }
        } else {
            left_trigger_status = 0;
        }

        //right part of panel
        if (right_edge_cnt && right_center_cnt) {
            for (i = 0; i < ts->max_num; i++) {
                if ((((right_edge_bit & TOUCH_BIT_CHECK) >> i) & 0x01) && !cross_status[i]) {
                    reject_status[i] = 1;
                    obj_attention = obj_attention & (~(1 << i));

                    //count of each grip
                    if (!right_trigger_status && ts->monitor_data.eli_hor_pos) {
                        ts->monitor_data.eli_hor_pos[(ts->resolution_info.max_y - points[i].y) / unit * max_array_x + (ts->resolution_info.max_x - points[i].x) / unit]++;
                    }
                }
            }

            right_trigger_status = 1;
        } else if (right_edge_cnt) {
            for (i = 0; i < ts->max_num; i++) {
                if ((((right_edge_bit & TOUCH_BIT_CHECK) >> i) & 0x01) && !cross_status[i] && reject_status[i]) {
                    obj_attention = obj_attention & (~(1 << i));
                }
            }
        } else {
            right_trigger_status = 0;
        }
    }

    return obj_attention;
}

#ifdef CONFIG_TOUCHPANEL_ALGORITHM
static void special_touch_handle(struct touchpanel_data *ts)
{
    int obj_attention = 0;
    int i = 0;
    static int touch_report_num = 0;
    struct point_info points[10];
    if (!ts->ts_ops->special_points_report) {
        return;
    }
    obj_attention = ts->ts_ops->special_points_report(ts->chip_data, points, ts->max_num);
    if (obj_attention <= 0) {
        return;
    }

    for (i = 0; i < ts->max_num; i++) {
        if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01 && (points[i].status == 0)) // buf[0] == 0 is wrong point, no process
            continue;
        if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01 && (points[i].status != 0)) {
            TPD_DEBUG("special point report\n");
#ifdef TYPE_B_PROTOCOL
            input_mt_slot(ts->input_dev, i);
            input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 1);
#endif
            touch_report_num++;
            tp_touch_down(ts, points[i], touch_report_num, i);
            SET_BIT(ts->irq_slot, (1 << i));

        }
    }

    input_sync(ts->input_dev);

}
#endif


static void tp_touch_handle(struct touchpanel_data *ts)
{
    int i = 0;
    uint8_t finger_num = 0, touch_near_edge = 0, finger_num_center = 0;
    int obj_attention = 0;
    struct point_info points[10];
    struct corner_info corner[4];
    static bool up_status = false;
    static struct point_info last_point = {.x = 0, .y = 0};
    static int touch_report_num = 0;
    static unsigned int repeat_count = 0;

    if (!ts->ts_ops->get_touch_points) {
        TPD_INFO("not support ts->ts_ops->get_touch_points callback\n");
        return;
    }

    memset(points, 0, sizeof(points));
    memset(corner, 0, sizeof(corner));

    if (time_after(jiffies, ts->monitor_data.monitor_down) && ts->monitor_data.monitor_down) {
        ts->monitor_data.miss_irq++;
        TPD_DEBUG("ts->monitor_data.monitor_down %lu jiffies %lu\n", ts->monitor_data.monitor_down, jiffies);
    }
    obj_attention = ts->ts_ops->get_touch_points(ts->chip_data, points, ts->max_num);
    if ((obj_attention == -EINVAL) || (obj_attention < 0)) {
        TPD_DEBUG("Invalid points, ignore..\n");
        return;
    }

    mutex_lock(&ts->report_mutex);

    if (ts->kernel_grip_support) {
        obj_attention = notify_prevention_handle(ts->grip_info, obj_attention, points);
        if (ts->health_monitor_v2_support) {
            tp_healthinfo_report(&ts->monitor_data_v2, HEALTH_GRIP, ts->grip_info);
        }
    }
    if (ts->health_monitor_support) {
        touch_elimination_caculate(ts, obj_attention, points);
    }
#ifdef CONFIG_TOUCHPANEL_ALGORITHM
    obj_attention = touch_algorithm_handle(ts, obj_attention, points);
    special_touch_handle(ts);

#endif
    if ((obj_attention & TOUCH_BIT_CHECK) != 0) {
        up_status = false;
        ts->monitor_data.monitor_down = (jiffies + 2 * HZ) * (!ts->is_suspended);
        for (i = 0; i < ts->max_num; i++) {
            if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01 && (points[i].status == 0)) // buf[0] == 0 is wrong point, no process
                continue;
            if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01 && (points[i].status != 0)) {
                //Edge process before report abs
                if (ts->edge_limit_support) {
                    if (corner_point_process(ts, corner, points, i) || (!ts->drlimit_remove_support && edge_point_process(ts, points[i])))
                        continue;
                }
#ifdef TYPE_B_PROTOCOL
                input_mt_slot(ts->input_dev, i);
                input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 1);
#endif
                touch_report_num++;
                tp_touch_down(ts, points[i], touch_report_num, i);
                SET_BIT(ts->irq_slot, (1 << i));
                finger_num++;
                if (ts->ear_sense_support && ts->es_enable && \
                    (((points[i].y < ts->resolution_info.max_y / 2) && (points[i].x > 30) && (points[i].x < ts->resolution_info.max_x - 30)) || \
                     ((points[i].y > ts->resolution_info.max_y / 2) && (points[i].x > 70) && (points[i].x < ts->resolution_info.max_x - 70)))) {
                    finger_num_center++;
                }
                if (ts->face_detect_support && ts->fd_enable && \
                    (points[i].y < ts->resolution_info.max_y / 2) && (points[i].x > 30) && (points[i].x < ts->resolution_info.max_x - 30)) {
                    finger_num_center++;
                }
                if (points[i].x > ts->resolution_info.max_x / 100 && points[i].x < ts->resolution_info.max_x * 99 / 100) {
                    ts->view_area_touched = finger_num;
                } else {
                    touch_near_edge++;
                }
                /*strore  the last point data*/
                memcpy(&last_point, &points[i], sizeof(struct point_info));
            }
#ifdef TYPE_B_PROTOCOL
            else {
                input_mt_slot(ts->input_dev, i);
                if (ts->kernel_grip_support && ts->grip_info && ts->grip_info->eli_reject_status[i]
                    && !(ts->grip_info->grip_disable_level & (1 << GRIP_DISABLE_UP2CANCEL))) {
                    input_report_abs(ts->input_dev, ABS_MT_PRESSURE, UP2CANCEL_PRESSURE_VALUE);
                }
                input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 0);
            }
#endif
        }

        if (touch_near_edge == finger_num) {        //means all the touchpoint is near the edge
            ts->view_area_touched = 0;
        }
        if(ts->ear_sense_support && ts->es_enable && (finger_num_center > (ts->touch_count & 0x0F))) {
            ts->delta_state = TYPE_DELTA_BUSY;
            queue_work(ts->delta_read_wq, &ts->read_delta_work);
        }
    } else {
        if (up_status) {
            tp_touch_up(ts);
            mutex_unlock(&ts->report_mutex);
            return;
        }
        if (time_before(jiffies, ts->monitor_data.monitor_up) && ts->monitor_data.monitor_up) {
            repeat_count++;
            if (repeat_count == 5) {
                ts->monitor_data.repeat_finger++;
            }
        } else {
            repeat_count = 0;
        }
        ts->total_operate_times++;
        ts->monitor_data.monitor_up = jiffies + 4;
        ts->monitor_data.monitor_down = 0;
        finger_num = 0;
        finger_num_center = 0;
        touch_report_num = 0;
        repeat_count = 0;
#ifdef TYPE_B_PROTOCOL
        for (i = 0; i < ts->max_num; i++) {
            input_mt_slot(ts->input_dev, i);
            input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 0);
        }
#endif
        tp_touch_up(ts);
        ts->view_area_touched = 0;
        ts->irq_slot = 0;
        up_status = true;
        TPD_DETAIL("all touch up,view_area_touched=%d finger_num=%d\n", ts->view_area_touched, finger_num);
        TPD_DETAIL("last point x:%d y:%d\n", last_point.x, last_point.y);
        if (ts->edge_limit_support)
            ts->edge_limit.in_which_area = AREA_NOTOUCH;
    }
    input_sync(ts->input_dev);
    ts->touch_count = (finger_num << 4) | (finger_num_center & 0x0F);

    mutex_unlock(&ts->report_mutex);

    if (ts->health_monitor_v2_support) {
        ts->monitor_data_v2.touch_points = points;
        ts->monitor_data_v2.touch_num = finger_num;
        tp_healthinfo_report(&ts->monitor_data_v2, HEALTH_TOUCH, &obj_attention);
    }
}

static void tp_btnkey_release(struct touchpanel_data *ts)
{
    if (CHK_BIT(ts->vk_bitmap, BIT_MENU))
        input_report_key_oppo(ts->kpd_input_dev, KEY_MENU, 0);
    if (CHK_BIT(ts->vk_bitmap, BIT_HOME))
        input_report_key_oppo(ts->kpd_input_dev, KEY_HOMEPAGE, 0);
    if (CHK_BIT(ts->vk_bitmap, BIT_BACK))
        input_report_key_oppo(ts->kpd_input_dev, KEY_BACK, 0);
    input_sync(ts->kpd_input_dev);
}

static void tp_btnkey_handle(struct touchpanel_data *ts)
{
    u8 touch_state = 0;

    if (ts->vk_type != TYPE_AREA_SEPRATE) {
        TPD_DEBUG("TP vk_type not proper, checktouchpanel, button-type\n");

        return;
    }
    if (!ts->ts_ops->get_keycode) {
        TPD_INFO("not support ts->ts_ops->get_keycode callback\n");

        return;
    }
    touch_state = ts->ts_ops->get_keycode(ts->chip_data);

    if (CHK_BIT(ts->vk_bitmap, BIT_MENU))
        input_report_key_oppo(ts->kpd_input_dev, KEY_MENU, CHK_BIT(touch_state, BIT_MENU));
    if (CHK_BIT(ts->vk_bitmap, BIT_HOME))
        input_report_key_oppo(ts->kpd_input_dev, KEY_HOMEPAGE, CHK_BIT(touch_state, BIT_HOME));
    if (CHK_BIT(ts->vk_bitmap, BIT_BACK))
        input_report_key_oppo(ts->kpd_input_dev, KEY_BACK, CHK_BIT(touch_state, BIT_BACK));
    input_sync(ts->kpd_input_dev);
}

static void tp_config_handle(struct touchpanel_data *ts)
{
    int ret = 0;
    if (!ts->ts_ops->fw_handle) {
        TPD_INFO("not support ts->ts_ops->fw_handle callback\n");
        return;
    }

    ret = ts->ts_ops->fw_handle(ts->chip_data);
}

static void health_monitor_handle(struct touchpanel_data *ts)
{
    if (!ts->ts_ops->health_report) {
        TPD_INFO("not support ts->debug_info_ops->health_report callback\n");
        return;
    }
    if (ts->health_monitor_support || tp_debug)
        ts->ts_ops->health_report(ts->chip_data, &ts->monitor_data);
}

static void tp_face_detect_handle(struct touchpanel_data *ts)
{
    int ps_state = 0;

    if (!ts->ts_ops->get_face_state) {
        TPD_INFO("not support ts->ts_ops->get_face_state callback\n");
        return;
    }

    ps_state = ts->ts_ops->get_face_state(ts->chip_data);
    if (ps_state < 0)
        return;

    input_event(ts->ps_input_dev, EV_MSC, MSC_RAW, ps_state);
    input_sync(ts->ps_input_dev);

    if (ts->health_monitor_v2_support) {
        tp_healthinfo_report(&ts->monitor_data_v2, HEALTH_FACE_DETECT, &ps_state);
    }
}

static void tp_fingerprint_handle(struct touchpanel_data *ts)
{
    struct fp_underscreen_info fp_tpinfo;

    if (!ts->ts_ops->screenon_fingerprint_info) {
        TPD_INFO("not support screenon_fingerprint_info callback.\n");
        return;
    }

    ts->ts_ops->screenon_fingerprint_info(ts->chip_data, &fp_tpinfo);
    ts->fp_info.area_rate = fp_tpinfo.area_rate;
    ts->fp_info.x = fp_tpinfo.x;
    ts->fp_info.y = fp_tpinfo.y;
    if(fp_tpinfo.touch_state == FINGERPRINT_DOWN_DETECT) {
        TPD_INFO("screen on down : (%d, %d)\n", ts->fp_info.x, ts->fp_info.y);
        ts->fp_info.touch_state = 1;
        opticalfp_irq_handler(&ts->fp_info);
        if (ts->health_monitor_v2_support) {
            tp_healthinfo_report(&ts->monitor_data_v2, HEALTH_FINGERPRINT, &fp_tpinfo.area_rate);
        }
    } else if(fp_tpinfo.touch_state == FINGERPRINT_UP_DETECT) {
        TPD_INFO("screen on up : (%d, %d)\n", ts->fp_info.x, ts->fp_info.y);
        ts->fp_info.touch_state = 0;
        opticalfp_irq_handler(&ts->fp_info);
    } else if (ts->fp_info.touch_state) {
        opticalfp_irq_handler(&ts->fp_info);
    }
}

static void tp_async_work_callback(void)
{
    struct touchpanel_data *ts = g_tp;

    if (ts == NULL)
        return;

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    if ((ts->boot_mode == META_BOOT || ts->boot_mode == FACTORY_BOOT))
#else
    if ((ts->boot_mode == MSM_BOOT_MODE__FACTORY || ts->boot_mode == MSM_BOOT_MODE__RF || ts->boot_mode == MSM_BOOT_MODE__WLAN))
#endif
    {
        TPD_INFO("%s: in ftm mode, no need to call back\n", __func__);
        return;
    }

    TPD_INFO("%s: async work\n", __func__);
    if (ts->use_resume_notify && ts->suspend_state == TP_RESUME_COMPLETE) {
        complete(&ts->resume_complete);
        return;
    }

    if (ts->in_test_process) {
        TPD_INFO("%s: In test process, do not switch mode\n", __func__);
        return;
    }
    TPD_INFO("%s schedule_work  start !!", __func__);
    schedule_work(&ts->async_work);
}

static void tp_async_work_lock(struct work_struct *work)
{
    struct touchpanel_data *ts = container_of(work, struct touchpanel_data, async_work);
    mutex_lock(&ts->mutex);
    if (ts->ts_ops->async_work) {
        ts->ts_ops->async_work(ts->chip_data);
    }
    mutex_unlock(&ts->mutex);
}

static void tp_work_common_callback(void)
{
    struct touchpanel_data *ts;

    if (g_tp == NULL)
        return;
    ts = g_tp;
    tp_work_func(ts);
}

static void tp_work_func(struct touchpanel_data *ts)
{
    u32 cur_event = 0;

    if (!ts->ts_ops->trigger_reason && !ts->ts_ops->u32_trigger_reason) {
        TPD_INFO("not support ts_ops->trigger_reason callback\n");
        return;
    }
    /*
     *  trigger_reason:this callback determine which trigger reason should be
     *  The value returned has some policy!
     *  1.IRQ_EXCEPTION /IRQ_GESTURE /IRQ_IGNORE /IRQ_FW_CONFIG --->should be only reported  individually
     *  2.IRQ_TOUCH && IRQ_BTN_KEY --->should depends on real situation && set correspond bit on trigger_reason
     */
    if (ts->ts_ops->u32_trigger_reason) {
        cur_event = ts->ts_ops->u32_trigger_reason(ts->chip_data, (ts->gesture_enable || ts->fp_enable), ts->is_suspended);
    } else {
        cur_event = ts->ts_ops->trigger_reason(ts->chip_data, (ts->gesture_enable || ts->fp_enable), ts->is_suspended);
    }
    if (CHK_BIT(cur_event, IRQ_TOUCH) || CHK_BIT(cur_event, IRQ_BTN_KEY) || CHK_BIT(cur_event, IRQ_FW_HEALTH) || \
        CHK_BIT(cur_event, IRQ_FACE_STATE) || CHK_BIT(cur_event, IRQ_FINGERPRINT)) {
        if (CHK_BIT(cur_event, IRQ_BTN_KEY)) {
            tp_btnkey_handle(ts);
        }
        if (CHK_BIT(cur_event, IRQ_TOUCH) && (!ts->is_suspended)) {
            tp_touch_handle(ts);
        }
        if (CHK_BIT(cur_event, IRQ_FW_HEALTH) && (!ts->is_suspended)) {
            health_monitor_handle(ts);
        }
        if (CHK_BIT(cur_event, IRQ_FACE_STATE) && ts->fd_enable) {
            tp_face_detect_handle(ts);
        }
        if (CHK_BIT(cur_event, IRQ_FINGERPRINT) && ts->fp_enable) {
            tp_fingerprint_handle(ts);
        }
    } else if (CHK_BIT(cur_event, IRQ_GESTURE)) {
        tp_gesture_handle(ts);
    } else if (CHK_BIT(cur_event, IRQ_EXCEPTION)) {
        tp_exception_handle(ts);
    } else if (CHK_BIT(cur_event, IRQ_FW_CONFIG)) {
        tp_config_handle(ts);
    }  else if (CHK_BIT(cur_event, IRQ_FW_AUTO_RESET)) {
        tp_fw_auto_reset_handle(ts);
    } else {
        TPD_DEBUG("unknown irq trigger reason\n");
    }
}

static void tp_work_func_unlock(struct touchpanel_data *ts)
{
    if (ts->ts_ops->irq_handle_unlock) {
        ts->ts_ops->irq_handle_unlock(ts->chip_data);
    }
}

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
extern void primary_display_esd_check_enable(int enable);
#endif
void __attribute__((weak)) display_esd_check_enable_bytouchpanel(bool enable)
{
    return;
}

static void tp_fw_update_work(struct work_struct *work)
{
    const struct firmware *fw = NULL;
    int ret, fw_update_result = 0;
    int count_tmp = 0, retry = 5;
    char *p_node = NULL;
    char *fw_name_fae = NULL;
    char *postfix = "_FAE";
    uint8_t copy_len = 0;
    u64 start_time = 0;

    struct touchpanel_data *ts = container_of(work, struct touchpanel_data,
                                 fw_update_work);

    if (!ts->ts_ops->fw_check || !ts->ts_ops->reset) {
        TPD_INFO("not support ts_ops->fw_check callback\n");
        complete(&ts->fw_complete);
        return;
    }

    TPD_INFO("%s: fw_name = %s\n", __func__, ts->panel_data.fw_name);

    if (ts->health_monitor_v2_support) {
        reset_healthinfo_time_counter(&start_time);
    }
    mutex_lock(&ts->mutex);

    if (!ts->irq_trigger_hdl_support && ts->int_mode == BANNABLE) {
        disable_irq_nosync(ts->irq);
    }

    ts->loading_fw = true;

    if (ts->esd_handle_support) {
        esd_handle_switch(&ts->esd_info, false);
    }

    display_esd_check_enable_bytouchpanel(0);
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    primary_display_esd_check_enable(0); //avoid rst pulled to low while updating
#endif

    if (ts->ts_ops->fw_update) {
        do {
            if(ts->firmware_update_type == 0 || ts->firmware_update_type == 1) {
                if(ts->fw_update_app_support) {
                    fw_name_fae = kzalloc(MAX_FW_NAME_LENGTH, GFP_KERNEL);
                    if(fw_name_fae == NULL) {
                        TPD_INFO("fw_name_fae kzalloc error!\n");
                        goto EXIT;
                    }
                    p_node  = strstr(ts->panel_data.fw_name, ".");
                    if(p_node == NULL) {
                        TPD_INFO("p_node strstr error!\n");
                        goto EXIT;
                    }
                    copy_len = p_node - ts->panel_data.fw_name;
                    memcpy(fw_name_fae, ts->panel_data.fw_name, copy_len);
                    strlcat(fw_name_fae, postfix, MAX_FW_NAME_LENGTH);
                    strlcat(fw_name_fae, p_node, MAX_FW_NAME_LENGTH);
                    TPD_INFO("fw_name_fae is %s\n", fw_name_fae);
                    ret = request_firmware(&fw, fw_name_fae, ts->dev);
                    if (!ret)
                        break;
                } else {
                    ret = request_firmware(&fw, ts->panel_data.fw_name, ts->dev);
                    if (!ret)
                        break;
                }
            } else {
                ret = request_firmware_select(&fw, ts->panel_data.fw_name, ts->dev);
                if (!ret)
                    break;
            }
        } while((ret < 0) && (--retry > 0));

        TPD_INFO("retry times %d\n", 5 - retry);

        if (!ret || ts->is_noflash_ic) {
            do {
                count_tmp++;
                ret = ts->ts_ops->fw_update(ts->chip_data, fw, ts->force_update);
                fw_update_result = ret;
                if (ret == FW_NO_NEED_UPDATE) {
                    break;
                }

                if(!ts->is_noflash_ic) {        //noflash update fw in reset and do bootloader reset in get_chip_info
                    ret |= ts->ts_ops->reset(ts->chip_data);
                    ret |= ts->ts_ops->get_chip_info(ts->chip_data);
                }

                ret |= ts->ts_ops->fw_check(ts->chip_data, &ts->resolution_info, &ts->panel_data);
            } while((count_tmp < 2) && (ret != 0));

            if(fw != NULL) {
                release_firmware(fw);
            }
        } else {
            TPD_INFO("%s: fw_name request failed %s %d\n", __func__, ts->panel_data.fw_name, ret);
            if (ts->health_monitor_v2_support) {
                tp_healthinfo_report(&ts->monitor_data_v2, HEALTH_FW_UPDATE, "FW Request Failed");
            }
            goto EXIT;
        }
    }

    if (ts->ts_ops->bootup_test && ts->health_monitor_support) {
        ret = request_firmware(&fw, ts->panel_data.test_limit_name, ts->dev);
        if (ret < 0) {
            TPD_INFO("Request firmware failed - %s (%d)\n", ts->panel_data.test_limit_name, ret);
        } else {
            ts->ts_ops->bootup_test(ts->chip_data, fw, &ts->monitor_data, &ts->hw_res);
            release_firmware(fw);
        }
    }

    tp_touch_release(ts);
    tp_btnkey_release(ts);
    operate_mode_switch(ts);
    if (fw_update_result != FW_NO_NEED_UPDATE) {
        if (ts->spurious_fp_support && ts->ts_ops->finger_proctect_data_get) {
            ts->ts_ops->finger_proctect_data_get(ts->chip_data);
        }
    }

EXIT:
    ts->loading_fw = false;

    display_esd_check_enable_bytouchpanel(1);
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    primary_display_esd_check_enable(1); //avoid rst pulled to low while updating
#endif

    if (ts->esd_handle_support) {
        esd_handle_switch(&ts->esd_info, true);
    }

    kfree(fw_name_fae);
    fw_name_fae = NULL;
    if (ts->int_mode == BANNABLE) {
        enable_irq(ts->irq);
    }
    mutex_unlock(&ts->mutex);
    if (ts->health_monitor_v2_support) {
        tp_healthinfo_report(&ts->monitor_data_v2, HEALTH_FW_UPDATE_COST, &start_time);
    }

    ts->force_update = 0;

    complete(&ts->fw_complete); //notify to init.rc that fw update finished
    return;
}

#ifndef TPD_USE_EINT
static enum hrtimer_restart touchpanel_timer_func(struct hrtimer *timer)
{
    struct touchpanel_data *ts = container_of(timer, struct touchpanel_data, timer);

    mutex_lock(&ts->mutex);
    tp_work_func(ts);
    mutex_unlock(&ts->mutex);
    hrtimer_start(&ts->timer, ktime_set(0, 12500000), HRTIMER_MODE_REL);

    return HRTIMER_NORESTART;
}
#else
static irqreturn_t tp_irq_thread_fn(int irq, void *dev_id)
{
    struct touchpanel_data *ts = (struct touchpanel_data *)dev_id;
    if (ts->ts_ops->tp_irq_throw_away) {
        if (ts->ts_ops->tp_irq_throw_away(ts->chip_data)) {
            return IRQ_HANDLED;
        }
    }

    if (ts->irq_need_dev_resume_ok) {
        if (ts->i2c_ready == false) {
            //TPD_INFO("Wait device resume!");
            wait_event_interruptible_timeout(waiter,
                                             ts->i2c_ready,
                                             msecs_to_jiffies(50));
            //TPD_INFO("Device maybe resume!");
        }
        if (ts->i2c_ready == false) {
            TPD_INFO("The device not resume 50ms!");
            return IRQ_HANDLED;
        }
    }

#ifdef CONFIG_TOUCHIRQ_UPDATE_QOS
    if (pm_qos_state && !ts->is_suspended && !ts->touch_count) {
        pm_qos_value = PM_QOS_TOUCH_WAKEUP_VALUE;
        pm_qos_update_request(&pm_qos_req, pm_qos_value);
    }
#endif

    if(ts->sec_long_low_trigger) {
        disable_irq_nosync(ts->irq);
    }
    if (ts->int_mode == BANNABLE) {
        mutex_lock(&ts->mutex);
        tp_work_func(ts);
        mutex_unlock(&ts->mutex);
    } else {
        tp_work_func_unlock(ts);
    }
    if(ts->sec_long_low_trigger) {
        enable_irq(ts->irq);
    }

#ifdef CONFIG_TOUCHIRQ_UPDATE_QOS
    if (PM_QOS_TOUCH_WAKEUP_VALUE == pm_qos_value) {
        pm_qos_value = PM_QOS_DEFAULT_VALUE;
        pm_qos_update_request(&pm_qos_req, pm_qos_value);
    }
#endif

    return IRQ_HANDLED;
}
#endif

static void tp_freq_hop_work(struct work_struct *work)
{
    struct touchpanel_data *ts = container_of(work, struct touchpanel_data,
                                 freq_hop_info.freq_hop_work.work);

    TPD_INFO("syna_tcm_freq_hop_work\n");
    if (!ts->is_suspended) {
        TPD_INFO("trigger frequency hopping~~~~\n");

        if (!ts->ts_ops->freq_hop_trigger) {
            TPD_INFO("%s:not support ts_ops->freq_hop_trigger callback\n", __func__);
            return;
        }
        ts->ts_ops->freq_hop_trigger(ts->chip_data);
    }

    if (ts->freq_hop_info.freq_hop_simulating) {
        TPD_INFO("queue_delayed_work again\n");
        queue_delayed_work(ts->freq_hop_info.freq_hop_workqueue, &ts->freq_hop_info.freq_hop_work, ts->freq_hop_info.freq_hop_freq * HZ);
    }
}

static void tp_freq_hop_simulate(struct touchpanel_data *ts, int freq_hop_freq)
{
    TPD_INFO("%s is called.\n", __func__);
    ts->freq_hop_info.freq_hop_freq = freq_hop_freq;
    if (ts->freq_hop_info.freq_hop_simulating && !freq_hop_freq) {
        ts->freq_hop_info.freq_hop_simulating = false;
    } else {
        if (!ts->freq_hop_info.freq_hop_simulating) {
            ts->freq_hop_info.freq_hop_simulating = true;
            queue_delayed_work(ts->freq_hop_info.freq_hop_workqueue, &ts->freq_hop_info.freq_hop_work, ts->freq_hop_info.freq_hop_freq * HZ);
        }
    }
}

/**
 * tp_gesture_enable_flag -   expose gesture control status for other module.
 * Return gesture_enable status.
 */
int tp_gesture_enable_flag(void)
{
    if (!g_tp || !g_tp->is_incell_panel)
        return LCD_POWER_OFF;

    TPD_INFO("g_tp->gesture_enable is %d\n", g_tp->gesture_enable);

    return (g_tp->gesture_enable > 0) ? LCD_POWER_ON : LCD_POWER_OFF;
}
EXPORT_SYMBOL(tp_gesture_enable_flag);
/*
*Interface for lcd to control reset pin
*/
int tp_control_reset_gpio(bool enable)
{
    if (!g_tp) {
        return 0;
    }

    if (gpio_is_valid(g_tp->hw_res.reset_gpio)) {
        if (g_tp->ts_ops->reset_gpio_control) {
            g_tp->ts_ops->reset_gpio_control(g_tp->chip_data, enable);
        }
    }

    return 0;
}


int tp_control_cs_gpio(bool enable)
{
    if(!g_tp){
        return 0;
    }

    if (gpio_is_valid(g_tp->hw_res.cs_gpio)) {
        if (g_tp->ts_ops->cs_gpio_control) {
            g_tp->ts_ops->cs_gpio_control(g_tp->chip_data, enable);
        }
    }

    return 0;
}

EXPORT_SYMBOL(tp_control_cs_gpio);


/*
 * check_touchirq_triggered--used for stop system going sleep when touch irq is triggered
 * 1 if irq triggered, otherwise is 0
*/
int check_touchirq_triggered(void)
{
    int value = -1;

    if (!g_tp) {
        return 0;
    }
    if ((1 != (g_tp->gesture_enable & 0x01)) && (0 == g_tp->fp_enable)) {
        return 0;
    }

    value = gpio_get_value(g_tp->hw_res.irq_gpio);
    if ((0 == value) && (g_tp->irq_flags & IRQF_TRIGGER_LOW)) {
        TPD_INFO("touch irq is triggered.\n");
        return 1; //means irq is triggered
    }
    if ((1 == value) && (g_tp->irq_flags & IRQF_TRIGGER_HIGH)) {
        TPD_INFO("touch irq is triggered.\n");
        return 1; //means irq is triggered
    }

    return 0;
}
EXPORT_SYMBOL(check_touchirq_triggered);


/*
 *  * check_headset_state----expose to be called by audio int to get headset state
 *   * @headset_state : 1 if headset checked, otherwise is 0
 *   */
void switch_headset_state(int headset_state)
{
    if (!g_tp) {
        return;
    }
    TPD_INFO("%s: ENTER\n", __func__);

    if (g_tp->headset_pump_support && (g_tp->is_headset_checked != headset_state)) {
        g_tp->is_headset_checked = !!headset_state;
        TPD_INFO("%s: check headset state : %d, is_suspended: %d\n", __func__, headset_state, g_tp->is_suspended);
        if (!g_tp->is_suspended && (g_tp->suspend_state == TP_SPEEDUP_RESUME_COMPLETE)
            && !g_tp->loading_fw) {
            mutex_lock(&g_tp->mutex);
            g_tp->ts_ops->mode_switch(g_tp->chip_data, MODE_HEADSET, g_tp->is_headset_checked);
            mutex_unlock(&g_tp->mutex);
        }
    }
    TPD_INFO("%s: END\n", __func__);
}
EXPORT_SYMBOL(switch_headset_state);

/*
 *    gesture_enable = 0 : disable gesture
 *    gesture_enable = 1 : enable gesture when ps is far away
 *    gesture_enable = 2 : disable gesture when ps is near
 *    gesture_enable = 3 : enable single tap gesture when ps is far away
 */
static ssize_t proc_gesture_control_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int value = 0;
    char buf[4] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > 2)
        return count;
    if (!ts)
        return count;

    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &value);
    if (value > 3 || (ts->gesture_test_support && ts->gesture_test.flag))
        return count;

    mutex_lock(&ts->mutex);
    if (ts->gesture_enable != value) {
        ts->gesture_enable = value;
        TPD_INFO("%s: gesture_enable = %d, is_suspended = %d\n", __func__, ts->gesture_enable, ts->is_suspended);
        if (ts->is_incell_panel && (ts->suspend_state == TP_RESUME_EARLY_EVENT || ts->disable_gesture_ctrl) && (ts->tp_resume_order == LCD_TP_RESUME)) {
            TPD_INFO("tp will resume, no need mode_switch in incell panel\n"); /*avoid i2c error or tp rst pulled down in lcd resume*/
        } else if (ts->is_suspended) {
            if (ts->fingerprint_underscreen_support && ts->fp_enable && ts->ts_ops->enable_gesture_mask) {
                ts->ts_ops->enable_gesture_mask(ts->chip_data, (ts->gesture_enable & 0x01) == 1);
            } else {
                operate_mode_switch(ts);
            }
        }
    } else {
        TPD_INFO("%s: do not do same operator :%d\n", __func__, value);
    }
    mutex_unlock(&ts->mutex);

    return count;
}

static ssize_t proc_gesture_control_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return 0;

    TPD_DEBUG("double tap enable is: %d\n", ts->gesture_enable);
    ret = snprintf(page, PAGESIZE - 1, "%d", ts->gesture_enable);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

static ssize_t proc_coordinate_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return 0;

    TPD_DEBUG("%s:gesture_type = %u\n", __func__, ts->gesture.gesture_type);
    ret = snprintf(page, PAGESIZE - 1, "%u,%d:%d,%d:%d,%d:%d,%d:%d,%d:%d,%d:%d,%u", ts->gesture.gesture_type,
                   ts->gesture.Point_start.x, ts->gesture.Point_start.y, ts->gesture.Point_end.x, ts->gesture.Point_end.y,
                   ts->gesture.Point_1st.x,   ts->gesture.Point_1st.y,   ts->gesture.Point_2nd.x, ts->gesture.Point_2nd.y,
                   ts->gesture.Point_3rd.x,   ts->gesture.Point_3rd.y,   ts->gesture.Point_4th.x, ts->gesture.Point_4th.y,
                   ts->gesture.clockwise);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

static const struct file_operations proc_gesture_control_fops = {
    .write = proc_gesture_control_write,
    .read  = proc_gesture_control_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static const struct file_operations proc_coordinate_fops = {
    .read  = proc_coordinate_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_ps_status_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int value = 0;
    char buf[4] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    if (count > 2)
        return count;
    if (!ts)
        return count;

    if (!ts->ts_ops->write_ps_status) {
        TPD_INFO("not support ts_ops->write_ps_status callback\n");
        return count;
    }

    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &value);
    if (value > 2)
        return count;

    if (!ts->is_suspended && (ts->suspend_state == TP_SPEEDUP_RESUME_COMPLETE)) {
        mutex_lock(&ts->mutex);
        ts->ps_status = value;
        ts->ts_ops->write_ps_status(ts->chip_data, value);
        mutex_unlock(&ts->mutex);
    }

    return count;
}

static ssize_t proc_ps_support_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts) {
        snprintf(page, PAGESIZE - 1, "%d\n", -1); //no support
    } else if (!ts->ts_ops->write_ps_status) {
        snprintf(page, PAGESIZE - 1, "%d\n", -1);
    } else {
        snprintf(page, PAGESIZE - 1, "%d\n", 0); //support
    }
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    return ret;
}

static const struct file_operations proc_write_ps_status_fops = {
    .write = proc_ps_status_write,
    .read  = proc_ps_support_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

//proc/touchpanel/game_switch_enable
static ssize_t proc_game_switch_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int value = 0 ;
    char buf[4] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > 4) {
        TPD_INFO("%s:count > 4\n", __func__);
        return count;
    }

    if (!ts) {
        TPD_INFO("%s: ts is NULL\n", __func__);
        return count;
    }

    if (!ts->ts_ops->mode_switch) {
        TPD_INFO("%s:not support ts_ops->mode_switch callback\n", __func__);
        return count;
    }
    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%x", (uint32_t *)&value);
    ts->noise_level = value;
    if (ts->health_monitor_v2_support) {
        ts->monitor_data_v2.in_game_mode = value;
    }

    TPD_INFO("%s: game_switch value=0x%x\n", __func__, value);
    if (!ts->is_suspended) {
        mutex_lock(&ts->mutex);
        ts->ts_ops->mode_switch(ts->chip_data, MODE_GAME, value > 0);
#ifdef CONFIG_TOUCHPANEL_ALGORITHM
        switch_kalman_fun(ts, value > 0);
#endif

        mutex_unlock(&ts->mutex);
    } else {
        TPD_INFO("%s: game_switch_support is_suspended.\n", __func__);
    }

    return count;
}

static ssize_t proc_game_switch_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts) {
        snprintf(page, PAGESIZE - 1, "%d\n", -1); //no support
    } else {
        snprintf(page, PAGESIZE - 1, "%d\n", ts->noise_level); //support
    }
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    return ret;
}

static const struct file_operations proc_game_switch_fops = {
    .write = proc_game_switch_write,
    .read  = proc_game_switch_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

//proc/touchpanel/black_screen_test
static ssize_t proc_black_screen_test_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    int retry = 20;
    int msg_size = 256;
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    TPD_INFO("%s %ld %lld\n", __func__, count, *ppos);

    if (!ts || !ts->gesture_test.flag)
        return 0;

    ts->gesture_test.message = kzalloc(msg_size, GFP_KERNEL);
    if (!ts->gesture_test.message) {
        TPD_INFO("failed to alloc memory\n");
        return 0;
    }

    /* wait until tp is in sleep, then sleep 500ms to make sure tp is in gesture mode*/
    do {
        if (ts->is_suspended) {
            msleep(500);
            break;
        }
        msleep(200);
    } while(--retry);

    TPD_INFO("%s retry times %d\n", __func__, retry);
    if (retry == 0 && !ts->is_suspended) {
        snprintf(ts->gesture_test.message, msg_size - 1, "1 errors: not in sleep ");
        goto OUT;
    }

    mutex_lock(&ts->mutex);
    if (ts->ts_ops->black_screen_test) {
        ts->ts_ops->black_screen_test(ts->chip_data, ts->gesture_test.message);
    } else {
        TPD_INFO("black_screen_test not support\n");
        snprintf(ts->gesture_test.message, msg_size - 1, "1 errors:not support gesture test");
    }
    mutex_unlock(&ts->mutex);

OUT:
    ts->gesture_test.flag = false;
    ts->gesture_enable = ts->gesture_test.gesture_backup;

    if (!ts->auto_test_force_pass_support) {
        ret = simple_read_from_buffer(user_buf, count, ppos, ts->gesture_test.message, strlen(ts->gesture_test.message));
    } else {
        ret = simple_read_from_buffer(user_buf, count, ppos, "0", 1);
    }
    kfree(ts->gesture_test.message);
    return ret;
}

static ssize_t proc_black_screen_test_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int value = 0;
    char buf[4] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > 2 || !ts)
        return count;

    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &value);
    TPD_INFO("%s %d\n", __func__, value);

    ts->gesture_test.gesture_backup = ts->gesture_enable;
    ts->gesture_enable = true;
    ts->gesture_test.flag = !!value;

    return count;
}

static const struct file_operations proc_black_screen_test_fops = {
    .owner = THIS_MODULE,
    .open  = simple_open,
    .read  = proc_black_screen_test_read,
    .write = proc_black_screen_test_write,
};

//proc/touchpanel/irq_depth
static ssize_t proc_get_irq_depth_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct irq_desc *desc = NULL;

    if(!ts) {
        return 0;
    }

    desc = irq_to_desc(ts->irq);

    snprintf(page, PAGESIZE - 1, "depth:%d, state:%d\n", desc->depth, gpio_get_value(ts->hw_res.irq_gpio));
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    return ret;
}

static ssize_t proc_irq_status_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
    int value = 0;
    char buf[4] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > 2 || !ts)
        return count;

    if (copy_from_user(buf, user_buf, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &value);
    TPD_INFO("%s %d, %s ts->irq=%d\n", __func__, value, value ? "enable" : "disable", ts->irq);

    if (value == 1) {
        enable_irq(ts->irq);
    } else {
        disable_irq_nosync(ts->irq);
    }

    return count;
}

static const struct file_operations proc_get_irq_depth_fops = {
    .owner = THIS_MODULE,
    .open  = simple_open,
    .read  = proc_get_irq_depth_read,
    .write = proc_irq_status_write,
};

static ssize_t proc_glove_control_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int ret = 0 ;
    char buf[3] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return count;
    if (count > 2)
        return count;
    if (!ts->ts_ops->mode_switch) {
        TPD_INFO("not support ts_ops->mode_switch callback\n");
        return count;
    }
    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &ret);
    TPD_INFO("%s:buf = %d, ret = %d\n", __func__, *buf, ret);
    if ((ret == 0) || (ret == 1)) {
        mutex_lock(&ts->mutex);
        ts->glove_enable = ret;
        ret = ts->ts_ops->mode_switch(ts->chip_data, MODE_GLOVE, ts->glove_enable);
        if (ret < 0) {
            TPD_INFO("%s, Touchpanel operate mode switch failed\n", __func__);
        }
        mutex_unlock(&ts->mutex);
    }
    switch(ret) {
    case 0:
        TPD_DEBUG("tp_glove_func will be disable\n");
        break;
    case 1:
        TPD_DEBUG("tp_glove_func will be enable\n");
        break;
    default:
        TPD_DEBUG("Please enter 0 or 1 to open or close the glove function\n");
    }

    return count;
}

static ssize_t proc_glove_control_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return 0;

    TPD_INFO("glove mode enable is: %d\n", ts->glove_enable);
    ret = snprintf(page, PAGESIZE - 1, "%d\n", ts->glove_enable);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

static const struct file_operations proc_glove_control_fops = {
    .write = proc_glove_control_write,
    .read =  proc_glove_control_read,
    .open = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t cap_vk_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct button_map *button_map;
    if (!g_tp)
        return sprintf(buf, "not support");

    button_map = &g_tp->button_map;
    return sprintf(buf,
                   __stringify(EV_KEY) ":" __stringify(KEY_MENU)   ":%d:%d:%d:%d"
                   ":" __stringify(EV_KEY) ":" __stringify(KEY_HOMEPAGE)   ":%d:%d:%d:%d"
                   ":" __stringify(EV_KEY) ":" __stringify(KEY_BACK)   ":%d:%d:%d:%d"
                   "\n", button_map->coord_menu.x, button_map->coord_menu.y, button_map->width_x, button_map->height_y, \
                   button_map->coord_home.x, button_map->coord_home.y, button_map->width_x, button_map->height_y, \
                   button_map->coord_back.x, button_map->coord_back.y, button_map->width_x, button_map->height_y);
}

static struct kobj_attribute virtual_keys_attr = {
    .attr = {
        .name = "virtualkeys."TPD_DEVICE,
        .mode = S_IRUGO,
    },
    .show = &cap_vk_show,
};

static struct attribute *properties_attrs[] = {
    &virtual_keys_attr.attr,
    NULL
};

static struct attribute_group properties_attr_group = {
    .attrs = properties_attrs,
};

static ssize_t proc_debug_control_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    uint8_t ret = 0;
    char page[PAGESIZE] = {0};

    TPD_INFO("%s: tp_debug = %d.\n", __func__, tp_debug);
    snprintf(page, PAGESIZE - 1, "%u", tp_debug);
    ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

    return ret;
}

static ssize_t proc_debug_control_write(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
#ifdef CONFIG_OPPO_TP_APK
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
#endif // end of CONFIG_OPPO_TP_APK

    int tmp = 0;
    char buffer[4] = {0};

    if (count > 2) {
        return count;
    }

    if (copy_from_user(buffer, buf, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }

    if (1 == sscanf(buffer, "%d", &tmp)) {
        tp_debug = tmp;
#ifdef CONFIG_OPPO_TP_APK
        if(ts && ts->apk_op && ts->apk_op->apk_debug_set) {
            mutex_lock(&ts->mutex);
            if (tp_debug == 0) {
                ts->apk_op->apk_debug_set(ts->chip_data, false);

            } else {
                ts->apk_op->apk_debug_set(ts->chip_data, true);
            }
            mutex_unlock(&ts->mutex);
        }
#endif // end of CONFIG_OPPO_TP_APK
    } else {
        TPD_DEBUG("invalid content: '%s', length = %zd\n", buf, count);
    }

    return count;
}

static const struct file_operations proc_debug_control_ops = {
    .write = proc_debug_control_write,
    .read  = proc_debug_control_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_limit_area_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    ssize_t ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return 0;
    TPD_DEBUG("limit_area is: %d\n", ts->edge_limit.limit_area);
    ret = snprintf(page, PAGESIZE - 1, "limit_area = %d left_x1 = %d right_x1 = %d left_x2 = %d right_x2 = %d left_x3 = %d right_x3 = %d left_y1 = %d right_y1 = %d left_y2 = %d right_y2 = %d left_y3 = %d right_y3 = %d\n",
                   ts->edge_limit.limit_area, ts->edge_limit.left_x1, ts->edge_limit.right_x1, ts->edge_limit.left_x2, ts->edge_limit.right_x2, ts->edge_limit.left_x3, ts->edge_limit.right_x3,
                   ts->edge_limit.left_y1, ts->edge_limit.right_y1, ts->edge_limit.left_y2, ts->edge_limit.right_y2, ts->edge_limit.left_y3, ts->edge_limit.right_y3);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

static ssize_t proc_limit_area_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    char buf[8];
    int temp;
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return count;

    if (count > 8) {
        return count;
    }

    if (copy_from_user(buf, buffer, count)) {
        TPD_DEBUG("%s: read proc input error.\n", __func__);
        return count;
    }

    sscanf(buf, "%d", &temp);
    if (temp < 0 || temp > 10)
        return count;

    ts->edge_limit.limit_area = temp;
    ts->edge_limit.left_x1    = (ts->edge_limit.limit_area * 1000) / 100;
    ts->edge_limit.right_x1   = ts->resolution_info.LCD_WIDTH - ts->edge_limit.left_x1;
    ts->edge_limit.left_x2    = 2 * ts->edge_limit.left_x1;
    ts->edge_limit.right_x2   = ts->resolution_info.LCD_WIDTH - (2 * ts->edge_limit.left_x1);
    ts->edge_limit.left_x3    = 5 * ts->edge_limit.left_x1;
    ts->edge_limit.right_x3   = ts->resolution_info.LCD_WIDTH - (5 * ts->edge_limit.left_x1);

    TPD_INFO("limit_area = %d; left_x1 = %d; right_x1 = %d; left_x2 = %d; right_x2 = %d; left_x3 = %d; right_x3 = %d\n",
             ts->edge_limit.limit_area, ts->edge_limit.left_x1, ts->edge_limit.right_x1, ts->edge_limit.left_x2, ts->edge_limit.right_x2, ts->edge_limit.left_x3, ts->edge_limit.right_x3);

    ts->edge_limit.left_y1    = (ts->edge_limit.limit_area * 1000) / 100;
    ts->edge_limit.right_y1   = ts->resolution_info.LCD_HEIGHT - ts->edge_limit.left_y1;
    ts->edge_limit.left_y2    = 2 * ts->edge_limit.left_y1;
    ts->edge_limit.right_y2   = ts->resolution_info.LCD_HEIGHT - (2 * ts->edge_limit.left_y1);
    ts->edge_limit.left_y3    = 5 * ts->edge_limit.left_y1;
    ts->edge_limit.right_y3   = ts->resolution_info.LCD_HEIGHT - (5 * ts->edge_limit.left_y1);

    TPD_INFO("limit_area = %d; left_y1 = %d; right_y1 = %d; left_y2 = %d; right_y2 = %d; left_y3 = %d; right_y3 = %d\n",
             ts->edge_limit.limit_area, ts->edge_limit.left_y1, ts->edge_limit.right_y1, ts->edge_limit.left_y2, ts->edge_limit.right_y2, ts->edge_limit.left_y3, ts->edge_limit.right_y3);

    return count;
}

static const struct file_operations proc_limit_area_ops = {
    .read  = proc_limit_area_read,
    .write = proc_limit_area_write,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_limit_control_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    ssize_t ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return 0;

    TPD_INFO("limit_enable is: 0x%x, ts->limit_edge = 0x%x, ts->limit_corner = 0x%x\n", ts->limit_enable, ts->limit_edge, ts->limit_corner);
    ret = snprintf(page, PAGESIZE - 1, "%d\n", ts->limit_enable);

    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

static ssize_t proc_limit_control_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    char buf[8] = {0};
    int ret = 0, temp = 0;
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return count;

    if (count > 3)
        count = 3;
    if (copy_from_user(buf, buffer, count)) {
        TPD_DEBUG("%s: read proc input error.\n", __func__);
        return count;
    }

    sscanf(buf, "%x", (uint32_t *)&temp);
    if (temp > 0x1F) {
        TPD_INFO("%s: temp = 0x%x > 0x1F \n", __func__, temp);
        return count;
    }

    mutex_lock(&ts->mutex);
    if (ts->default_hor_area && !ts->limit_valid && !(temp & 0x01)) {
        TPD_DETAIL("%s: return.\n", __func__);
        mutex_unlock(&ts->mutex);
        return count;
    }

    ts->limit_enable = temp;
    ts->limit_edge = ts->limit_enable & 1;
    //It works just when default_hor_area unconfiged
    if (!ts->default_hor_area) {
        ts->limit_corner = ts->limit_enable >> 1;
    }
    TPD_INFO("%s: limit_enable = 0x%x, ts->limit_edge = 0x%x, ts->limit_corner=0x%x\n", __func__, ts->limit_enable, ts->limit_edge, ts->limit_corner);

    if (ts->is_suspended == 0) {
        ret = ts->ts_ops->mode_switch(ts->chip_data, MODE_EDGE, ts->limit_edge);
        if (ret < 0) {
            TPD_INFO("%s, Touchpanel operate mode switch failed\n", __func__);
        }
    }
    mutex_unlock(&ts->mutex);

    return count;
}

static const struct file_operations proc_limit_control_ops = {
    .read  = proc_limit_control_read,
    .write = proc_limit_control_write,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_dir_control_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    ssize_t ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts || !ts->ts_ops->get_touch_direction)
        return 0;

    snprintf(page, PAGESIZE - 1, "%d\n", ts->ts_ops->get_touch_direction(ts->chip_data));
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

static ssize_t proc_dir_control_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    char buf[8] = {0};
    int temp = 0;
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return count;

    if (count > 2)
        return count;
    if (copy_from_user(buf, buffer, count)) {
        TPD_DEBUG("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &temp);

    mutex_lock(&ts->mutex);
    if (ts->default_hor_area && !ts->limit_valid && temp) {   //For compatible for old grip version, if configed and not in whitelist
        TPD_DETAIL("%s: return.\n", __func__);
        mutex_unlock(&ts->mutex);
        return count;
    }

    TPD_INFO("%s: value = %d\n", __func__, temp);
    if (ts->ts_ops->set_touch_direction) {
        ts->ts_ops->set_touch_direction(ts->chip_data, temp);
    }
    ts->monitor_data_v2.direction = temp;
#ifdef CONFIG_TOUCHPANEL_ALGORITHM
    set_algorithm_direction(ts,temp);
#endif

    //just for compatible for old grip version
    if (ts->default_hor_area) {             //if configed
        if (!temp) {        //in vertical mode
            ts->edge_limit.limit_area = 1;
            ts->limit_corner = 0;
        } else {            //in horizonal mode
            ts->edge_limit.limit_area = ts->default_hor_area;
            ts->limit_corner = (1 == temp) ? (1 << CORNER_TOPLEFT) : (1 << CORNER_BOTTOMRIGHT);
        }
        ts->edge_limit.left_x1    = (ts->edge_limit.limit_area * 1000) / 100;
        ts->edge_limit.right_x1   = ts->resolution_info.LCD_WIDTH - ts->edge_limit.left_x1;
        ts->edge_limit.left_x2    = 2 * ts->edge_limit.left_x1;
        ts->edge_limit.right_x2   = ts->resolution_info.LCD_WIDTH - (2 * ts->edge_limit.left_x1);
        ts->edge_limit.left_x3    = 5 * ts->edge_limit.left_x1;
        ts->edge_limit.right_x3   = ts->resolution_info.LCD_WIDTH - (5 * ts->edge_limit.left_x1);

        TPD_INFO("limit_area = %d; left_x1 = %d; right_x1 = %d; left_x2 = %d; right_x2 = %d; left_x3 = %d; right_x3 = %d\n",
                 ts->edge_limit.limit_area, ts->edge_limit.left_x1, ts->edge_limit.right_x1, ts->edge_limit.left_x2, ts->edge_limit.right_x2, ts->edge_limit.left_x3, ts->edge_limit.right_x3);

        ts->edge_limit.left_y1    = (ts->edge_limit.limit_area * 1000) / 100;
        ts->edge_limit.right_y1   = ts->resolution_info.LCD_HEIGHT - ts->edge_limit.left_y1;
        ts->edge_limit.left_y2    = 2 * ts->edge_limit.left_y1;
        ts->edge_limit.right_y2   = ts->resolution_info.LCD_HEIGHT - (2 * ts->edge_limit.left_y1);
        ts->edge_limit.left_y3    = 5 * ts->edge_limit.left_y1;
        ts->edge_limit.right_y3   = ts->resolution_info.LCD_HEIGHT - (5 * ts->edge_limit.left_y1);

        TPD_INFO("limit_area = %d; left_y1 = %d; right_y1 = %d; left_y2 = %d; right_y2 = %d; left_y3 = %d; right_y3 = %d\n",
                 ts->edge_limit.limit_area, ts->edge_limit.left_y1, ts->edge_limit.right_y1, ts->edge_limit.left_y2, ts->edge_limit.right_y2, ts->edge_limit.left_y3, ts->edge_limit.right_y3);
    }

    mutex_unlock(&ts->mutex);

    return count;
}

static const struct file_operations touch_dir_proc_fops = {
    .read  = proc_dir_control_read,
    .write = proc_dir_control_write,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_limit_valid_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    ssize_t ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    snprintf(page, PAGESIZE - 1, "%d\n", ts->limit_valid);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

static ssize_t proc_limit_valid_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    char buf[8] = {0};
    int temp = 0;
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return count;

    if (count > 2)
        return count;
    if (copy_from_user(buf, buffer, count)) {
        TPD_DEBUG("%s: read proc input error.\n", __func__);
        return count;
    }

    sscanf(buf, "%d", &temp);
    TPD_DETAIL("%s: value = %d\n", __func__, temp);

    mutex_lock(&ts->mutex);
    ts->limit_valid = temp;
    mutex_unlock(&ts->mutex);

    return count;
}

static const struct file_operations limit_valid_proc_fops = {
    .read  = proc_limit_valid_read,
    .write = proc_limit_valid_write,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_noise_modetest_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    ssize_t ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts || !ts->ts_ops->get_noise_modetest)
        return 0;

    snprintf(page, PAGESIZE - 1, "%d\n", ts->ts_ops->get_noise_modetest(ts->chip_data));
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

static ssize_t proc_noise_modetest_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    char buf[8] = {0};
    int temp = 0;
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return count;

    if (count > 2)
        return count;
    if (copy_from_user(buf, buffer, count)) {
        TPD_DEBUG("%s: read proc input error.\n", __func__);
        return count;
    }

    sscanf(buf, "%d", &temp);
    TPD_DETAIL("%s: value = %d\n", __func__, temp);

    mutex_lock(&ts->mutex);
    if (ts->ts_ops->set_noise_modetest) {
        ts->ts_ops->set_noise_modetest(ts->chip_data, temp);
    }
    mutex_unlock(&ts->mutex);

    return count;
}

static const struct file_operations proc_noise_modetest_fops = {
    .read  = proc_noise_modetest_read,
    .write = proc_noise_modetest_write,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_fw_update_write(struct file *file, const char __user *page, size_t size, loff_t *lo)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    int val = 0;
    int ret = 0;
    char buf[4] = {0};
    if (!ts)
        return size;
    if (size > 2)
        return size;

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    if (ts->boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT)
#else
    if (ts->boot_mode == MSM_BOOT_MODE__CHARGE)
#endif
    {
        TPD_INFO("boot mode is MSM_BOOT_MODE__CHARGE,not need update tp firmware\n");
        return size;
    }

    if (copy_from_user(buf, page, size)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return size;
    }

    sscanf(buf, "%d", &val);
    ts->firmware_update_type = val;
    if (!ts->force_update && ts->firmware_update_type != 2)
        ts->force_update = !!val;

    schedule_work(&ts->fw_update_work);

    ret = wait_for_completion_killable_timeout(&ts->fw_complete, FW_UPDATE_COMPLETE_TIMEOUT);
    if (ret < 0) {
        TPD_INFO("kill signal interrupt\n");
    }

#ifdef CONFIG_TOUCHIRQ_UPDATE_QOS
    if (!pm_qos_state) {
        pm_qos_add_request(&pm_qos_req, PM_QOS_CPU_DMA_LATENCY, pm_qos_value);
        TPD_INFO("add qos request in touch driver.\n");
        pm_qos_state = 1;
    }
#endif

    TPD_INFO("fw update finished\n");
    return size;
}

static const struct file_operations proc_fw_update_ops = {
    .write = proc_fw_update_write,
    .open = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_aging_test_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    uint8_t ret = 0;
    char page[PAGESIZE] = {0};
    if(!ts)
        return 0;
    if(ts->aging_test_ops){
        if((ts->aging_test_ops->start_aging_test)&&(ts->aging_test_ops->finish_aging_test))
            ret = 1;
    }
    if(!ret)
        TPD_INFO("%s:no support aging_test2\n", __func__);
    ret = snprintf(page, PAGESIZE - 1, "%d, %d\n", ret, ts->aging_test);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    return ret;
}

static ssize_t proc_aging_test_write(struct file *file, const char __user *page, size_t size, loff_t *lo)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    int val = 0;
    char buf[4] = {0};
    if (!ts)
        return size;
    if(!ts->aging_test_ops)
        return size;
    if((!ts->aging_test_ops->start_aging_test)||(!ts->aging_test_ops->finish_aging_test))
        return size;
    if (size > 2)
        return size;
    if (copy_from_user(buf, page, size)){
        TPD_INFO("%s:read proc input error.\n", __func__);
        return size;
    }
    sscanf(buf, "%d", &val);
    ts->aging_test = !!val;
    if (ts->aging_test)
        ts->aging_test_ops->start_aging_test(ts->chip_data);
    else
        ts->aging_test_ops->finish_aging_test(ts->chip_data);
    return size;

}

static const struct file_operations proc_aging_test_ops = {
    .write = proc_aging_test_write,
    .read  = proc_aging_test_read,
    .open = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_finger_protect_result_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    uint8_t ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    if(!ts)
        return 0;

    if(ts->spuri_fp_touch.fp_touch_st == FINGER_PROTECT_TOUCH_UP || ts->spuri_fp_touch.fp_touch_st == FINGER_PROTECT_TOUCH_DOWN)
        TPD_INFO("%s report_finger_protect = %d\n", __func__, ts->spuri_fp_touch.fp_touch_st);
    ret = snprintf(page, PAGESIZE - 1, "%d\n", ts->spuri_fp_touch.fp_touch_st);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    return ret;
}


static const struct file_operations proc_finger_protect_result = {
    .read  =  proc_finger_protect_result_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_finger_protect_trigger_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int op = 0;
    char buf[4] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > 2) {
        return count;
    }
    if (!ts) {
        return count;
    }

    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }

    if (1 == sscanf(buf, "%d", &op)) {
        if (op == 1) {
            ts->spuri_fp_touch.fp_trigger = true;
            ts->spuri_fp_touch.fp_touch_st = FINGER_PROTECT_NOTREADY;
            TPD_INFO("%s : %d\n", __func__, __LINE__);
            wake_up_interruptible(&waiter);
        }
    } else {
        TPD_INFO("invalid content: '%s', length = %zd\n", buffer, count);
        return -EINVAL;
    }

    return count;
}

static const struct file_operations proc_finger_protect_trigger = {
    .write = proc_finger_protect_trigger_write,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

void lcd_wakeup_finger_protect(bool wakeup)
{
    TPD_INFO("%s wakeup=%d\n", __func__, wakeup);
    if (g_tp != NULL) {
        if (g_tp->spuri_fp_touch.lcd_trigger_fp_check) {
            if(wakeup) {
                g_tp->spuri_fp_touch.lcd_resume_ok = true;
                wake_up_interruptible(&waiter);
            } else {
                mutex_lock(&g_tp->mutex);
                g_tp->spuri_fp_touch.lcd_resume_ok = false;
                mutex_unlock(&g_tp->mutex);
            }
        }
    }
}

static int finger_protect_handler(void *data)
{
    struct touchpanel_data *ts = (struct touchpanel_data *)data;
    if (!ts) {
        TPD_INFO("ts is null should nerver get here!\n");
        return 0;
    };
    if (!ts->ts_ops->spurious_fp_check) {
        TPD_INFO("not support spurious_fp_check call back\n");
        return 0;
    }

    do {
        if (ts->spuri_fp_touch.lcd_trigger_fp_check)
            wait_event_interruptible(waiter, ts->spuri_fp_touch.fp_trigger && ts->i2c_ready && ts->spuri_fp_touch.lcd_resume_ok);
        else
            wait_event_interruptible(waiter, ts->spuri_fp_touch.fp_trigger && ts->i2c_ready);
        ts->spuri_fp_touch.fp_trigger = false;
        ts->spuri_fp_touch.fp_touch_st = FINGER_PROTECT_NOTREADY;

        mutex_lock(&ts->mutex);
        if (g_tp->spuri_fp_touch.lcd_trigger_fp_check && !g_tp->spuri_fp_touch.lcd_resume_ok) {
            TPD_INFO("LCD is suspend, can not detect finger touch in incell panel\n");
            mutex_unlock(&ts->mutex);
            continue;
        }

        ts->spuri_fp_touch.fp_touch_st = ts->ts_ops->spurious_fp_check(ts->chip_data);
        if (ts->view_area_touched) {
            TPD_INFO("%s tp touch down,clear flag\n", __func__);
            ts->view_area_touched = 0;
        }
        operate_mode_switch(ts);
        mutex_unlock(&ts->mutex);
    } while (!kthread_should_stop());
    return 0;
}

//proc/touchpanel/oplus_register_info node use info:
//first choose register_add and lenght, example: echo 000e,1 > oplus_register_info
//second read: cat oplus_register_info
static ssize_t proc_register_info_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    int i = 0;
    ssize_t num_read_chars = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return 0;

    if (ts->reg_info.reg_length < 1 || ts->reg_info.reg_length > 9) {
        TPD_INFO("ts->reg_info.reg_length error!\n");
        return 0;
    }
    ts->reg_info.reg_result = kzalloc(ts->reg_info.reg_length * (sizeof(uint16_t)), GFP_KERNEL);
    if (!ts->reg_info.reg_result) {
        TPD_INFO("ts->reg_info.reg_result kzalloc error\n");
        return 0;
    }

    if (ts->ts_ops->register_info_read) {
        mutex_lock(&ts->mutex);
        ts->ts_ops->register_info_read(ts->chip_data, ts->reg_info.reg_addr, ts->reg_info.reg_result, ts->reg_info.reg_length);
        mutex_unlock(&ts->mutex);
        for(i = 0; i < ts->reg_info.reg_length; i++) {
            num_read_chars += snprintf(&(page[num_read_chars]), PAGESIZE - 1, "reg_addr(0x%x) = 0x%x\n", ts->reg_info.reg_addr, ts->reg_info.reg_result[i]);
        }
        ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    }

    kfree(ts->reg_info.reg_result);
    return ret;
}

//write info: echo 000e,1 > oplus_register_info
static ssize_t proc_register_info_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int addr = 0, length = 0;
    char buf[16] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > 7) {
        TPD_INFO("%s count = %ld\n", __func__, count);
        return count;
    }
    if (!ts) {
        TPD_INFO("ts not exist!\n");
        return count;
    }
    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }

    sscanf(buf, "%x,%d", (uint32_t *)&addr, &length);
    ts->reg_info.reg_addr = (uint16_t)addr;
    ts->reg_info.reg_length = (uint16_t)length;
    TPD_INFO("ts->reg_info.reg_addr = 0x%x, ts->reg_info.reg_lenght = %d\n", ts->reg_info.reg_addr, ts->reg_info.reg_length);

    return count;
}


static const struct file_operations proc_register_info_fops = {
    .owner = THIS_MODULE,
    .open  = simple_open,
    .read  = proc_register_info_read,
    .write = proc_register_info_write,
};

static ssize_t proc_incell_panel_info_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    uint8_t ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    snprintf(page, PAGESIZE - 1, "%d", ts->is_incell_panel);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

static const struct file_operations proc_incell_panel_fops = {
    .owner = THIS_MODULE,
    .open = simple_open,
    .read = proc_incell_panel_info_read,
};

static ssize_t proc_report_point_first_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return 0;

    if (!ts->ts_ops)
        return 0;
    if (!ts->ts_ops->get_report_point_first) {
        TPD_INFO("not support ts_ops->get_report_point_first callback\n");
        return 0;
    }

    mutex_lock(&ts->mutex);
    ret =  ts->ts_ops->get_report_point_first(ts->chip_data);
    mutex_unlock(&ts->mutex);
    snprintf(page, PAGESIZE - 1, "enable is %d, register is : %d\n",
             ts->report_point_first_enable, ret);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

static ssize_t proc_report_point_first_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int value = 0;
    char buf[4] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > 2)
        return count;

    if (!ts)
        return count;

    if (!ts->ts_ops)
        return count;
    if (!ts->ts_ops->set_report_point_first) {
        TPD_INFO("not support ts_ops->set_report_point_first callback\n");
        return count;
    }

    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &value);
    if (value > 2)
        return count;

    if (!ts->is_suspended && (ts->suspend_state == TP_SPEEDUP_RESUME_COMPLETE)) {
        mutex_lock(&ts->mutex);
        ts->report_point_first_enable = value;
        ts->ts_ops->set_report_point_first(ts->chip_data, value);
        mutex_unlock(&ts->mutex);
        TPD_INFO("%s: value = %d\n", __func__, value);
    }

    return count;
}

static const struct file_operations report_point_first_proc_fops = {
    .read  = proc_report_point_first_read,
    .write = proc_report_point_first_write,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_rate_white_list_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int value = 0 ;
    char buf[4] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > 4) {
        TPD_INFO("%s:count > 4\n", __func__);
        return count;
    }

    if (!ts) {
        TPD_INFO("%s: ts is NULL\n", __func__);
        return count;
    }

    if (!ts->ts_ops->rate_white_list_ctrl) {
        TPD_INFO("%s:not support ts_ops->rate_white_list_ctrl callback\n", __func__);
        return count;
    }

    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &value);

    mutex_lock(&ts->mutex);

    ts->rate_ctrl_level = value;

    TPD_INFO("%s: write value=%d\n", __func__, value);
    if (!ts->is_suspended) {

        ts->ts_ops->rate_white_list_ctrl(ts->chip_data, value);

    } else {
        TPD_INFO("%s: TP is_suspended.\n", __func__);
    }
    mutex_unlock(&ts->mutex);

    return count;
}

static ssize_t proc_rate_white_list_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts) {
        snprintf(page, PAGESIZE - 1, "%d\n", -1); //no support
    } else {
        snprintf(page, PAGESIZE - 1, "%d\n", ts->rate_ctrl_level); //support
    }
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    return ret;
}

static const struct file_operations proc_rate_white_list_fops = {
    .write = proc_rate_white_list_write,
    .read  = proc_rate_white_list_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_switch_usb_state_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int usb_state = 0 ;
    char buf[4] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > 4) {
        TPD_INFO("%s:count > 4\n", __func__);
        return count;
    }

    if (!ts) {
        TPD_INFO("%s: ts is NULL\n", __func__);
        return count;
    }


    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &usb_state);
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    if ((ts->boot_mode == META_BOOT || ts->boot_mode == FACTORY_BOOT))
#else
    if ((ts->boot_mode == MSM_BOOT_MODE__FACTORY || ts->boot_mode == MSM_BOOT_MODE__RF || ts->boot_mode == MSM_BOOT_MODE__WLAN))
#endif
    {
        TPD_INFO("Ftm mode, do not switch usb state\n");
        return count;
    }
    if (ts->is_usb_checked != usb_state) {
        ts->is_usb_checked = !!usb_state;
        mutex_lock(&ts->mutex);
#ifdef CONFIG_TOUCHPANEL_ALGORITHM
        switch_kalman_fun(ts, ts->noise_level > 0);
#endif
        TPD_INFO("%s: check usb state : %d, is_suspended: %d\n", __func__, usb_state, ts->is_suspended);
        if (!ts->is_suspended && (ts->suspend_state == TP_SPEEDUP_RESUME_COMPLETE)
            && !ts->loading_fw) {
            ts->ts_ops->mode_switch(ts->chip_data, MODE_CHARGE, ts->is_usb_checked);

            if (ts->smooth_level_array_support && ts->smooth_level_charging_array_support && ts->ts_ops->smooth_lv_set && ts->smooth_level_chosen) {
                if (ts->is_usb_checked) {
                    ts->ts_ops->smooth_lv_set(ts->chip_data, ts->smooth_level_charging_array[ts->smooth_level_chosen]);
                } else {
                    ts->ts_ops->smooth_lv_set(ts->chip_data, ts->smooth_level_array[ts->smooth_level_chosen]);
                }
            }
        }
        mutex_unlock(&ts->mutex);
    }
    return count;
}

static ssize_t proc_switch_usb_state_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts) {
        snprintf(page, PAGESIZE - 1, "%d\n", -1); //no support
    } else {
        snprintf(page, PAGESIZE - 1, "%d\n", ts->is_usb_checked); //support
    }
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    return ret;
}

static const struct file_operations proc_switch_usb_state_fops = {
    .write = proc_switch_usb_state_write,
    .read  = proc_switch_usb_state_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_wireless_charge_detect_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int wireless_state = 0 ;
    char buf[4] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > 4) {
        TPD_INFO("%s:count > 4\n", __func__);
        return count;
    }

    if (!ts) {
        TPD_INFO("%s: ts is NULL\n", __func__);
        return count;
    }


    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &wireless_state);
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    if ((ts->boot_mode == META_BOOT || ts->boot_mode == FACTORY_BOOT))
#else
    if ((ts->boot_mode == MSM_BOOT_MODE__FACTORY || ts->boot_mode == MSM_BOOT_MODE__RF || ts->boot_mode == MSM_BOOT_MODE__WLAN))
#endif
    {
        TPD_INFO("Ftm mode, do not switch wireless state\n");
        return count;
    }

    if (ts->is_wireless_checked != wireless_state) {
        ts->is_wireless_checked = !!wireless_state;
        TPD_INFO("%s: check wireless state : %d, is_suspended: %d\n", __func__, wireless_state, ts->is_suspended);
        mutex_lock(&ts->mutex);
        if (!ts->is_suspended && (ts->suspend_state == TP_SPEEDUP_RESUME_COMPLETE)
            && !ts->loading_fw) {
            ts->ts_ops->mode_switch(ts->chip_data, MODE_WIRELESS_CHARGE, ts->is_wireless_checked);

        }
        mutex_unlock(&ts->mutex);
    }
    return count;
}

static ssize_t proc_wireless_charge_detect_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts) {
        snprintf(page, PAGESIZE - 1, "%d\n", -1); //no support
    } else {
        snprintf(page, PAGESIZE - 1, "%d\n", ts->is_wireless_checked); //support
    }
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    return ret;
}


static const struct file_operations proc_wireless_charge_detect_fops = {
    .write = proc_wireless_charge_detect_write,
    .read  = proc_wireless_charge_detect_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_smooth_level_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int value = 0, raw_level = 0;
    char buf[5] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > 5) {
        TPD_INFO("%s:count > 5\n", __func__);
        return count;
    }

    if (!ts) {
        TPD_INFO("%s: ts is NULL\n", __func__);
        return count;
    }

    if (!ts->ts_ops->smooth_lv_set) {
        TPD_INFO("%s:not support ts_ops->smooth_lv_set callback\n", __func__);
        return count;
    }

    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &value);

    mutex_lock(&ts->mutex);
    if (ts->smooth_level_support) {
        ts->smooth_level = value;
        raw_level = value;
        TPD_INFO("%s: write value=%d\n", __func__, value);
    } else if (ts->smooth_level_array_support) {
        if (value < 0) {
            raw_level = -value;
        } else {
            if (value < SMOOTH_LEVEL_NUM) {
                ts->smooth_level_chosen = value;
            } else {
                ts->smooth_level_chosen = SMOOTH_LEVEL_NUM - 1;
            }
            if (ts->health_monitor_v2_support && ts->smooth_level_chosen) {
                ts->monitor_data_v2.smooth_level_chosen = ts->smooth_level_chosen;
            }
            if (ts->smooth_level_charging_array_support && ts->charger_pump_support && ts->is_usb_checked) {
                raw_level = ts->smooth_level_charging_array[ts->smooth_level_chosen];
            } else {
                raw_level = ts->smooth_level_array[ts->smooth_level_chosen];
            }
        }

        TPD_INFO("%s: level=%d value=%d\n", __func__, ts->smooth_level_chosen, raw_level);
    }
    if (!ts->is_suspended) {

        ts->ts_ops->smooth_lv_set(ts->chip_data, raw_level);

    } else {
        TPD_INFO("%s: TP is_suspended.\n", __func__);
    }
#ifdef CONFIG_TOUCHPANEL_ALGORITHM
        switch_kalman_fun(ts, 0);
#endif
    mutex_unlock(&ts->mutex);

    return count;
}

static ssize_t proc_smooth_level_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (ts && ts->smooth_level_support) {
        snprintf(page, PAGESIZE - 1, "%d\n", ts->smooth_level); //support
    } else if (ts && ts->smooth_level_array_support) {
        snprintf(page, PAGESIZE - 1, "%d\n", ts->smooth_level_chosen); //support
    } else {
        snprintf(page, PAGESIZE - 1, "%d\n", -1); //no support
    }
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    return ret;
}

static const struct file_operations proc_smooth_level_fops = {
    .write = proc_smooth_level_write,
    .read  = proc_smooth_level_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_sensitive_level_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int value = 0, raw_level = 0;
    char buf[5] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > 5) {
        TPD_INFO("%s:count > 5\n", __func__);
        return count;
    }

    if (!ts) {
        TPD_INFO("%s: ts is NULL\n", __func__);
        return count;
    }

    if (!ts->ts_ops->sensitive_lv_set) {
        TPD_INFO("%s:not support ts_ops->sensitive_lv_set callback\n", __func__);
        return count;
    }

    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &value);

    mutex_lock(&ts->mutex);
    if (value < 0) {
        raw_level = -value;
    } else {
        if (value < SENSITIVE_LEVEL_NUM) {
            ts->sensitive_level_chosen = value;
        } else {
            ts->sensitive_level_chosen = SENSITIVE_LEVEL_NUM - 1;
        }
        if (ts->health_monitor_v2_support && ts->sensitive_level_chosen) {
            ts->monitor_data_v2.sensitive_level_chosen = ts->sensitive_level_chosen;
        }
        raw_level = ts->sensitive_level_array[ts->sensitive_level_chosen];
    }

    TPD_INFO("%s: level=%d value=%d\n", __func__, ts->sensitive_level_chosen, raw_level);
    if (!ts->is_suspended) {
        ts->ts_ops->sensitive_lv_set(ts->chip_data, raw_level);
    } else {
        TPD_INFO("%s: TP is_suspended.\n", __func__);
    }
    mutex_unlock(&ts->mutex);

    return count;
}

static ssize_t proc_sensitive_level_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts || !ts->sensitive_level_array_support) {
        snprintf(page, PAGESIZE - 1, "%d\n", -1); //no support
    } else {
        snprintf(page, PAGESIZE - 1, "%d\n", ts->sensitive_level_chosen); //support
    }
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    return ret;
}

static const struct file_operations proc_sensitive_level_fops = {
    .write = proc_sensitive_level_write,
    .read  = proc_sensitive_level_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_optimized_time_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int value = 0;
    char buf[5] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if ((count > 5) || !ts) {
        TPD_INFO("%s error:count:%d.\n", __func__, count);
        return count;
    }

    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &value);

    if (0 == value) {
        ts->total_operate_times = 0;        //clear total count
    }

    return count;
}

static ssize_t proc_optimized_time_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts) {
        snprintf(page, PAGESIZE - 1, "%d\n", -1); //error handler
    } else {
        snprintf(page, PAGESIZE - 1, "%d\n", ts->single_optimized_time * ts->total_operate_times);
    }
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    return ret;
}

static const struct file_operations proc_optimized_time_fops = {
    .write = proc_optimized_time_write,
    .read  = proc_optimized_time_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static int calibrate_fops_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;

    if (!ts->ts_ops->calibrate) {
        TPD_INFO("[TP]no calibration, need back virtual calibration result");
        seq_printf(s, "0 error, calibration and verify success\n");
        return 0;
    }

    disable_irq_nosync(ts->irq);
    mutex_lock(&ts->mutex);
    if (!ts->touch_count) {
        ts->ts_ops->calibrate(s, ts->chip_data);
    } else {
        seq_printf(s, "1 error, release touch on the screen, now has %d touch\n", ts->touch_count);
    }
    mutex_unlock(&ts->mutex);
    enable_irq(ts->irq);

    return 0;
}

static int proc_calibrate_fops_open(struct inode *inode, struct file *file)
{
    return single_open(file, calibrate_fops_read_func, PDE_DATA(inode));
}

static const struct file_operations proc_calibrate_fops = {
    .owner = THIS_MODULE,
    .open  = proc_calibrate_fops_open,
    .read  = seq_read,
    .release = single_release,
};

static int cal_status_read_func(struct seq_file *s, void *v)
{
    bool cal_needed = false;
    struct touchpanel_data *ts = s->private;

    if (!ts->ts_ops->get_cal_status) {
        TPD_INFO("[TP]no calibration status, need back virtual calibration status");
        seq_printf(s, "0 error, calibration data is ok\n");
        return 0;
    }

    mutex_lock(&ts->mutex);
    cal_needed = ts->ts_ops->get_cal_status(s, ts->chip_data);
    if (cal_needed) {
        seq_printf(s, "1 error, need do calibration\n");
    } else {
        seq_printf(s, "0 error, calibration data is ok\n");
    }
    mutex_unlock(&ts->mutex);

    return 0;
}

static int proc_cal_status_fops_open(struct inode *inode, struct file *file)
{
    return single_open(file, cal_status_read_func, PDE_DATA(inode));
}

static const struct file_operations proc_cal_status_fops = {
    .owner = THIS_MODULE,
    .open  = proc_cal_status_fops_open,
    .read  = seq_read,
    .release = single_release,
};

static ssize_t proc_curved_size_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts) {
        snprintf(page, PAGESIZE - 1, "%d\n", 0); //no support
    } else {
        snprintf(page, PAGESIZE - 1, "%d\n", ts->curved_size); //support
    }
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    return ret;
}

static const struct file_operations proc_curved_size_fops = {
    .read  = proc_curved_size_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

//proc/touchpanel/kernel_grip_handle
static ssize_t proc_kernel_grip_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int value = -1;
    char buf[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > PAGESIZE) {
        TPD_INFO("%s:count > PAGESIZE\n", __func__);
        return count;
    }

    if (!ts) {
        TPD_INFO("%s: ts is NULL\n", __func__);
        return count;
    }

    if (!ts->ts_ops)
        return count;

    if (!ts->ts_ops->tp_set_grip_level) {
        TPD_INFO("not support tp_set_grip_level callback\n");
        return count;
    }

    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }

    if ((value = touch_get_key_value(buf, "grip_disable_level"))  >= 0) {
        ts->grip_level |= 1 << value;
        TPD_INFO("change grip_disable_level to %d.\n", value);
    } else if ((value = touch_get_key_value(buf, "grip_enable_level"))  >= 0) {
        ts->grip_level &= ~(1 << value);
        TPD_INFO("change grip_enable_level to %d.\n", value);
    } else {
        return count;
    }

    TPD_INFO("%s: grip_level:=0x%x\n", __func__, ts->grip_level);

    if (!ts->is_suspended) {
        mutex_lock(&ts->mutex);
        ts->ts_ops->tp_set_grip_level(ts->chip_data, ts->grip_level);
        mutex_unlock(&ts->mutex);
    } else {
        TPD_INFO("%s: grip_level is_suspended.\n", __func__);
    }

    return count;
}

static ssize_t proc_kernel_grip_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts) {
        snprintf(page, PAGESIZE - 1, "%d\n", -1); //no support
    } else {
        snprintf(page, PAGESIZE - 1, "%d\n", ts->grip_level); //support
    }
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    return ret;
}

static const struct file_operations proc_kernel_grip_fops = {
    .write = proc_kernel_grip_write,
    .read  = proc_kernel_grip_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};


//proc/touchpanel/kernel_grip_default_para
static int tp_grip_default_para_read(struct seq_file *s, void *v)
{
    int ret = 0;
    struct touchpanel_data *ts = s->private;
    const struct firmware *fw = NULL;

    char *p_node = NULL;
    char *grip_config_name = NULL;
    char *postfix = "_sys_edge_touch_config";
    uint8_t copy_len = 0;

    TPD_INFO("%s:s->size:%d,s->count:%d\n", __func__, s->size,
        s->count);

    if (!ts)
        return 0;

    grip_config_name = kzalloc(MAX_FW_NAME_LENGTH, GFP_KERNEL);
    if(grip_config_name == NULL) {
        TPD_INFO("grip_config_name kzalloc error!\n");
        return 0;
    }
    p_node = strstr(ts->panel_data.fw_name, ".");
    if(p_node == NULL) {
        TPD_INFO("p_node strstr error!\n");
        kfree(grip_config_name);
        return 0;
    }
    copy_len = p_node - ts->panel_data.fw_name;
    memcpy(grip_config_name, ts->panel_data.fw_name, copy_len);
    strlcat(grip_config_name, postfix, MAX_FW_NAME_LENGTH);
    strlcat(grip_config_name, p_node, MAX_FW_NAME_LENGTH);
    TPD_INFO("grip_config_name is %s\n", grip_config_name);

    ret = request_firmware(&fw, grip_config_name, ts->dev);
    if (ret < 0) {
        TPD_INFO("Request firmware failed - %s (%d)\n", grip_config_name, ret);
        seq_printf(s, "Request failed, Check the path %s\n",grip_config_name);
        kfree(grip_config_name);
        return 0;
    }
    TPD_INFO("%s Request ok,size is:%d\n", grip_config_name, fw->size);

    //mutex_lock(&ts->mutex);
    /*the result data is big than one page, so do twice.*/
    if (s->size <= (fw->size)) {
        s->count = s->size;
        //mutex_unlock(&ts->mutex);
        goto EXIT;
    }

    if (fw) {
        if (fw->size) {
            seq_write(s, fw->data, fw->size);
            TPD_INFO("%s:seq_write data ok\n", __func__);
        }
    }

EXIT:
    release_firmware(fw);
    kfree(grip_config_name);
    return ret;
}

static int tp_grip_default_para_open(struct inode *inode, struct file *file)
{
    return single_open(file, tp_grip_default_para_read, PDE_DATA(inode));
}

static const struct file_operations tp_grip_default_para_fops = {
    .owner = THIS_MODULE,
    .open  = tp_grip_default_para_open,
    .read  = seq_read,
    .release = single_release,
};


#ifdef CONFIG_OPPO_TP_APK
void log_buf_write(struct touchpanel_data *ts, u8 value)
{
    if (ts->log_buf) {
        u16 *head;
        head = (u16 *)(&ts->log_buf[0]);
        if ((*head) == 0) {
            (*head) = 1;
        }
        if ((*head) < 1023) {
            (*head)++;
        } else {
            (*head) = 2;
        }

        ts->log_buf[*head] = value;
    }
}

static int log_buf_read(struct touchpanel_data *ts, char *buf, int len)
{
    if (ts->log_buf == NULL) {
        return 0;
    }
    if (len > 1024) {
        len = 1024;
    }
    memcpy((u8 *)buf, ts->log_buf, len);
    return len;
}


static char apk_charger_sta_read(struct touchpanel_data *ts)
{
    if (ts->apk_op->apk_charger_get) {
        if (ts->apk_op->apk_charger_get(ts->chip_data)) {
            return '1';
        }
        return '0';

    }

    return '-';
}

static int apk_data_read(struct touchpanel_data *ts, char *buf, int len)
{
    switch (ts->data_now) {
    case BASE_DATA:
        if (ts->apk_op->apk_basedata_get) {
            return ts->apk_op->apk_basedata_get(ts->chip_data, buf, len);
        }
        break;
    case DIFF_DATA:
        if (ts->apk_op->apk_diffdata_get) {
            return ts->apk_op->apk_diffdata_get(ts->chip_data, buf, len);
        }
        break;
    case DEBUG_INFO:
        return log_buf_read(ts, buf, len);
        break;
    case RAW_DATA:
        if (ts->apk_op->apk_rawdata_get) {
            return ts->apk_op->apk_rawdata_get(ts->chip_data, buf, len);
        }
        break;
    case BACK_DATA:
        if (ts->apk_op->apk_backdata_get) {
            return ts->apk_op->apk_backdata_get(ts->chip_data, buf, len);
        }
        break;
    default:
        break;
    }
    buf[0] = '-';
    return 1;
}

static char apk_earphone_sta_read(struct touchpanel_data *ts)
{
    if (ts->apk_op->apk_earphone_get) {
        if (ts->apk_op->apk_earphone_get(ts->chip_data)) {
            return '1';
        }
        return '0';

    }

    return '-';
}

static int apk_gesture_read(struct touchpanel_data *ts, char *buf, int len)
{
    if (ts->apk_op->apk_gesture_get) {
        if (ts->apk_op->apk_gesture_get(ts->chip_data)) {
            return ts->apk_op->apk_gesture_info(ts->chip_data, buf, len);
        }
        buf[0] = '0';
        return 1;
    }

    buf[0] = '-';
    return 1;
}

static int apk_info_read(struct touchpanel_data *ts, char *buf, int len)
{
    if (ts->apk_op->apk_tp_info_get) {
        return ts->apk_op->apk_tp_info_get(ts->chip_data, buf, len);
    }
    buf[0] = '-';
    return 1;
}

static char apk_noise_read(struct touchpanel_data *ts)
{
    if (ts->apk_op->apk_noise_get) {
        if (ts->apk_op->apk_noise_get(ts->chip_data)) {
            return '1';
        }
        return '0';

    }

    return '-';
}

static char apk_water_read(struct touchpanel_data *ts)
{
    if (ts->apk_op->apk_water_get) {
        if (ts->apk_op->apk_water_get(ts->chip_data) == 1) {
            return '1';
        }
        if (ts->apk_op->apk_water_get(ts->chip_data) == 2) {
            return '2';
        }
        return '0';

    }

    return '-';
}


static char apk_proximity_read(struct touchpanel_data *ts)
{

    if (ts->apk_op->apk_proximity_dis) {
        int dis;
        dis = ts->apk_op->apk_proximity_dis(ts->chip_data);
        if (dis > 0) {
            if (dis == 1) {
                return '1';
            }
            return '2';
        }
        return '0';

    }

    return '-';
}

static char apk_debug_sta(struct touchpanel_data *ts)
{
    if (ts->apk_op->apk_debug_get) {
        if (ts->apk_op->apk_debug_get(ts->chip_data)) {
            return '1';
        }
        return '0';

    }

    return '-';

}

static char apk_game_read(struct touchpanel_data *ts)
{
    if (ts->apk_op->apk_game_get) {
        if (ts->apk_op->apk_game_get(ts->chip_data)) {
            return '1';
        }
        return '0';

    }

    return '-';
}

static ssize_t touch_apk_read(struct file *file,
                             char __user *user_buf,
                             size_t count,
                             loff_t *ppos)
{
    char *buf;
    int len = 0;
    int ret = 0;
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    if (!ts) {
        TPD_INFO("ts not exist!\n");
        return -ENODEV;
    }

    if (ts->apk_op == NULL) {
        TPD_INFO("ts apk_op not exist!\n");
        return -ENODEV;
    }

    if(*ppos != 0 || count < 1) {
        return 0;
    }
    TPD_INFO("apk read is %c, count is %d.\n", (char)ts->type_now, (int)count);
    buf = kzalloc(count, GFP_KERNEL);
    if (IS_ERR(buf) || buf == NULL) {
        ret = -EFAULT;
        goto read_exit;
    }
    mutex_lock(&ts->mutex);
    switch (ts->type_now) {
    case APK_CHARGER:
        buf[0] = apk_charger_sta_read(ts);
        len = 1;
        break;
    case APK_DATA:
        len = apk_data_read(ts, buf, count);
        break;
    case APK_EARPHONE:
        buf[0] = apk_earphone_sta_read(ts);
        len = 1;
        break;
    case APK_GESTURE:
        len = apk_gesture_read(ts, buf, count);
        break;
    case APK_INFO:
        len = apk_info_read(ts, buf, count);
        break;
    case APK_NOISE:
        buf[0] = apk_noise_read(ts);
        len = 1;
        break;
    case APK_PROXIMITY:
        buf[0] = apk_proximity_read(ts);
        len = 1;
        break;
    case APK_WATER:
        buf[0] = apk_water_read(ts);
        len = 1;
        break;
    case APK_DEBUG_MODE:
        buf[0] = apk_debug_sta(ts);
        len = 1;
        break;
    case APK_GAME_MODE:
        buf[0] = apk_game_read(ts);
        len = 1;
        break;
    default:
        break;
    }

    if (len == 1 && len < count) {
        buf[len] = '\n';
        len++;
    }

    mutex_unlock(&ts->mutex);
    if (copy_to_user(user_buf, buf, len)) {
        TPD_INFO("%s: can not copy the buf.\n", __func__);
        ret = -EFAULT;
        goto read_exit;
    }
    ret = len;
    *ppos += ret;

read_exit:
    if (buf != NULL) {
        kfree(buf);
    }
    return ret;
}



static void apk_charger_switch(struct touchpanel_data *ts, char on_off)
{
    if (ts->apk_op->apk_charger_set) {
        if (on_off == '1') {
            ts->apk_op->apk_charger_set(ts->chip_data, true);
        } else if (on_off == '0') {
            ts->apk_op->apk_charger_set(ts->chip_data, false);
        }
    }
}

static void apk_earphone_switch(struct touchpanel_data *ts, char on_off)
{
    if (ts->apk_op->apk_earphone_set) {
        if (on_off == '1') {
            ts->apk_op->apk_earphone_set(ts->chip_data, true);
        } else if (on_off == '0') {
            ts->apk_op->apk_earphone_set(ts->chip_data, false);
        }
    }
}

static void apk_gesture_debug(struct touchpanel_data *ts, char on_off)
{
    if (ts->apk_op->apk_gesture_debug) {
        if (on_off == '1') {

            if (ts->gesture_buf == NULL) {
                ts->gesture_buf = kzalloc(1024, GFP_KERNEL);
            }
            if (ts->gesture_buf) {
                ts->gesture_debug_sta = true;
                ts->apk_op->apk_gesture_debug(ts->chip_data, true);
            } else {
                ts->gesture_debug_sta = false;
                ts->apk_op->apk_gesture_debug(ts->chip_data, false);
            }
        } else if (on_off == '0') {
            ts->apk_op->apk_gesture_debug(ts->chip_data, false);
            if (ts->gesture_buf) {
                kfree(ts->gesture_buf);
                ts->gesture_buf = NULL;

            }
            ts->gesture_debug_sta = false;
        }
    }
}

static void apk_noise_switch(struct touchpanel_data *ts, char on_off)
{
    if (ts->apk_op->apk_noise_set) {
        if (on_off == '1') {
            ts->apk_op->apk_noise_set(ts->chip_data, true);
        } else if (on_off == '0') {
            ts->apk_op->apk_noise_set(ts->chip_data, false);
        }
    }
}

static void apk_water_switch(struct touchpanel_data *ts, char on_off)
{
    if (ts->apk_op->apk_water_set) {
        if (on_off == '1') {
            ts->apk_op->apk_water_set(ts->chip_data, 1);
        } else if (on_off == '2') {
            ts->apk_op->apk_water_set(ts->chip_data, 2);
        } else if (on_off == '0') {
            ts->apk_op->apk_water_set(ts->chip_data, 0);
        }
    }
}

static void apk_proximity_switch(struct touchpanel_data *ts,
                                 char on_off)
{
    if (ts->apk_op->apk_proximity_set) {
        if (on_off == '1') {
            ts->apk_op->apk_proximity_set(ts->chip_data, true);
        } else if (on_off == '0') {
            ts->apk_op->apk_proximity_set(ts->chip_data, false);
        }
    }
}

static void apk_debug_switch(struct touchpanel_data *ts, char on_off)
{
    if (ts->apk_op->apk_debug_set) {
        if (on_off == '1') {
            ts->apk_op->apk_debug_set(ts->chip_data, true);
            if (ts->log_buf == NULL) {
                ts->log_buf = kzalloc(1024, GFP_KERNEL);
            }
        } else if (on_off == '0') {
            ts->apk_op->apk_debug_set(ts->chip_data, false);
            if (ts->log_buf) {
                kfree(ts->log_buf);
                ts->log_buf = NULL;
            }
        }
    }

}

static void apk_data_type_set(struct touchpanel_data *ts, char ch)
{
    APK_DATA_TYPE type;
    type = (APK_DATA_TYPE)ch;
    switch (type) {
    case BASE_DATA:
    case DIFF_DATA:
    case DEBUG_INFO:
    case RAW_DATA:
    case BACK_DATA:
        ts->data_now = type;
        if (ts->apk_op->apk_data_type_set) {
            ts->apk_op->apk_data_type_set(ts->chip_data, ts->data_now);
        }
        break;
    default:
        break;
    }
}

static void apk_game_switch(struct touchpanel_data *ts, char on_off)
{
    if (ts->apk_op->apk_game_set) {
        if (on_off == '1') {
            ts->apk_op->apk_game_set(ts->chip_data, true);
        } else if (on_off == '0') {
            ts->apk_op->apk_game_set(ts->chip_data, false);
        }
    }

}

static ssize_t touch_apk_write(struct file *file,
                              const char __user *buffer,
                              size_t count,
                              loff_t *ppos)
{
    char *buf;
    APK_SWITCH_TYPE type;
    int ret = count;
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    if (!ts) {
        TPD_INFO("ts not exist!\n");
        return -ENODEV;
    }

    if (ts->apk_op == NULL) {
        TPD_INFO("ts apk_op not exist!\n");
        return -ENODEV;
    }

    if (count < 1) {
        return 0;
    }


    buf = kzalloc(count, GFP_KERNEL);

    if (IS_ERR(buf) || buf == NULL) {
        ret = -EFAULT;
        goto write_exit;
    }

    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: can not copy the buf.\n", __func__);
        ret = -EFAULT;
        goto write_exit;
    }
    mutex_lock(&ts->mutex);

    type = (APK_SWITCH_TYPE)buf[0];
    TPD_INFO("apk write type is %c, count is %d.\n", (char)type,
             (int)count);
    if (count > 1) {
        switch (type) {
        case APK_CHARGER:
            apk_charger_switch(ts, buf[1]);
            break;
        case APK_DATA:
            apk_data_type_set(ts, buf[1]);
            break;
        case APK_EARPHONE:
            apk_earphone_switch(ts, buf[1]);
            break;
        case APK_GESTURE:
            apk_gesture_debug(ts, buf[1]);
            break;
        case APK_INFO: // read only, do nothing in write.
            break;
        case APK_NOISE:
            apk_noise_switch(ts, buf[1]);
            break;
        case APK_PROXIMITY:
            apk_proximity_switch(ts, buf[1]);
            break;
        case APK_WATER:
            apk_water_switch(ts, buf[1]);
            break;
        case APK_DEBUG_MODE:
            apk_debug_switch(ts, buf[1]);
            break;
        case APK_GAME_MODE:
            apk_game_switch(ts, buf[1]);
            break;
        default:
            type = APK_NULL;
            break;
        }
    }
    ts->type_now = type;
    mutex_unlock(&ts->mutex);

write_exit:
    if (buf != NULL) {
        kfree(buf);
    }

    return ret;
}


static const struct file_operations proc_touch_apk_fops = {
    .owner = THIS_MODULE,
    .open = simple_open,
    .read = touch_apk_read,
    .write = touch_apk_write,
};

#endif //end of CONFIG_OPPO_TP_APK

/**
 * init_touchpanel_proc - Using for create proc interface
 * @ts: touchpanel_data struct using for common driver
 *
 * we need to set touchpanel_data struct as private_data to those file_inode
 * Returning zero(success) or negative errno(failed)
 */
static int init_touchpanel_proc(struct touchpanel_data *ts)
{
    int ret = 0;
    struct proc_dir_entry *prEntry_tp = NULL;
    struct proc_dir_entry *prEntry_tmp = NULL;

    TPD_INFO("%s entry\n", __func__);

    //proc files-step1:/proc/devinfo/tp  (touchpanel device info)
    if(ts->fw_update_app_support) {
        register_devinfo("tp", &ts->panel_data.manufacture_info);
    } else {
        TPD_INFO("register_devinfo not defined!\n");
        register_device_proc("tp", ts->panel_data.manufacture_info.version, ts->panel_data.manufacture_info.manufacture);
    }

    //proc files-step2:/proc/touchpanel
    prEntry_tp = proc_mkdir("touchpanel", NULL);
    if (prEntry_tp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create TP proc entry\n", __func__);
    }

    //proc files-step2-1:/proc/touchpanel/tp_debug_log_level (log control interface)
    prEntry_tmp = proc_create_data("debug_level", 0644, prEntry_tp, &proc_debug_control_ops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    //proc files-step2-2:/proc/touchpanel/oppo_tp_fw_update (FW update interface)
    prEntry_tmp = proc_create_data("tp_fw_update", 0666, prEntry_tp, &proc_fw_update_ops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }
    prEntry_tmp = proc_create_data("tp_aging_test", 0666,prEntry_tp, &proc_aging_test_ops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't creat proc entry, %d\n", __func__, __LINE__);
    }
    //proc files-step2-3:/proc/touchpanel/oppo_tp_fw_update (edge limit control interface)
    if (ts->edge_limit_support || ts->fw_edge_limit_support) {
        prEntry_tmp = proc_create_data("oplus_tp_limit_enable", 0664, prEntry_tp, &proc_limit_control_ops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }

        prEntry_tmp = proc_create_data("oplus_tp_direction", 0666, prEntry_tp, &touch_dir_proc_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }

        prEntry_tmp = proc_create_data("oplus_tp_limit_whitelist", 0666, prEntry_tp, &limit_valid_proc_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }

        if (ts->edge_limit_support) {
            prEntry_tmp = proc_create_data("oppo_tp_limit_area", 0664, prEntry_tp, &proc_limit_area_ops, ts);
            if (prEntry_tmp == NULL) {
                ret = -ENOMEM;
                TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
            }
        }
    }

    //proc files-step2-4:/proc/touchpanel/double_tap_enable (black gesture related interface)
    if (ts->black_gesture_support) {
        prEntry_tmp = proc_create_data("double_tap_enable", 0666, prEntry_tp, &proc_gesture_control_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
        prEntry_tmp = proc_create_data("coordinate", 0444, prEntry_tp, &proc_coordinate_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }

    //proc files-step2-5:/proc/touchpanel/glove_mode_enable (Glove mode related interface)
    if (ts->glove_mode_support) {
        prEntry_tmp = proc_create_data("glove_mode_enable", 0666, prEntry_tp, &proc_glove_control_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }

    //proc files-step2-6:/proc/touchpanel/finger_protect_result
    if (ts->spurious_fp_support) {
        prEntry_tmp = proc_create_data("finger_protect_result", 0666, prEntry_tp, &proc_finger_protect_result, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
        prEntry_tmp = proc_create_data("finger_protect_trigger", 0666, prEntry_tp, &proc_finger_protect_trigger, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }

    //proc files-step2-7:/proc/touchpanel/oplus_register_info
    prEntry_tmp = proc_create_data("oplus_register_info", 0664, prEntry_tp, &proc_register_info_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    prEntry_tmp = proc_create_data("ps_status", 0666, prEntry_tp, &proc_write_ps_status_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }


    //proc files-step2-8:/proc/touchpanel/incell_panel
    if (ts->is_incell_panel) {
        prEntry_tmp = proc_create_data("incell_panel", 0664, prEntry_tp, &proc_incell_panel_fops, ts);
    }

    if (ts->gesture_test_support) {
        prEntry_tmp = proc_create_data("black_screen_test", 0666, prEntry_tp, &proc_black_screen_test_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }

    //proc file-step2-9:/proc/touchpanel/irq_depth
    prEntry_tmp = proc_create_data("irq_depth", 0666, prEntry_tp, &proc_get_irq_depth_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    //proc files-step2-10:/proc/touchpanel/game_switch_enable (edge limit control interface)
    if (ts->game_switch_support) {
        prEntry_tmp = proc_create_data("game_switch_enable", 0666, prEntry_tp, &proc_game_switch_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }

    //proc files-step2-11:/proc/touchpanel/oppo_tp_noise_modetest
    if (ts->noise_modetest_support) {
        prEntry_tmp = proc_create_data("oppo_tp_noise_modetest", 0664, prEntry_tp, &proc_noise_modetest_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }

    if (ts->report_rate_white_list_support) {
        prEntry_tmp = proc_create_data("report_rate_white_list", 0666, prEntry_tp, &proc_rate_white_list_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }
    if (ts->charger_pump_support) {
        prEntry_tmp = proc_create_data("charge_detect", 0666, prEntry_tp, &proc_switch_usb_state_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }
    if (ts->wireless_charger_support) {
        prEntry_tmp = proc_create_data("wireless_charge_detect", 0666, prEntry_tp, &proc_wireless_charge_detect_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }
    //proc files:/proc/touchpanel/curved_size
    if (ts->curved_size) {
        prEntry_tmp = proc_create_data("curved_size", 0666, prEntry_tp, &proc_curved_size_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }

    if (ts->smooth_level_support || ts->smooth_level_array_support) {
        prEntry_tmp = proc_create_data("smooth_level", 0666, prEntry_tp, &proc_smooth_level_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }
    if (ts->sensitive_level_array_support) {
        prEntry_tmp = proc_create_data("sensitive_level", 0666, prEntry_tp, &proc_sensitive_level_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }

    if (ts->optimized_show_support) {
        prEntry_tmp = proc_create_data("oplus_optimized_time", 0666, prEntry_tp, &proc_optimized_time_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }
/*samsung ic need to calibration if fw changed  start*/
   prEntry_tmp = proc_create_data("calibration", 0666, prEntry_tp, &proc_calibrate_fops, ts);
   if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
   }

   prEntry_tmp = proc_create_data("calibration_status", 0666, prEntry_tp, &proc_cal_status_fops, ts);
   if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
   }
/*samsung ic need to calibration if fw changed  end*/

    if (ts->kernel_grip_support) {
        prEntry_tmp = proc_create_data("kernel_grip_default_para", 0664, prEntry_tp, &tp_grip_default_para_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }
#ifdef CONFIG_OPPO_TP_APK
    // proc/touchpanel/touch_apk. Add the new test node for debug and apk. By zhangping 20190402 start
    prEntry_tmp = proc_create_data("touch_apk", 0666,
                                   prEntry_tp, &proc_touch_apk_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n",
                 __func__, __LINE__);
    }
    // End of 20190402
#endif // end of CONFIG_OPPO_TP_APK

    ts->prEntry_tp = prEntry_tp;

    //create debug_info node
    init_debug_info_proc(ts);
#ifdef CONFIG_TOUCHPANEL_ALGORITHM
    oppo_touch_algorithm_init(ts);
#endif

    //create kernel grip proc file
    if (ts->kernel_grip_support) {
        init_kernel_grip_proc(ts->prEntry_tp, ts->grip_info);
    } else if (ts->fw_grip_support) {
        prEntry_tmp = proc_create_data("kernel_grip_handle", 0666, prEntry_tp, &proc_kernel_grip_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create kernel grip proc entry, %d\n", __func__, __LINE__);
        }
    }

    return ret;
}

//proc/touchpanel/debug_info/baseline
static int tp_baseline_debug_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;
    struct debug_info_proc_operations *debug_info_ops;

    if (!ts)
        return 0;
    debug_info_ops = (struct debug_info_proc_operations *)(ts->debug_info_ops);
    if (!debug_info_ops) {
        TPD_INFO("debug_info_ops==NULL");
        return 0;
    }
    if (!debug_info_ops->baseline_read && !debug_info_ops->baseline_blackscreen_read) {
        seq_printf(s, "Not support baseline proc node\n");
        return 0;
    }
    if ((ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE) && (1 != (ts->gesture_enable & 0x01))) {
        seq_printf(s, "Not in resume over or gesture state\n");
        return 0;
    }

    //the diff  is big than one page, so do twice.
    if (s->size <= (PAGE_SIZE * 2)) {
        s->count = s->size;
        return 0;
    }

    if (ts->int_mode == BANNABLE) {
        disable_irq_nosync(ts->irq);
    }
    mutex_lock(&ts->mutex);
    if (ts->is_suspended && ts->gesture_enable) {
        if (debug_info_ops->baseline_blackscreen_read) {
            debug_info_ops->baseline_blackscreen_read(s, ts->chip_data);
        }
    } else {
        if (debug_info_ops->baseline_read) {
            debug_info_ops->baseline_read(s, ts->chip_data);
        }
    }

    //step6: return to normal mode
    ts->ts_ops->reset(ts->chip_data);
    if (ts->is_suspended == 0) {
        tp_touch_btnkey_release();
    }
    operate_mode_switch(ts);

    mutex_unlock(&ts->mutex);

    if (ts->int_mode == BANNABLE) {
        enable_irq(ts->irq);
    }
    return 0;

}

static int data_baseline_open(struct inode *inode, struct file *file)
{
    return single_open(file, tp_baseline_debug_read_func, PDE_DATA(inode));
}

static const struct file_operations tp_baseline_data_proc_fops = {
    .owner = THIS_MODULE,
    .open = data_baseline_open,
    .read = seq_read,
    .release = single_release,
};

//proc/touchpanel/debug_info/delta
static int tp_delta_debug_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;
    struct debug_info_proc_operations *debug_info_ops;

    if (!ts)
        return 0;
    debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;

    if (!debug_info_ops)
        return 0;
    if (!debug_info_ops->delta_read) {
        seq_printf(s, "Not support delta proc node\n");
        return 0;
    }
    if (ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE) {
        seq_printf(s, "Not in resume over state\n");
        return 0;
    }

    //the diff  is big than one page, so do twice.
    if (s->size <= (PAGE_SIZE * 2)) {
        s->count = s->size;
        return 0;
    }

    if (ts->int_mode == BANNABLE) {
        disable_irq_nosync(ts->irq);
    }
    mutex_lock(&ts->mutex);
    debug_info_ops->delta_read(s, ts->chip_data);
    mutex_unlock(&ts->mutex);
    if (ts->int_mode == BANNABLE) {
        enable_irq(ts->irq);
    }
    return 0;
}

static int data_delta_open(struct inode *inode, struct file *file)
{
    return single_open(file, tp_delta_debug_read_func, PDE_DATA(inode));
}

static const struct file_operations tp_delta_data_proc_fops = {
    .owner = THIS_MODULE,
    .open  = data_delta_open,
    .read  = seq_read,
    .release = single_release,
};

//proc/touchpanel/debug_info/self_delta
static int tp_self_delta_debug_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;
    struct debug_info_proc_operations *debug_info_ops;

    if (!ts)
        return 0;
    debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;

    if (!debug_info_ops)
        return 0;
    if (!debug_info_ops->self_delta_read) {
        seq_printf(s, "Not support self_delta proc node\n");
        return 0;
    }
    if (ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE) {
        seq_printf(s, "Not in resume over state\n");
        return 0;
    }

    if (ts->int_mode == BANNABLE) {
        disable_irq_nosync(ts->irq);
    }
    mutex_lock(&ts->mutex);
    debug_info_ops->self_delta_read(s, ts->chip_data);
    mutex_unlock(&ts->mutex);
    if (ts->int_mode == BANNABLE) {
        enable_irq(ts->irq);
    }
    return 0;
}

static int data_self_delta_open(struct inode *inode, struct file *file)
{
    return single_open(file, tp_self_delta_debug_read_func, PDE_DATA(inode));
}

static const struct file_operations tp_self_delta_data_proc_fops = {
    .owner = THIS_MODULE,
    .open  = data_self_delta_open,
    .read  = seq_read,
    .release = single_release,
};

//proc/touchpanel/debug_info/self_raw
static int tp_self_raw_debug_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;
    struct debug_info_proc_operations *debug_info_ops;

    if (!ts)
        return 0;
    debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;

    if (!debug_info_ops)
        return 0;
    if (!debug_info_ops->self_raw_read) {
        seq_printf(s, "Not support self_raw proc node\n");
        return 0;
    }
    if (ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE) {
        seq_printf(s, "Not in resume over state\n");
        return 0;
    }

    if (ts->int_mode == BANNABLE) {
        disable_irq_nosync(ts->irq);
    }
    mutex_lock(&ts->mutex);
    debug_info_ops->self_raw_read(s, ts->chip_data);
    mutex_unlock(&ts->mutex);
    if (ts->int_mode == BANNABLE) {
        enable_irq(ts->irq);
    }
    return 0;
}

static int data_self_raw_open(struct inode *inode, struct file *file)
{
    return single_open(file, tp_self_raw_debug_read_func, PDE_DATA(inode));
}

static const struct file_operations tp_self_raw_data_proc_fops = {
    .owner = THIS_MODULE,
    .open  = data_self_raw_open,
    .read  = seq_read,
    .release = single_release,
};

//proc/touchpanel/debug_info/main_register
static int tp_main_register_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;
    struct debug_info_proc_operations *debug_info_ops;

    TPD_INFO("%s: call.", __func__);

    if (!ts)
        return 0;
    debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;

    if (!debug_info_ops)
        return 0;
    if (!debug_info_ops->main_register_read) {
        seq_printf(s, "Not support main_register proc node\n");
        return 0;
    }
    if (ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE) {
        seq_printf(s, "Not in resume over state\n");
        return 0;
    }

    if (ts->int_mode == BANNABLE) {
        disable_irq_nosync(ts->irq);
    }
    mutex_lock(&ts->mutex);
    seq_printf(s, "es_enable:%d\n", ts->es_enable);
    seq_printf(s, "touch_count:%d\n", ts->touch_count);
    debug_info_ops->main_register_read(s, ts->chip_data);
    if (ts->kernel_grip_support) {
        seq_printf(s, "kernel_grip_info:\n");
        kernel_grip_print_func(s, ts->grip_info);
    }
    mutex_unlock(&ts->mutex);
    if (ts->int_mode == BANNABLE) {
        enable_irq(ts->irq);
    }
    return 0;
}

static int main_register_open(struct inode *inode, struct file *file)
{
    return single_open(file, tp_main_register_read_func, PDE_DATA(inode));
}

static const struct file_operations tp_main_register_proc_fops = {
    .owner = THIS_MODULE,
    .open  = main_register_open,
    .read  = seq_read,
    .release = single_release,
};

//proc/touchpanel/debug_info/reserve
static int tp_reserve_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;
    struct debug_info_proc_operations *debug_info_ops;

    if (!ts)
        return 0;
    debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;

    if (!debug_info_ops)
        return 0;
    if (!debug_info_ops->reserve_read) {
        seq_printf(s, "Not support main_register proc node\n");
        return 0;
    }
    if (ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE) {
        seq_printf(s, "Not in resume over state\n");
        return 0;
    }

    if (ts->int_mode == BANNABLE) {
        disable_irq_nosync(ts->irq);
    }
    mutex_lock(&ts->mutex);
    debug_info_ops->reserve_read(s, ts->chip_data);
    mutex_unlock(&ts->mutex);

    if (ts->int_mode == BANNABLE) {
        enable_irq(ts->irq);
    }
    return 0;
}

static int reserve_open(struct inode *inode, struct file *file)
{
    return single_open(file, tp_reserve_read_func, PDE_DATA(inode));
}

static const struct file_operations tp_reserve_proc_fops = {
    .owner = THIS_MODULE,
    .open  = reserve_open,
    .read  = seq_read,
    .release = single_release,
};

//proc/touchpanel/debug_info/data_limit
static int tp_limit_data_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;
    struct debug_info_proc_operations *debug_info_ops;

    if (!ts)
        return 0;
    debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;
    if (!debug_info_ops)
        return 0;
    if (!debug_info_ops->limit_read) {
        seq_printf(s, "Not support limit_data proc node\n");
        return 0;
    }
    debug_info_ops->limit_read(s, ts);

    return 0;
}

static int limit_data_open(struct inode *inode, struct file *file)
{
    return single_open(file, tp_limit_data_read_func, PDE_DATA(inode));
}

static const struct file_operations tp_limit_data_proc_fops = {
    .owner = THIS_MODULE,
    .open  = limit_data_open,
    .read  = seq_read,
    .release = single_release,
};

//proc/touchpanel/debug_info/abs_doze
static int tp_abs_doze_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;
    struct debug_info_proc_operations *debug_info_ops;

    if (!ts)
        return 0;
    debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;

    if (!debug_info_ops)
        return 0;
    if (!debug_info_ops->abs_doze_read) {
        seq_printf(s, "Not support main_register proc node\n");
        return 0;
    }
    if (ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE) {
        seq_printf(s, "Not in resume over state\n");
        return 0;
    }

    if (ts->int_mode == BANNABLE) {
        disable_irq_nosync(ts->irq);
    }
    mutex_lock(&ts->mutex);
    debug_info_ops->abs_doze_read(s, ts->chip_data);
    mutex_unlock(&ts->mutex);

    if (ts->int_mode == BANNABLE) {
        enable_irq(ts->irq);
    }
    return 0;
}

static int abs_doze_open(struct inode *inode, struct file *file)
{
    return single_open(file, tp_abs_doze_read_func, PDE_DATA(inode));
}

static const struct file_operations tp_abs_doze_proc_fops = {
    .owner = THIS_MODULE,
    .open  = abs_doze_open,
    .read  = seq_read,
    .release = single_release,
};


//proc/touchpanel/debug_info/freq_hop_simulate
static ssize_t proc_freq_hop_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int value = 0 ;
    char buf[4] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    if (count > 4) {
        TPD_INFO("%s:count > 4\n", __func__);
        return count;
    }

    if (!ts) {
        TPD_INFO("%s: ts is NULL\n", __func__);
        return count;
    }

    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%x", (uint32_t *)&value);
    ts->freq_hop_info.freq_hop_freq = value;

    TPD_INFO("%s: freq_hop_simulate value=0x%x\n", __func__, value);
    if (!ts->is_suspended) {
        mutex_lock(&ts->mutex);
        tp_freq_hop_simulate(ts, value);
        mutex_unlock(&ts->mutex);
    } else {
        TPD_INFO("%s: freq_hop_simulate is_suspended.\n", __func__);
        ts->freq_hop_info.freq_hop_freq = value;
    }

    return count;
}

static ssize_t proc_freq_hop_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts) {
        snprintf(page, PAGESIZE - 1, "%d\n", -1); //no support
    } else {
        snprintf(page, PAGESIZE - 1, "%d\n", ts->freq_hop_info.freq_hop_freq); //support
    }
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    return ret;
}

static const struct file_operations proc_freq_hop_fops = {
    .write = proc_freq_hop_write,
    .read  = proc_freq_hop_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

//write function of /proc/touchpanel/earsense/palm_control
static ssize_t proc_earsense_palm_control_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int value = 0;
    char buf[4] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > 2)
        return count;
    if (!ts)
        return count;

    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &value);
    if (value > 2)
        return count;

    TPD_DEBUG("%s value: %d, palm_enable :%d\n", __func__, value, ts->palm_enable);
    if (value == ts->palm_enable)
        return count;

    mutex_lock(&ts->mutex);
    ts->palm_enable = value;
    if (ts->suspend_state == TP_SPEEDUP_RESUME_COMPLETE) {
        ts->ts_ops->mode_switch(ts->chip_data, MODE_PALM_REJECTION, value);
    }
    mutex_unlock(&ts->mutex);

    return count;
}

//read function of /proc/touchpanel/earsense/palm_control
static ssize_t proc_earsense_palm_control_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return 0;

    TPD_DEBUG("%s value: %d\n", __func__, ts->palm_enable);
    ret = snprintf(page, PAGESIZE - 1, "%d\n", ts->palm_enable);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

// operation of /proc/touchpanel/earsense/palm_control
static const struct file_operations tp_earsense_palm_control_fops = {
    .write = proc_earsense_palm_control_write,
    .read  = proc_earsense_palm_control_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

// write function of /proc/touchpanel/earsense/es_enable
static ssize_t proc_earsense_es_enable_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int value = 0;
    bool state_changed = true;
    char buf[4] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > 2)
        return count;
    if (!ts)
        return count;

    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &value);
    if (value > 2)
        return count;

    TPD_DEBUG("%s value: %d, es_enable :%d\n", __func__, value, ts->es_enable);
    if (value == ts->es_enable)
        return count;

    mutex_lock(&ts->mutex);
    if ((ts->es_enable != 1) && (value != 1))
        state_changed = false;
    ts->es_enable = value;
    if (!ts->es_enable) {
        memset(ts->earsense_delta, 0, 2 * ts->hw_res.EARSENSE_TX_NUM * ts->hw_res.EARSENSE_RX_NUM);
    }
    if (!ts->is_suspended && (ts->suspend_state == TP_SPEEDUP_RESUME_COMPLETE) && state_changed) {
        ts->ts_ops->mode_switch(ts->chip_data, MODE_EARSENSE, ts->es_enable == 1);
    }
    mutex_unlock(&ts->mutex);

    return count;
}

// read function of /proc/touchpanel/earsense/es_enable
static ssize_t proc_earsense_es_enable_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return 0;

    TPD_DEBUG("%s value: %d\n", __func__, ts->es_enable);
    ret = snprintf(page, PAGESIZE - 1, "%d\n", ts->es_enable);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

// operation of /proc/touchpanel/earsense/es_enable
static const struct file_operations tp_earsense_es_enable_fops = {
    .write = proc_earsense_es_enable_write,
    .read  = proc_earsense_es_enable_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

// read function of /proc/touchpanel/earsense/es_touch_count
static ssize_t proc_earsense_touchcnt_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return 0;

    TPD_DEBUG("%s value: %d\n", __func__, ts->touch_count);
    mutex_lock(&ts->mutex);
    ret = snprintf(page, PAGESIZE - 1, "%d\n", (ts->touch_count & 0x0F));
    mutex_unlock(&ts->mutex);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

// operation of /proc/touchpanel/earsense/es_touch_count
static const struct file_operations tp_earsense_es_touchcnt_fops = {
    .read  = proc_earsense_touchcnt_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

// read function of /proc/touchpanel/earsense/rawdata
static ssize_t proc_earsense_rawdata_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    int read_len = 0;
    char *tmp_data = NULL;

    if (!ts)
        return 0;
    if (*ppos > 0)
        return 0;
    read_len = 2 * ts->hw_res.EARSENSE_TX_NUM * ts->hw_res.EARSENSE_RX_NUM;
    if (count != read_len) {
        TPD_INFO("%s, length:%d not match data_len:%d\n", __func__, (int)count, read_len);
        return 0;
    }

    TPD_DEBUG("%s is called\n", __func__);
    mutex_lock(&ts->mutex);
    if ((!ts->es_enable) || (ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE)) {
        *ppos += 11;
        mutex_unlock(&ts->mutex);
        return 0;
    }
    if (ts->delta_state == TYPE_DELTA_IDLE) {
        tmp_data = kzalloc(read_len, GFP_KERNEL);
        ts->earsense_ops->rawdata_read(ts->chip_data, tmp_data, read_len);
        mutex_unlock(&ts->mutex);
        ret = copy_to_user(user_buf, tmp_data, read_len);
        if (ret)
            TPD_INFO("touch rawdata read fail\n");
        kfree(tmp_data);
        *ppos += 11;
    } else {
        mutex_unlock(&ts->mutex);
        msleep(3);
        *ppos += 1;
    }

    return read_len;
}

// operation of /proc/touchpanel/earsense/rawdata
static const struct file_operations tp_earsense_rawdata_fops = {
    .read  = proc_earsense_rawdata_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

// read function of /proc/touchpanel/earsense/delta
static ssize_t proc_earsense_delta_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    int read_len = 0;

    if (!ts)
        return 0;
    if (*ppos > 0)
        return 0;
    read_len = 2 * ts->hw_res.EARSENSE_TX_NUM * ts->hw_res.EARSENSE_RX_NUM;
    if (count != read_len) {
        TPD_INFO("%s, length:%d not match data_len:%d\n", __func__, (int)count, read_len);
        return 0;
    }

    TPD_DEBUG("%s is called\n", __func__);
    if (!ts->es_enable) {
        return 0;
    }

    mutex_lock(&ts->mutex_earsense);
    ret = copy_to_user(user_buf, ts->earsense_delta, read_len);
    mutex_unlock(&ts->mutex_earsense);
    if (ret)
        TPD_INFO("tp rawdata read fail\n");
    *ppos += read_len;
    return read_len;
}

// operation of /proc/touchpanel/earsense/delta
static const struct file_operations tp_earsense_delta_fops = {
    .read  = proc_earsense_delta_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

// read function of /proc/touchpanel/earsense/hover_selfdata
static ssize_t proc_earsense_selfdata_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    uint16_t data_len = 0;
    char *tmp_data = NULL;

    if (!ts)
        return 0;
    if (*ppos > 0)
        return 0;
    data_len = 2 * (ts->hw_res.TX_NUM + ts->hw_res.RX_NUM);
    if (count != data_len) {
        TPD_INFO("%s, length:%d not match data_len:%d\n", __func__, (int)count, data_len);
        return 0;
    }

    TPD_DEBUG("%s is called\n", __func__);
    mutex_lock(&ts->mutex);
    if ((!ts->es_enable) || (ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE)) {
        *ppos += 11;
        mutex_unlock(&ts->mutex);
        return 0;
    }
    if (ts->delta_state == TYPE_DELTA_IDLE) {
        tmp_data = kzalloc(data_len, GFP_KERNEL);
        ts->earsense_ops->self_data_read(ts->chip_data, tmp_data, data_len);
        mutex_unlock(&ts->mutex);
        ret = copy_to_user(user_buf, tmp_data, data_len);
        if (ret)
            TPD_INFO("tp self delta read fail\n");
        kfree(tmp_data);
        *ppos += 11;
    } else {
        mutex_unlock(&ts->mutex);
        msleep(3);
        *ppos += 1;
    }

    return data_len;
}

// operation of /proc/touchpanel/earsense/hover_selfdata
static const struct file_operations tp_earsense_selfdata_fops = {
    .read  = proc_earsense_selfdata_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

// write function of /proc/touchpanel/earsense/es_enable
static ssize_t proc_fd_enable_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int value = 0;
    char buf[4] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > 2)
        return count;
    if (!ts)
        return count;

    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &value);
    if (value > 2)
        return count;

    TPD_DEBUG("%s value: %d, es_enable :%d\n", __func__, value, ts->fd_enable);
    if (value == ts->fd_enable)
        return count;

    mutex_lock(&ts->mutex);
    ts->fd_enable = value;
    if (!ts->is_suspended && (ts->suspend_state == TP_SPEEDUP_RESUME_COMPLETE)) {
        if (ts->fd_enable) {
            input_event(ts->ps_input_dev, EV_MSC, MSC_RAW, 0);
            input_sync(ts->ps_input_dev);
        }
        ts->ts_ops->mode_switch(ts->chip_data, MODE_FACE_DETECT, ts->fd_enable == 1);
    }
    mutex_unlock(&ts->mutex);

    return count;
}

// read function of /proc/touchpanel/fd_enable
static ssize_t proc_fd_enable_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return 0;

    TPD_DEBUG("%s value: %d\n", __func__, ts->fd_enable);
    ret = snprintf(page, PAGESIZE - 1, "%d\n", ts->fd_enable);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

// operation of /proc/touchpanel/fd_enable
static const struct file_operations tp_fd_enable_fops = {
    .write = proc_fd_enable_write,
    .read  = proc_fd_enable_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

// write function of /proc/touchpanel/fp_enable
static ssize_t proc_fp_enable_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int value = 0;
    char buf[4] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (count > 2)
        return count;
    if (!ts)
        return count;

    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &value);
    if (value > 2)
        return count;

    TPD_DETAIL("%s value: %d, fp_enable :%d\n", __func__, value, ts->fp_enable);
    if (value == ts->fp_enable)
        return count;

    mutex_lock(&ts->mutex);
    ts->fp_enable = value;
    if (!ts->fp_enable)
        ts->fp_info.touch_state = 0;    //reset touch state
    if (!ts->is_suspended && (ts->suspend_state == TP_SPEEDUP_RESUME_COMPLETE)) {
        ts->ts_ops->enable_fingerprint(ts->chip_data, !!ts->fp_enable);
    } else if (ts->is_suspended) {
        if (ts->black_gesture_support && (1 == (ts->gesture_enable & 0x01))) {
            ts->ts_ops->enable_fingerprint(ts->chip_data, !!ts->fp_enable);
        } else {
            operate_mode_switch(ts);
        }
    }
    mutex_unlock(&ts->mutex);

    return count;
}

// read function of /proc/touchpanel/fp_enable
static ssize_t proc_fp_enable_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return 0;

    TPD_DEBUG("%s value: %d\n", __func__, ts->fp_enable);
    ret = snprintf(page, PAGESIZE - 1, "%d\n", ts->fp_enable);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

// operation of /proc/touchpanel/fp_enable
static const struct file_operations tp_fp_enable_fops = {
    .write = proc_fp_enable_write,
    .read  = proc_fp_enable_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

// read function of /proc/touchpanel/event_num
static ssize_t proc_event_num_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    const char *devname = NULL;
    struct input_handle *handle;
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return 0;

    list_for_each_entry(handle, &ts->ps_input_dev->h_list, d_node) {
        if (strncmp(handle->name, "event", 5) == 0) {
            devname = handle->name;
            break;
        }
    }

    if (devname) {
        ret = simple_read_from_buffer(user_buf, count, ppos, devname, strlen(devname));
    }
    return ret;
}

// operation of /proc/touchpanel/event_num
static const struct file_operations tp_event_num_fops = {
    .read  = proc_event_num_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

// read function of /proc/touchpanel/fd_touch_count
static ssize_t proc_fd_touch_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    if (!ts)
        return 0;

    TPD_DEBUG("%s value: %d\n", __func__, ts->touch_count);
//    mutex_lock(&ts->mutex);
    ret = snprintf(page, PAGESIZE - 1, "%d\n", ts->touch_count & 0x0F);
//    mutex_unlock(&ts->mutex);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

// operation of /proc/touchpanel/fd_touch_count
static const struct file_operations fd_touch_num_fops = {
    .read  = proc_fd_touch_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

//proc/touchpanel/debug_info/health_monitor
static int tp_health_monitor_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;
    struct monitor_data *mon_data = &ts->monitor_data;
    struct monitor_data_v2 *mon_data_v2 = &ts->monitor_data_v2;

    mutex_lock(&ts->mutex);
    if (ts->health_monitor_v2_support) {

        if (mon_data_v2->fw_version) {
            memset(mon_data_v2->fw_version, 0, MAX_DEVICE_VERSION_LENGTH);
            strncpy(mon_data_v2->fw_version, ts->panel_data.manufacture_info.version, strlen(ts->panel_data.manufacture_info.version));
        }

        tp_healthinfo_read(s, mon_data_v2);
    } else {
        seq_printf(s, "fw version:%s\n"     "tp vendor:%s\n"      "bootup_test:%d\n"  "repeat finger:%d\n" "miss up:%d\n"
                   "grip_report:%d\n"   "baseline_err:%d\n"   "noise:%d\n"
                   "shield_palm:%d\n"   "shield_edge:%d\n"    "shield_metal:%d\n"
                   "shield_water:%d\n"  "shield_esd:%d\n"     "hard_rst:%d\n"
                   "inst_rst:%d\n"      "parity_rst:%d\n"     "wd_rst:%d\n"
                   "other_rst:%d\n"     "reserve1:%d\n"       "reserve2:%d\n"
                   "reserve3:%d\n"      "reserve4:%d\n"       "reserve5:%d\n"
                   "fw_download_retry:%d\n"   "fw_download_fail:%d\n",
                   ts->panel_data.manufacture_info.version, ts->panel_data.manufacture_info.manufacture, mon_data->bootup_test, mon_data->repeat_finger, mon_data->miss_irq,
                   mon_data->grip_report,  mon_data->baseline_err, mon_data->noise_count,
                   mon_data->shield_palm,  mon_data->shield_edge,  mon_data->shield_metal,
                   mon_data->shield_water, mon_data->shield_esd,   mon_data->hard_rst,
                   mon_data->inst_rst,     mon_data->parity_rst,   mon_data->wd_rst,
                   mon_data->other_rst,    mon_data->reserve1,     mon_data->reserve2,
                   mon_data->reserve3,     mon_data->reserve4,     mon_data->reserve5,
                   mon_data->fw_download_retry,    mon_data->fw_download_fail);

        seq_printf(s, "--last_exception--\n");
        if(mon_data->ctl_type == 1 || tp_debug) {
            seq_printf(s, "NULL\n");
        }
    }
    mutex_unlock(&ts->mutex);
    return 0;
}

static ssize_t health_monitor_control(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    char buffer[4] = {0};
    int tmp = 0;
    int *p_hor = NULL, *p_ver = NULL, eli_unit = 0, hor_range = 0, ver_range = 0;

    if (count > 2) {
        return count;
    }
    if (copy_from_user(buffer, buf, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }

    if (!ts->health_monitor_v2_support) {
        ts->monitor_data.miss_irq = 0;
        if (1 == sscanf(buffer, "%d", &tmp)) {
            if(tmp != 1 && tmp != 2)
                return count;

            p_hor = ts->monitor_data.eli_hor_pos;
            p_ver = ts->monitor_data.eli_ver_pos;
            eli_unit = ts->monitor_data.eli_size;
            hor_range = ts->monitor_data.eli_hor_range;
            ver_range = ts->monitor_data.eli_ver_range;
            memset(&ts->monitor_data, 0, sizeof(struct monitor_data));
            ts->monitor_data.ctl_type = tmp;
            ts->monitor_data.eli_hor_pos = p_hor;
            ts->monitor_data.eli_ver_pos = p_ver;
            ts->monitor_data.eli_size = eli_unit;
            ts->monitor_data.eli_hor_range = hor_range;
            ts->monitor_data.eli_ver_range = ver_range;
        } else {
            TPD_INFO("invalid content: '%s', length = %zd\n", buf, count);
        }
    }

    return count;
}

static int health_monitor_open(struct inode *inode, struct file *file)
{
    return single_open(file, tp_health_monitor_read_func, PDE_DATA(inode));
}

static const struct file_operations tp_health_monitor_proc_fops = {
    .owner = THIS_MODULE,
    .open  = health_monitor_open,
    .read  = seq_read,
    .write = health_monitor_control,
    .release = single_release,
};

#if GESTURE_RATE_MODE
//proc/touchpanel/debug_info/gesture_rate
static int tp_gesture_rate_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;
    struct debug_info_proc_operations *debug_info_ops;
    int count = 0;
    int fail_count = 0;
    int i = 0;

    debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;
    if (!debug_info_ops)
        return 0;
    if (!debug_info_ops->gesture_rate) {
        seq_printf(s, "Not support\n");
        return 0;
    }
    ts->geature_ignore = true;
    TPD_DETAIL("gona test loop: %lu\n", sizeof(coord_arg) / sizeof(coord_arg[0]));
    //	dump_stack();
    //    for (i = 0; i < sizeof(coord_arg)/sizeof(coord_arg[0]); i++)  {
    for (i = 0; i < sizeof(coord_arg) / sizeof(coord_arg[0]); i++)  {
        mutex_lock(&ts->mutex);
        debug_info_ops->gesture_rate(s, &coord_arg[i][0], ts->chip_data);
        mutex_unlock(&ts->mutex);
        do {
            msleep(count * 10);
            count++;
        } while((ts->gesture.gesture_type == 0) && count < 30);
        if (ts->gesture.gesture_type != coord_arg[i][0]) {
            seq_printf(s, "row:%d, %d  %d - %d\n", i, count, coord_arg[i][0], ts->gesture.gesture_type);
            TPD_INFO("fail row:%d, %d\n", i, count);
            fail_count++;
        }
        count = 0;
        memset(&ts->gesture, 0, sizeof(struct gesture_info));
        if(coord_arg[i][0] == DouSwip)
            i++;
        TPD_INFO("test row %d done\n", i);
    }
    TPD_INFO("ALL Test %d Group Data, fail:%d", i, fail_count);
    seq_printf(s, "ALL Test %d Group Data, fail:%d", i, fail_count);
    ts->geature_ignore = false;
    return 0;
}

static int gesture_rate_open(struct inode *inode, struct file *file)
{
    return single_open(file, tp_gesture_rate_read_func, PDE_DATA(inode));
}

static const struct file_operations tp_gesture_rate_proc_fops = {
    .owner = THIS_MODULE,
    .open  = gesture_rate_open,
    .read  = seq_read,
    .release = single_release,
};
#endif

//proc/touchpanel/debug_info
static int init_debug_info_proc(struct touchpanel_data *ts)
{
    int ret = 0;
    struct proc_dir_entry *prEntry_debug_info = NULL;
    struct proc_dir_entry *prEntry_earsense = NULL;
    struct proc_dir_entry *prEntry_tmp = NULL;

    TPD_INFO("%s entry\n", __func__);

    //proc files-step1:/proc/touchpanel/debug_info
    prEntry_debug_info = proc_mkdir("debug_info", ts->prEntry_tp);
    if (prEntry_debug_info == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create debug_info proc entry\n", __func__);
    }

    // show limit data interface
    prEntry_tmp = proc_create_data("data_limit", 0666, prEntry_debug_info, &tp_limit_data_proc_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    // show baseline data interface
    prEntry_tmp = proc_create_data("baseline", 0666, prEntry_debug_info, &tp_baseline_data_proc_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    // show delta interface
    prEntry_tmp = proc_create_data("delta", 0666, prEntry_debug_info, &tp_delta_data_proc_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    // show self delta interface
    prEntry_tmp = proc_create_data("self_delta", 0666, prEntry_debug_info, &tp_self_delta_data_proc_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    // show self_raw interface
    prEntry_tmp = proc_create_data("self_raw", 0666, prEntry_debug_info, &tp_self_raw_data_proc_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    // show main_register interface
    prEntry_tmp = proc_create_data("main_register", 0666, prEntry_debug_info, &tp_main_register_proc_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    // show reserve interface
    prEntry_tmp = proc_create_data("reserve", 0666, prEntry_debug_info, &tp_reserve_proc_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    // show abs_doze interface
    prEntry_tmp = proc_create_data("abs_doze", 0666, prEntry_debug_info, &tp_abs_doze_proc_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    if (ts->health_monitor_support || ts->health_monitor_v2_support) {
        // show health_monitor interface
        prEntry_tmp = proc_create_data("health_monitor", 0666, prEntry_debug_info, &tp_health_monitor_proc_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }

    if (ts->freq_hop_simulate_support) {
        //simulate frequency hopping interface
        prEntry_tmp = proc_create_data("freq_hop_simulate", 0666, prEntry_debug_info, &proc_freq_hop_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }

    ts->prEntry_debug_tp = prEntry_debug_info;

    if (ts->ear_sense_support) {
        //proc files-step1:/proc/touchpanel/earsense
        prEntry_earsense = proc_mkdir("earsense", ts->prEntry_tp);
        if (prEntry_earsense == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create debug_info proc entry\n", __func__);
        }
        // show baseline for earsense
        prEntry_tmp = proc_create_data("rawdata", 0666, prEntry_earsense, &tp_earsense_rawdata_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
        // show delta for earsense
        prEntry_tmp = proc_create_data("delta", 0666, prEntry_earsense, &tp_earsense_delta_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
        // show self delta for earsense
        prEntry_tmp = proc_create_data("hover_selfdata", 0666, prEntry_earsense, &tp_earsense_selfdata_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
        // palm control for earsense
        prEntry_tmp = proc_create_data("palm_control", 0666, prEntry_earsense, &tp_earsense_palm_control_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
        // es_enable for earsense
        prEntry_tmp = proc_create_data("es_enable", 0666, prEntry_earsense, &tp_earsense_es_enable_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }

        // touch count for earsense
        prEntry_tmp = proc_create_data("es_touch_count", 0666, prEntry_earsense, &tp_earsense_es_touchcnt_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }

    if (ts->face_detect_support) {
        // proc for face detect
        prEntry_tmp = proc_create_data("fd_enable", 0666, ts->prEntry_tp, &tp_fd_enable_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }

        prEntry_tmp = proc_create_data("event_num", 0666, ts->prEntry_tp, &tp_event_num_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }

        prEntry_tmp = proc_create_data("fd_touch_count", 0666, ts->prEntry_tp, &fd_touch_num_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }

    if (ts->fingerprint_underscreen_support) {
        prEntry_tmp = proc_create_data("fp_enable", 0666, ts->prEntry_tp, &tp_fp_enable_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }

    if (ts->report_point_first_support) {
        prEntry_tmp = proc_create_data("report_point_first", 0666, ts->prEntry_tp, &report_point_first_proc_fops, ts);
        if (prEntry_tmp == NULL) {
            ret = -ENOMEM;
            TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
        }
    }

    return ret;
}

/**
 * init_input_device - Using for register input device
 * @ts: touchpanel_data struct using for common driver
 *
 * we should using this function setting input report capbility && register input device
 * Returning zero(success) or negative errno(failed)
 */
static int init_input_device(struct touchpanel_data *ts)
{
    int ret = 0;
    struct kobject *vk_properties_kobj;

    TPD_INFO("%s is called\n", __func__);
    ts->input_dev = input_allocate_device();
    if (ts->input_dev == NULL) {
        ret = -ENOMEM;
        TPD_INFO("Failed to allocate input device\n");
        return ret;
    }

    ts->kpd_input_dev  = input_allocate_device();
    if (ts->kpd_input_dev == NULL) {
        ret = -ENOMEM;
        TPD_INFO("Failed to allocate key input device\n");
        return ret;
    }

    if (ts->face_detect_support) {
        ts->ps_input_dev  = input_allocate_device();
        if (ts->ps_input_dev == NULL) {
            ret = -ENOMEM;
            TPD_INFO("Failed to allocate ps input device\n");
            return ret;
        }

        ts->ps_input_dev->name = TPD_DEVICE"_ps";
        set_bit(EV_MSC, ts->ps_input_dev->evbit);
        set_bit(MSC_RAW, ts->ps_input_dev->mscbit);
    }

    ts->input_dev->name = TPD_DEVICE;
    set_bit(EV_SYN, ts->input_dev->evbit);
    set_bit(EV_ABS, ts->input_dev->evbit);
    set_bit(EV_KEY, ts->input_dev->evbit);
    set_bit(ABS_MT_TOUCH_MAJOR, ts->input_dev->absbit);
    set_bit(ABS_MT_WIDTH_MAJOR, ts->input_dev->absbit);
    set_bit(ABS_MT_POSITION_X, ts->input_dev->absbit);
    set_bit(ABS_MT_POSITION_Y, ts->input_dev->absbit);
    set_bit(ABS_MT_PRESSURE, ts->input_dev->absbit);
    set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);
    set_bit(BTN_TOUCH, ts->input_dev->keybit);
    if (ts->black_gesture_support) {
        set_bit(KEY_F4, ts->input_dev->keybit);
#ifdef CONFIG_OPPO_TP_APK
        set_bit(KEY_POWER, ts->input_dev->keybit);
#endif //end of CONFIG_OPPO_TP_APK
    }

    ts->kpd_input_dev->name = TPD_DEVICE"_kpd";
    set_bit(EV_KEY, ts->kpd_input_dev->evbit);
    set_bit(EV_SYN, ts->kpd_input_dev->evbit);

    switch(ts->vk_type) {
    case TYPE_PROPERTIES : {
        TPD_DEBUG("Type 1: using board_properties\n");
        vk_properties_kobj = kobject_create_and_add("board_properties", NULL);
        if (vk_properties_kobj)
            ret = sysfs_create_group(vk_properties_kobj, &properties_attr_group);
        if (!vk_properties_kobj || ret)
            TPD_DEBUG("failed to create board_properties\n");
        break;
    }
    case TYPE_AREA_SEPRATE: {
        TPD_DEBUG("Type 2:using same IC (button zone &&  touch zone are seprate)\n");
        if (CHK_BIT(ts->vk_bitmap, BIT_MENU))
            set_bit(KEY_MENU, ts->kpd_input_dev->keybit);
        if (CHK_BIT(ts->vk_bitmap, BIT_HOME))
            set_bit(KEY_HOMEPAGE, ts->kpd_input_dev->keybit);
        if (CHK_BIT(ts->vk_bitmap, BIT_BACK))
            set_bit(KEY_BACK, ts->kpd_input_dev->keybit);
        break;
    }
    default :
        break;
    }

#ifdef TYPE_B_PROTOCOL
    if (ts->external_touch_support) {
        input_mt_init_slots(ts->input_dev, ts->max_num + 1, 0);
    } else {
        input_mt_init_slots(ts->input_dev, ts->max_num, 0);
    }
#endif
    input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->resolution_info.max_x - 1, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, ts->resolution_info.max_y - 1, 0, 0);
    input_set_drvdata(ts->input_dev, ts);
    input_set_drvdata(ts->kpd_input_dev, ts);

    if (input_register_device(ts->input_dev)) {
        TPD_INFO("%s: Failed to register input device\n", __func__);
        input_free_device(ts->input_dev);
        return -1;
    }

    if (input_register_device(ts->kpd_input_dev)) {
        TPD_INFO("%s: Failed to register key input device\n", __func__);
        input_free_device(ts->kpd_input_dev);
        return -1;
    }

    if (ts->face_detect_support) {
        if (input_register_device(ts->ps_input_dev)) {
            TPD_INFO("%s: Failed to register ps input device\n", __func__);
            input_free_device(ts->ps_input_dev);
            return -1;
        }
    }

    return 0;
}

/**
 * init_parse_dts - parse dts, get resource defined in Dts
 * @dev: i2c_client->dev using to get device tree
 * @ts: touchpanel_data, using for common driver
 *
 * If there is any Resource needed by chip_data, we can add a call-back func in this function
 * Do not care the result : Returning void type
 */
static int init_parse_dts(struct device *dev, struct touchpanel_data *ts)
{
    int rc;
    struct device_node *np;
    int temp_array[8];
    int tx_rx_num[2];
    int val = 0;
    int i = 0;

    np = dev->of_node;
    //project
    rc = of_property_read_u32(np, "project_id", &ts->panel_data.project_id);
    if (rc) {
        TPD_INFO("project_id not specified\n");
    }

    rc = of_property_count_u32_elems(np, "platform_support_project");
    ts->panel_data.project_num = rc;
    if (!rc) {
        TPD_INFO("project not specified\n");
    }
    if (ts->panel_data.project_num >0){
        rc = of_property_read_u32_array(np, "platform_support_project", ts->panel_data.platform_support_project,ts->panel_data.project_num);
        if (rc)  {
            TPD_INFO("platform_support_project not specified");
            return -1;
        }
        rc = of_property_read_u32_array(np, "platform_support_project_dir", ts->panel_data.platform_support_project_dir,ts->panel_data.project_num);
        if (rc) {
            TPD_INFO("platform_support_project_dir not specified");
            return -1;
        }
        for (i=0; i<ts->panel_data.project_num; i++){
            ts->panel_data.platform_support_commandline[i] = devm_kzalloc(dev, 100, GFP_KERNEL);
            ts->panel_data.platform_support_external_name[i] = devm_kzalloc(dev, 7, GFP_KERNEL);
            if (ts->panel_data.platform_support_commandline[i] == NULL || ts->panel_data.platform_support_external_name[i] == NULL) {
                TPD_INFO("panel_data.platform_support_commandline or platform_support_external_name kzalloc error\n");
                goto commandline_kazalloc_error;
            }
            rc = of_property_read_string_index(np, "platform_support_project_commandline",i, (const char **)&ts->panel_data.platform_support_commandline[i]);
            if (rc) {
                TPD_INFO("platform_support_project_commandline not specified");
                goto commandline_kazalloc_error;
            }
            rc = of_property_read_string_index(np, "platform_support_project_external_name",i, (const char **)&ts->panel_data.platform_support_external_name[i]);
            if (rc) {
                TPD_INFO("platform_support_project_external_name not specified");
            }
        }
        if(!tp_judge_ic_match_commandline(&ts->panel_data )){
            TPD_INFO("commandline not match, please update dts");
            goto commandline_kazalloc_error;
        }
    }
    else {
        TPD_INFO("project and commandline not specified in dts, please update dts");
    }
    rc = of_property_read_u32(np, "vid_len", &ts->panel_data.vid_len);
    if (rc) {
        TPD_INFO("panel_data.vid_len not specified\n");
    }
    rc = of_property_read_u32(np, "tp_type", &ts->panel_data.tp_type);
    if (rc) {
        TPD_INFO("tp_type not specified\n");
    }
    ts->register_is_16bit       = of_property_read_bool(np, "register-is-16bit");
    ts->edge_limit_support      = of_property_read_bool(np, "edge_limit_support");
    ts->fw_edge_limit_support   = of_property_read_bool(np, "fw_edge_limit_support");
    ts->drlimit_remove_support  = of_property_read_bool(np, "drlimit_remove_support");
    ts->glove_mode_support      = of_property_read_bool(np, "glove_mode_support");
    ts->esd_handle_support      = of_property_read_bool(np, "esd_handle_support");
    ts->spurious_fp_support     = of_property_read_bool(np, "spurious_fingerprint_support");
    ts->charger_pump_support    = of_property_read_bool(np, "charger_pump_support");
    ts->wireless_charger_support = of_property_read_bool(np, "wireless_charger_support");
    ts->headset_pump_support    = of_property_read_bool(np, "headset_pump_support");
    ts->black_gesture_support   = of_property_read_bool(np, "black_gesture_support");
    ts->single_tap_support      = of_property_read_bool(np, "single_tap_support");
    ts->gesture_test_support    = of_property_read_bool(np, "black_gesture_test_support");
    ts->fw_update_app_support   = of_property_read_bool(np, "fw_update_app_support");
    ts->game_switch_support     = of_property_read_bool(np, "game_switch_support");
    ts->ear_sense_support       = of_property_read_bool(np, "ear_sense_support");
    ts->smart_gesture_support   = of_property_read_bool(np, "smart_gesture_support");
    ts->pressure_report_support = of_property_read_bool(np, "pressure_report_support");
    ts->is_noflash_ic           = of_property_read_bool(np, "noflash_support");
    ts->face_detect_support     = of_property_read_bool(np, "face_detect_support");
    ts->sec_long_low_trigger     = of_property_read_bool(np, "sec_long_low_trigger");
    ts->external_touch_support  = of_property_read_bool(np, "external_touch_support");
    ts->kernel_grip_support     = of_property_read_bool(np, "kernel_grip_support");
    ts->kernel_grip_support_special = of_property_read_bool(np, "kernel_grip_support_special");
    ts->fw_grip_support     = of_property_read_bool(np, "fw_grip_support");
    ts->spuri_fp_touch.lcd_trigger_fp_check = of_property_read_bool(np, "lcd_trigger_fp_check");
    ts->health_monitor_support = of_property_read_bool(np, "health_monitor_support");
    ts->health_monitor_v2_support = of_property_read_bool(np, "health_monitor_v2_support");
    ts->lcd_trigger_load_tp_fw_support = of_property_read_bool(np, "lcd_trigger_load_tp_fw_support");
    ts->fingerprint_underscreen_support = of_property_read_bool(np, "fingerprint_underscreen_support");
    ts->suspend_gesture_cfg   = of_property_read_bool(np, "suspend_gesture_cfg");
    ts->auto_test_force_pass_support = of_property_read_bool(np, "auto_test_force_pass_support");
    ts->freq_hop_simulate_support = of_property_read_bool(np, "freq_hop_simulate_support");
    ts->irq_trigger_hdl_support = of_property_read_bool(np, "irq_trigger_hdl_support");
    ts->noise_modetest_support = of_property_read_bool(np, "noise_modetest_support");
    ts->fw_update_in_probe_with_headfile = of_property_read_bool(np, "fw_update_in_probe_with_headfile");
    ts->report_point_first_support = of_property_read_bool(ts->dev->of_node, "report_point_first_support");
    ts->lcd_wait_tp_resume_finished_support = of_property_read_bool(np, "lcd_wait_tp_resume_finished_support");
    ts->spuri_fp_touch.lcd_resume_ok = true;
    ts->new_set_irq_wake_support = of_property_read_bool(np, "new_set_irq_wake_support");
    ts->report_flow_unlock_support = of_property_read_bool(np, "report_flow_unlock_support");
    ts->screenoff_fingerprint_info_support = of_property_read_bool(np, "screenoff_fingerprint_info_support");
    ts->irq_need_dev_resume_ok =  of_property_read_bool(np, "irq_need_dev_resume_ok");
    ts->report_rate_white_list_support = of_property_read_bool(np, "report_rate_white_list_support");
    ts->lcd_tp_refresh_support = of_property_read_bool(np, "lcd_tp_refresh_support");

    ts->smooth_level_support = of_property_read_bool(np, "smooth_level_support");
    ts->cs_gpio_need_pull = of_property_read_bool(np, "cs_gpio_need_pull");
    rc = of_property_read_u32(np, "smooth_level", &val);
    if (rc) {
        TPD_INFO("smooth_level not specified\n");
    } else {

        ts->smooth_level = val;

    }


    rc = of_property_read_u32(np, "vdd_2v8_volt", &ts->hw_res.vdd_volt);
    if (rc < 0) {
        ts->hw_res.vdd_volt = 0;
        TPD_INFO("vdd_2v8_volt not defined\n");
    }

    // irq gpio
    ts->hw_res.irq_gpio = of_get_named_gpio_flags(np, "irq-gpio", 0, &(ts->irq_flags));
    rc = of_property_read_string(np, "chip-name", &ts->panel_data.chip_name);
    if (rc < 0) {
        TPD_INFO("failed to get chip name, firmware/limit name will be invalid\n");
    }
    if (gpio_is_valid(ts->hw_res.irq_gpio)) {
        rc = gpio_request(ts->hw_res.irq_gpio, "tp_irq_gpio");
        if (rc) {
            TPD_INFO("unable to request gpio [%d]\n", ts->hw_res.irq_gpio);
        }
    } else {
        TPD_INFO("irq-gpio not specified in dts\n");
    }

    // reset gpio
    ts->hw_res.reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);
    if (gpio_is_valid(ts->hw_res.reset_gpio)) {
        rc = gpio_request(ts->hw_res.reset_gpio, "reset-gpio");
        if (rc)
            TPD_INFO("unable to request gpio [%d]\n", ts->hw_res.reset_gpio);
    } else {
        TPD_INFO("ts->reset-gpio not specified\n");
    }

    TPD_INFO("%s : irq_gpio = %d, irq_flags = 0x%x, reset_gpio = %d\n",
             __func__, ts->hw_res.irq_gpio, ts->irq_flags, ts->hw_res.reset_gpio);


     //cs gpio
    ts->hw_res.cs_gpio = of_get_named_gpio(np, "cs-gpio", 0);
    if (gpio_is_valid(ts->hw_res.cs_gpio)) {
       rc = gpio_request(ts->hw_res.cs_gpio, "cs-gpio");
       if (rc)
           TPD_INFO("unable to request gpio [%d]\n", ts->hw_res.cs_gpio);
    } else {
        TPD_INFO("ts->cs-gpio not specified\n");
    }

    // tp type gpio
    ts->hw_res.id1_gpio = of_get_named_gpio(np, "id1-gpio", 0);
    if (gpio_is_valid(ts->hw_res.id1_gpio)) {
        rc = gpio_request(ts->hw_res.id1_gpio, "TP_ID1");
        if (rc)
            TPD_INFO("unable to request gpio [%d]\n", ts->hw_res.id1_gpio);
    } else {
        TPD_INFO("id1_gpio not specified\n");
    }

    ts->hw_res.id2_gpio = of_get_named_gpio(np, "id2-gpio", 0);
    if (gpio_is_valid(ts->hw_res.id2_gpio)) {
        rc = gpio_request(ts->hw_res.id2_gpio, "TP_ID2");
        if (rc)
            TPD_INFO("unable to request gpio [%d]\n", ts->hw_res.id2_gpio);
    } else {
        TPD_INFO("id2_gpio not specified\n");
    }

    ts->hw_res.id3_gpio = of_get_named_gpio(np, "id3-gpio", 0);
    if (gpio_is_valid(ts->hw_res.id3_gpio)) {
        rc = gpio_request(ts->hw_res.id3_gpio, "TP_ID3");
        if (rc)
            TPD_INFO("unable to request gpio [%d]\n", ts->hw_res.id3_gpio);
    } else {
        TPD_INFO("id3_gpio not specified\n");
    }

    ts->hw_res.pinctrl = devm_pinctrl_get(dev);
    if (IS_ERR_OR_NULL(ts->hw_res.pinctrl)) {
        TPD_INFO("Getting pinctrl handle failed");
    } else {
        ts->hw_res.pin_set_high = pinctrl_lookup_state(ts->hw_res.pinctrl, "pin_set_high");
        if (IS_ERR_OR_NULL(ts->hw_res.pin_set_high)) {
            TPD_INFO("Failed to get the high state pinctrl handle\n");
        }

        ts->hw_res.pin_set_low = pinctrl_lookup_state(ts->hw_res.pinctrl, "pin_set_low");
        if (IS_ERR_OR_NULL(ts->hw_res.pin_set_low)) {
            TPD_INFO(" Failed to get the low state pinctrl handle\n");
        }

        ts->hw_res.pin_set_nopull = pinctrl_lookup_state(ts->hw_res.pinctrl, "pin_set_nopull");
        if (IS_ERR_OR_NULL(ts->hw_res.pin_set_nopull)) {
            TPD_INFO("Failed to get the input state pinctrl handle\n");
        }
    }
    ts->hw_res.enable2v8_gpio = of_get_named_gpio(np, "enable2v8_gpio", 0);
    if (ts->hw_res.enable2v8_gpio < 0) {
        TPD_INFO("ts->hw_res.enable2v8_gpio not specified\n");
    } else {
        if (gpio_is_valid(ts->hw_res.enable2v8_gpio)) {
            rc = gpio_request(ts->hw_res.enable2v8_gpio, "vdd2v8-gpio");
            if (rc) {
                TPD_INFO("unable to request gpio [%d] %d\n", ts->hw_res.enable2v8_gpio, rc);
            }
        }
    }

    ts->hw_res.enable1v8_gpio = of_get_named_gpio(np, "enable1v8_gpio", 0);
    if (ts->hw_res.enable1v8_gpio < 0) {
        TPD_INFO("ts->hw_res.enable1v8_gpio not specified\n");
    } else {
        if (gpio_is_valid(ts->hw_res.enable1v8_gpio)) {
            rc = gpio_request(ts->hw_res.enable1v8_gpio, "vcc1v8-gpio");
            if (rc) {
                TPD_INFO("unable to request gpio [%d], %d\n", ts->hw_res.enable1v8_gpio, rc);
            }
        }
    }

    // interrupt mode
    ts->int_mode = BANNABLE;
    rc = of_property_read_u32(np, "touchpanel,int-mode", &val);
    if (rc) {
        TPD_INFO("int-mode not specified\n");
    } else {
        if (val < INTERRUPT_MODE_MAX) {
            ts->int_mode = val;
        }
    }


    //firmware
    rc = of_property_read_u32(np, "firmware-len", &val);
    if (rc) {
        TPD_INFO("No firmware in dts !\n");
    } else {
        TPD_INFO("The firmware len id %d!\n", val);
        ts->firmware_in_dts = kzalloc(sizeof(struct firmware), GFP_KERNEL);
        if (ts->firmware_in_dts != NULL) {
            ts->firmware_in_dts->size = val;
            ts->firmware_in_dts->data = kzalloc(val, GFP_KERNEL | GFP_DMA);
            if (ts->firmware_in_dts->data == NULL) {
                kfree(ts->firmware_in_dts);
                ts->firmware_in_dts = NULL;
            } else {
                rc = of_property_read_u8_array(np, "firmware-data", (u8 *)ts->firmware_in_dts->data, val);
                if (rc) {
                    TPD_INFO("Can not get the firmware in dts!\n");
                    kfree(ts->firmware_in_dts->data);
                    ts->firmware_in_dts->data = NULL;
                    kfree(ts->firmware_in_dts);
                    ts->firmware_in_dts = NULL;
                }
            }
        }
    }

    // resolution info
    rc = of_property_read_u32(np, "touchpanel,max-num-support", &ts->max_num);
    if (rc) {
        TPD_INFO("ts->max_num not specified\n");
        ts->max_num = 10;
    }

    rc = of_property_read_u32_array(np, "touchpanel,tx-rx-num", tx_rx_num, 2);
    if (rc) {
        TPD_INFO("tx-rx-num not set\n");
        ts->hw_res.TX_NUM = 0;
        ts->hw_res.RX_NUM = 0;
    } else {
        ts->hw_res.TX_NUM = tx_rx_num[0];
        ts->hw_res.RX_NUM = tx_rx_num[1];
    }
    TPD_INFO("TX_NUM = %d, RX_NUM = %d \n", ts->hw_res.TX_NUM, ts->hw_res.RX_NUM);

    rc = of_property_read_u32_array(np, "earsense,tx-rx-num", tx_rx_num, 2);
    if (rc) {
        TPD_INFO("tx-rx-num not set\n");
        ts->hw_res.EARSENSE_TX_NUM = ts->hw_res.TX_NUM;
        ts->hw_res.EARSENSE_RX_NUM = ts->hw_res.RX_NUM / 2;
    } else {
        ts->hw_res.EARSENSE_TX_NUM = tx_rx_num[0];
        ts->hw_res.EARSENSE_RX_NUM = tx_rx_num[1];
    }
    TPD_INFO("EARSENSE_TX_NUM = %d, EARSENSE_RX_NUM = %d \n", ts->hw_res.EARSENSE_TX_NUM, ts->hw_res.EARSENSE_RX_NUM);

    rc = of_property_read_u32_array(np, "touchpanel,display-coords", temp_array, 2);
    if (rc) {
        TPD_INFO("Lcd size not set\n");
        ts->resolution_info.LCD_WIDTH = 0;
        ts->resolution_info.LCD_HEIGHT = 0;
    } else {
        ts->resolution_info.LCD_WIDTH = temp_array[0];
        ts->resolution_info.LCD_HEIGHT = temp_array[1];
    }

    rc = of_property_read_u32_array(np, "touchpanel,panel-coords", temp_array, 2);
    if (rc) {
        ts->resolution_info.max_x = 0;
        ts->resolution_info.max_y = 0;
    } else {
        ts->resolution_info.max_x = temp_array[0];
        ts->resolution_info.max_y = temp_array[1];
    }
    rc = of_property_read_u32_array(np, "touchpanel,touchmajor-limit", temp_array, 2);
    if (rc) {
        ts->touch_major_limit.width_range = 0;
        ts->touch_major_limit.height_range = 54;    //set default value
    } else {
        ts->touch_major_limit.width_range = temp_array[0];
        ts->touch_major_limit.height_range = temp_array[1];
    }
    TPD_INFO("LCD_WIDTH = %d, LCD_HEIGHT = %d, max_x = %d, max_y = %d, limit_witdh = %d, limit_height = %d\n",
             ts->resolution_info.LCD_WIDTH, ts->resolution_info.LCD_HEIGHT, ts->resolution_info.max_x, ts->resolution_info.max_y, \
             ts->touch_major_limit.width_range, ts->touch_major_limit.height_range);

    if (ts->health_monitor_support) {
        rc = of_property_read_u32_array(np, "touchpanel,elimination-range", temp_array, 3);
        if (rc) {
            ts->monitor_data.eli_size = 10;
            ts->monitor_data.eli_ver_range = 300;
            ts->monitor_data.eli_hor_range = 300;
        } else {
            ts->monitor_data.eli_size = temp_array[0];
            ts->monitor_data.eli_ver_range = temp_array[1];
            ts->monitor_data.eli_hor_range = temp_array[2];
        }
        TPD_INFO("eli_size = %d, eli_ver_range = %d, eli_hor_range = %d\n",
                 ts->monitor_data.eli_size, ts->monitor_data.eli_ver_range, ts->monitor_data.eli_hor_range);
    }

    rc = of_property_read_u32_array(np, "touchpanel,smooth-level", temp_array, SMOOTH_LEVEL_NUM);
    if (rc) {
        TPD_INFO("smooth_level_array not specified %d\n", rc);
    } else {
        ts->smooth_level_array_support = true;
        for (i=0; i < SMOOTH_LEVEL_NUM; i++) {
            ts->smooth_level_array[i] = temp_array[i];
        }
    }

    rc = of_property_read_u32_array(np, "touchpanel,smooth-level-charging", temp_array, SMOOTH_LEVEL_NUM);
    if (rc) {
        TPD_INFO("smooth_level_charging_array not specified %d\n", rc);
    } else {
        ts->smooth_level_charging_array_support = true;
        for (i=0; i < SMOOTH_LEVEL_NUM; i++) {
            ts->smooth_level_charging_array[i] = temp_array[i];
        }
    }

    rc = of_property_read_u32_array(np, "touchpanel,sensitive-level", temp_array, SENSITIVE_LEVEL_NUM);
    if (rc) {
        TPD_INFO("sensitive_level_array not specified %d\n", rc);
    } else {
        ts->sensitive_level_array_support = true;
        for (i=0; i < SENSITIVE_LEVEL_NUM; i++) {
            ts->sensitive_level_array[i] = temp_array[i];
        }
    }
    rc = of_property_read_u32(ts->dev->of_node, "touchpanel,default_hor_area", &ts->default_hor_area);
    if (rc) {
        ts->default_hor_area = 0;
    } else {
        TPD_INFO("set default horizontal area value:%d.\n", ts->default_hor_area);
    }

    // virturl key Related
    rc = of_property_read_u32_array(np, "touchpanel,button-type", temp_array, 2);
    if (rc < 0) {
        TPD_INFO("error:button-type should be setting in dts!");
    } else {
        ts->vk_type = temp_array[0];
        ts->vk_bitmap = temp_array[1] & 0xFF;
        if (ts->vk_type == TYPE_PROPERTIES) {
            rc = of_property_read_u32_array(np, "touchpanel,button-map", temp_array, 8);
            if (rc) {
                TPD_INFO("button-map not set\n");
            } else {
                ts->button_map.coord_menu.x = temp_array[0];
                ts->button_map.coord_menu.y = temp_array[1];
                ts->button_map.coord_home.x = temp_array[2];
                ts->button_map.coord_home.y = temp_array[3];
                ts->button_map.coord_back.x = temp_array[4];
                ts->button_map.coord_back.y = temp_array[5];
                ts->button_map.width_x = temp_array[6];
                ts->button_map.height_y = temp_array[7];
            }
        }
    }

    //touchkey take tx num and rx num
    rc = of_property_read_u32_array(np, "touchpanel.button-TRx", temp_array, 2);
    if(rc < 0) {
        TPD_INFO("error:button-TRx should be setting in dts!\n");
        ts->hw_res.key_TX = 0;
        ts->hw_res.key_RX = 0;
    } else {
        ts->hw_res.key_TX = temp_array[0];
        ts->hw_res.key_RX = temp_array[1];
        TPD_INFO("key_tx is %d, key_rx is %d\n", ts->hw_res.key_TX, ts->hw_res.key_RX);
    }

    //set incell panel parameter, for of_property_read_bool return 1 when success and return 0 when item is not exist
    rc = ts->is_incell_panel = of_property_read_bool(np, "incell_screen");
    if(rc > 0) {
        TPD_INFO("panel is incell!\n");
        ts->is_incell_panel = 1;
    } else {
        TPD_INFO("panel is oncell!\n");
        ts->is_incell_panel = 0;
    }

    rc = of_property_read_u32(np, "touchpanel,curved-size", &ts->curved_size);
    if (rc < 0) {
        TPD_INFO("ts->curved_size not specified\n");
        ts->curved_size = 0;
    } else {
        TPD_INFO("ts->curved_size is %d\n", ts->curved_size);
    }

    rc = of_property_read_u32(np, "touchpanel,single-optimized-time", &ts->single_optimized_time);
    if (rc) {
        TPD_INFO("ts->single_optimized_time not specified\n");
        ts->single_optimized_time = 0;
        ts->optimized_show_support = false;
    } else {
        ts->total_operate_times = 0;
        ts->optimized_show_support = true;
    }

    return 0;
commandline_kazalloc_error:
    return -1;	
    // We can Add callback fuction here if necessary seprate some dts config for chip_data
}

int init_power_control(struct touchpanel_data *ts)
{
    int ret = 0;

    // 1.8v
    ts->hw_res.vcc_1v8 = regulator_get(ts->dev, "vcc_1v8");
    if (IS_ERR_OR_NULL(ts->hw_res.vcc_1v8)) {
        TPD_INFO("Regulator get failed vcc_1v8, ret = %d\n", ret);
    } else {
        if (regulator_count_voltages(ts->hw_res.vcc_1v8) > 0) {
            ret = regulator_set_voltage(ts->hw_res.vcc_1v8, 1800000, 1800000);
            if (ret) {
                dev_err(ts->dev, "Regulator set_vtg failed vcc_i2c rc = %d\n", ret);
                goto regulator_vcc_1v8_put;
            }

            ret = regulator_set_load(ts->hw_res.vcc_1v8, 200000);
            if (ret < 0) {
                dev_err(ts->dev, "Failed to set vcc_1v8 mode(rc:%d)\n", ret);
                goto regulator_vcc_1v8_put;
            }
        }
    }
    // vdd 2.8v
    ts->hw_res.vdd_2v8 = regulator_get(ts->dev, "vdd_2v8");
    if (IS_ERR_OR_NULL(ts->hw_res.vdd_2v8)) {
        TPD_INFO("Regulator vdd2v8 get failed, ret = %d\n", ret);
    } else {
        if (regulator_count_voltages(ts->hw_res.vdd_2v8) > 0) {
            TPD_INFO("set avdd voltage to %d uV\n", ts->hw_res.vdd_volt);
            if (ts->hw_res.vdd_volt) {
                ret = regulator_set_voltage(ts->hw_res.vdd_2v8, ts->hw_res.vdd_volt, ts->hw_res.vdd_volt);
            } else {
                ret = regulator_set_voltage(ts->hw_res.vdd_2v8, 3100000, 3100000);
            }
            if (ret) {
                dev_err(ts->dev, "Regulator set_vtg failed vdd rc = %d\n", ret);
                goto regulator_vdd_2v8_put;
            }

            ret = regulator_set_load(ts->hw_res.vdd_2v8, 200000);
            if (ret < 0) {
                dev_err(ts->dev, "Failed to set vdd_2v8 mode(rc:%d)\n", ret);
                goto regulator_vdd_2v8_put;
            }
        }
    }

    return 0;

regulator_vdd_2v8_put:
    regulator_put(ts->hw_res.vdd_2v8);
    ts->hw_res.vdd_2v8 = NULL;
regulator_vcc_1v8_put:
    if (!IS_ERR_OR_NULL(ts->hw_res.vcc_1v8)) {
        regulator_put(ts->hw_res.vcc_1v8);
        ts->hw_res.vcc_1v8 = NULL;
    }

    return ret;
}

int tp_powercontrol_1v8(struct hw_resource *hw_res, bool on)
{
    int ret = 0;

    if (on) {// 1v8 power on
        if (!IS_ERR_OR_NULL(hw_res->vcc_1v8)) {
            TPD_INFO("Enable the Regulator1v8.\n");
            ret = regulator_enable(hw_res->vcc_1v8);
            if (ret) {
                TPD_INFO("Regulator vcc_i2c enable failed ret = %d\n", ret);
                return ret;
            }
        }

        if (hw_res->enable1v8_gpio > 0) {
            TPD_INFO("Enable the 1v8_gpio\n");
            ret = gpio_direction_output(hw_res->enable1v8_gpio, 1);
            if (ret) {
                TPD_INFO("enable the enable1v8_gpio failed.\n");
                return ret;
            }
        }
    } else {// 1v8 power off
        if (!IS_ERR_OR_NULL(hw_res->vcc_1v8)) {
            ret = regulator_disable(hw_res->vcc_1v8);
            if (ret) {
                TPD_INFO("Regulator vcc_i2c enable failed rc = %d\n", ret);
                return ret;
            }
        }

        if (hw_res->enable1v8_gpio > 0) {
            TPD_INFO("disable the 1v8_gpio\n");
            ret = gpio_direction_output(hw_res->enable1v8_gpio, 0);
            if (ret) {
                TPD_INFO("disable the enable2v8_gpio failed.\n");
                return ret;
            }
        }
    }

    return 0;
}

int tp_powercontrol_2v8(struct hw_resource *hw_res, bool on)
{
    int ret = 0;

    if (on) {// 2v8 power on
        if (!IS_ERR_OR_NULL(hw_res->vdd_2v8)) {
            TPD_INFO("Enable the Regulator2v8.\n");
            ret = regulator_enable(hw_res->vdd_2v8);
            if (ret) {
                TPD_INFO("Regulator vdd enable failed ret = %d\n", ret);
                return ret;
            }
        }
        if (hw_res->enable2v8_gpio > 0) {
            TPD_INFO("Enable the 2v8_gpio, hw_res->enable2v8_gpio is %d\n", hw_res->enable2v8_gpio);
            ret = gpio_direction_output(hw_res->enable2v8_gpio, 1);
            if (ret) {
                TPD_INFO("enable the enable2v8_gpio failed.\n");
                return ret;
            }
        }
    } else {// 2v8 power off
        if (!IS_ERR_OR_NULL(hw_res->vdd_2v8)) {
            ret = regulator_disable(hw_res->vdd_2v8);
            if (ret) {
                TPD_INFO("Regulator vdd disable failed rc = %d\n", ret);
                return ret;
            }
        }
        if (hw_res->enable2v8_gpio > 0) {
            TPD_INFO("disable the 2v8_gpio\n");
            ret = gpio_direction_output(hw_res->enable2v8_gpio, 0);
            if (ret) {
                TPD_INFO("disable the enable2v8_gpio failed.\n");
                return ret;
            }
        }
    }
    return ret;
}


static void esd_handle_func(struct work_struct *work)
{
    int ret = 0;
    struct touchpanel_data *ts = container_of(work, struct touchpanel_data,
                                 esd_info.esd_check_work.work);

    if (ts->loading_fw) {
        TPD_INFO("FW is updating, stop esd handle!\n");
        return;
    }

    mutex_lock(&ts->esd_info.esd_lock);
    if (!ts->esd_info.esd_running_flag) {
        TPD_INFO("Esd protector has stopped!\n");
        goto ESD_END;
    }

    if (ts->is_suspended == 1) {
        TPD_INFO("Touch panel has suspended!\n");
        goto ESD_END;
    }

    if (!ts->ts_ops->esd_handle) {
        TPD_INFO("not support ts_ops->esd_handle callback\n");
        goto ESD_END;
    }

    ret = ts->ts_ops->esd_handle(ts->chip_data);
    if (ret == -1) {    //-1 means esd hanppened: handled in IC part, recovery the state here
        operate_mode_switch(ts);
    }

    if (ts->esd_info.esd_running_flag)
        queue_delayed_work(ts->esd_info.esd_workqueue, &ts->esd_info.esd_check_work, ts->esd_info.esd_work_time);
    else
        TPD_INFO("Esd protector suspended!");

ESD_END:
    mutex_unlock(&ts->esd_info.esd_lock);
    return;
}

/**
 * esd_handle_switch - open or close esd thread
 * @esd_info: touchpanel_data, using for common driver resource
 * @on: bool variable using for  indicating open or close esd check function.
 *     true:open;
 *     false:close;
 */
void esd_handle_switch(struct esd_information *esd_info, bool on)
{
    mutex_lock(&esd_info->esd_lock);

    if (on) {
        if (!esd_info->esd_running_flag) {
            esd_info->esd_running_flag = 1;

            TPD_INFO("Esd protector started, cycle: %d s\n", esd_info->esd_work_time / HZ);
            queue_delayed_work(esd_info->esd_workqueue, &esd_info->esd_check_work, esd_info->esd_work_time);
        }
    } else {
        if (esd_info->esd_running_flag) {
            esd_info->esd_running_flag = 0;

            TPD_INFO("Esd protector stoped!\n");
            cancel_delayed_work(&esd_info->esd_check_work);
        }
    }

    mutex_unlock(&esd_info->esd_lock);
}

int tp_register_irq_func(struct touchpanel_data *ts)
{
    int ret = 0;
#ifdef TPD_USE_EINT
    if (gpio_is_valid(ts->hw_res.irq_gpio)) {
        TPD_DEBUG("%s, irq_gpio is %d, ts->irq is %d\n", __func__, ts->hw_res.irq_gpio, ts->irq);

        if(ts->irq_flags_cover) {
            ts->irq_flags = ts->irq_flags_cover;
            TPD_INFO("%s irq_flags is covered by 0x%x\n", __func__, ts->irq_flags_cover);
        }

        if(ts->irq <= 0) {
            ts->irq = gpio_to_irq(ts->hw_res.irq_gpio);
            TPD_DEBUG("%s, [irq info] irq_gpio is %d, ts->irq is %d\n", __func__, ts->hw_res.irq_gpio, ts->irq);

            if(ts->is_noflash_ic) {
                ts->s_client->irq = gpio_to_irq(ts->hw_res.irq_gpio);
                TPD_DEBUG("%s, [irq info] irq_gpio is %d, ts->s_client->irq is %d\n", __func__, ts->hw_res.irq_gpio, ts->s_client->irq);
            } else {
                ts->client->irq = gpio_to_irq(ts->hw_res.irq_gpio);
                TPD_DEBUG("%s, [irq info] irq_gpio is %d, ts->client->irq is %d\n", __func__, ts->hw_res.irq_gpio, ts->client->irq);
            }
        }

        ret = request_threaded_irq(ts->irq, NULL,
                                   tp_irq_thread_fn,
                                   ts->irq_flags | IRQF_ONESHOT,
                                   TPD_DEVICE, ts);
        if (ret < 0) {
            TPD_INFO("%s request_threaded_irq ret is %d\n", __func__, ret);
        }
    } else {
        TPD_INFO("%s:no valid irq\n", __func__);
    }
#else
    hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    ts->timer.function = touchpanel_timer_func;
    hrtimer_start(&ts->timer, ktime_set(3, 0), HRTIMER_MODE_REL);
#endif

    return ret;
}

//work schdule for reading&update delta
static void touch_read_delta(struct work_struct *work)
{
    struct touchpanel_data *ts = container_of(work, struct touchpanel_data, read_delta_work);

    mutex_lock(&ts->mutex_earsense);
    mutex_lock(&ts->mutex);
    if (!ts->is_suspended) {
        ts->earsense_ops->delta_read(ts->chip_data, ts->earsense_delta, 2 * ts->hw_res.EARSENSE_TX_NUM * ts->hw_res.EARSENSE_RX_NUM);
    }
    mutex_unlock(&ts->mutex);
    mutex_unlock(&ts->mutex_earsense);
    ts->delta_state = TYPE_DELTA_IDLE;
}

#ifdef CONFIG_OPLUS_SYSTEM_SEC_DEBUG
static void tp_async_secdebug (void *data)
{
    char *env[2] = {"TPLG_STATUS=ON", NULL};
    struct touchpanel_data *ts = (struct touchpanel_data *)data;

    if(!ts)
        return;

    if(kobject_uevent_env(&ts->dev->kobj, KOBJ_CHANGE, env)) {
        TPD_INFO("failed to send uevent");
    }

    /*maybe we can change tp debug log level here*/
    return;
}
#endif


/**
 * register_common_touch_device - parse dts, get resource defined in Dts
 * @pdata: touchpanel_data, using for common driver
 *
 * entrance of common touch Driver
 * Returning zero(sucess) or negative errno(failed)
 */
int register_common_touch_device(struct touchpanel_data *pdata)
{
    struct touchpanel_data *ts = pdata;
    struct invoke_method *invoke;

    int ret = -1;

    TPD_INFO("%s  is called\n", __func__);
    //step1 : dts parse
    ret = init_parse_dts(ts->dev, ts);
    if (ret<0){
	TPD_INFO("%s: parse dts  failed.\n", __func__);
	return -1;
	}

    //step2 : initial health info parameter
    if (ts->health_monitor_v2_support) {
        ret = tp_healthinfo_init(ts->dev, &ts->monitor_data_v2);
        if (ret < 0) {
            TPD_INFO("health info init failed.\n");
        }
        ts->monitor_data_v2.health_monitor_support = true;
        ts->monitor_data_v2.chip_data = ts->chip_data;
        ts->monitor_data_v2.debug_info_ops = ts->debug_info_ops;
    }

    //step3 : IIC interfaces init
    init_touch_interfaces(ts->dev, ts->register_is_16bit);

    //step3 : mutex init
    mutex_init(&ts->mutex);
    mutex_init(&ts->report_mutex);
    init_completion(&ts->pm_complete);
    init_completion(&ts->fw_complete);
    init_completion(&ts->resume_complete);

    INIT_WORK(&ts->async_work, tp_async_work_lock);

    if (ts->has_callback) {
        TPD_INFO("%s: synaptics ic need async work", __func__);
        invoke = (struct invoke_method *)pdata->chip_data;
        invoke->invoke_common = tp_work_common_callback;
        invoke->async_work = tp_async_work_callback;
    } else {
        TPD_INFO("%s No synaptics ic cancel async work", __func__);
        cancel_work_sync(&ts->async_work);
    }
    //step4 : Power init && setting
    preconfig_power_control(ts);
    ret = init_power_control(ts);
    if (ret) {
        TPD_INFO("%s: tp power init failed.\n", __func__);
        return -1;
    }
    ret = reconfig_power_control(ts);
    if (ret) {
        TPD_INFO("%s: reconfig power failed.\n", __func__);
        return -1;
    }
    if (!ts->ts_ops->power_control) {
        ret = -EINVAL;
        TPD_INFO("tp power_control NULL!\n");
        goto power_control_failed;
    }
    ret = ts->ts_ops->power_control(ts->chip_data, true);
    if (ret) {
        TPD_INFO("%s: tp power init failed.\n", __func__);
        goto power_control_failed;
    }

    //step5 : I2C function check
    if (!ts->is_noflash_ic) {
        if (!i2c_check_functionality(ts->client->adapter, I2C_FUNC_I2C)) {
            TPD_INFO("%s: need I2C_FUNC_I2C\n", __func__);
            ret = -ENODEV;
            goto err_check_functionality_failed;
        }
    }

    //step6 : touch input dev init
    ret = init_input_device(ts);
    if (ret < 0) {
        ret = -EINVAL;
        TPD_INFO("tp_input_init failed!\n");
        goto err_check_functionality_failed;
    }

    if (ts->int_mode == UNBANNABLE) {
        ret = tp_register_irq_func(ts);
        if (ret < 0) {
            goto free_touch_panel_input;
        }
        ts->i2c_ready = true;
    }

    //step7 : Alloc fw_name/devinfo memory space
    ts->panel_data.fw_name = kzalloc(MAX_FW_NAME_LENGTH, GFP_KERNEL);
    if (ts->panel_data.fw_name == NULL) {
        ret = -ENOMEM;
        TPD_INFO("panel_data.fw_name kzalloc error\n");
        goto free_touch_panel_input;
    }

    ts->panel_data.manufacture_info.version = kzalloc(MAX_DEVICE_VERSION_LENGTH, GFP_KERNEL);
    if (ts->panel_data.manufacture_info.version == NULL) {
        ret = -ENOMEM;
        TPD_INFO("manufacture_info.version kzalloc error\n");
        goto manu_version_alloc_err;
    }

    ts->panel_data.manufacture_info.manufacture = kzalloc(MAX_DEVICE_MANU_LENGTH, GFP_KERNEL);
    if (ts->panel_data.manufacture_info.manufacture == NULL) {
        ret = -ENOMEM;
        TPD_INFO("panel_data.fw_name kzalloc error\n");
        goto manu_info_alloc_err;
    }

    //step8 : touchpanel vendor
    tp_util_get_vendor(&ts->hw_res, &ts->panel_data);
    if (ts->ts_ops->get_vendor) {
        ts->ts_ops->get_vendor(ts->chip_data, &ts->panel_data);
    }
    if (ts->health_monitor_v2_support) {
        ts->monitor_data_v2.vendor = ts->panel_data.manufacture_info.manufacture;
    }

    //step9 : FTM process
    ts->boot_mode = get_boot_mode();
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
    if ((ts->boot_mode == META_BOOT || ts->boot_mode == FACTORY_BOOT))
#else
    if ((ts->boot_mode == MSM_BOOT_MODE__FACTORY || ts->boot_mode == MSM_BOOT_MODE__RF || ts->boot_mode == MSM_BOOT_MODE__WLAN))
#endif
    {
        ts->ts_ops->ftm_process(ts->chip_data);
        ret = -EFTM;
        if (ts->int_mode == UNBANNABLE) {
            free_irq(ts->irq, ts);
        }
        g_tp = ts;
        TPD_INFO("%s: not int normal mode, return.\n", __func__);
        return ret;
    }

    //step10:get chip info
    if (!ts->ts_ops->get_chip_info) {
        ret = -EINVAL;
        TPD_INFO("tp get_chip_info NULL!\n");
        goto err_check_functionality_failed;
    }
    ret = ts->ts_ops->get_chip_info(ts->chip_data);
    if (ret < 0) {
        ret = -EINVAL;
        TPD_INFO("tp get_chip_info failed!\n");
        goto err_check_functionality_failed;
    }

    //step11 : touchpanel Fw check
    if(!ts->is_noflash_ic) {            //noflash don't have firmware before fw update
        if (!ts->ts_ops->fw_check) {
            ret = -EINVAL;
            TPD_INFO("tp fw_check NULL!\n");
            goto manu_info_alloc_err;
        }
        ret = ts->ts_ops->fw_check(ts->chip_data, &ts->resolution_info, &ts->panel_data);
        if (ret == FW_ABNORMAL) {
            ts->force_update = 1;
            TPD_INFO("This FW need to be updated!\n");
        } else {
            ts->force_update = 0;
        }
    }


    //step12 : enable touch ic irq output ability
    if (!ts->ts_ops->mode_switch) {
        ret = -EINVAL;
        TPD_INFO("tp mode_switch NULL!\n");
        goto manu_info_alloc_err;
    }
    ret = ts->ts_ops->mode_switch(ts->chip_data, MODE_NORMAL, true);
    if (ret < 0) {
        ret = -EINVAL;
        TPD_INFO("%s:modem switch failed!\n", __func__);
        goto manu_info_alloc_err;
    }

    //step13 : irq request setting
    if (ts->int_mode == BANNABLE) {
        ret = tp_register_irq_func(ts);
        if (ret < 0) {
            goto manu_info_alloc_err;
        }
    }

    //step14 : suspend && resume fuction register
#if defined(CONFIG_DRM_MSM)
    ts->fb_notif.notifier_call = fb_notifier_callback;
    ret = msm_drm_register_client(&ts->fb_notif);
    if (ret) {
        TPD_INFO("Unable to register fb_notifier: %d\n", ret);
    }
#elif defined(CONFIG_FB)
    ts->fb_notif.notifier_call = fb_notifier_callback;
    ret = fb_register_client(&ts->fb_notif);
    if (ret) {
        TPD_INFO("Unable to register fb_notifier: %d\n", ret);
    }
#endif/*CONFIG_FB*/

    //step15 : workqueue create(speedup_resume)
    ts->speedup_resume_wq = create_singlethread_workqueue("speedup_resume_wq");
    if (!ts->speedup_resume_wq) {
        ret = -ENOMEM;
        goto threaded_irq_free;
    }

    ts->lcd_trigger_load_tp_fw_wq = create_singlethread_workqueue("lcd_trigger_load_tp_fw_wq");
    if (!ts->lcd_trigger_load_tp_fw_wq) {
        ret = -ENOMEM;
        goto threaded_irq_free;
    }

    INIT_WORK(&ts->speed_up_work, speedup_resume);
    INIT_WORK(&ts->fw_update_work, tp_fw_update_work);
    INIT_WORK(&ts->lcd_trigger_load_tp_fw_work, lcd_trigger_load_tp_fw);

    //step 16 : short edge shield
    if (ts->edge_limit_support) {
        ts->limit_enable = 1;
        ts->limit_edge = ts->limit_enable & 1;
        ts->limit_corner = 0;
        ts->limit_valid = 0;
        ts->edge_limit.limit_area = 1;
        ts->edge_limit.in_which_area = AREA_NOTOUCH;

        ts->edge_limit.left_x1  = (ts->edge_limit.limit_area * 1000) / 100;
        ts->edge_limit.right_x1 = ts->resolution_info.LCD_WIDTH - ts->edge_limit.left_x1;
        ts->edge_limit.left_x2  = 2 * ts->edge_limit.left_x1;
        ts->edge_limit.right_x2 = ts->resolution_info.LCD_WIDTH - (2 * ts->edge_limit.left_x1);
        ts->edge_limit.left_x3  = 5 * ts->edge_limit.left_x1;
        ts->edge_limit.right_x3 = ts->resolution_info.LCD_WIDTH - (5 * ts->edge_limit.left_x1);

        ts->edge_limit.left_y1  = (ts->edge_limit.limit_area * 1000) / 100;
        ts->edge_limit.right_y1 = ts->resolution_info.LCD_HEIGHT - ts->edge_limit.left_y1;
        ts->edge_limit.left_y2  = 2 * ts->edge_limit.left_y1;
        ts->edge_limit.right_y2 = ts->resolution_info.LCD_HEIGHT - (2 * ts->edge_limit.left_y1);
        ts->edge_limit.left_y3  = 5 * ts->edge_limit.left_y1;
        ts->edge_limit.right_y3 = ts->resolution_info.LCD_HEIGHT - (5 * ts->edge_limit.left_y1);
    } else if (ts->fw_edge_limit_support) {
        ts->limit_enable = 1;
        ts->limit_edge = ts->limit_enable & 1;
        ts->limit_corner = 0;
        ts->limit_valid = 0;
    }

    //step 17:esd recover support
    if (ts->esd_handle_support) {
        ts->esd_info.esd_workqueue = create_singlethread_workqueue("esd_workthread");
        INIT_DELAYED_WORK(&ts->esd_info.esd_check_work, esd_handle_func);

        mutex_init(&ts->esd_info.esd_lock);

        ts->esd_info.esd_running_flag = 0;
        ts->esd_info.esd_work_time = 2 * HZ; // HZ: clock ticks in 1 second generated by system
        TPD_INFO("Clock ticks for an esd cycle: %d\n", ts->esd_info.esd_work_time);

        esd_handle_switch(&ts->esd_info, true);
    }

    //frequency hopping simulate support
    if (ts->freq_hop_simulate_support) {
        ts->freq_hop_info.freq_hop_workqueue = create_singlethread_workqueue("syna_tcm_freq_hop");
        INIT_DELAYED_WORK(&ts->freq_hop_info.freq_hop_work, tp_freq_hop_work);
        ts->freq_hop_info.freq_hop_simulating = false;
        ts->freq_hop_info.freq_hop_freq = 0;
    }

    //step 18:spurious_fingerprint support
    if (ts->spurious_fp_support) {
        ts->spuri_fp_touch.thread = kthread_run(finger_protect_handler, ts, "touchpanel_fp");
        if (IS_ERR(ts->spuri_fp_touch.thread)) {
            TPD_INFO("spurious fingerprint thread create failed\n");
        }
    }

    // step 20: ear sense support
    if (ts->ear_sense_support) {
        mutex_init(&ts->mutex_earsense);    // init earsense operate mutex

        //malloc space for storing earsense delta
        ts->earsense_delta = kzalloc(2 * ts->hw_res.EARSENSE_TX_NUM * ts->hw_res.EARSENSE_RX_NUM, GFP_KERNEL);
        if (ts->earsense_delta == NULL) {
            ret = -ENOMEM;
            TPD_INFO("earsense_delta kzalloc error\n");
            goto threaded_irq_free;
        }

        //create work queue for read earsense delta
        ts->delta_read_wq = create_singlethread_workqueue("touch_delta_wq");
        if (!ts->delta_read_wq) {
            ret = -ENOMEM;
            goto earsense_alloc_free;
        }
        INIT_WORK(&ts->read_delta_work, touch_read_delta);
    }

    if (ts->health_monitor_support) {
        ts->monitor_data.eli_ver_pos = kzalloc((ts->monitor_data.eli_ver_range / ts->monitor_data.eli_size) * (ts->resolution_info.max_y / ts->monitor_data.eli_size) * sizeof(int), GFP_KERNEL);
        ts->monitor_data.eli_hor_pos = kzalloc((ts->monitor_data.eli_hor_range / ts->monitor_data.eli_size) * (ts->resolution_info.max_x / ts->monitor_data.eli_size) * sizeof(int), GFP_KERNEL);
        if ((NULL == ts->monitor_data.eli_ver_pos) || (NULL == ts->monitor_data.eli_hor_pos)) {
            TPD_INFO("array for elimination area kzalloc failed.\n");
        }
    }

    //initial kernel grip parameter
    if (ts->kernel_grip_support) {
        ts->grip_info = kernel_grip_init(ts->dev);
        if (!ts->grip_info) {
            TPD_INFO("kernel grip init failed.\n");
        }
    }

    // lcd_tp_refresh_support support
    if (ts->lcd_tp_refresh_support) {
        ts->tp_refresh_wq = create_singlethread_workqueue("tp_refresh_wq");
        if (!ts->tp_refresh_wq) {
            ret = -ENOMEM;
            goto threaded_irq_free;
        }

        INIT_WORK(&ts->tp_refresh_work, lcd_tp_refresh_work);
    }

    //step 21 : createproc proc files interface
    init_touchpanel_proc(ts);

#ifdef CONFIG_OPLUS_SYSTEM_SEC_DEBUG
    oplus_register_secdebug(TOUCH, tp_async_secdebug, NULL, NULL, ts);
#endif
    //step 22 : Other****
    ts->i2c_ready = true;
    ts->loading_fw = false;
    ts->is_suspended = 0;
    ts->suspend_state = TP_SPEEDUP_RESUME_COMPLETE;
    ts->gesture_enable = 0;
    ts->es_enable = 0;
    ts->fd_enable = 0;
    ts->fp_enable = 0;
    ts->fp_info.touch_state = 0;
    ts->palm_enable = 1;
    ts->touch_count = 0;
    ts->glove_enable = 0;
    ts->view_area_touched = 0;
    ts->external_touch_status = false;
    ts->tp_suspend_order = LCD_TP_SUSPEND;
    ts->tp_resume_order = TP_LCD_RESUME;
    ts->skip_suspend_operate = false;
    ts->skip_reset_in_resume = false;
    ts->irq_slot = 0;
    ts->firmware_update_type = 0;
    ts->report_point_first_enable = 0;//reporting point first ,when baseline error
    ts->resume_finished = 1;
    if(ts->is_noflash_ic) {
        ts->irq = ts->s_client->irq;
    } else {
        ts->irq = ts->client->irq;
    }
    tp_register_times++;
    g_tp = ts;
    complete(&ts->pm_complete);
    TPD_INFO("Touch panel probe : normal end\n");
    return 0;

earsense_alloc_free:
    kfree(ts->earsense_delta);

threaded_irq_free:
    free_irq(ts->irq, ts);

manu_info_alloc_err:
    kfree(ts->panel_data.manufacture_info.version);

manu_version_alloc_err:
    kfree(ts->panel_data.fw_name);

free_touch_panel_input:
    input_unregister_device(ts->input_dev);
    input_unregister_device(ts->kpd_input_dev);

err_check_functionality_failed:
    ts->ts_ops->power_control(ts->chip_data, false);

power_control_failed:

    if (!IS_ERR_OR_NULL(ts->hw_res.vdd_2v8)) {
        regulator_put(ts->hw_res.vdd_2v8);
        ts->hw_res.vdd_2v8 = NULL;
    }

    if (!IS_ERR_OR_NULL(ts->hw_res.vcc_1v8)) {
        regulator_put(ts->hw_res.vcc_1v8);
        ts->hw_res.vcc_1v8 = NULL;
    }

    if (gpio_is_valid(ts->hw_res.enable2v8_gpio))
        gpio_free(ts->hw_res.enable2v8_gpio);

    if (gpio_is_valid(ts->hw_res.enable1v8_gpio))
        gpio_free(ts->hw_res.enable1v8_gpio);

    if (gpio_is_valid(ts->hw_res.irq_gpio)) {
        gpio_free(ts->hw_res.irq_gpio);
    }

    if (gpio_is_valid(ts->hw_res.reset_gpio)) {
        gpio_free(ts->hw_res.reset_gpio);
    }

    if (gpio_is_valid(ts->hw_res.id1_gpio)) {
        gpio_free(ts->hw_res.id1_gpio);
    }

    if (gpio_is_valid(ts->hw_res.id2_gpio)) {
        gpio_free(ts->hw_res.id2_gpio);
    }

    if (gpio_is_valid(ts->hw_res.id3_gpio)) {
        gpio_free(ts->hw_res.id3_gpio);
    }

    return ret;
}

/**
 * touchpanel_ts_suspend - touchpanel suspend function
 * @dev: i2c_client->dev using to get touchpanel_data resource
 *
 * suspend function bind to LCD on/off status
 * Returning zero(sucess) or negative errno(failed)
 */
static int tp_suspend(struct device *dev)
{
    u64 start_time = 0;
    int ret;
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    TPD_INFO("%s: start.\n", __func__);

    TPD_INFO("tp_suspend ts->spuri_fp_touch.fp_trigger =%d  ts->i2c_ready =%d  ts->spuri_fp_touch.lcd_resume_ok=%d \n",
             ts->spuri_fp_touch.fp_trigger, ts->i2c_ready, ts->spuri_fp_touch.lcd_resume_ok);
    ts->spuri_fp_touch.lcd_resume_ok = false;
    //step1:detect whether we need to do suspend
    if (ts->input_dev == NULL) {
        TPD_INFO("input_dev  registration is not complete\n");
        goto NO_NEED_SUSPEND;
    }
    if (ts->loading_fw) {
        TPD_INFO("FW is updating while suspending");
        goto NO_NEED_SUSPEND;
    }

    if (ts->health_monitor_v2_support) {
        reset_healthinfo_time_counter(&start_time);
    }

#ifndef TPD_USE_EINT
    hrtimer_cancel(&ts->timer);
#endif

    /* release all complete first */
    if (ts->ts_ops->reinit_device) {
        ts->ts_ops->reinit_device(ts->chip_data);
    }

    //step2:get mutex && start process suspend flow
    mutex_lock(&ts->mutex);
    if (!ts->is_suspended) {
        ts->monitor_data.monitor_down = 0;
        ts->monitor_data.monitor_up = 0;
        ts->is_suspended = 1;
        ts->suspend_state = TP_SUSPEND_COMPLETE;
    } else {
        TPD_INFO("%s: do not suspend twice.\n", __func__);
        goto EXIT;
    }

    //step3:Release key && touch event before suspend
    tp_btnkey_release(ts);
    tp_touch_release(ts);

    //step4:cancel esd test
    if (ts->esd_handle_support) {
        esd_handle_switch(&ts->esd_info, false);
    }

    ts->rate_ctrl_level = 0;

    if (!ts->is_incell_panel || (ts->black_gesture_support && ts->gesture_enable > 0)) {
        //step5:gamde mode support
        if (ts->game_switch_support)
            ts->ts_ops->mode_switch(ts->chip_data, MODE_GAME, false);

        if (ts->report_point_first_support)
            ts->ts_ops->set_report_point_first(ts->chip_data, false);

        if (ts->report_rate_white_list_support&&ts->ts_ops->rate_white_list_ctrl) {
            ts->ts_ops->rate_white_list_ctrl(ts->chip_data, 0);
        }

        //step5:ear sense support
        if (ts->ear_sense_support) {
            ts->ts_ops->mode_switch(ts->chip_data, MODE_EARSENSE, false);
        }
        if (ts->face_detect_support && ts->fd_enable) {
            ts->ts_ops->mode_switch(ts->chip_data, MODE_FACE_DETECT, false);
        }
    }

    //step6:finger print support handle
    if (ts->fingerprint_underscreen_support) {
        operate_mode_switch(ts);
        ts->fp_info.touch_state = 0;
        opticalfp_irq_handler(&ts->fp_info);
        goto EXIT;
    }

    //step7:gesture mode status process
    if (ts->black_gesture_support) {
        if ((ts->gesture_enable & 0x01) == 1) {
            if (ts->single_tap_support && ts->ts_ops->enable_single_tap) {
                if (ts->gesture_enable == 3) {
                    ts->ts_ops->enable_single_tap(ts->chip_data, true);
                } else {
                    ts->ts_ops->enable_single_tap(ts->chip_data, false);
                }
            }
            ts->ts_ops->mode_switch(ts->chip_data, MODE_GESTURE, true);
            goto EXIT;
        }
    }

    //step for suspend_gesture_cfg when ps is near ts->gesture_enable == 2
    if (ts->suspend_gesture_cfg && ts->black_gesture_support && ts->gesture_enable == 2) {
        ts->ts_ops->mode_switch(ts->chip_data, MODE_GESTURE, true);
        operate_mode_switch(ts);
        goto EXIT;
    }

    //step8:skip suspend operate only when gesture_enable is 0
    if (ts->skip_suspend_operate && (!ts->gesture_enable)) {
        goto EXIT;
    }

    //step9:switch mode to sleep
    ret = ts->ts_ops->mode_switch(ts->chip_data, MODE_SLEEP, true);
    if (ret < 0) {
        TPD_INFO("%s, Touchpanel operate mode switch failed\n", __func__);
    }

EXIT:
    if (ts->health_monitor_v2_support) {
        tp_healthinfo_report(&ts->monitor_data_v2, HEALTH_SUSPEND, &start_time);
    }
    TPD_INFO("%s: end.\n", __func__);
    mutex_unlock(&ts->mutex);

NO_NEED_SUSPEND:
    complete(&ts->pm_complete);

    return 0;
}

/**
 * touchpanel_ts_suspend - touchpanel resume function
 * @dev: i2c_client->dev using to get touchpanel_data resource
 *
 * resume function bind to LCD on/off status, this fuction start thread to speedup screen on flow.
 * Do not care the result: Return void type
 */
static void tp_resume(struct device *dev)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    TPD_INFO("%s start.\n", __func__);

    if (!ts->is_suspended) {
        TPD_INFO("%s: do not resume twice.\n", __func__);
        goto NO_NEED_RESUME;
    }
    ts->monitor_data.monitor_down = 0;
    ts->monitor_data.monitor_up = 0;
    ts->is_suspended = 0;
    ts->suspend_state = TP_RESUME_COMPLETE;
    ts->disable_gesture_ctrl = false;
    if (ts->loading_fw)
        goto NO_NEED_RESUME;

    //free irq at first
    if(!ts->irq_trigger_hdl_support) {
        if (ts->int_mode == UNBANNABLE) {
            mutex_lock(&ts->mutex);
        }
        free_irq(ts->irq, ts);
        if (ts->int_mode == UNBANNABLE) {
            mutex_unlock(&ts->mutex);
        }
    }

    if (ts->ts_ops->reinit_device) {
        ts->ts_ops->reinit_device(ts->chip_data);
    }
    if(ts->ts_ops->resume_prepare) {
        mutex_lock(&ts->mutex);
        ts->ts_ops->resume_prepare(ts->chip_data);
        mutex_unlock(&ts->mutex);
    }

    if (ts->lcd_wait_tp_resume_finished_support) {
        ts->resume_finished = 0;
    }

    if (ts->kernel_grip_support) {
        if (ts->grip_info) {
            kernel_grip_reset(ts->grip_info);
        } else {
            ts->grip_info = kernel_grip_init(ts->dev);
            init_kernel_grip_proc(ts->prEntry_tp, ts->grip_info);
        }
    }

    queue_work(ts->speedup_resume_wq, &ts->speed_up_work);
    return;

NO_NEED_RESUME:
    ts->suspend_state = TP_SPEEDUP_RESUME_COMPLETE;
    complete(&ts->pm_complete);
}

void lcd_trigger_tp_irq_reset(void)
{
    if (!g_tp)
        return;
    if (g_tp->irq_trigger_hdl_support) {
        TPD_INFO("%s\n", __func__);
        free_irq(g_tp->irq, g_tp);
        tp_register_irq_func(g_tp);
    }
}
EXPORT_SYMBOL(lcd_trigger_tp_irq_reset);

void lcd_queue_load_tp_fw(void)
{
    if (!g_tp)
        return;
    if (g_tp->lcd_trigger_load_tp_fw_support) {
        TPD_INFO("%s\n", __func__);
        g_tp->disable_gesture_ctrl = true;
        if (g_tp->ts_ops) {
            if (g_tp->ts_ops->tp_queue_work_prepare) {
                mutex_lock(&g_tp->mutex);
                g_tp->ts_ops->tp_queue_work_prepare();
                mutex_unlock(&g_tp->mutex);
            }
        }
        queue_work(g_tp->lcd_trigger_load_tp_fw_wq, &(g_tp->lcd_trigger_load_tp_fw_work));
    }
}

static void lcd_trigger_load_tp_fw(struct work_struct *work)
{
    struct touchpanel_data *ts = container_of(work, struct touchpanel_data,
                                 lcd_trigger_load_tp_fw_work);
    static bool is_running = false;
    u64 start_time = 0;

    if (ts->lcd_trigger_load_tp_fw_support) {
        if (is_running) {
            TPD_INFO("%s is running, can not repeat\n", __func__);
        } else {
            TPD_INFO("%s start\n", __func__);
            if (ts->health_monitor_v2_support) {
                reset_healthinfo_time_counter(&start_time);
            }
            is_running = true;
            mutex_lock(&ts->mutex);
            ts->ts_ops->reset(ts->chip_data);
            mutex_unlock(&ts->mutex);
            is_running = false;

            if (ts->health_monitor_v2_support) {
                tp_healthinfo_report(&ts->monitor_data_v2, HEALTH_FW_UPDATE_COST, &start_time);
            }
        }
    }
}

void lcd_wait_tp_resume_finished(void)
{
    int retry_cnt = 0;
    if (!g_tp)
        return;
    if (g_tp->lcd_wait_tp_resume_finished_support) {
        TPD_INFO("%s\n", __func__);

        do {
            if(retry_cnt) {
                msleep(100);
            }
            retry_cnt++;
            TPD_DETAIL("Wait hdl finished retry %d times...  \n", retry_cnt);
        } while(!g_tp->resume_finished && retry_cnt < 20);
    }
}
EXPORT_SYMBOL(lcd_wait_tp_resume_finished);

static void lcd_tp_refresh_work(struct work_struct *work)
{
    struct touchpanel_data *ts = container_of(work, struct touchpanel_data,
                                 tp_refresh_work);

    mutex_lock(&ts->mutex);
    ts->ts_ops->tp_refresh_switch(ts->chip_data,
            ts->lcd_fps);
    mutex_unlock(&ts->mutex);

}

void lcd_tp_refresh_switch(int fps)
{

    if (!g_tp)
        return;
    if (g_tp->lcd_tp_refresh_support) {
        TPD_INFO("%s:fps:%d\n", __func__, fps);
        g_tp->lcd_fps = fps;
        if (g_tp->ts_ops) {
            if (g_tp->ts_ops->tp_refresh_switch && !g_tp->is_suspended) {
                queue_work(g_tp->tp_refresh_wq, &g_tp->tp_refresh_work);
            }
        }
    }

}
EXPORT_SYMBOL(lcd_tp_refresh_switch);

/**
 * speedup_resume - speedup resume thread process
 * @work: work struct using for this thread
 *
 * do actully resume function
 * Do not care the result: Return void type
 */
static void speedup_resume(struct work_struct *work)
{
    int timed_out = 0;
    u64 start_time = 0;
    struct touchpanel_data *ts = container_of(work, struct touchpanel_data,
                                 speed_up_work);

    TPD_INFO("%s is called\n", __func__);

    //step1: get mutex for locking i2c acess flow
    mutex_lock(&ts->mutex);

    if (ts->health_monitor_v2_support) {
        reset_healthinfo_time_counter(&start_time);
    }

    //step2:before Resume clear All of touch/key event Reset some flag to default satus
    if (ts->edge_limit_support)
        ts->edge_limit.in_which_area = AREA_NOTOUCH;
    tp_btnkey_release(ts);
    tp_touch_release(ts);

    if (!ts->irq_trigger_hdl_support) {
        if (ts->int_mode == UNBANNABLE) {
            tp_register_irq_func(ts);
        }
    }

    if (ts->use_resume_notify && (!ts->fp_info.touch_state || !ts->report_flow_unlock_support)) {
        reinit_completion(&ts->resume_complete);
    }

    //step3:Reset IC && switch work mode, ft8006 is reset by lcd, no more reset needed
    if (!ts->skip_reset_in_resume && !ts->fp_info.touch_state) {
        if (!ts->lcd_trigger_load_tp_fw_support) {
            ts->ts_ops->reset(ts->chip_data);
        }
    }

    //step4:If use resume notify, exit wait first
    if (ts->use_resume_notify && (!ts->fp_info.touch_state || !ts->report_flow_unlock_support)) {
        timed_out = wait_for_completion_timeout(&ts->resume_complete, 1 * HZ); //wait resume over for 1s
        if ((0 == timed_out) || (ts->resume_complete.done)) {
            TPD_INFO("resume state, timed_out:%d, done:%d\n", timed_out, ts->resume_complete.done);
            if (!timed_out && ts->ts_ops->resume_timedout_operate) {
                ts->ts_ops->resume_timedout_operate(ts->chip_data);
                ts->suspend_state = TP_SPEEDUP_RESUME_COMPLETE;
                TPD_INFO("%s: end!\n", __func__);
                mutex_unlock(&ts->mutex);
                complete(&ts->pm_complete);
                if (ts->lcd_wait_tp_resume_finished_support) {
                    ts->resume_finished = 1;
                }
                return;
            }
        }
    }

    if (ts->ts_ops->specific_resume_operate) {
        ts->ts_ops->specific_resume_operate(ts->chip_data);
    }

    //step5: set default ps status to far
    if (ts->ts_ops->write_ps_status) {
        ts->ts_ops->write_ps_status(ts->chip_data, 0);
    }

    operate_mode_switch(ts);

    if (ts->esd_handle_support) {
        esd_handle_switch(&ts->esd_info, true);
    }

    //step6:Request irq again
    if (!ts->irq_trigger_hdl_support) {
        if (ts->int_mode == BANNABLE) {
            tp_register_irq_func(ts);
        }
    }

    ts->suspend_state = TP_SPEEDUP_RESUME_COMPLETE;

    if (ts->lcd_wait_tp_resume_finished_support) {
        ts->resume_finished = 1;
    }

    if (ts->health_monitor_v2_support) {
        tp_healthinfo_report(&ts->monitor_data_v2, HEALTH_RESUME, &start_time);
    }

    //step7:Unlock  && exit
    TPD_INFO("%s: end!\n", __func__);
    mutex_unlock(&ts->mutex);
    complete(&ts->pm_complete);
}

#if defined(CONFIG_FB) || defined(CONFIG_DRM_MSM)
static int fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
    int *blank;
    int timed_out = -1;
    struct fb_event *evdata = data;
    struct touchpanel_data *ts = container_of(self, struct touchpanel_data, fb_notif);

    //to aviod some kernel bug (at fbmem.c some local veriable are not initialized)
#ifdef CONFIG_DRM_MSM
    if(event != MSM_DRM_EARLY_EVENT_BLANK && event != MSM_DRM_EVENT_BLANK)
#else
    if(event != FB_EARLY_EVENT_BLANK && event != FB_EVENT_BLANK)
#endif
        return 0;

    if (evdata && evdata->data && ts && ts->chip_data) {
        blank = evdata->data;
        TPD_INFO("%s: event = %ld, blank = %d\n", __func__, event, *blank);
#ifdef CONFIG_DRM_MSM
        if (*blank == MSM_DRM_BLANK_POWERDOWN) { //suspend
            if (event == MSM_DRM_EARLY_EVENT_BLANK) {    //early event
#else
        if (*blank == FB_BLANK_POWERDOWN) { //suspend
            if (event == FB_EARLY_EVENT_BLANK) {    //early event
#endif
                timed_out = wait_for_completion_timeout(&ts->pm_complete, 0.5 * HZ); //wait resume over for 0.5s
                if ((0 == timed_out) || (ts->pm_complete.done)) {
                    TPD_INFO("completion state, timed_out:%d, done:%d\n", timed_out, ts->pm_complete.done);
                }

                ts->suspend_state = TP_SUSPEND_EARLY_EVENT;      //set suspend_resume_state
                if (ts->esd_handle_support && ts->is_incell_panel && (ts->tp_suspend_order == LCD_TP_SUSPEND)) {
                    esd_handle_switch(&ts->esd_info, false);     //incell panel need cancel esd early
                }

                if (ts->tp_suspend_order == TP_LCD_SUSPEND) {
                    tp_suspend(ts->dev);
                } else if (ts->tp_suspend_order == LCD_TP_SUSPEND) {
                    if (!ts->gesture_enable && ts->is_incell_panel) {
                        disable_irq_nosync(ts->irq);
                    }
                }
#ifdef CONFIG_DRM_MSM
            } else if (event == MSM_DRM_EVENT_BLANK) {   //event
#else
            } else if (event == FB_EVENT_BLANK) {   //event
#endif
                if (ts->tp_suspend_order == TP_LCD_SUSPEND) {

                } else if (ts->tp_suspend_order == LCD_TP_SUSPEND) {
                    tp_suspend(ts->dev);
                }
            }
#ifdef CONFIG_DRM_MSM
        } else if (*blank == MSM_DRM_BLANK_UNBLANK) {//resume
            if (event == MSM_DRM_EARLY_EVENT_BLANK) {    //early event
#else
        } else if (*blank == FB_BLANK_UNBLANK ) {//resume
            if (event == FB_EARLY_EVENT_BLANK) {    //early event
#endif
                timed_out = wait_for_completion_timeout(&ts->pm_complete, 0.5 * HZ); //wait suspend over for 0.5s
                if ((0 == timed_out) || (ts->pm_complete.done)) {
                    TPD_INFO("completion state, timed_out:%d, done:%d\n", timed_out, ts->pm_complete.done);
                }

                ts->suspend_state = TP_RESUME_EARLY_EVENT;      //set suspend_resume_state

                if (ts->tp_resume_order == TP_LCD_RESUME) {
                    tp_resume(ts->dev);
                } else if (ts->tp_resume_order == LCD_TP_RESUME) {
                    if (!ts->irq_trigger_hdl_support) {
                        disable_irq_nosync(ts->irq);
                    }
                }
#ifdef CONFIG_DRM_MSM
            } else if (event == MSM_DRM_EVENT_BLANK) {   //event
#else
            } else if (event == FB_EVENT_BLANK) {   //event
#endif
                if (ts->tp_resume_order == TP_LCD_RESUME) {

                } else if (ts->tp_resume_order == LCD_TP_RESUME) {
                    tp_resume(ts->dev);
                    if (!ts->irq_trigger_hdl_support) {
                        enable_irq(ts->irq);
                    }
                }
            }
        }
    }

    return 0;
}
#endif

#if defined CONFIG_TOUCHPANEL_MTK_PLATFORM || defined CONFIG_TOUCHPANEL_NEW_SET_IRQ_WAKE
void tp_i2c_suspend(struct touchpanel_data *ts)
{
    ts->i2c_ready = false;
    if (ts->black_gesture_support || ts->fingerprint_underscreen_support) {
        if ((ts->gesture_enable & 0x01) == 1 || ts->fp_enable) {
            /*enable gpio wake system through interrupt*/
            enable_irq_wake(ts->irq);
            return;
        }
    }
    disable_irq(ts->irq);
}

void tp_i2c_resume(struct touchpanel_data *ts)
{
    if (ts->black_gesture_support || ts->fingerprint_underscreen_support) {
        if ((ts->gesture_enable & 0x01) == 1 || ts->fp_enable) {
            /*disable gpio wake system through intterrupt*/
            disable_irq_wake(ts->irq);
            goto OUT;
        }
    }
    enable_irq(ts->irq);

OUT:
    ts->i2c_ready = true;
    if (((ts->black_gesture_support|| ts->fingerprint_underscreen_support)
        && ((ts->gesture_enable & 0x01) == 1|| ts->fp_enable)) ||
        (ts->spurious_fp_support && ts->spuri_fp_touch.fp_trigger)) {
        wake_up_interruptible(&waiter);
    }
}

#else
void tp_i2c_suspend(struct touchpanel_data *ts)
{
    ts->i2c_ready = false;
    if (ts->black_gesture_support || ts->fingerprint_underscreen_support) {
        if ((ts->gesture_enable & 0x01) == 1 || ts->fp_enable) {
            /*enable gpio wake system through interrupt*/
            enable_irq_wake(ts->irq);
            if (ts->new_set_irq_wake_support) {
                return;
            }
        }
    }
    disable_irq(ts->irq);
}

void tp_i2c_resume(struct touchpanel_data *ts)
{
    if (ts->black_gesture_support || ts->fingerprint_underscreen_support) {
        if ((ts->gesture_enable & 0x01) == 1 || ts->fp_enable) {
            /*disable gpio wake system through intterrupt*/
            disable_irq_wake(ts->irq);
            if (ts->new_set_irq_wake_support) {
                goto OUT;
            }
        }
    }
    enable_irq(ts->irq);

OUT:
    ts->i2c_ready = true;
    if (ts->spurious_fp_support && ts->spuri_fp_touch.fp_trigger) {
        wake_up_interruptible(&waiter);
    }
}
#endif

struct touchpanel_data *common_touch_data_alloc(void)
{
    if (g_tp) {
        TPD_INFO("%s:common panel struct has alloc already!\n", __func__);
        return NULL;
    }
    return kzalloc(sizeof(struct touchpanel_data), GFP_KERNEL);
}

int common_touch_data_free(struct touchpanel_data *pdata)
{
    if (pdata) {
        kfree(pdata);
    }

    g_tp = NULL;
    return 0;
}

void tp_ftm_extra(void)
{
    if (g_tp == NULL) {
        return ;
    }
    if (g_tp->ts_ops) {
        if (g_tp->ts_ops->ftm_process_extra) {
            g_tp->ts_ops->ftm_process_extra();
        }
    }
    return ;
}

EXPORT_SYMBOL(tp_ftm_extra);



/**
 * input_report_key_oppo - Using for report virtual key
 * @work: work struct using for this thread
 *
 * before report virtual key, detect whether touch_area has been touched
 * Do not care the result: Return void type
 */
void input_report_key_oppo(struct input_dev *dev, unsigned int code, int value)
{
    if (value) {//report Key[down]
        if (g_tp) {
            if (g_tp->view_area_touched == 0) {
                input_report_key(dev, code, value);
            } else
                TPD_INFO("Sorry,tp is touch down,can not report touch key\n");
        }
    } else {
        input_report_key(dev, code, value);
    }
}

void clear_view_touchdown_flag(void)
{
    if (g_tp) {
        g_tp->view_area_touched = 0;
    }
}

static oem_verified_boot_state oem_verifiedbootstate = OEM_VERIFIED_BOOT_STATE_LOCKED;
bool is_oem_unlocked(void)
{
    return (oem_verifiedbootstate == OEM_VERIFIED_BOOT_STATE_UNLOCKED);
}
int __init get_oem_verified_boot_state(void)
{
    if (strstr(boot_command_line, "androidboot.verifiedbootstate=orange")) {
        oem_verifiedbootstate = OEM_VERIFIED_BOOT_STATE_UNLOCKED;
    } else {
        oem_verifiedbootstate = OEM_VERIFIED_BOOT_STATE_LOCKED;
    }
    return 0;
}
