// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "touchpanel_common.h"
#include <touchpanel_prevention.h>
#include <linux/uaccess.h>

/*******Start of LOG TAG Declear**********************************/
#define TPD_DEVICE "prevent"
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
#ifdef CONFIG_TOUCHPANEL_ALGORITHM
__attribute__((weak)) void set_algorithm_direction(struct touchpanel_data *ts, int dir)
{
    return;
}
#endif


extern struct touchpanel_data *g_tp;

//judge if should exit dead zone grip
static bool dead_grip_judged(struct kernel_grip_info *grip_info, struct point_info cur_p)
{
    struct list_head *pos = NULL;
    struct grip_zone_area *grip_area = NULL;

    list_for_each(pos, &grip_info->dead_zone_list) {
        grip_area = (struct grip_zone_area *)pos;

        if ((grip_area->support_dir >> grip_info->touch_dir) & 0x01) {
            if ((cur_p.x <= grip_area->start_x + grip_area->x_width) && (cur_p.x >= grip_area->start_x) &&
                (cur_p.y <= grip_area->start_y + grip_area->y_width) && (cur_p.y >= grip_area->start_y)) {
                return false;
            }
        }
    }

    return true;
}

// strategy 1: dead rejection
int dead_grip_handle(struct kernel_grip_info *grip_info, int obj_attention, struct point_info *points)
{
    int i = 0;
    bool is_exit = false;
    int obj_final = obj_attention;

    for (i = 0; i < TOUCH_MAX_NUM; i++) {
        if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01) {
            if (grip_info->dead_out_status[i])      //if already outside the range, skip handle
                continue;

            is_exit = dead_grip_judged(grip_info, points[i]);
            grip_info->dead_out_status[i] = is_exit;        //set outside flag
            if (!is_exit) {
                obj_final = obj_final & (~(1 << i));
            }
        } else {
            grip_info->dead_out_status[i] = 0;
        }
    }

    return obj_final;
}

static void init_filter_data(struct kernel_grip_info *grip_info, uint8_t index, struct point_info point)
{
    int i = 0, cnt = 0;
    struct coord_buffer *buf = NULL;

    if (!grip_info || (index >= TOUCH_MAX_NUM)) {
        TPD_INFO("null or index too large:%d.\n", index);
        return;
    }
    cnt = grip_info->coord_filter_cnt;
    buf = &grip_info->coord_buf[cnt * index];

    for (i = 0; i < cnt; i++) {
        buf[i].x = point.x;
        buf[i].y = point.y;
    }
}

static void add_filter_data_tail(struct kernel_grip_info *grip_info, uint8_t index, struct point_info point)
{
    int i = 0, cnt = 0;
    struct coord_buffer *buf = NULL;

    if (!grip_info || (index >= TOUCH_MAX_NUM)) {
        TPD_INFO("null or index too large:%d.\n", index);
        return;
    }
    cnt = grip_info->coord_filter_cnt;
    buf = &grip_info->coord_buf[cnt * index];

    if (cnt < 2) {
        TPD_INFO("filter buffer size is too small(%d).\n", cnt);
        return;
    }

    for (i = 0; i < cnt - 1; i++) {
        buf[i].x = buf[i + 1].x;
        buf[i].y = buf[i + 1].y;
    }

    buf[cnt - 1].x = point.x;
    buf[cnt - 1].y = point.y;
}

static void assign_filtered_data(struct kernel_grip_info *grip_info, uint8_t index, struct point_info *point)
{
    int i = 0, cnt = 0;
    uint32_t total_x = 0, total_y = 0, total_weight = 0;
    struct coord_buffer *buf = NULL;

    if (!grip_info || !point || (index >= TOUCH_MAX_NUM)) {
        TPD_INFO("null or index too large:%d.\n", index);
        return;
    }
    cnt = grip_info->coord_filter_cnt;
    buf = &grip_info->coord_buf[cnt * index];

    for (i = 0; i < cnt; i++) {
        total_x += buf[i].x * buf[i].weight;
        total_y += buf[i].y * buf[i].weight;
        total_weight += buf[i].weight;
    }

    if (total_weight) {
        point->x = total_x / total_weight;
        point->y = total_y / total_weight;
    }
}

//judge if this area should skip judge
static bool skip_handle_judge(struct kernel_grip_info *grip_info, struct point_info *cur_p)
{
    if (!grip_info->no_handle_dir) {        //default right side
        if ((cur_p->x > grip_info->max_x / 2) && (cur_p->y > grip_info->no_handle_y1) && (cur_p->y < grip_info->no_handle_y2))
            return true;
    } else {
        if ((cur_p->x < grip_info->max_x / 2) && (cur_p->y > grip_info->no_handle_y1) && (cur_p->y < grip_info->no_handle_y2))
            return true;
    }

    return false;
}

//judge if should exit large area
static bool large_area_judged(struct kernel_grip_info *grip_info, uint16_t *grip_side, struct point_info *points, int index)
{
    int thd = 0;
    bool is_exit = true;
    struct list_head *pos = NULL;
    struct grip_zone_area *grip_area = NULL;
    struct point_info cur_p = points[index];

    if(skip_handle_judge(grip_info, &cur_p))
        return true;

    list_for_each(pos, &grip_info->large_zone_list) {
        grip_area = (struct grip_zone_area *)pos;

        if ((grip_area->support_dir >> grip_info->touch_dir) & 0x01) {
            if ((cur_p.x <= grip_area->start_x + grip_area->x_width) && (cur_p.x >= grip_area->start_x) &&
                (cur_p.y <= grip_area->start_y + grip_area->y_width) && (cur_p.y >= grip_area->start_y)) {
                *grip_side |= grip_area->grip_side;
                is_exit = false;
            }

            if ((grip_info->first_point[index].x <= grip_area->start_x + grip_area->x_width) && (grip_info->first_point[index].x >= grip_area->start_x) &&
                (grip_info->first_point[index].y <= grip_area->start_y + grip_area->y_width) && (grip_info->first_point[index].y >= grip_area->start_y)) {
                if (((grip_area->grip_side >> TYPE_LONG_CORNER_SIDE) & 0x01) || ((grip_area->grip_side >> TYPE_SHORT_CORNER_SIDE) & 0x01)) {
                    thd = grip_area->exit_thd;
                }
            }
        }
    }

    if (!is_exit && (TYPE_REJECT_DONE == grip_info->large_reject[index]) && thd) {
        if ((abs(grip_info->first_point[index].x - cur_p.x) > thd) || (abs(grip_info->first_point[index].y - cur_p.y) > thd)) {
            is_exit = true;
        }
    }

    return is_exit;
}

//judge if should exit large area for curved touchscreen
static bool large_area_judged_curved(struct kernel_grip_info *grip_info, uint16_t *grip_side, struct point_info *points, int index)
{
    int thd = 0, tx_er_thd = 0, rx_er_thd = 0;
    bool is_exit = true;
    struct list_head *pos = NULL;
    struct grip_zone_area *grip_area = NULL;
    struct point_info cur_p = points[index];
    struct curved_judge_para long_side_para = grip_info->curved_long_side_para;
    struct curved_judge_para short_side_para = grip_info->curved_short_side_para;

    list_for_each(pos, &grip_info->large_zone_list) {
        grip_area = (struct grip_zone_area *)pos;

        if ((grip_area->support_dir >> grip_info->touch_dir) & 0x01) {
            if ((cur_p.x <= grip_area->start_x + grip_area->x_width) && (cur_p.x >= grip_area->start_x) &&
                (cur_p.y <= grip_area->start_y + grip_area->y_width) && (cur_p.y >= grip_area->start_y)) {
                *grip_side |= grip_area->grip_side;
                is_exit = false;
            }

            if ((grip_info->first_point[index].x <= grip_area->start_x + grip_area->x_width) && (grip_info->first_point[index].x >= grip_area->start_x) &&
                (grip_info->first_point[index].y <= grip_area->start_y + grip_area->y_width) && (grip_info->first_point[index].y >= grip_area->start_y)) {
                thd = grip_area->exit_thd;
                tx_er_thd = grip_area->exit_tx_er;
                rx_er_thd = grip_area->exit_rx_er;
            }
        }
    }

    if (!is_exit && (TYPE_REJECT_DONE == grip_info->large_reject[index]) && thd) {
        if ((abs(grip_info->first_point[index].x - cur_p.x) > thd) || (abs(grip_info->first_point[index].y - cur_p.y) > thd)) {
            is_exit = true;
        }
    }

    if (is_exit && (TYPE_REJECT_DONE == grip_info->large_reject[index]) && grip_info->large_finger_status[index] >= TYPE_PALM_SHORT_SIZE) {
        if (cur_p.tx_er >= tx_er_thd || cur_p.rx_er >= rx_er_thd) {
            is_exit = false;
        }
        if (cur_p.tx_press == 0 && grip_info->first_point[index].tx_press != 0) {
            if (cur_p.rx_er <= long_side_para.normal_finger_thd_2 && cur_p.rx_press < long_side_para.large_palm_thd_1) { // finger need to exit right now
                is_exit = true;
            }
        }
        if (cur_p.rx_press == 0 && grip_info->first_point[index].rx_press != 0) {
            if (cur_p.tx_er <= short_side_para.normal_finger_thd_2 && cur_p.tx_press < short_side_para.large_palm_thd_1) { // finger need to exit right now
                is_exit = true;
            }
        }
    }

    return is_exit;
}

//judge if satisify large size
static enum large_judge_status large_shape_judged(struct kernel_grip_info *grip_info, uint16_t side, struct point_info *points, int index)
{
    uint16_t thd = 0;
    bool area_flag = false;
    uint16_t frame = grip_info->frame_cnt[index];
    struct point_info cur_p = points[index];
    enum large_judge_status judge_status = JUDGE_LARGE_CONTINUE;

    if (((side >> TYPE_SHORT_CORNER_SIDE) & 0x01) || ((side >> TYPE_LONG_CORNER_SIDE) & 0x01)) {
        if (frame <= grip_info->large_corner_frame_limit) {
            TPD_INFO("rx:%d, tx:%d.(%d %d)\n", cur_p.rx_press, cur_p.tx_press, cur_p.x, cur_p.y);
            if ((side >> TYPE_SHORT_CORNER_SIDE) & 0x01) {
                thd = grip_info->large_hor_corner_thd;

                if (cur_p.y < grip_info->max_y / 2) {
                    area_flag = grip_info->first_point[index].y < grip_info->large_hor_corner_width;
                } else {
                    area_flag = grip_info->first_point[index].y > grip_info->max_y - grip_info->large_hor_corner_width;
                }

                if (area_flag && (grip_info->first_point[index].tx_press >= thd) && (cur_p.tx_press >= thd)
                    && (abs(cur_p.y - grip_info->first_point[index].y) > grip_info->large_corner_distance)) {
                    judge_status = JUDGE_LARGE_OK;
                }
            }

            if ((side >> TYPE_LONG_CORNER_SIDE) & 0x01) {
                thd = grip_info->large_ver_corner_thd;

                if (cur_p.x < grip_info->max_x / 2) {
                    area_flag = grip_info->first_point[index].x < grip_info->large_ver_corner_width;
                } else {
                    area_flag = grip_info->first_point[index].x > grip_info->max_x - grip_info->large_ver_corner_width;
                }

                if (area_flag && (grip_info->first_point[index].rx_press >= thd) && (cur_p.rx_press >= thd)
                    && (abs(cur_p.x - grip_info->first_point[index].x) > grip_info->large_corner_distance)) {
                    judge_status = JUDGE_LARGE_OK;
                }
            }
        } else {
            judge_status = JUDGE_LARGE_TIMEOUT;
        }
    } else if (((side >> TYPE_LONG_SIDE) & 0x01) || ((side >> TYPE_SHORT_SIDE) & 0x01)) {
        if (frame <= grip_info->large_frame_limit) {
            TPD_INFO("rx:%d, tx:%d\n", cur_p.rx_press, cur_p.tx_press);
            if ((side >> TYPE_LONG_SIDE) & 0x01) {
                if (1 == cur_p.tx_press)
                    thd = grip_info->large_ver_thd * 2;
                else
                    thd = grip_info->large_ver_thd;
                if (cur_p.rx_press * 100 / cur_p.tx_press >= thd) {
                    TPD_INFO("large reject for rx:%d, tx:%d.\n", cur_p.rx_press, cur_p.tx_press);
                    judge_status = JUDGE_LARGE_OK;
                }
            }

            if ((side >> TYPE_SHORT_SIDE) & 0x01) {
                if (1 == cur_p.rx_press)
                    thd = grip_info->large_hor_thd * 2;
                else
                    thd = grip_info->large_hor_thd;
                if (cur_p.tx_press * 100 / cur_p.rx_press >= thd) {
                    TPD_INFO("large reject for tx:%d, rx:%d.\n", cur_p.tx_press, cur_p.rx_press);
                    judge_status = JUDGE_LARGE_OK;
                }
            }
        } else {
            judge_status = JUDGE_LARGE_TIMEOUT;
        }
    }

    return judge_status;
}

//judge if satisify large size for curved screen
static enum large_judge_status large_shape_judged_curved(struct kernel_grip_info *grip_info, uint16_t side, struct point_info *points, int index)
{
    struct point_info cur_p = points[index];
    struct curved_judge_para long_side_para = grip_info->curved_long_side_para;
    struct curved_judge_para short_side_para = grip_info->curved_short_side_para;
    enum large_judge_status judge_status = JUDGE_LARGE_CONTINUE;
    s64 now_time_ms = ktime_to_ms(ktime_get());
    s64 delta_time_ms = now_time_ms - grip_info->first_point[index].time_ms;

