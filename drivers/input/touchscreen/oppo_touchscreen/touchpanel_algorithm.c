// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "touchpanel_common.h"
#include <touchpanel_algorithm.h>
#include <linux/uaccess.h>

/*******Start of LOG TAG Declear**********************************/
#define TPD_DEVICE "tp_algorithm"
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
/*******End of LOG TAG Declear***********************************/
static void init_touch_point(struct algo_point_buf *point_buf, struct point_info *point)
{

    point_buf->down_point.x = point->x;
    point_buf->down_point.y = point->y;
    point_buf->down_point.z = point->z;

    point_buf->kal_x_last.x= INVALID_POINT;

    point_buf->status = NORMAL;

}

static void init_kalman_point(struct algo_point_buf *point_buf, struct point_info *point)
{
    point_buf->kal_p_last.x = 1;
    point_buf->kal_p_last.y = 1;
    point_buf->kal_p_last.z = 1;

    point_buf->kal_x_last.x = point->x;
    point_buf->kal_x_last.y = point->y;
    point_buf->kal_x_last.z = point->z;
    point_buf->delta_time = 0;
}


static void touch_algoithm_click_down(struct touchpanel_data *ts)
{

    int i = 0;
    int count = 0;
    struct touch_algorithm_info *algo_info = ts->algo_info;
    for (i = 0; i < ts->max_num; i++) {
        struct algo_point_buf *point_buf = &algo_info->point_buf[i];
        if (point_buf->status == CLICK_UP) {
#ifdef TYPE_B_PROTOCOL
            count++;
            input_mt_slot(ts->input_dev, i);
            input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 1);
            input_report_key(ts->input_dev, BTN_TOUCH, 1);
            input_report_key(ts->input_dev, BTN_TOOL_FINGER, 1);
            input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, point_buf->down_point.z);
            input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, point_buf->down_point.z);
            if (ts->pressure_report_support) {
                input_report_abs(ts->input_dev, ABS_MT_PRESSURE, point_buf->down_point.z);   //add for fixing gripview tap no function issue
            }
            input_report_abs(ts->input_dev, ABS_MT_POSITION_X, point_buf->down_point.x);
            input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, point_buf->down_point.y);
#endif
            point_buf->status = NORMAL;
            point_buf->touch_time = 0;
        }
    }
#ifdef TYPE_B_PROTOCOL
    if (count) {
        input_sync(ts->input_dev);
    }
#endif
}

static void KalmanFilter(int ResrcData, uint16_t *x_last, uint16_t *p_last,
                       uint16_t delta_time, struct algo_kalman_info *kal_value)
{
    int R;
    int Q;
    int x_mid;
    int x_now;
    int p_mid ;
    int p_now;
    //int kg;
    int temp;
    int filter_dis;
    int kal_lv;

    temp = abs(ResrcData - *x_last);
    if (delta_time == 0) {
        delta_time = 1;
    }
    temp = temp * TIME_ONE_SECOND / delta_time; // speed pixel/s;
    filter_dis = kal_value->kalman_dis - kal_value->min_dis;
    kal_lv = kal_value->Q_Max - kal_value->Q_Min;
    if (temp > kal_value->kalman_dis) {
        Q = kal_value->Q_Max;
        R = KALMAN_LEVEL - Q;
    } else if (temp < kal_value->min_dis) {
        Q = 0;
        R = KALMAN_LEVEL;
    } else {
        temp = temp - kal_value->min_dis;
        if (kal_lv == 0) {
            Q = kal_value->Q_Min;
            R = KALMAN_LEVEL - kal_value->Q_Min;
        } else {
            Q = kal_value->Q_Min + (2 * temp * kal_lv + filter_dis) / (2 * filter_dis);
            R = KALMAN_LEVEL - Q;
        }
    }
    x_mid = (*x_last) * VARIABLE_GAIN;
    ResrcData = ResrcData * VARIABLE_GAIN;

    p_mid = *p_last + Q;

    //kg=p_mid/(p_mid+R);

    x_now = (VARIABLE_GAIN / 2) + x_mid;
    x_now = (x_now + (long)(p_mid * (ResrcData - x_mid)) / (p_mid + R) ) / VARIABLE_GAIN;

    p_now = (2 * R * p_mid + p_mid + R) / (2 * (p_mid + R));

    *p_last = (uint16_t)p_now;

    *x_last = (uint16_t)x_now;
    TPD_DEBUG("R %d,Q %d, p_mid %d, p_now %d\n", R, Q, p_mid, p_now);

}

