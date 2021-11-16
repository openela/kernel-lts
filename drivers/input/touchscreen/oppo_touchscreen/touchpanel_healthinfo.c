// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "touchpanel_healthinfo.h"

#include <linux/err.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/ktime.h>
#include <linux/stacktrace.h>
#include <asm/stack_pointer.h>
#include <asm/stacktrace.h>
#include <asm/current.h>
#include <linux/version.h>

#include "touchpanel_common.h"
#include "touchpanel_prevention.h"

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


int upload_touchpanel_kevent_data(unsigned char *payload)
{
    return 0;
}

void reset_healthinfo_time_counter(u64 *time_counter)
{
    *time_counter = ktime_to_ms(ktime_get());
}
EXPORT_SYMBOL(reset_healthinfo_time_counter);

u64 check_healthinfo_time_counter_timeout(u64 time_counter, int ms)
{
    u64 curr = ktime_to_ms(ktime_get());

    if (curr < time_counter + ms) {
        return 0;
    } else {
        return (curr - time_counter);
    }
}

bool is_point_reporting(int obj_attention, struct point_info *points, int num)
{
    return (((obj_attention & TOUCH_BIT_CHECK) >> num) & 0x01 && (points[num].status != 0));
}

bool is_point_in_zone(struct list_head *zone_list, struct point_info cur_p, int direction)
{
    struct list_head *pos = NULL;
    struct grip_zone_area *grip_area = NULL;

    list_for_each(pos, zone_list) {
        grip_area = (struct grip_zone_area *)pos;

        if ((grip_area->support_dir >> direction) & 0x01) {
            if ((cur_p.x < grip_area->start_x + grip_area->x_width) && (cur_p.x > grip_area->start_x) &&
                (cur_p.y < grip_area->start_y + grip_area->y_width) && (cur_p.y > grip_area->start_y)) {
                return true;
            }
        }
    }

    return false;
}

int add_point_to_record(struct points_record *points_record, struct point_info point)
{
    int i = 0;
    //int length = sizeof(points_record->points) / sizeof(struct Coordinate);
    int length = points_record->count < RECORD_POINTS_COUNT - 1 ? points_record->count : RECORD_POINTS_COUNT - 1;
    for (i = length; i > 0; i--) {
        memcpy(&points_record->points[i], &points_record->points[i - 1], sizeof(struct Coordinate));
    }
    points_record->points[i].x = point.x;
    points_record->points[i].y = point.y;
    points_record->count++;

    return points_record->count;
}

int add_swipe_to_record(struct swipes_record *swipes_record, struct point_info start_point, struct point_info end_point)
{
    int i = 0;
    //int length = sizeof(swipes_record->start_points) / sizeof(struct Coordinate);
    int length = swipes_record->count < RECORD_POINTS_COUNT - 1 ? swipes_record->count : RECORD_POINTS_COUNT - 1;

    for (i = length; i > 0; i--) {
        memcpy(&swipes_record->start_points[i], &swipes_record->start_points[i - 1], sizeof(struct Coordinate));
        memcpy(&swipes_record->end_points[i], &swipes_record->end_points[i - 1], sizeof(struct Coordinate));
    }
    swipes_record->start_points[i].x = start_point.x;
    swipes_record->start_points[i].y = start_point.y;

    swipes_record->end_points[i].x = end_point.x;
    swipes_record->end_points[i].y = end_point.y;

    swipes_record->count++;

    return swipes_record->count;
}

int print_point_from_record(struct seq_file *s, struct points_record *points_record, char *prefix)
{
    int i = 0;
    int length = points_record->count < RECORD_POINTS_COUNT ? points_record->count : RECORD_POINTS_COUNT;

    if (!points_record->count) {
        return 0;
    }

    if (s) {
        seq_printf(s, "%spoints count:%d\n", prefix ? prefix : "", points_record->count);
    }
    TPD_DETAIL("%spoints count:%d\n", prefix ? prefix : "", points_record->count);

    for (i = 0; i < length; i++) {
        if (s) {
            seq_printf(s, "%spoint[%d]:[%d %d]\n", prefix ? prefix : "", i, points_record->points[i].x, points_record->points[i].y);
        }
        TPD_DETAIL("%spoint[%d]:[%d %d]\n", prefix ? prefix : "", i, points_record->points[i].x, points_record->points[i].y);
    }

    return 0;
}

int print_swipe_from_record(struct seq_file *s, struct swipes_record *swipes_record, char *prefix)
{
    int i = 0;
    int length = swipes_record->count < RECORD_POINTS_COUNT ? swipes_record->count : RECORD_POINTS_COUNT;

    if (!swipes_record->count) {
        return 0;
    }

    if (s) {
        seq_printf(s, "%sswipes count:%d\n", prefix ? prefix : "", swipes_record->count);
    }
    TPD_DETAIL("%sswipes count:%d\n", prefix ? prefix : "", swipes_record->count);
    for (i = 0; i < length; i++) {
        if (s) {
            seq_printf(s, "%sswipe[%d]:[%d %d] to [%d %d]\n", prefix ? prefix : "", i, swipes_record->start_points[i].x, swipes_record->start_points[i].y, swipes_record->end_points[i].x, swipes_record->end_points[i].y);
        }
        TPD_DETAIL("%sswipe[%d]:[%d %d] to [%d %d]\n", prefix ? prefix : "", i, swipes_record->start_points[i].x, swipes_record->start_points[i].y, swipes_record->end_points[i].x, swipes_record->end_points[i].y);
    }

    return 0;
}

int update_value_count_list (struct list_head *list, void *value, value_record_type value_type)
{
    struct list_head *pos = NULL;
    struct health_value_count *vc = NULL;
    char *value_str = (char *)value;
    int *value_int = (int *)value;
    int *vc_value = NULL;

    vc_value = kzalloc(sizeof(int), GFP_KERNEL);
    if(!vc_value) {
        return -1;
    }

    list_for_each(pos, list) {
        vc = (struct health_value_count *)pos;

        switch (value_type) {
        case TYPE_RECORD_INT:
            memcpy(vc_value, &vc->value, sizeof(int));
            if (vc->value_type == value_type && *vc_value == *value_int) {
                vc->count++;
                TPD_DETAIL("%s int=%d, count=%d\n", __func__, *vc_value, vc->count);
                kfree(vc_value);
                return vc->count;
            }
            break;
        case TYPE_RECORD_STR:
            if (vc->value_type == value_type && !strcmp((char *)vc->value, value_str)) {
                vc->count++;
                TPD_DETAIL("%s str=%s, count=%d\n", __func__, (char *)vc->value, vc->count);
                kfree(vc_value);
                return vc->count;
            }
            break;
        default:
            break;
        }
    }
    kfree(vc_value);

    vc = kzalloc(sizeof(struct health_value_count), GFP_KERNEL);

    if (vc) {
        switch (value_type) {
        case TYPE_RECORD_INT:
            vc->value = kzalloc(sizeof(int), GFP_KERNEL);
            if (vc->value) {
                memcpy(&vc->value, value_int, sizeof(int));
                vc->value_type = value_type;
                vc->count = 1;
                list_add_tail(&vc->head, list);
                TPD_DETAIL("%s int=%d, count=%d\n", __func__, *value_int, vc->count);
                return vc->count;
            } else {
                TPD_INFO("vc->value kzalloc failed.\n");
                kfree(vc);
                return -1;
            }
        case TYPE_RECORD_STR:
            vc->value = kzalloc(sizeof(char) * strlen(value_str), GFP_KERNEL);
            if (vc->value) {
                strncpy((char *)vc->value, value_str, strlen(value_str));
                vc->value_type = value_type;
                vc->count = 1;
                list_add_tail(&vc->head, list);
                TPD_DETAIL("%s str=%s, count=%d\n", __func__, (char *)vc->value, vc->count);
                return vc->count;
            } else {
                TPD_INFO("vc->value kzalloc failed.\n");
                kfree(vc);
                return -1;
            }
        default:
            break;
        }
    } else {
        TPD_INFO("kzalloc failed.\n");
        return -1;
    }

    return 0;
}

int print_value_count_list(struct seq_file *s, struct list_head *list, value_record_type value_type, char *prefix)
{
    struct list_head *pos = NULL;
    struct health_value_count *vc = NULL;
    int *vc_value = NULL;

    vc_value = kzalloc(sizeof(int), GFP_KERNEL);
    if(!vc_value) {
        return -1;
    }

    list_for_each(pos, list) {
        vc = (struct health_value_count *)pos;
        if (value_type == vc->value_type) {
            switch (value_type) {
            case TYPE_RECORD_INT:
                memcpy(vc_value, &vc->value, sizeof(int));
                if (s) {
                    seq_printf(s, "%s%d:%d\n", prefix ? prefix : "", *vc_value, vc->count);
                }
                TPD_DETAIL("%s%d:%d\n", prefix ? prefix : "", *vc_value, vc->count);
                break;
            case TYPE_RECORD_STR:
                if (s) {
                    seq_printf(s, "%s%s:%d\n", prefix ? prefix : "", (char *)vc->value, vc->count);
                }
                TPD_DETAIL("%s%s:%d\n", prefix ? prefix : "", (char *)vc->value, vc->count);
                break;
            default:
                break;
            }
        }
    }
    kfree(vc_value);

    return 0;
}