    if (((side >> TYPE_LONG_SIDE) & 0x01) || ((side >> TYPE_SHORT_SIDE) & 0x01)) {
        if (delta_time_ms <= grip_info->large_detect_time_ms) {
            TPD_DETAIL("id:%d, rx:%d, tx:%d, rx_er:%d, tx_er:%d. (%d %d)\n", index, cur_p.rx_press, cur_p.tx_press, cur_p.rx_er, cur_p.tx_er, cur_p.x, cur_p.y);
            if ((side >> TYPE_LONG_SIDE) & 0x01) {
                if (cur_p.tx_press == 0) { // long side
                    if (cur_p.rx_press >= long_side_para.large_palm_thd_1 || (cur_p.rx_er >= long_side_para.large_palm_thd_2 &&
                        delta_time_ms <= grip_info->large_detect_time_ms / 5)) {
                        TPD_INFO("palm long size reject id:%d for rx:%d, tx:%d, rx_er:%d, tx_er:%d.\n", index, cur_p.rx_press,
                            cur_p.tx_press, cur_p.rx_er, cur_p.tx_er);
                        grip_info->large_finger_status[index] = TYPE_PALM_LONG_SIZE;
                        judge_status = JUDGE_LARGE_OK;
                    } else if (cur_p.rx_er >= long_side_para.edge_finger_thd && delta_time_ms <= grip_info->large_detect_time_ms / 5) {
                        grip_info->large_finger_status[index] = TYPE_EDGE_FINGER;
                    } else if (grip_info->first_point[index].rx_er - cur_p.rx_er >= long_side_para.hold_finger_thd) {
                        grip_info->large_finger_status[index] = TYPE_HOLD_FINGER;
                    } else if (grip_info->first_point[index].rx_er <= long_side_para.normal_finger_thd_1 && cur_p.rx_er <= long_side_para.normal_finger_thd_2
                        && cur_p.rx_press <= long_side_para.normal_finger_thd_3) { // finger need to exit right now
                        grip_info->large_finger_status[index] = TYPE_NORMAL_FINGER;
                        judge_status = JUDGE_LARGE_TIMEOUT;
                    }
                } else if (cur_p.rx_press != 0 && !(grip_info->touch_dir == VERTICAL_SCREEN && grip_info->first_point[index].y <= grip_info->max_y / 2)) { // long corner side
                    if (cur_p.rx_press >= long_side_para.large_palm_thd_1 || (cur_p.rx_er >= long_side_para.large_palm_thd_2 &&
                        delta_time_ms <= grip_info->large_detect_time_ms / 5)) {
                        TPD_INFO("large palm corner reject id:%d for rx:%d, tx:%d, rx_er:%d, tx_er:%d.\n", index, cur_p.rx_press,
                            cur_p.tx_press, cur_p.rx_er, cur_p.tx_er);
                        grip_info->large_finger_status[index] = TYPE_LARGE_PALM_CORNER;
                        judge_status = JUDGE_LARGE_OK;
                    } else if ((cur_p.rx_press + cur_p.tx_press >= long_side_para.palm_thd_1) &&
                        (cur_p.tx_er >= long_side_para.palm_thd_2 || cur_p.rx_er >= long_side_para.palm_thd_2)) {
                        TPD_INFO("palm corner reject id:%d for rx:%d, tx:%d, rx_er:%d, tx_er:%d.\n", index, cur_p.rx_press,
                            cur_p.tx_press, cur_p.rx_er, cur_p.tx_er);
                        grip_info->large_finger_status[index] = TYPE_PALM_CORNER;
                        judge_status = JUDGE_LARGE_OK;
                    } else if ((cur_p.rx_er / cur_p.rx_press >= long_side_para.small_palm_thd_1)&& cur_p.rx_press <= long_side_para.small_palm_thd_2) {
                        grip_info->large_finger_status[index] = TYPE_SMALL_PALM_CORNER;
                    } else if (cur_p.rx_er >= long_side_para.edge_finger_thd && delta_time_ms <= grip_info->large_detect_time_ms / 5) {
                        grip_info->large_finger_status[index] = TYPE_EDGE_FINGER;
                    } else if (grip_info->first_point[index].rx_er - cur_p.rx_er >= long_side_para.hold_finger_thd) {
                        grip_info->large_finger_status[index] = TYPE_HOLD_FINGER;
                    }
                }
            }

            if ((side >> TYPE_SHORT_SIDE) & 0x01) {
                if (cur_p.rx_press == 0) { // short side
                    if (cur_p.tx_press >= short_side_para.large_palm_thd_1 || (cur_p.tx_er >= short_side_para.large_palm_thd_2 &&
                        delta_time_ms <= grip_info->large_detect_time_ms / 5)) {
                        TPD_INFO("palm short size reject id:%d for rx:%d, tx:%d, rx_er:%d, tx_er:%d.\n", index, cur_p.rx_press,
                            cur_p.tx_press, cur_p.rx_er, cur_p.tx_er);
                        grip_info->large_finger_status[index] = TYPE_PALM_SHORT_SIZE;
                        judge_status = JUDGE_LARGE_OK;
                    } else if (cur_p.tx_er >= short_side_para.edge_finger_thd && delta_time_ms <= grip_info->large_detect_time_ms / 5) {
                        grip_info->large_finger_status[index] = TYPE_EDGE_FINGER;
                    } else if (grip_info->first_point[index].tx_er <= short_side_para.normal_finger_thd_1 && cur_p.tx_er <= short_side_para.normal_finger_thd_2
                        && cur_p.tx_press <= short_side_para.normal_finger_thd_3) { // finger need to exit right now
                        grip_info->large_finger_status[index] = TYPE_NORMAL_FINGER;
                        judge_status = JUDGE_LARGE_TIMEOUT;
                    }
                } else if (cur_p.tx_press != 0) { // short corner side
                    if (cur_p.tx_press >= short_side_para.large_palm_thd_1 || (cur_p.tx_er >= short_side_para.large_palm_thd_2 &&
                        delta_time_ms <= grip_info->large_detect_time_ms / 5)) {
                        TPD_INFO("large palm corner reject id:%d for rx:%d, tx:%d, rx_er:%d, tx_er:%d.\n", index, cur_p.rx_press,
                            cur_p.tx_press, cur_p.rx_er, cur_p.tx_er);
                        grip_info->large_finger_status[index] = TYPE_LARGE_PALM_CORNER;
                        judge_status = JUDGE_LARGE_OK;
                    } else if ((cur_p.rx_press + cur_p.tx_press >= short_side_para.palm_thd_1) &&
                        (cur_p.tx_er >= short_side_para.palm_thd_2 || cur_p.rx_er >= short_side_para.palm_thd_2)) {
                        TPD_INFO("palm corner reject id:%d for rx:%d, tx:%d, rx_er:%d, tx_er:%d.\n", index, cur_p.rx_press,
                            cur_p.tx_press, cur_p.rx_er, cur_p.tx_er);
                        grip_info->large_finger_status[index] = TYPE_PALM_CORNER;
                        judge_status = JUDGE_LARGE_OK;
                    } else if ((cur_p.tx_er / cur_p.tx_press >= short_side_para.small_palm_thd_1) && cur_p.tx_press <= short_side_para.small_palm_thd_2) {
                        grip_info->large_finger_status[index] = TYPE_SMALL_PALM_CORNER;
                    }
                }
            }
        } else {
            if (grip_info->large_finger_status[index] == TYPE_EDGE_FINGER) {
                TPD_INFO("edge finger reject id:%d for rx:%d, tx:%d, rx_er:%d, tx_er:%d.\n", index, cur_p.rx_press,
                    cur_p.tx_press, cur_p.rx_er, cur_p.tx_er);
                judge_status = JUDGE_LARGE_OK;
            } else if (grip_info->large_finger_status[index] == TYPE_HOLD_FINGER) {
                TPD_INFO("hold finger reject id:%d for rx:%d, tx:%d, rx_er:%d, tx_er:%d.\n", index, cur_p.rx_press,
                    cur_p.tx_press, cur_p.rx_er, cur_p.tx_er);
                judge_status = JUDGE_LARGE_OK;
            } else if (grip_info->large_finger_status[index] == TYPE_SMALL_PALM_CORNER) {
                TPD_INFO("small palm corner reject id:%d for rx:%d, tx:%d, rx_er:%d, tx_er:%d.\n", index, cur_p.rx_press,
                    cur_p.tx_press, cur_p.rx_er, cur_p.tx_er);
                judge_status = JUDGE_LARGE_OK;
            } else {
                judge_status = JUDGE_LARGE_TIMEOUT;
            }
        }
    }

    return judge_status;
}

//judge if should exit conditional area
static bool condition_area_judged(struct kernel_grip_info *grip_info, struct point_info *points, int index)
{
    int thd = 0;
    struct list_head *pos = NULL;
    struct grip_zone_area *condition_area = NULL;
    struct point_info cur_p = points[index];

    list_for_each(pos, &grip_info->condition_zone_list) {
        condition_area = (struct grip_zone_area *)pos;

        if ((condition_area->support_dir >> grip_info->touch_dir) & 0x01) {
            if ((cur_p.x <= condition_area->start_x + condition_area->x_width) && (cur_p.x >= condition_area->start_x) &&
                (cur_p.y <= condition_area->start_y + condition_area->y_width) && (cur_p.y >= condition_area->start_y)) {
                if ((condition_area->grip_side >> TYPE_SHORT_SIDE) & 0x01) {
                    if (abs(grip_info->first_point[index].x - cur_p.x) < condition_area->exit_thd)
                        return false;
                } else if ((condition_area->grip_side >> TYPE_LONG_SIDE) & 0x01) {
                    if (abs(grip_info->first_point[index].y - cur_p.y) < condition_area->exit_thd)
                        return false;
                }
            }
            if ((grip_info->first_point[index].x <= condition_area->start_x + condition_area->x_width) && (grip_info->first_point[index].x >= condition_area->start_x) &&
                (grip_info->first_point[index].y <= condition_area->start_y + condition_area->y_width) && (grip_info->first_point[index].y >= condition_area->start_y)) {
                thd = condition_area->exit_thd;
            }
        }
    }

    if (!grip_info->condition_out_status[index] && (grip_info->frame_cnt[index] >= grip_info->condition_frame_limit)) {
        if ((abs(grip_info->first_point[index].y - cur_p.y) < thd) &&
            (abs(grip_info->first_point[index].x - cur_p.x) < thd)) {
            return false;
        }
    }

    return true;
}

void grip_status_reset(struct kernel_grip_info *grip_info, uint8_t index)
{
    if (!grip_info) {
        TPD_INFO("null and return.\n");
        return;
    }

    if (index >= TOUCH_MAX_NUM) {
        TPD_INFO("invalid index :%d.\n", index);
        return;
    }

    grip_info->dead_out_status[index] = 0;
    grip_info->frame_cnt[index] = 0;
    grip_info->large_out_status[index] = 0;
    grip_info->large_reject[index] = 0;
    grip_info->condition_out_status[index] = 0;
    grip_info->makeup_cnt[index] = 0;
    grip_info->point_unmoved[index] = 0;
    grip_info->grip_hold_status[index] = 0;
    grip_info->large_finger_status[index] = 0;
    grip_info->large_point_status[index] = UP_POINT;

    grip_info->eli_out_status[index] = 0;
    grip_info->eli_reject_status[index] = 0;
    TPD_DETAIL("reset id :%d.\n", index);
}

//using for report touch up event
static void touch_report_work(struct work_struct *work)
{
    uint8_t up_id = 0;
    int ret = 0;

    if (!g_tp) {
        TPD_INFO("g_tp is null.\n");
        return;
    }

    mutex_lock(&g_tp->report_mutex);

    ret = kfifo_get(&g_tp->grip_info->up_fifo, &up_id);
    if (!ret) {
        TPD_INFO("upfifo is empty.\n");
        goto OUT;
    }

    if (!g_tp->grip_info->grip_hold_status[up_id]) {
        TPD_INFO("id:%d is alreay up.\n", up_id);
        goto OUT;
    }

    input_mt_slot(g_tp->input_dev, up_id);
    input_mt_report_slot_state(g_tp->input_dev, MT_TOOL_FINGER, 0);
    grip_status_reset(g_tp->grip_info, up_id);                  //reset status of this ID
    if (g_tp->grip_info->record_total_cnt)
        g_tp->grip_info->record_total_cnt--;        //update touch down count
    TPD_INFO("report id(%d) up, left total:%d.\n", up_id, g_tp->grip_info->record_total_cnt);
    if (!g_tp->grip_info->record_total_cnt) {
        input_report_key(g_tp->input_dev, BTN_TOUCH, 0);
        input_report_key(g_tp->input_dev, BTN_TOOL_FINGER, 0);
        g_tp->view_area_touched = 0; //realse all touch point,must clear this flag
        g_tp->touch_count = 0;
        g_tp->irq_slot = 0;
    }
    input_sync(g_tp->input_dev);

#ifdef CONFIG_TOUCHPANEL_ALGORITHM
    if (g_tp->algo_info && g_tp->algo_info->point_buf) {
        g_tp->algo_info->point_buf[up_id].kal_x_last.x = INVALID_POINT;
        g_tp->algo_info->point_buf[up_id].touch_time = 0;
        g_tp->algo_info->point_buf[up_id].status = NORMAL;
        TPD_DETAIL("Reset the algorithm id:%d points\n", up_id);
    }
#endif

    g_tp->grip_info->grip_hold_status[up_id] = 0;
OUT:
    mutex_unlock(&g_tp->report_mutex);
}

static enum hrtimer_restart touch_up_timer_func(struct hrtimer *hrtimer)
{
    static int work_id = 0;

    if (g_tp == NULL) {
        TPD_INFO("g_tp is null.\n");
        return HRTIMER_NORESTART;
    }

    TPD_INFO("time called once.\n");
    work_id++;
    if (work_id >= TOUCH_MAX_NUM)
        work_id = 0;
    queue_work(g_tp->grip_info->grip_up_handle_wq, &g_tp->grip_info->grip_up_work[work_id]);
    return HRTIMER_NORESTART;
}