static void kalman_handle(struct touchpanel_data *ts, int obj_attention, struct point_info *points)
{
    int i = 0;
    struct touch_algorithm_info *algo_info;

    algo_info = ts->algo_info;
    if (algo_info->kalman_info_used->kalman_dis == 0) {
        return;
    }
    //do kalman
    for (i = 0; i < ts->max_num; i++) {
        struct algo_point_buf *point_buf = &algo_info->point_buf[i];
        if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01 && (points[i].status != 0)) {
            if(point_buf->kal_x_last.x == INVALID_POINT){
                init_kalman_point(point_buf, &points[i]);
            }
            KalmanFilter(points[i].x, &point_buf->kal_x_last.x, &point_buf->kal_p_last.x, point_buf->delta_time, algo_info->kalman_info_used);
            KalmanFilter(points[i].y, &point_buf->kal_x_last.y, &point_buf->kal_p_last.y, point_buf->delta_time, algo_info->kalman_info_used);
            TPD_DEBUG("The point id %d before kalman is %d,%d,after kalman is %d,%d.\n",
                     i, points[i].x, points[i].y, point_buf->kal_x_last.x, point_buf->kal_x_last.y);

            points[i].x = point_buf->kal_x_last.x;
            points[i].y = point_buf->kal_x_last.y;
        } else {
            point_buf->kal_x_last.x = INVALID_POINT;
        }
    }

}