char *print_as_matrix(struct seq_file *s, void *value, int len, int linebreak, bool need_feedback)
{
    uint8_t *tmp_uint8 = NULL;
    char *tmp_str = NULL;
    char *tmp_str_child = NULL;
    char *tmp_str_feedback = NULL;
    int i = 0;

    if (!linebreak) {
        linebreak = DEFAULT_BUF_MATRIX_LINEBREAK;
    }

    tmp_uint8 = kzalloc(len, GFP_KERNEL);
    if (!tmp_uint8) {
        TPD_INFO("tmp_uint8 kzalloc failed.\n");
        goto out;
    }
    tmp_str = kzalloc(linebreak * DEFAULT_CHILD_STR_LEN, GFP_KERNEL);
    if (!tmp_str) {
        TPD_INFO("tmp_str kzalloc failed.\n");
        goto out;
    }
    tmp_str_child = kzalloc(DEFAULT_CHILD_STR_LEN, GFP_KERNEL);
    if (!tmp_str_child) {
        TPD_INFO("tmp_str_child kzalloc failed.\n");
        goto out;
    }
    if (need_feedback) {
        tmp_str_feedback = kzalloc(linebreak * DEFAULT_CHILD_STR_LEN, GFP_KERNEL);
    }

    memcpy(tmp_uint8, value, len);
    memset(tmp_str, 0, linebreak * DEFAULT_CHILD_STR_LEN);
    for (i = 0; i < len; i++) {
        memset(tmp_str_child, 0, DEFAULT_CHILD_STR_LEN);
        snprintf(tmp_str_child, DEFAULT_CHILD_STR_LEN, "0x%02x, ", tmp_uint8[i]);
        strcat(tmp_str, tmp_str_child);
        if (i % linebreak == linebreak - 1) {
            if (s) {
                seq_printf(s, "%s\n", tmp_str);
            }
            TPD_DETAIL("%s\n", tmp_str);
            if (tmp_str_feedback && (i / linebreak == 0)) {
                strncpy(tmp_str_feedback, tmp_str, strlen(tmp_str));
            }
            memset(tmp_str, 0, linebreak * DEFAULT_CHILD_STR_LEN);
        }
    }
    if (i % linebreak) {
        if (s) {
            seq_printf(s, "%s\n", tmp_str);
        }
        TPD_DETAIL("%s\n", tmp_str);
        if (tmp_str_feedback && (i / linebreak == 0)) {
            strncpy(tmp_str_feedback, tmp_str, strlen(tmp_str));
        }
    }

out:
    kfree(tmp_uint8);
    kfree(tmp_str);
    kfree(tmp_str_child);

    return tmp_str_feedback;
}

char *record_buffer_data(struct list_head *list, char *record,  uint16_t recordlen, int max_num)
{
    struct list_head *pos = NULL;
    struct health_value_count *vc = NULL;
    void *pre_str = NULL;
    void *next_str = NULL;
    char *feedback = NULL;
    int pre_count = 0;
    int next_count = 0;
    int list_num = 0;

    list_for_each(pos, list) {
        vc = (struct health_value_count *)pos;

        if (!list_num) {
            next_str = vc->value;
            vc->value = kzalloc(recordlen, GFP_KERNEL);
            if (vc->value) {
                memcpy(vc->value, record, recordlen);
                next_count = vc->count;
                vc->count = recordlen;
                TPD_DETAIL("len = %d\n", vc->count);
                feedback = print_as_matrix(NULL, vc->value, vc->count, DEFAULT_BUF_MATRIX_LINEBREAK, true);
            } else {
                TPD_INFO("vc->value kzalloc failed.\n");
                vc->value = next_str;
                return feedback;
            }
        } else {
            pre_str = next_str;
            next_str = vc->value;
            vc->value = pre_str;

            pre_count = next_count;
            next_count = vc->count;
            vc->count = pre_count;
            TPD_DETAIL("len = %d\n", vc->count);
            print_as_matrix(NULL, vc->value, vc->count, DEFAULT_BUF_MATRIX_LINEBREAK, false);
        }
        list_num++;
    }

    if (list_num < max_num) {
        vc = kzalloc(sizeof(struct health_value_count), GFP_KERNEL);

        if (vc) {
            if (!list_num) {
                vc->value = kzalloc(recordlen, GFP_KERNEL);
                if (vc->value) {
                    memcpy(vc->value, record, recordlen);
                    vc->count = recordlen;
                    TPD_DETAIL("len = %d\n", vc->count);
                    feedback = print_as_matrix(NULL, vc->value, vc->count, DEFAULT_BUF_MATRIX_LINEBREAK, true);
                } else {
                    TPD_INFO("vc->value kzalloc failed.\n");
                    kfree(vc);
                    return feedback;
                }
            } else {
                vc->value = next_str;
                vc->count = next_count;
                TPD_DETAIL("len = %d\n", vc->count);
                print_as_matrix(NULL, vc->value, vc->count, DEFAULT_BUF_MATRIX_LINEBREAK, false);
            }
            vc->value_type = TYPE_RECORD_STR;
            list_add_tail(&vc->head, list);
            list_num++;

        } else {
            TPD_INFO("kzalloc failed.\n");
            return feedback;
        }
    } else {
        kfree(next_str);
    }

    return feedback;
}

void print_buffer_list(struct seq_file *s, struct list_head *list, char *prefix)
{
    struct list_head *pos = NULL;
    struct health_value_count *vc = NULL;

    list_for_each(pos, list) {
        vc = (struct health_value_count *)pos;

        if (s) {
            seq_printf(s, "%slen=%d\n", prefix ? prefix : "", vc->count);
        }
        print_as_matrix(s, vc->value, vc->count, DEFAULT_BUF_MATRIX_LINEBREAK, false);
    }
}

void print_delta_data(struct seq_file *s, int32_t *delta_data, int TX_NUM, int RX_NUM)
{
    char *tmp_str = NULL;
    char *tmp_str_child = NULL;
    int i = 0, j = 0;

    if (!delta_data) {
        TPD_INFO("delta data hasn't alloc space.\n");
        return;
    }

    tmp_str = kzalloc(DEFAULT_CHILD_STR_LEN * (RX_NUM + 1), GFP_KERNEL);
    if (!tmp_str) {
        TPD_INFO("tmp_str kzaloc failed\n");
        return;
    }
    tmp_str_child = kzalloc(DEFAULT_CHILD_STR_LEN, GFP_KERNEL);
    if (!tmp_str_child) {
        TPD_INFO("tmp_str_child kzaloc failed\n");
        kfree(tmp_str);
        return;
    }

    for (i = 0; i < TX_NUM; i++) {
        memset(tmp_str, 0, DEFAULT_CHILD_STR_LEN * (RX_NUM + 1));
        memset(tmp_str_child, 0, DEFAULT_CHILD_STR_LEN);
        snprintf(tmp_str_child, DEFAULT_CHILD_STR_LEN, "[%2d]", i);
        strcat(tmp_str, tmp_str_child);

        for (j = 0; j < RX_NUM; j++) {
            memset(tmp_str_child, 0, DEFAULT_CHILD_STR_LEN);
            snprintf(tmp_str_child, DEFAULT_CHILD_STR_LEN, "%4d, ", delta_data[RX_NUM * i + j]);
            strcat(tmp_str, tmp_str_child);
        }

        if (s) {
            seq_printf(s, "%s", tmp_str);
        }
        TPD_DETAIL("%s\n", tmp_str);
    }
    if (s) {
        seq_printf(s, "\n");
    }

    kfree(tmp_str);
    kfree(tmp_str_child);
}

bool is_delta_data_allzero (int32_t *delta_data, int TX_NUM, int RX_NUM)
{
    int i = 0;

    for (i = 0; i < TX_NUM * RX_NUM; i++) {
        if (delta_data[i]) {
            return false;
        }
    }

    return true;
}

int catch_delta_data(struct monitor_data_v2 *monitor_data, int32_t *delta_data)
{
    struct debug_info_proc_operations *debug_info_ops = monitor_data->debug_info_ops;

    if (!tp_debug) {
        return 0;
    }

    if (!debug_info_ops) {
        TPD_INFO("debug info procs is not supported.\n");
        return -1;
    }

    if (!debug_info_ops->get_delta_data) {
        TPD_INFO("catching delta data is not supported.\n");
        return -1;
    }

    if (!delta_data) {
        TPD_INFO("delta data hasn't alloc space.\n");
        return -1;
    }

    memset(delta_data, 0, sizeof(int32_t) * monitor_data->TX_NUM * monitor_data->RX_NUM);
    debug_info_ops->get_delta_data(monitor_data->chip_data, delta_data);

    print_delta_data(NULL, delta_data, monitor_data->TX_NUM, monitor_data->RX_NUM);

    return 0;
}

int point_state_up_handle(struct monitor_data_v2 *monitor_data, int i, int direction)
{
    if (!monitor_data->points_state) {
        return -1;
    }

    if (monitor_data->points_state[i].down_up_state == STATE_DOWN) {
        monitor_data->points_state[i].down_up_state = STATE_UP;

        if (monitor_data->points_state[i].max_swipe_distance_sq == 0) {
            monitor_data->points_state[i].touch_action = ACTION_CLICK;
            monitor_data->click_count++;
            monitor_data->click_count_array[monitor_data->points_state[i].last_point.y * CLICK_COUNT_ARRAY_HEIGHT / monitor_data->max_y * CLICK_COUNT_ARRAY_WIDTH +
                                                                                       monitor_data->points_state[i].last_point.x * CLICK_COUNT_ARRAY_WIDTH / monitor_data->max_x]++;
        } else {
            monitor_data->points_state[i].touch_action = ACTION_SWIPE;
            monitor_data->swipe_count++;
        }

        if (!monitor_data->kernel_grip_support) {
            if (is_point_in_zone(&monitor_data->dead_zone_list, monitor_data->points_state[i].first_point, direction)
                && is_point_in_zone(&monitor_data->dead_zone_list, monitor_data->points_state[i].last_point, direction)
                && !monitor_data->points_state[i].is_down_handled) {//grip dead zone2
                add_point_to_record(&monitor_data->dead_zone_points, monitor_data->points_state[i].first_point);
                TPD_DETAIL("dead zone [%d %d] / %d.\n", monitor_data->points_state[i].first_point.x, monitor_data->points_state[i].first_point.y, monitor_data->dead_zone_points.count);
            }
            //grip elimination 2
            if (direction == VERTICAL_SCREEN) {
                if (monitor_data->elizone_point_tophalf_i == i) {
                    add_point_to_record(&monitor_data->elimination_zone_points,  monitor_data->points_state[i].first_point);
                    TPD_DETAIL("eli zone [%d %d] / %d.\n",  monitor_data->points_state[i].first_point.x,  monitor_data->points_state[i].first_point.y, monitor_data->elimination_zone_points.count);
                    monitor_data->elizone_point_tophalf_i = -1;
                }
                if (monitor_data->elizone_point_bothalf_i == i) {
                    add_point_to_record(&monitor_data->elimination_zone_points,  monitor_data->points_state[i].first_point);
                    TPD_DETAIL("eli zone [%d %d] / %d.\n",  monitor_data->points_state[i].first_point.x,  monitor_data->points_state[i].first_point.y, monitor_data->elimination_zone_points.count);
                    monitor_data->elizone_point_bothalf_i = -1;
                }
            } else {
                if (monitor_data->elizone_point_tophalf_i == i) {
                    add_point_to_record(&monitor_data->lanscape_elimination_zone_points,  monitor_data->points_state[i].first_point);
                    TPD_DETAIL("lans eli zone [%d %d] / %d.\n",  monitor_data->points_state[i].first_point.x,  monitor_data->points_state[i].first_point.y, monitor_data->lanscape_elimination_zone_points.count);
                    monitor_data->elizone_point_tophalf_i = -1;
                }
                if (monitor_data->elizone_point_bothalf_i == i) {
                    add_point_to_record(&monitor_data->lanscape_elimination_zone_points,  monitor_data->points_state[i].first_point);
                    TPD_DETAIL("lans eli zone [%d %d] / %d.\n",  monitor_data->points_state[i].first_point.x,  monitor_data->points_state[i].first_point.y, monitor_data->lanscape_elimination_zone_points.count);
                    monitor_data->elizone_point_bothalf_i = -1;
                }
            }
        }
        monitor_data->points_state[i].max_swipe_distance_sq = 0;
        monitor_data->points_state[i].last_swipe_distance_sq = 0;
        reset_healthinfo_time_counter(&monitor_data->points_state[i].time_counter);
    }

    return 0;
}