static int large_condition_handle(struct kernel_grip_info *grip_info, int obj_attention, struct point_info *points)
{
    int i = 0, up_id = 0, ret = 0;
    bool is_exit = false, dead_out = false;
    int obj_final = obj_attention;
    struct point_info tmp_point;
    uint16_t large_side = 0, fiter_cnt = grip_info->coord_filter_cnt;
    enum large_judge_status judge_state = JUDGE_LARGE_CONTINUE;

    if (grip_info->grip_handle_in_fw) {
        return obj_attention;
    }

    for (i = 0; i < TOUCH_MAX_NUM; i++) {
        if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01) {      //finger down
            grip_info->frame_cnt[i]++;                           //count down frames

            if (grip_info->large_out_status[i]) {        //if already exit large area judge
                if (grip_info->condition_out_status[i]) {
                    if ((grip_info->makeup_cnt[i] > 0) && (grip_info->makeup_cnt[i] <= fiter_cnt)) {
                        tmp_point = points[i];
                        assign_filtered_data(grip_info, i, &points[i]);
                        add_filter_data_tail(grip_info, i, tmp_point);
                        grip_info->makeup_cnt[i]++;
                        TPD_INFO("make up :%d times.\n", grip_info->makeup_cnt[i]);
                    }
                } else {
                    grip_info->condition_out_status[i] = condition_area_judged(grip_info, points, i);
                    if (grip_info->condition_out_status[i]) {
                        tmp_point = points[i];
                        assign_filtered_data(grip_info, i, &points[i]);
                        add_filter_data_tail(grip_info, i, tmp_point);
                        grip_info->makeup_cnt[i]++;
                        TPD_INFO("make up a:%d times.\n", grip_info->makeup_cnt[i]);
                    } else {
                        obj_final = obj_final & (~(1 << i));            //reject for condition grip judge
                    }
                }
                continue;
            }

            if (!(((grip_info->obj_prev_bit & TOUCH_BIT_CHECK) >> i) & 0x01)) {        //init coord buff when first touch down
                init_filter_data(grip_info, i, points[i]);
                grip_info->first_point[i].x = points[i].x;
                grip_info->first_point[i].y = points[i].y;
                grip_info->first_point[i].tx_press = points[i].tx_press;
                grip_info->first_point[i].rx_press = points[i].rx_press;
            }

            grip_info->point_unmoved[i] = ((points[i].x == grip_info->coord_buf[i * fiter_cnt].x) &&
                                           (points[i].y == grip_info->coord_buf[i * fiter_cnt].y));

            is_exit = large_area_judged(grip_info, &large_side, points, i);
            if (is_exit) {
                grip_info->large_out_status[i] = true;           //set large outside flag
                grip_info->condition_out_status[i] = true;       //set condition outside flag

                if (!grip_info->point_unmoved[i]) {    //means once in large judge area
                    tmp_point = points[i];
                    assign_filtered_data(grip_info, i, &points[i]);
                    add_filter_data_tail(grip_info, i, tmp_point);
                    grip_info->makeup_cnt[i]++;
                    TPD_INFO("make up b:%d times.\n", grip_info->makeup_cnt[i]);
                }
            } else if (TYPE_REJECT_DONE == grip_info->large_reject[i]) {
                obj_final = obj_final & (~(1 << i));                //if already reject, just mask it
            } else {
                grip_info->condition_out_status[i] = condition_area_judged(grip_info, points, i);

                judge_state = large_shape_judged(grip_info, large_side, points, i);
                if (JUDGE_LARGE_OK == judge_state) {
                    obj_final = obj_final & (~(1 << i));            //reject for big area
                    grip_info->large_reject[i] = TYPE_REJECT_DONE;
                } else if (JUDGE_LARGE_TIMEOUT == judge_state) {
                    grip_info->large_out_status[i] = true;       //set outside flag

                    if (grip_info->condition_out_status[i]) {
                        if (!grip_info->point_unmoved[i]) {
                            tmp_point = points[i];
                            assign_filtered_data(grip_info, i, &points[i]);  //makeup points
                            add_filter_data_tail(grip_info, i, tmp_point);
                            grip_info->makeup_cnt[i]++;
                            TPD_INFO("make up c:%d times.\n", grip_info->makeup_cnt[i]);
                        }
                    } else {
                        obj_final = obj_final & (~(1 << i));            //reject for condition grip judge
                    }
                } else {
                    obj_final = obj_final & (~(1 << i));            //reject for continue detect
                    grip_info->large_reject[i] = TYPE_REJECT_HOLD;
                }
            }
        } else { //finger up
            if (!grip_info->large_out_status[i] && (TYPE_REJECT_DONE == grip_info->large_reject[i])) {
                TPD_INFO("reject id:%d for large area.\n", i);
            } else if (!grip_info->large_out_status[i] && (TYPE_REJECT_HOLD == grip_info->large_reject[i])) {
                points[i].x = grip_info->coord_buf[i * fiter_cnt].x;
                points[i].y = grip_info->coord_buf[i * fiter_cnt].y;

                dead_out = dead_grip_judged(grip_info, points[i]);
                if (dead_out) {
                    points[i].status = 1;
                    grip_info->grip_hold_status[i] = 1;
                    obj_final = obj_final | (1 << i);

                    if (hrtimer_active(&grip_info->grip_up_timer[i])) {
                        hrtimer_cancel(&grip_info->grip_up_timer[i]);
                        ret = kfifo_get(&grip_info->up_fifo, &up_id);
                        if (!ret) {
                            TPD_INFO("large get id failed, empty.\n");
                        }
                        TPD_INFO("large get id(%d) and cancel timer.\n", up_id);
                    }

                    kfifo_put(&grip_info->up_fifo, i);   //put the up id into up fifo
                    TPD_INFO("large put id(%d) into fifo.\n", i);
                    hrtimer_start(&grip_info->grip_up_timer[i], ktime_set(0, grip_info->condition_updelay_ms * 1000000), HRTIMER_MODE_REL);
                } else {
                    TPD_INFO("reject id:%d for dead zone.\n", i);
                }
            } else if (!grip_info->condition_out_status[i] && grip_info->point_unmoved[i]) {
                if (grip_info->frame_cnt[i] < grip_info->condition_frame_limit) {
                    points[i].x = grip_info->coord_buf[i * fiter_cnt].x;
                    points[i].y = grip_info->coord_buf[i * fiter_cnt].y;

                    dead_out = dead_grip_judged(grip_info, points[i]);
                    if (dead_out) {
                        points[i].status = 1;
                        grip_info->grip_hold_status[i] = 1;
                        obj_final = obj_final | (1 << i);

                        if (hrtimer_active(&grip_info->grip_up_timer[i])) {
                            hrtimer_cancel(&grip_info->grip_up_timer[i]);
                            ret = kfifo_get(&grip_info->up_fifo, &up_id);
                            if (!ret) {
                                TPD_INFO("get id failed, empty.\n");
                            }
                            TPD_INFO("get id(%d) and cancel timer.\n", up_id);
                        }

                        kfifo_put(&grip_info->up_fifo, i);   //put the up id into up fifo
                        TPD_INFO("put id(%d) into fifo.\n", i);
                        hrtimer_start(&grip_info->grip_up_timer[i], ktime_set(0, grip_info->condition_updelay_ms * 1000000), HRTIMER_MODE_REL);
                    } else {
                        TPD_INFO("reject id:%d for dead zone.\n", i);
                    }
                } else {
                    TPD_INFO("conditon reject for down frame:%d(%d).\n", grip_info->frame_cnt[i], grip_info->condition_frame_limit);
                }
            }

            //reset status of this id
            grip_info->frame_cnt[i] = 0;
            grip_info->large_out_status[i] = 0;
            grip_info->large_reject[i] = 0;
            grip_info->condition_out_status[i] = 0;
            grip_info->makeup_cnt[i] = 0;
            grip_info->point_unmoved[i] = 0;
        }
    }

    grip_info->obj_prev_bit = obj_attention;
    return obj_final;
}

static int curved_large_handle(struct kernel_grip_info *grip_info, int obj_attention, struct point_info *points)
{
    int i = 0, up_id = 0, ret = 0, j = 0;
    bool is_exit = false;
    int obj_final = obj_attention;
    struct point_info tmp_point;
    uint16_t large_side = 0, fiter_cnt = grip_info->coord_filter_cnt;
    enum large_judge_status judge_state = JUDGE_LARGE_CONTINUE;

    for (i = 0; i < TOUCH_MAX_NUM; i++) {
        if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01) {      //finger down
            grip_info->frame_cnt[i]++;                           //count down frames

            if (grip_info->large_out_status[i]) {        //if already exit large area judge
                if ((grip_info->makeup_cnt[i] > 0) && (grip_info->makeup_cnt[i] <= fiter_cnt)) {
                    tmp_point = points[i];
                    assign_filtered_data(grip_info, i, &points[i]);
                    add_filter_data_tail(grip_info, i, tmp_point);
                    grip_info->makeup_cnt[i]++;
                    TPD_INFO("id:%d make up :%d times.(%d %d)\n", i, grip_info->makeup_cnt[i], points[i].x, points[i].y);
                }
                continue;
            }

            if (!(((grip_info->obj_prev_bit & TOUCH_BIT_CHECK) >> i) & 0x01)) {        //init coord buff when first touch down
                init_filter_data(grip_info, i, points[i]);
                grip_info->first_point[i].x = points[i].x;
                grip_info->first_point[i].y = points[i].y;
                grip_info->first_point[i].tx_press = points[i].tx_press;
                grip_info->first_point[i].rx_press = points[i].rx_press;
                grip_info->first_point[i].tx_er = points[i].tx_er;
                grip_info->first_point[i].rx_er = points[i].rx_er;
                grip_info->first_point[i].time_ms = ktime_to_ms(ktime_get());

                if (grip_info->large_point_status[i] == UP_POINT) { // if there is some point touch down, mark previous down points no need to make up point
                    grip_info->large_point_status[i] = DOWN_POINT_NEED_MAKEUP;
                    for (j = 0; j < TOUCH_MAX_NUM; j++) {
                        if ((((obj_attention & TOUCH_BIT_CHECK) >> j) & 0x01) && i != j) {
                            if (!(((grip_info->obj_prev_bit & TOUCH_BIT_CHECK) >> j) & 0x01)) {
                                grip_info->large_point_status[i] = DOWN_POINT;
                                grip_info->large_point_status[j] = DOWN_POINT;
                            } else {
                                grip_info->large_point_status[j] = DOWN_POINT;
                            }
                        }
                    }
                }

                is_exit = large_area_judged_curved(grip_info, &large_side, points, i);
                if (is_exit) {
                    grip_info->lastest_down_time_ms = ktime_to_ms(ktime_get());
                }
            }

            grip_info->point_unmoved[i] = ((points[i].x == grip_info->coord_buf[i * fiter_cnt].x) &&
                                           (points[i].y == grip_info->coord_buf[i * fiter_cnt].y));

            is_exit = large_area_judged_curved(grip_info, &large_side, points, i);
            if (is_exit) {
                grip_info->large_out_status[i] = true;           //set large outside flag

                if (!grip_info->point_unmoved[i]) {    //means once in large judge area
                    tmp_point = points[i];
                    assign_filtered_data(grip_info, i, &points[i]);
                    add_filter_data_tail(grip_info, i, tmp_point);
                    grip_info->makeup_cnt[i]++;
                    TPD_INFO("id:%d make up b:%d times.(%d %d)(%d %d %d %d)\n", i, grip_info->makeup_cnt[i], points[i].x, points[i].y,
                        points[i].tx_press, points[i].rx_press, points[i].tx_er, points[i].rx_er);
                }
            } else if (TYPE_REJECT_DONE == grip_info->large_reject[i]) {
                obj_final = obj_final & (~(1 << i));                //if already reject, just mask it
            } else {
                judge_state = large_shape_judged_curved(grip_info, large_side, points, i);
                if (JUDGE_LARGE_OK == judge_state) {
                    obj_final = obj_final & (~(1 << i));            //reject for big area
                    grip_info->large_reject[i] = TYPE_REJECT_DONE;
                } else if (JUDGE_LARGE_TIMEOUT == judge_state) {
                    grip_info->large_out_status[i] = true;       //set outside flag
                    if (!grip_info->point_unmoved[i]) {
                        tmp_point = points[i];
                        assign_filtered_data(grip_info, i, &points[i]);  //makeup points
                        add_filter_data_tail(grip_info, i, tmp_point);
                        grip_info->makeup_cnt[i]++;
                        TPD_INFO("id:%d make up c:%d times.(%d %d)(%d %d %d %d)\n", i, grip_info->makeup_cnt[i], points[i].x, points[i].y,
                            points[i].tx_press, points[i].rx_press, points[i].tx_er, points[i].rx_er);
                    }
                } else {
                    obj_final = obj_final & (~(1 << i));            //reject for continue detect
                    grip_info->large_reject[i] = TYPE_REJECT_HOLD;
                }
            }
        } else { //finger up
            if (!grip_info->large_out_status[i] && (TYPE_REJECT_DONE == grip_info->large_reject[i])) {
                TPD_INFO("reject id:%d for large area.\n", i);
            } else if (grip_info->large_point_status[i] != DOWN_POINT_NEED_MAKEUP && !grip_info->large_out_status[i]
                && (TYPE_REJECT_HOLD == grip_info->large_reject[i])) {
                TPD_INFO("reject no need make up click point id:%d for large area.\n", i);
            } else if ((grip_info->first_point[i].time_ms - grip_info->lastest_down_time_ms <= grip_info->down_delta_time_ms)
                && !grip_info->large_out_status[i] && (TYPE_REJECT_HOLD == grip_info->large_reject[i])) {
                TPD_INFO("reject short time click point id:%d for large area.\n", i);
            } else if (!grip_info->large_out_status[i] && (TYPE_REJECT_HOLD == grip_info->large_reject[i])) {
                points[i].x = grip_info->coord_buf[i * fiter_cnt].x;
                points[i].y = grip_info->coord_buf[i * fiter_cnt].y;
                points[i].status = 1;
                grip_info->grip_hold_status[i] = 1;
                obj_final = obj_final | (1 << i);

                if (hrtimer_active(&grip_info->grip_up_timer[i])) {
                    hrtimer_cancel(&grip_info->grip_up_timer[i]);
                    ret = kfifo_get(&grip_info->up_fifo, &up_id);
                    if (!ret) {
                        TPD_INFO("large get id failed, empty.\n");
                    }
                    TPD_INFO("large get id(%d) and cancel timer.\n", up_id);
                }

                kfifo_put(&grip_info->up_fifo, i);   //put the up id into up fifo
                TPD_INFO("large put id(%d) into fifo.\n", i);
                hrtimer_start(&grip_info->grip_up_timer[i], ktime_set(0, grip_info->condition_updelay_ms * 1000000), HRTIMER_MODE_REL);
            }

            //reset status of this id
            grip_info->frame_cnt[i] = 0;
            grip_info->large_out_status[i] = 0;
            grip_info->large_reject[i] = 0;
            grip_info->condition_out_status[i] = 0;
            grip_info->makeup_cnt[i] = 0;
            grip_info->point_unmoved[i] = 0;
            grip_info->large_finger_status[i] = 0;
            grip_info->large_point_status[i] = UP_POINT;
        }
    }

    grip_info->obj_prev_bit = obj_attention;
    return obj_final;
}