static int stretch_point_handle(struct touchpanel_data *ts, int obj_attention, struct point_info *points)
{
    int i = 0;
    int obj_new = 0;

    uint32_t temp;
    struct touch_algorithm_info *algo_info;

    if (ts->algo_info == NULL) {
        return obj_attention;
    }
    algo_info = ts->algo_info;

    for (i = 0; i < ts->max_num; i++) {
        struct algo_point_buf *point_buf = &algo_info->point_buf[i];
        if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01 && (points[i].status == 0)) // buf[0] == 0 is wrong point, no process
            continue;
        if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01 && (points[i].status != 0)) {

            if (point_buf->touch_time == 0) {
                point_buf->touch_time = 1;

                //top and bottom
                if (algo_info->stretch_info.top_frame > 0
                    && points[i].y > algo_info->stretch_info.top_start
                    && points[i].y < algo_info->stretch_info.top_end) {

                    if (algo_info->phone_direction & algo_info->stretch_info.top_open_dir) {
                        TPD_INFO("The id %d maybe need top stretch point is %d,%d", i, points[i].x, points[i].y);
                        point_buf->status |= TOP_START;

                    }
                } else if (algo_info->stretch_info.bottom_frame > 0
                           && points[i].y > algo_info->stretch_info.bottom_start
                           && points[i].y < algo_info->stretch_info.bottom_end) {

                    if (algo_info->phone_direction & algo_info->stretch_info.bottom_open_dir) {
                        TPD_INFO("The id %d maybe need bottom stretch point is %d,%d", i, points[i].x, points[i].y);
                        point_buf->status |= BOTTOM_START;

                    }
                }

                //left and right
                if (algo_info->stretch_info.left_frame > 0
                    && points[i].x > algo_info->stretch_info.left_start
                    && points[i].x < algo_info->stretch_info.left_end) {

                    if (algo_info->phone_direction & algo_info->stretch_info.left_open_dir) {
                        TPD_INFO("The id %d maybe need left stretch point is %d,%d", i, points[i].x, points[i].y);
                        point_buf->status |= LEFT_START;

                    }
                } else if (algo_info->stretch_info.right_frame > 0
                           && points[i].x > algo_info->stretch_info.right_start
                           && points[i].x < algo_info->stretch_info.right_end) {

                    if (algo_info->phone_direction & algo_info->stretch_info.right_open_dir) {
                        TPD_INFO("The id %d maybe need right stretch point is %d,%d", i, points[i].x, points[i].y);
                        point_buf->status |= RIGHT_START;

                    }
                }
            }

            // stretch
            if (point_buf->status & TOP_START) {
                if (point_buf->down_point.y < points[i].y
                    && point_buf->touch_time <= algo_info->stretch_info.top_frame) {
                    TPD_INFO("Start stretch, the origin point is id:%d, x:%d, y:%d.\n",
                             i, points[i].x, points[i].y);
                    temp = points[i].y - point_buf->down_point.y;
                    temp = ((temp * algo_info->stretch_info.top_stretch_time) / (point_buf->touch_time + 1)) + 1;
                    if (point_buf->down_point.y < temp) {
                        temp = point_buf->down_point.y;
                    }
                    points[i].y = point_buf->down_point.y - temp;

                    point_buf->status &= ~TOP_START;
                    TPD_INFO("Stretch point is y:%d time is %d.\n", points[i].y, point_buf->touch_time);
                } else if (point_buf->down_point.y > points[i].y
                           || point_buf->down_point.x != points[i].x
                           || point_buf->touch_time > algo_info->stretch_info.top_frame) {
                    point_buf->status &= ~TOP_START;
                }

            }


            if (point_buf->status & BOTTOM_START) {
                if (point_buf->down_point.y  > points[i].y
                    && point_buf->touch_time <= algo_info->stretch_info.bottom_frame) {
                    TPD_INFO("Start stretch, the origin point is id:%d, x:%d, y:%d.\n",
                             i, points[i].x, points[i].y);

                    temp = point_buf->down_point.y - points[i].y;
                    temp = ((temp * algo_info->stretch_info.bottom_stretch_time) / (point_buf->touch_time + 1)) + 1;
                    points[i].y = point_buf->down_point.y + temp;
                    if (points[i].y >= ts->resolution_info.max_y) {
                        points[i].y = ts->resolution_info.max_y - 1;
                    }

                    point_buf->status &= ~BOTTOM_START;
                    TPD_INFO("Stretch point is y:%d time is %d.\n", points[i].y, point_buf->touch_time);
                } else if (point_buf->down_point.y < points[i].y
                           || point_buf->down_point.x != points[i].x
                           || point_buf->touch_time > algo_info->stretch_info.bottom_frame) {
                    point_buf->status &= ~BOTTOM_START;
                }

            }

            if (point_buf->status & LEFT_START) {
                if (point_buf->down_point.x  < points[i].x
                    && point_buf->touch_time <= algo_info->stretch_info.left_frame) {
                    TPD_INFO("Start stretch, the origin point is id:%d, x:%d, y:%d.\n",
                             i, points[i].x, points[i].y);
                    temp = points[i].x - point_buf->down_point.x;
                    temp = ((temp * algo_info->stretch_info.left_stretch_time) / (point_buf->touch_time + 1)) + 1;
                    if (point_buf->down_point.x < temp) {
                        temp = point_buf->down_point.x;
                    }
                    points[i].x = point_buf->down_point.x - temp;

                    point_buf->status &= ~LEFT_START;
                    TPD_INFO("Stretch point is x:%d time is %d.\n", points[i].x, point_buf->touch_time);
                } else if (point_buf->down_point.x > points[i].x
                           || point_buf->down_point.y != points[i].y
                           || point_buf->touch_time > algo_info->stretch_info.left_frame) {
                    point_buf->status &= ~LEFT_START;
                }

            }


            if (point_buf->status & RIGHT_START) {
                if (point_buf->down_point.x  > points[i].x
                    && point_buf->touch_time <= algo_info->stretch_info.right_frame) {
                    TPD_INFO("Start stretch, the origin point is id:%d, x:%d, y:%d.\n",
                             i, points[i].x, points[i].y);

                    temp = point_buf->down_point.x - points[i].x;
                    temp = ((temp * algo_info->stretch_info.right_stretch_time) / (point_buf->touch_time + 1)) + 1;
                    points[i].x = point_buf->down_point.x + temp;
                    if (points[i].x >= ts->resolution_info.max_x) {
                        points[i].x = ts->resolution_info.max_x - 1;
                    }

                    point_buf->status &= ~RIGHT_START;
                    TPD_INFO("Stretch point is x:%d time is %d.\n", points[i].x, point_buf->touch_time);
                } else if (point_buf->down_point.x < points[i].x
                           || point_buf->down_point.y != points[i].y
                           || point_buf->touch_time > algo_info->stretch_info.right_frame) {
                    point_buf->status &= ~RIGHT_START;
                }

            }

            if (point_buf->status != NORMAL) {
                points[i].status = 0;
                //continue; // let the main handle know finger down
            }

            obj_new = obj_new | (1 << i);


        }
    }

    return obj_new;

}