int tp_touch_healthinfo_handle(struct monitor_data_v2 *monitor_data, int obj_attention, struct point_info *points, int finger_num, int max_num, int direction)
{
    int i = 0, j = 0;
    int points_handled = 0;
    int x_dist = 0, y_dist = 0;
    unsigned long dist_sq = 0;
    bool is_first_point = false;
    bool is_jumping_point = false;

    int elizone_points_tophalf_count = 0;
    int elizone_points_bothalf_count = 0;
    int points_tophalf_count = 0;
    int points_bothalf_count = 0;
    bool need_record_eli_point_tophalf = false;
    bool need_record_eli_point_bothalf = false;
    int elizone_point_tophalf_i = -1;
    int elizone_point_bothalf_i = -1;

    u64 touch_time = 0;

    //char *report = NULL;

    if (!monitor_data || !monitor_data->points_state || (!points && finger_num)) {
        return -1;
    }

    if (finger_num) {
        while (points_handled < finger_num && i < max_num) {
            is_first_point = false;
            is_jumping_point = false;
            if (monitor_data->points_state[i].down_up_state == STATE_DOWN && !is_point_reporting(obj_attention, points, i)) {
                point_state_up_handle(monitor_data, i, direction);
            } else if (is_point_reporting(obj_attention, points, i)) {
                if (monitor_data->points_state[i].down_up_state == STATE_UP) {
                    is_first_point = true;
                    monitor_data->points_state[i].jumping_times++;
                    monitor_data->points_state[i].down_up_state = STATE_DOWN;
                    memcpy(&monitor_data->points_state[i].first_point, &points[i], sizeof(struct point_info));

                    x_dist = points[i].x - monitor_data->points_state[i].last_point.x;
                    y_dist = points[i].y - monitor_data->points_state[i].last_point.y;
                    dist_sq = x_dist * x_dist + y_dist * y_dist;
                    if(monitor_data->points_state[i].touch_action == ACTION_SWIPE && dist_sq < monitor_data->swipe_broken_judge_distance * monitor_data->swipe_broken_judge_distance
                       && !check_healthinfo_time_counter_timeout(monitor_data->points_state[i].time_counter, monitor_data->in_game_mode ?
                               (SWIPE_BROKEN_FRAMES_MS / monitor_data->report_rate_in_game) : (SWIPE_BROKEN_FRAMES_MS / monitor_data->report_rate))) {//up and down in short time(2.5 frames) and short distance. judge as broken swipe
                        //add_swipe_to_record(&monitor_data->broken_swipes, monitor_data->points_state[i].last_point, points[i]);
                        monitor_data->broken_swipes_count_array[points[i].y * (monitor_data->RX_NUM / 2) / monitor_data->max_y * (monitor_data->TX_NUM / 2) +
                                                                                       points[i].x * (monitor_data->TX_NUM / 2) / monitor_data->max_x]++;
                        TPD_DETAIL("broken swipe:[%d %d] to [%d %d].\n", monitor_data->points_state[i].first_point.x, monitor_data->points_state[i].first_point.y,
                                   monitor_data->points_state[i].last_point.x, monitor_data->points_state[i].last_point.y);
                    }
                    if(monitor_data->points_state[i].touch_action == ACTION_CLICK && dist_sq < monitor_data->jumping_point_judge_distance * monitor_data->jumping_point_judge_distance
                       && !check_healthinfo_time_counter_timeout(monitor_data->points_state[i].time_counter, monitor_data->in_game_mode ?
                               (JUMPING_POINT_FRAMES_MS / monitor_data->report_rate_in_game) : (JUMPING_POINT_FRAMES_MS / monitor_data->report_rate))) {//time between last click and this down less than 1.5 frames, and distance less than 20px, judge as jumping point
                        is_jumping_point = true;
                    }

                    reset_healthinfo_time_counter(&monitor_data->points_state[i].time_counter);
                }

                if (!(monitor_data->points_state[i].last_point.x == points[i].x && monitor_data->points_state[i].last_point.y == points[i].y)) {
                    if (!is_first_point) {
                        x_dist = points[i].x - monitor_data->points_state[i].last_point.x;
                        y_dist = points[i].y - monitor_data->points_state[i].last_point.y;
                        dist_sq = x_dist * x_dist + y_dist * y_dist;
                        if (dist_sq > monitor_data->points_state[i].last_swipe_distance_sq
                            && int_sqrt(dist_sq) - int_sqrt(monitor_data->points_state[i].last_swipe_distance_sq) > monitor_data->long_swipe_judge_distance) {//distance between points changed sunddently when swiping
                            add_swipe_to_record(&monitor_data->long_swipes, monitor_data->points_state[i].last_point, points[i]);
                            TPD_DETAIL("long swipe:[%d %d] to [%d %d], dis:%d to %d.\n", monitor_data->points_state[i].last_point.x, monitor_data->points_state[i].last_point.y,
                                    points[i].x, points[i].y, int_sqrt(monitor_data->points_state[i].last_swipe_distance_sq), int_sqrt(dist_sq));
                        }
                        monitor_data->points_state[i].last_swipe_distance_sq = dist_sq;

                        x_dist = points[i].x - monitor_data->points_state[i].first_point.x;
                        y_dist = points[i].y - monitor_data->points_state[i].first_point.y;
                        dist_sq = x_dist * x_dist + y_dist * y_dist;
                        if (dist_sq > monitor_data->points_state[i].max_swipe_distance_sq) {
                            monitor_data->points_state[i].max_swipe_distance_sq = dist_sq;
                        }

                        reset_healthinfo_time_counter(&monitor_data->points_state[i].time_counter);
                    } else {
                        if (!is_jumping_point) {
                            monitor_data->points_state[i].is_down_handled = false;
                        }
                    }

                    memcpy(&monitor_data->points_state[i].last_point, &points[i], sizeof(struct point_info));
                    if (!is_jumping_point) {
                        monitor_data->points_state[i].jumping_times = 1;
                    }
                }

                if (monitor_data->points_state[i].jumping_times > JUMPING_POINT_TIMES) {
                    if (!monitor_data->points_state[i].is_down_handled) {
                        //add_point_to_record(&monitor_data->jumping_points, points[i]);
                        monitor_data->jumping_points_count_array[points[i].y * (monitor_data->RX_NUM / 2) / monitor_data->max_y * (monitor_data->TX_NUM / 2) +
                                                                                       points[i].x * (monitor_data->TX_NUM / 2) / monitor_data->max_x]++;
                        catch_delta_data(monitor_data, monitor_data->jumping_point_delta_data);//catch delta data
                        monitor_data->points_state[i].is_down_handled = true;
                    }
                    if (monitor_data->points_state[i].jumping_times > monitor_data->max_jumping_times) {
                        monitor_data->max_jumping_times = monitor_data->points_state[i].jumping_times;
                        TPD_DETAIL("the %d th time down.\n", monitor_data->max_jumping_times);
                    }
                }

                if (!monitor_data->points_state[i].is_down_handled && check_healthinfo_time_counter_timeout(monitor_data->points_state[i].time_counter, STUCK_POINT_TIME)) {
                    if (direction == VERTICAL_SCREEN) {
                        //add_point_to_record(&monitor_data->stuck_points, points[i]);
                        monitor_data->stuck_points_count_array[points[i].y * (monitor_data->RX_NUM / 2) / monitor_data->max_y * (monitor_data->TX_NUM / 2) +
                                                                                       points[i].x * (monitor_data->TX_NUM / 2) / monitor_data->max_x]++;
                        TPD_DETAIL("stuck point:[%d %d]\n", points[i].x, points[i].y);
                    } else {
                        //add_point_to_record(&monitor_data->lanscape_stuck_points, points[i]);
                        monitor_data->lanscape_stuck_points_count_array[points[i].y * (monitor_data->RX_NUM / 2) / monitor_data->max_y * (monitor_data->TX_NUM / 2) +
                                                                                       points[i].x * (monitor_data->TX_NUM / 2) / monitor_data->max_x]++;
                        TPD_DETAIL("lanscape stuck point:[%d %d]\n", points[i].x, points[i].y);
                    }
                    catch_delta_data(monitor_data, monitor_data->stuck_point_delta_data);//coordinate not change over 15s, catch delta data
                    /*
                    report = kzalloc(DEFAULT_REPORT_STR_LEN, GFP_KERNEL);
                    if (report) {
                        snprintf(report, DEFAULT_REPORT_STR_LEN, "StuckPoint$$Count@@%d$$Coor@@[%d %d]", monitor_data->stuck_points.count, points[i].x, points[i].y);
                        upload_touchpanel_kevent_data(report);
                        kfree(report);
                    } else {
                        TPD_INFO("alloc report kzalloc failed.\n");
                    }
                    */
                    monitor_data->points_state[i].is_down_handled = true;
                }
                if (!monitor_data->kernel_grip_support) {
                    //grip judge
                    if (!monitor_data->points_state[i].is_down_handled && !is_point_in_zone(&monitor_data->dead_zone_list, points[i], direction)
                        && is_point_in_zone(&monitor_data->dead_zone_list, monitor_data->points_state[i].first_point, direction)) {//grip dead zone 1
                        monitor_data->points_state[i].is_down_handled = true;
                        TPD_DETAIL("swipe from dead zone[%d %d] to [%d %d].\n", monitor_data->points_state[i].first_point.x, monitor_data->points_state[i].first_point.y, points[i].x, points[i].y);
                    } else if (monitor_data->points_state[i].max_swipe_distance_sq == 0 && !monitor_data->points_state[i].is_down_handled
                               && is_point_in_zone(&monitor_data->condition_zone_list, points[i], direction)
                               && check_healthinfo_time_counter_timeout(monitor_data->points_state[i].time_counter, LONG_CLICK_TIME)) {//grip condition zone long press(>400ms)
                        add_point_to_record(&monitor_data->condition_zone_points, points[i]);
                        TPD_DETAIL("condition zone [%d %d] / %d.\n", points[i].x, points[i].y, monitor_data->condition_zone_points.count);
                        monitor_data->points_state[i].is_down_handled = true;
                    } else if (is_point_in_zone(&monitor_data->elimination_zone_list, points[i], direction)) {//grip elimination 1
                        if (points[i].y < monitor_data->max_y / 2) {
                            elizone_points_tophalf_count++;
                            if (!monitor_data->points_state[i].is_down_handled && !need_record_eli_point_tophalf
                                && monitor_data->points_state[i].max_swipe_distance_sq == 0
                                && monitor_data->elizone_point_tophalf_i < 0) {
                                need_record_eli_point_tophalf = true;
                                elizone_point_tophalf_i = i;
                            }
                        } else {
                            elizone_points_bothalf_count++;
                            if (!monitor_data->points_state[i].is_down_handled && !need_record_eli_point_bothalf
                                && monitor_data->points_state[i].max_swipe_distance_sq == 0
                                && monitor_data->elizone_point_bothalf_i < 0) {
                                need_record_eli_point_bothalf = true;
                                elizone_point_bothalf_i = i;
                            }
                        }
                    } else {
                        if (monitor_data->elizone_point_tophalf_i == i) {
                            monitor_data->elizone_point_tophalf_i = -1;
                        }
                        if (monitor_data->elizone_point_bothalf_i == i) {
                            monitor_data->elizone_point_bothalf_i = -1;
                        }
                    }
                    if (points[i].y < monitor_data->max_y / 2) {
                        points_tophalf_count++;
                    } else {
                        points_bothalf_count++;
                    }
                }
                points_handled++;
            }
            i++;
        }

        if (!monitor_data->kernel_grip_support) {
            //grip elimination 2
            if (direction == VERTICAL_SCREEN) {
                if ((elizone_points_tophalf_count || elizone_points_bothalf_count)
                    && elizone_points_tophalf_count + elizone_points_bothalf_count < finger_num) {
                    if (need_record_eli_point_tophalf) {
                        //add_point_to_record(&monitor_data->elimination_zone_points, points[elizone_point_tophalf_i]);
                        //TPD_DETAIL("eli zone [%d %d] / %d.\n", points[elizone_point_tophalf_i].x, points[elizone_point_tophalf_i].y, monitor_data->elimination_zone_points.count);
                        monitor_data->elizone_point_tophalf_i = elizone_point_tophalf_i;
                        monitor_data->points_state[elizone_point_tophalf_i].is_down_handled = true;
                    }
                    if (need_record_eli_point_bothalf) {
                        //add_point_to_record(&monitor_data->elimination_zone_points, points[elizone_point_bothalf_i]);
                        //TPD_DETAIL("eli zone [%d %d] / %d.\n", points[elizone_point_bothalf_i].x, points[elizone_point_bothalf_i].y, monitor_data->elimination_zone_points.count);
                        monitor_data->elizone_point_bothalf_i = elizone_point_bothalf_i;
                        monitor_data->points_state[elizone_point_bothalf_i].is_down_handled = true;
                    }
                }
            } else {
                if (elizone_points_tophalf_count && elizone_points_tophalf_count < points_tophalf_count && need_record_eli_point_tophalf) {
                    //add_point_to_record(&monitor_data->lanscape_elimination_zone_points, points[elizone_point_tophalf_i]);
                    //TPD_DETAIL("lans eli zone [%d %d] / %d.\n", points[elizone_point_tophalf_i].x, points[elizone_point_tophalf_i].y, monitor_data->lanscape_elimination_zone_points.count);
                    monitor_data->elizone_point_tophalf_i = elizone_point_tophalf_i;
                    monitor_data->points_state[elizone_point_tophalf_i].is_down_handled = true;
                }
                if (elizone_points_bothalf_count && elizone_points_bothalf_count < points_bothalf_count && need_record_eli_point_bothalf) {
                    //add_point_to_record(&monitor_data->lanscape_elimination_zone_points, points[elizone_point_bothalf_i]);
                    //TPD_DETAIL("lans eli zone [%d %d] / %d.\n", points[elizone_point_bothalf_i].x, points[elizone_point_bothalf_i].y, monitor_data->lanscape_elimination_zone_points.count);
                    monitor_data->elizone_point_bothalf_i = elizone_point_bothalf_i;
                    monitor_data->points_state[elizone_point_bothalf_i].is_down_handled = true;
                }
            }
        }

        if (monitor_data->in_game_mode) {
            if (finger_num > monitor_data->max_touch_num_in_game) {
                monitor_data->max_touch_num_in_game = finger_num;
            }
        } else {
            if (finger_num > monitor_data->max_touch_num) {
                monitor_data->max_touch_num = finger_num;
            }
        }
    }

    if (finger_num != monitor_data->current_touch_num) {
        if (monitor_data->current_touch_num) {
            touch_time = check_healthinfo_time_counter_timeout(monitor_data->touch_timer, 0);
            if (monitor_data->in_game_mode) {
                monitor_data->total_touch_time_in_game[0] += touch_time;
                monitor_data->total_touch_time_in_game[monitor_data->current_touch_num] += touch_time;
            } else {
                monitor_data->total_touch_time += touch_time;
            }
            monitor_data->holding_touch_time += touch_time;
            if (!finger_num) {
                if (monitor_data->holding_touch_time > monitor_data->max_holding_touch_time) {
                    monitor_data->max_holding_touch_time = monitor_data->holding_touch_time;
                }
                monitor_data->holding_touch_time = 0;
            }
        }
        reset_healthinfo_time_counter(&monitor_data->touch_timer);
        monitor_data->current_touch_num = finger_num;
    }

    for (j = i; j < max_num; j++) {
        point_state_up_handle(monitor_data, j, direction);
    }

    return 0;
}

