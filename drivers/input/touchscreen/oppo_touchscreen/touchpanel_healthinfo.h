/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _TOUCHPANEL_HEALTHONFO_
#define _TOUCHPANEL_HEALTHONFO_

#include <linux/i2c.h>
#include <linux/firmware.h>

#define LONG_SWIPE_JUDGE_RATIO      9

#define SWIPE_BROKEN_FRAMES_MS      2500//2.5frams * 1000ms
#define SWIPE_BROKEN_JUDGE_RATIO    35

#define JUMPING_POINT_FRAMES_MS     1500//1.5frams * 1000ms
#define JUMPING_POINT_JUDGE_RATIO   175
#define JUMPING_POINT_TIMES         5

#define STUCK_POINT_TIME            15 * 1000

#define LONG_CLICK_TIME             400

#define GESTURE_RESPONSE_TIME       800

#define MAX_BUS_ERROR_BUFF_RECORD   5
#define DEFAULT_CHILD_STR_LEN       8
#define DEFAULT_REPORT_STR_LEN      100
#define DEFAULT_BUF_MATRIX_LINEBREAK    8

#define CLICK_COUNT_ARRAY_WIDTH     4
#define CLICK_COUNT_ARRAY_HEIGHT    5

#define MS_PER_SECOND               1000

#define KEVENT_LOG_TAG              "psw_bsp_tp"
#define KEVENT_EVENT_ID             "tp_fatal_report"
#define FIRMWARE_UPDATE_SUCCESS_KEYWORD "Success"
#define PREFIX_FW_UPDATE_RESULT     "fw_update_result-"
#define PREFIX_BUS_TRANS_ERRNO      "bus_transfer_errno-"
#define PREFIX_BUS_TRANS_ERRBUF     "bus_transfer_error_buffer-"
#define PREFIX_ALLOC_ERR_FUNC       "alloc_error_function-"
#define PREFIX_HEALTH_REPORT        "health_report-"
#define PREFIX_GRIP_DEAD_ZONE       "grip_dead_zone-"
#define PREFIX_GRIP_CONDIT_ZONE     "grip_condition_zone-"
#define PREFIX_GRIP_ELI_ZONE        "grip_elimination_zone-"
#define PREFIX_GRIP_LANS_ELI_ZONE   "lanscape_grip_elimination_zone-"
#define PREFIX_POINT_JUMPING        "jumping-"
#define PREFIX_POINT_STUCK          "stuck-"
#define PREFIX_POINT_LANSCAPE_STUCK "lanscape_stuck-"
#define PREFIX_SWIPE_BROKER         "broken-"
#define PREFIX_SWIPE_SUDDNT_LONG    "suddent_long-"
#define PREFIX_GESTURE              "Gesture-"
#define PREFIX_GESTURE_INVLID       "Invalid_Gesture-"
#define PREFIX_FP_AREA_RATE         "fp_area_rate-"
#define PREFIX_FD                   "face_detect-"

typedef enum {
    HEALTH_TOUCH = 0,
    HEALTH_GESTURE,
    HEALTH_FINGERPRINT,
    HEALTH_FACE_DETECT,
    HEALTH_REPORT,
    HEALTH_PROBE,
    HEALTH_RESUME,
    HEALTH_SUSPEND,
    HEALTH_TEST_AUTO,
    HEALTH_TEST_BLACKSCREEN,
    HEALTH_BUS,
    HEALTH_ALLOC_SUCCESS,
    HEALTH_ALLOC_FAILED,
    HEALTH_FW_UPDATE,
    HEALTH_FW_UPDATE_COST,
    HEALTH_GRIP,
} healthinfo_type;

void reset_healthinfo_time_counter(u64 *time_counter);
void reset_healthinfo_grip_time_record(void *tp_monitor_data, void *tp_grip_info);
u64 check_healthinfo_time_counter_timeout(u64 time_counter, int ms);

int tp_healthinfo_report(void *tp_monitor_data, healthinfo_type type, void *value);

int tp_healthinfo_read(struct seq_file *s, void *tp_monitor_data);

int tp_healthinfo_init(struct device *dev, void *tp_monitor_data);

#endif /* _TOUCHPANEL_HEALTHONFO_ */