void switch_kalman_fun(struct touchpanel_data *ts, bool game_switch)
{
    if (ts->algo_info == NULL) {
        return;
    }
    if (ts->charger_pump_support&&ts->is_usb_checked) {
        ts->algo_info->kalman_info_used = &(ts->algo_info->charger_kalman_info);
    } else {
        ts->algo_info->kalman_info_used = &(ts->algo_info->kalman_info);
    }
    if (game_switch) {
        if (CHK_BIT(ts->algo_info->kalman_info_used->enable_bit, KALMAN_GAME_BIT)) {
            ts->algo_info->enable_kalman = true;
        } else {
            ts->algo_info->enable_kalman = false;
        }
    } else {

        if (CHK_BIT(ts->algo_info->kalman_info_used->enable_bit,(1 << ts->smooth_level))) {
            ts->algo_info->enable_kalman = true;
        } else {
            ts->algo_info->enable_kalman = false;
        }
    }
    if (ts->algo_info->enable_kalman == false) {
        int i = 0;
        for (i = 0; i < ts->max_num; i++) {
            ts->algo_info->point_buf[i].kal_x_last.x = INVALID_POINT;
        }
    }
    TPD_INFO("The kalman switch is %d.\n",ts->algo_info->enable_kalman);
}

int touch_algorithm_handle(struct touchpanel_data *ts, int obj_attention, struct point_info *points)
{
    int i = 0;
    int obj_new = 0;
    ktime_t calltime;
    ktime_t delta;
    unsigned long long time_ms = 0;
    struct touch_algorithm_info *algo_info;

    if (ts->algo_info == NULL) {
        return obj_attention;
    }
    algo_info = ts->algo_info;
    calltime = ktime_get();
    delta = ktime_sub(calltime, algo_info->last_time);
    time_ms = (unsigned long long) ktime_to_ms(delta);
    algo_info->last_time = calltime;
    TPD_DEBUG("delta time is %lld ms.\n", time_ms);
    if (time_ms > TIME_MAX) {
        time_ms = TIME_MAX;
    }
    for (i = 0; i < ts->max_num; i++) {
        struct algo_point_buf *point_buf = &algo_info->point_buf[i];
        if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01 && (points[i].status == 0)) // buf[0] == 0 is wrong point, no process
            continue;
        if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01 && (points[i].status != 0)) {

            if (point_buf->touch_time == 0) {
                init_touch_point(point_buf, &points[i]);

            } else {
                if (point_buf->touch_time < TIME_MAX) {
                    point_buf->touch_time = point_buf->touch_time + time_ms;
                }
                point_buf->delta_time = time_ms;
            }

        } else {
            if (point_buf->status != NORMAL) {// neep report click

                TPD_INFO("The point is up before stretch id:%d, status: %d.\n", i, point_buf->status);
                point_buf->status = CLICK_UP;

            } else {
                point_buf->touch_time = 0;
                point_buf->status = NORMAL;

            }

        }
    }

    obj_new = stretch_point_handle(ts, obj_attention, points);

    touch_algoithm_click_down(ts);

    if (algo_info->enable_kalman) {
        kalman_handle(ts, obj_new, points);
    }

    return obj_new;

}