int tp_gesture_healthinfo_handle(struct monitor_data_v2 *monitor_data, int gesture_type)
{
    if (!monitor_data) {
        return 0;
    }

    if (monitor_data->is_gesture_waiting_resume) {
        update_value_count_list(&monitor_data->invalid_gesture_values_list, &monitor_data->gesture_waiting, TYPE_RECORD_INT);
    }
    monitor_data->is_gesture_waiting_resume = true;
    reset_healthinfo_time_counter(&monitor_data->gesture_received_time);
    monitor_data->gesture_waiting = gesture_type;

    return 0;
}

int tp_fingerprint_healthinfo_handle(struct monitor_data_v2 *monitor_data, uint8_t area_rate)
{
    int rate_rate = 0;

    if (!monitor_data || !area_rate) {
        return 0;
    }

    rate_rate =  area_rate / 10;

    return update_value_count_list(&monitor_data->fp_area_rate_list, &rate_rate, TYPE_RECORD_INT);;
}

int tp_face_detect_healthinfo_handle(struct monitor_data_v2 *monitor_data, int ps_state)
{
    if (!ps_state) {
        return 0;
    }
    return update_value_count_list(&monitor_data->fd_values_list, &ps_state, TYPE_RECORD_INT);
}

int tp_report_healthinfo_handle(struct monitor_data_v2 *monitor_data, char *report)
{
    int ret = 0;

    if (!monitor_data) {
        return 0;
    }

    ret = update_value_count_list(&monitor_data->health_report_list, report, TYPE_RECORD_STR);

    return ret;
}

void reset_healthinfo_grip_time_record(void *tp_monitor_data, void *tp_grip_info)
{
    struct monitor_data_v2 *monitor_data = (struct monitor_data_v2 *)tp_monitor_data;
    struct kernel_grip_info *grip_info = (struct kernel_grip_info *)tp_grip_info;

    if (!monitor_data || !grip_info) {
        return;
    }

    if (monitor_data->grip_time_record_flag == TYPE_START_RECORD) {
        monitor_data->total_grip_time_no_touch += ktime_to_ms(ktime_get()) - monitor_data->grip_start_time_no_touch;
        monitor_data->grip_time_record_flag = TYPE_END_RECORD;
    }

    grip_info->obj_bit_rcd = 0;
    grip_info->obj_prced_bit_rcd = 0;
}

