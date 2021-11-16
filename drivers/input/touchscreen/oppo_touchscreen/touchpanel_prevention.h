/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _TOUCHPANEL_PREVENTION_H_
#define _TOUCHPANEL_PREVENTION_H_

#include <linux/uaccess.h>
#include <linux/kfifo.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

#define TOUCH_MAX_NUM               (10)
#define GRIP_TAG_SIZE               (64)
#define MAX_STRING_CNT              (15)
#define MAX_AREA_PARAMETER          (10)
#define UP2CANCEL_PRESSURE_VALUE    (0xFF)

typedef enum edge_grip_side {
    TYPE_UNKNOW,
    TYPE_LONG_SIDE,
    TYPE_SHORT_SIDE,
    TYPE_LONG_CORNER_SIDE,
    TYPE_SHORT_CORNER_SIDE,
} grip_side;

/*shows a rectangle
* x start at @start_x and end at @start_x+@x_width
* y start at @start_y and end at @start_y+@y_width
* @area_list: list used for connected with other area
* @start_x: original x coordinate
* @start_y: original y coordinate
* @x_width: witdh in x axis
* @y_width: witdh in y axis
*/
struct grip_zone_area {
    struct list_head        area_list;  //must be the first member to easy get the pointer of grip_zone_area
    uint16_t                start_x;
    uint16_t                start_y;
    uint16_t                x_width;
    uint16_t                y_width;
    uint16_t                exit_thd;
    uint16_t                exit_tx_er;
    uint16_t                exit_rx_er;
    uint16_t                support_dir;
    uint16_t                grip_side;
    char                    name[GRIP_TAG_SIZE];
};

struct coord_buffer {
    uint16_t            x;
    uint16_t            y;
    uint16_t            weight;
};

enum large_judge_status {
    JUDGE_LARGE_CONTINUE,
    JUDGE_LARGE_TIMEOUT,
    JUDGE_LARGE_OK,
};

enum large_reject_type {
    TYPE_REJECT_NONE,
    TYPE_REJECT_HOLD,
    TYPE_REJECT_DONE,
};

enum large_finger_status {
    TYPE_NORMAL_FINGER,
    TYPE_HOLD_FINGER,
    TYPE_EDGE_FINGER,
    TYPE_PALM_SHORT_SIZE,
    TYPE_PALM_LONG_SIZE,
    TYPE_SMALL_PALM_CORNER,
    TYPE_PALM_CORNER,
    TYPE_LARGE_PALM_CORNER,
};

enum large_point_status {
    UP_POINT,
    DOWN_POINT,
    DOWN_POINT_NEED_MAKEUP,
};

enum grip_diable_level {
    GRIP_DISABLE_LARGE,
    GRIP_DISABLE_ELI,
    GRIP_DISABLE_UP2CANCEL,
};

typedef enum grip_operate_cmd {
    OPERATE_UNKNOW,
    OPERATE_ADD,
    OPERATE_DELTE,
    OPERATE_MODIFY,
} operate_cmd;

typedef enum grip_operate_object {
    OBJECT_UNKNOW,
    OBJECT_PARAMETER,
    OBJECT_LONG_CURVED_PARAMETER,    //for modify curved srceen judge para for long side
    OBJECT_SHORT_CURVED_PARAMETER,   //for modify curved srceen judge para for short side
    OBJECT_CONDITION_AREA,      //for modify condition area
    OBJECT_LARGE_AREA,          //for modify large judge area
    OBJECT_ELI_AREA,            //for modify elimination area
    OBJECT_DEAD_AREA,           //for modify dead area
    OBJECT_SKIP_HANDLE,         //for modify no handle setting
} operate_oject;

struct grip_point_info {
    uint16_t x;
    uint16_t y;
    uint8_t  tx_press;
    uint8_t  rx_press;
    uint8_t  tx_er;
    uint8_t  rx_er;
    s64 time_ms;  // record the point first down time
};

struct curved_judge_para {
    uint16_t edge_finger_thd;
    uint16_t hold_finger_thd;
    uint16_t normal_finger_thd_1;
    uint16_t normal_finger_thd_2;
    uint16_t normal_finger_thd_3;
    uint16_t large_palm_thd_1;
    uint16_t large_palm_thd_2;
    uint16_t palm_thd_1;
    uint16_t palm_thd_2;
    uint16_t small_palm_thd_1;
    uint16_t small_palm_thd_2;
};

struct kernel_grip_info {
    int                       touch_dir;                              /*shows touchpanel direction*/
    uint32_t                  max_x;                                  /*touchpanel width*/
    uint32_t                  max_y;                                  /*touchpanel height*/
    struct mutex              grip_mutex;                             /*using for protect grip parameter working*/
    uint32_t                  no_handle_y1;                           /*min y of no grip handle*/
    uint32_t                  no_handle_y2;                           /*max y of no grip handle*/
    uint8_t                   no_handle_dir;                          /*show which side no grip handle*/
    uint8_t                   grip_disable_level;                     /*show whether if do grip handle*/
    uint8_t                   record_total_cnt;                       /*remember total count*/

    struct grip_point_info    first_point[TOUCH_MAX_NUM];             /*store the fist point of each ID*/
    bool                      dead_out_status[TOUCH_MAX_NUM];         /*show if exit the dead grip*/
    struct list_head          dead_zone_list;                         /*list all area using dead grip strategy*/