void release_algorithm_points(struct touchpanel_data *ts)
{
    int i = 0;
    if (ts->algo_info == NULL) {
        return ;
    }

    for (i = 0; i < ts->max_num; i++) {
        ts->algo_info->point_buf[i].kal_x_last.x = INVALID_POINT;
        ts->algo_info->point_buf[i].touch_time = 0;
        ts->algo_info->point_buf[i].status = NORMAL;
    }
    TPD_INFO("Reset the algorithm points\n");
}


static ssize_t proc_stretch_config_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    uint8_t ret = 0;
    char *page;

    page = kzalloc(PROC_MAX_BUF+1, GFP_KERNEL);

    if (IS_ERR(page) || page == NULL) {
        return -EFAULT;
    }

    snprintf(page, PROC_MAX_BUF, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
             ts->algo_info->stretch_info.top_frame,
             ts->algo_info->stretch_info.top_stretch_time,
             ts->algo_info->stretch_info.top_open_dir,
             ts->algo_info->stretch_info.top_start,
             ts->algo_info->stretch_info.top_end,
             ts->algo_info->stretch_info.bottom_frame,
             ts->algo_info->stretch_info.bottom_stretch_time,
             ts->algo_info->stretch_info.bottom_open_dir,
             ts->algo_info->stretch_info.bottom_start,
             ts->algo_info->stretch_info.bottom_end,
             ts->algo_info->stretch_info.left_frame,
             ts->algo_info->stretch_info.left_stretch_time,
             ts->algo_info->stretch_info.left_open_dir,
             ts->algo_info->stretch_info.left_start,
             ts->algo_info->stretch_info.left_end,
             ts->algo_info->stretch_info.right_frame,
             ts->algo_info->stretch_info.right_stretch_time,
             ts->algo_info->stretch_info.right_open_dir,
             ts->algo_info->stretch_info.right_start,
             ts->algo_info->stretch_info.right_end);
    ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

    kfree(page);

    return ret;
}