int tp_grip_healthinfo_handle(struct monitor_data_v2 *monitor_data, struct kernel_grip_info *grip_info)
{
    int ret = 0;
    int obj_attention_raw = 0;
    int obj_attention = 0;
    u64 delta_time = 0;

    if (!monitor_data || !grip_info) {
        return 0;
    }

    obj_attention_raw = grip_info->obj_bit_rcd;
    obj_attention = grip_info->obj_prced_bit_rcd;

    if (obj_attention_raw != 0 && obj_attention == 0) { // touch is all suppressed by grip function
        if (monitor_data->grip_time_record_flag != TYPE_START_RECORD) {
            monitor_data->grip_start_time_no_touch = ktime_to_ms(ktime_get());
            monitor_data->grip_time_record_flag = TYPE_START_RECORD;
        }
    } else if (obj_attention != 0 || obj_attention_raw == 0) {
        if (monitor_data->grip_time_record_flag == TYPE_START_RECORD) {
            delta_time = ktime_to_ms(ktime_get()) - monitor_data->grip_start_time_no_touch;
            monitor_data->total_grip_time_no_touch += delta_time;
            if (delta_time >= MS_PER_SECOND) {
                monitor_data->total_grip_time_no_touch_one_sec += delta_time;
            }
            if (delta_time >= 2 * MS_PER_SECOND) {
                monitor_data->total_grip_time_no_touch_two_sec += delta_time;
            }
            if (delta_time >= 3 * MS_PER_SECOND) {
                monitor_data->total_grip_time_no_touch_three_sec += delta_time;
            }
            if (delta_time >= 5 * MS_PER_SECOND) {
                monitor_data->total_grip_time_no_touch_five_sec += delta_time;
            }
            monitor_data->grip_time_record_flag = TYPE_END_RECORD;
        }
    }

    return ret;
}

int tp_probe_healthinfo_handle(struct monitor_data_v2 *monitor_data, u64 start_time)
{
    if (!monitor_data) {
        return 0;
    }

    monitor_data->boot_time = start_time;
    monitor_data->probe_time = check_healthinfo_time_counter_timeout(start_time, 0);

    reset_healthinfo_time_counter(&monitor_data->screenon_timer);

    return 0;
}

int update_max_time(struct monitor_data_v2 *monitor_data, u64 *max_time, u64 start_time)
{
    u64 time_cost = 0;

    if (!monitor_data) {
        return 0;
    }

    time_cost = check_healthinfo_time_counter_timeout(start_time, 0);
    if (time_cost > *max_time) {
        *max_time = time_cost;
    }

    return 0;
}

int tp_resume_healthinfo_handle(struct monitor_data_v2 *monitor_data, u64 start_time)
{
    if (!monitor_data) {
        return 0;
    }

    if (monitor_data->is_gesture_waiting_resume) {
        if (check_healthinfo_time_counter_timeout(monitor_data->gesture_received_time, GESTURE_RESPONSE_TIME)) {
            update_value_count_list(&monitor_data->invalid_gesture_values_list, &monitor_data->gesture_waiting, TYPE_RECORD_INT);
        } else {
            update_value_count_list(&monitor_data->gesture_values_list, &monitor_data->gesture_waiting, TYPE_RECORD_INT);
        }
        monitor_data->is_gesture_waiting_resume = false;
    }

    update_max_time(monitor_data, &monitor_data->max_resume_time, start_time);

    reset_healthinfo_time_counter(&monitor_data->screenon_timer);

    return 0;
}

int tp_suspend_healthinfo_handle(struct monitor_data_v2 *monitor_data, u64 start_time)
{
    u64 screenon_time = 0;

    if (!monitor_data) {
        return 0;
    }

    tp_touch_healthinfo_handle(monitor_data, 0, monitor_data->touch_points, 0, monitor_data->max_finger_support, monitor_data->direction);

    update_max_time(monitor_data, &monitor_data->max_suspend_time, start_time);

    screenon_time = monitor_data->screenon_timer ? check_healthinfo_time_counter_timeout(monitor_data->screenon_timer, 0) : 0;

    monitor_data->total_screenon_time += screenon_time;
    monitor_data->screenon_timer = 0;

    return 0;
}

int tp_test_healthinfo_handle(struct monitor_data_v2 *monitor_data, healthinfo_type test_type, int error_count)
{
    int ret = 0;

    if (!monitor_data) {
        return 0;
    }

    switch (test_type) {
    case HEALTH_TEST_AUTO:
        monitor_data->auto_test_total_times++;
        if (error_count) {
            monitor_data->auto_test_failed_times++;
        }
        ret = monitor_data->auto_test_failed_times;

        TPD_INFO("TEST FAILED RATE:%d/%d.\n", monitor_data->auto_test_failed_times, monitor_data->auto_test_total_times);
        break;
    case HEALTH_TEST_BLACKSCREEN:
        monitor_data->blackscreen_test_total_times++;
        if (error_count) {
            monitor_data->blackscreen_test_failed_times++;
        }
        ret = monitor_data->blackscreen_test_failed_times;

        TPD_INFO("TEST FAILED RATE:%d/%d.\n", monitor_data->blackscreen_test_failed_times, monitor_data->blackscreen_test_total_times);
        break;
    default:
        break;
    }

    return ret;
}

int tp_bus_err_healthinfo_handle(struct monitor_data_v2 *monitor_data, int errno, unsigned char *writebuf, uint16_t writelen)
{
    char *report = NULL;
    char *buff_part = NULL;

    if (!monitor_data || errno >= 0 || !writebuf) {
        return 0;
    }

    buff_part = record_buffer_data(&monitor_data->bus_errs_buff_list, writebuf, writelen, MAX_BUS_ERROR_BUFF_RECORD);

    if (buff_part) {
        report = kzalloc(DEFAULT_REPORT_STR_LEN + strlen(buff_part), GFP_KERNEL);
        if (report) {
            snprintf(report, DEFAULT_REPORT_STR_LEN, "BusErr$$Errno@@%d$$Buff@@%s", errno, buff_part);
            upload_touchpanel_kevent_data(report);
            kfree(report);
        } else {
            TPD_INFO("alloc report kzalloc failed.\n");
        }
        kfree(buff_part);
    }

    return update_value_count_list(&monitor_data->bus_errs_list, &errno, TYPE_RECORD_INT);
}

int tp_alloc_healthinfo_handle(struct monitor_data_v2 *monitor_data, long alloc_size, bool alloc_success)
{
    int ret = 0;
    int deep = 2;
    char *report = NULL;
    char *func = NULL;
    struct stackframe frame;

    if (!monitor_data) {
        return 0;
    }

    if (alloc_success) {
        monitor_data->alloced_size = monitor_data->alloced_size + alloc_size;
        TPD_DETAIL("%ld(%ld) bytes has been alloced.\n", monitor_data->alloced_size, alloc_size);
    } else {
        if (!monitor_data->max_alloc_err_size && !monitor_data->min_alloc_err_size) {
            monitor_data->max_alloc_err_size = alloc_size;
            monitor_data->min_alloc_err_size = alloc_size;
        } else if (alloc_size > monitor_data->max_alloc_err_size) {
            monitor_data->max_alloc_err_size = alloc_size;
        } else if (alloc_size < monitor_data->min_alloc_err_size) {
            monitor_data->min_alloc_err_size = alloc_size;
        }
        TPD_INFO("alloc_err_size [%ld - %ld].\n", monitor_data->min_alloc_err_size, monitor_data->max_alloc_err_size);

        frame.fp = (unsigned long)__builtin_frame_address(0);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,9,0)
        frame.sp = current_stack_pointer;
#endif
        frame.pc = (unsigned long)tp_alloc_healthinfo_handle;
        while (deep) {
#if IS_BUILTIN(CONFIG_TOUCHPANEL_OPPO)
            ret = unwind_frame(current, &frame);
#else
            ret = -1;
#endif
            if (ret < 0) {
                return ret;
            }
            deep--;
        }
        report = kzalloc(DEFAULT_REPORT_STR_LEN, GFP_KERNEL);
        if (report) {
            TPD_INFO("%pS alloc %ld failed\n", (void *)frame.pc, alloc_size);
            snprintf(report, DEFAULT_REPORT_STR_LEN, "AllocErr$$Func@@%pS$$Size@@%ld", (void *)frame.pc, alloc_size);
            upload_touchpanel_kevent_data(report);

            func = kzalloc(DEFAULT_REPORT_STR_LEN, GFP_KERNEL);
            if (func) {
                memset(report, 0, DEFAULT_REPORT_STR_LEN);
                snprintf(report, DEFAULT_REPORT_STR_LEN, "%pS", (void *)frame.pc);
                strncpy(func, report, strstr(report, "+") - report);
                update_value_count_list(&monitor_data->alloc_err_funcs_list, func, TYPE_RECORD_STR);
            }
            kfree(report);
            kfree(func);
        } else {
            TPD_INFO("alloc report kzalloc failed.\n");
        }
    }

    return ret;
}

int tp_fw_update_healthinfo_handle(struct monitor_data_v2 *monitor_data, char *result)
{
    int ret = 0;
    char *report = NULL;

    if (!monitor_data) {
        return 0;
    }

    ret = update_value_count_list(&monitor_data->fw_update_result_list, result, TYPE_RECORD_STR);

    if (!strstr(result, FIRMWARE_UPDATE_SUCCESS_KEYWORD)) {
        report = kzalloc(DEFAULT_REPORT_STR_LEN, GFP_KERNEL);
        if (report) {
            snprintf(report, DEFAULT_REPORT_STR_LEN, "FwUpdateErr$$ErrMsg@@%s", result);
            upload_touchpanel_kevent_data(report);
            kfree(report);
        }
    }

    return ret;
}

