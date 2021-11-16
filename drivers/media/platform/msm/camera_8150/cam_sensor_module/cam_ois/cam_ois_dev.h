/* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _CAM_OIS_DEV_H_
#define _CAM_OIS_DEV_H_

#include <linux/i2c.h>
#include <linux/gpio.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ioctl.h>
#include <media/cam_sensor.h>
#include <cam_sensor_i2c.h>
#include <cam_sensor_spi.h>
#include <cam_sensor_io.h>
#include <cam_cci_dev.h>
#include <cam_req_mgr_util.h>
#include <cam_req_mgr_interface.h>
#include <cam_mem_mgr.h>
#include <cam_subdev.h>
#include "cam_soc_util.h"
#include "cam_context.h"
#include <linux/kfifo.h>
#include <linux/workqueue.h>

#define DEFINE_MSM_MUTEX(mutexname) \
	static struct mutex mutexname = __MUTEX_INITIALIZER(mutexname)

#ifdef VENDOR_EDIT
#define OIS_HALL_DATA_INFO_CACHE_SIZE 3
#define OIS_HALL_MAX_NUMBER 100
struct hall_info
{
    uint32_t timeStampSec;  //us
    uint32_t timeStampUsec;
    uint32_t mHalldata;
};

typedef struct OISHall2Eis
{
    struct hall_info datainfo[OIS_HALL_MAX_NUMBER];
} OISHALL2EIS;

#define GET_HALL_DATA_VERSION_DEFUALT         0
#define GET_HALL_DATA_VERSION_V2              1
#define GET_HALL_DATA_VERSION_V3              2

#endif
struct cam_ois_hall_data_in_driver {
    uint32_t high_dword;
    uint32_t low_dword;
    uint32_t hall_data;
};
#define SAMPLE_COUNT_IN_DRIVER        100
#define SAMPLE_COUNT_IN_OIS           34
#define SAMPLE_SIZE_IN_OIS            6
#define SAMPLE_SIZE_IN_DRIVER         (sizeof(struct cam_ois_hall_data_in_driver))
#define OIS_HALL_SAMPLE_COUNT    100
#define SAMPLE_COUNT_IN_OIS_FIFO 7
#define OIS_HALL_SAMPLE_BYTE     12
#define CLOCK_TICKCOUNT_MS       19200

enum cam_ois_state {
	CAM_OIS_INIT,
	CAM_OIS_ACQUIRE,
	CAM_OIS_CONFIG,
	CAM_OIS_START,
};

#ifdef VENDOR_EDIT
#define HALL_MAX_NUMBER 12
struct ois_hall_type {
	uint32_t dataNum;
	uint32_t mdata[HALL_MAX_NUMBER];
	uint32_t timeStamp;
};

enum cam_ois_type_vendor {
	CAM_OIS_MASTER,
	CAM_OIS_SLAVE,
	CAM_OIS_NONE,
	CAM_OIS_TYPE_MAX,
};

enum cam_ois_state_vendor {
	CAM_OIS_INVALID,
	CAM_OIS_FW_DOWNLOADED,
	CAM_OIS_READY,
};
#endif

/**
 * struct cam_ois_registered_driver_t - registered driver info
 * @platform_driver      :   flag indicates if platform driver is registered
 * @i2c_driver           :   flag indicates if i2c driver is registered
 *
 */
struct cam_ois_registered_driver_t {
	bool platform_driver;
	bool i2c_driver;
};

/**
 * struct cam_ois_i2c_info_t - I2C info
 * @slave_addr      :   slave address
 * @i2c_freq_mode   :   i2c frequency mode
 *
 */
struct cam_ois_i2c_info_t {
	uint16_t slave_addr;
	uint8_t i2c_freq_mode;
};

/**
 * struct cam_ois_soc_private - ois soc private data structure
 * @ois_name        :   ois name
 * @i2c_info        :   i2c info structure
 * @power_info      :   ois power info
 *
 */
struct cam_ois_soc_private {
	const char *ois_name;
	struct cam_ois_i2c_info_t i2c_info;
	struct cam_sensor_power_ctrl_t power_info;
};

/**
 * struct cam_ois_intf_params - bridge interface params
 * @device_hdl   : Device Handle
 * @session_hdl  : Session Handle
 * @ops          : KMD operations
 * @crm_cb       : Callback API pointers
 */
struct cam_ois_intf_params {
	int32_t device_hdl;
	int32_t session_hdl;
	int32_t link_hdl;
	struct cam_req_mgr_kmd_ops ops;
	struct cam_req_mgr_crm_cb *crm_cb;
};

/**
 * struct cam_ois_ctrl_t - OIS ctrl private data
 * @pdev            :   platform device
 * @ois_mutex       :   ois mutex
 * @soc_info        :   ois soc related info
 * @io_master_info  :   Information about the communication master
 * @cci_i2c_master  :   I2C structure
 * @v4l2_dev_str    :   V4L2 device structure
 * @bridge_intf     :   bridge interface params
 * @i2c_init_data   :   ois i2c init settings
 * @i2c_mode_data   :   ois i2c mode settings
 * @i2c_calib_data  :   ois i2c calib settings
 * @ois_device_type :   ois device type
 * @cam_ois_state   :   ois_device_state
 * @ois_name        :   ois name
 * @ois_fw_flag     :   flag for firmware download
 * @is_ois_calib    :   flag for Calibration data
 * @opcode          :   ois opcode
 * @device_name     :   Device name
 *
 */
struct cam_ois_ctrl_t {
	struct platform_device *pdev;
	struct mutex ois_mutex;
	struct cam_hw_soc_info soc_info;
	struct camera_io_master io_master_info;
	enum cci_i2c_master_t cci_i2c_master;
	enum cci_device_num cci_num;
	struct cam_subdev v4l2_dev_str;
	struct cam_ois_intf_params bridge_intf;
	struct i2c_settings_array i2c_init_data;
	struct i2c_settings_array i2c_calib_data;
	struct i2c_settings_array i2c_mode_data;
	enum msm_camera_device_type_t ois_device_type;
	enum cam_ois_state cam_ois_state;
	char device_name[20];
	char ois_name[32];
	uint8_t ois_fw_flag;
	uint8_t is_ois_calib;
	struct cam_ois_opcode opcode;
	enum cam_ois_type_vendor ois_type;  //Master or Slave
	uint8_t ois_gyro_position;          //Gyro positon
	uint8_t ois_gyro_vendor;            //Gyro vendor
	uint8_t ois_actuator_vendor;        //Actuator vendor
	uint8_t ois_module_vendor;          //Module vendor
	struct mutex ois_read_mutex;
	bool ois_read_thread_start_to_read;
	struct task_struct *ois_read_thread;
	struct mutex ois_hall_data_mutex;
	struct mutex ois_poll_thread_mutex;
	bool ois_poll_thread_exit;
	uint32_t ois_poll_thread_control_cmd;
	struct task_struct *ois_poll_thread;
	struct kfifo ois_hall_data_fifo;
	struct delayed_work ois_delaywork;
	OISHALL2EIS hall_data[OIS_HALL_DATA_INFO_CACHE_SIZE];
};

#endif /*_CAM_OIS_DEV_H_ */