    uint16_t                  frame_cnt[TOUCH_MAX_NUM];               /*show down frame of each id*/
    int                       obj_prev_bit;                           /*show last frame obj attention*/
    int                       obj_bit_rcd;                            /*show current frame obj attention for record*/
    int                       obj_prced_bit_rcd;                      /*show current frame processed obj attention for record*/
    uint16_t                  coord_filter_cnt;                       /*cnt of make up points*/
    struct coord_buffer       *coord_buf;                             /*store point and its weight to do filter*/
    bool                      large_out_status[TOUCH_MAX_NUM];        /*show if exit the large area grip*/
    uint8_t                   large_reject[TOUCH_MAX_NUM];            /*show if rejected state*/
    uint8_t                   large_finger_status[TOUCH_MAX_NUM];     /*show large area finger status for grip strategy*/
    uint8_t                   large_point_status[TOUCH_MAX_NUM];      /*parameter for judge if ponits in large area need make up or not*/
    uint16_t                  large_detect_time_ms;                   /*large area detection time limit in ms*/
    struct list_head          large_zone_list;                        /*list all area using large area grip strategy*/
    struct list_head          condition_zone_list;                    /*list all area using conditional grip strategy*/
    bool                      condition_out_status[TOUCH_MAX_NUM];    /*show if exit the conditional rejection*/
    uint8_t                   makeup_cnt[TOUCH_MAX_NUM];              /*show makeup count of each id*/
    uint16_t                  large_frame_limit;                      /*max time to judge big area*/
    uint16_t                  large_ver_thd;                          /*threshold to determine large area size in vetical*/
    uint16_t                  large_hor_thd;                          /*threshold to determine large area size in horizon*/
    uint16_t                  large_corner_frame_limit;               /*max time to judge big area in corner area*/
    uint16_t                  large_ver_corner_thd;                   /*threshold to determine large area size in vetical corner*/
    uint16_t                  large_hor_corner_thd;                   /*threshold to determine large area size in horizon corner*/
    uint16_t                  large_ver_corner_width;                 /*threshold to determine should be judged in vertical corner way*/
    uint16_t                  large_hor_corner_width;                 /*threshold to determine should be judged in horizon corner way*/
    uint16_t                  large_corner_distance;                  /*threshold to judge move from edge*/

    struct curved_judge_para  curved_long_side_para;                  /*parameter for curved touchscreen long side judge*/
    struct curved_judge_para  curved_short_side_para;                 /*parameter for curved touchscreen short side judge*/
    bool                      is_curved_screen;                       /*curved screen judge flag*/
    s64                       lastest_down_time_ms;                   /*record the lastest down time out of large judged*/
    s64                       down_delta_time_ms;                     /*threshold to judge whether need to make up point*/

    bool                      point_unmoved[TOUCH_MAX_NUM];           /*show point have already moved*/
    uint8_t                   condition_frame_limit;                  /*keep rejeected while beyond this time*/
    unsigned long             condition_updelay_ms;                   /*time after to report touch up*/
    struct kfifo              up_fifo;                                /*store up touch id according  to the sequence*/
    struct hrtimer            grip_up_timer[TOUCH_MAX_NUM];           /*using for report touch up event*/
    bool                      grip_hold_status[TOUCH_MAX_NUM];        /*show if this id is in hold status*/
    struct work_struct        grip_up_work[TOUCH_MAX_NUM];            /*using for report touch up*/
    struct workqueue_struct   *grip_up_handle_wq;                     /*just for handle report up event*/

    bool                      eli_out_status[TOUCH_MAX_NUM];          /*store cross range status of each id*/
    bool                      eli_reject_status[TOUCH_MAX_NUM];       /*show reject status if each id*/
    struct list_head          elimination_zone_list;                  /*list all area using elimination strategy*/

    bool                      grip_handle_in_fw;                      /*show whether we should handle prevention in fw*/
    struct fw_grip_operations *fw_ops;                                /*fw grip setting func call back*/
};

struct fw_grip_operations {
    int                     (*set_fw_grip_area) (struct grip_zone_area *grip_zone, bool enable);   /*set the fw grip area*/
    void                    (*set_touch_direction) (uint8_t dir);                                  /*set touch direction in fw*/
    int                     (*set_no_handle_area) (struct kernel_grip_info *grip_info);            /*set no handle area in fw*/
    int                     (*set_condition_frame_limit) (int frame);                              /*set condition frame limit in fw*/
    int                     (*set_large_frame_limit) (int frame);                                  /*set large frame limit in fw*/
    int                     (*set_large_corner_frame_limit) (int frame);                           /*set large condition frame limit in fw*/
    int                     (*set_large_ver_thd) (int thd);                                        /*set large ver thd in fw*/
};

struct kernel_grip_info *kernel_grip_init(struct device *dev);
void init_kernel_grip_proc(struct proc_dir_entry *prEntry_tp, struct kernel_grip_info *grip_info);
void grip_status_reset(struct kernel_grip_info *grip_info, uint8_t index);
void kernel_grip_reset(struct kernel_grip_info *grip_info);
int kernel_grip_print_func(struct seq_file *s, struct kernel_grip_info *grip_info);
int touch_get_key_value(char *in, char *check);

#endif