int tp_healthinfo_report(void *tp_monitor_data, healthinfo_type type, void *value)
{
    int ret = 0;
    uint8_t *value_uint8 = (uint8_t *)value;
    char *value_str = (char *)value;
    int *value_int = (int *)value;
    long *value_long = (long *)value;
    u64 *value_u64 = (u64 *)value;
    struct kernel_grip_info *grip_info = (struct kernel_grip_info *)value;
    struct monitor_data_v2 *monitor_data = (struct monitor_data_v2 *)tp_monitor_data;

    if (!monitor_data || !monitor_data->health_monitor_support) {
        return 0;
    }

    switch(type) {
    case HEALTH_TOUCH:
        if (monitor_data->touch_points) {
            ret = tp_touch_healthinfo_handle(monitor_data, *value_int, monitor_data->touch_points, monitor_data->touch_num, monitor_data->max_finger_support, monitor_data->direction);
            monitor_data->touch_points = NULL;
        }
        break;
    case HEALTH_GESTURE:
        ret = tp_gesture_healthinfo_handle(monitor_data, *value_int);
        break;
    case HEALTH_FINGERPRINT:
        ret = tp_fingerprint_healthinfo_handle(monitor_data, *value_uint8);
        break;
    case HEALTH_FACE_DETECT:
        ret = tp_face_detect_healthinfo_handle(monitor_data, *value_int);
        break;
    case HEALTH_REPORT:
        ret = tp_report_healthinfo_handle(monitor_data, value_str);
        break;
    case HEALTH_PROBE:
        ret = tp_probe_healthinfo_handle(monitor_data, *value_u64);
        break;
    case HEALTH_RESUME:
        ret = tp_resume_healthinfo_handle(monitor_data, *value_u64);
        break;
    case HEALTH_SUSPEND:
        ret = tp_suspend_healthinfo_handle(monitor_data, *value_u64);
        break;
    case HEALTH_TEST_AUTO:
    case HEALTH_TEST_BLACKSCREEN:
        ret = tp_test_healthinfo_handle(monitor_data, type, *value_int);
        break;
    case HEALTH_BUS:
        if (monitor_data->bus_buf) {
            ret = tp_bus_err_healthinfo_handle(monitor_data, *value_int, monitor_data->bus_buf, monitor_data->bus_len);
            monitor_data->bus_buf = NULL;
        }
        break;
    case HEALTH_ALLOC_SUCCESS:
        ret = tp_alloc_healthinfo_handle(monitor_data, *value_long, true);
        break;
    case HEALTH_ALLOC_FAILED:
        ret = tp_alloc_healthinfo_handle(monitor_data, *value_long, false);
        break;
    case HEALTH_FW_UPDATE:
        ret = tp_fw_update_healthinfo_handle(monitor_data, value_str);
        break;
    case HEALTH_FW_UPDATE_COST:
        ret = update_max_time(monitor_data, &monitor_data->max_fw_update_time, *value_u64);
        break;
    case HEALTH_GRIP:
        ret = tp_grip_healthinfo_handle(monitor_data, grip_info);
        break;
    default:
        break;
    }

    return ret;
}
EXPORT_SYMBOL(tp_healthinfo_report);

int tp_healthinfo_read(struct seq_file *s, void *tp_monitor_data)
{
    struct list_head *pos = NULL;
    struct health_value_count *vc = NULL;
    struct monitor_data_v2 *monitor_data = (struct monitor_data_v2 *)tp_monitor_data;
    int *vc_value = NULL;
    u64 screenon_time = 0;

    if (!monitor_data->health_monitor_support) {
        seq_printf(s, "health monitor not supported\n");
        return 0;
    }

    if (monitor_data->tp_ic) {
        if (monitor_data->vendor) {
            seq_printf(s, "tp_ic:%s+%s\n", monitor_data->tp_ic, monitor_data->vendor);
        } else {
            seq_printf(s, "tp_ic:%s\n", monitor_data->tp_ic);
        }
    } else {
        seq_printf(s, "tp_ic:%s\n", TPD_DEVICE);
    }
    seq_printf(s, "boot_time:%llds\n", check_healthinfo_time_counter_timeout(monitor_data->boot_time, 0) / MS_PER_SECOND);
    seq_printf(s, "probe_time:%lldms\n", monitor_data->probe_time);
    seq_printf(s, "max_resume_time:%lldms\n", monitor_data->max_resume_time);
    seq_printf(s, "max_suspend_time:%lldms\n", monitor_data->max_suspend_time);

    //touch time rate
    screenon_time = monitor_data->screenon_timer ? check_healthinfo_time_counter_timeout(monitor_data->screenon_timer, 0) : 0;
    seq_printf(s, "total_screen_on_time:%llds\n", (monitor_data->total_screenon_time + screenon_time) / MS_PER_SECOND);
    seq_printf(s, "total_touch_time:%llds\n", monitor_data->total_touch_time / MS_PER_SECOND);
    if (monitor_data->max_touch_num_in_game) {
        seq_printf(s, "total_touch_time(in_game):%lld[%lld %lld %lld %lld %lld %lld %lld %lld %lld %lld]s\n",
                   monitor_data->total_touch_time_in_game[0] / MS_PER_SECOND, monitor_data->total_touch_time_in_game[1] / MS_PER_SECOND, monitor_data->total_touch_time_in_game[2] / MS_PER_SECOND,
                   monitor_data->total_touch_time_in_game[3] / MS_PER_SECOND, monitor_data->total_touch_time_in_game[4] / MS_PER_SECOND, monitor_data->total_touch_time_in_game[5] / MS_PER_SECOND,
                   monitor_data->total_touch_time_in_game[6] / MS_PER_SECOND, monitor_data->total_touch_time_in_game[7] / MS_PER_SECOND, monitor_data->total_touch_time_in_game[8] / MS_PER_SECOND,
                   monitor_data->total_touch_time_in_game[9] / MS_PER_SECOND, monitor_data->total_touch_time_in_game[10] / MS_PER_SECOND);
    }
    seq_printf(s, "max_holding_touch_time:%llds\n", monitor_data->max_holding_touch_time / MS_PER_SECOND);
    seq_printf(s, "max_touch_number:%d\n", monitor_data->max_touch_num);
    if (monitor_data->max_touch_num_in_game) {
        seq_printf(s, "max_touch_number(in_game):%d\n", monitor_data->max_touch_num_in_game);
    }
    seq_printf(s, "panel_coords:%d*%d\n", monitor_data->max_x, monitor_data->max_y);
    seq_printf(s, "tx_rx_num:%d*%d\n", monitor_data->TX_NUM, monitor_data->RX_NUM);
    seq_printf(s, "click_count:%d", monitor_data->click_count);
    print_delta_data(s, monitor_data->click_count_array, CLICK_COUNT_ARRAY_HEIGHT, CLICK_COUNT_ARRAY_WIDTH);
    seq_printf(s, "swipe_count:%d\n", monitor_data->swipe_count);

    //firmware update
    if (monitor_data->fw_version) {
        seq_printf(s, "fw_version:%s\n" "max_fw_update_time:%lldms\n",
                   monitor_data->fw_version, monitor_data->max_fw_update_time);
    }
    print_value_count_list(s, &monitor_data->fw_update_result_list, TYPE_RECORD_STR, PREFIX_FW_UPDATE_RESULT);

    //bus transfer
    print_value_count_list(s, &monitor_data->bus_errs_list, TYPE_RECORD_INT, PREFIX_BUS_TRANS_ERRNO);
    print_buffer_list(s, &monitor_data->bus_errs_buff_list, PREFIX_BUS_TRANS_ERRBUF);

    //alloc
    seq_printf(s, "alloced_size:%ld\n", monitor_data->alloced_size);
    if (monitor_data->min_alloc_err_size) {
        seq_printf(s, "alloc_error_size:%ld~%ld\n", monitor_data->min_alloc_err_size, monitor_data->max_alloc_err_size);
        print_value_count_list(s, &monitor_data->alloc_err_funcs_list, TYPE_RECORD_STR, PREFIX_ALLOC_ERR_FUNC);
    }

    //debug info
    print_value_count_list(s, &monitor_data->health_report_list, TYPE_RECORD_STR, PREFIX_HEALTH_REPORT);

    if (!monitor_data->kernel_grip_support) {
        //grip
        print_point_from_record(s, &monitor_data->dead_zone_points, PREFIX_GRIP_DEAD_ZONE);
        print_point_from_record(s, &monitor_data->condition_zone_points, PREFIX_GRIP_CONDIT_ZONE);
        print_point_from_record(s, &monitor_data->elimination_zone_points, PREFIX_GRIP_ELI_ZONE);
        print_point_from_record(s, &monitor_data->lanscape_elimination_zone_points, PREFIX_GRIP_LANS_ELI_ZONE);
    } else {
        //grip time
        seq_printf(s, "total_grip_time_no_touch:%llds\n", monitor_data->total_grip_time_no_touch / MS_PER_SECOND);
        seq_printf(s, "total_grip_time_no_touch_one_sec:%llds\n", monitor_data->total_grip_time_no_touch_one_sec / MS_PER_SECOND);
        seq_printf(s, "total_grip_time_no_touch_two_sec:%llds\n", monitor_data->total_grip_time_no_touch_two_sec / MS_PER_SECOND);
        seq_printf(s, "total_grip_time_no_touch_three_sec:%llds\n", monitor_data->total_grip_time_no_touch_three_sec / MS_PER_SECOND);
        seq_printf(s, "total_grip_time_no_touch_five_sec:%llds\n", monitor_data->total_grip_time_no_touch_five_sec / MS_PER_SECOND);
    }

    //abnormal touch and swipe
    if (monitor_data->max_jumping_times > JUMPING_POINT_TIMES) {
        seq_printf(s, "max_point-jumping_times:%d\n", monitor_data->max_jumping_times);
        //print_point_from_record(s, &monitor_data->jumping_points, PREFIX_POINT_JUMPING);
        if (!is_delta_data_allzero(monitor_data->jumping_points_count_array, monitor_data->RX_NUM / 2, monitor_data->TX_NUM / 2)) {
            seq_printf(s, "%spoint:\n", PREFIX_POINT_JUMPING);
            print_delta_data(s, monitor_data->jumping_points_count_array, monitor_data->RX_NUM / 2, monitor_data->TX_NUM / 2);
        }
        if (!is_delta_data_allzero(monitor_data->jumping_point_delta_data, monitor_data->TX_NUM, monitor_data->RX_NUM)) {
            print_delta_data(s, monitor_data->jumping_point_delta_data, monitor_data->TX_NUM, monitor_data->RX_NUM);
        }
    }

    //print_point_from_record(s, &monitor_data->stuck_points, PREFIX_POINT_STUCK);
    //print_point_from_record(s, &monitor_data->lanscape_stuck_points, PREFIX_POINT_LANSCAPE_STUCK);
    if (!is_delta_data_allzero(monitor_data->stuck_points_count_array, monitor_data->RX_NUM / 2, monitor_data->TX_NUM / 2)) {
        seq_printf(s, "%spoint:", PREFIX_POINT_STUCK);
        print_delta_data(s, monitor_data->stuck_points_count_array, monitor_data->RX_NUM / 2, monitor_data->TX_NUM / 2);
    }
    if (!is_delta_data_allzero(monitor_data->lanscape_stuck_points_count_array, monitor_data->RX_NUM / 2, monitor_data->TX_NUM / 2)) {
        seq_printf(s, "%spoint:", PREFIX_POINT_LANSCAPE_STUCK);
        print_delta_data(s, monitor_data->lanscape_stuck_points_count_array, monitor_data->RX_NUM / 2, monitor_data->TX_NUM / 2);
    }
    if (!is_delta_data_allzero(monitor_data->stuck_point_delta_data, monitor_data->TX_NUM, monitor_data->RX_NUM)) {
        seq_printf(s, "stuck-point:");
        print_delta_data(s, monitor_data->stuck_point_delta_data, monitor_data->TX_NUM, monitor_data->RX_NUM);
    }

    if (!is_delta_data_allzero(monitor_data->broken_swipes_count_array, monitor_data->RX_NUM / 2, monitor_data->TX_NUM / 2)) {
        seq_printf(s, "%sswipe:", PREFIX_SWIPE_BROKER);
        print_delta_data(s, monitor_data->broken_swipes_count_array, monitor_data->RX_NUM / 2, monitor_data->TX_NUM / 2);
    }
    //print_swipe_from_record(s, &monitor_data->broken_swipes, PREFIX_SWIPE_BROKER);
    print_swipe_from_record(s, &monitor_data->long_swipes, PREFIX_SWIPE_SUDDNT_LONG);

    vc_value = kzalloc(sizeof(int), GFP_KERNEL);
    if(vc_value) {
        //black gesture
        list_for_each(pos, &monitor_data->gesture_values_list) {
            vc = (struct health_value_count *)pos;
            memcpy(vc_value, &vc->value, sizeof(int));
            seq_printf(s, "%s%s:%d\n", PREFIX_GESTURE, *vc_value == DouTap ? "double tap" :
                       *vc_value == UpVee ? "up vee" :
                       *vc_value == DownVee ? "down vee" :
                       *vc_value == LeftVee ? "(>)" :
                       *vc_value == RightVee ? "(<)" :
                       *vc_value == Circle ? "circle" :
                       *vc_value == DouSwip ? "(||)" :
                       *vc_value == Left2RightSwip ? "(-->)" :
                       *vc_value == Right2LeftSwip ? "(<--)" :
                       *vc_value == Up2DownSwip ? "up to down |" :
                       *vc_value == Down2UpSwip ? "down to up |" :
                       *vc_value == Mgestrue ? "(M)" :
                       *vc_value == FingerprintDown ? "(fingerprintdown)" :
                       *vc_value == FingerprintUp ? "(fingerprintup)" :
                       *vc_value == SingleTap ? "single tap" : "unknown", vc->count);
        }

        list_for_each(pos, &monitor_data->invalid_gesture_values_list) {
            vc = (struct health_value_count *)pos;
            memcpy(vc_value, &vc->value, sizeof(int));
            seq_printf(s, "%s%s:%d\n", PREFIX_GESTURE_INVLID, *vc_value == DouTap ? "double tap" :
                       *vc_value == UpVee ? "up vee" :
                       *vc_value == DownVee ? "down vee" :
                       *vc_value == LeftVee ? "(>)" :
                       *vc_value == RightVee ? "(<)" :
                       *vc_value == Circle ? "circle" :
                       *vc_value == DouSwip ? "(||)" :
                       *vc_value == Left2RightSwip ? "(-->)" :
                       *vc_value == Right2LeftSwip ? "(<--)" :
                       *vc_value == Up2DownSwip ? "up to down |" :
                       *vc_value == Down2UpSwip ? "down to up |" :
                       *vc_value == Mgestrue ? "(M)" :
                       *vc_value == Wgestrue ? "(W)" :
                       *vc_value == FingerprintDown ? "(fingerprintdown)" :
                       *vc_value == FingerprintUp ? "(fingerprintup)" :
                       *vc_value == SingleTap ? "single tap" : "unknown", vc->count);
        }
        kfree(vc_value);
    }
    //fingerprint area rate
    print_value_count_list(s, &monitor_data->fp_area_rate_list, TYPE_RECORD_INT, PREFIX_FP_AREA_RATE);

    //face detect
    print_value_count_list(s, &monitor_data->fd_values_list, TYPE_RECORD_INT, PREFIX_FD);

    //auto test
    if (monitor_data->auto_test_total_times) {
        seq_printf(s, "auto_test_failed_rate:%d/%d\n", monitor_data->auto_test_failed_times, monitor_data->auto_test_total_times);
    }
    if (monitor_data->blackscreen_test_total_times) {
        seq_printf(s, "blackscreen_test_failed_rate:%d/%d\n", monitor_data->blackscreen_test_failed_times, monitor_data->blackscreen_test_total_times);
    }

    seq_printf(s, "--last_exception--\n");

    return 0;
}