static ssize_t proc_stretch_config_write(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    char *buffer;

    if (count > PROC_MAX_BUF) {
        TPD_INFO("%s:count is %d > %d\n", __func__, count, PROC_MAX_BUF);
        return count;
    }

    buffer = kzalloc(count+1, GFP_KERNEL);

    if (IS_ERR(buffer) || buffer == NULL) {
        return -EFAULT;
    }

    if (copy_from_user(buffer, buf, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        kfree(buffer);
        return count;
    }

    mutex_lock(&ts->mutex);
    if (20 == sscanf(buffer, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
                    &ts->algo_info->stretch_info.top_frame,
                    &ts->algo_info->stretch_info.top_stretch_time,
                    &ts->algo_info->stretch_info.top_open_dir,
                    &ts->algo_info->stretch_info.top_start,
                    &ts->algo_info->stretch_info.top_end,
                    &ts->algo_info->stretch_info.bottom_frame,
                    &ts->algo_info->stretch_info.bottom_stretch_time,
                    &ts->algo_info->stretch_info.bottom_open_dir,
                    &ts->algo_info->stretch_info.bottom_start,
                    &ts->algo_info->stretch_info.bottom_end,
                    &ts->algo_info->stretch_info.left_frame,
                    &ts->algo_info->stretch_info.left_stretch_time,
                    &ts->algo_info->stretch_info.left_open_dir,
                    &ts->algo_info->stretch_info.left_start,
                    &ts->algo_info->stretch_info.left_end,
                    &ts->algo_info->stretch_info.right_frame,
                    &ts->algo_info->stretch_info.right_stretch_time,
                    &ts->algo_info->stretch_info.right_open_dir,
                    &ts->algo_info->stretch_info.right_start,
                    &ts->algo_info->stretch_info.right_end)) {
    } else {
        TPD_DEBUG("invalid content: '%s', length = %zd\n", buffer, count);
    }
    mutex_unlock(&ts->mutex);

    TPD_INFO("Strecth cfg is : %u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u.\n",
             ts->algo_info->stretch_info.top_frame,
             ts->algo_info->stretch_info.top_stretch_time,
             ts->algo_info->stretch_info.top_open_dir,
             ts->algo_info->stretch_info.top_start,
             ts->algo_info->stretch_info.top_end,
             ts->algo_info->stretch_info.bottom_frame,
             ts->algo_info->stretch_info.bottom_stretch_time,
             ts->algo_info->stretch_info.bottom_open_dir,
             ts->algo_info->stretch_info.bottom_start,
             ts->algo_info->stretch_info.bottom_end,
             ts->algo_info->stretch_info.left_frame,
             ts->algo_info->stretch_info.left_stretch_time,
             ts->algo_info->stretch_info.left_open_dir,
             ts->algo_info->stretch_info.left_start,
             ts->algo_info->stretch_info.left_end,
             ts->algo_info->stretch_info.right_frame,
             ts->algo_info->stretch_info.right_stretch_time,
             ts->algo_info->stretch_info.right_open_dir,
             ts->algo_info->stretch_info.right_start,
             ts->algo_info->stretch_info.right_end);

    kfree(buffer);
    return count;
}


static const struct file_operations proc_stretch_config_ops = {
    .write = proc_stretch_config_write,
    .read  = proc_stretch_config_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_kalman_value_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    uint8_t ret = 0;
    char *page;

    page = kzalloc(PROC_MAX_BUF+1, GFP_KERNEL);

    if (IS_ERR(page) || page == NULL) {
        return -EFAULT;
    }
    snprintf(page, PROC_MAX_BUF, "%u,%u,%u,%u,0x%08x,%u,%u,%u,%u,0x%08x.\n",
             ts->algo_info->kalman_info.kalman_dis,
             ts->algo_info->kalman_info.min_dis,
             ts->algo_info->kalman_info.Q_Min,
             ts->algo_info->kalman_info.Q_Max,
             ts->algo_info->kalman_info.enable_bit,
             ts->algo_info->charger_kalman_info.kalman_dis,
             ts->algo_info->charger_kalman_info.min_dis,
             ts->algo_info->charger_kalman_info.Q_Min,
             ts->algo_info->charger_kalman_info.Q_Max,
             ts->algo_info->charger_kalman_info.enable_bit);
    ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

    kfree(page);

    return ret;
}

static ssize_t proc_kalman_value_write(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));

    char *buffer;
    if (count > PROC_MAX_BUF) {
        TPD_INFO("%s:count is %d > %d\n", __func__, count, PROC_MAX_BUF);
        return count;
    }

    buffer = kzalloc(count+1, GFP_KERNEL);

    if (IS_ERR(buffer) || buffer == NULL) {
        return -EFAULT;
    }

    if (copy_from_user(buffer, buf, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        kfree(buffer);
        return count;
    }

    mutex_lock(&ts->mutex);
    if (10 == sscanf(buffer, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
                    &ts->algo_info->kalman_info.kalman_dis,
                    &ts->algo_info->kalman_info.min_dis,
                    &ts->algo_info->kalman_info.Q_Min,
                    &ts->algo_info->kalman_info.Q_Max,
                    &ts->algo_info->kalman_info.enable_bit,
                    &ts->algo_info->charger_kalman_info.kalman_dis,
                    &ts->algo_info->charger_kalman_info.min_dis,
                    &ts->algo_info->charger_kalman_info.Q_Min,
                    &ts->algo_info->charger_kalman_info.Q_Max,
                    &ts->algo_info->charger_kalman_info.enable_bit)) {
    } else {
        TPD_DEBUG("invalid content: '%s', length = %zd\n", buffer, count);
    }

    if (ts->algo_info->kalman_info.Q_Max == 0
        || ts->algo_info->kalman_info.Q_Max > KALMAN_LEVEL) {
        ts->algo_info->kalman_info.Q_Max = KALMAN_LEVEL;

    }
    if (ts->algo_info->kalman_info.Q_Min > ts->algo_info->kalman_info.Q_Max) {
        ts->algo_info->kalman_info.Q_Max = ts->algo_info->kalman_info.Q_Min;
    }
    if (ts->algo_info->charger_kalman_info.Q_Max == 0
        || ts->algo_info->charger_kalman_info.Q_Max > KALMAN_LEVEL) {
        ts->algo_info->charger_kalman_info.Q_Max = KALMAN_LEVEL;

    }
    if (ts->algo_info->charger_kalman_info.Q_Min > ts->algo_info->charger_kalman_info.Q_Max) {
        ts->algo_info->charger_kalman_info.Q_Max = ts->algo_info->charger_kalman_info.Q_Min;
    }
    switch_kalman_fun(ts, ts->noise_level > 0);
    mutex_unlock(&ts->mutex);

    TPD_INFO("Kalman value is : %u,%u,%u,%u,0x%08x."
             "Charger kalman value is : %u,%u,%u,%u,0x%08x.\n",
             ts->algo_info->kalman_info.kalman_dis,
             ts->algo_info->kalman_info.min_dis,
             ts->algo_info->kalman_info.Q_Min,
             ts->algo_info->kalman_info.Q_Max,
             ts->algo_info->kalman_info.enable_bit,
             ts->algo_info->charger_kalman_info.kalman_dis,
             ts->algo_info->charger_kalman_info.min_dis,
             ts->algo_info->charger_kalman_info.Q_Min,
             ts->algo_info->charger_kalman_info.Q_Max,
             ts->algo_info->charger_kalman_info.enable_bit);

    kfree(buffer);
    return count;
}