//judge if out of the eliminated area
static bool eliminated_area_judged(struct kernel_grip_info *grip_info, struct point_info cur_p)
{
    struct list_head *pos = NULL;
    struct grip_zone_area *grip_area = NULL;

    list_for_each(pos, &grip_info->elimination_zone_list) {
        grip_area = (struct grip_zone_area *)pos;

        if ((grip_area->support_dir >> grip_info->touch_dir) & 0x01) {
            if ((cur_p.x <= grip_area->start_x + grip_area->x_width) && (cur_p.x >= grip_area->start_x) &&
                (cur_p.y <= grip_area->start_y + grip_area->y_width) && (cur_p.y >= grip_area->start_y)) {
                return false;
            }
        }
    }

    return true;
}

static int touch_elimination_handle(struct kernel_grip_info *grip_info, int obj_attention, struct point_info *points)
{
    bool out_status = false;
    int i = 0, obj_final = obj_attention;
    uint16_t left_edge_bit = 0, right_edge_bit = 0;
    int left_edge_cnt = 0, right_edge_cnt = 0, left_center_cnt = 0, right_center_cnt = 0;

    if (grip_info->is_curved_screen) {
        return obj_attention;
    }

    for (i = 0; i < TOUCH_MAX_NUM; i++) {
        if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01) {
            if (VERTICAL_SCREEN == grip_info->touch_dir) {
                out_status = eliminated_area_judged(grip_info, points[i]);
                if (!out_status) {
                    left_edge_cnt++;
                    left_edge_bit = left_edge_bit | (1 << i);
                } else {
                    left_center_cnt++;
                    grip_info->eli_out_status[i] = true;    //set cross status of i
                }
            } else {
                if (points[i].y < grip_info->max_y / 2) {
                    out_status = eliminated_area_judged(grip_info, points[i]);
                    if (!out_status) {
                        left_edge_cnt++;
                        left_edge_bit = left_edge_bit | (1 << i);
                    } else {
                        left_center_cnt++;
                        grip_info->eli_out_status[i] = true;    //set cross status of i
                    }
                } else {
                    out_status = eliminated_area_judged(grip_info, points[i]);
                    if (!out_status) {
                        right_edge_cnt++;
                        right_edge_bit = right_edge_bit | (1 << i);
                    } else {
                        right_center_cnt++;
                        grip_info->eli_out_status[i] = true;    //set cross status of i
                    }
                }
            }
        } else {
            grip_info->eli_out_status[i] = false;
            grip_info->eli_reject_status[i] = false;
        }
    }

    if (VERTICAL_SCREEN == grip_info->touch_dir) {      //in vertical mode
        if (left_edge_cnt && left_center_cnt) {     //need to judge which point shoud be eliminated
            for (i = 0; i < TOUCH_MAX_NUM; i++) {
                if ((((left_edge_bit & TOUCH_BIT_CHECK) >> i) & 0x01) && !grip_info->eli_out_status[i]) {
                    grip_info->eli_reject_status[i] = true;
                    obj_final = obj_final & (~(1 << i));
                }
            }
        } else if (left_edge_cnt) {
            for (i = 0; i < TOUCH_MAX_NUM; i++) {
                if ((((left_edge_bit & TOUCH_BIT_CHECK) >> i) & 0x01) && !grip_info->eli_out_status[i] && grip_info->eli_reject_status[i]) {
                    obj_final = obj_final & (~(1 << i));
                }
            }
        }
    } else {
        //left part of panel
        if (left_edge_cnt && left_center_cnt) {
            for (i = 0; i < TOUCH_MAX_NUM; i++) {
                if ((((left_edge_bit & TOUCH_BIT_CHECK) >> i) & 0x01) && !grip_info->eli_out_status[i]) {
                    grip_info->eli_reject_status[i] = true;
                    obj_final = obj_final & (~(1 << i));
                }
            }
        } else if (left_edge_cnt) {
            for (i = 0; i < TOUCH_MAX_NUM; i++) {
                if ((((left_edge_bit & TOUCH_BIT_CHECK) >> i) & 0x01) && !grip_info->eli_out_status[i] && grip_info->eli_reject_status[i]) {
                    obj_final = obj_final & (~(1 << i));
                }
            }
        }

        //right part of panel
        if (right_edge_cnt && right_center_cnt) {
            for (i = 0; i < TOUCH_MAX_NUM; i++) {
                if ((((right_edge_bit & TOUCH_BIT_CHECK) >> i) & 0x01) && !grip_info->eli_out_status[i]) {
                    grip_info->eli_reject_status[i] = true;
                    obj_final = obj_final & (~(1 << i));
                }
            }
        } else if (right_edge_cnt) {
            for (i = 0; i < TOUCH_MAX_NUM; i++) {
                if ((((right_edge_bit & TOUCH_BIT_CHECK) >> i) & 0x01) && !grip_info->eli_out_status[i] && grip_info->eli_reject_status[i]) {
                    obj_final = obj_final & (~(1 << i));
                }
            }
        }
    }

    return obj_final;
}

static uint8_t get_bit_count(int var)
{
    int m = 0, bits = 0;

    for (m = 0; m < 8 * sizeof(var); m++) {
        if ((var >> m) & 0x01) {
            bits++;
        }
    }

    return bits;
}

int notify_prevention_handle(struct kernel_grip_info *grip_info, int obj_attention, struct point_info *points)
{
    int i = 0;

    if (!g_tp || !grip_info || !points) {
        TPD_INFO("grip_info or points is null.\n");
        return obj_attention;
    }

    mutex_lock(&grip_info->grip_mutex);

    grip_info->obj_bit_rcd = obj_attention;

    if (!(grip_info->grip_disable_level & (1 << GRIP_DISABLE_LARGE))) {\
        if (grip_info->is_curved_screen) {
            obj_attention = curved_large_handle(grip_info, obj_attention, points);
        } else {
            obj_attention = large_condition_handle(grip_info, obj_attention, points);
        }
    }
    if (!(grip_info->grip_disable_level & (1 << GRIP_DISABLE_ELI)))
        obj_attention = touch_elimination_handle(grip_info, obj_attention, points);

    grip_info->obj_prced_bit_rcd = obj_attention;

    for (i = 0; i < TOUCH_MAX_NUM; i++) {
        if (grip_info->grip_hold_status[i]) {    //handle hold id
            if (((grip_info->obj_prev_bit & TOUCH_BIT_CHECK) >> i) & 0x01) {     //ID down from IC, report touch up firstly
                input_mt_slot(g_tp->input_dev, i);
                input_mt_report_slot_state(g_tp->input_dev, MT_TOOL_FINGER, 0);
                grip_status_reset(grip_info, i);        //reset status of this ID

                if (grip_info->record_total_cnt)
                    grip_info->record_total_cnt--;        //update touch down count
                grip_info->grip_hold_status[i] = 0;
                TPD_INFO("id:%d report touch up firstly, left total(%d).\n", i, grip_info->record_total_cnt);
            } else if (!grip_info->eli_reject_status[i]) {  //aviod hold points be cleared by next down frame
                obj_attention = obj_attention | (1 << i);   //point[i].status is 0, means no down and no up report
            } else {
                TPD_INFO("id:%d grip hold and reject by eli.\n", i);
            }
        }
    }
    if (!grip_info->record_total_cnt) {
        input_report_key(g_tp->input_dev, BTN_TOUCH, 0);
        input_report_key(g_tp->input_dev, BTN_TOOL_FINGER, 0);
    }
    input_sync(g_tp->input_dev);

    mutex_unlock(&grip_info->grip_mutex);

    grip_info->record_total_cnt = get_bit_count(obj_attention & TOUCH_BIT_CHECK);
    return obj_attention;
}

int kernel_grip_print_func(struct seq_file *s, struct kernel_grip_info *grip_info)
{
    struct list_head *pos = NULL;
    struct grip_zone_area *grip_area = NULL;
    struct curved_judge_para *long_side_para = &grip_info->curved_long_side_para;
    struct curved_judge_para *short_side_para = &grip_info->curved_short_side_para;

    if (!grip_info) {
        TPD_INFO("%s read grip info failed.\n", __func__);
        return 0;
    }

    seq_printf(s, "grip_disable_level:%d\n", grip_info->grip_disable_level);
    seq_printf(s, "skip area:%d, %d, %d.\n", grip_info->no_handle_dir, grip_info->no_handle_y1, grip_info->no_handle_y2);

    seq_printf(s, "dead zone:\n");
    list_for_each(pos, &grip_info->dead_zone_list) {
        grip_area = (struct grip_zone_area *)pos;

        seq_printf(s, "name:%24s, start_point:(%4d, %4d), width:(%4d, %4d), exit_thd:%4d, support_dir:0x%02x, side:0x%02x.\n",
                   grip_area->name, grip_area->start_x, grip_area->start_y, grip_area->x_width, grip_area->y_width,
                   grip_area->exit_thd, grip_area->support_dir, grip_area->grip_side);
    }
    seq_printf(s, "\n");

    seq_printf(s, "condition zone:\n");
    list_for_each(pos, &grip_info->condition_zone_list) {
        grip_area = (struct grip_zone_area *)pos;

        seq_printf(s, "name:%24s, start_point:(%4d, %4d), width:(%4d, %4d), exit_thd:%4d, support_dir:0x%02x, side:0x%02x.\n",
                   grip_area->name, grip_area->start_x, grip_area->start_y, grip_area->x_width, grip_area->y_width,
                   grip_area->exit_thd, grip_area->support_dir, grip_area->grip_side);
    }
    seq_printf(s, "condition_frame_limit:%4d, condition_updelay_ms:%4lu\n",
               grip_info->condition_frame_limit, grip_info->condition_updelay_ms);
    seq_printf(s, "\n");

    seq_printf(s, "large zone:\n");
    list_for_each(pos, &grip_info->large_zone_list) {
        grip_area = (struct grip_zone_area *)pos;
        if (grip_info->is_curved_screen) {
            if (strstr(grip_area->name, "curved")) {
                seq_printf(s, "name:%24s, start_point:(%4d, %4d), width:(%4d, %4d), exit_thd:%4d, exit_tx_er:%4d, exit_rx_er:%4d, support_dir:0x%02x, side:0x%02x.\n",
                           grip_area->name, grip_area->start_x, grip_area->start_y, grip_area->x_width, grip_area->y_width, grip_area->exit_thd,
                           grip_area->exit_tx_er, grip_area->exit_rx_er, grip_area->support_dir, grip_area->grip_side);
            }
        } else {
            seq_printf(s, "name:%24s, start_point:(%4d, %4d), width:(%4d, %4d), exit_thd:%4d, support_dir:0x%02x, side:0x%02x.\n",
                       grip_area->name, grip_area->start_x, grip_area->start_y, grip_area->x_width, grip_area->y_width,
                       grip_area->exit_thd, grip_area->support_dir, grip_area->grip_side);
        }
    }
    seq_printf(s, "large_frame_limit:%4d, large_ver_thd:%4d, large_hor_thd:%4d\n",
               grip_info->large_frame_limit, grip_info->large_ver_thd, grip_info->large_hor_thd);
    seq_printf(s, "large_corner_frame_limit:%4d, large_ver_corner_thd:%4d, large_hor_corner_thd:%4d\n\
large_ver_corner_width:%4d, large_hor_corner_width:%4d, large_corner_distance:%4d\n",
               grip_info->large_corner_frame_limit, grip_info->large_ver_corner_thd, grip_info->large_hor_corner_thd,
               grip_info->large_ver_corner_width, grip_info->large_hor_corner_width, grip_info->large_corner_distance);
    seq_printf(s, "\n");

    if (grip_info->is_curved_screen) {
        seq_printf(s, "large_detect_time_ms:%4d\n", grip_info->large_detect_time_ms);
        seq_printf(s, "down_delta_time_ms:%4d\n", grip_info->down_delta_time_ms);
        seq_printf(s, "curved_large_judge_para_for_long_side:\n");
        seq_printf(s, "edge_finger_thd:%4d, hold_finger_thd:%4d, normal_finger_thd_1:%4d, normal_finger_thd_2:%4d, normal_finger_thd_3:%4d\n\
    large_palm_thd_1:%4d, large_palm_thd_2:%4d, palm_thd_1:%4d, palm_thd_2:%4d, small_palm_thd_1:%4d, small_palm_thd_2:%4d\n",
                   long_side_para->edge_finger_thd, long_side_para->hold_finger_thd, long_side_para->normal_finger_thd_1,
                   long_side_para->normal_finger_thd_2, long_side_para->normal_finger_thd_3, long_side_para->large_palm_thd_1, long_side_para->large_palm_thd_2,
                   long_side_para->palm_thd_1, long_side_para->palm_thd_2, long_side_para->small_palm_thd_1, long_side_para->small_palm_thd_2);
        seq_printf(s, "curved_large_judge_para_for_short_side:\n");
        seq_printf(s, "edge_finger_thd:%4d, hold_finger_thd:%4d, normal_finger_thd_1:%4d, normal_finger_thd_2:%4d, normal_finger_thd_3:%4d\n\
    large_palm_thd_1:%4d, large_palm_thd_2:%4d, palm_thd_1:%4d, palm_thd_2:%4d, small_palm_thd_1:%4d, small_palm_thd_2:%4d\n",
                   short_side_para->edge_finger_thd, short_side_para->hold_finger_thd, short_side_para->normal_finger_thd_1,
                   short_side_para->normal_finger_thd_2, short_side_para->normal_finger_thd_3, short_side_para->large_palm_thd_1, short_side_para->large_palm_thd_2,
                   short_side_para->palm_thd_1, short_side_para->palm_thd_2, short_side_para->small_palm_thd_1, short_side_para->small_palm_thd_2);
        seq_printf(s, "\n");
    }

    seq_printf(s, "elimination zone:\n");
    list_for_each(pos, &grip_info->elimination_zone_list) {
        grip_area = (struct grip_zone_area *)pos;

        seq_printf(s, "name:%24s, start_point:(%4d, %4d), width:(%4d, %4d), exit_thd:%4d, support_dir:0x%02x, side:0x%02x.\n",
                   grip_area->name, grip_area->start_x, grip_area->start_y, grip_area->x_width, grip_area->y_width,
                   grip_area->exit_thd, grip_area->support_dir, grip_area->grip_side);
    }
    seq_printf(s, "\n");

    return 0;
}