int init_grip_zone(struct device *dev, struct monitor_data_v2 *monitor_data)
{
    int ret = 0;
    struct grip_zone_area *grip_zone = NULL;
    int dead_width[2] = {0}, cond_width[4] = {0}, eli_width[4] = {0};

    if (!monitor_data) {
        TPD_INFO("monitor_data is NULL.\n");
        return -1;
    }

    monitor_data->elizone_point_tophalf_i = -1;
    monitor_data->elizone_point_bothalf_i = -1;

    ret = of_property_read_u32_array(dev->of_node, "prevention,dead_area_width", dead_width, 2);
    if (ret) {
        dead_width[0] = 10;
        dead_width[1] = 10;
        TPD_INFO("panel coords using default.\n");
    }

    ret = of_property_read_u32_array(dev->of_node, "prevention,condition_area_width", cond_width, 4);
    if (ret) {
        cond_width[0] = 30;
        cond_width[1] = 30;
        cond_width[2] = 100;
        cond_width[3] = 80;
        TPD_INFO("condition area width using default.\n");
    }

    ret = of_property_read_u32_array(dev->of_node, "prevention,eli_area_width", eli_width, 4);
    if (ret) {
        eli_width[0] = 80;
        eli_width[1] = 500;
        eli_width[2] = 600;
        eli_width[3] = 120;
        TPD_INFO("eli area width using default.\n");
    }

    //dead zone grip init
    INIT_LIST_HEAD(&monitor_data->dead_zone_list);
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = 0;
        grip_zone->x_width = dead_width[0];
        grip_zone->y_width = monitor_data->max_y;
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "ver_left_dead");
        grip_zone->grip_side = 1 << TYPE_LONG_SIDE;
        grip_zone->support_dir = (1 << VERTICAL_SCREEN) | (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &monitor_data->dead_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for ver_left_dead failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = monitor_data->max_x - dead_width[0];
        grip_zone->start_y = 0;
        grip_zone->x_width = dead_width[0];
        grip_zone->y_width = monitor_data->max_y;
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "ver_right_dead");
        grip_zone->grip_side = 1 << TYPE_LONG_SIDE;
        grip_zone->support_dir = (1 << VERTICAL_SCREEN) | (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &monitor_data->dead_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for ver_right_dead failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = 0;
        grip_zone->x_width = monitor_data->max_x;
        grip_zone->y_width = dead_width[1];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_left_dead");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &monitor_data->dead_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_left_dead failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = monitor_data->max_y - dead_width[1];
        grip_zone->x_width = monitor_data->max_x;
        grip_zone->y_width = dead_width[1];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_right_dead");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &monitor_data->dead_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_right_dead failed.\n");
    }

    //condition grip init
    INIT_LIST_HEAD(&monitor_data->condition_zone_list);
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = 0;
        grip_zone->x_width = cond_width[0];
        grip_zone->y_width = monitor_data->max_y;
        grip_zone->exit_thd = cond_width[2];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "ver_left_condtion");
        grip_zone->grip_side = 1 << TYPE_LONG_SIDE;
        grip_zone->support_dir = (1 << VERTICAL_SCREEN) | (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &monitor_data->condition_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for coord_buffer failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = monitor_data->max_x - cond_width[0];
        grip_zone->start_y = 0;
        grip_zone->x_width = cond_width[0];
        grip_zone->y_width = monitor_data->max_y;
        grip_zone->exit_thd = cond_width[2];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "ver_right_condtion");
        grip_zone->grip_side = 1 << TYPE_LONG_SIDE;
        grip_zone->support_dir = (1 << VERTICAL_SCREEN) | (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &monitor_data->condition_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for ver_right_condtion failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = 0;
        grip_zone->x_width = monitor_data->max_x;
        grip_zone->y_width = cond_width[1];
        grip_zone->exit_thd = cond_width[3];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_left_condtion");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &monitor_data->condition_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_left_condtion failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = monitor_data->max_y - cond_width[1];
        grip_zone->x_width = monitor_data->max_x;
        grip_zone->y_width = cond_width[1];
        grip_zone->exit_thd = cond_width[3];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_right_condtion");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &monitor_data->condition_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_right_condtion failed.\n");
    }

    //elimination grip init
    INIT_LIST_HEAD(&monitor_data->elimination_zone_list);
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = monitor_data->max_y - eli_width[1];
        grip_zone->x_width = eli_width[0];
        grip_zone->y_width = eli_width[1];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "ver_left_eli");
        grip_zone->grip_side = 1 << TYPE_LONG_SIDE;
        grip_zone->support_dir = 1 << VERTICAL_SCREEN;
        list_add_tail(&grip_zone->area_list, &monitor_data->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for ver_left_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = monitor_data->max_x - eli_width[0];
        grip_zone->start_y = monitor_data->max_y - eli_width[1];
        grip_zone->x_width = eli_width[0];
        grip_zone->y_width = eli_width[1];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "ver_right_eli");
        grip_zone->grip_side = 1 << TYPE_LONG_SIDE;
        grip_zone->support_dir = 1 << VERTICAL_SCREEN;
        list_add_tail(&grip_zone->area_list, &monitor_data->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for ver_right_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = 0;
        grip_zone->x_width = eli_width[2];
        grip_zone->y_width = eli_width[3];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_90_left_0_eli");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = 1 << LANDSCAPE_SCREEN_90;
        list_add_tail(&grip_zone->area_list, &monitor_data->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_90_left_0_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = 0;
        grip_zone->x_width = eli_width[3];
        grip_zone->y_width = eli_width[2];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_90_left_1_eli");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = 1 << LANDSCAPE_SCREEN_90;
        list_add_tail(&grip_zone->area_list, &monitor_data->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_90_left_1_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = monitor_data->max_y - eli_width[3];
        grip_zone->x_width = eli_width[2];
        grip_zone->y_width = eli_width[3];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_90_right_0_eli");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = 1 << LANDSCAPE_SCREEN_90;
        list_add_tail(&grip_zone->area_list, &monitor_data->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_90_right_0_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = monitor_data->max_y - eli_width[2];
        grip_zone->x_width = eli_width[3];
        grip_zone->y_width = eli_width[2];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_90_right_1_eli");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = 1 << LANDSCAPE_SCREEN_90;
        list_add_tail(&grip_zone->area_list, &monitor_data->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_90_right_1_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = monitor_data->max_x - eli_width[2];
        grip_zone->start_y = 0;
        grip_zone->x_width = eli_width[2];
        grip_zone->y_width = eli_width[3];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_270_left_0_eli");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = 1 << LANDSCAPE_SCREEN_270;
        list_add_tail(&grip_zone->area_list, &monitor_data->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_270_left_0_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = monitor_data->max_x - eli_width[3];
        grip_zone->start_y = 0;
        grip_zone->x_width = eli_width[3];
        grip_zone->y_width = eli_width[2];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_270_left_1_eli");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = 1 << LANDSCAPE_SCREEN_270;
        list_add_tail(&grip_zone->area_list, &monitor_data->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_270_left_1_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = monitor_data->max_x - eli_width[2];
        grip_zone->start_y = monitor_data->max_y - eli_width[3];
        grip_zone->x_width = eli_width[2];
        grip_zone->y_width = eli_width[3];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_270_right_0_eli");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = 1 << LANDSCAPE_SCREEN_270;
        list_add_tail(&grip_zone->area_list, &monitor_data->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_270_right_0_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = monitor_data->max_x - eli_width[3];
        grip_zone->start_y = monitor_data->max_y - eli_width[2];
        grip_zone->x_width = eli_width[3];
        grip_zone->y_width = eli_width[2];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_270_right_1_eli");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = 1 << LANDSCAPE_SCREEN_270;
        list_add_tail(&grip_zone->area_list, &monitor_data->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_270_right_1_eli failed.\n");
    }

    return 0;
}

int tp_healthinfo_init(struct device *dev, void *tp_monitor_data)
{
    int ret = 0;
    int temp_array[2] = {0};
    struct monitor_data_v2 *monitor_data = (struct monitor_data_v2 *)tp_monitor_data;

    if (!monitor_data) {
        TPD_INFO("monitor_data is NULL.\n");
        return -1;
    }

    ret = of_property_read_u32(dev->of_node, "touchpanel,max-num-support", &monitor_data->max_finger_support);
    if (ret) {
        TPD_INFO("monitor_data->max_finger_support not specified\n");
        monitor_data->max_finger_support = 10;
    }

    ret = of_property_read_u32_array(dev->of_node, "touchpanel,tx-rx-num", temp_array, 2);
    if (ret) {
        TPD_INFO("tx-rx-num not set\n");
        monitor_data->TX_NUM = 0;
        monitor_data->RX_NUM = 0;
    } else {
        monitor_data->TX_NUM = temp_array[0];
        monitor_data->RX_NUM = temp_array[1];
    }

    ret = of_property_read_u32_array(dev->of_node, "touchpanel,panel-coords", temp_array, 2);
    if (ret) {
        monitor_data->max_x = 1080;
        monitor_data->max_y = 2340;
        TPD_INFO("panel coords using default.\n");
    } else {
        monitor_data->max_x = temp_array[0];
        monitor_data->max_y = temp_array[1];
    }
    monitor_data->long_swipe_judge_distance = int_sqrt(monitor_data->max_x * monitor_data->max_x + monitor_data->max_y * monitor_data->max_y) / LONG_SWIPE_JUDGE_RATIO;
    monitor_data->swipe_broken_judge_distance = int_sqrt(monitor_data->max_x * monitor_data->max_x + monitor_data->max_y * monitor_data->max_y) / SWIPE_BROKEN_JUDGE_RATIO;
    monitor_data->jumping_point_judge_distance = int_sqrt(monitor_data->max_x * monitor_data->max_x + monitor_data->max_y * monitor_data->max_y) / JUMPING_POINT_JUDGE_RATIO;
    TPD_INFO("long_swipe_judge_distance=%d swipe_broken_judge_distance=%d jumping_point_judge_distance=%d\n",
            monitor_data->long_swipe_judge_distance, monitor_data->swipe_broken_judge_distance, monitor_data->jumping_point_judge_distance);

    ret = of_property_read_u32_array(dev->of_node, "touchpanel,report-rate", temp_array, 2);
    if (ret) {
        monitor_data->report_rate = 120;
        monitor_data->report_rate_in_game = 120;
        TPD_INFO("report rate using default.\n");
    } else {
        monitor_data->report_rate = temp_array[0];
        monitor_data->report_rate_in_game = temp_array[1];
        TPD_INFO("report rate %d-%d.\n", monitor_data->report_rate, monitor_data->report_rate_in_game);
    }

    ret = of_property_read_string(dev->of_node, "chip-name", &monitor_data->tp_ic);
    if (ret) {
        TPD_INFO("failed to get tp ic\n");
    }

    monitor_data->kernel_grip_support = of_property_read_bool(dev->of_node, "kernel_grip_support");

    monitor_data->fw_version = kzalloc(MAX_DEVICE_VERSION_LENGTH, GFP_KERNEL);
    if (!monitor_data->fw_version) {
        TPD_INFO("kzalloc fw_version failed.\n");
        ret = -1;
        goto err;
    }

    monitor_data->jumping_point_delta_data = kzalloc(sizeof(int32_t) * monitor_data->TX_NUM * monitor_data->RX_NUM, GFP_KERNEL);
    if (!monitor_data->jumping_point_delta_data) {
        TPD_INFO("kzalloc jumping_point_delta_data failed.\n");
        ret = -1;
        goto err;
    }

    monitor_data->stuck_point_delta_data = kzalloc(sizeof(int32_t) * monitor_data->TX_NUM * monitor_data->RX_NUM, GFP_KERNEL);
    if (!monitor_data->stuck_point_delta_data) {
        TPD_INFO("kzalloc stuck_point_delta_data failed.\n");
        ret = -1;
        goto err;
    }

    monitor_data->points_state = kzalloc(sizeof(struct point_state_monitor) * monitor_data->max_finger_support, GFP_KERNEL);
    if (!monitor_data->points_state) {
        TPD_INFO("kzalloc points_state failed.\n");
        ret = -1;
        goto err;
    }

    monitor_data->click_count_array = kzalloc(sizeof(int32_t) * CLICK_COUNT_ARRAY_HEIGHT * CLICK_COUNT_ARRAY_WIDTH, GFP_KERNEL);
    if (!monitor_data->click_count_array) {
        TPD_INFO("kzalloc click_count_array failed.\n");
        ret = -1;
        goto err;
    }

    monitor_data->jumping_points_count_array = kzalloc(sizeof(int32_t) * (monitor_data->TX_NUM / 2) * (monitor_data->RX_NUM / 2), GFP_KERNEL);
    if (!monitor_data->jumping_points_count_array) {
        TPD_INFO("kzalloc jumping_points_count_array failed.\n");
        ret = -1;
        goto err;
    }

    monitor_data->stuck_points_count_array = kzalloc(sizeof(int32_t) * (monitor_data->TX_NUM / 2) * (monitor_data->RX_NUM / 2), GFP_KERNEL);
    if (!monitor_data->stuck_points_count_array) {
        TPD_INFO("kzalloc stuck_points_count_array failed.\n");
        ret = -1;
        goto err;
    }

    monitor_data->lanscape_stuck_points_count_array = kzalloc(sizeof(int32_t) * (monitor_data->TX_NUM / 2) * (monitor_data->RX_NUM / 2), GFP_KERNEL);
    if (!monitor_data->lanscape_stuck_points_count_array) {
        TPD_INFO("kzalloc lanscape_stuck_points_count_array failed.\n");
        ret = -1;
        goto err;
    }

    monitor_data->broken_swipes_count_array = kzalloc(sizeof(int32_t) * (monitor_data->TX_NUM / 2) * (monitor_data->RX_NUM / 2), GFP_KERNEL);
    if (!monitor_data->broken_swipes_count_array) {
        TPD_INFO("kzalloc broken_swipes_count_array failed.\n");
        ret = -1;
        goto err;
    }

    monitor_data->total_touch_time_in_game = kzalloc(sizeof(u64) * (monitor_data->max_finger_support + 1), GFP_KERNEL);
    if (!monitor_data->total_touch_time_in_game) {
        TPD_INFO("kzalloc total_touch_time_in_game failed.\n");
        ret = -1;
        goto err;
    }
    //values list init
    INIT_LIST_HEAD(&monitor_data->gesture_values_list);
    INIT_LIST_HEAD(&monitor_data->invalid_gesture_values_list);
    INIT_LIST_HEAD(&monitor_data->fp_area_rate_list);
    INIT_LIST_HEAD(&monitor_data->fd_values_list);
    INIT_LIST_HEAD(&monitor_data->health_report_list);
    INIT_LIST_HEAD(&monitor_data->bus_errs_list);
    INIT_LIST_HEAD(&monitor_data->bus_errs_buff_list);
    INIT_LIST_HEAD(&monitor_data->alloc_err_funcs_list);
    INIT_LIST_HEAD(&monitor_data->fw_update_result_list);

    if (!monitor_data->kernel_grip_support) {
        init_grip_zone(dev, monitor_data);
    }

    return 0;
err:
    kfree(monitor_data->fw_version);
    kfree(monitor_data->jumping_point_delta_data);
    kfree(monitor_data->stuck_point_delta_data);
    kfree(monitor_data->points_state);
    kfree(monitor_data->click_count_array);
    kfree(monitor_data->jumping_points_count_array);
    kfree(monitor_data->stuck_points_count_array);
    kfree(monitor_data->lanscape_stuck_points_count_array);
    kfree(monitor_data->broken_swipes_count_array);
    kfree(monitor_data->total_touch_time_in_game);

    return ret;
}
