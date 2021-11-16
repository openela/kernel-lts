/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _OPPO_FP_COMMON_H_
#define _OPPO_FP_COMMON_H_

#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#define FP_ID_MAX_LENGTH                60 /*the length of /proc/fp_id should less than FP_ID_MAX_LENGTH !!!*/
#define ENGINEER_MENU_SELECT_MAXLENTH   20
#define FP_ID_SUFFIX_MAX_LENGTH         30 /*the length of FP ID SUFFIX !!!*/
#define MAX_ID_AMOUNT                   3 /*normally ,the id amount should be less than 3*/
#define FP_POWER_NODE                   "power-mode" /* 0: power mode not set, 1: ldo power, 2: gpio power, 3: auto power */
#define FP_POWER_NAME_NODE              "power-name"
#define FP_POWER_CONFIG                 "power-config"
#define FP_POWER_DELAY_TIME             "delay-time"
#define FP_POWERON_LEVEL_NODE           "poweron-level"
#define FP_NOTIFY_TPINFO_FLAG           "notify_tpinfo_flag"
#define FP_FTM_POWEROFF_FLAG            "ftm_poweroff_flag"

#define LDO_POWER_NODE                  "ldo"
#define LDO_CONFIG_NODE                 "ldo-config"
#define LDO_PARAM_AMOUNT                3

#define LDO_VMAX_INDEX                  (0)
#define LDO_VMIN_INDEX                  (1)
#define LDO_UA_INDEX                    (2)

#define FP_MAX_PWR_LIST_LEN             (4)

typedef enum {
    FP_FPC_1022 = 0,
    FP_FPC_1023 = 1,
    FP_FPC_1140 = 2,
    FP_FPC_1260 = 3,
    FP_FPC_1270 = 4,
    FP_GOODIX_5658 = 5,
    FP_FPC_1511 = 6,
    FP_GOODIX_3268 = 7,
    FP_GOODIX_5288 = 8,
    FP_GOODIX_5298 = 9,
    FP_GOODIX_5228 = 10,
    FP_GOODIX_OPTICAL_95 = 11,
    FP_GOODIX_5298_GLASS = 12,
    FP_SILEAD_OPTICAL_70 = 13,
    FP_FPC_1023_GLASS = 14,
    FP_EGIS_OPTICAL_ET713 = 15,
    FP_GOODIX_3626 = 16,
    FP_FPC_1541 = 17,
    FP_EGIS_520 = 18,
    FP_UNKNOWN,
} fp_vendor_t;

enum {
    FP_OK,
    FP_ERROR_GPIO,
    FP_ERROR_GENERAL,
};

struct fp_data {
    struct device *dev;
    uint32_t fp_id_amount;
    uint32_t gpio_index[MAX_ID_AMOUNT];
    uint32_t fp_id[MAX_ID_AMOUNT];
    fp_vendor_t fpsensor_type;
};

typedef struct fp_underscreen_info {
    uint8_t touch_state;
    uint8_t area_rate;
    uint16_t x;
    uint16_t y;
}fp_underscreen_info_t;

typedef struct netlink_msg_info {
    uint8_t netlink_cmd;
    fp_underscreen_info_t tp_info;
}netlink_msg_info_t;

struct fp_vreg_config {
    const char *name;
    int vmin;
    int vmax;
    int ua_load;
};

typedef enum {
    FP_POWER_MODE_NOT_SET, //
    FP_POWER_MODE_LDO,
    FP_POWER_MODE_GPIO,
    FP_POWER_MODE_AUTO,
} fp_power_mode_t;

typedef struct {
    uint32_t pwr_type;
    signed pwr_gpio;
    struct fp_vreg_config vreg_config;
    struct regulator *vreg;
    unsigned delay;
    unsigned poweron_level;
} fp_power_info_t;

fp_vendor_t get_fpsensor_type(void);

typedef int (*opticalfp_handler)(struct fp_underscreen_info *tp_info);
void opticalfp_irq_handler_register(opticalfp_handler handler);

#endif  /*_OPPO_FP_COMMON_H_*/