static int kernel_grip_read_func(struct seq_file *s, void *v)
{
    struct kernel_grip_info *grip_info = s->private;

    if (!grip_info) {
        TPD_INFO("%s read grip info failed.\n", __func__);
    }

    return kernel_grip_print_func(s, grip_info);
}

int touch_get_key_value(char *in, char *check)
{
    int out = 0;
    char *pos = NULL;
    int i = 0, real = 0, in_cnt = 0, check_cnt = 0;

    if (!in || !check) {
        TPD_INFO("%s:null.\n", __func__);
        return -1;
    }

    pos = strstr(in, check);
    if (!pos) {
        TPD_DEBUG("%s:can't find string:%s in %s.\n", __func__, check, in);
        return -1;
    }

    in_cnt = strlen(in);
    check_cnt = strlen(check);
    TPD_DEBUG("checklen:%d, inlen:%d.\n", check_cnt, in_cnt);
    if (':' != pos[check_cnt]) {  //must match the format of string:value
        TPD_INFO("%s:%s do not match the format.\n", __func__, in);
        return -1;
    }

    for (i = check_cnt + 1; i < in_cnt; i++) {     //check valid character
        if (pos[i] < '0' || pos[i] > '9') {
            if ((' ' == pos[i]) || (0 == pos[i]) || ('\n' == pos[i])) {
                break;
            } else if (real) {
                TPD_INFO("%s: incorrect char 0x%02x in %s.\n", __func__, pos[i], in);
                return -1;
            }
        } else {
            real = 1;
            out = (out * 10) + (pos[i] - '0');
            TPD_DEBUG("found char:%c.\n", pos[i]);
        }
    }

    TPD_DEBUG("return:%d.\n", out);
    return out;
}

static int str_to_int(char *in, int start_pos, int end_pos)
{
    int i = 0, value = 0;

    if (start_pos > end_pos) {
        TPD_INFO("wrong pos : (%d, %d).\n", start_pos, end_pos);
        return -1;
    }

    for (i = start_pos; i <= end_pos; i++) {
        value = (value * 10) + (in[i] - '0');
    }

    TPD_DEBUG("%s return %d.\n", __func__, value);
    return value;
}

//parse string according to name:value1,value2,value3...
static int str_parse(char *in, char *name, uint16_t max_len, uint16_t *array, uint16_t array_max)
{
    int i = 0, in_cnt = 0, name_index = 0;
    int start_pos = 0, value_cnt = 0;

    if (!array || !in) {
        TPD_INFO("%s:array or in is null.\n", __func__);
        return -1;
    }

    in_cnt = strlen(in);

    //parse name
    for (i = 0; i < in_cnt; i++) {
        if (':' == in[i]) {     //split name and parameter by ":" symbol
            if (i > max_len) {
                TPD_INFO("%s:string %s name too long.\n", __func__, in);
                return -1;
            }
            name_index = i;
            memcpy(name, in, name_index);   //copy to name buffer
            TPD_DEBUG("%s:set name %s.\n", __func__, name);
        }
    }

    //parse parameter and put it into split_value array
    start_pos = name_index + 1;
    for (i = name_index + 1; i <= in_cnt; i++) {
        if (in[i] < '0' || in[i] > '9') {
            if ((' ' == in[i]) || (0 == in[i]) || ('\n' == in[i]) || (',' == in[i])) {
                if (value_cnt <= array_max) {
                    array[value_cnt++] = str_to_int(in, start_pos, i - 1);
                    start_pos = i + 1;
                } else {
                    TPD_INFO("%s: too many parameter(%s).\n", __func__, in);
                    return -1;
                }
            } else {
                TPD_INFO("%s: incorrect char 0x%02x in %s.\n", __func__, in[i], in);
                return -1;
            }
        }
    }

    return 0;
}

static int grip_area_add_modify(struct list_head *handle_list, char *in, bool is_add, struct kernel_grip_info *grip_info)
{
    int ret = 0;
    char name[GRIP_TAG_SIZE] = {0};
    uint16_t split_value[MAX_AREA_PARAMETER] = {0};
    struct list_head *pos = NULL;
    struct grip_zone_area *grip_zone = NULL;
    struct fw_grip_operations *op = grip_info->fw_ops;

    if (!handle_list || !in) {
        TPD_INFO("%s:handle_list or in is null.\n", __func__);
        return -1;
    }

    ret = str_parse(in, name, GRIP_TAG_SIZE, split_value, MAX_AREA_PARAMETER);
    if (ret < 0) {
        TPD_INFO("%s str parse failed.\n", __func__);
        return -1;
    }

    if (is_add) {
        list_for_each(pos, handle_list) {
            grip_zone = (struct grip_zone_area *)pos;
            if (strstr(name, grip_zone->name)) {
                TPD_INFO("%s: same string(%s, %s).\n", __func__, name, grip_zone->name);
                return -1;
            }
        }

        grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
        if (!grip_zone) {
            TPD_INFO("%s kmalloc failed.\n", __func__);
            return -1;
        }
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "%s", name);
        grip_zone->start_x = split_value[0];
        grip_zone->start_y = split_value[1];
        grip_zone->x_width = split_value[2];
        grip_zone->y_width = split_value[3];
        grip_zone->exit_thd = split_value[4];
        grip_zone->support_dir = split_value[5];
        grip_zone->grip_side = split_value[6];
        list_add_tail(&grip_zone->area_list, handle_list);
        TPD_INFO("%s add: [%d, %d] [%d %d] %d %d %d.\n",
                 grip_zone->name, grip_zone->start_x, grip_zone->start_y,
                 grip_zone->x_width, grip_zone->y_width, grip_zone->exit_thd,
                 grip_zone->support_dir, grip_zone->grip_side);

        if (grip_info->grip_handle_in_fw && op && op->set_fw_grip_area
            && g_tp && !g_tp->loading_fw) {
            ret = op->set_fw_grip_area(grip_zone, true);
            if (ret < 0) {
                TPD_INFO("%s: set grip area in fw failed !\n", __func__);
                return ret;
            }
        }

    } else {
        list_for_each(pos, handle_list) {
            grip_zone = (struct grip_zone_area *)pos;
            if (strstr(in, grip_zone->name)) {
                grip_zone->start_x = split_value[0];
                grip_zone->start_y = split_value[1];
                grip_zone->x_width = split_value[2];
                grip_zone->y_width = split_value[3];
                grip_zone->exit_thd = split_value[4];
                grip_zone->support_dir = split_value[5];
                grip_zone->grip_side = split_value[6];
                if (grip_info->is_curved_screen) {
                    grip_zone->exit_tx_er = split_value[7];
                    grip_zone->exit_rx_er = split_value[8];
                    TPD_INFO("%s modify: [%d, %d] [%d %d] %d %d %d %d %d.\n",
                             grip_zone->name, grip_zone->start_x, grip_zone->start_y,
                             grip_zone->x_width, grip_zone->y_width, grip_zone->exit_thd,
                             grip_zone->support_dir, grip_zone->grip_side,
                             grip_zone->exit_tx_er, grip_zone->exit_rx_er);
                } else {
                    TPD_INFO("%s modify: [%d, %d] [%d %d] %d %d %d.\n",
                             grip_zone->name, grip_zone->start_x, grip_zone->start_y,
                             grip_zone->x_width, grip_zone->y_width, grip_zone->exit_thd,
                             grip_zone->support_dir, grip_zone->grip_side);
                }

                if (grip_info->grip_handle_in_fw && op && op->set_fw_grip_area
                    && g_tp && !g_tp->loading_fw) {
                    ret = op->set_fw_grip_area(grip_zone, true);
                    if (ret < 0) {
                        TPD_INFO("%s: set grip area in fw failed !\n", __func__);
                        return ret;
                    }
                }
            }
        }
    }

    return 0;
}

static void grip_area_del(struct list_head *handle_list, char *in, struct kernel_grip_info *grip_info)
{
    struct list_head *pos = NULL;
    struct grip_zone_area *grip_area = NULL;
    struct fw_grip_operations *op = grip_info->fw_ops;

    if (!handle_list || !in) {
        TPD_INFO("%s:handle_list or in is null.\n", __func__);
        return;
    }

    list_for_each(pos, handle_list) {
        grip_area = (struct grip_zone_area *)pos;
        if (strstr(in, grip_area->name)) {
            if (grip_info->grip_handle_in_fw && op && op->set_fw_grip_area
                && g_tp && !g_tp->loading_fw) {
                op->set_fw_grip_area(grip_area, false);
            }
            list_del(&grip_area->area_list);
            kfree(grip_area);
            grip_area = NULL;
            TPD_INFO("%s:remove area: %s.\n", __func__, in);
            return;
        }
    }

    TPD_INFO("%s:can not found  area: %s.\n", __func__, in);
    return;
}

static void skip_area_modify(struct kernel_grip_info *grip_info, char *in)
{
    int ret = 0;
    char name[GRIP_TAG_SIZE] = {0};
    uint16_t split_value[MAX_AREA_PARAMETER] = {0};
    struct fw_grip_operations *op = NULL;

    if (!grip_info || !in) {
        TPD_INFO("%s:grip_info or in is null.\n", __func__);
        return;
    }

    op = grip_info->fw_ops;

    ret = str_parse(in, name, GRIP_TAG_SIZE, split_value, MAX_AREA_PARAMETER);
    if (ret < 0) {
        TPD_INFO("%s str parse failed.\n", __func__);
        return;
    }

    grip_info->no_handle_dir = split_value[0];
    grip_info->no_handle_y1 = split_value[1];
    grip_info->no_handle_y2 = split_value[2];
    TPD_INFO("set skip (%d,%d,%d).\n", grip_info->no_handle_dir, grip_info->no_handle_y1, grip_info->no_handle_y2);

    if (grip_info->grip_handle_in_fw && op && op->set_no_handle_area
        && g_tp && !g_tp->loading_fw) {
        ret = op->set_no_handle_area(grip_info);
        if (ret < 0) {
            TPD_INFO("%s: set no handle area in fw failed !\n", __func__);
        }
    }

    return;
}