static const struct file_operations proc_kalman_value_ops = {
    .write = proc_kalman_value_write,
    .read  = proc_kalman_value_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};


void set_algorithm_direction(struct touchpanel_data *ts, int dir)
{
    if (ts->algo_info == NULL) {
        return;
    }
    if (dir == 0) {
        ts->algo_info->phone_direction = BOTTOM_SIDE;
    } else if (dir == 1) {
        ts->algo_info->phone_direction = LEFT_SIDE;
    } else if (dir == 2) {
        ts->algo_info->phone_direction = RIGHT_SIDE;
    }
    TPD_INFO("phone_direction is %d.\n", ts->algo_info->phone_direction);
}


void oppo_touch_algorithm_init(struct touchpanel_data *ts)
{
    int rc = 0;
    struct proc_dir_entry *prEntry_tmp = NULL;
    struct touch_algorithm_info *algo_info = NULL;
    int err = 0;

    TPD_INFO("%s entry\n", __func__);
    if (of_property_read_bool(ts->dev->of_node, "algorithm_support") == false) {
        return;
    }

    algo_info = kzalloc(sizeof(struct touch_algorithm_info), GFP_KERNEL);
    if (!algo_info) {
        TPD_INFO("kzalloc touch algorithm info failed.\n");
        return;
    }
    // read config from dts

    // virturl key Related
    rc = of_property_read_u32_array(ts->dev->of_node,
                                    "algorithm_stretch_cfg",
                                    (uint32_t *)&algo_info->stretch_info,
                                    sizeof(struct algo_stretch_info) / sizeof(uint32_t));
    if (rc < 0) {
        TPD_INFO("error:algorithm_stretch_cfg should be setting in dts!");
        memset((void *)&algo_info->stretch_info, 0, sizeof(struct algo_stretch_info));
    }
    TPD_INFO("Strecth cfg is : %u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u.\n",
             algo_info->stretch_info.top_frame,
             algo_info->stretch_info.top_stretch_time,
             algo_info->stretch_info.top_open_dir,
             algo_info->stretch_info.top_start,
             algo_info->stretch_info.top_end,
             algo_info->stretch_info.bottom_frame,
             algo_info->stretch_info.bottom_stretch_time,
             algo_info->stretch_info.bottom_open_dir,
             algo_info->stretch_info.bottom_start,
             algo_info->stretch_info.bottom_end,
             algo_info->stretch_info.left_frame,
             algo_info->stretch_info.left_stretch_time,
             algo_info->stretch_info.left_open_dir,
             algo_info->stretch_info.left_start,
             algo_info->stretch_info.left_end,
             algo_info->stretch_info.right_frame,
             algo_info->stretch_info.right_stretch_time,
             algo_info->stretch_info.right_open_dir,
             algo_info->stretch_info.right_start,
             algo_info->stretch_info.right_end);

    rc = of_property_read_u32_array(ts->dev->of_node, "kalman_value",
                                    (uint32_t *)&algo_info->kalman_info,
                                    sizeof(struct algo_kalman_info) / sizeof(uint32_t));
    if (rc < 0) {
        TPD_INFO("error:algorithm kalman value not setting in dts!");
        algo_info->kalman_info.kalman_dis = 0;
    }
    if (algo_info->kalman_info.Q_Max == 0
        || algo_info->kalman_info.Q_Max > KALMAN_LEVEL) {
        algo_info->kalman_info.Q_Max = KALMAN_LEVEL;

    }
    if (algo_info->kalman_info.Q_Min > algo_info->kalman_info.Q_Max) {
        algo_info->kalman_info.Q_Max = algo_info->kalman_info.Q_Min;
    }
    TPD_INFO("Kalman value is %d,%d,%d,%d,0x%08x.\n",
             algo_info->kalman_info.kalman_dis,
             algo_info->kalman_info.min_dis,
             algo_info->kalman_info.Q_Min,
             algo_info->kalman_info.Q_Max,
             algo_info->kalman_info.enable_bit);

    rc = of_property_read_u32_array(ts->dev->of_node, "charger_kalman_value",
                                    (uint32_t *)&algo_info->charger_kalman_info,
                                    sizeof(struct algo_kalman_info) / sizeof(uint32_t));
    if (rc < 0) {
        TPD_INFO("error:algorithm charger kalman value not setting in dts, use default!");
        memcpy((void *)&algo_info->charger_kalman_info,
               (const void *)&algo_info->kalman_info,
               sizeof(struct algo_kalman_info));
    }
    if (algo_info->charger_kalman_info.Q_Max == 0
        || algo_info->charger_kalman_info.Q_Max > KALMAN_LEVEL) {
        algo_info->charger_kalman_info.Q_Max = KALMAN_LEVEL;

    }
    if (algo_info->charger_kalman_info.Q_Min > algo_info->charger_kalman_info.Q_Max) {
        algo_info->charger_kalman_info.Q_Max = algo_info->charger_kalman_info.Q_Min;
    }
    TPD_INFO("Charger kalman value is %d,%d,%d,%d,0x%08x.\n",
             algo_info->charger_kalman_info.kalman_dis,
             algo_info->charger_kalman_info.min_dis,
             algo_info->charger_kalman_info.Q_Min,
             algo_info->charger_kalman_info.Q_Max,
             algo_info->charger_kalman_info.enable_bit);

    algo_info->kalman_info_used = &algo_info->kalman_info;

    algo_info->point_buf = kzalloc(sizeof(struct algo_point_buf) * ts->max_num, GFP_KERNEL);

    algo_info->phone_direction = BOTTOM_SIDE;
    algo_info->last_time = ktime_get();

    if (algo_info->point_buf == NULL) {
        err = err | 0x01;
        goto OUT;
    }

    ts->algo_info = algo_info;

    release_algorithm_points(ts);

    switch_kalman_fun(ts, 0);

    if (ts->prEntry_tp == NULL) {
        TPD_INFO("No tp entry found!");
        return;
    }


    //proc files-step2-1:/proc/touchpanel/tp_debug_log_level (log control interface)
    prEntry_tmp = proc_create_data("stretch_config_set", 0666, ts->prEntry_tp, &proc_stretch_config_ops, ts);
    if (prEntry_tmp == NULL) {
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }
    prEntry_tmp = proc_create_data("kalman_value_set", 0666, ts->prEntry_tp, &proc_kalman_value_ops, ts);
    if (prEntry_tmp == NULL) {
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }
OUT:
    if (err) {
        if (algo_info) {
            if (algo_info->point_buf) {
                kfree(algo_info->point_buf);
            }
            kfree(algo_info);
            algo_info = NULL;
        }
    }

}


