/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/smem.h>
#include <linux/sysfs.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>

#include <linux/delay.h>
#include <linux/slab.h>
#include <soc/oplus/system/oplus_project.h>
#define SENSOR_DEVINFO_SYNC_TIME  10000 //10s

//SMEM_SENSOR = SMEM_VERSION_FIRST + 23,
#define SMEM_SENSOR 130

#define REG_NUM 10
#define PARAMETER_NUM 25
#define FEATURE_NUM 10
#define SOURCE_NUM 2
#define ALGO_PARAMETER_NUM 15
#define ALGO_FEATURE_NUM  5
#define DEFAULT_CONFIG 0xff

#define SENSOR_DEBUG
#ifdef SENSOR_DEBUG
#define SENSOR_DEVINFO_DEBUG(a, arg...) \
	do{\
		pr_err(a, ##arg);\
	}while(0)
#else
#define SENSOR_DEVINFO_DEBUG(a, arg...)
#endif
enum sensor_id {
    OPLUS_ACCEL,
    OPLUS_GYRO,
    OPLUS_MAG,
    OPLUS_LIGHT,
    OPLUS_PROXIMITY,
    OPLUS_SAR,
    OPLUS_CCT,
    OPLUS_CCT_REAR,
    OPLUS_BAROMETER,
    SENSORS_NUM
};

enum sensor_algo_id {
    OPLUS_PICKUP_DETECT,
    OPLUS_LUX_AOD,
    OPLUS_TP_GESTURE,
    OPLUS_FP_DISPLAY,
    OPLUS_FREE_FALL,
    OPLUS_CAMERA_PROTECT,
    SENSOR_ALGO_NUM
};

enum  {
    STK3A5X = 0x01,
    TCS3701 = 0x02,
    TCS3408 = 0x04,
    STK3A6X = 0x08,
};

enum {
    LSM6DSM = 0x01,
    BMI160 = 0x02,
    LSM6DS3 = 0x04,
    BMI260 = 0x08,
};

enum {
    AKM09918 = 0x01,
    MMC5603 = 0x02,
    MXG4300 = 0X04,
};

enum {
    LPS22HH = 0x01,
    BMP380 = 0x02,
};

enum {
    SX9324 = 0x01,
};

enum {
    CCT_TCS3408 = 0x01,
    CCT_STK37600 = 0x02
};

struct sensor_feature {
    int reg[REG_NUM];
    int parameter[PARAMETER_NUM];
    int feature[FEATURE_NUM];
};

struct sensor_hw {
    uint8_t sensor_name;
    uint8_t bus_number;
    uint8_t direction;
    uint8_t irq_number;
    struct sensor_feature feature;
};

struct sensor_vector {
    int sensor_id;
    struct sensor_hw hw[SOURCE_NUM];
};


struct sensor_algorithm {
    int sensor_id;
    int parameter[ALGO_PARAMETER_NUM];
    int feature[ALGO_FEATURE_NUM];
};

struct sensor_info {
    int magic_num;
    struct sensor_vector s_vector[SENSORS_NUM];
    struct sensor_algorithm a_vector[SENSOR_ALGO_NUM];
};

struct oplus_als_cali_data {
    int red_max_lux;
    int green_max_lux;
    int blue_max_lux;
    int white_max_lux;
    int cali_coe;
    int row_coe;
    struct proc_dir_entry   *proc_oplus_als;
};


typedef enum {
    NORMAL = 0x01,
    UNDER_LCD = 0x02,
} alsps_position_type;

typedef enum {
    SOFTWARE_CAIL = 0x01,
    HARDWARE_CAIL = 0x02,
} ps_calibration_type;