//format is opearation object name:x,y,z,m,m
static int kernel_grip_parse(struct kernel_grip_info *grip_info, char *input, int len)
{
    int value = 0, ret = 0;
    operate_cmd cmd = OPERATE_UNKNOW;
    operate_oject object = OBJECT_UNKNOW;
    struct list_head *handle_list = NULL;
    int i = 0, str_cnt = 0, start_pos = 0, end_pos = 0;
    char split_str[MAX_STRING_CNT][GRIP_TAG_SIZE] = {{0}};
    struct fw_grip_operations *op = grip_info->fw_ops;
    struct curved_judge_para *long_side_para = &grip_info->curved_long_side_para;
    struct curved_judge_para *short_side_para = &grip_info->curved_short_side_para;

    //split string using space
    for (i = 1; i < len; i++) {
        if ((' ' == input[i]) || (len - 1 == i)) {      //find a space or to the end
            if (' ' == input[i]) {
                end_pos = i - 1;
            } else {
                end_pos = i;
            }

            if (end_pos - start_pos + 1 > GRIP_TAG_SIZE) {
                input[i] = '\0';
                TPD_INFO("found too long string:%s, return.\n", &input[start_pos]);
                return 0;
            }
            if (str_cnt >= MAX_STRING_CNT) {
                input[i] = '\0';
                TPD_INFO("found too many string:%d, last:%s.\n", str_cnt, &input[start_pos]);
                return 0;
            }

            memcpy(split_str[str_cnt], &input[start_pos], end_pos - start_pos + 1);   //copy to str buffer
            start_pos = i + 1;
            str_cnt++;
        }
    }

    i = 0;
    str_cnt--;          //get real count
    while (i < str_cnt) {
        if (OPERATE_UNKNOW == cmd) {
            if (!strcmp(split_str[i], "add")) {
                cmd = OPERATE_ADD;
                i++;
            } else if (!strcmp(split_str[i], "del")) {
                cmd = OPERATE_DELTE;
                i++;
            } else if (!strcmp(split_str[i], "mod")) {
                cmd = OPERATE_MODIFY;
                i++;
            }
        }

        if (OBJECT_UNKNOW == object) {
            if (!strcmp(split_str[i], "para")) {
                object = OBJECT_PARAMETER;
                i++;
                TPD_DEBUG("set object to para.\n");
            } else if (!strcmp(split_str[i], "long_curved_para")) {
                object = OBJECT_LONG_CURVED_PARAMETER;
                i++;
                TPD_DEBUG("set object to curved screen judge para for long side.\n");
            } else if (!strcmp(split_str[i], "short_curved_para")) {
                object = OBJECT_SHORT_CURVED_PARAMETER;
                i++;
                TPD_DEBUG("set object to curved screen judge para for short side.\n");
            } else if (!strcmp(split_str[i], "condition_area")) {
                handle_list = &grip_info->condition_zone_list;
                object = OBJECT_CONDITION_AREA;
                i++;
                TPD_DEBUG("set object to condition_area.\n");
            } else if (!strcmp(split_str[i], "large_area")) {
                handle_list = &grip_info->large_zone_list;
                object = OBJECT_LARGE_AREA;
                i++;
                TPD_DEBUG("set object to large_area.\n");
            } else if (!strcmp(split_str[i], "eli_area")) {
                handle_list = &grip_info->elimination_zone_list;
                object = OBJECT_ELI_AREA;
                i++;
                TPD_DEBUG("set object to eli_area.\n");
            } else if (!strcmp(split_str[i], "dead_area")) {
                handle_list = &grip_info->dead_zone_list;
                object = OBJECT_DEAD_AREA;
                i++;
                TPD_DEBUG("set object to dead_area.\n");
            } else if (!strcmp(split_str[i], "edgescreen")) {
                object = OBJECT_SKIP_HANDLE;
                i++;
                TPD_DEBUG("set object to edgescreen.\n");
            }
        }

        if (OBJECT_PARAMETER == object) {
            if(OPERATE_MODIFY == cmd) {
                if ((value = touch_get_key_value(split_str[i], "condition_frame_limit")) >= 0) {
                    grip_info->condition_frame_limit = value;
                    TPD_INFO("change condition_frame_limit to %d.\n", value);
                    if (grip_info->grip_handle_in_fw && op && op->set_condition_frame_limit
                        && g_tp && !g_tp->loading_fw) {
                        ret = op->set_condition_frame_limit(value);
                        if (ret < 0) {
                            TPD_INFO("%s: set condition frame limit in fw failed !\n", __func__);
                        }
                    }
                } else if ((value = touch_get_key_value(split_str[i], "condition_updelay_ms")) >= 0) {
                    grip_info->condition_updelay_ms = value;
                    TPD_INFO("change condition_updelay_ms to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "large_frame_limit"))  >= 0) {
                    grip_info->large_frame_limit = value;
                    TPD_INFO("change large_frame_limit to %d.\n", value);
                    if (grip_info->grip_handle_in_fw && op && op->set_large_frame_limit
                        && g_tp && !g_tp->loading_fw) {
                        ret = op->set_large_frame_limit(value);
                        if (ret < 0) {
                            TPD_INFO("%s: set condition frame limit in fw failed !\n", __func__);
                        }
                    }
                } else if ((value = touch_get_key_value(split_str[i], "large_ver_thd"))  >= 0) {
                    grip_info->large_ver_thd = value;
                    TPD_INFO("change large_ver_thd to %d.\n", value);
                    if (grip_info->grip_handle_in_fw && op && op->set_large_ver_thd
                        && g_tp && !g_tp->loading_fw) {
                        ret = op->set_large_ver_thd(value);
                        if (ret < 0) {
                            TPD_INFO("%s: set large ver thd in fw failed !\n", __func__);
                        }
                    }
                } else if ((value = touch_get_key_value(split_str[i], "large_hor_thd"))  >= 0) {
                    grip_info->large_hor_thd = value;
                    TPD_INFO("change large_hor_thd to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "large_hor_corner_thd")) >= 0) {
                    grip_info->large_hor_corner_thd = value;
                    TPD_INFO("change large_hor_corner_thd to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "large_ver_corner_thd"))  >= 0) {
                    grip_info->large_ver_corner_thd = value;
                    TPD_INFO("change large_ver_corner_thd to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "large_corner_frame_limit"))  >= 0) {
                    grip_info->large_corner_frame_limit = value;
                    TPD_INFO("change large_corner_frame_limit to %d.\n", value);
                    if (grip_info->grip_handle_in_fw && op && op->set_large_corner_frame_limit
                        && g_tp && !g_tp->loading_fw) {
                        ret = op->set_large_corner_frame_limit(value);
                        if (ret < 0) {
                            TPD_INFO("%s: set large condition frame limit in fw failed !\n", __func__);
                        }
                    }
                } else if ((value = touch_get_key_value(split_str[i], "large_ver_corner_width"))  >= 0) {
                    grip_info->large_ver_corner_width = value;
                    TPD_INFO("change large_ver_corner_width to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "large_hor_corner_width"))  >= 0) {
                    grip_info->large_hor_corner_width = value;
                    TPD_INFO("change large_hor_corner_width to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "large_corner_distance"))  >= 0) {
                    grip_info->large_corner_distance = value;
                    TPD_INFO("change large_corner_distance to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "grip_disable_level"))  >= 0) {
                    grip_info->grip_disable_level |= 1 << value;
                    TPD_INFO("change grip_disable_level to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "grip_enable_level"))  >= 0) {
                    grip_info->grip_disable_level &= ~(1 << value);
                    TPD_INFO("change grip_enable_level to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "large_detect_time_ms"))  >= 0) {
                    grip_info->large_detect_time_ms = value;
                    TPD_INFO("change large_detect_time_ms to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "down_delta_time_ms"))  >= 0) {
                    grip_info->down_delta_time_ms = value;
                    TPD_INFO("change down_delta_time_ms to %d.\n", value);
                } else {
                    TPD_INFO("not support:%s.\n", split_str[i]);
                }
            } else {
                TPD_INFO("not support %d opeartion for parameter modify.\n", cmd);
            }
        } else if (OBJECT_LONG_CURVED_PARAMETER == object) {
            if(OPERATE_MODIFY == cmd) {
                if ((value = touch_get_key_value(split_str[i], "edge_finger_thd")) >= 0) {
                    long_side_para->edge_finger_thd = value;
                    TPD_INFO("change long side edge_finger_thd to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "hold_finger_thd")) >= 0) {
                    long_side_para->hold_finger_thd = value;
                    TPD_INFO("change long side hold_finger_thd to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "normal_finger_thd_1"))  >= 0) {
                    long_side_para->normal_finger_thd_1 = value;
                    TPD_INFO("change long side normal_finger_thd_1 to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "normal_finger_thd_2"))  >= 0) {
                    long_side_para->normal_finger_thd_2 = value;
                    TPD_INFO("change long side normal_finger_thd_2 to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "normal_finger_thd_3"))  >= 0) {
                    long_side_para->normal_finger_thd_3 = value;
                    TPD_INFO("change long side normal_finger_thd_3 to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "large_palm_thd_1"))  >= 0) {
                    long_side_para->large_palm_thd_1 = value;
                    TPD_INFO("change long side large_palm_thd_1 to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "large_palm_thd_2")) >= 0) {
                    long_side_para->large_palm_thd_2 = value;
                    TPD_INFO("change long side large_palm_thd_2 to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "small_palm_thd_1"))  >= 0) {
                    long_side_para->small_palm_thd_1 = value;
                    TPD_INFO("change long side small_palm_thd_1 to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "small_palm_thd_2"))  >= 0) {
                    long_side_para->small_palm_thd_2 = value;
                    TPD_INFO("change long side small_palm_thd_2 to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "palm_thd_1"))  >= 0) {
                    long_side_para->palm_thd_1 = value;
                    TPD_INFO("change long side palm_thd_1 to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "palm_thd_2"))  >= 0) {
                    long_side_para->palm_thd_2 = value;
                    TPD_INFO("change long side palm_thd_2 to %d.\n", value);
                } else {
                    TPD_INFO("not support:%s.\n", split_str[i]);
                }
            } else {
                TPD_INFO("not support %d opeartion for long sid curved screen parameter modify.\n", cmd);
            }
        } else if (OBJECT_SHORT_CURVED_PARAMETER == object) {
            if(OPERATE_MODIFY == cmd) {
                if ((value = touch_get_key_value(split_str[i], "edge_finger_thd")) >= 0) {
                    short_side_para->edge_finger_thd = value;
                    TPD_INFO("change short side edge_finger_thd to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "hold_finger_thd")) >= 0) {
                    short_side_para->hold_finger_thd = value;
                    TPD_INFO("change short side hold_finger_thd to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "normal_finger_thd_1"))  >= 0) {
                    short_side_para->normal_finger_thd_1 = value;
                    TPD_INFO("change short side normal_finger_thd_1 to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "normal_finger_thd_2"))  >= 0) {
                    short_side_para->normal_finger_thd_2 = value;
                    TPD_INFO("change short side normal_finger_thd_2 to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "normal_finger_thd_3"))  >= 0) {
                    short_side_para->normal_finger_thd_3 = value;
                    TPD_INFO("change short side normal_finger_thd_3 to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "large_palm_thd_1"))  >= 0) {
                    short_side_para->large_palm_thd_1 = value;
                    TPD_INFO("change short side large_palm_thd_1 to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "large_palm_thd_2")) >= 0) {
                    short_side_para->large_palm_thd_2 = value;
                    TPD_INFO("change short side large_palm_thd_2 to %d.\n", value);
                }  else if ((value = touch_get_key_value(split_str[i], "small_palm_thd_1"))  >= 0) {
                    short_side_para->small_palm_thd_1 = value;
                    TPD_INFO("change short side small_palm_thd_1 to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "small_palm_thd_2"))  >= 0) {
                    short_side_para->small_palm_thd_2 = value;
                    TPD_INFO("change short side small_palm_thd_2 to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "palm_thd_1"))  >= 0) {
                    short_side_para->palm_thd_1 = value;
                    TPD_INFO("change short side palm_thd_1 to %d.\n", value);
                } else if ((value = touch_get_key_value(split_str[i], "palm_thd_2"))  >= 0) {
                    short_side_para->palm_thd_2 = value;
                    TPD_INFO("change short side palm_thd_2 to %d.\n", value);
                } else {
                    TPD_INFO("not support:%s.\n", split_str[i]);
                }
            } else {
                TPD_INFO("not support %d opeartion for short sid curved screen parameter modify.\n", cmd);
            }
        } else if ((OBJECT_CONDITION_AREA == object) || (OBJECT_LARGE_AREA == object) ||
                   (OBJECT_ELI_AREA == object) || (OBJECT_DEAD_AREA == object)) {
            if (OPERATE_ADD == cmd) {
                TPD_DETAIL("add %d by %s.\n", object, split_str[i]);
                grip_area_add_modify(handle_list, split_str[i], true, grip_info);       //add new area to list
            } else if (OPERATE_DELTE == cmd) {
                TPD_DETAIL("del %d by %s.\n", object, split_str[i]);
                grip_area_del(handle_list, split_str[i], grip_info);       //del area from list
            } else if (OPERATE_MODIFY == cmd) {
                TPD_DETAIL("modify %d by %s.\n", object, split_str[i]);
                grip_area_add_modify(handle_list, split_str[i], false, grip_info);       //modify paramter of the area
            } else {
                TPD_INFO("not support %d opeartion for area modify.\n", cmd);
            }
        } else if (OBJECT_SKIP_HANDLE == object) {
            if (OPERATE_MODIFY == cmd) {
                TPD_INFO("modify %d by %s.\n", object, split_str[i]);
                skip_area_modify(grip_info, split_str[i]);
            } else {
                TPD_INFO("not support %d opeartion for skip handle modify.\n", cmd);
            }
        }

        handle_list = NULL;         //reset handle list
        cmd = OPERATE_UNKNOW;       //reset cmd status
        object = OBJECT_UNKNOW;     //reset object status
        i++;
    }

    return 0;
}

static ssize_t kernel_grip_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    char buf[PAGESIZE] = {0};
    struct kernel_grip_info *grip_info = PDE_DATA(file_inode(file));

    if (!grip_info)
        return count;

    if (count > PAGESIZE) {
        TPD_INFO("%s: count is too large :%d.\n",  __func__, (int)count);
        return count;
    }
    if (copy_from_user(buf, buffer, count)) {
        TPD_INFO("%s: read proc input error.\n", __func__);
        return count;
    }

    mutex_lock(&grip_info->grip_mutex);
    kernel_grip_parse(grip_info, buf, count);
    mutex_unlock(&grip_info->grip_mutex);

    return count;
}

static int kernel_grip_open(struct inode *inode, struct file *file)
{
    return single_open(file, kernel_grip_read_func, PDE_DATA(inode));
}

static const struct file_operations tp_kernel_grip_fops = {
    .owner = THIS_MODULE,
    .open  = kernel_grip_open,
    .read  = seq_read,
    .write = kernel_grip_write,
    .release = single_release,
};

static ssize_t proc_touch_dir_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    ssize_t ret = 0;
    char page[PAGESIZE] = {0};
    struct kernel_grip_info *grip_info = PDE_DATA(file_inode(file));

    if (!grip_info)
        return count;

    snprintf(page, PAGESIZE - 1, "%d\n", grip_info->touch_dir);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    return ret;
}

static ssize_t proc_touch_dir_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    char buf[8] = {0};
    int temp = 0;
    struct kernel_grip_info *grip_info = PDE_DATA(file_inode(file));
    struct fw_grip_operations *op;

    if (!grip_info) {
        TPD_INFO("%s: no value.\n", __func__);
        return count;
    }

    op = grip_info->fw_ops;

    if (count > 2)
        return count;
    if (copy_from_user(buf, buffer, count)) {
        TPD_DEBUG("%s: read proc input error.\n", __func__);
        return count;
    }
    sscanf(buf, "%d", &temp);

    mutex_lock(&grip_info->grip_mutex);
    grip_info->touch_dir = temp;
    if (op && op->set_touch_direction && g_tp && !g_tp->loading_fw) {
        op->set_touch_direction(temp);
    }
    mutex_unlock(&grip_info->grip_mutex);
#ifdef CONFIG_TOUCHPANEL_ALGORITHM
        if (g_tp) {
            set_algorithm_direction(g_tp,temp);
        }
#endif

    TPD_INFO("%s: value = %d\n", __func__, temp);
    return count;
}

static const struct file_operations touch_dir_proc_fops = {
    .read  = proc_touch_dir_read,
    .write = proc_touch_dir_write,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

void init_kernel_grip_proc(struct proc_dir_entry *prEntry_tp, struct kernel_grip_info *grip_info)
{
    int ret = 0;
    struct proc_dir_entry *prEntry_tmp = NULL;

    if (!grip_info || !prEntry_tp)
        return;

    prEntry_tmp = proc_create_data("kernel_grip_handle", 0666, prEntry_tp, &tp_kernel_grip_fops, grip_info);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create kernel grip proc entry, %d\n", __func__, __LINE__);
    }

    prEntry_tmp = proc_create_data("oplus_tp_direction", 0666, prEntry_tp, &touch_dir_proc_fops, grip_info);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create oplus_tp_direction proc entry, %d\n", __func__, __LINE__);
    }
}

static void kernel_grip_release(struct kernel_grip_info *grip_info)
{
    struct list_head *pos = NULL, *tmp = NULL;
    struct grip_zone_area *grip_area = NULL;

    if (!grip_info) {
        TPD_INFO("%s:grip_info is null.\n", __func__);
        return;
    }

    list_for_each_safe(pos, tmp, &grip_info->dead_zone_list) {
        grip_area = (struct grip_zone_area *)pos;
        list_del(&grip_area->area_list);
        kfree(grip_area);
    }

    list_for_each_safe(pos, tmp, &grip_info->condition_zone_list) {
        grip_area = (struct grip_zone_area *)pos;
        list_del(&grip_area->area_list);
        kfree(grip_area);
    }

    list_for_each_safe(pos, tmp, &grip_info->large_zone_list) {
        grip_area = (struct grip_zone_area *)pos;
        list_del(&grip_area->area_list);
        kfree(grip_area);
    }

    list_for_each_safe(pos, tmp, &grip_info->elimination_zone_list) {
        grip_area = (struct grip_zone_area *)pos;
        list_del(&grip_area->area_list);
        kfree(grip_area);
    }

    if (grip_info->coord_buf) {
        kfree(grip_info->coord_buf);
        grip_info->coord_buf = NULL;
    }

    if (grip_info->grip_up_handle_wq) {
        destroy_workqueue(grip_info->grip_up_handle_wq);
        grip_info->grip_up_handle_wq = NULL;
    }

    kfree(grip_info);
    return;
}

void kernel_grip_reset(struct kernel_grip_info *grip_info)
{
    int i = 0;

    if (!grip_info) {
        TPD_INFO("%s:grip_info is null.\n", __func__);
        return;
    }

    for (i = 0; i < TOUCH_MAX_NUM; i++) {
        grip_status_reset(grip_info, i);
    }
    kfifo_reset(&grip_info->up_fifo);
    grip_info->obj_prev_bit = 0;        //clear down bit
    grip_info->record_total_cnt = 0;    //clear touch count

    return;
}

struct kernel_grip_info *kernel_grip_init(struct device *dev)
{
    int i = 0, j = 0, ret = 0, large_corner_para[6] = {0};
    int dead_width[2] = {0}, makeup_para[10] = {0}, large_para[3] = {0}, cond_para[2] = {0},
        no_handle_para[3] = {0}, long_judge_para[20] = {0}, short_judge_para[20] = {0};
    int temp_array[2] = {0}, cond_width[4] = {0}, large_width[2] = {0}, large_corner_width[3] = {0}, eli_width[6] = {0}, curved_large_width[5] = {0};
    struct grip_zone_area *grip_zone = NULL;
    struct kernel_grip_info *grip_info = NULL;

    grip_info = kzalloc(sizeof(struct kernel_grip_info), GFP_KERNEL);
    if (!grip_info) {
        TPD_INFO("kzalloc kernel grip info failed.\n");
        return NULL;
    }

    grip_info->coord_buf = NULL;
    grip_info->grip_up_handle_wq = NULL;

    //parameter init
    mutex_init(&grip_info->grip_mutex);
    ret = of_property_read_u32_array(dev->of_node, "touchpanel,panel-coords", temp_array, 2);
    if (ret) {
        grip_info->max_x = 1080;
        grip_info->max_y = 2340;
        TPD_INFO("panel coords using default.\n");
    } else {
        grip_info->max_x = temp_array[0];
        grip_info->max_y = temp_array[1];
    }

    ret = of_property_read_u32_array(dev->of_node, "prevention,grip_disable_level", temp_array, 1);
    if (ret) {
        grip_info->grip_disable_level = 0;
        TPD_INFO("grip disable level using default.\n");
    } else {
        grip_info->grip_disable_level = temp_array[0];
    }

    ret = of_property_read_u32_array(dev->of_node, "prevention,no_handle_para", no_handle_para, 3);
    if (ret) {
        no_handle_para[0] = 0;
        no_handle_para[1] = 0;
        no_handle_para[2] = 0;
        TPD_INFO("grip no handle para using default.\n");
    }

    ret = of_property_read_u32_array(dev->of_node, "prevention,dead_area_width", dead_width, 2);
    if (ret) {
        dead_width[0] = 10;
        dead_width[1] = 10;
        TPD_INFO("panel coords using default.\n");
    }

    ret = of_property_read_u32_array(dev->of_node, "prevention,makeup_cnt_weight", makeup_para, 5);
    if (ret) {
        makeup_para[0] = 4;
        makeup_para[1] = 1;
        makeup_para[2] = 2;
        makeup_para[3] = 2;
        makeup_para[4] = 1;
        TPD_INFO("makeup cnt and weight using default.\n");
    }

    ret = of_property_read_u32_array(dev->of_node, "prevention,large_judge_para", large_para, 3);
    if (ret) {
        large_para[0] = 3;
        large_para[1] = 300;
        large_para[2] = 300;
        TPD_INFO("large judge para using default.\n");
    }

    ret = of_property_read_u32_array(dev->of_node, "prevention,large_corner_judge_para", large_corner_para, 6);
    if (ret) {
        large_corner_para[0] = 10;
        large_corner_para[1] = 2;
        large_corner_para[2] = 2;
        large_corner_para[3] = 30;
        large_corner_para[4] = 30;
        large_corner_para[5] = 1;
        TPD_INFO("large corner judge para using default.\n");
    }

    grip_info->is_curved_screen = of_property_read_bool(dev->of_node, "prevention,curved_screen");
    if (grip_info->is_curved_screen) {
        TPD_INFO("this is curved screen.\n");
    }

    if (grip_info->is_curved_screen) {
        ret = of_property_read_u32_array(dev->of_node, "prevention,large_curved_long_judge_para", long_judge_para, 11);
        if (ret) {
            long_judge_para[0] = 7;
            long_judge_para[1] = 90;
            long_judge_para[2] = 55;
            long_judge_para[3] = 30;
            long_judge_para[4] = 30;
            long_judge_para[5] = 20;
            long_judge_para[6] = 4;
            long_judge_para[7] = 7;
            long_judge_para[8] = 5;
            long_judge_para[9] = 3;
            long_judge_para[10] = 3;
            TPD_INFO("curved large long side judge para using default.\n");
        }

        ret = of_property_read_u32_array(dev->of_node, "prevention,large_curved_short_judge_para", short_judge_para, 11);
        if (ret) {
            short_judge_para[0] = 7;
            short_judge_para[1] = 90;
            short_judge_para[2] = 55;
            short_judge_para[3] = 0;
            short_judge_para[4] = 30;
            short_judge_para[5] = 20;
            short_judge_para[6] = 4;
            short_judge_para[7] = 7;
            short_judge_para[8] = 5;
            short_judge_para[9] = 2;
            short_judge_para[10] = 2;
            TPD_INFO("curved large corner judge para using default.\n");
        }

        ret = of_property_read_u32_array(dev->of_node, "prevention,curved_large_area_width", curved_large_width, 5);
        if (ret) {
            curved_large_width[0] = 100;
            curved_large_width[1] = 100;
            curved_large_width[2] = 80;
            curved_large_width[3] = 2;
            curved_large_width[4] = 2;
            TPD_INFO("curved large area width para using default.\n");
        }

        ret = of_property_read_u32_array(dev->of_node, "prevention,grip_large_detect_time", temp_array, 1);
        if (ret) {
            grip_info->large_detect_time_ms = 150;
            TPD_INFO("grip large detect times using default.\n");
        } else {
            grip_info->large_detect_time_ms = temp_array[0];
        }

        ret = of_property_read_u32_array(dev->of_node, "prevention,grip_down_delta_time", temp_array, 1);
        if (ret) {
            grip_info->down_delta_time_ms = 100;
            TPD_INFO("grip down delta time using default.\n");
        } else {
            grip_info->down_delta_time_ms = temp_array[0];
        }
    }

    ret = of_property_read_u32_array(dev->of_node, "prevention,condition_judge_para", cond_para, 2);
    if (ret) {
        cond_para[0] = 40;
        cond_para[1] = 50;
        TPD_INFO("condition judge para using default.\n");
    }

    ret = of_property_read_u32_array(dev->of_node, "prevention,condition_area_width", cond_width, 4);
    if (ret) {
        cond_width[0] = 30;
        cond_width[1] = 30;
        cond_width[2] = 100;
        cond_width[3] = 80;
        TPD_INFO("condition area width using default.\n");
    }

    ret = of_property_read_u32_array(dev->of_node, "prevention,large_area_width", large_width, 2);
    if (ret) {
        large_width[0] = 100;
        large_width[1] = 100;
        TPD_INFO("large area width using default.\n");
    }

    ret = of_property_read_u32_array(dev->of_node, "prevention,large_corner_width", large_corner_width, 3);
    if (ret) {
        large_corner_width[0] = 120;
        large_corner_width[1] = 200;
        large_corner_width[2] = 20;
        TPD_INFO("large corner width using default.\n");
    }

    ret = of_property_read_u32_array(dev->of_node, "prevention,eli_area_width", eli_width, 6);
    if (ret) {
        eli_width[0] = 120;
        eli_width[1] = 500;
        eli_width[2] = 250;
        eli_width[3] = 120;
        eli_width[4] = 250;
        eli_width[5] = 120;
        TPD_INFO("eli area width using default.\n");
    }

    grip_info->grip_handle_in_fw = of_property_read_bool(dev->of_node, "prevention,grip_handle_in_fw");
    if (grip_info->grip_handle_in_fw) {
        TPD_INFO("grip area will handle in fw.\n");
    }

    //dead zone grip init
    INIT_LIST_HEAD(&grip_info->dead_zone_list);
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = 0;
        grip_zone->x_width = dead_width[0];
        grip_zone->y_width = grip_info->max_y;
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "ver_left_dead");
        grip_zone->grip_side = 1 << TYPE_LONG_SIDE;
        grip_zone->support_dir = (1 << VERTICAL_SCREEN) | (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &grip_info->dead_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for ver_left_dead failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = grip_info->max_x - dead_width[0];
        grip_zone->start_y = 0;
        grip_zone->x_width = dead_width[0];
        grip_zone->y_width = grip_info->max_y;
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "ver_right_dead");
        grip_zone->grip_side = 1 << TYPE_LONG_SIDE;
        grip_zone->support_dir = (1 << VERTICAL_SCREEN) | (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &grip_info->dead_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for ver_right_dead failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = 0;
        grip_zone->x_width = grip_info->max_x;
        grip_zone->y_width = dead_width[1];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_left_dead");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &grip_info->dead_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_left_dead failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = grip_info->max_y - dead_width[1];
        grip_zone->x_width = grip_info->max_x;
        grip_zone->y_width = dead_width[1];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_right_dead");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &grip_info->dead_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_right_dead failed.\n");
    }

    grip_info->large_frame_limit = large_para[0];
    grip_info->large_ver_thd = large_para[1];
    grip_info->large_hor_thd = large_para[2];
    grip_info->large_corner_frame_limit = large_corner_para[0];
    grip_info->large_ver_corner_thd = large_corner_para[1];
    grip_info->large_hor_corner_thd = large_corner_para[2];
    grip_info->large_ver_corner_width = large_corner_para[3];
    grip_info->large_hor_corner_width = large_corner_para[4];
    grip_info->large_corner_distance = large_corner_para[5];
    grip_info->condition_frame_limit = cond_para[0];
    grip_info->condition_updelay_ms = cond_para[1];
    grip_info->coord_filter_cnt = makeup_para[0];
    grip_info->no_handle_dir = no_handle_para[0];
    grip_info->no_handle_y1 = no_handle_para[1];
    grip_info->no_handle_y2 = no_handle_para[2];

    if (grip_info->is_curved_screen) {
        grip_info->curved_long_side_para.large_palm_thd_1 = long_judge_para[0];
        grip_info->curved_long_side_para.large_palm_thd_2 = long_judge_para[1];
        grip_info->curved_long_side_para.edge_finger_thd = long_judge_para[2];
        grip_info->curved_long_side_para.hold_finger_thd = long_judge_para[3];
        grip_info->curved_long_side_para.normal_finger_thd_1 = long_judge_para[4];
        grip_info->curved_long_side_para.normal_finger_thd_2 = long_judge_para[5];
        grip_info->curved_long_side_para.normal_finger_thd_3 = long_judge_para[6];
        grip_info->curved_long_side_para.palm_thd_1 = long_judge_para[7];
        grip_info->curved_long_side_para.palm_thd_2 = long_judge_para[8];
        grip_info->curved_long_side_para.small_palm_thd_1 = long_judge_para[9];
        grip_info->curved_long_side_para.small_palm_thd_2 = long_judge_para[10];

        grip_info->curved_short_side_para.large_palm_thd_1 = short_judge_para[0];
        grip_info->curved_short_side_para.large_palm_thd_2 = short_judge_para[1];
        grip_info->curved_short_side_para.edge_finger_thd = short_judge_para[2];
        grip_info->curved_short_side_para.hold_finger_thd = short_judge_para[3];
        grip_info->curved_short_side_para.normal_finger_thd_1 = short_judge_para[4];
        grip_info->curved_short_side_para.normal_finger_thd_2 = short_judge_para[5];
        grip_info->curved_short_side_para.normal_finger_thd_3 = short_judge_para[6];
        grip_info->curved_short_side_para.palm_thd_1 = short_judge_para[7];
        grip_info->curved_short_side_para.palm_thd_2 = short_judge_para[8];
        grip_info->curved_short_side_para.small_palm_thd_1 = short_judge_para[9];
        grip_info->curved_short_side_para.small_palm_thd_2 = short_judge_para[10];
    }

    grip_info->coord_buf = kzalloc(TOUCH_MAX_NUM * grip_info->coord_filter_cnt * sizeof(struct coord_buffer), GFP_KERNEL);
    if (grip_info->coord_buf) {
        for (i = 0; i < TOUCH_MAX_NUM; i++) {
            for (j = 0; j < grip_info->coord_filter_cnt; j++) {
                grip_info->coord_buf[i * grip_info->coord_filter_cnt + j].weight = makeup_para[j + 1];
            }
        }
    } else {
        kernel_grip_release(grip_info);
        TPD_INFO("kzalloc grip_zone_area for coord_buffer failed.\n");
        grip_info = NULL;
        return NULL;
    }

    //condition grip init
    INIT_LIST_HEAD(&grip_info->condition_zone_list);
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = 0;
        grip_zone->x_width = cond_width[0];
        grip_zone->y_width = grip_info->max_y;
        grip_zone->exit_thd = cond_width[2];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "ver_left_condtion");
        grip_zone->grip_side = 1 << TYPE_LONG_SIDE;
        grip_zone->support_dir = (1 << VERTICAL_SCREEN) | (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &grip_info->condition_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for coord_buffer failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = grip_info->max_x - cond_width[0];
        grip_zone->start_y = 0;
        grip_zone->x_width = cond_width[0];
        grip_zone->y_width = grip_info->max_y;
        grip_zone->exit_thd = cond_width[2];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "ver_right_condtion");
        grip_zone->grip_side = 1 << TYPE_LONG_SIDE;
        grip_zone->support_dir = (1 << VERTICAL_SCREEN) | (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &grip_info->condition_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for ver_right_condtion failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = 0;
        grip_zone->x_width = grip_info->max_x;
        grip_zone->y_width = cond_width[1];
        grip_zone->exit_thd = cond_width[3];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_left_condtion");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &grip_info->condition_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_left_condtion failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = grip_info->max_y - cond_width[1];
        grip_zone->x_width = grip_info->max_x;
        grip_zone->y_width = cond_width[1];
        grip_zone->exit_thd = cond_width[3];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_right_condtion");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &grip_info->condition_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_right_condtion failed.\n");
    }

    //corner large grip init
    INIT_LIST_HEAD(&grip_info->large_zone_list);
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = 0;
        grip_zone->x_width = large_corner_width[1];
        grip_zone->y_width = large_corner_width[0];
        grip_zone->exit_thd = large_corner_width[2];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor90_left_corner_large");
        grip_zone->grip_side = 1 << TYPE_SHORT_CORNER_SIDE;
        grip_zone->support_dir = (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &grip_info->large_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor90_left_large_corner failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = grip_info->max_y - large_corner_width[0];
        grip_zone->x_width = large_corner_width[1];
        grip_zone->y_width = large_corner_width[0];
        grip_zone->exit_thd = large_corner_width[2];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor90_right_corner_large");
        grip_zone->grip_side = 1 << TYPE_SHORT_CORNER_SIDE;
        grip_zone->support_dir = (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &grip_info->large_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor90_right_large_corner failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = grip_info->max_x - large_corner_width[1];
        grip_zone->start_y = 0;
        grip_zone->x_width = large_corner_width[1];
        grip_zone->y_width = large_corner_width[0];
        grip_zone->exit_thd = large_corner_width[2];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor270_left_corner_large");
        grip_zone->grip_side = 1 << TYPE_SHORT_CORNER_SIDE;
        grip_zone->support_dir = (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &grip_info->large_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor270_left_large_corner failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = grip_info->max_x - large_corner_width[1];
        grip_zone->start_y = grip_info->max_y - large_corner_width[0];
        grip_zone->x_width = large_corner_width[1];
        grip_zone->y_width = large_corner_width[0];
        grip_zone->exit_thd = large_corner_width[2];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor270_right_corner_large");
        grip_zone->grip_side = 1 << TYPE_SHORT_CORNER_SIDE;
        grip_zone->support_dir = (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &grip_info->large_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor270_right_large_corner failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = grip_info->max_y - large_corner_width[1];
        grip_zone->x_width = large_corner_width[0];
        grip_zone->y_width = large_corner_width[1];
        grip_zone->exit_thd = large_corner_width[2];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "ver_left_bottom_large");
        grip_zone->grip_side = 1 << TYPE_LONG_CORNER_SIDE;
        grip_zone->support_dir = 1 << VERTICAL_SCREEN;
        list_add_tail(&grip_zone->area_list, &grip_info->large_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for ver_left_bottom_large failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = grip_info->max_x - large_corner_width[0];
        grip_zone->start_y = grip_info->max_y - large_corner_width[1];
        grip_zone->x_width = large_corner_width[0];
        grip_zone->y_width = large_corner_width[1];
        grip_zone->exit_thd = large_corner_width[2];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "ver_right_bottom_large");
        grip_zone->grip_side = 1 << TYPE_LONG_CORNER_SIDE;
        grip_zone->support_dir = 1 << VERTICAL_SCREEN;
        list_add_tail(&grip_zone->area_list, &grip_info->large_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for ver_right_bottom_large failed.\n");
    }

    //large grip init
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = 0;
        grip_zone->x_width = grip_info->max_x;
        grip_zone->y_width = large_width[1];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_left_large");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &grip_info->large_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_left_large failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = grip_info->max_y - large_width[1];
        grip_zone->x_width = grip_info->max_x;
        grip_zone->y_width = large_width[1];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_right_large");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &grip_info->large_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_right_large failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = 0;
        grip_zone->x_width = large_width[0];
        grip_zone->y_width = grip_info->max_y;
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "ver_left_large");
        grip_zone->grip_side = 1 << TYPE_LONG_SIDE;
        grip_zone->support_dir = (1 << VERTICAL_SCREEN) | (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &grip_info->large_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for ver_left_large failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = grip_info->max_x - large_width[0];
        grip_zone->start_y = 0;
        grip_zone->x_width = large_width[0];
        grip_zone->y_width = grip_info->max_y;
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "ver_right_large");
        grip_zone->grip_side = 1 << TYPE_LONG_SIDE;
        grip_zone->support_dir = (1 << VERTICAL_SCREEN) | (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
        list_add_tail(&grip_zone->area_list, &grip_info->large_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for ver_right_large failed.\n");
    }

    //curved large grip init
    if (grip_info->is_curved_screen) {
        grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
        if (grip_zone) {
            grip_zone->start_x = 0;
            grip_zone->start_y = 0;
            grip_zone->x_width = grip_info->max_x;
            grip_zone->y_width = curved_large_width[1];
            grip_zone->exit_thd = curved_large_width[2];
            grip_zone->exit_tx_er = curved_large_width[3];
            grip_zone->exit_rx_er = curved_large_width[4];
            snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "curved_hor_left_large");
            grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
            grip_zone->support_dir = (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
            list_add_tail(&grip_zone->area_list, &grip_info->large_zone_list);
        } else {
            TPD_INFO("kzalloc grip_zone_area for hor_left_large failed.\n");
        }
        grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
        if (grip_zone) {
            grip_zone->start_x = 0;
            grip_zone->start_y = grip_info->max_y - curved_large_width[1];
            grip_zone->x_width = grip_info->max_x;
            grip_zone->y_width = curved_large_width[1];
            grip_zone->exit_thd = curved_large_width[2];
            grip_zone->exit_tx_er = curved_large_width[3];
            grip_zone->exit_rx_er = curved_large_width[4];
            snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "curved_hor_right_large");
            grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
            grip_zone->support_dir = (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
            list_add_tail(&grip_zone->area_list, &grip_info->large_zone_list);
        } else {
            TPD_INFO("kzalloc grip_zone_area for hor_right_large failed.\n");
        }
        grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
        if (grip_zone) {
            grip_zone->start_x = 0;
            grip_zone->start_y = 0;
            grip_zone->x_width = curved_large_width[0];
            grip_zone->y_width = grip_info->max_y;
            grip_zone->exit_thd = curved_large_width[2];
            grip_zone->exit_tx_er = curved_large_width[3];
            grip_zone->exit_rx_er = curved_large_width[4];
            snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "curved_ver_left_large");
            grip_zone->grip_side = 1 << TYPE_LONG_SIDE;
            grip_zone->support_dir = (1 << VERTICAL_SCREEN) | (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
            list_add_tail(&grip_zone->area_list, &grip_info->large_zone_list);
        } else {
            TPD_INFO("kzalloc grip_zone_area for ver_left_large failed.\n");
        }
        grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
        if (grip_zone) {
            grip_zone->start_x = grip_info->max_x - curved_large_width[0];
            grip_zone->start_y = 0;
            grip_zone->x_width = curved_large_width[0];
            grip_zone->y_width = grip_info->max_y;
            grip_zone->exit_thd = curved_large_width[2];
            grip_zone->exit_tx_er = curved_large_width[3];
            grip_zone->exit_rx_er = curved_large_width[4];
            snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "curved_ver_right_large");
            grip_zone->grip_side = 1 << TYPE_LONG_SIDE;
            grip_zone->support_dir = (1 << VERTICAL_SCREEN) | (1 << LANDSCAPE_SCREEN_90) | (1 << LANDSCAPE_SCREEN_270);
            list_add_tail(&grip_zone->area_list, &grip_info->large_zone_list);
        } else {
            TPD_INFO("kzalloc grip_zone_area for ver_right_large failed.\n");
        }
    }

    ret = kfifo_alloc(&grip_info->up_fifo, PAGE_SIZE, GFP_KERNEL);
    if (ret) {
        kernel_grip_release(grip_info);
        TPD_INFO("up_fifo malloc failed.\n");
        grip_info = NULL;
        return NULL;
    }
    for (i = 0; i < TOUCH_MAX_NUM; i++) {
        hrtimer_init(&grip_info->grip_up_timer[i], CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        grip_info->grip_up_timer[i].function = touch_up_timer_func;
        INIT_WORK(&grip_info->grip_up_work[i], touch_report_work);
    }
    grip_info->grip_up_handle_wq = create_singlethread_workqueue("touch_up_wq");

    //elimination grip init
    INIT_LIST_HEAD(&grip_info->elimination_zone_list);
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = grip_info->max_y - eli_width[1];
        grip_zone->x_width = eli_width[0];
        grip_zone->y_width = eli_width[1];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "ver_left_eli");
        grip_zone->grip_side = 1 << TYPE_LONG_SIDE;
        grip_zone->support_dir = 1 << VERTICAL_SCREEN;
        list_add_tail(&grip_zone->area_list, &grip_info->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for ver_left_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = grip_info->max_x - eli_width[0];
        grip_zone->start_y = grip_info->max_y - eli_width[1];
        grip_zone->x_width = eli_width[0];
        grip_zone->y_width = eli_width[1];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "ver_right_eli");
        grip_zone->grip_side = 1 << TYPE_LONG_SIDE;
        grip_zone->support_dir = 1 << VERTICAL_SCREEN;
        list_add_tail(&grip_zone->area_list, &grip_info->elimination_zone_list);
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
        list_add_tail(&grip_zone->area_list, &grip_info->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_90_left_0_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = 0;
        grip_zone->x_width = eli_width[5];
        grip_zone->y_width = eli_width[4];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_90_left_1_eli");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = 1 << LANDSCAPE_SCREEN_90;
        list_add_tail(&grip_zone->area_list, &grip_info->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_90_left_1_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = grip_info->max_y - eli_width[3];
        grip_zone->x_width = eli_width[2];
        grip_zone->y_width = eli_width[3];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_90_right_0_eli");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = 1 << LANDSCAPE_SCREEN_90;
        list_add_tail(&grip_zone->area_list, &grip_info->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_90_right_0_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = 0;
        grip_zone->start_y = grip_info->max_y - eli_width[4];
        grip_zone->x_width = eli_width[5];
        grip_zone->y_width = eli_width[4];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_90_right_1_eli");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = 1 << LANDSCAPE_SCREEN_90;
        list_add_tail(&grip_zone->area_list, &grip_info->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_90_right_1_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = grip_info->max_x - eli_width[2];
        grip_zone->start_y = 0;
        grip_zone->x_width = eli_width[2];
        grip_zone->y_width = eli_width[3];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_270_left_0_eli");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = 1 << LANDSCAPE_SCREEN_270;
        list_add_tail(&grip_zone->area_list, &grip_info->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_270_left_0_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = grip_info->max_x - eli_width[5];
        grip_zone->start_y = 0;
        grip_zone->x_width = eli_width[5];
        grip_zone->y_width = eli_width[4];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_270_left_1_eli");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = 1 << LANDSCAPE_SCREEN_270;
        list_add_tail(&grip_zone->area_list, &grip_info->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_270_left_1_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = grip_info->max_x - eli_width[2];
        grip_zone->start_y = grip_info->max_y - eli_width[3];
        grip_zone->x_width = eli_width[2];
        grip_zone->y_width = eli_width[3];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_270_right_0_eli");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = 1 << LANDSCAPE_SCREEN_270;
        list_add_tail(&grip_zone->area_list, &grip_info->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_270_right_0_eli failed.\n");
    }
    grip_zone = kzalloc(sizeof(struct grip_zone_area), GFP_KERNEL);
    if (grip_zone) {
        grip_zone->start_x = grip_info->max_x - eli_width[5];
        grip_zone->start_y = grip_info->max_y - eli_width[4];
        grip_zone->x_width = eli_width[5];
        grip_zone->y_width = eli_width[4];
        snprintf(grip_zone->name, GRIP_TAG_SIZE - 1, "hor_270_right_1_eli");
        grip_zone->grip_side = 1 << TYPE_SHORT_SIDE;
        grip_zone->support_dir = 1 << LANDSCAPE_SCREEN_270;
        list_add_tail(&grip_zone->area_list, &grip_info->elimination_zone_list);
    } else {
        TPD_INFO("kzalloc grip_zone_area for hor_270_right_1_eli failed.\n");
    }

    return grip_info;
}
