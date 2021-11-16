// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/task_work.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/machine.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>

#ifdef CONFIG_FB
#include <linux/fb.h>
#include <linux/notifier.h>
#endif

#include "synaptics_s3706.h"

static struct chip_data_s3706 *g_chip_info = NULL;
extern int tp_register_times;
static int synaptics_get_chip_info(void *chip_data);
static int synaptics_mode_switch(void *chip_data, work_mode mode, bool flag);
static int checkCMD(struct chip_data_s3706 *chip_info, int retry_time);

/*******Part0:LOG TAG Declear********************/

#define TPD_DEVICE "synaptics-s3706"
#define TPD_INFO(a, arg...)  pr_err("[TP]"TPD_DEVICE ": " a, ##arg)
#define TPD_DEBUG(a, arg...)\
        do {\
                if (LEVEL_DEBUG == tp_debug) {\
                        pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
                }\
        }while(0)

#define TPD_DETAIL(a, arg...)\
        do {\
                if (LEVEL_BASIC != tp_debug) {\
                        pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
                }\
        }while(0)

#define TPD_DEBUG_NTAG(a, arg...)\
        do {\
                if (tp_debug) {\
                        printk(a, ##arg);\
                }\
        }while(0)

/*******Part1:Call Back Function implement*******/

static int synaptics_get_touch_points(void *chip_data, struct point_info *points, int max_num)
{
        int ret, i, obj_attention;
        static int enable_obj_attention = 0;
        unsigned char fingers_to_process = max_num;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;
        char buf[80];

        memset(buf, 0, sizeof(buf));
        obj_attention = touch_i2c_read_word(chip_info->client, chip_info->reg_info.F12_2D_DATA15);
        for (i = 9; ; i--) {
                if ((obj_attention & 0x03FF) >> i  || i == 0) {
                        break;
                } else {
                        fingers_to_process--;
                }
        }

        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F12_2D_DATA_BASE, 8*fingers_to_process, buf);
        if (ret < 0) {
                TPD_INFO("touch i2c read block failed\n");
                return -1;
        }
        for (i = 0; i< fingers_to_process; i++) {
                points[i].x = ((buf[i*8 + 2] & 0x0f) << 8) | (buf[i*8 + 1] & 0xff);
                points[i].y = ((buf[i*8 + 4] & 0x0f) << 8) | (buf[i*8 + 3] & 0xff);
                points[i].z = buf[i*8 + 5];
                points[i].touch_major = max(buf[i*8 + 6], buf[i*8 + 7]);
                points[i].width_major = ((buf[i*8 + 6] & 0x0f) + (buf[i*8 + 7] & 0x0f)) / 2;
                points[i].status = buf[i*8];
        }

        if (!chip_info->filter_state) {
                enable_obj_attention = obj_attention;
        } else {
                enable_obj_attention &= obj_attention;
        }

        return enable_obj_attention;
}

static int synaptics_ftm_process(void *chip_data)
{
        int ret = 0;
        TPD_INFO("%s go to sleep in ftm\n", __func__);
        synaptics_get_chip_info(chip_data);
        synaptics_mode_switch(chip_data, MODE_SLEEP, true);
        if (ret < 0) {
                TPD_INFO("%s, Touchpanel operate mode switch failed\n", __func__);
        }
        return 0;
}

static int synaptics_get_vendor(void *chip_data, struct panel_info *panel_data)
{
        char manu_temp[MAX_DEVICE_MANU_LENGTH] = SYNAPTICS_PREFIX;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        chip_info->tp_type = panel_data->tp_type;
        chip_info->p_tp_fw = &panel_data->TP_FW;
        strlcat(manu_temp, panel_data->manufacture_info.manufacture, MAX_DEVICE_MANU_LENGTH);
        strncpy(panel_data->manufacture_info.manufacture, manu_temp, MAX_DEVICE_MANU_LENGTH);
        TPD_INFO("chip_info->tp_type = %d, panel_data->test_limit_name = %s, panel_data->fw_name = %s\n",
                chip_info->tp_type, panel_data->test_limit_name, panel_data->fw_name);
        return 0;
}

static int synaptics_read_F54_base_reg(struct chip_data_s3706 *chip_info)
{
        uint8_t buf[4] = {0};
        int ret = 0;

        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
        if (ret < 0) {
                TPD_INFO("%s: failed for page select\n", __func__);
                return -1;
        }
        ret = touch_i2c_read_block(chip_info->client, 0xE9, 4, &(buf[0x0]));
        chip_info->reg_info.F54_ANALOG_QUERY_BASE = buf[0];
        chip_info->reg_info.F54_ANALOG_COMMAND_BASE = buf[1];
        chip_info->reg_info.F54_ANALOG_CONTROL_BASE = buf[2];
        chip_info->reg_info.F54_ANALOG_DATA_BASE = buf[3];
        TPD_INFO("F54_QUERY_BASE = %x \n\
                        F54_CMD_BASE        = %x \n\
                        F54_CTRL_BASE   = %x \n\
                        F54_DATA_BASE   = %x \n",
                        chip_info->reg_info.F54_ANALOG_QUERY_BASE, chip_info->reg_info.F54_ANALOG_COMMAND_BASE,
                        chip_info->reg_info.F54_ANALOG_CONTROL_BASE, chip_info->reg_info.F54_ANALOG_DATA_BASE);

        return ret;
}

static void synaptics_health_zone_init(struct chip_data_s3706 *chip_info)
{
        int ret = 0;
        uint8_t l_tmp = 0, h_tmp = 0;

        /*Get status of IC log*/
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x04);        /* page 4*/
        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_QUERY_BASE + 0x04);
        TPD_INFO("F51_CUSTOM_QUERY05 is %d\n", ret);
        if (1 == ret) {
                chip_info->health_zone.health_support = true;
                l_tmp = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_QUERY_BASE + 0x05);
                h_tmp = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_QUERY_BASE + 0x06);
                chip_info->health_zone.loglength_addr = (h_tmp << 8) | l_tmp;
                TPD_INFO("l_tmp = 0x%x h_tmp = 0x%x chip_info->health_zone.loglength_addr = 0x%x \n", l_tmp, h_tmp, chip_info->health_zone.loglength_addr);
        }
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x00);        /* page 0*/
}

static int synaptics_get_chip_info(void *chip_data)
{
        uint8_t buf[4] = {0};
        int ret;
        struct chip_data_s3706        *chip_info = (struct chip_data_s3706 *)chip_data;
        struct synaptics_register *reg_info = &chip_info->reg_info;

        memset(buf, 0, sizeof(buf));
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x0);   /* page 0*/
        if (ret < 0) {
                TPD_INFO("%s: failed for page select\n", __func__);
                return -1;
        }
        ret = touch_i2c_read_block(chip_info->client, 0xDD, 4, &(buf[0x0]));
        if (ret < 0) {
                TPD_INFO("failed for page select!\n");
                return -1;
        }

        reg_info->F12_2D_QUERY_BASE = buf[0];
        reg_info->F12_2D_CMD_BASE = buf[1];
        reg_info->F12_2D_CTRL_BASE = buf[2];
        reg_info->F12_2D_DATA_BASE = buf[3];

        TPD_INFO("F12_2D_QUERY_BASE = 0x%x \n\
                        F12_2D_CMD_BASE        = 0x%x \n\
                        F12_2D_CTRL_BASE   = 0x%x \n\
                        F12_2D_DATA_BASE   = 0x%x \n",
                        reg_info->F12_2D_QUERY_BASE, reg_info->F12_2D_CMD_BASE,
                        reg_info->F12_2D_CTRL_BASE,  reg_info->F12_2D_DATA_BASE);

        ret = touch_i2c_read_block(chip_info->client, 0xE3, 4, &(buf[0x0]));
        reg_info->F01_RMI_QUERY_BASE = buf[0];
        reg_info->F01_RMI_CMD_BASE = buf[1];
        reg_info->F01_RMI_CTRL_BASE = buf[2];
        reg_info->F01_RMI_DATA_BASE = buf[3];
        TPD_INFO("F01_RMI_QUERY_BASE = 0x%x \n\
                        F01_RMI_CMD_BASE        = 0x%x \n\
                        F01_RMI_CTRL_BASE   = 0x%x \n\
                        F01_RMI_DATA_BASE   = 0x%x \n",
                        reg_info->F01_RMI_QUERY_BASE, reg_info->F01_RMI_CMD_BASE,
                        reg_info->F01_RMI_CTRL_BASE,  reg_info->F01_RMI_DATA_BASE);

        ret = touch_i2c_read_block(chip_info->client, 0xE9, 4, &(buf[0x0]));
        reg_info->F34_FLASH_QUERY_BASE = buf[0];
        reg_info->F34_FLASH_CMD_BASE = buf[1];
        reg_info->F34_FLASH_CTRL_BASE = buf[2];
        reg_info->F34_FLASH_DATA_BASE = buf[3];
        TPD_INFO("F34_FLASH_QUERY_BASE   = 0x%x \n\
                        F34_FLASH_CMD_BASE          = 0x%x \n\
                        F34_FLASH_CTRL_BASE         = 0x%x \n\
                        F34_FLASH_DATA_BASE         = 0x%x \n",
                        reg_info->F34_FLASH_QUERY_BASE, reg_info->F34_FLASH_CMD_BASE,
                        reg_info->F34_FLASH_CTRL_BASE,  reg_info->F34_FLASH_DATA_BASE);

        reg_info->F01_RMI_QUERY11 = reg_info->F12_2D_QUERY_BASE + 11;   /*no use*/
        reg_info->F01_RMI_CTRL00 = reg_info->F01_RMI_CTRL_BASE;
        reg_info->F01_RMI_CTRL01 = reg_info->F01_RMI_CTRL_BASE + 1;
        reg_info->F01_RMI_CTRL02 = reg_info->F01_RMI_CTRL_BASE + 2;
        reg_info->F01_RMI_CMD00  = reg_info->F01_RMI_CMD_BASE;  /*no use*/
        reg_info->F01_RMI_DATA01 = reg_info->F01_RMI_DATA_BASE + 1;

        reg_info->F12_2D_CTRL08 = reg_info->F12_2D_CTRL_BASE;
        reg_info->F12_2D_CTRL11 = reg_info->F12_2D_CTRL_BASE + 3;
        reg_info->F12_2D_CTRL23 = reg_info->F12_2D_CTRL_BASE + 8;
        reg_info->F12_2D_CTRL32 = reg_info->F12_2D_CTRL_BASE + 15;  /*no use*/
        reg_info->F12_2D_DATA04 = reg_info->F12_2D_DATA_BASE + 2;   /*where is gesture type register*/
        reg_info->F12_2D_DATA15 = reg_info->F12_2D_DATA_BASE + 3;
        reg_info->F12_2D_DATA38 = reg_info->F12_2D_DATA_BASE + 54;  /*no use*/
        reg_info->F12_2D_DATA39 = reg_info->F12_2D_DATA_BASE + 55;  /*no use*/
        reg_info->F12_2D_CMD00  = reg_info->F12_2D_CMD_BASE;        /*no use*/
        reg_info->F12_2D_CTRL20 = reg_info->F12_2D_CTRL_BASE + 0x06;        /*no use*/
        reg_info->F12_2D_CTRL27 = reg_info->F12_2D_CTRL_BASE + 0x09;        /*where is gesture type register*/

        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x4);         /* page 4*/
        if (ret < 0) {
                TPD_INFO("%s: failed for page select\n", __func__);
                return -1;
        }
        ret = touch_i2c_read_block(chip_info->client, 0xE9, 4, &(buf[0x0]));
        reg_info->F51_CUSTOM_QUERY_BASE = buf[0];
        reg_info->F51_CUSTOM_CMD_BASE   = buf[1];
        reg_info->F51_CUSTOM_CTRL_BASE  = buf[2];
        reg_info->F51_CUSTOM_DATA_BASE  = buf[3];
        TPD_INFO("F51_CUSTOM_QUERY_BASE  = 0x%x \n\
                        F51_CUSTOM_CMD_BASE         = 0x%x \n\
                        F51_CUSTOM_CTRL_BASE        = 0x%x \n\
                        F51_CUSTOM_DATA_BASE        = 0x%x \n",
                        reg_info->F51_CUSTOM_QUERY_BASE, reg_info->F51_CUSTOM_CMD_BASE,
                        reg_info->F51_CUSTOM_CTRL_BASE,  reg_info->F51_CUSTOM_DATA_BASE);

        reg_info->F51_CUSTOM_CTRL00 = reg_info->F51_CUSTOM_CTRL_BASE;   /*no use*/
        reg_info->F51_CUSTOM_DATA   = reg_info->F51_CUSTOM_DATA_BASE;   /*Roland, where is gesture type register*/
        reg_info->F51_CUSTOM_CTRL31 = reg_info->F51_CUSTOM_CTRL_BASE + 11;  /*no use*/
        reg_info->F51_CUSTOM_CTRL50 = reg_info->F51_CUSTOM_CTRL_BASE + 0x0A;        /*where is edge register*/
        reg_info->F51_CUSTOM_CTRL04_05 = reg_info->F51_CUSTOM_CTRL_BASE + 0x0D;
        reg_info->F51_CUSTOM_CTRL04_06 = reg_info->F51_CUSTOM_CTRL_BASE + 0x0E;
        reg_info->F51_CUSTOM_CTRL04_08 = reg_info->F51_CUSTOM_CTRL_BASE + 0x10;         /*switch for corner mode*/
        reg_info->F51_CUSTOM_CTRL13 = reg_info->F51_CUSTOM_CTRL_BASE + 0x17;
        reg_info->F51_CUSTOM_CTRL20 = reg_info->F51_CUSTOM_CTRL_BASE + 0x26;
        reg_info->F51_CUSTOM_CTRL30 = reg_info->F51_CUSTOM_CTRL_BASE + 0x38;    /*switch for game mode*/

        synaptics_read_F54_base_reg(chip_info);
        reg_info->F55_SENSOR_CTRL01 = 0x01;
        reg_info->F55_SENSOR_CTRL02 = 0x02;
        /* select page 0*/
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x00);
        synaptics_health_zone_init(chip_info);
        return ret;
}

/**
 * synaptics_get_fw_id -   get device fw id.
 * @chip_info: struct include i2c resource.
 * Return fw version result.
 */
static uint32_t synaptics_get_fw_id(struct chip_data_s3706 *chip_info)
{
        uint8_t buf[4];
        uint32_t current_firmware = 0;

        touch_i2c_write_byte(chip_info->client, 0xff, 0x0);
        touch_i2c_read_block(chip_info->client, chip_info->reg_info.F34_FLASH_CTRL_BASE, 4, buf);
        current_firmware = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
        TPD_INFO("CURRENT_FIRMWARE_ID = 0x%x\n", current_firmware);

        return current_firmware;
}

static uint8_t synaptics_get_noise_level(struct chip_data_s3706 *chip_info)
{
        int ret = -1;
        uint8_t game_buffer[13];

        touch_i2c_write_byte(chip_info->client, 0xff, 0x0);
        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F12_2D_CTRL11, 13, &(game_buffer[0])); 
        if (ret < 0) {
            TPD_INFO("%s:get noise fail\n",__func__);
            return 0x28;
        } else {
            TPD_INFO("%s:game_buffer[12]=0x%x\n",__func__, game_buffer[12]);
        }

        return game_buffer[12];
}

static uint8_t synaptics_get_lock_point_level(struct chip_data_s3706 *chip_info)
{
        int ret = -1;
        uint8_t tmp;

        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x04);      //set page 4
        if (ret < 0) {
            TPD_INFO("%s:set page 4 fail\n", __func__);
            return 0x1E;
        }

        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL30);
        if (ret < 0) {
            TPD_INFO("%s:get lock point level fail\n", __func__);
            return 0x1E;
        } else {
            tmp = ret & 0xFF;
            TPD_INFO("%s:lock point level =0x%x\n", __func__, tmp);
        }

        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x00);      //set page 0
        if (ret < 0) {
            TPD_INFO("%s:set page 0 fail\n", __func__);
        }

        return tmp;
}

static fw_check_state synaptics_fw_check(void *chip_data, struct resolution_info *resolution_info, struct panel_info *panel_data)
{
        uint32_t bootloader_mode;
        int max_y_ic = 0;
        int max_x_ic = 0;
        uint8_t buf[4];
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        touch_i2c_write_byte(chip_info->client, 0xff, 0x00);

        touch_i2c_read_block(chip_info->client, chip_info->reg_info.F12_2D_CTRL08, 4, buf);
        max_x_ic = ((buf[1] << 8) & 0xffff) | (buf[0] & 0xffff);
        max_y_ic = ((buf[3] << 8) & 0xffff) | (buf[2] & 0xffff);
        TPD_INFO("max_x = %d, max_y = %d, max_x_ic = %d, max_y_ic = %d\n", resolution_info->max_x, resolution_info->max_y, max_x_ic, max_y_ic);
        if ((resolution_info->max_x == 0) ||(resolution_info->max_y == 0)) {
                resolution_info->max_x = max_x_ic;
                resolution_info->max_y = max_y_ic;
        }

        bootloader_mode = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F01_RMI_DATA_BASE);
        bootloader_mode = bootloader_mode & 0xff;
        bootloader_mode = bootloader_mode & 0x40;
        TPD_INFO("%s, bootloader_mode = 0x%x\n", __func__, bootloader_mode);

        if ((max_x_ic == 0) || (max_y_ic == 0) || (bootloader_mode == 0x40)) {
                TPD_INFO("Something terrible wrong, Trying Update the Firmware again\n");
                return FW_ABNORMAL;
        }

        /*fw check normal need update TP_FW  && device info*/
        panel_data->TP_FW = synaptics_get_fw_id(chip_info);
        if (panel_data->manufacture_info.version) {
                sprintf(panel_data->manufacture_info.version, "0x%x", panel_data->TP_FW);
        }
        chip_info->default_nosie_level = synaptics_get_noise_level(chip_info);
        if (chip_info->report_120hz_support) {
            chip_info->default_lock_point_level = synaptics_get_lock_point_level(chip_info);
            TPD_INFO("%s: default_nosie_level = 0x%x, default_lock_point_level = 0x%x .\n", __func__, chip_info->default_nosie_level, chip_info->default_lock_point_level);
        }

        return FW_NORMAL;
}

/**
 * synaptics_enable_interrupt -   Device interrupt ability control.
 * @chip_info: struct include i2c resource.
 * @enable: disable or enable control purpose.
 * Return  0: succeed, -1: failed.
 */
static int synaptics_enable_interrupt(struct chip_data_s3706 *chip_info, bool enable)
{
        int ret;
        uint8_t abs_status_int;

        TPD_INFO("%s enter, enable = %d.\n", __func__, enable);
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x0);
        if (ret < 0) {
                TPD_INFO("%s: select page failed ret = %d\n", __func__, ret);
                return -1;
        }
        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL01);
        if ((0x7f == ret) && enable) {
            TPD_INFO("interrupt enabled, just return.\n");
            return 0;
        }
        if (enable) {
                abs_status_int = 0x7f;
                /*clear interrupt bits for previous touch*/
                ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F01_RMI_DATA01);
                if (ret < 0) {
                        TPD_INFO("%s :clear interrupt bits failed\n", __func__);
                        return -1;
                }
        } else {
                abs_status_int = 0x0;
        }

        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL01, abs_status_int);
        if (ret < 0) {
                TPD_INFO("%s: enable or disable abs interrupt failed, abs_int = %d\n", __func__, abs_status_int);
                return -1;
        }

        return 0;
}

static u8 synaptics_trigger_reason(void *chip_data, int gesture_enable, int is_suspended)
{
        int ret = 0;
        uint8_t device_status = 0;
        uint8_t interrupt_status = 0;
        u8 result_event = 0;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

#ifdef CONFIG_SYNAPTIC_RED
        if (chip_info->enable_remote) {
                return IRQ_IGNORE;
        }
#endif
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x0);   /* page 0*/
        ret = touch_i2c_read_word(chip_info->client, chip_info->reg_info.F01_RMI_DATA_BASE);
        if (ret < 0) {
                TPD_INFO("%s, i2c read error, ret = %d\n", __func__, ret);
                return IRQ_EXCEPTION;
        }
        device_status = ret & 0xff;
        interrupt_status = (ret & 0x7f00) >> 8;

        if (device_status) {
                TPD_INFO("%s, interrupt_status = 0x%x, device_status = 0x%x\n", __func__, interrupt_status, device_status);
                return IRQ_EXCEPTION;
        }
        if (interrupt_status & 0x04) {
                if (gesture_enable && is_suspended) {
                        return IRQ_GESTURE;
                } else if (is_suspended) {
                        return IRQ_IGNORE;
                }
                SET_BIT(result_event, IRQ_TOUCH);
        }
        if (interrupt_status & 0x10) {
                SET_BIT(result_event, IRQ_BTN_KEY);
        }
        if (interrupt_status & 0x20) {
                SET_BIT(result_event, IRQ_FACE_STATE);
                SET_BIT(result_event, IRQ_FW_HEALTH);
        }

        return  result_event;
}

static u32 synaptics_u32_trigger_reason(void *chip_data, int gesture_enable, int is_suspended)
{
        int ret = 0;
        uint8_t device_status = 0;
        uint8_t interrupt_status = 0;
        u32 result_event = 0;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

#ifdef CONFIG_SYNAPTIC_RED
        if (chip_info->enable_remote) {
                return IRQ_IGNORE;
        }
#endif
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x0);   /* page 0*/
        ret = touch_i2c_read_word(chip_info->client, chip_info->reg_info.F01_RMI_DATA_BASE);
        if (ret < 0) {
                TPD_INFO("%s, i2c read error, ret = %d\n", __func__, ret);
                return IRQ_EXCEPTION;
        }
        device_status = ret & 0xff;
        interrupt_status = (ret & 0x7f00) >> 8;

        if (device_status) {
                TPD_INFO("%s, interrupt_status = 0x%x, device_status = 0x%x\n", __func__, interrupt_status, device_status);
                return IRQ_EXCEPTION;
        }
        if (interrupt_status & 0x04) {
                if (gesture_enable && is_suspended) {
                        return IRQ_GESTURE;
                } else if (is_suspended) {
                        return IRQ_IGNORE;
                }
                SET_BIT(result_event, IRQ_TOUCH);
                SET_BIT(result_event, IRQ_FINGERPRINT);
        }
        if (interrupt_status & 0x10) {
                SET_BIT(result_event, IRQ_BTN_KEY);
        }
        if (interrupt_status & 0x20) {
                SET_BIT(result_event, IRQ_FACE_STATE);
                SET_BIT(result_event, IRQ_FW_HEALTH);
        }

        return  result_event;
}

static int synaptics_resetgpio_set(struct hw_resource *hw_res, bool on)
{
        if (gpio_is_valid(hw_res->reset_gpio)) {
                TPD_DEBUG("Set the reset_gpio \n");
                gpio_direction_output(hw_res->reset_gpio, on);
        }

        return 0;
}

/*
 * return success: 0; fail : negative
 */
static int synaptics_reset(void *chip_data)
{
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        TPD_INFO("%s.\n", __func__);
        synaptics_resetgpio_set(chip_info->hw_res, false); /* reset gpio*/
        msleep(10);
        synaptics_resetgpio_set(chip_info->hw_res, true); /* reset gpio*/
        msleep(RESET_TO_NORMAL_TIME);
        chip_info->is_fp_down = false;
        chip_info->force_update_needed = false;

        return 0;
}

static int synaptics_configuration_init(struct chip_data_s3706 *chip_info, bool config)
{
        int ret = 0, retval = 0;
        unsigned char ctrl_buf[3];

        TPD_INFO("%s, configuration init = %d\n", __func__, config);
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x0); // page 0
        if (ret < 0) {
                TPD_INFO("init_panel failed for page select\n");
                return -1;
        }

        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL00);
        if (ret < 0) {
                TPD_INFO("failed for get F01_RMI_CTRL00\n");
                return -1;
        }

        //device control: normal operation
        if (config) {//configed  && out of sleep mode
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL00, (ret & 0xf8) | 0x80);
                if (ret < 0) {
                        TPD_INFO("%s failed for mode select\n", __func__);
                        return -1;
                }
                touch_i2c_read_block(chip_info->client, chip_info->reg_info.F12_2D_CTRL20, 3, &(ctrl_buf[0x0]));
                if (ctrl_buf[2] & 0x02) {
                        touch_i2c_write_byte(chip_info->client, 0xff, 0x04); 
                        retval = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL13);
                        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL13, retval & 0xFE);
                        retval = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL00 + 3);
                        if (retval != 0) {
                                touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL00 + 3, 0x00);
                                touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
                                touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x04);    /* force update*/
                                checkCMD(chip_info, 30);
                        }
                        TPD_INFO("F51 and palm debug closed\n");
                }
        } else {//sleep mode
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL00, (ret & 0xf8) | 0x81);
                if (ret < 0) {
                        TPD_INFO("%s failed for mode select\n", __func__);
                        return -1;
                }
        }

        return ret;
}

static int synaptics_glove_mode_enable(struct chip_data_s3706 *chip_info, bool enable)
{
        int ret = 0;

        TPD_INFO("%s, enable = %d\n", __func__, enable);
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x00);
        if (ret < 0) {
                TPD_DEBUG("touch_i2c_write_byte failed for mode select\n");
                return ret;
        }

        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F12_2D_CTRL23);
        if (enable) {
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F12_2D_CTRL23, ret | 0x20);
        } else {
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F12_2D_CTRL23, ret & 0xdf);
        }

        return ret;
}

static void synaptics_enable_fingerprint_underscreen(void * chip_data, uint32_t enable)
{
        int ret = 0;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        touch_i2c_write_byte(chip_info->client, 0xff, 0x4);
        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL20);
        if (ret != enable)
            ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL20, enable);                    /*open gesture for fingerprint under screen*/

        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL20);
        TPD_INFO("%s :F51_CUSTOM_CTRL20 is %d\n", __func__, ret);

        if (!enable) {
                chip_info->is_fp_down = false;
                if (chip_info->force_update_needed) {
                        chip_info->force_update_needed = false;
                        touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
                        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x04);    /* force update*/
                        checkCMD(chip_info, 30);
                        TPD_INFO("do force update\n");
                }
        }
        touch_i2c_write_byte(chip_info->client, 0xff, 0x0);

        return;
}

static int synaptics_enable_black_gesture(struct chip_data_s3706 *chip_info, bool enable)
{
        int ret;
        unsigned char report_gesture_ctrl_buf[3];
        unsigned char wakeup_gesture_enable_buf;

        TPD_INFO("%s, enable = %d\n", __func__, enable);
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x0);
        if (ret < 0) {
                TPD_INFO("%s: select page failed ret = %d\n", __func__, ret);
                return -1;
        }
        touch_i2c_read_block(chip_info->client, chip_info->reg_info.F12_2D_CTRL20, 3, &(report_gesture_ctrl_buf[0x0]));
        if (enable) {
                report_gesture_ctrl_buf[2] |= 0x02;
                if (!chip_info->report_120hz_support) {
                    ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL02, 0x3);                 /*set doze interval to 30ms*/
                }
        } else  {
                report_gesture_ctrl_buf[2] &= 0xfd;
                if (!chip_info->report_120hz_support) {
                    ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL02, 0x1);                 /*set doze interval to 10ms*/
                }
        }

        touch_i2c_write_block(chip_info->client, chip_info->reg_info.F12_2D_CTRL20, 3, &(report_gesture_ctrl_buf[0x0]));
        wakeup_gesture_enable_buf = 0xef;   /* all kinds of gesture except triangle*/
        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F12_2D_CTRL27, wakeup_gesture_enable_buf);

        return 0;
}

static int synaptics_corner_limit_handle(struct chip_data_s3706 *chip_info, bool enable)
{
        int ret = -1;
        uint8_t pre_set_1 = 0, pre_set_2 = 0, pre_set_3 = 0;

        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x04);
        if (ret < 0) {
                TPD_INFO("%s: select page failed ret = %d\n", __func__, ret);
                return -1;
        }

        pre_set_1 = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL04_05);
        pre_set_2 = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL04_06);
        pre_set_3 = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL04_08);
        //enable is 0x02 means notch is on the left, 0x10 notch is on the right, 0x0 means close corner mode
        if((LANDSCAPE_SCREEN_90 == chip_info->touch_direction) && !enable) {
                //set area parameter
                if ((0xFF == pre_set_1) && (0x44 == pre_set_2) && (0x03 == pre_set_3)) {
                    TPD_INFO("%s: do not enable twice dir = %d\n", __func__, chip_info->touch_direction);
                    return 0;
                }
                touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL04_05, 0xFF);        //x part
                touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL04_06, 0x44);        //y part
                touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL04_08, 0x03);
        } else if ((LANDSCAPE_SCREEN_270 == chip_info->touch_direction) && !enable) {
                if ((0xFF == pre_set_1) && (0x44 == pre_set_2) && (0x0C == pre_set_3)) {
                    TPD_INFO("%s: do not enable twice dir = %d\n", __func__, chip_info->touch_direction);
                    return 0;
                }
                touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL04_05, 0xFF);        //x part
                touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL04_06, 0x44);        //y part
                touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL04_08, 0x0C);
        } else if ((VERTICAL_SCREEN == chip_info->touch_direction) && enable) {
                if ((0x24 == pre_set_1) && (0xF5 == pre_set_2) && (0x05 == pre_set_3)) {
                    TPD_INFO("%s: do not enable twice dir = %d\n", __func__, chip_info->touch_direction);
                    return 0;
                }
                touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL04_05, 0x24);        //x part
                touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL04_06, 0xF5);        //y part
                touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL04_08, 0x05);
        }
        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL04_08);
        TPD_INFO("set corner mode, ret is 0x%x\n", ret);
        if (!chip_info->is_fp_down) {
                chip_info->force_update_needed = false;
                touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
                touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x04);    /* force update*/
                checkCMD(chip_info, 30);
        } else {
                chip_info->force_update_needed = true;
                TPD_INFO("%s: finger is down, skip force update\n", __func__);
        }
        touch_i2c_write_byte(chip_info->client, 0xff, 0x00);

        chip_info->rotation_changed_time = jiffies;     //update change time
        return ret;
}

static int synaptics_enable_edge_limit(struct chip_data_s3706 *chip_info, bool enable)
{
        int ret;

        TPD_INFO("%s, edge limit enable = %d, dir: %d\n", __func__, enable, chip_info->touch_direction);
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x04);
        if (ret < 0) {
                TPD_INFO("%s: select page failed ret = %d\n", __func__, ret);
                return -1;
        }

        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL50);
        if (enable) {
                if (chip_info->fw_corner_limit_support) {
                        ret &= 0xF7;
                } else {
                        ret |= 0x01;
                }
                TPD_INFO("enable is %d, ret = %d\n", enable, ret);
                touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL50, ret);
        } else  {
                if (chip_info->fw_corner_limit_support) {
                        ret |= 0x08;
                } else {
                        ret &= 0xFE;
                }
                TPD_INFO("enable is %d, ret = %d\n", enable, ret);
                touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL50, ret);
        }
        if (chip_info->fw_corner_limit_support) {
            synaptics_corner_limit_handle(chip_info, enable);
        }

        touch_i2c_write_byte(chip_info->client, 0xff, 0x00);

        return ret;
}

//enable face dectect function
static int synaptics_enable_face_detect(struct chip_data_s3706 *chip_info, bool enable)
{
        int ret = 0;

        touch_i2c_write_byte(chip_info->client, 0xff, 0x00);//set page 0
        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL00);
        if (ret < 0) {
                TPD_INFO("failed for get F01_RMI_CTRL00\n");
                return ret;
        }

        if (enable) {//enable nosleep flag
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL00, (ret | 0x04));
                if (ret < 0) {
                        TPD_INFO("%s failed for enable nodoze\n", __func__);
                        return ret;
                }
        } else {//disable nosleep flag
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL00, (ret & 0xFB));
                if (ret < 0) {
                        TPD_INFO("%s failed for disable nodoze\n", __func__);
                        return ret;
                }
        }
        msleep(5);  //add for fix sleep failed issue

        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x04);      //set page 4
        if(enable) {
                ret |= touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_DATA_BASE+0x29, 0x01);
        } else {
                ret |= touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_DATA_BASE+0x29, 0x00);
        }
        touch_i2c_write_byte(chip_info->client, 0xff, 0x00);      //set page 0

        TPD_INFO("%s state: %d %s\n", __func__, enable, ret < 0 ? "failed":"success");
        return ret;
}

static void synaptics_enable_charge_mode(struct chip_data_s3706 *chip_info, bool enable)
{
        int ret = 0, arg = 0;

        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x00);      //set page 0
        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL00);

        if(enable) {
                arg = ret | 0x20;
                TPD_DEBUG("%s enable is %d, arg is 0x%x\n", __func__, enable, arg);
                touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL00, arg);
        } else {
                arg = ret & 0xDF;
                TPD_DEBUG("%s enable is %d, arg is 0x%x\n", __func__, enable, arg);
                touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL00, arg);
        }
}

static void synaptics_enable_game_mode(struct chip_data_s3706 *chip_info, bool enable)
{
        int ret = 0;
        uint8_t tmp = 0x0;
        uint8_t game_buffer[13];
        struct touchpanel_data *ts = i2c_get_clientdata(chip_info->client);

        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x00);      //set page 0
        if (ret < 0) {
                TPD_INFO("%s:set page 0 fail\n",__func__);
                return;
        }

        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F12_2D_CTRL11, 13, &(game_buffer[0]));      /*get noise*/
        if (ret < 0) {
                TPD_INFO("%s:get noise fail\n",__func__);
                return;
        } else {
                TPD_INFO("%s:game_buffer[12]=0x%x\n",__func__, game_buffer[12]);
        }

        if (chip_info->report_120hz_support) {
            ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x04);      //set page 4
            if (ret < 0) {
                TPD_INFO("%s:set page 4 fail\n", __func__);
                return;
            }

            ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL30);  // read the strong lock point level related register
            if (ret < 0) {
                TPD_INFO("%s:get lock point related register fail\n", __func__);
                return;
            } else {
                tmp = ret & 0xFF;
                TPD_INFO("%s:lock point related register = 0x%x\n", __func__, tmp);
            }

            if(enable) {
                if (game_buffer[12] == ts->noise_level && tmp == ts->noise_level) {
                    TPD_INFO("game mode value already set, return\n");
                    return;
                }
                tmp = ts->noise_level;
                game_buffer[12] = ts->noise_level;
                TPD_DEBUG("%s enable is %d, arg is 0x%x\n", __func__, enable, game_buffer[12]);
            } else {
                if (game_buffer[12] == chip_info->default_nosie_level && tmp == chip_info->default_lock_point_level) {
                    TPD_INFO("un game mode value already set, return\n");
                    return;
                }
                tmp = chip_info->default_lock_point_level;
                game_buffer[12] = chip_info->default_nosie_level;
                TPD_DEBUG("%s disable is %d, arg is 0x%x\n", __func__, enable, game_buffer[12]);
            }

            ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL30, tmp);  // set the strong lock point level
            if (ret < 0) {
                TPD_INFO("%s:set F51_CUSTOM_CTRL30 for game mode fail\n", __func__);
                return;
            }

            ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x00);      //set page 0
            if (ret < 0) {
                TPD_INFO("%s:set page 0 fail\n",__func__);
                return;
            }
        } else {
            if(enable) {
                if (game_buffer[12] == ts->noise_level) {
                    TPD_INFO("game mode value already set, return\n");
                    return;
                }
                game_buffer[12] = ts->noise_level;
                TPD_DEBUG("%s enable is %d, arg is 0x%x\n", __func__, enable, game_buffer[12]);
            } else {
                if (game_buffer[12] == chip_info->default_nosie_level) {
                    TPD_INFO("un game mode value already set, return\n");
                    return;
                }
                game_buffer[12] = chip_info->default_nosie_level;
                TPD_DEBUG("%s disable is %d, arg is 0x%x\n", __func__, enable, game_buffer[12]);
            }
        }

        ret = touch_i2c_write_block(chip_info->client, chip_info->reg_info.F12_2D_CTRL11, 13, &game_buffer[0]);
        if (ret < 0) {
                TPD_INFO("%s:F12_2D_CTRL11 fail\n",__func__);
                return;
        }

        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x01);      //set page 1
        if (ret < 0) {
                TPD_INFO("%s:set page 1 fail\n",__func__);
                return;
        }
        TPD_INFO("%s:chip_info->reg_info.F54_ANALOG_COMMAND_BASE=0x%x\n",__func__, chip_info->reg_info.F54_ANALOG_COMMAND_BASE);
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x04);    /* force update*/
        if (ret < 0) {
                TPD_INFO("%s:force update fail\n",__func__);
                return;
        }
        checkCMD(chip_info, 30);
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x00);      //set page 0
        if (ret < 0) {
                TPD_INFO("%s:set page 0 fail\n",__func__);
                return;
        }


        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F12_2D_CTRL11, 13, &(game_buffer[0]));      /*get noise*/
        if (ret < 0) {
                TPD_INFO("%s:get noise fail\n",__func__);
                return;
        } else {
                TPD_INFO("%s:game_buffer[12]=0x%x\n",__func__, game_buffer[12]);
        }

}


static int synaptics_mode_switch(void *chip_data, work_mode mode, bool flag)
{
        int ret = -1;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        switch (mode) {
        case MODE_NORMAL:
                ret = synaptics_configuration_init(chip_info, true);
                if (ret < 0) {
                        TPD_INFO("%s: synaptics configuration init failed.\n", __func__);
                        return ret;
                }
                ret = synaptics_enable_interrupt(chip_info, true);
                if (ret < 0) {
                        TPD_INFO("%s: synaptics enable interrupt failed.\n", __func__);
                        return ret;
                }
                break;

        case MODE_SLEEP:
                ret = synaptics_enable_interrupt(chip_info, false);
                if (ret < 0) {
                        TPD_INFO("%s: synaptics enable interrupt failed.\n", __func__);
                        return ret;
                }

                /*device control: sleep mode*/
                ret = synaptics_configuration_init(chip_info, false);
                if (ret < 0) {
                        TPD_INFO("%s: synaptics configuration init failed.\n", __func__);
                        return ret;
                }
                break;

        case MODE_GESTURE:
                ret = synaptics_enable_black_gesture(chip_info, flag);
                if (ret < 0) {
                        TPD_INFO("%s: synaptics enable gesture failed.\n", __func__);
                        return ret;
                }
                break;

        case MODE_GLOVE:
                ret = synaptics_glove_mode_enable(chip_info, flag);
                if (ret < 0) {
                        TPD_INFO("%s: synaptics enable glove mode failed.\n", __func__);
                        return ret;
                }
                break;

        case MODE_EDGE:
                ret = synaptics_enable_edge_limit(chip_info, flag);
                if (ret < 0) {
                        TPD_INFO("%s: synaptics enable edg limit failed.\n", __func__);
                        return ret;
                }
                break;

        case MODE_FACE_DETECT:
                ret = synaptics_enable_face_detect(chip_info, flag);
                if (ret < 0) {
                        TPD_INFO("%s: synaptics enable face detect failed.\n", __func__);
                        return ret;
                }
                break;

        case MODE_CHARGE:
                synaptics_enable_charge_mode(chip_info, flag);
                break;

        case MODE_GAME:
                synaptics_enable_game_mode(chip_info, flag);
                break;

        default:
                TPD_INFO("%s: Wrong mode.\n", __func__);
        }

        return ret;
}

static int synaptics_get_gesture_info(void *chip_data, struct gesture_info * gesture)
{
        int ret = 0, i, gesture_sign, regswipe;
        uint8_t gesture_buffer[10];
        uint8_t coordinate_buf[25] = {0};
        uint16_t trspoint = 0;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x00);
        if (ret < 0) {
                TPD_INFO("failed to transfer the data, ret = %d\n", ret);
                return -1;
        }

        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F12_2D_DATA04, 5, &(gesture_buffer[0]));      /*get gesture type*/
        gesture_sign = gesture_buffer[0];
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x4);
        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F51_CUSTOM_DATA_BASE, 25, &(coordinate_buf[0]));      /*get gesture coordinate and swipe type*/
        regswipe = coordinate_buf[24];
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x0);

        /*detect the gesture mode*/
        switch (gesture_sign) {
        case DTAP_DETECT:
                gesture->gesture_type = DouTap;
                break;
        case SWIPE_DETECT:
                gesture->gesture_type = (regswipe == 0x41) ? Left2RightSwip   :
                        (regswipe == 0x42) ? Right2LeftSwip   :
                        (regswipe == 0x44) ? Up2DownSwip          :
                        (regswipe == 0x48) ? Down2UpSwip          :
                        (regswipe == 0x80) ? DouSwip                  :
                        UnkownGesture;
                break;
        case CIRCLE_DETECT:
                gesture->gesture_type = Circle;
                break;
        case VEE_DETECT:
                gesture->gesture_type = (gesture_buffer[2] == 0x01) ? DownVee  :
                        (gesture_buffer[2] == 0x02) ? UpVee        :
                        (gesture_buffer[2] == 0x04) ? RightVee :
                        (gesture_buffer[2] == 0x08) ? LeftVee  :
                        UnkownGesture;
                break;
        case UNICODE_DETECT:
                gesture->gesture_type = (gesture_buffer[2] == 0x77 && gesture_buffer[3] == 0x00) ? Wgestrue :
                        (gesture_buffer[2] == 0x6d && gesture_buffer[3] == 0x00) ? Mgestrue :
                        UnkownGesture;
                break;
        case FINGERPRINT_DOWN_DETECT:
                chip_info->is_fp_down = true;
                gesture->gesture_type = FingerprintDown;
                break;
        case FINGERPRINT_UP_DETECT:
                chip_info->is_fp_down = false;
                gesture->gesture_type = FingerprintUp;
                if (chip_info->force_update_needed) {
                        chip_info->force_update_needed = false;
                        touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
                        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x04);    /* force update*/
                        checkCMD(chip_info, 30);
                        touch_i2c_write_byte(chip_info->client, 0xff, 0x0);
                        TPD_INFO("do force update\n");
                }
                break;
        default:
                gesture->gesture_type = UnkownGesture;
        }

        if (gesture->gesture_type != UnkownGesture) {
                for (i = 0; i < 23; i += 2) {
                        trspoint = coordinate_buf[i]|coordinate_buf[i + 1] << 8;
                        TPD_DEBUG("synaptics TP read coordinate_point[%d] = %d\n", i, trspoint);
                }

                TPD_DEBUG("synaptics TP coordinate_buf = 0x%x\n", coordinate_buf[24]);
                gesture->Point_start.x = (coordinate_buf[0] | (coordinate_buf[1] << 8));
                gesture->Point_start.y = (coordinate_buf[2] | (coordinate_buf[3] << 8));
                gesture->Point_end.x   = (coordinate_buf[4] | (coordinate_buf[5] << 8));
                gesture->Point_end.y   = (coordinate_buf[6] | (coordinate_buf[7] << 8));
                gesture->Point_1st.x   = (coordinate_buf[8] | (coordinate_buf[9] << 8));
                gesture->Point_1st.y   = (coordinate_buf[10] | (coordinate_buf[11] << 8));
                gesture->Point_2nd.x   = (coordinate_buf[12] | (coordinate_buf[13] << 8));
                gesture->Point_2nd.y   = (coordinate_buf[14] | (coordinate_buf[15] << 8));
                gesture->Point_3rd.x   = (coordinate_buf[16] | (coordinate_buf[17] << 8));
                gesture->Point_3rd.y   = (coordinate_buf[18] | (coordinate_buf[19] << 8));
                gesture->Point_4th.x   = (coordinate_buf[20] | (coordinate_buf[21] << 8));
                gesture->Point_4th.y   = (coordinate_buf[22] | (coordinate_buf[23] << 8));
                gesture->clockwise         = (coordinate_buf[24] & 0x10) ? 1 :
                        (coordinate_buf[24] & 0x20) ? 0 : 2; /* 1--clockwise, 0--anticlockwise, not circle, report 2*/

                TPD_INFO("%s, gesture_sign: 0x%x, gesture_type: %d, clockwise: %d, points: (%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)\n", \
                            __func__, gesture_sign, gesture->gesture_type, gesture->clockwise, \
                            gesture->Point_start.x, gesture->Point_start.y, \
                            gesture->Point_end.x, gesture->Point_end.y, \
                            gesture->Point_1st.x, gesture->Point_1st.y, \
                            gesture->Point_2nd.x, gesture->Point_2nd.y, \
                            gesture->Point_3rd.x, gesture->Point_3rd.y, \
                            gesture->Point_4th.x, gesture->Point_4th.y);
        }

        return 0;
}

static int synaptics_power_control(void *chip_data, bool enable)
{
        int ret = 0;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        if (true == enable) {
                ret = tp_powercontrol_2v8(chip_info->hw_res, true);
                if (ret) {
                        return -1;
                }
                ret = tp_powercontrol_1v8(chip_info->hw_res, true);
                if (ret) {
                        return -1;
                }
                msleep(POWEWRUP_TO_RESET_TIME);
                synaptics_resetgpio_set(chip_info->hw_res, true);
                msleep(RESET_TO_NORMAL_TIME);
        } else {
                ret = tp_powercontrol_1v8(chip_info->hw_res, false);
                if (ret) {
                        return -1;
                }
                ret = tp_powercontrol_2v8(chip_info->hw_res, false);
                if (ret) {
                        return -1;
                }
                synaptics_resetgpio_set(chip_info->hw_res, false);
        }

        return ret;
}

static int checkCMD(struct chip_data_s3706 *chip_info, int retry_time)
{
        int ret;
        int flag_err = 0;

        do {
                msleep(10); /*wait 10ms*/
                ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE);
                ret = ret & 0x07;
                flag_err++;
        }while((ret > 0x00) && (flag_err < retry_time * 3));
        if (ret > 0x00) {
                TPD_INFO("checkCMD error ret is %x flag_err is %d\n", ret, flag_err);
                return -1;
        }

        return 0;
}

static int checkCMD_for_finger(struct chip_data_s3706 *chip_info)
{
        int ret;
        int flag_err = 0;

        do {
                msleep(10); /*wait 10ms*/
                ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE);
                flag_err++;
        }while((ret > 0x00) && (flag_err < SPURIOUS_FP_BASE_DATA_RETRY));
        TPD_INFO("checkCMD error ret is %x flag_err is %d\n", ret, flag_err);

        if (10 == flag_err) {
                TPD_INFO("checkCMD_for_finger error, reset  then msleep(80)\n");
                synaptics_reset(chip_info);
                msleep(80);
                synaptics_mode_switch(chip_info, MODE_NORMAL, true);
                return -1;
        } else {
                TPD_INFO("checkCMD_for_finger ok\n");
                return 0;
        }
}

unsigned char GetLogicalPin(unsigned char p_pin, uint8_t RX_NUM, uint8_t * rx_physical)
{
    unsigned char i = 0;
    for(i = 0; i < RX_NUM; i++)
    {
        if (rx_physical[i] == p_pin)
          return i;
    }
    return 0xff;
}

static int synaptics_capacity_test(struct seq_file *s, struct chip_data_s3706 *chip_info, struct syna_testdata *syna_testdata, struct test_header *ph, uint8_t *raw_data, uint8_t *data_buf)
{
        int ret = 0;
        int x = 0, y = 0, z = 0;
        int error_count = 0;
        uint8_t tmp_arg1 = 0, tmp_arg2 = 0;
        int16_t *baseline_data_test = NULL;
        int16_t baseline_data = 0, count = 0;

        /*step 1: Close CBC and test RT20*/
        TPD_INFO("\n step 1:Close CBC and test RT20\n");
        if (syna_testdata->fd >= 0) {
                sprintf(data_buf, "%s\n", "[RT20 Close CBC]");
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
        }
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x14);/*select report type 0x14*/
        if (ret < 0) {
                TPD_INFO("[line:%d]read_baseline: touch_i2c_write_byte failed \n", __LINE__);
                seq_printf(s, "[line:%d]read_baseline: touch_i2c_write_byte failed \n", __LINE__);
                error_count++;
                return error_count;
        }

        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE + 0x14, 0x01);        /*No SignalClarity*/
        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE + 0x17);                /*0125  CBC Xmtr carrier select*/
        tmp_arg1 = ret&0xff;

        /*Close CBC*/
        TPD_DEBUG("ret = %x, tmp_arg1 = %x, tmp_arg2 = %x\n", ret, tmp_arg1, (tmp_arg1 & 0xdf));
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE + 0x17, (tmp_arg1 & 0xdf));/*Set CBC, F54_ANALOG_CTRL88, close CBC*/
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x04);/*force update*/
        checkCMD(chip_info, 30);
        TPD_DEBUG("Test disable cbc\n");
        baseline_data_test = (uint16_t *)(syna_testdata->fw->data + ph->array_limit_offset);

        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0X02);/*force Cal*/
        checkCMD(chip_info, 30);
        TPD_DEBUG("Force Cal oK\n");
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
        checkCMD(chip_info, 30);
        TPD_INFO("key_TX is %d, key_RX is %d\n", syna_testdata->key_TX, syna_testdata->key_RX);
        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+3,
                syna_testdata->TX_NUM*syna_testdata->RX_NUM*2, raw_data);         /*read data*/
        for (x = 0; x < syna_testdata->TX_NUM; x++) {
                TPD_DEBUG_NTAG("[%d]: ", x);
                for (y = 0; y < syna_testdata->RX_NUM; y++) {
                        z = syna_testdata->RX_NUM * x + y;
                        tmp_arg1 = raw_data[z*2] & 0xff;
                        tmp_arg2 = raw_data[z*2 + 1] & 0xff;
                        baseline_data = (tmp_arg2 << 8) | tmp_arg1;

                        if (syna_testdata->fd >= 0) {
                                sprintf(data_buf, "%d, ", baseline_data);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                                ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                                sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
                        }

                        if ((y < syna_testdata->RX_NUM - syna_testdata->key_RX) && (x < syna_testdata->TX_NUM - syna_testdata->key_TX)) {
                                TPD_DEBUG_NTAG("%d, ", baseline_data);
                                if ((baseline_data < *(baseline_data_test + count*2)) || (baseline_data > *(baseline_data_test + count*2 + 1))) {
                                        TPD_INFO("Synaptic:touchpanel failed, raw data erro baseline_data[%d][%d] = %d[%d, %d]\n",
                                                x, y, baseline_data, *(baseline_data_test + count*2), *(baseline_data_test + count*2 + 1));
                                        seq_printf(s, "touchpanel failed, raw data erro baseline_data[%d][%d] = %d[%d, %d]\n",
                                                x, y, baseline_data, *(baseline_data_test + count*2), *(baseline_data_test + count*2 + 1));
                                        error_count++;
                                        return error_count;
                                }
                        } else if ((x == syna_testdata->TX_NUM - syna_testdata->key_TX) && (y >= syna_testdata->RX_NUM - syna_testdata->key_RX)) {
                                TPD_DEBUG("touchkey test xy(%d, %d), baseline_data=%d, limite( %d, %d)\n ", x, y, baseline_data,
                                        *(baseline_data_test + count*2), *(baseline_data_test + count*2 + 1));
                                if (((baseline_data) < *(baseline_data_test + count*2)) || ((baseline_data) > *(baseline_data_test + count*2 + 1))) {
                                        TPD_INFO("Synaptic:touchkey failed, raw data erro baseline_data[%d][%d] = %d[%d, %d]\n",
                                                x, y, baseline_data, *(baseline_data_test + count*2), *(baseline_data_test + count*2 + 1));
                                        seq_printf(s, "touchkey failed, raw data erro baseline_data[%d][%d] = %d[%d, %d]\n",
                                                x, y, baseline_data, *(baseline_data_test + count*2), *(baseline_data_test + count*2 + 1));
                                        error_count++;
                                        return error_count;
                                }
                        }

                        count++;
                }
                if (syna_testdata->fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                        ksys_write(syna_testdata->fd, "\n", 1);
#else
                        sys_write(syna_testdata->fd, "\n", 1);
#endif
                }
                TPD_DEBUG_NTAG("\n");
        }
        if (syna_testdata->fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                ksys_write(syna_testdata->fd, "\n", 1);
#else
                sys_write(syna_testdata->fd, "\n", 1);
#endif
        }
        /*step 2:check raw capacitance, Open CBC and testRT3*/
        TPD_INFO("\n step 2:Open CBC and test RT3\n");
        if (syna_testdata->fd >= 0) {
                sprintf(data_buf, "%s\n", "[RT3 Open CBC]");
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
        }

        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x03);/*select report type 0x03*/
        if (ret < 0) {
                TPD_INFO("read_baseline: touch_i2c_write_byte failed \n");
                error_count++;
                return error_count;
        }
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE + 0x14, 0x01);        /*No SignalClarity*/
        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE + 0x17);          /*0125  CBC Xmtr carrier select*/
        tmp_arg1 = ret&0xff;

        TPD_DEBUG("ret = %x, tmp_arg1 = %x, tmp_arg2 = %x\n", ret, tmp_arg1, (tmp_arg1 | 0x10));
        ret = touch_i2c_write_byte(chip_info->client,
                chip_info->reg_info.F54_ANALOG_CONTROL_BASE + 0x14, 0x00);          /* Disable No SignalClarity*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE + 0x17, (tmp_arg1 | 0x10));        /*open CBC*/
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x04);
        checkCMD(chip_info, 30);
        TPD_DEBUG("Test open cbc\n");
        baseline_data_test = (uint16_t *)(syna_testdata->fw->data + ph->array_limitcbc_offset);
        /******write No Relax to 1******/
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x04); /* force update*/
        checkCMD(chip_info, 30);
        TPD_DEBUG("forbid Forbid NoiseMitigation oK\n");
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0X02);/*force Cal*/
        checkCMD(chip_info, 30);
        TPD_DEBUG("Force Cal oK\n");
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
        checkCMD(chip_info, 30);

        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+3,
                syna_testdata->TX_NUM*syna_testdata->RX_NUM*2, raw_data);         /*read data*/
        count = 0;
        for (x = 0; x < syna_testdata->TX_NUM; x++) {
                TPD_DEBUG_NTAG("[%d]: ", x);
                for (y = 0; y < syna_testdata->RX_NUM; y++) {
                        z = syna_testdata->RX_NUM * x + y;
                        tmp_arg1 = raw_data[z*2] & 0xff;
                        tmp_arg2 = raw_data[z*2 + 1] & 0xff;
                        baseline_data = (tmp_arg2 << 8) | tmp_arg1;

                        if (syna_testdata->fd >= 0) {
                                sprintf(data_buf, "%d, ", baseline_data);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                                ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                                sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
                        }
                        if ((y < syna_testdata->RX_NUM - syna_testdata->key_RX) && (x < syna_testdata->TX_NUM - syna_testdata->key_TX)) {
                                TPD_DEBUG_NTAG("%d, ", baseline_data);
                                if ((baseline_data < *(baseline_data_test + count*2)) || (baseline_data > *(baseline_data_test + count*2 + 1))) {
                                        TPD_INFO("Synaptic:touchpanel failed, raw data erro baseline_data[%d][%d] = %d[%d, %d]\n",
                                                x, y, baseline_data, *(baseline_data_test + count*2), *(baseline_data_test + count*2 + 1));
                                        seq_printf(s, "touchpanel failed, raw data erro baseline_data[%d][%d] = %d[%d, %d]\n",
                                                x, y, baseline_data, *(baseline_data_test + count*2), *(baseline_data_test + count*2 + 1));
                                        error_count++;
                                        return error_count;
                                }
                        } else if ((x == syna_testdata->TX_NUM - syna_testdata->key_TX) && (y >= syna_testdata->RX_NUM - syna_testdata->key_RX)) {
                                TPD_INFO("touchkey test xy(%d, %d), baseline_data=%d, limite( %d, %d)\n ", x, y, baseline_data,
                                        *(baseline_data_test + count*2), *(baseline_data_test + count*2 + 1));
                                if (((baseline_data) < *(baseline_data_test + count*2)) || ((baseline_data) > *(baseline_data_test + count*2 + 1))) {
                                        TPD_INFO("Synaptic:touchkey failed, raw data erro baseline_data[%d][%d] = %d[%d, %d]\n",
                                                x, y, baseline_data, *(baseline_data_test + count*2), *(baseline_data_test + count*2 + 1));
                                        seq_printf(s, "touchkey failed, raw data erro baseline_data[%d][%d] = %d[%d, %d]\n",
                                                x, y, baseline_data, *(baseline_data_test + count*2), *(baseline_data_test + count*2 + 1));
                                        error_count++;
                                        return error_count;
                                }
                        }
                        count++;
                }
                if (syna_testdata->fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                        ksys_write(syna_testdata->fd, "\n", 1);
#else
                        sys_write(syna_testdata->fd, "\n", 1);
#endif
                }
                TPD_DEBUG_NTAG("\n");
        }

        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x04);
        checkCMD(chip_info, 30);

        return error_count;
}

static int synaptics_auto_test_rt25(struct seq_file *s, struct chip_data_s3706 *chip_info, struct syna_testdata *syna_testdata)
{
        int ret = 0;
        int i = 0, j = 0, x = 0;
        int error_count = 0;
        uint8_t buffer[9];
        uint8_t *buffer_rx = NULL;
        uint8_t *buffer_tx = NULL;
        //uint8_t buffer_rx[syna_testdata->RX_NUM];
        //uint8_t buffer_tx[syna_testdata->TX_NUM];
        int sum_line_error = 0;

        buffer_rx = kzalloc(syna_testdata->RX_NUM * (sizeof(uint8_t)), GFP_KERNEL);
        buffer_tx = kzalloc(syna_testdata->TX_NUM * (sizeof(uint8_t)), GFP_KERNEL);

        /*step 3 :check TRx-to-Ground, with rt25*/
        TPD_INFO("step 3:check TRx-to-Ground, with rt25\n");
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x19);/*select report type 25*/
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x0);
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
        /*msleep(100);*/
        checkCMD(chip_info, 30);
        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 3, 7, buffer);

        /*guomingqiang@phone.bsp, 2016-06-27, add for tp test step2*/
        touch_i2c_write_byte(chip_info->client, 0xff, 0x3);
        touch_i2c_read_block(chip_info->client, chip_info->reg_info.F55_SENSOR_CTRL01, syna_testdata->RX_NUM, buffer_rx);
        touch_i2c_read_block(chip_info->client, chip_info->reg_info.F55_SENSOR_CTRL02, syna_testdata->TX_NUM, buffer_tx);

        TPD_INFO("RX_NUM : ");
        for (i = 0; i< syna_testdata->RX_NUM; i++) {
                TPD_INFO("%d, ", buffer_rx[i]);
        }
        TPD_INFO("\n");

        TPD_INFO("TX_NUM : ");
        for (i = 0; i< syna_testdata->TX_NUM; i++) {
                TPD_INFO("%d, ", buffer_tx[i]);
        }
        TPD_INFO("\n");

        for (x = 0; x < 7; x++) {
                if (buffer[x] == 0) {
                        for (i = 0; i< 8; i++) {
                                if ((buffer[x] & (1 << i)) != 0) {
                                        sum_line_error = 8 * x + i + 1;
                                        for (j = 0; j < syna_testdata->RX_NUM; j++) {
                                                if (sum_line_error == buffer_rx[j]) {
                                                        error_count++;
                                                        TPD_INFO(" step 3 :check TRx-to-Ground, with rt25 error,  error_line is rx = %d\n", buffer_rx[j]);
                                                        seq_printf(s, " step 3 :check TRx-to-Ground, with rt25,  error_line is rx = %d\n", buffer_rx[j]);
                                                        goto OUT;
                                                }
                                        }
                                        for (j = 0; j < syna_testdata->TX_NUM; j++) {
                                                if (sum_line_error == buffer_tx[j]) {
                                                        error_count++;
                                                        TPD_INFO(" step 3 :check TRx-to-Ground, with rt25, error_line is tx = %d\n", buffer_tx[j]);
                                                        seq_printf(s, " step 3 :check TRx-to-Ground, with rt25, error_line is rx = %d\n", buffer_rx[j]);
                                                        goto OUT;
                                                }
                                        }
                                }
                        }
                }
        }

OUT:
        kfree(buffer_rx);
        buffer_rx = NULL;
        kfree(buffer_tx);
        buffer_tx = NULL;
        return error_count;
}

static int synaptics_auto_test_rt26(struct seq_file *s, struct chip_data_s3706 *chip_info, struct syna_testdata *syna_testdata)
{
        int ret = 0;
        int error_count = 0;
        int i = 0, x = 0;
        uint8_t buffer[9];
        uint8_t *buffer_rx;
        uint8_t *buffer_tx;
        //uint8_t buffer_rx[syna_testdata->RX_NUM];
        //uint8_t buffer_tx[syna_testdata->TX_NUM];
        //int sum_line_error = 0;

        buffer_rx = kzalloc(syna_testdata->RX_NUM * (sizeof(uint8_t)), GFP_KERNEL);
        buffer_tx = kzalloc(syna_testdata->TX_NUM * (sizeof(uint8_t)), GFP_KERNEL);

        /*step 4 :check tx-to-tx and tx-to-vdd*/
        TPD_INFO("step 4:check TRx-TRx & TRx-Vdd short\n");
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x1A);/*select report type 26*/
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x0);
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
        /*msleep(100);*/
        checkCMD(chip_info, 30);
        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 3, 7, buffer);

        /*guomingqiang@phone.bsp, 2016-06-27, add for tp test step2*/
        touch_i2c_write_byte(chip_info->client, 0xff, 0x3);                /*page 3*/
        touch_i2c_read_block(chip_info->client, chip_info->reg_info.F55_SENSOR_CTRL01, syna_testdata->RX_NUM, buffer_rx);
        touch_i2c_read_block(chip_info->client, chip_info->reg_info.F55_SENSOR_CTRL02, syna_testdata->TX_NUM, buffer_tx);
        touch_i2c_write_byte(chip_info->client, 0xff, 0x1);                /*page 1*/

        TPD_INFO("RX_NUM : ");
        for (i = 0; i< syna_testdata->RX_NUM; i++) {
                TPD_INFO("%d, ", buffer_rx[i]);
        }
        TPD_INFO("\n");

        TPD_INFO("TX_NUM : ");
        for (i = 0; i< syna_testdata->TX_NUM; i++) {
                TPD_INFO("%d, ", buffer_tx[i]);
        }
        TPD_INFO("\n");

        for (x = 0; x < 7; x++) {
        TPD_INFO("RT26 data byte %d - data 0x%x\n", x, buffer[x]);
        if (buffer[x] != 0) {
            if ((x == 0) && (buffer[x] & 0x01)) {   //pin - 0, these four pins should be test in RT100

            } else if ((x == 0) && (buffer[x] & 0x02)) {      //pin - 1

            } else if ((x == 4) && (buffer[x] & 0x01)) {      //pin -32

            } else if ((x == 4) && (buffer[x] & 0x02)) {      //pin -33

            } else {
                    error_count++;
                TPD_INFO("RT26 test fail!!!  data byte %d - data 0x%x\n", x, buffer[x]);
                                seq_printf(s, " step 4 :RT26 test fail!!!  data byte %d - data 0x%x\n", x, buffer[x]);
            }
        }
    }

        kfree(buffer_rx);
        buffer_rx = NULL;
        kfree(buffer_tx);
        buffer_tx = NULL;
        return error_count;
}

static int synaptics_auto_test_rt100(struct seq_file *s, struct chip_data_s3706 *chip_info, struct syna_testdata *syna_testdata, uint8_t * raw_data)
{
        int ret = 0;
        int error_count = 0;
        int i = 0, j = 0, x = 0, y = 0, z = 0;
        unsigned char temp_data[64] = {0};
        int16_t baseline_data = 0;
        short * p_data_baseline1 = NULL;
        short * p_data_baseline2 = NULL;
        short * p_data_delta = NULL;
        //uint8_t rx_assignment[syna_testdata->RX_NUM];
        //uint8_t rx_physical[syna_testdata->RX_NUM];
        //short minRX[syna_testdata->RX_NUM + 1];
        //short maxRX[syna_testdata->RX_NUM + 1];
        uint8_t *rx_assignment = NULL;
        uint8_t *rx_physical = NULL;
        short *minRX = NULL;
        short *maxRX = NULL;
        unsigned char logical_pin = 0xff;
        uint8_t ExtendRT26_pin[4] = {0, 1, 32, 33};

        p_data_baseline1 = kzalloc(syna_testdata->TX_NUM * syna_testdata->RX_NUM * 2 * (sizeof(uint8_t)), GFP_KERNEL);
        if (!p_data_baseline1) {
                error_count++;
                TPD_INFO("p_data_baseline1 kzalloc error\n");
                return error_count;
        }

        p_data_baseline2 = kzalloc(syna_testdata->TX_NUM * syna_testdata->RX_NUM * 2 * (sizeof(uint8_t)), GFP_KERNEL);
        if (!p_data_baseline2) {
                error_count++;
                TPD_INFO("p_data_baseline2 kzalloc error\n");
                kfree(p_data_baseline1);
                p_data_baseline1 = NULL;
                return error_count;
        }

        p_data_delta = kzalloc(syna_testdata->TX_NUM * syna_testdata->RX_NUM * 2 * (sizeof(uint8_t)), GFP_KERNEL);
        if(!p_data_delta) {
                error_count++;
                TPD_INFO("p_data_delta kzalloc error\n");
                kfree(p_data_baseline1);
                p_data_baseline1 = NULL;
                kfree(p_data_baseline2);
                p_data_baseline2 = NULL;
                return error_count;
        }

        rx_assignment = kzalloc(syna_testdata->RX_NUM * (sizeof(uint8_t)), GFP_KERNEL);
        rx_physical = kzalloc(syna_testdata->RX_NUM * (sizeof(uint8_t)), GFP_KERNEL);
        minRX = kzalloc((syna_testdata->RX_NUM + 1) * (sizeof(short)), GFP_KERNEL);
        maxRX = kzalloc((syna_testdata->RX_NUM + 1) * (sizeof(short)), GFP_KERNEL);

        //step 5:check RT100 for pin 0,1,32,33
        TPD_INFO("step 5:check RT100 for pin 0,1,32,33\n");
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE);         //read no scan
        ret |= 0x02;
        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE, ret);           //set no scan

        //set all local cbc to 0
        touch_i2c_write_block(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE + 0x1D, syna_testdata->RX_NUM, &temp_data[0]);
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x04);    /* force update*/
        checkCMD(chip_info, 30);

        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x64);/*select report type 100*/
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x0);
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/

        checkCMD(chip_info, 30);
        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 3, syna_testdata->TX_NUM * syna_testdata->RX_NUM * 2, raw_data);         /*read raw data1*/

        /*debug log*/
        TPD_INFO("baseline1:\n");
        for (x = 0, z = 0; x < syna_testdata->TX_NUM; x++) {
                TPD_DEBUG_NTAG("[%d]: ", x);
        for (y = 0; y < syna_testdata->RX_NUM; y++) {
            baseline_data = raw_data[z] | (raw_data[z + 1] << 8);
            p_data_baseline1[x * syna_testdata->RX_NUM + y] = baseline_data;
            z = z + 2;
                        TPD_DEBUG_NTAG("%d, ", p_data_baseline1[x * syna_testdata->RX_NUM + y]);
        }
                TPD_DEBUG_NTAG("\n");
    }
        /*end*/

        //get logical pin
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x03);        /* page 3*/
        touch_i2c_read_block(chip_info->client, 0x01, syna_testdata->RX_NUM, &rx_assignment[0]);
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
        for(i = 0; i < syna_testdata->RX_NUM; i++) {
                if(rx_assignment[i] != 0xff) {
                        rx_physical[i] = rx_assignment[i];
                }
        }

        for(i = 0; i < 4; i++) {
                for(j = 0; j < syna_testdata->RX_NUM; j++) {
            minRX[j] = 5000;
            maxRX[j] = 0;
                }

                logical_pin = GetLogicalPin(ExtendRT26_pin[i], syna_testdata->RX_NUM, rx_physical);

                if (logical_pin == 0xFF)
                        continue;

                //TPD_INFO("\ninfo: RT26 pin %d, logical pin %d \n", ExtendRT26_pin[i], logical_pin);

                // 14. set local CBC to 8pf(2D) 3.5pf(0D)
        temp_data[logical_pin] = 0x0f;          //EXTENDED_TRX_SHORT_CBC;

                ret = touch_i2c_write_block(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE + 0x1D, syna_testdata->RX_NUM, &temp_data[0]);
                if (ret < 0) {
                        error_count++;
            TPD_INFO("error: %s fail to set all F54 control_96 register after changing the cbc\n", __func__);
            goto END;
        }
                temp_data[logical_pin] = 0;

                // 15. force update
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x04);    /* force update*/
                checkCMD(chip_info, 30);

                // 16. read report type 100
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x64);/*select report type 100*/
                ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x0);
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/

                checkCMD(chip_info, 30);
                ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 3, syna_testdata->TX_NUM * syna_testdata->RX_NUM * 2, raw_data);         /*read raw data2*/

                /*debug log*/
                TPD_INFO("baseline2:\n");
                for (x = 0, z = 0; x < syna_testdata->TX_NUM; x++) {
                        TPD_DEBUG_NTAG("[%d]: ", x);
            for (y = 0; y < syna_testdata->RX_NUM; y++) {
                baseline_data = raw_data[z] | (raw_data[z + 1] << 8);
                p_data_baseline2[x * syna_testdata->RX_NUM + y] = baseline_data;
                z = z + 2;
                                TPD_DEBUG_NTAG("%d, ", p_data_baseline2[x * syna_testdata->RX_NUM + y]);
            }
                        TPD_DEBUG_NTAG("\n");
        }
                /*end*/

                // 17. get delta image between baseline image 1 and baseline image 2
        for (x = 0; x < syna_testdata->TX_NUM; x++) {
            for (y = 0; y < syna_testdata->RX_NUM; y++) {
                p_data_delta[x * syna_testdata->RX_NUM + y] = 
                    ABS( p_data_baseline1[x * syna_testdata->RX_NUM + y], p_data_baseline2[x * syna_testdata->RX_NUM + y]);

                if (maxRX[y] < p_data_delta[x * syna_testdata->RX_NUM + y])
                  maxRX[y] = p_data_delta[x * syna_testdata->RX_NUM + y];

                if (minRX[y] > p_data_delta[x * syna_testdata->RX_NUM + y])
                  minRX[y] = p_data_delta[x * syna_testdata->RX_NUM + y];
            }
        }

                /*debug log*/
                TPD_DEBUG("\n");
                for (y = 0; y < syna_testdata->RX_NUM; y++ ) {
                        TPD_DEBUG_NTAG("Rx%-2d(max, min) = (%4d, %4d)\n", y, maxRX[y], minRX[y]);
                }
                TPD_DEBUG("\n");
                /*end*/

                // 18. Check data: TREX w/o CBC raised changes >= 200 or TREXn* (TREX with CBC raised) changes < 2000
        // Flag TRX0* as 1 as well if any other RX changes are >=200
        for (x = 0; x < syna_testdata->RX_NUM; x++) {
            if (x == logical_pin) {
                if (minRX[x] < 600) {
                                        error_count++;
                    TPD_INFO("step 5:check RT100 for pin 0,1,32,33 test failed: minRX[%-2d] = %4d when test pin %d (RX Logical pin [%d])\n",
                                x, minRX[x], ExtendRT26_pin[i], logical_pin);
                                        seq_printf(s, "step 5:check RT100 for pin 0,1,32,33 test failed: minRX[%-2d] = %4d when test pin %d (RX Logical pin [%d])\n",
                                                x, minRX[x], ExtendRT26_pin[i], logical_pin);
                                        goto END;
                }
            } else {
                if (maxRX[x] >= 550) {
                                        error_count++;
                    TPD_INFO("step 5:check RT100 for pin 0,1,32,33 test failed: minRX[%-2d] = %4d when test pin %d (RX Logical pin [%d])\n",
                                x, minRX[x], ExtendRT26_pin[i], logical_pin);
                                        seq_printf(s, "step 5:check RT100 for pin 0,1,32,33 test failed: minRX[%-2d] = %4d when test pin %d (RX Logical pin [%d])\n",
                                                x, minRX[x], ExtendRT26_pin[i], logical_pin);
                                        goto END;
                }
            }
        }
        }

END:
        kfree(p_data_baseline1);
        p_data_baseline1 = NULL;
        kfree(p_data_baseline2);
        p_data_baseline2 = NULL;
        kfree(p_data_delta);
        p_data_delta = NULL;
        kfree(rx_assignment);
        rx_assignment = NULL;
        kfree(rx_physical);
        rx_physical = NULL;
        kfree(minRX);
        minRX = NULL;
        kfree(maxRX);
        maxRX = NULL;
        return error_count;
}

static int synaptics_auto_test_rt133(struct seq_file *s, struct chip_data_s3706 *chip_info, struct syna_testdata *syna_testdata)
{
        int ret = 0;
        int y = 0;
        int error_count = 0;
        uint8_t tmp_arg1 = 0, tmp_arg2 = 0;
        int16_t baseline_data = 0;

        /*Step 6 : Check the broken line with RT133*/
        TPD_INFO("Step 6 : Check the broken line\n");
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x85);/*select report type 0x85*/
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
        checkCMD(chip_info, 30);

        for (y = 0; y < syna_testdata->RX_NUM; y++) {
                ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 3);
                tmp_arg1 = ret & 0xff;
                ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 3);
                tmp_arg2 = ret & 0xff;
                baseline_data = (tmp_arg2 << 8) | tmp_arg1;
                TPD_INFO("Step 6 data[%d] is %d\n", y, baseline_data);
                if (baseline_data > 100) {
                        error_count++;
                        TPD_INFO(" step 6 :check the broken line, error_line is y =%d\n", y);
                        seq_printf(s, "step 6 :check the broken line, error_line is y =%d, data[%d] = %d\n", y, y, baseline_data);
                        return error_count;
                }
        }

        seq_printf(s, "\n");

        return error_count;
}

static int synaptics_auto_test_rt150(struct seq_file *s, struct chip_data_s3706 *chip_info, struct syna_testdata *syna_testdata, uint8_t *data_buf)
{
        int ret = 0;
        int y = 0;
        int error_count = 0;
        uint8_t tmp_arg1 = 0, tmp_arg2 = 0;
        uint16_t unsigned_baseline_data = 0;

        /*Step 7 : Check RT150 for random touch event*/
        TPD_INFO("Step 7 : Check RT150 for random touch event\n");
        if (syna_testdata->fd >= 0) {
                sprintf(data_buf, "%s\n", "[RT150]");
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
        }
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x96);/*select report type RT150*/
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
        checkCMD(chip_info, 70);
        for (y = 0; y < syna_testdata->RX_NUM + syna_testdata->TX_NUM - syna_testdata->key_TX - syna_testdata->key_RX; y++) {
                ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 3);
                tmp_arg1 = ret & 0xff;
                ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 3);
                tmp_arg2 = ret & 0xff;
                unsigned_baseline_data = (tmp_arg2 << 8) | tmp_arg1;
                if (syna_testdata->fd >= 0) {
                        sprintf(data_buf, "%d, ", unsigned_baseline_data);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                        ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                        sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
                }
                if (unsigned_baseline_data < 7000) {
                        error_count++;
                        TPD_INFO("Step 7 : Check RT150 for random touch event, error_line is y =%d, data[%d] = %hu\n", y, y, unsigned_baseline_data);
                        seq_printf(s, "Step 7 : Check RT150 for random touch event, error_line is y =%d, data[%d] = %hu\n", y, y, unsigned_baseline_data);
                        return error_count;
                }
        }
        if (syna_testdata->fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                ksys_write(syna_testdata->fd, "\n", 1);
#else
                sys_write(syna_testdata->fd, "\n", 1);
#endif
        }

        return error_count;
}

static int synaptics_auto_test_rt154(struct seq_file *s, struct chip_data_s3706 *chip_info, struct syna_testdata *syna_testdata, uint8_t *data_buf)
{
        int ret = 0;
        int error_count = 0;
        int y = 0;
        uint8_t tmp_arg1 = 0, tmp_arg2 = 0;
        uint16_t unsigned_baseline_data = 0;

        /*Step 8 : Check RT154 for self raw data check*/
        TPD_INFO("Step 8 : Check RT154 for self raw data check\n");
        if (syna_testdata->fd >= 0) {
                sprintf(data_buf, "%s\n", "[RT154]");
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
        }
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x9A);/*select report type 0xFE*/
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
        checkCMD(chip_info, 30);
        for (y = 0; y < chip_info->hw_res->RX_NUM + chip_info->hw_res->TX_NUM; y++) {
                ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 3);
                tmp_arg1 = ret & 0xff;
                ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 3);
                tmp_arg2 = ret & 0xff;
                unsigned_baseline_data = (tmp_arg2 << 8) | tmp_arg1;
                if (syna_testdata->fd >= 0) {
                        sprintf(data_buf, "%d, ", unsigned_baseline_data);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                        ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                        sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
                }
                if ((syna_testdata->key_RX != 0 || syna_testdata->key_TX != 0)
                        && (((y > chip_info->hw_res->RX_NUM - syna_testdata->key_RX - 1) && (y < chip_info->hw_res->RX_NUM))
                        || (y > chip_info->hw_res->RX_NUM + chip_info->hw_res->TX_NUM - syna_testdata->key_TX - 1))) {
                        TPD_INFO("y = %d\n", y);
                        continue;
                }
                if (unsigned_baseline_data < 50 || unsigned_baseline_data > 975) {
                        error_count++;
                        TPD_INFO("Step 8 : Check RT154 for self raw data check, error_line is y =%d, data[%d] = %hu\n", y, y, unsigned_baseline_data);
                        seq_printf(s, "Step 8 : Check RT154 for self raw data check, error_line is y =%d, data[%d] = %hu\n", y, y, unsigned_baseline_data);
                        return error_count;
                }
        }
        if (syna_testdata->fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                ksys_write(syna_testdata->fd, "\n", 1);
#else
                sys_write(syna_testdata->fd, "\n", 1);
#endif
        }

        return error_count;
}

static int synaptics_auto_test_rt155(struct seq_file *s, struct chip_data_s3706 *chip_info, struct syna_testdata *syna_testdata, uint8_t *data_buf)
{
        int ret = 0;
        int error_count = 0;
        int y = 0;
        uint8_t tmp_arg1 = 0, tmp_arg2 = 0;
        int16_t baseline_data = 0;

        /*Step 9 : Check RT155 for abs delta error*/
        TPD_INFO("Step 9 : Check RT155 for abs delta\n");
        if (syna_testdata->fd >= 0) {
                sprintf(data_buf, "%s\n", "[RT155]");
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
        }
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE);      /*get 0x10E status*/
        ret |= 0x01;
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE, ret);       /*set no relax*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x04);    /* force update*/
        checkCMD(chip_info, 30);
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x9B);/*select report type 0x9B*/
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
        checkCMD(chip_info, 30);
        for (y = 0; y < chip_info->hw_res->RX_NUM + chip_info->hw_res->TX_NUM; y++) {
                ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 3);
                tmp_arg1 = ret & 0xff;
                ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 3);
                tmp_arg2 = ret & 0xff;
                baseline_data = (tmp_arg2 << 8) | tmp_arg1;
                if (syna_testdata->fd >= 0) {
                        sprintf(data_buf, "%d, ", baseline_data);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                        ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                        sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
                }
                if (baseline_data > 2000) {
                        error_count++;
                        TPD_INFO("Step 9 : Check RT155 for abs delta error, error_line is y =%d, data[%d] = %d\n", y, y, baseline_data);
                        seq_printf(s, "Step 9 : Check RT155 for abs delta error, error_line is y =%d, data[%d] = %d\n", y, y, baseline_data);
                        return error_count;
                }
        }
        if (syna_testdata->fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                ksys_write(syna_testdata->fd, "\n", 1);
#else
                sys_write(syna_testdata->fd, "\n", 1);
#endif
        }

        return error_count;
}

static int synaptics_auto_test_rt155_fd(struct seq_file *s, struct chip_data_s3706 *chip_info, struct syna_testdata *syna_testdata, uint8_t *data_buf)
{
        int ret = 0, j = 0, error_count = 0;
        const int node_num = syna_testdata->TX_NUM + syna_testdata->RX_NUM, judge_threshold = 300;
        uint8_t raw_data[200] = {0};
        int32_t max_node_data = 0;

        /*Step 9 : Check RT155 for abs delta error*/
        TPD_INFO("Step 9 : Check RT155 for fd abs noise\n");
        if (syna_testdata->fd >= 0) {
                sprintf(data_buf, "%s\n", "[RT155]");
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
        }
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
        ret |= touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x9B);/*select report type 0x9B*/
        ret |= touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
        ret |= touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
        checkCMD(chip_info, 40);
        ret |= touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+3, node_num*2, raw_data);         /*read data*/
        if (ret < 0) {
                error_count++;
                TPD_INFO("Step 9 : read RT155 for fd abs noise error.\n");
                seq_printf(s, "Step 9 : read RT155 for fd abs noise error.\n");
                return error_count;
        }
        for (j = 0; j < syna_testdata->RX_NUM; j++) {
                max_node_data = (raw_data[j*2] | (raw_data[j*2+1]  << 8));
                if (syna_testdata->fd >= 0) {
                        sprintf(data_buf, "%d, ", max_node_data);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                        ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                        sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
                }
                if (max_node_data > judge_threshold) {
                        TPD_INFO("Check RT155 for fd abs noise, data[%d] = %d(%d)\n", j, max_node_data, judge_threshold);
                        if (!error_count)
                                seq_printf(s, "Check RT155 for fd abs noise, data[%d] = %d(%d)\n", j, max_node_data, judge_threshold);
                        error_count++;
                }
        }
        if (syna_testdata->fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                ksys_write(syna_testdata->fd, "\n", 1);
#else
                sys_write(syna_testdata->fd, "\n", 1);
#endif
        }

        return error_count;
}

static int synaptics_auto_test_rt59(struct seq_file *s, struct chip_data_s3706 *chip_info, struct syna_testdata *syna_testdata, uint8_t *data_buf)
{
        int ret = 0;
        int error_count = 0;
        int i = 0, j = 0;
        const int data_cycle = 20, node_num = syna_testdata->TX_NUM+syna_testdata->RX_NUM, judge_threshold = 300;
        int32_t max_store[50] = {0};
        uint8_t raw_data[200] = {0};
        int32_t node_data = 0;

        /*Step 9 : Check RT155 for abs delta error*/
        TPD_INFO("Step 9 : Check RT59 for abs delta\n");
        if (syna_testdata->fd >= 0) {
                sprintf(data_buf, "%s\n", "[RT59]");
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
        }
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x3B);/*select report type 59*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0X02);/*force Cal*/
        checkCMD(chip_info, 30);

        for (i = 0; i < data_cycle; i++) {
                ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
                checkCMD(chip_info, 30);
                ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+3, node_num*4, raw_data);         /*read data*/
                for (j = 0; j < node_num; j++) {
                        node_data = (raw_data[j*4] | (raw_data[j*4+1]  << 8) | (raw_data[j*4+2]  << 16) | (raw_data[j*4+3]  << 24));
                        if (max_store[j] < node_data) {
                                max_store[j] = node_data;
                        }
                }
        }

        for (i = 0; i < syna_testdata->RX_NUM; i++) {
                if (syna_testdata->fd >= 0) {
                        sprintf(data_buf, "%d, ", max_store[i]);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                        ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                        sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
                }
                if (abs(max_store[i]) > judge_threshold) {
                        TPD_INFO("Check RT59 for abs noise, data[%d] = %d(%d)\n", i, max_store[i], judge_threshold);
                        if (!error_count)
                                seq_printf(s, "Check RT59 for abs noise, data[%d] = %d(%d)\n", i, max_store[i], judge_threshold);
                        error_count++;
                }
        }
        if (syna_testdata->fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                ksys_write(syna_testdata->fd, "\n", 1);
#else
                sys_write(syna_testdata->fd, "\n", 1);
#endif
        }

        return error_count;
}

static int synaptics_auto_test_rt63(struct seq_file *s, struct chip_data_s3706 *chip_info, struct syna_testdata *syna_testdata, uint8_t *data_buf)
{
        int ret = 0, error_count = 0, i = 0;
        const int node_num = syna_testdata->TX_NUM+syna_testdata->RX_NUM;
        uint8_t raw_data[200] = {0};
        int32_t node_data = 0;

        /*Step 9 : Check RT155 for abs delta error*/
        TPD_INFO("Step 9 : Check RT63 for hover sensing\n");
        if (syna_testdata->fd >= 0) {
                sprintf(data_buf, "%s\n", "[RT63]");
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
        }

        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x3F);/*select report type 63*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0X02);/*force Cal*/
        checkCMD(chip_info, 30);
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
        checkCMD(chip_info, 30);
        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+3, node_num*4, raw_data);         /*read data*/
        if (ret < 0) {
            error_count++;
            TPD_INFO("read RT63 data failed\n");
            seq_printf(s, "read RT63 data failed\n");
            return error_count;
        }

        for (i = 0; i < syna_testdata->RX_NUM; i++) {
            node_data = (raw_data[i*4] | (raw_data[i*4+1]  << 8) | (raw_data[i*4+2]  << 16) | (raw_data[i*4+3]  << 24));
            if (syna_testdata->fd >= 0) {
                    sprintf(data_buf, "%d, ", node_data);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                    ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                    sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
            }
            if ((node_data < 10000) || (node_data > 60000)) {
                TPD_INFO("Check RT63 for hover, data[%d] = %d\n", i, node_data);
                if (!error_count)
                        seq_printf(s, "Check RT63 for hover, data[%d] = %d\n", i, node_data);
                error_count++;
            }
        }
        if (syna_testdata->fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                ksys_write(syna_testdata->fd, "\n", 1);
#else
                sys_write(syna_testdata->fd, "\n", 1);
#endif
        }

        return error_count;
}

static void synaptics_auto_test(struct seq_file *s, void *chip_data, struct syna_testdata *syna_testdata)
{
        int ret = 0;
        int error_count = 0;
        int eint_status, eint_count = 0, read_gpio_num = 0;
        uint8_t  data_buf[64];
        struct test_header *ph = NULL;
        uint8_t * raw_data = NULL;

        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        raw_data = kzalloc(syna_testdata->TX_NUM * syna_testdata->RX_NUM * 2 * (sizeof(uint8_t)), GFP_KERNEL);
        if (!raw_data) {
                        TPD_INFO("raw_data kzalloc error\n");
                        return;
        }

        /*open file to save test data*/
        ph = (struct test_header *)(syna_testdata->fw->data);

        TPD_INFO("%s, step 0: begin to check INT-GND short item\n", __func__);
        ret = synaptics_enable_interrupt(chip_info, false);
        eint_count = 0;
        read_gpio_num = 10;
        while (read_gpio_num--) {
                msleep(5);
                eint_status = gpio_get_value(syna_testdata->irq_gpio);
                if (eint_status == 1) {
                        eint_count--;
                }
                else {
                        eint_count++;
                }
                TPD_INFO("%s eint_count = %d  eint_status = %d\n", __func__, eint_count, eint_status);
        }
        TPD_INFO("TP EINT PIN direct short! eint_count = %d\n", eint_count);
        if (eint_count == 10) {
                TPD_INFO("error :  TP EINT PIN direct short!\n");
                error_count++;
                seq_printf(s, "eint_status is low, TP EINT direct stort\n");
                sprintf(data_buf, "eint_status is low, TP EINT direct stort, \n");
                if (syna_testdata->fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                        ksys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#else
                        sys_write(syna_testdata->fd, data_buf, strlen(data_buf));
#endif
                }
                eint_count = 0;
                goto END;
        }

        synaptics_read_F54_base_reg(chip_info);

        ret = synaptics_capacity_test(s, chip_info, syna_testdata, ph, raw_data, data_buf);
        if(ret > 0) {
                TPD_INFO("synaptics_capacity_test failed! ret is %d\n", ret);
                error_count++;
                goto END;
        }

        ret = synaptics_auto_test_rt25(s, chip_info, syna_testdata);
        if(ret > 0) {
                TPD_INFO("synaptics_auto_test_rt25 failed, ret is %d\n", ret);
                error_count++;
                goto END;
        }
        synaptics_reset(chip_info);
        msleep(50);

        ret = synaptics_auto_test_rt26(s, chip_info, syna_testdata);
        if(ret > 0) {
                TPD_INFO("synaptics_auto_test_rt26 failed! ret is %d\n", ret);
                error_count++;
                goto END;
        }
        synaptics_reset(chip_info);
        msleep(50);

        ret = synaptics_auto_test_rt100(s, chip_info, syna_testdata, raw_data);
        if(ret > 0) {
                TPD_INFO("synaptics_auto_test_rt100 failed! ret is %d\n", ret);
                error_count++;
                goto END;
        }
        synaptics_reset(chip_info);
        msleep(50);

        ret = synaptics_auto_test_rt133(s, chip_info, syna_testdata);
        if(ret > 0) {
                TPD_INFO("synaptics_auto_test_rt133 failed! ret is %d\n", ret);
                error_count++;
                goto END;
        }

        msleep(20);

        ret = synaptics_auto_test_rt150(s, chip_info, syna_testdata, data_buf);
        if(ret > 0) {
                TPD_INFO("synaptics_auto_test_rt150 failed! ret is %d\n", ret);
                error_count++;
                goto END;
        }

        ret = synaptics_auto_test_rt154(s, chip_info, syna_testdata, data_buf);
        if(ret > 0) {
                TPD_INFO("synaptics_auto_test_rt154 failed! ret is %d\n", ret);
                error_count++;
                goto END;
        }

        if(!syna_testdata->fingerprint_underscreen_support) {
                ret = synaptics_auto_test_rt155(s, chip_info, syna_testdata, data_buf);
                if(ret > 0) {
                        TPD_INFO("synaptics_auto_test_rt155 failed! ret is %d\n", ret);
                        error_count++;
                        goto END;
                }
        }
        synaptics_reset(chip_info);
        msleep(50);

        if (syna_testdata->fd_support) {
                if (chip_info->rt155_fdreplace_rt59_support) {
                        ret = synaptics_auto_test_rt155_fd(s, chip_info, syna_testdata, data_buf);
                } else {
                        ret = synaptics_auto_test_rt59(s, chip_info, syna_testdata, data_buf);
                }
                if(ret > 0) {
                        TPD_INFO("synaptics_auto_test_rt59/rt155 failed! ret is %d\n", ret);
                        error_count++;
                        goto END;
                }

                ret = synaptics_auto_test_rt63(s, chip_info, syna_testdata, data_buf);
                if(ret > 0) {
                        TPD_INFO("synaptics_auto_test_rt63 failed! ret is %d\n", ret);
                        error_count++;
                        goto END;
                }
                synaptics_reset(chip_info);
        }

        touch_i2c_write_byte(chip_info->client, 0xff, 0x00);        /* page 0*/

END:
        kfree(raw_data);
        seq_printf(s, "imageid = 0x%llx, deviceid = 0x%llx\n", syna_testdata->TP_FW, syna_testdata->TP_FW);
        seq_printf(s, "%d error(s). %s\n", error_count, error_count?"":"All test passed.");
        TPD_INFO(" TP auto test %d error(s). %s\n", error_count, error_count?"":"All test passed.");
        TPD_INFO("\n\nstep5 reset and open irq complete\n");
}

static void synaptics_baseline_read(struct seq_file *s, void *chip_data)
{
        int ret = 0;
        int x = 0, y = 0, z = 0;
        uint8_t tmp_arg1 = 0;
        uint16_t baseline_data = 0;
        uint8_t *raw_data = NULL;
        int enable_cbc = 0;/*enable cbc flag for baseline limit test*/
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        if (!chip_info) {
                return;
        }
        raw_data = kzalloc(chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * 2 * (sizeof(uint8_t)), GFP_KERNEL);
        if (!raw_data) {
                        TPD_INFO("raw_data kzalloc error\n");
                        return;
        }
        synaptics_read_F54_base_reg(chip_info);
        do {
                if (enable_cbc) {
                        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x03);/*select report type 0x03*/
                } else {
                        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x14);/*select report type 0x14*/
                }
                if (ret < 0) {
                        TPD_INFO("read_baseline: touch_i2c_write_byte failed \n");
                        seq_printf(s, "what the hell4, ");
                        goto END;
                }
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE + 0x14, 0x01);        /*No SignalClarity*/
                ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE + 0x17);
                tmp_arg1 = ret & 0xff;

                if (enable_cbc) {
                        seq_printf(s, "\nWith CBC:\n");
                        TPD_DEBUG("ret = %x, tmp_arg1 = %x, tmp_arg2 = %x\n", ret, tmp_arg1, (tmp_arg1 | 0x10));
                        ret = touch_i2c_write_byte(chip_info->client,
                                chip_info->reg_info.F54_ANALOG_CONTROL_BASE + 0x14, 0x00);          /* Disable No SignalClarity*/
                        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE + 0x17, (tmp_arg1 | 0x10));
                        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x04);
                        checkCMD(chip_info, 30);
                        TPD_DEBUG("Test open cbc\n");
                } else {
                        seq_printf(s, "\nWithout CBC:\n");
                        TPD_DEBUG("ret = %x, tmp_arg1 = %x, tmp_arg2 = %x\n", ret, tmp_arg1, (tmp_arg1 & 0xef));
                        ret = touch_i2c_write_byte(chip_info->client,
                                chip_info->reg_info.F54_ANALOG_CONTROL_BASE + 20, 0x01);          /* Enable No SignalClarity*/
                        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE + 0x17, (tmp_arg1 & 0xef));
                        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x04); /* force update*/
                        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_CONTROL_BASE + 7, 0x01);/* Forbid NoiseMitigation*/
                }
                /******write No Relax to 1******/
                ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x04); /* force update*/
                checkCMD(chip_info, 30);
                TPD_DEBUG("forbid Forbid NoiseMitigation oK\n");
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0X02);/*force Cal*/
                checkCMD(chip_info, 30);
                TPD_DEBUG("Force Cal oK\n");
                ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
                checkCMD(chip_info, 30);
                ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+3,
                        chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * 2, raw_data);         /*read data*/

                for (x = 0; x < chip_info->hw_res->TX_NUM; x++) {
                        seq_printf(s, "[%2d] ", x);
                        for (y = 0; y < chip_info->hw_res->RX_NUM; y++) {
                                z = chip_info->hw_res->RX_NUM * x + y;
                                baseline_data = (raw_data[z * 2 + 1] << 8) | raw_data[z * 2];
                                seq_printf(s, "%4d, ", baseline_data);
                        }
                        seq_printf(s, "\n");
                }
                enable_cbc++;
        }while(enable_cbc < 2);

END:
        kfree(raw_data);
        return;
}

/*Reserved node*/
static void synaptics_reserve_read(struct seq_file *s, void *chip_data)
{
        int ret = 0;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        if (!chip_info) {
                return;
        }

        /*1.get firmware doze mode info*/
        TPD_INFO("1.get firmware doze mode info\n");
        seq_printf(s, "1.get firmware doze mode info\n");
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x04);        /* page 4*/
        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_DATA_BASE + 0x1A);
        TPD_INFO("TP doze mode status is %d\n", ret);
        seq_printf(s, "TP doze mode status is %d\n", ret);

        msleep(10);
}

static void synaptics_RT76_read(struct seq_file *s, void *chip_data)
{
        int ret = 0, x = 0, y = 0, z = 0;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;
        int16_t temp_delta = 0;
        uint8_t *raw_data = NULL;

        if (!chip_info) {
                return;
        }

        raw_data = kzalloc(chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * 2 * (sizeof(uint8_t)), GFP_KERNEL);
        if (!raw_data) {
                        TPD_INFO("raw_data kzalloc error\n");
                        return;
        }

        /*disable irq when read data from IC*/
        synaptics_read_F54_base_reg(chip_info);

        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x4C);/*select report type 0x4C*/
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
        checkCMD(chip_info, 30);
        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+3,
                chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * 2, raw_data);         /*read data*/
        for (x = 0; x < chip_info->hw_res->TX_NUM; x++) {
                seq_printf(s, "\n[%2d]", x);
                for (y = 0; y < chip_info->hw_res->RX_NUM; y++) {
                        z = chip_info->hw_res->RX_NUM * x + y;
                        temp_delta = (raw_data[z * 2 + 1] << 8) | raw_data[z * 2];
                        seq_printf(s, "%4d, ", temp_delta);
                }
        }
        seq_printf(s, "\n");

        touch_i2c_write_byte(chip_info->client, 0xff, 0x00);        /* page 0*/
        msleep(60);
        kfree(raw_data);
}

static void synaptics_RT251_read(struct seq_file *s, void *chip_data)
{
        int ret = 0, y = 0;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;
        uint8_t tmp_arg1 = 0, tmp_arg2 = 0;
        uint16_t RT_data = 0;

        if (!chip_info) {
                return;
        }

        /*disable irq when read data from IC*/
        synaptics_read_F54_base_reg(chip_info);

        /*Check RT252 for random touch event*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x96);/*select report type 0xFC*/
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
        checkCMD(chip_info, 70);
        seq_printf(s, "\n[RT252]");
        for (y = 0; y < chip_info->hw_res->RX_NUM + chip_info->hw_res->TX_NUM - chip_info->hw_res->key_TX - chip_info->hw_res->key_RX; y++) {
                ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 3);
                tmp_arg1 = ret & 0xff;
                ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 3);
                tmp_arg2 = ret & 0xff;
                RT_data = (tmp_arg2 << 8) | tmp_arg1;
                seq_printf(s, "%hu, ", RT_data);
        }
        seq_printf(s, "\n");

        /*Get RT254 info*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x9A);/*select report type 0xFE*/
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
        checkCMD(chip_info, 70);
        seq_printf(s, "\n[RT254]");
        for (y = 0; y < chip_info->hw_res->RX_NUM + chip_info->hw_res->TX_NUM - chip_info->hw_res->key_TX - chip_info->hw_res->key_RX; y++) {
                ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 3);
                tmp_arg1 = ret & 0xff;
                ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 3);
                tmp_arg2 = ret & 0xff;
                RT_data = (tmp_arg2 << 8) | tmp_arg1;
                seq_printf(s, "%hu, ", RT_data);
        }
        seq_printf(s, "\n");

        touch_i2c_write_byte(chip_info->client, 0xff, 0x00);        /* page 0*/
        synaptics_reset(chip_info);
        msleep(60);
}

static void synaptics_main_register_read(struct seq_file *s, void *chip_data)
{
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        if (!chip_info)
                return ;
        TPD_INFO("%s start\n", __func__);
        /*disable irq when read data from IC*/
        seq_printf(s, "====================================================\n");
        if(chip_info->p_tp_fw) {
                seq_printf(s, "tp fw = 0x%x\n", *(chip_info->p_tp_fw));
        }
        seq_printf(s, "====================================================\n");

        msleep(10);
}

#if GESTURE_COORD_GET
#define GESTURE_COORD_LEN 4096
static char str_gesture[GESTURE_COORD_LEN];
static void synaptics_get_gesture_coord(void *chip_data, uint32_t gesture_type)
{
        int ret = 0, x = 0, y = 0, z = 0;
        int16_t temp_delta = 0;
        uint8_t *raw_data = NULL;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;
        char str_tmp[10] = {0};
        mm_segment_t old_fs;
        char *gesture_file_bf = "/sdcard/gesture_test.txt";
        static int fd = -1;
        static int loop = 0;
        if (!chip_info) {
                return;
        }

        memset(str_gesture, 0, GESTURE_COORD_LEN);
        sprintf(str_tmp, " %d,", gesture_type);
        strcat(str_gesture, str_tmp);

        raw_data = kzalloc(chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * 2 * (sizeof(uint8_t)), GFP_KERNEL);
        if (!raw_data) {
                TPD_INFO("raw_data kzalloc error\n");
                 return;
        }
        old_fs = get_fs();
        set_fs(KERNEL_DS);
        if(loop == 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
                fd = ksys_open(gesture_file_bf, O_WRONLY | O_CREAT | O_TRUNC, 0);
#else
                fd = sys_open(gesture_file_bf, O_WRONLY | O_CREAT | O_TRUNC, 0);
#endif
                if (fd < 0) {
                        TPD_INFO("Open gesture_file_bf file '%s' failed.\n", gesture_file_bf);
                        set_fs(old_fs);
                        return;
                }
        }
        loop++;
        TPD_INFO("Open gesture_file_bf ret %d.\n", ret);
        /*disable irq when read data from IC*/
        synaptics_read_F54_base_reg(chip_info);

        /*TPD_DEBUG("\nstep 2:report type2 delta image\n");*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x02);/*select report type 0x02*/
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
        checkCMD(chip_info, 30);
        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+3,
                chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * 2, raw_data);         /*read data*/
        for (x = 0; x < chip_info->hw_res->TX_NUM / 3; x++) {
                for (y = 0; y < chip_info->hw_res->RX_NUM; y++) {
                        z = chip_info->hw_res->RX_NUM * x + y;
                        temp_delta = (raw_data[z * 2 + 1] << 8) | raw_data[z * 2];
                        sprintf(str_tmp, " %4d,", temp_delta);
                        strcat(str_gesture, str_tmp);
                }
        }
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_write(fd, str_gesture, strlen(str_gesture));
        ksys_write(fd, "\n", 1);
#else
        sys_write(fd, str_gesture, strlen(str_gesture));
        sys_write(fd, "\n", 1);
#endif
        TPD_INFO("str_gesture:%s\n", str_gesture);
        if (gesture_type == DouSwip) {
            TPD_INFO("str_gessec\n");
            memset(str_gesture, 0, GESTURE_COORD_LEN);
            memset(raw_data, 0, chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * 2 * (sizeof(uint8_t)));
            /*TPD_DEBUG("\nstep 2:report type3 delta image\n");*/
            ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x03);/*select report type 0x02*/
            ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
            ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
            checkCMD(chip_info, 30);
            ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+3,
                    chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * 2, raw_data);         /*read data*/
            for (x = 0; x < chip_info->hw_res->TX_NUM / 3; x++) {
                    for (y = 0; y < chip_info->hw_res->RX_NUM; y++) {
                            z = chip_info->hw_res->RX_NUM * x + y;
                            temp_delta = (raw_data[z * 2 + 1] << 8) | raw_data[z * 2];
                            sprintf(str_tmp, " %4d,", temp_delta);
                            strcat(str_gesture, str_tmp);
                    }
            }
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
            ksys_write(fd, str_gesture, strlen(str_gesture));
            ksys_write(fd, "\n", 1);
#else
            sys_write(fd, str_gesture, strlen(str_gesture));
            sys_write(fd, "\n", 1);
#endif
            TPD_INFO("str_gesture:%s\n", str_gesture);
        }
    //    if (fd >= 0) {
    //        sys_close(fd);
    //        set_fs(old_fs);
    //    }
        set_fs(old_fs);
        touch_i2c_write_byte(chip_info->client, 0xff, 0x00);        /* page 0*/
        msleep(30);
        kfree(raw_data);
}
#endif /*GESTURE_COORD_GET*/

static void synaptics_delta_read(struct seq_file *s, void *chip_data)
{
        int ret = 0, x = 0, y = 0, z = 0;
        int16_t temp_delta = 0;
        uint8_t *raw_data = NULL;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        if (!chip_info) {
                return;
        }

        raw_data = kzalloc(chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * 2 * (sizeof(uint8_t)), GFP_KERNEL);
        if (!raw_data) {
                        TPD_INFO("raw_data kzalloc error\n");
                        return;
        }

        /*disable irq when read data from IC*/
        synaptics_read_F54_base_reg(chip_info);

        /*TPD_DEBUG("\nstep 2:report type2 delta image\n");*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x02);/*select report type 0x02*/
        ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
        checkCMD(chip_info, 30);
        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+3,
                chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * 2, raw_data);         /*read data*/
        for (x = 0; x < chip_info->hw_res->TX_NUM; x++) {
                seq_printf(s, "\n[%2d]", x);
                for (y = 0; y < chip_info->hw_res->RX_NUM; y++) {
                        z = chip_info->hw_res->RX_NUM * x + y;
                        temp_delta = (raw_data[z * 2 + 1] << 8) | raw_data[z * 2];
                        seq_printf(s, "%4d, ", temp_delta);
                }
        }
        seq_printf(s, "\n");
        msleep(10);

        if (tp_debug != 0 && chip_info->health_zone.health_support == true) {
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0xC8);/*select report type 0xC8*/
                ret = touch_i2c_write_word(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE + 1, 0x00);/*set fifo 00*/
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/
                ret = checkCMD(chip_info, 5);
                if (ret < 0) {
                    TPD_INFO("%s: check cmd failed.\n", __func__);
                    goto OUT;
                }
                ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+3,
                        chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM * 2, raw_data);         /*read data*/
                for (x = 0; x < chip_info->hw_res->TX_NUM; x++) {
                        seq_printf(s, "\n[%2d]", x);
                        for (y = 0; y < chip_info->hw_res->RX_NUM; y++) {
                                z = chip_info->hw_res->RX_NUM * x + y;
                                temp_delta = (raw_data[z * 2 + 1] << 8) | raw_data[z * 2];
                                seq_printf(s, "%4d, ", temp_delta);
                        }
                }
                seq_printf(s, "\n");
        }

OUT:
        touch_i2c_write_byte(chip_info->client, 0xff, 0x00);        /* page 0*/
        msleep(60);
        kfree(raw_data);
}

static int s3706_reset_device(struct synaptics_rmi4_data *rmi4_data, bool rebuild)
{
        TPD_INFO("%s.\n", __func__);
        synaptics_resetgpio_set(g_chip_info->hw_res, false); /* reset gpio*/
        msleep(10);
        synaptics_resetgpio_set(g_chip_info->hw_res, true); /* reset gpio*/
        msleep(RESET_TO_NORMAL_TIME);

        return 0;
}

static int fwu_recovery_check_status(struct chip_data_s3706 *chip_info)
{
        int retval;
        unsigned char data_base;
        unsigned char status;

        data_base = chip_info->fwu->f35_fd.data_base_addr;

        retval = touch_i2c_read_block(chip_info->client,
                        data_base + F35_ERROR_CODE_OFFSET,
                        1,
                        &status);
        if (retval < 0) {
                TPD_INFO("%s: Failed to read status\n", __func__);
                return retval;
        }

        status = status & MASK_5BIT;

        if (status != 0x00) {
                TPD_INFO("%s: Recovery mode status = %d\n", __func__, status);
                return -EINVAL;
        }

        return 0;
}

static int fwu_scan_pdt(struct chip_data_s3706 *chip_info)
{
        int retval;
        unsigned char ii;
        unsigned char intr_count = 0;
        unsigned char intr_off;
        unsigned char intr_src;
        unsigned short addr;
        bool f01found = false;
        bool f34found = false;
        bool f35found = false;
        struct synaptics_rmi4_fn_desc rmi_fd;
        struct synaptics_rmi4_data *rmi4_data = chip_info->fwu->rmi4_data;

        chip_info->fwu->in_ub_mode = false;        /*in_ub_mode declare*/

        for (addr = PDT_START; addr > PDT_END; addr -= PDT_ENTRY_SIZE) {
                retval = touch_i2c_read_block(chip_info->client, addr, sizeof(rmi_fd), (unsigned char *)&rmi_fd);
                if (retval < 0) {
                        return retval;
                }

                if (rmi_fd.fn_number) {
                        TPD_DEBUG("%s: Found F%02x\n", __func__, rmi_fd.fn_number);
                        switch (rmi_fd.fn_number) {
                        case SYNAPTICS_RMI4_F01:        /*found*/
                                f01found = true;

                                rmi4_data->f01_query_base_addr =
                                                rmi_fd.query_base_addr;
                                rmi4_data->f01_ctrl_base_addr =
                                                rmi_fd.ctrl_base_addr;
                                rmi4_data->f01_data_base_addr =
                                                rmi_fd.data_base_addr;
                                rmi4_data->f01_cmd_base_addr =
                                                rmi_fd.cmd_base_addr;
                                break;
                        case SYNAPTICS_RMI4_F34:        /*found*/
                                f34found = true;
                                chip_info->fwu->f34_fd.query_base_addr =
                                                rmi_fd.query_base_addr;
                                chip_info->fwu->f34_fd.ctrl_base_addr =
                                                rmi_fd.ctrl_base_addr;
                                chip_info->fwu->f34_fd.data_base_addr =
                                                rmi_fd.data_base_addr;

                                TPD_DEBUG("fn_version is %d\n", rmi_fd.fn_version);
                                switch (rmi_fd.fn_version) {
                                case F34_V0:
                                        chip_info->fwu->bl_version = BL_V5;
                                        break;
                                case F34_V1:
                                        chip_info->fwu->bl_version = BL_V6;
                                        break;
                                case F34_V2:
                                        chip_info->fwu->bl_version = BL_V7;         /* why not direct set BL_V8*/
                                        break;
                                default:
                                        TPD_INFO("%s: Unrecognized F34 version\n", __func__);
                                        return -EINVAL;
                                }

                                chip_info->fwu->intr_mask = 0;
                                intr_src = rmi_fd.intr_src_count;
                                intr_off = intr_count % 8;
                                for (ii = intr_off; ii < (intr_src + intr_off); ii++) {
                                        chip_info->fwu->intr_mask |= 1 << ii;
                                }
                                break;
                        }
                } else {
                        break;
                }

                intr_count += rmi_fd.intr_src_count;
        }

        if (!f01found || !f34found) {
                TPD_INFO("%s: Failed to find both F01 and F34\n", __func__);
                if (!f35found) {
                        TPD_INFO("%s: Failed to find F35\n", __func__);
                        return -EINVAL;
                } else {
                        chip_info->fwu->in_ub_mode = true;
                        TPD_INFO("%s: In microbootloader mode\n",
                                        __func__);
                        fwu_recovery_check_status(chip_info);
                        return 0;
                }
        }

        rmi4_data->intr_mask[0] |= chip_info->fwu->intr_mask;           //Roland interrupts mode for write flash, like checkFlashState in 3508 notice

        addr = rmi4_data->f01_ctrl_base_addr + 1;

        retval = touch_i2c_write_block(chip_info->client,
                        addr,
                        sizeof(rmi4_data->intr_mask[0]),
                        &(rmi4_data->intr_mask[0]));
        if (retval < 0) {
                TPD_INFO("%s: Failed to set interrupt enable bit\n", __func__);
                return retval;
        }

        return 0;
}

static int fwu_write_f34_v7_partition_id(struct chip_data_s3706 *chip_info, unsigned char cmd)
{
        int retval;
        unsigned char data_base;
        unsigned char partition;

        data_base = chip_info->fwu->f34_fd.data_base_addr;

        switch (cmd) {
        case CMD_WRITE_FW:
                partition = CORE_CODE_PARTITION;
                break;
        case CMD_WRITE_CONFIG:
        case CMD_READ_CONFIG:
                if (chip_info->fwu->config_area == UI_CONFIG_AREA) {
                        partition = CORE_CONFIG_PARTITION;
                }
                else if (chip_info->fwu->config_area == DP_CONFIG_AREA) {
                        partition = DISPLAY_CONFIG_PARTITION;
                }
                else if (chip_info->fwu->config_area == PM_CONFIG_AREA) {
                        partition = GUEST_SERIALIZATION_PARTITION;
                }
                else if (chip_info->fwu->config_area == BL_CONFIG_AREA) {
                        partition = GLOBAL_PARAMETERS_PARTITION;
                }
                else if (chip_info->fwu->config_area == FLASH_CONFIG_AREA) {
                        partition = FLASH_CONFIG_PARTITION;
                }
                else if (chip_info->fwu->config_area == UPP_AREA) {
                        partition = UTILITY_PARAMETER_PARTITION;
                }
                break;
        case CMD_WRITE_LOCKDOWN:
                partition = DEVICE_CONFIG_PARTITION;
                break;
        case CMD_WRITE_GUEST_CODE:
                partition = GUEST_CODE_PARTITION;
                break;
        case CMD_WRITE_BOOTLOADER:
                partition = BOOTLOADER_PARTITION;
                break;
        case CMD_WRITE_UTILITY_PARAM:
                partition = UTILITY_PARAMETER_PARTITION;
                break;
        case CMD_ERASE_ALL:
                partition = CORE_CODE_PARTITION;
                break;
        case CMD_ERASE_BL_CONFIG:
                partition = GLOBAL_PARAMETERS_PARTITION;
                break;
        case CMD_ERASE_UI_CONFIG:
                partition = CORE_CONFIG_PARTITION;
                break;
        case CMD_ERASE_DISP_CONFIG:
                partition = DISPLAY_CONFIG_PARTITION;
                break;
        case CMD_ERASE_FLASH_CONFIG:
                partition = FLASH_CONFIG_PARTITION;
                break;
        case CMD_ERASE_GUEST_CODE:
                partition = GUEST_CODE_PARTITION;
                break;
        case CMD_ERASE_BOOTLOADER:
                partition = BOOTLOADER_PARTITION;
                break;
        case CMD_ENABLE_FLASH_PROG:
                partition = BOOTLOADER_PARTITION;
                break;
        default:
                TPD_INFO("%s: Invalid command 0x%02x\n", __func__, cmd);
                return -EINVAL;
        }

        retval = touch_i2c_write_block(chip_info->client,
                        data_base + chip_info->fwu->off.partition_id,
                        sizeof(partition),
                        &partition);
        if (retval < 0) {
                TPD_INFO("%s: Failed to write partition ID\n", __func__);
                return retval;
        }

        return 0;
}

static int fwu_write_f34_partition_id(struct chip_data_s3706 *chip_info, unsigned char cmd)
{
        int retval = -1;

        if (chip_info->fwu->bl_version == BL_V7 || chip_info->fwu->bl_version == BL_V8) {
                retval = fwu_write_f34_v7_partition_id(chip_info, cmd);
        }

        return retval;
}

static int fwu_read_flash_status(struct chip_data_s3706 *chip_info)
{
        int retval;
        unsigned char status;
        unsigned char command;

        retval = touch_i2c_read_block(chip_info->client,
                        chip_info->fwu->f34_fd.data_base_addr + chip_info->fwu->off.flash_status,
                        sizeof(status),
                        &status);              //get ic state , idle or other
        if (retval < 0) {
                TPD_INFO("%s: Failed to read flash status\n", __func__);
                return retval;
        }

        chip_info->fwu->in_bl_mode = status >> 7;

        /*fwu->bl_version is 8*/
        if (chip_info->fwu->bl_version == BL_V5) {
                chip_info->fwu->flash_status = (status >> 4) & MASK_3BIT;
        } else if (chip_info->fwu->bl_version == BL_V6) {
                chip_info->fwu->flash_status = status & MASK_3BIT;
        } else if (chip_info->fwu->bl_version == BL_V7 || chip_info->fwu->bl_version == BL_V8) {
                chip_info->fwu->flash_status = status & MASK_5BIT;
        }

        if (chip_info->fwu->write_bootloader) {
                chip_info->fwu->flash_status = 0x00;
        }

        if (chip_info->fwu->flash_status != 0x00) {
                TPD_INFO("%s: Flash status = %d, command = 0x%02x\n",
                                __func__, chip_info->fwu->flash_status, chip_info->fwu->command);
        }

        if (chip_info->fwu->bl_version == BL_V7 || chip_info->fwu->bl_version == BL_V8) {
                if (chip_info->fwu->flash_status == 0x08) {
                        chip_info->fwu->flash_status = 0x00;
                }
        }

        retval = touch_i2c_read_block(chip_info->client,
                        chip_info->fwu->f34_fd.data_base_addr + chip_info->fwu->off.flash_cmd,
                        sizeof(command),
                        &command);      //read recent command
        if (retval < 0) {
                TPD_INFO("%s: Failed to read flash command\n",
                                __func__);
                return retval;
        }

        if (chip_info->fwu->bl_version == BL_V5) {
                chip_info->fwu->command = command & MASK_4BIT;
        } else if (chip_info->fwu->bl_version == BL_V6) {
                chip_info->fwu->command = command & MASK_6BIT;
        } else if (chip_info->fwu->bl_version == BL_V7 || chip_info->fwu->bl_version == BL_V8) {
                chip_info->fwu->command = command;
        }

        if (chip_info->fwu->write_bootloader) {
                chip_info->fwu->command = 0x00;
        }

        return 0;
}

static int fwu_wait_for_idle(struct chip_data_s3706 *chip_info, int timeout_ms, bool poll)
{
        int count = 0;
        int timeout_count = ((timeout_ms * 1000) / MAX_SLEEP_TIME_US) + 1;

        do {
                usleep_range(MIN_SLEEP_TIME_US, MAX_SLEEP_TIME_US);

                count++;
                if (poll || (count == timeout_count)) {
                        fwu_read_flash_status(chip_info);
                }

                if ((chip_info->fwu->command == CMD_IDLE) && (chip_info->fwu->flash_status == 0x00)) {
                        return 0;
                }
        } while (count < timeout_count);

        TPD_INFO("%s: Timed out waiting for idle status\n", __func__);

        return -ETIMEDOUT;
}

static int fwu_write_f34_v7_command_single_transaction(struct chip_data_s3706 *chip_info, unsigned char cmd)
{
        int retval;
        unsigned char data_base;
        struct f34_v7_data_1_5 data_1_5;

        data_base = chip_info->fwu->f34_fd.data_base_addr;

        memset(data_1_5.data, 0x00, sizeof(data_1_5.data));

        switch (cmd) {
        case CMD_ERASE_ALL:
                data_1_5.partition_id = CORE_CODE_PARTITION;
                data_1_5.command = CMD_V7_ERASE_AP;
                break;
        case CMD_ERASE_UI_FIRMWARE:
                data_1_5.partition_id = CORE_CODE_PARTITION;
                data_1_5.command = CMD_V7_ERASE;
                break;
        case CMD_ERASE_BL_CONFIG:
                data_1_5.partition_id = GLOBAL_PARAMETERS_PARTITION;
                data_1_5.command = CMD_V7_ERASE;
                break;
        case CMD_ERASE_UI_CONFIG:
                data_1_5.partition_id = CORE_CONFIG_PARTITION;
                data_1_5.command = CMD_V7_ERASE;
                break;
        case CMD_ERASE_DISP_CONFIG:
                data_1_5.partition_id = DISPLAY_CONFIG_PARTITION;
                data_1_5.command = CMD_V7_ERASE;
                break;
        case CMD_ERASE_FLASH_CONFIG:
                data_1_5.partition_id = FLASH_CONFIG_PARTITION;
                data_1_5.command = CMD_V7_ERASE;
                break;
        case CMD_ERASE_GUEST_CODE:
                data_1_5.partition_id = GUEST_CODE_PARTITION;
                data_1_5.command = CMD_V7_ERASE;
                break;
        case CMD_ERASE_BOOTLOADER:
                data_1_5.partition_id = BOOTLOADER_PARTITION;
                data_1_5.command = CMD_V7_ERASE;
                break;
        case CMD_ERASE_UTILITY_PARAMETER:
                data_1_5.partition_id = UTILITY_PARAMETER_PARTITION;
                data_1_5.command = CMD_V7_ERASE;
                break;
        case CMD_ENABLE_FLASH_PROG:
                data_1_5.partition_id = BOOTLOADER_PARTITION;
                data_1_5.command = CMD_V7_ENTER_BL;
                break;
        }

        data_1_5.payload_0 = chip_info->fwu->bootloader_id[0];
        data_1_5.payload_1 = chip_info->fwu->bootloader_id[1];

        retval = touch_i2c_write_block(chip_info->client,
                        data_base + chip_info->fwu->off.partition_id,
                        sizeof(data_1_5.data),
                        data_1_5.data);
        if (retval < 0) {
                TPD_INFO("%s: Failed to write single transaction command\n", __func__);
                return retval;
        }

        return 0;
}

static int fwu_write_f34_v7_command(struct chip_data_s3706 *chip_info, unsigned char cmd)
{
        int retval;
        unsigned char data_base;
        unsigned char command;

        data_base = chip_info->fwu->f34_fd.data_base_addr;

        switch (cmd) {
        case CMD_WRITE_FW:
        case CMD_WRITE_CONFIG:
        case CMD_WRITE_LOCKDOWN:
        case CMD_WRITE_GUEST_CODE:
        case CMD_WRITE_BOOTLOADER:
        case CMD_WRITE_UTILITY_PARAM:
                command = CMD_V7_WRITE;
                break;
        case CMD_READ_CONFIG:
                command = CMD_V7_READ;
                break;
        case CMD_ERASE_ALL:
                command = CMD_V7_ERASE_AP;
                break;
        case CMD_ERASE_UI_FIRMWARE:
        case CMD_ERASE_BL_CONFIG:
        case CMD_ERASE_UI_CONFIG:
        case CMD_ERASE_DISP_CONFIG:
        case CMD_ERASE_FLASH_CONFIG:
        case CMD_ERASE_GUEST_CODE:
        case CMD_ERASE_BOOTLOADER:
        case CMD_ERASE_UTILITY_PARAMETER:
                command = CMD_V7_ERASE;
                break;
        case CMD_ENABLE_FLASH_PROG:
                command = CMD_V7_ENTER_BL;
                break;
        default:
                TPD_INFO("%s: Invalid command 0x%02x\n", __func__, cmd);
                return -EINVAL;
        }

        chip_info->fwu->command = command;

        switch (cmd) {
        case CMD_ERASE_ALL:
        case CMD_ERASE_UI_FIRMWARE:
        case CMD_ERASE_BL_CONFIG:
        case CMD_ERASE_UI_CONFIG:
        case CMD_ERASE_DISP_CONFIG:
        case CMD_ERASE_FLASH_CONFIG:
        case CMD_ERASE_GUEST_CODE:
        case CMD_ERASE_BOOTLOADER:
        case CMD_ERASE_UTILITY_PARAMETER:
        case CMD_ENABLE_FLASH_PROG:
                retval = fwu_write_f34_v7_command_single_transaction(chip_info, cmd);
                if (retval < 0) {
                        return retval;
                } else {
                        return 0;
                }
        default:
                break;
        }

        retval = touch_i2c_write_block(chip_info->client,
                        data_base + chip_info->fwu->off.flash_cmd,
                        sizeof(command),
                        &command);
        if (retval < 0) {
                TPD_INFO("%s: Failed to write flash command\n", __func__);
                return retval;
        }

        return 0;
}

static int fwu_write_f34_command(struct chip_data_s3706 *chip_info, unsigned char cmd)
{
        int retval = -1;

        if (chip_info->fwu->bl_version == BL_V7 || chip_info->fwu->bl_version == BL_V8) {
                retval = fwu_write_f34_v7_command(chip_info, cmd);
        }

        return retval;
}

static int fwu_read_f34_v7_partition_table(struct chip_data_s3706 *chip_info, unsigned char *partition_table)
{
        int retval;
        unsigned char data_base;
        unsigned char length[2];
        unsigned short block_number = 0;

        data_base = chip_info->fwu->f34_fd.data_base_addr;

        chip_info->fwu->config_area = FLASH_CONFIG_AREA;

        retval = fwu_write_f34_partition_id(chip_info, CMD_READ_CONFIG);
        if (retval < 0) {
                return retval;
        }

        retval = touch_i2c_write_block(chip_info->client,
                        data_base + chip_info->fwu->off.block_number,
                        sizeof(block_number),
                        (unsigned char *)&block_number);
        if (retval < 0) {
                TPD_INFO("%s: Failed to write block number\n", __func__);
                return retval;
        }

        length[0] = (unsigned char)(chip_info->fwu->flash_config_length & MASK_8BIT);
        length[1] = (unsigned char)(chip_info->fwu->flash_config_length >> 8);

        retval = touch_i2c_write_block(chip_info->client,
                        data_base + chip_info->fwu->off.transfer_length,
                        sizeof(length),
                        length);
        if (retval < 0) {
                TPD_INFO("%s: Failed to write transfer length\n", __func__);
                return retval;
        }

        retval = fwu_write_f34_command(chip_info, CMD_READ_CONFIG);
        if (retval < 0) {
                TPD_INFO("%s: Failed to write command\n", __func__);
                return retval;
        }

        msleep(READ_CONFIG_WAIT_MS);

        retval = fwu_wait_for_idle(chip_info, WRITE_WAIT_MS, true);
        if (retval < 0) {
                TPD_INFO("%s: Failed to wait for idle status\n", __func__);
                return retval;
        }

        retval = touch_i2c_read_block(chip_info->client,
                        data_base + chip_info->fwu->off.payload,
                        chip_info->fwu->partition_table_bytes,
                        partition_table);
        if (retval < 0) {
                TPD_INFO("%s: Failed to read block data\n", __func__);
                return retval;
        }

        return 0;
}

static void fwu_parse_partition_table(struct chip_data_s3706 *chip_info, const unsigned char *partition_table,
                struct block_count *blkcount, struct physical_address *phyaddr)
{
        unsigned char ii;
        unsigned char index;
        unsigned char offset;
        unsigned short partition_length;
        unsigned short physical_address;
        struct partition_table *ptable = NULL;


        for (ii = 0; ii < chip_info->fwu->partitions; ii++) {
                index = ii * 8 + 2;
                ptable = (struct partition_table *)&partition_table[index];
                partition_length = ptable->partition_length_15_8 << 8 |
                                ptable->partition_length_7_0;
                physical_address = ptable->start_physical_address_15_8 << 8 |
                                ptable->start_physical_address_7_0;
                TPD_DEBUG("%s: Partition entry %d:\n", __func__, ii);
                for (offset = 0; offset < 8; offset++) {
                        TPD_DEBUG("%s: 0x%02x\n", __func__, partition_table[index + offset]);
                }
                switch (ptable->partition_id) {
                case CORE_CODE_PARTITION:           /*ii = 4*/
                        blkcount->ui_firmware = partition_length;
                        phyaddr->ui_firmware = physical_address;
                        TPD_DEBUG("%s: Core code block count: %d\n",
                                        __func__, blkcount->ui_firmware);
                        blkcount->total_count += partition_length;
                        break;
                case CORE_CONFIG_PARTITION:         /*ii = 5*/
                        blkcount->ui_config = partition_length;
                        phyaddr->ui_config = physical_address;
                        TPD_DEBUG("%s: Core config block count: %d\n",
                                        __func__, blkcount->ui_config);
                        blkcount->total_count += partition_length;
                        break;
                case BOOTLOADER_PARTITION:          /*ii = 0*/
                        blkcount->bl_image = partition_length;
                        phyaddr->bl_image = physical_address;
                        TPD_DEBUG("%s: Bootloader block count: %d\n",
                                        __func__, blkcount->bl_image);
                        blkcount->total_count += partition_length;
                        break;
                case UTILITY_PARAMETER_PARTITION:
                        blkcount->utility_param = partition_length;
                        phyaddr->utility_param = physical_address;
                        TPD_DEBUG("%s: Utility parameter block count: %d\n",
                                        __func__, blkcount->utility_param);
                        blkcount->total_count += partition_length;
                        break;
                case DISPLAY_CONFIG_PARTITION:
                        blkcount->dp_config = partition_length;
                        phyaddr->dp_config = physical_address;
                        TPD_DEBUG("%s: Display config block count: %d\n",
                                        __func__, blkcount->dp_config);
                        blkcount->total_count += partition_length;
                        break;
                case FLASH_CONFIG_PARTITION:                /*ii = 3*/
                        blkcount->fl_config = partition_length;
                        phyaddr->fl_config = physical_address;
                        TPD_DEBUG("%s: Flash config block count: %d\n",
                                        __func__, blkcount->fl_config);
                        blkcount->total_count += partition_length;
                        break;
                case GUEST_CODE_PARTITION:
                        blkcount->guest_code = partition_length;
                        phyaddr->guest_code = physical_address;
                        TPD_DEBUG("%s: Guest code block count: %d\n",
                                        __func__, blkcount->guest_code);
                        blkcount->total_count += partition_length;
                        break;
                case GUEST_SERIALIZATION_PARTITION:         /*ii = 6*/
                        blkcount->pm_config = partition_length;
                        phyaddr->pm_config = physical_address;
                        TPD_DEBUG("%s: Guest serialization block count: %d\n",
                                        __func__, blkcount->pm_config);
                        blkcount->total_count += partition_length;
                        break;
                case GLOBAL_PARAMETERS_PARTITION:           /*ii = 2*/
                        blkcount->bl_config = partition_length;
                        phyaddr->bl_config = physical_address;
                        TPD_DEBUG("%s: Global parameters block count: %d\n",
                                        __func__, blkcount->bl_config);
                        blkcount->total_count += partition_length;
                        break;
                case DEVICE_CONFIG_PARTITION:           /*ii = 1*/
                        blkcount->lockdown = partition_length;
                        phyaddr->lockdown = physical_address;
                        TPD_DEBUG("%s: Device config block count: %d\n",
                                        __func__, blkcount->lockdown);
                        blkcount->total_count += partition_length;
                        break;
                }
        }

        return;
}

static int fwu_read_f34_v7_queries(struct chip_data_s3706 *chip_info)
{
        int retval;
        unsigned char ii;
        unsigned char query_base;
        unsigned char index;
        unsigned char offset;
        unsigned char *ptable;
        struct f34_v7_query_0 query_0;
        struct f34_v7_query_1_7 query_1_7;

        query_base = chip_info->fwu->f34_fd.query_base_addr;

        retval = touch_i2c_read_block(chip_info->client,
                        query_base,
                        sizeof(query_0.data),
                        query_0.data);
        if (retval < 0) {
                TPD_INFO("%s: Failed to read query 0\n", __func__);
                return retval;
        }

        offset = query_0.subpacket_1_size + 1;

        retval = touch_i2c_read_block(chip_info->client,
                        query_base + offset,
                        sizeof(query_1_7.data),
                        query_1_7.data);
        if (retval < 0) {
                TPD_INFO("%s: Failed to read queries 1 to 7\n", __func__);
                return retval;
        }

        chip_info->fwu->bootloader_id[0] = query_1_7.bl_minor_revision;
        chip_info->fwu->bootloader_id[1] = query_1_7.bl_major_revision;

        if (chip_info->fwu->bootloader_id[1] == BL_V8) {
                chip_info->fwu->bl_version = BL_V8;
        }

        chip_info->fwu->block_size = query_1_7.block_size_15_8 << 8 |
                        query_1_7.block_size_7_0;

        chip_info->fwu->flash_config_length = query_1_7.flash_config_length_15_8 << 8 |
                        query_1_7.flash_config_length_7_0;

        chip_info->fwu->payload_length = query_1_7.payload_length_15_8 << 8 |
                        query_1_7.payload_length_7_0;

        chip_info->fwu->off.flash_status = V7_FLASH_STATUS_OFFSET;
        chip_info->fwu->off.partition_id = V7_PARTITION_ID_OFFSET;
        chip_info->fwu->off.block_number = V7_BLOCK_NUMBER_OFFSET;
        chip_info->fwu->off.transfer_length = V7_TRANSFER_LENGTH_OFFSET;
        chip_info->fwu->off.flash_cmd = V7_COMMAND_OFFSET;
        chip_info->fwu->off.payload = V7_PAYLOAD_OFFSET;

        index = sizeof(query_1_7.data) - V7_PARTITION_SUPPORT_BYTES;

        chip_info->fwu->partitions = 0;
        for (offset = 0; offset < V7_PARTITION_SUPPORT_BYTES; offset++) {       //count partition nums
                for (ii = 0; ii < 8; ii++) {
                        if (query_1_7.data[index + offset] & (1 << ii)) {
                                chip_info->fwu->partitions++;
                        }
                }

                TPD_DEBUG("%s: Supported partitions: 0x%02x\n",
                                __func__, query_1_7.data[index + offset]);
        }

        chip_info->fwu->partition_table_bytes = chip_info->fwu->partitions * 8 + 2;

        ptable = kzalloc(chip_info->fwu->partition_table_bytes, GFP_KERNEL);
        if (!ptable) {
                TPD_INFO("%s: Failed to alloc mem for partition table\n", __func__);
                return -ENOMEM;
        }

        retval = fwu_read_f34_v7_partition_table(chip_info, ptable);            //partition table
        if (retval < 0) {
                TPD_INFO("%s: Failed to read partition table\n", __func__);
                kfree(ptable);
                return retval;
        }

        fwu_parse_partition_table(chip_info, ptable, &chip_info->fwu->blkcount, &chip_info->fwu->phyaddr);      //firmware in IC condition

        if (chip_info->fwu->blkcount.dp_config) {
                chip_info->fwu->flash_properties.has_disp_config = 1;
        } else {
                chip_info->fwu->flash_properties.has_disp_config = 0;
        }

        if (chip_info->fwu->blkcount.pm_config) {
                chip_info->fwu->flash_properties.has_pm_config = 1;
        } else {
                chip_info->fwu->flash_properties.has_pm_config = 0;
        }

        if (chip_info->fwu->blkcount.bl_config) {
                chip_info->fwu->flash_properties.has_bl_config = 1;
        } else {
                chip_info->fwu->flash_properties.has_bl_config = 0;
        }

        if (chip_info->fwu->blkcount.guest_code) {
                chip_info->fwu->has_guest_code = 1;
        } else {
                chip_info->fwu->has_guest_code = 0;
        }

        if (chip_info->fwu->blkcount.utility_param) {
                chip_info->fwu->has_utility_param = 1;
        } else {
                chip_info->fwu->has_utility_param = 0;
        }

        kfree(ptable);

        return 0;
}

static int fwu_read_f34_queries(struct chip_data_s3706 *chip_info)
{
        int retval = -1;

        memset(&chip_info->fwu->blkcount, 0x00, sizeof(chip_info->fwu->blkcount));
        memset(&chip_info->fwu->phyaddr, 0x00, sizeof(chip_info->fwu->phyaddr));

        if (chip_info->fwu->bl_version == BL_V7) {
                retval = fwu_read_f34_v7_queries(chip_info); /*enter*/
        }

        return retval;
}

static int fwu_get_device_config_id(struct chip_data_s3706 *chip_info)
{
        int retval;
        unsigned char config_id_size = 0;

        if (chip_info->fwu->bl_version == BL_V7 || chip_info->fwu->bl_version == BL_V8) {
                config_id_size = V7_CONFIG_ID_SIZE;
        }

        retval = touch_i2c_read_block(chip_info->client,
                                chip_info->fwu->f34_fd.ctrl_base_addr,
                                config_id_size,
                                chip_info->fwu->config_id);
        if (retval < 0) {
                return retval;
        }

        return 0;
}

static int synaptics_rmi4_fwu_init(struct chip_data_s3706 *chip_info, bool force)
{
        int retval;
        struct pdt_properties pdt_props;
        unsigned char build_id[3] = {0};

        if (!chip_info) {
                TPD_INFO("chip_info is NULL, return!\n");
                return -1;
        }

        if (chip_info->fwu) {
                TPD_INFO("%s: Handle already exists\n", __func__);
                return -1;
        }

        chip_info->fwu = kzalloc(sizeof(struct synaptics_rmi4_fwu_handle), GFP_KERNEL);
        if (!chip_info->fwu) {
                TPD_INFO("%s: Failed to alloc mem for fwu\n", __func__);
                retval = -ENOMEM;
                goto exit;
        }

        retval = touch_i2c_read_block(chip_info->client, PDT_PROPS, sizeof(pdt_props.data), pdt_props.data);
        if (retval < 0) {
                TPD_INFO("%s: Failed to read PDT properties, assuming 0x00\n", __func__);
        } else if (pdt_props.has_bsr) {
                TPD_INFO("%s: Reflash for LTS not currently supported\n", __func__);
                retval = -ENODEV;
                goto exit_free_mem;
        }

        chip_info->fwu->rmi4_data = kzalloc(sizeof(struct synaptics_rmi4_data), GFP_KERNEL);
        if (!chip_info->fwu->rmi4_data) {
                TPD_INFO("%s: Failed to alloc mem for fwu\n", __func__);
                retval = -ENOMEM;
                goto exit_free_rmi;
        }

        chip_info->fwu->rmi4_data->reset_device = s3706_reset_device;                /*no need*/
        //change in resume and suspend, notice if suspend hold mutex before the fw update queue work
        chip_info->fwu->rmi4_data->sensor_sleep = 0;   /*don't enter sensor sleep or suspend while update*/

        retval = fwu_scan_pdt(chip_info);

        retval = touch_i2c_read_block(chip_info->client,
                chip_info->fwu->rmi4_data->f01_query_base_addr + 18,
                sizeof(build_id),
                build_id);
        if (retval < 0) {
                goto exit_free_rmi;
        }

        chip_info->fwu->rmi4_data->firmware_id = (unsigned int)build_id[0] +
                        (unsigned int)build_id[1] * 0x100 +
                        (unsigned int)build_id[2] * 0x10000;

        if (retval < 0) {
                goto exit_free_mem;
        }

        if (!chip_info->fwu->in_ub_mode) {
                retval = fwu_read_f34_queries(chip_info);
                if (retval < 0) {
                        goto exit_free_mem;
                }

                retval = fwu_get_device_config_id(chip_info);
                if (retval < 0) {
                        TPD_INFO("%s: Failed to read device config ID\n", __func__);
                        goto exit_free_mem;
                }
        }

        chip_info->fwu->force_update = force;
        chip_info->fwu->do_lockdown = DO_LOCKDOWN;

        return 0;

exit_free_rmi:
        kfree(chip_info->fwu->rmi4_data);
        chip_info->fwu->rmi4_data = NULL;

exit_free_mem:
        kfree(chip_info->fwu);
        chip_info->fwu = NULL;
exit:
        return retval;
}

static unsigned int le_to_uint(const unsigned char *ptr)
{
        return (unsigned int)ptr[0] +
                        (unsigned int)ptr[1] * 0x100 +
                        (unsigned int)ptr[2] * 0x10000 +
                        (unsigned int)ptr[3] * 0x1000000;
}

static void fwu_parse_image_header_10_bootloader(struct chip_data_s3706 *chip_info, const unsigned char *image)
{
        unsigned char ii;
        unsigned char num_of_containers;
        unsigned int addr;
        unsigned int container_id;
        unsigned int length;
        const unsigned char *content;
        struct container_descriptor *descriptor;

        num_of_containers = (chip_info->fwu->img.bootloader.size - 4) / 4;

        for (ii = 1; ii <= num_of_containers; ii++) {
                addr = le_to_uint(chip_info->fwu->img.bootloader.data + (ii * 4));
                descriptor = (struct container_descriptor *)(image + addr);
                container_id = descriptor->container_id[0] |
                                descriptor->container_id[1] << 8;
                content = image + le_to_uint(descriptor->content_address);
                length = le_to_uint(descriptor->content_length);
                switch (container_id) {
                case BL_IMAGE_CONTAINER:
                        chip_info->fwu->img.bl_image.data = content;
                        chip_info->fwu->img.bl_image.size = length;
                        break;
                case BL_CONFIG_CONTAINER:
                case GLOBAL_PARAMETERS_CONTAINER:
                        chip_info->fwu->img.bl_config.data = content;
                        chip_info->fwu->img.bl_config.size = length;
                        break;
                case BL_LOCKDOWN_INFO_CONTAINER:
                case DEVICE_CONFIG_CONTAINER:
                        chip_info->fwu->img.lockdown.data = content;
                        chip_info->fwu->img.lockdown.size = length;
                        break;
                default:
                        break;
                }
        }

        return;
}

static void fwu_parse_image_header_10_utility(struct chip_data_s3706 *chip_info, const unsigned char *image)
{
        unsigned char ii;
        unsigned char num_of_containers;
        unsigned int addr;
        unsigned int container_id;
        unsigned int length;
        const unsigned char *content;
        struct container_descriptor *descriptor;

        num_of_containers = chip_info->fwu->img.utility.size / 4;

        for (ii = 0; ii < num_of_containers; ii++) {
                if (ii >= MAX_UTILITY_PARAMS) {
                        continue;
                }
                addr = le_to_uint(chip_info->fwu->img.utility.data + (ii * 4));
                descriptor = (struct container_descriptor *)(image + addr);
                container_id = descriptor->container_id[0] |
                                descriptor->container_id[1] << 8;
                content = image + le_to_uint(descriptor->content_address);
                length = le_to_uint(descriptor->content_length);
                switch (container_id) {
                case UTILITY_PARAMETER_CONTAINER:
                        chip_info->fwu->img.utility_param[ii].data = content;
                        chip_info->fwu->img.utility_param[ii].size = length;
                        chip_info->fwu->img.utility_param_id[ii] = content[0];
                        break;
                default:
                        break;
                }
        }

        return;
}


static void fwu_parse_image_header_10(struct chip_data_s3706 *chip_info)
{
        unsigned char ii;
        unsigned char num_of_containers;
        unsigned int addr;
        unsigned int offset;
        unsigned int container_id;
        unsigned int length;
        const unsigned char *image;
        const unsigned char *content;
        struct container_descriptor *descriptor;
        struct image_header_10 *header;

        image = chip_info->fwu->image;
        header = (struct image_header_10 *)image;

        chip_info->fwu->img.checksum = le_to_uint(header->checksum);

        /* address of top level container */
        offset = le_to_uint(header->top_level_container_start_addr);
        descriptor = (struct container_descriptor *)(image + offset);

        /* address of top level container content */
        offset = le_to_uint(descriptor->content_address);
        num_of_containers = le_to_uint(descriptor->content_length) / 4;

        for (ii = 0; ii < num_of_containers; ii++) {
                addr = le_to_uint(image + offset);
                offset += 4;
                descriptor = (struct container_descriptor *)(image + addr);
                container_id = descriptor->container_id[0] |
                                descriptor->container_id[1] << 8;
                content = image + le_to_uint(descriptor->content_address);
                length = le_to_uint(descriptor->content_length);
                switch (container_id) {
                case UI_CONTAINER:
                case CORE_CODE_CONTAINER:
                        chip_info->fwu->img.ui_firmware.data = content;
                        chip_info->fwu->img.ui_firmware.size = length;
                        break;
                case UI_CONFIG_CONTAINER:
                case CORE_CONFIG_CONTAINER:
                        chip_info->fwu->img.ui_config.data = content;
                        chip_info->fwu->img.ui_config.size = length;
                        break;
                case BL_CONTAINER:
                        chip_info->fwu->img.bl_version = *content;
                        chip_info->fwu->img.bootloader.data = content;
                        chip_info->fwu->img.bootloader.size = length;
                        fwu_parse_image_header_10_bootloader(chip_info, image);
                        break;
                case UTILITY_CONTAINER:
                        chip_info->fwu->img.utility.data = content;
                        chip_info->fwu->img.utility.size = length;
                        fwu_parse_image_header_10_utility(chip_info, image);
                        break;
                case GUEST_CODE_CONTAINER:
                        chip_info->fwu->img.contains_guest_code = true;
                        chip_info->fwu->img.guest_code.data = content;
                        chip_info->fwu->img.guest_code.size = length;
                        break;
                case DISPLAY_CONFIG_CONTAINER:
                        chip_info->fwu->img.contains_disp_config = true;
                        chip_info->fwu->img.dp_config.data = content;
                        chip_info->fwu->img.dp_config.size = length;
                        break;
                case PERMANENT_CONFIG_CONTAINER:
                case GUEST_SERIALIZATION_CONTAINER:
                        chip_info->fwu->img.contains_perm_config = true;
                        chip_info->fwu->img.pm_config.data = content;
                        chip_info->fwu->img.pm_config.size = length;
                        break;
                case FLASH_CONFIG_CONTAINER:
                        chip_info->fwu->img.contains_flash_config = true;
                        chip_info->fwu->img.fl_config.data = content;
                        chip_info->fwu->img.fl_config.size = length;
                        break;
                case GENERAL_INFORMATION_CONTAINER:
                        chip_info->fwu->img.contains_firmware_id = true;
                        chip_info->fwu->img.firmware_id = le_to_uint(content + 4);
                        break;
                default:
                        break;
                }
        }

        return;
}

static void fwu_compare_partition_tables(struct chip_data_s3706 *chip_info)
{
        chip_info->fwu->incompatible_partition_tables = false;

        if (chip_info->fwu->phyaddr.bl_image != chip_info->fwu->img.phyaddr.bl_image) {
                chip_info->fwu->incompatible_partition_tables = true;
        } else if (chip_info->fwu->phyaddr.lockdown != chip_info->fwu->img.phyaddr.lockdown) {
                chip_info->fwu->incompatible_partition_tables = true;
        } else if (chip_info->fwu->phyaddr.bl_config != chip_info->fwu->img.phyaddr.bl_config) {
                chip_info->fwu->incompatible_partition_tables = true;
        } else if (chip_info->fwu->phyaddr.utility_param != chip_info->fwu->img.phyaddr.utility_param) {
                chip_info->fwu->incompatible_partition_tables = true;
        }

        if (chip_info->fwu->bl_version == BL_V7) {
                if (chip_info->fwu->phyaddr.fl_config != chip_info->fwu->img.phyaddr.fl_config) {
                        chip_info->fwu->incompatible_partition_tables = true;
                }
        }

        chip_info->fwu->new_partition_table = false;

        if (chip_info->fwu->phyaddr.ui_firmware != chip_info->fwu->img.phyaddr.ui_firmware) {
                chip_info->fwu->new_partition_table = true;
        } else if (chip_info->fwu->phyaddr.ui_config != chip_info->fwu->img.phyaddr.ui_config) {
                chip_info->fwu->new_partition_table = true;
        }

        if (chip_info->fwu->flash_properties.has_disp_config) {
                if (chip_info->fwu->phyaddr.dp_config != chip_info->fwu->img.phyaddr.dp_config) {
                        chip_info->fwu->new_partition_table = true;
                }
        }

        if (chip_info->fwu->has_guest_code) {
                if (chip_info->fwu->phyaddr.guest_code != chip_info->fwu->img.phyaddr.guest_code) {
                        chip_info->fwu->new_partition_table = true;
                }
        }

        return;
}

static int fwu_parse_image_info(struct chip_data_s3706 *chip_info)
{
        struct image_header_10 *header;

        header = (struct image_header_10 *)chip_info->fwu->image;

        memset(&chip_info->fwu->img, 0x00, sizeof(chip_info->fwu->img));

        switch (header->major_header_version) {         /*header->major_header_version is 0x10*/
        case IMAGE_HEADER_VERSION_10:
                fwu_parse_image_header_10(chip_info);
                break;
        default:
                TPD_INFO("%s: Unsupported image file format (0x%02x)\n",
                                __func__, header->major_header_version);
                return -EINVAL;
        }

        if (chip_info->fwu->bl_version == BL_V7 || chip_info->fwu->bl_version == BL_V8) {
                if (!chip_info->fwu->img.contains_flash_config) {
                        TPD_INFO("%s: No flash config found in firmware image\n",
                                        __func__);
                        return -EINVAL;
                }

                fwu_parse_partition_table(chip_info, chip_info->fwu->img.fl_config.data,
                                &chip_info->fwu->img.blkcount, &chip_info->fwu->img.phyaddr);           //firmware img in emmc

                if (chip_info->fwu->img.blkcount.utility_param) {
                        chip_info->fwu->img.contains_utility_param = true;
                }

                fwu_compare_partition_tables(chip_info);        //compare two firmware img between ic and emmc, compare addr , if not equal, return or reflash partition
        } else {
                chip_info->fwu->new_partition_table = false;
                chip_info->fwu->incompatible_partition_tables = false;
        }

        return 0;
}

static int fwu_get_image_firmware_id(struct chip_data_s3706 *chip_info, unsigned int *fw_id)
{
        int retval;
        unsigned char index = 0;
        char *strptr;
        char *firmware_id;

        if (chip_info->fwu->img.contains_firmware_id) {
                *fw_id = chip_info->fwu->img.firmware_id;
        } else {
                strptr = strnstr(chip_info->fwu->image_name, "PR", MAX_IMAGE_NAME_LEN);
                if (!strptr) {
                        TPD_INFO("%s: No valid PR number (PRxxxxxxx) found in image file name (%s)\n",
                                        __func__, chip_info->fwu->image_name);
                        return -EINVAL;
                }

                strptr += 2;
                firmware_id = kzalloc(MAX_FIRMWARE_ID_LEN, GFP_KERNEL);
                if (!firmware_id) {
                        TPD_INFO("%s: Failed to alloc mem for firmware_id\n",
                                        __func__);
                        return -ENOMEM;
                }
                while (strptr[index] >= '0' && strptr[index] <= '9') {
                        firmware_id[index] = strptr[index];
                        index++;
                        if (index == MAX_FIRMWARE_ID_LEN - 1) {
                                break;
                        }
                }

                retval = SSTRTOUL(firmware_id, 10, (unsigned long *)fw_id);
                kfree(firmware_id);
                if (retval) {
                        TPD_INFO("%s: Failed to obtain image firmware ID\n", __func__);
                        return -EINVAL;
                }
        }

        return 0;
}

static enum flash_area fwu_go_nogo(struct chip_data_s3706 *chip_info)
{
        int retval;
        enum flash_area flash_area = NONE;
        unsigned char ii;
        unsigned char config_id_size = 0;
        unsigned int device_fw_id;
        unsigned int image_fw_id;
        struct synaptics_rmi4_data *rmi4_data = chip_info->fwu->rmi4_data;
        unsigned char tmp[3] = {0};
        unsigned char config_id_img[100] = {0};
        unsigned char config_id_panel[100] = {0};

        if (chip_info->fwu->force_update) {
                flash_area = UI_FIRMWARE;
                goto exit;
        }

        /* Update both UI and config if device is in bootloader mode */
        if (chip_info->fwu->bl_mode_device) {
                flash_area = UI_FIRMWARE;
                goto exit;
        }

        /* Get device firmware ID */
        device_fw_id = rmi4_data->firmware_id;          /*this is firmware id and control whether to update*/
        TPD_INFO("%s: Device firmware ID = %d\n", __func__, device_fw_id);

        /* Get image firmware ID */
        retval = fwu_get_image_firmware_id(chip_info, &image_fw_id);   /*the firmware id in the img, if not equal device firmware id , update*/
        if (retval < 0) {
                flash_area = NONE;
                goto exit;
        }
        TPD_INFO("%s: Image firmware ID = %d device_fw_id = %d\n", __func__, image_fw_id, device_fw_id);

        if (image_fw_id != device_fw_id) {
                flash_area = UI_FIRMWARE;           /*UI_FIRMWARE means to update firmware and config, no need to judge config*/
                goto exit;
        }

        /* Get device config ID */
        retval = fwu_get_device_config_id(chip_info);        /*get config id*/
        if (retval < 0) {
                TPD_INFO("%s: Failed to read device config ID\n", __func__);
                flash_area = NONE;
                goto exit;
        }

        if (chip_info->fwu->bl_version == BL_V7 || chip_info->fwu->bl_version == BL_V8) {
                config_id_size = V7_CONFIG_ID_SIZE;
        }

        for (ii = 0; ii < config_id_size; ii++) {
                sprintf(tmp, "%02x", (unsigned char)chip_info->fwu->img.ui_config.data[ii]);
                memcpy(&config_id_img[ii * 2], tmp, 2);
                sprintf(tmp, "%02x", (unsigned char)chip_info->fwu->config_id[ii]);
                memcpy(&config_id_panel[ii * 2], tmp, 2);
                if (chip_info->fwu->img.ui_config.data[ii] != chip_info->fwu->config_id[ii]) {
                        flash_area = UI_CONFIG;
                        goto exit;
                }
        }

        flash_area = NONE;

exit:
        TPD_INFO("config_id_img is %s\n", config_id_img);
        TPD_INFO("config_id_panel is %s\n", config_id_panel);
        if (flash_area == NONE) {
                TPD_INFO("%s: No need to do reflash\n", __func__);
        } else {
                TPD_INFO("%s: Updating %s\n", __func__,
                                flash_area == UI_FIRMWARE ?
                                "UI firmware and config" :
                                "UI config only");
        }

        return flash_area;
}

static int fwu_enter_flash_prog(struct chip_data_s3706 *chip_info)
{
        int retval;
        struct f01_device_control f01_device_control;

        retval = fwu_read_flash_status(chip_info);
        if (retval < 0) {
                return retval;
        }

        if (chip_info->fwu->in_bl_mode) {
                return 0;
        }

        msleep(INT_DISABLE_WAIT_MS);

        retval = fwu_write_f34_command(chip_info, CMD_ENABLE_FLASH_PROG);       //enter flash program mode
        if (retval < 0) {
                return retval;
        }

        retval = fwu_wait_for_idle(chip_info, ENABLE_WAIT_MS, false);   //wait for complete
        if (retval < 0) {
                return retval;
        }

        if (!chip_info->fwu->in_bl_mode) {
                TPD_INFO("%s: BL mode not entered\n", __func__);
                return -EINVAL;
        }

        retval = fwu_scan_pdt(chip_info);       //rescan in bootloader mode
        if (retval < 0) {
                return retval;
        }

        retval = fwu_read_f34_queries(chip_info);
        if (retval < 0) {
                return retval;
        }

        retval = touch_i2c_read_block(chip_info->client,
                        chip_info->fwu->rmi4_data->f01_ctrl_base_addr,
                        sizeof(f01_device_control.data),
                        f01_device_control.data);
        if (retval < 0) {
                TPD_INFO("%s: Failed to read F01 device control\n", __func__);
                return retval;
        }

        f01_device_control.nosleep = true;
        f01_device_control.sleep_mode = SLEEP_MODE_NORMAL;

        retval = touch_i2c_write_block(chip_info->client,
                        chip_info->fwu->rmi4_data->f01_ctrl_base_addr,
                        sizeof(f01_device_control.data),
                        f01_device_control.data);               //set no sleep mode
        if (retval < 0) {
                TPD_INFO("%s: Failed to write F01 device control\n", __func__);
                return retval;
        }

        msleep(ENTER_FLASH_PROG_WAIT_MS);

        return retval;
}

static int fwu_write_f34_v7_blocks(struct chip_data_s3706 *chip_info, unsigned char *block_ptr,
                unsigned short block_cnt, unsigned char command)
{
        int retval = -1;
        unsigned char data_base;
        unsigned char length[2];
        unsigned short transfer;
        unsigned short remaining = block_cnt;
        unsigned short block_number = 0;
        unsigned short left_bytes;
        unsigned short write_size;
        unsigned short max_write_size;

        data_base = chip_info->fwu->f34_fd.data_base_addr;

        retval = fwu_write_f34_partition_id(chip_info, command);
        if (retval < 0) {
                return retval;
        }

        retval = touch_i2c_write_block(chip_info->client,
                        data_base + chip_info->fwu->off.block_number,
                        sizeof(block_number),
                        (unsigned char *)&block_number);
        if (retval < 0) {
                TPD_INFO("%s: Failed to write block number\n", __func__);
                return retval;
        }

        do {
                if (remaining / chip_info->fwu->payload_length) {
                        transfer = chip_info->fwu->payload_length;
                } else {
                        transfer = remaining;
                }

                length[0] = (unsigned char)(transfer & MASK_8BIT);
                length[1] = (unsigned char)(transfer >> 8);

                retval = touch_i2c_write_block(chip_info->client,
                                data_base + chip_info->fwu->off.transfer_length,
                                sizeof(length),
                                length);
                if (retval < 0) {
                        TPD_INFO("%s: Failed to write transfer length (remaining = %d)\n", __func__, remaining);
                        return retval;
                }

                retval = fwu_write_f34_command(chip_info, command);
                if (retval < 0) {
                        TPD_INFO("%s: Failed to write command (remaining = %d)\n", __func__, remaining);
                        return retval;
                }

#ifdef MAX_WRITE_SIZE
                max_write_size = MAX_WRITE_SIZE;
                if (max_write_size >= transfer * chip_info->fwu->block_size) {
                        max_write_size = transfer * chip_info->fwu->block_size;
                } else if (max_write_size > chip_info->fwu->block_size) {
                        max_write_size -= max_write_size % chip_info->fwu->block_size;
                } else {
                        max_write_size = chip_info->fwu->block_size;
                }
#else
                max_write_size = transfer * chip_info->fwu->block_size;
#endif
                left_bytes = transfer * chip_info->fwu->block_size;

                do {
                        if (left_bytes / max_write_size) {
                                write_size = max_write_size;
                        } else {
                                write_size = left_bytes;
                        }

                        retval = touch_i2c_write_block(chip_info->client,
                                        data_base + chip_info->fwu->off.payload,
                                        write_size,
                                        block_ptr);
                        if (retval < 0) {
                                TPD_INFO("%s: Failed to write block data (remaining = %d)\n",
                                                __func__, remaining);
                                return retval;
                        }

                        block_ptr += write_size;
                        left_bytes -= write_size;
                } while (left_bytes);

                retval = fwu_wait_for_idle(chip_info, WRITE_WAIT_MS, true);
                if (retval < 0) {
                        TPD_INFO("%s: Failed to wait for idle status (remaining = %d)\n",
                                        __func__, remaining);
                        return retval;
                }

                remaining -= transfer;
        } while (remaining);

        return 0;
}

static int fwu_write_f34_blocks(struct chip_data_s3706 *chip_info, unsigned char *block_ptr,
                unsigned short block_cnt, unsigned char cmd)
{
        int retval = 0;

        if (chip_info->fwu->bl_version == BL_V7 || chip_info->fwu->bl_version == BL_V8) {
                retval = fwu_write_f34_v7_blocks(chip_info, block_ptr, block_cnt, cmd);
        } else {
                //retval = fwu_write_f34_v5v6_blocks(chip_info, block_ptr, block_cnt, cmd);     Roland
        }

        return retval;
}

static int fwu_write_lockdown(struct chip_data_s3706 *chip_info)
{
        unsigned short lockdown_block_count;

        lockdown_block_count = chip_info->fwu->img.lockdown.size / chip_info->fwu->block_size;

        return fwu_write_f34_blocks(chip_info, (unsigned char *)chip_info->fwu->img.lockdown.data,
                        lockdown_block_count, CMD_WRITE_LOCKDOWN);
}

static int fwu_do_lockdown_v7(struct chip_data_s3706 *chip_info)
{
        int retval;
        struct f34_v7_data0 status;

        retval = fwu_enter_flash_prog(chip_info);
        if (retval < 0)
                return retval;

        retval = touch_i2c_read_block(chip_info->client,
                        chip_info->fwu->f34_fd.data_base_addr + chip_info->fwu->off.flash_status,
                        sizeof(status.data),
                        status.data);
        if (retval < 0) {
                TPD_INFO("%s: Failed to read flash status\n", __func__);
                return retval;
        }

        if (status.device_cfg_status == 2) {
                TPD_INFO("%s: Device already locked down\n", __func__);
                return 0;
        }

        retval = fwu_write_lockdown(chip_info);
        if (retval < 0)
                return retval;

        TPD_INFO("%s: Lockdown programmed\n", __func__);

        return retval;
}

static int fwu_check_ui_configuration_size(struct chip_data_s3706 *chip_info)
{
        unsigned short block_count;

        block_count = chip_info->fwu->img.ui_config.size / chip_info->fwu->block_size;

        if (block_count != chip_info->fwu->blkcount.ui_config) {
                TPD_INFO("%s: UI configuration size mismatch\n", __func__);
                return -EINVAL;
        }

        return 0;
}

static int fwu_erase_configuration(struct chip_data_s3706 *chip_info)
{
        int retval;

        switch (chip_info->fwu->config_area) {
        case UI_CONFIG_AREA:
                retval = fwu_write_f34_command(chip_info, CMD_ERASE_UI_CONFIG);
                if (retval < 0)
                        return retval;
                break;
        case DP_CONFIG_AREA:
                retval = fwu_write_f34_command(chip_info, CMD_ERASE_DISP_CONFIG);
                if (retval < 0)
                        return retval;
                break;
        case BL_CONFIG_AREA:
                retval = fwu_write_f34_command(chip_info, CMD_ERASE_BL_CONFIG);
                if (retval < 0)
                        return retval;
                break;
        case FLASH_CONFIG_AREA:
                retval = fwu_write_f34_command(chip_info, CMD_ERASE_FLASH_CONFIG);
                if (retval < 0)
                        return retval;
                break;
        case UPP_AREA:
                retval = fwu_write_f34_command(chip_info, CMD_ERASE_UTILITY_PARAMETER);
                if (retval < 0)
                        return retval;
                break;
        default:
                TPD_INFO("%s: Invalid config area\n", __func__);
                return -EINVAL;
        }

        TPD_INFO("%s: Erase command written\n", __func__);

        retval = fwu_wait_for_idle(chip_info, ERASE_WAIT_MS, false);
        if (retval < 0)
                return retval;

        TPD_INFO("%s: Idle status detected\n", __func__);

        return retval;
}

static int fwu_write_configuration(struct chip_data_s3706 *chip_info)
{
        return fwu_write_f34_blocks(chip_info, (unsigned char *)chip_info->fwu->config_data,
                        chip_info->fwu->config_block_count, CMD_WRITE_CONFIG);
}

static int fwu_write_ui_configuration(struct chip_data_s3706 *chip_info)
{
        chip_info->fwu->config_area = UI_CONFIG_AREA;
        chip_info->fwu->config_data = chip_info->fwu->img.ui_config.data;
        chip_info->fwu->config_size = chip_info->fwu->img.ui_config.size;
        chip_info->fwu->config_block_count = chip_info->fwu->config_size / chip_info->fwu->block_size;

        return fwu_write_configuration(chip_info);
}

static int fwu_check_ui_firmware_size(struct chip_data_s3706 *chip_info)
{
        unsigned short block_count;

        block_count = chip_info->fwu->img.ui_firmware.size / chip_info->fwu->block_size;

        if (block_count != chip_info->fwu->blkcount.ui_firmware) {
                TPD_INFO("%s: UI firmware size mismatch\n", __func__);
                return -EINVAL;
        }

        return 0;
}

static int fwu_check_dp_configuration_size(struct chip_data_s3706 *chip_info)
{
        unsigned short block_count;

        block_count = chip_info->fwu->img.dp_config.size / chip_info->fwu->block_size;

        if (block_count != chip_info->fwu->blkcount.dp_config) {
                TPD_INFO("%s: Display configuration size mismatch\n", __func__);
                return -EINVAL;
        }

        return 0;
}

static int fwu_check_guest_code_size(struct chip_data_s3706 *chip_info)
{
        unsigned short block_count;

        block_count = chip_info->fwu->img.guest_code.size / chip_info->fwu->block_size;
        if (block_count != chip_info->fwu->blkcount.guest_code) {
                TPD_INFO("%s: Guest code size mismatch\n", __func__);
                return -EINVAL;
        }

        return 0;
}

static int fwu_check_bl_configuration_size(struct chip_data_s3706 *chip_info)
{
        unsigned short block_count;

        block_count = chip_info->fwu->img.bl_config.size / chip_info->fwu->block_size;

        if (block_count != chip_info->fwu->blkcount.bl_config) {
                TPD_INFO("%s: Bootloader configuration size mismatch\n", __func__);
                return -EINVAL;
        }

        return 0;
}

static int fwu_erase_guest_code(struct chip_data_s3706 *chip_info)
{
        int retval;

        retval = fwu_write_f34_command(chip_info, CMD_ERASE_GUEST_CODE);
        if (retval < 0)
                return retval;

        TPD_INFO("%s: Erase command written\n", __func__);

        retval = fwu_wait_for_idle(chip_info, ERASE_WAIT_MS, false);
        if (retval < 0)
                return retval;

        TPD_INFO("%s: Idle status detected\n", __func__);

        return 0;
}

static int fwu_erase_all(struct chip_data_s3706 *chip_info)
{
        int retval;

        if (chip_info->fwu->bl_version == BL_V7) {
                retval = fwu_write_f34_command(chip_info, CMD_ERASE_UI_FIRMWARE);
                if (retval < 0)
                        return retval;

                TPD_INFO("%s: Erase command written\n", __func__);

                retval = fwu_wait_for_idle(chip_info, ERASE_WAIT_MS, false);
                if (retval < 0)
                        return retval;

                TPD_INFO("%s: Idle status detected\n", __func__);

                chip_info->fwu->config_area = UI_CONFIG_AREA;
                retval = fwu_erase_configuration(chip_info);
                if (retval < 0)
                        return retval;
        } else {
                retval = fwu_write_f34_command(chip_info, CMD_ERASE_ALL);
                if (retval < 0)
                        return retval;

                TPD_INFO("%s: Erase all command written\n", __func__);

                retval = fwu_wait_for_idle(chip_info, ERASE_WAIT_MS, false);
                if (!(chip_info->fwu->bl_version == BL_V8 &&
                                chip_info->fwu->flash_status == BAD_PARTITION_TABLE)) {
                        if (retval < 0)
                                return retval;
                }

                TPD_INFO("%s: Idle status detected\n", __func__);

                if (chip_info->fwu->bl_version == BL_V8)
                        return 0;
        }

        if (chip_info->fwu->flash_properties.has_disp_config) {
                chip_info->fwu->config_area = DP_CONFIG_AREA;
                retval = fwu_erase_configuration(chip_info);
                if (retval < 0)
                        return retval;
        }

        if (chip_info->fwu->has_guest_code) {
                retval = fwu_erase_guest_code(chip_info);
                if (retval < 0)
                        return retval;
        }

        return 0;
}

static int fwu_erase_bootloader(struct chip_data_s3706 *chip_info)
{
        int retval;

        retval = fwu_write_f34_command(chip_info, CMD_ERASE_BOOTLOADER);
        if (retval < 0)
                return retval;

        TPD_INFO("%s: Erase command written\n", __func__);

        retval = fwu_wait_for_idle(chip_info, ERASE_WAIT_MS, false);
        if (retval < 0)
                return retval;

        TPD_INFO("%s: Idle status detected\n", __func__);

        return 0;
}

static int fwu_write_bootloader(struct chip_data_s3706 *chip_info)
{
        int retval;
        unsigned short bootloader_block_count;

        bootloader_block_count = chip_info->fwu->img.bl_image.size / chip_info->fwu->block_size;

        chip_info->fwu->write_bootloader = true;
        retval = fwu_write_f34_blocks(chip_info, (unsigned char *)chip_info->fwu->img.bl_image.data,
                        bootloader_block_count, CMD_WRITE_BOOTLOADER);
        chip_info->fwu->write_bootloader = false;

        return retval;
}

static int fwu_allocate_read_config_buf(struct chip_data_s3706 *chip_info, unsigned int count)
{
        if (count > chip_info->fwu->read_config_buf_size) {
                kfree(chip_info->fwu->read_config_buf);
                chip_info->fwu->read_config_buf = kzalloc(count, GFP_KERNEL);
                if (!chip_info->fwu->read_config_buf) {
                        TPD_INFO("%s: Failed to alloc mem for fwu->read_config_buf\n",
                                        __func__);
                        chip_info->fwu->read_config_buf_size = 0;
                        return -ENOMEM;
                }
                chip_info->fwu->read_config_buf_size = count;
        }

        return 0;
}

static void calculate_checksum(unsigned short *data, unsigned long len,
                unsigned long *result)
{
        unsigned long temp;
        unsigned long sum1 = 0xffff;
        unsigned long sum2 = 0xffff;

        *result = 0xffffffff;

        while (len--) {
                temp = *data;
                sum1 += temp;
                sum2 += sum1;
                sum1 = (sum1 & 0xffff) + (sum1 >> 16);
                sum2 = (sum2 & 0xffff) + (sum2 >> 16);
                data++;
        }

        *result = sum2 << 16 | sum1;

        return;
}

static void convert_to_little_endian(unsigned char *dest, unsigned long src)
{
        dest[0] = (unsigned char)(src & 0xff);
        dest[1] = (unsigned char)((src >> 8) & 0xff);
        dest[2] = (unsigned char)((src >> 16) & 0xff);
        dest[3] = (unsigned char)((src >> 24) & 0xff);

        return;
}

static int fwu_write_utility_parameter(struct chip_data_s3706 *chip_info)
{
        int retval;
        unsigned char ii;
        unsigned char checksum_array[4];
        unsigned char *pbuf;
        unsigned short remaining_size;
        unsigned short utility_param_size;
        unsigned long checksum;

        utility_param_size = chip_info->fwu->blkcount.utility_param * chip_info->fwu->block_size;
        retval = fwu_allocate_read_config_buf(chip_info, utility_param_size);
        if (retval < 0)
                return retval;
        memset(chip_info->fwu->read_config_buf, 0x00, utility_param_size);

        pbuf = chip_info->fwu->read_config_buf;
        remaining_size = utility_param_size - 4;

        for (ii = 0; ii < MAX_UTILITY_PARAMS; ii++) {
                if (chip_info->fwu->img.utility_param_id[ii] == UNUSED)
                        continue;

#ifdef F51_DISCRETE_FORCE   /*no need*/
                if (fwu->img.utility_param_id[ii] == FORCE_PARAMETER) {
                        if (fwu->bl_mode_device) {
                                dev_info_3706(rmi4_data->pdev->dev.parent,
                                                "%s: Device in bootloader mode, skipping calibration data restoration\n",
                                                __func__);
                                goto image_param;
                        }
                        retval = secure_memcpy(&(pbuf[4]),
                                        remaining_size - 4,
                                        fwu->cal_data,
                                        fwu->cal_data_buf_size,
                                        fwu->cal_data_size);
                        if (retval < 0) {
                                dev_err_3706(rmi4_data->pdev->dev.parent,
                                                "%s: Failed to copy force calibration data\n",
                                                __func__);
                                return retval;
                        }
                        pbuf[0] = FORCE_PARAMETER;
                        pbuf[1] = 0x00;
                        pbuf[2] = (4 + fwu->cal_data_size) / 2;
                        pbuf += (fwu->cal_data_size + 4);
                        remaining_size -= (fwu->cal_data_size + 4);
                        continue;
                }
image_param:
#endif

                retval = secure_memcpy(pbuf,
                                remaining_size,
                                chip_info->fwu->img.utility_param[ii].data,
                                chip_info->fwu->img.utility_param[ii].size,
                                chip_info->fwu->img.utility_param[ii].size);
                if (retval < 0) {
                        TPD_INFO("%s: Failed to copy utility parameter data\n", __func__);
                        return retval;
                }
                pbuf += chip_info->fwu->img.utility_param[ii].size;
                remaining_size -= chip_info->fwu->img.utility_param[ii].size;
        }

        calculate_checksum((unsigned short *)chip_info->fwu->read_config_buf,
                        ((utility_param_size - 4) / 2),
                        &checksum);

        convert_to_little_endian(checksum_array, checksum);

        chip_info->fwu->read_config_buf[utility_param_size - 4] = checksum_array[0];
        chip_info->fwu->read_config_buf[utility_param_size - 3] = checksum_array[1];
        chip_info->fwu->read_config_buf[utility_param_size - 2] = checksum_array[2];
        chip_info->fwu->read_config_buf[utility_param_size - 1] = checksum_array[3];

        retval = fwu_write_f34_blocks(chip_info, (unsigned char *)chip_info->fwu->read_config_buf,
                        chip_info->fwu->blkcount.utility_param, CMD_WRITE_UTILITY_PARAM);
        if (retval < 0)
                return retval;

        return 0;
}

static int fwu_write_bl_area_v7(struct chip_data_s3706 *chip_info)
{
        int retval;
        bool has_utility_param;
        struct synaptics_rmi4_data *rmi4_data = chip_info->fwu->rmi4_data;

        has_utility_param = chip_info->fwu->has_utility_param;

        if (chip_info->fwu->has_utility_param) {
                chip_info->fwu->config_area = UPP_AREA;
                retval = fwu_erase_configuration(chip_info);
                if (retval < 0)
                        return retval;
        }

        chip_info->fwu->config_area = BL_CONFIG_AREA;
        retval = fwu_erase_configuration(chip_info);
        if (retval < 0)
                return retval;

        chip_info->fwu->config_area = FLASH_CONFIG_AREA;
        retval = fwu_erase_configuration(chip_info);
        if (retval < 0)
                return retval;

        retval = fwu_erase_bootloader(chip_info);
        if (retval < 0)
                return retval;

        retval = fwu_write_bootloader(chip_info);
        if (retval < 0)
                return retval;

        /*msleep(rmi4_data->hw_if->board_data->reset_delay_ms);*/
        msleep(200);
        chip_info->fwu->rmi4_data->reset_device(rmi4_data, false);

        chip_info->fwu->config_area = FLASH_CONFIG_AREA;
        chip_info->fwu->config_data = chip_info->fwu->img.fl_config.data;
        chip_info->fwu->config_size = chip_info->fwu->img.fl_config.size;
        chip_info->fwu->config_block_count = chip_info->fwu->config_size / chip_info->fwu->block_size;
        retval = fwu_write_configuration(chip_info);
        if (retval < 0)
                return retval;
        chip_info->fwu->rmi4_data->reset_device(rmi4_data, false);

        chip_info->fwu->config_area = BL_CONFIG_AREA;
        chip_info->fwu->config_data = chip_info->fwu->img.bl_config.data;
        chip_info->fwu->config_size = chip_info->fwu->img.bl_config.size;
        chip_info->fwu->config_block_count = chip_info->fwu->config_size / chip_info->fwu->block_size;
        retval = fwu_write_configuration(chip_info);
        if (retval < 0)
                return retval;

        if (chip_info->fwu->img.contains_utility_param) {
                retval = fwu_write_utility_parameter(chip_info);
                if (retval < 0)
                        return retval;
        }

        return 0;
}

static int fwu_read_f34_v7_blocks(struct chip_data_s3706 *chip_info, unsigned short block_cnt,
                unsigned char command)
{
        int retval;
        unsigned char data_base;
        unsigned char length[2];
        unsigned short transfer;
        unsigned short remaining = block_cnt;
        unsigned short block_number = 0;
        unsigned short index = 0;

        data_base = chip_info->fwu->f34_fd.data_base_addr;

        retval = fwu_write_f34_partition_id(chip_info, command);
        if (retval < 0)
                return retval;

        retval = touch_i2c_write_block(chip_info->client,
                        data_base + chip_info->fwu->off.block_number,
                        sizeof(block_number),
                        (unsigned char *)&block_number);
        if (retval < 0) {
                TPD_INFO("%s: Failed to write block number\n", __func__);
                return retval;
        }

        do {
                if (remaining / chip_info->fwu->payload_length)
                        transfer = chip_info->fwu->payload_length;
                else
                        transfer = remaining;

                length[0] = (unsigned char)(transfer & MASK_8BIT);
                length[1] = (unsigned char)(transfer >> 8);

                retval = touch_i2c_write_block(chip_info->client,
                                data_base + chip_info->fwu->off.transfer_length,
                                sizeof(length),
                                length);
                if (retval < 0) {
                        TPD_INFO("%s: Failed to write transfer length (remaining = %d)\n",
                                        __func__, remaining);
                        return retval;
                }

                retval = fwu_write_f34_command(chip_info, command);
                if (retval < 0) {
                        TPD_INFO("%s: Failed to write command (remaining = %d)\n",
                                        __func__, remaining);
                        return retval;
                }

                retval = fwu_wait_for_idle(chip_info, WRITE_WAIT_MS, true);
                if (retval < 0) {
                        TPD_INFO("%s: Failed to wait for idle status (remaining = %d)\n",
                                        __func__, remaining);
                        return retval;
                }

                retval = touch_i2c_read_block(chip_info->client,
                                data_base + chip_info->fwu->off.payload,
                                transfer * chip_info->fwu->block_size,
                                &chip_info->fwu->read_config_buf[index]);
                if (retval < 0) {
                        TPD_INFO("%s: Failed to read block data (remaining = %d)\n",
                                        __func__, remaining);
                        return retval;
                }

                index += (transfer * chip_info->fwu->block_size);
                remaining -= transfer;
        } while (remaining);

        return 0;
}

static int fwu_read_f34_blocks(struct chip_data_s3706 *chip_info, unsigned short block_cnt, unsigned char cmd)
{
        int retval = 0;

        if (chip_info->fwu->bl_version == BL_V7 || chip_info->fwu->bl_version == BL_V8)
                retval = fwu_read_f34_v7_blocks(chip_info, block_cnt, cmd);
        return retval;
}

static int fwu_write_flash_configuration(struct chip_data_s3706 *chip_info)
{
        int retval;
        struct synaptics_rmi4_data *rmi4_data = chip_info->fwu->rmi4_data;

        chip_info->fwu->config_area = FLASH_CONFIG_AREA;
        chip_info->fwu->config_data = chip_info->fwu->img.fl_config.data;
        chip_info->fwu->config_size = chip_info->fwu->img.fl_config.size;
        chip_info->fwu->config_block_count = chip_info->fwu->config_size / chip_info->fwu->block_size;

        if (chip_info->fwu->config_block_count != chip_info->fwu->blkcount.fl_config) {
                TPD_INFO("%s: Flash configuration size mismatch\n", __func__);
                return -EINVAL;
        }

        retval = fwu_erase_configuration(chip_info);
        if (retval < 0)
                return retval;

        retval = fwu_write_configuration(chip_info);
        if (retval < 0)
                return retval;

        chip_info->fwu->rmi4_data->reset_device(rmi4_data, false);

        return 0;
}

static int fwu_write_partition_table_v7(struct chip_data_s3706 *chip_info)
{
        int retval;
        unsigned short block_count;

        block_count = chip_info->fwu->blkcount.bl_config;
        chip_info->fwu->config_area = BL_CONFIG_AREA;
        chip_info->fwu->config_size = chip_info->fwu->block_size * block_count;

        retval = fwu_allocate_read_config_buf(chip_info, chip_info->fwu->config_size);
        if (retval < 0)
                return retval;

        retval = fwu_read_f34_blocks(chip_info, block_count, CMD_READ_CONFIG);
        if (retval < 0)
                return retval;

        retval = fwu_erase_configuration(chip_info);
        if (retval < 0)
                return retval;

        retval = fwu_write_flash_configuration(chip_info);
        if (retval < 0)
                return retval;

        chip_info->fwu->config_area = BL_CONFIG_AREA;
        chip_info->fwu->config_data = chip_info->fwu->read_config_buf;
        chip_info->fwu->config_size = chip_info->fwu->img.bl_config.size;
        chip_info->fwu->config_block_count = chip_info->fwu->config_size / chip_info->fwu->block_size;

        retval = fwu_write_configuration(chip_info);
        if (retval < 0)
                return retval;

        return 0;
}

static int fwu_write_partition_table_v8(struct chip_data_s3706 *chip_info)
{
        int retval;
        struct synaptics_rmi4_data *rmi4_data = chip_info->fwu->rmi4_data;

        chip_info->fwu->config_area = FLASH_CONFIG_AREA;
        chip_info->fwu->config_data = chip_info->fwu->img.fl_config.data;
        chip_info->fwu->config_size = chip_info->fwu->img.fl_config.size;
        chip_info->fwu->config_block_count = chip_info->fwu->config_size / chip_info->fwu->block_size;

        if (chip_info->fwu->config_block_count != chip_info->fwu->blkcount.fl_config) {
                TPD_INFO("%s: Flash configuration size mismatch\n", __func__);
                return -EINVAL;
        }

        retval = fwu_write_configuration(chip_info);
        if (retval < 0)
                return retval;

        chip_info->fwu->rmi4_data->reset_device(rmi4_data, false);
        retval = fwu_enter_flash_prog(chip_info);
        TPD_INFO("%s: call re-enter flash_prog mode\n", __func__);
        if (retval < 0) {
                TPD_INFO("%s: re-enter flash_prog mode error\n", __func__);
        }

        return 0;
}

static int fwu_write_dp_configuration(struct chip_data_s3706 *chip_info)
{
        chip_info->fwu->config_area = DP_CONFIG_AREA;
        chip_info->fwu->config_data = chip_info->fwu->img.dp_config.data;
        chip_info->fwu->config_size = chip_info->fwu->img.dp_config.size;
        chip_info->fwu->config_block_count = chip_info->fwu->config_size / chip_info->fwu->block_size;

        return fwu_write_configuration(chip_info);
}

static int fwu_write_guest_code(struct chip_data_s3706 *chip_info)
{
        int retval;
        unsigned short guest_code_block_count;

        guest_code_block_count = chip_info->fwu->img.guest_code.size / chip_info->fwu->block_size;

        retval = fwu_write_f34_blocks(chip_info, (unsigned char *)chip_info->fwu->img.guest_code.data,
                        guest_code_block_count, CMD_WRITE_GUEST_CODE);
        if (retval < 0)
                return retval;

        return 0;
}

static int fwu_write_firmware(struct chip_data_s3706 *chip_info)
{
        unsigned short firmware_block_count;

        firmware_block_count = chip_info->fwu->img.ui_firmware.size / chip_info->fwu->block_size;

        return fwu_write_f34_blocks(chip_info, (unsigned char *)chip_info->fwu->img.ui_firmware.data,
                        firmware_block_count, CMD_WRITE_FW);
}

static int fwu_do_reflash(struct chip_data_s3706 *chip_info)
{
        int retval;
        bool do_bl_update = false;

        if (!chip_info->fwu->new_partition_table) {
                retval = fwu_check_ui_firmware_size(chip_info); //check firmware block nums , a block is 16bytes
                if (retval < 0)
                        return retval;

                retval = fwu_check_ui_configuration_size(chip_info);
                if (retval < 0)
                        return retval;

                if (chip_info->fwu->flash_properties.has_disp_config &&
                                chip_info->fwu->img.contains_disp_config) {
                        retval = fwu_check_dp_configuration_size(chip_info);    //display
                        if (retval < 0)
                                return retval;
                }

                if (chip_info->fwu->has_guest_code && chip_info->fwu->img.contains_guest_code) {
                        retval = fwu_check_guest_code_size(chip_info);
                        if (retval < 0)
                                return retval;
                }
        } else if (chip_info->fwu->bl_version == BL_V7) {
                retval = fwu_check_bl_configuration_size(chip_info);    //bootloader size
                if (retval < 0)
                        return retval;
        }

        if (!chip_info->fwu->has_utility_param && chip_info->fwu->img.contains_utility_param) {
                if (chip_info->fwu->bl_version == BL_V7 || chip_info->fwu->bl_version == BL_V8)
                        do_bl_update = true;
        }

        if (chip_info->fwu->has_utility_param && !chip_info->fwu->img.contains_utility_param) {
                if (chip_info->fwu->bl_version == BL_V7 || chip_info->fwu->bl_version == BL_V8)
                        do_bl_update = true;
        }

        if (!do_bl_update && chip_info->fwu->incompatible_partition_tables) {
                TPD_INFO("%s: Incompatible partition tables\n", __func__);
                return -EINVAL;
        } else if (!do_bl_update && chip_info->fwu->new_partition_table) {
                if (!chip_info->fwu->force_update) {
                        TPD_INFO("%s: Partition table mismatch\n", __func__);
                        return -EINVAL;
                }
        }

        retval = fwu_erase_all(chip_info);
        if (retval < 0)
                return retval;

        if (do_bl_update) {
                retval = fwu_write_bl_area_v7(chip_info);
                if (retval < 0)
                        return retval;
                TPD_INFO("%s: Bootloader area programmed\n", __func__);
        } else if (chip_info->fwu->bl_version == BL_V7 && chip_info->fwu->new_partition_table) {
                retval = fwu_write_partition_table_v7(chip_info);       //reflash partition table
                if (retval < 0)
                        return retval;
                TPD_INFO("%s: Partition table programmed\n", __func__);
        } else if (chip_info->fwu->bl_version == BL_V8) {
                retval = fwu_write_partition_table_v8(chip_info);       //partition table is earsed in fwu_erase_all, rewrite FLASH_CONFIG_AREA partition
                if (retval < 0)
                        return retval;
                TPD_INFO("%s: Partition table programmed\n", __func__);
        }

        chip_info->fwu->config_area = UI_CONFIG_AREA;
        if (chip_info->fwu->flash_properties.has_disp_config &&
                        chip_info->fwu->img.contains_disp_config) {
                retval = fwu_write_dp_configuration(chip_info);
                if (retval < 0)
                        return retval;
                TPD_INFO("%s: Display configuration programmed\n", __func__);
        }

        retval = fwu_write_ui_configuration(chip_info);         //core configuration
        if (retval < 0)
                return retval;
        TPD_INFO("%s: Configuration programmed\n", __func__);

        if (chip_info->fwu->has_guest_code && chip_info->fwu->img.contains_guest_code) {
                retval = fwu_write_guest_code(chip_info);
                if (retval < 0)
                        return retval;
                TPD_INFO("%s: Guest code programmed\n", __func__);
        }

        retval = fwu_write_firmware(chip_info);         //core code
        if (retval < 0)
                return retval;
        TPD_INFO("%s: Firmware programmed\n", __func__);

        return retval;
}

static int fwu_start_reflash(struct chip_data_s3706 *chip_info)
{
        int retval = 0;
        enum flash_area flash_area;
        bool do_rebuild = false;
        struct synaptics_rmi4_data *rmi4_data = chip_info->fwu->rmi4_data;

        if (rmi4_data->sensor_sleep) {
                TPD_INFO("%s: Sensor sleeping\n", __func__);
                return -ENODEV;
        }
        rmi4_data->stay_awake = true;   /*no need*/
        TPD_INFO("%s: Start of reflash process\n", __func__);

        retval = fwu_parse_image_info(chip_info);
        if (retval < 0)
                goto exit;

        if (chip_info->fwu->blkcount.total_count != chip_info->fwu->img.blkcount.total_count) {
                TPD_INFO("%s: Flash size mismatch\n", __func__);
                retval = -EINVAL;
                goto exit;
        }

        if (chip_info->fwu->bl_version != chip_info->fwu->img.bl_version) {
                TPD_INFO("%s: Bootloader version mismatch\n", __func__);
                retval = -EINVAL;
                goto exit;
        }

        retval = fwu_read_flash_status(chip_info);
        if (retval < 0)
                goto exit;

        if (chip_info->fwu->in_bl_mode) {
                chip_info->fwu->bl_mode_device = true;
                TPD_INFO("%s: Device in bootloader mode\n", __func__);
        } else {
                chip_info->fwu->bl_mode_device = false;
        }

        flash_area = fwu_go_nogo(chip_info);         /*judge whether to update*/

        if (flash_area != NONE) {
                retval = fwu_enter_flash_prog(chip_info);   /*lots of info, large function*///enter bootloader mode
                if (retval < 0) {
                        chip_info->fwu->rmi4_data->reset_device(rmi4_data, false);
                        goto exit;
                }
        }

        switch (flash_area) {
        case UI_FIRMWARE:
                do_rebuild = true;
                retval = fwu_do_reflash(chip_info);
                break;
        case UI_CONFIG:
                do_rebuild = true;
                retval = fwu_check_ui_configuration_size(chip_info);
                if (retval < 0)
                        break;
                chip_info->fwu->config_area = UI_CONFIG_AREA;   /*fwu_erase_configuration should set config_area first*/
                retval = fwu_erase_configuration(chip_info);
                if (retval < 0)
                        break;
                retval = fwu_write_ui_configuration(chip_info);
                break;
        case NONE:
        default:
                break;
        }

        if (chip_info->fwu->do_lockdown && (chip_info->fwu->img.lockdown.data != NULL)) {         //lockdown mode , only can write once, use for save i2c addr, pull up, pull down info
                TPD_INFO("Enter lockdown notice!!!!!!!!!!!!!!\n");
                switch (chip_info->fwu->bl_version) {
                case BL_V7:
                case BL_V8:
                        retval = fwu_do_lockdown_v7(chip_info);
                        if (retval < 0) {
                                TPD_INFO("%s: Failed to do lockdown\n", __func__);
                        }
                        chip_info->fwu->rmi4_data->reset_device(rmi4_data, false);
                        break;
                default:
                        break;
                }
        }

exit:
        if (do_rebuild)
                chip_info->fwu->rmi4_data->reset_device(rmi4_data, true);
        TPD_INFO("%s: End of reflash process\n", __func__);
        rmi4_data->stay_awake = false;  /*no need*/

        return retval;
}

static fw_update_state synaptics_fw_update(void *chip_data, const struct firmware *fw, bool force)
{
        int retval = 0;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        /*firmware update init*/
        retval = synaptics_rmi4_fwu_init(chip_info, force);
        if (retval < 0) {        /*less zero means the space has been released in synaptics_rmi4_fwu_init*/
                goto no_need_release_mem;
        }

        /*start reflash*/
        chip_info->fwu->image = fw->data;
        retval = fwu_start_reflash(chip_info);
        if (retval < 0) {
                TPD_INFO("fwu_start_reflash failed!\n");
        } else {
                TPD_INFO("firmwre update successed!\n");
        }

        kfree(chip_info->fwu->rmi4_data);
        chip_info->fwu->rmi4_data = NULL;
        kfree(chip_info->fwu);
        chip_info->fwu = NULL;
        return (retval < 0) ? FW_NO_NEED_UPDATE : FW_UPDATE_SUCCESS;

no_need_release_mem:
        return FW_NO_NEED_UPDATE;
}

static fp_touch_state synaptics_spurious_fp_check(void *chip_data)
{
        int x = 0, y = 0, z = 0, err_count = 0;
        int ret = 0, TX_NUM = 0, RX_NUM = 0;
        int16_t temp_data = 0, delta_data = 0;
        uint8_t *raw_data = NULL;
        fp_touch_state fp_touch_state = FINGER_PROTECT_TOUCH_UP;

        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;
        TPD_INFO(" synaptics_spurious_fp_check  start\n");

        if (TX_NUM*RX_NUM*(sizeof(int16_t)) > 1800){
                TPD_INFO("%s, TX_NUM*RX_NUM*(sizeof(int16_t)>1800, There is not enough space\n", __func__);
                return FINGER_PROTECT_NOTREADY;
        }

        if (!chip_info->spuri_fp_data) {
                TPD_INFO("chip_info->spuri_fp_data kzalloc error\n");
                return fp_touch_state;
        }
        TX_NUM = chip_info->hw_res->TX_NUM;
        RX_NUM = chip_info->hw_res->RX_NUM;

        raw_data = kzalloc(TX_NUM * SPURIOUS_FP_RX_NUM * 2 * (sizeof(uint8_t)), GFP_KERNEL);
        if (!raw_data) {
                        TPD_INFO("raw_data kzalloc error\n");
                        return fp_touch_state;
        }

        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x0);
        if (ret < 0) {
                TPD_INFO("%s, I2C transfer error\n", __func__);
                goto OUT;
        }

        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL00);
        ret = (ret & 0xF8) | 0x80;
        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL00, ret);   /*exit sleep*/
        msleep(5);
        touch_i2c_write_byte(chip_info->client, 0xff, 0x1);
        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x7c);/*select report type 124*/
        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+1, 0x00);/*set LSB*/
        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+2, 0x00);/*set MSB*/
        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/

        ret = checkCMD_for_finger(chip_info);
        if (ret < 0) {
                fp_touch_state = FINGER_PROTECT_TOUCH_DOWN;
                goto OUT;
        }

        fp_touch_state = FINGER_PROTECT_TOUCH_UP;
        touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+3, TX_NUM*SPURIOUS_FP_RX_NUM*2, raw_data); /*read baseline data*/
        for (x = 1; x < TX_NUM - 2; x++) {
                TPD_DEBUG_NTAG("[%2d]: ", x);
                for (y = 0; y < (SPURIOUS_FP_RX_NUM - 3); y++) {
                        z = SPURIOUS_FP_RX_NUM * x + y;
                        temp_data = (raw_data[z * 2 + 1] << 8) | raw_data[z * 2];
                        delta_data = temp_data - chip_info->spuri_fp_data[z];
                        TPD_DEBUG_NTAG("%4d, ", delta_data);
                        if ((delta_data + SPURIOUS_FP_LIMIT) < 0) {
                                if (!tp_debug)
                                        TPD_INFO("delta_data too large, delta_data = %d TX[%d] RX[%d]\n", delta_data, x, y);
                                err_count++;
                        }
                }
                TPD_DEBUG_NTAG("\n");
                if (err_count > 2) {
                        fp_touch_state = FINGER_PROTECT_TOUCH_DOWN;
                        err_count = 0;
                        if (!tp_debug)
                                break;
                }
        }

        TPD_INFO("%s:%d chip_info->reg_info.F54_ANALOG_COMMAND_BASE=0x%x set 0, \n", __func__, __LINE__, chip_info->reg_info.F54_ANALOG_COMMAND_BASE); /*add for Prevent TP failure*/
        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0);

        touch_i2c_write_byte(chip_info->client, 0xff, 0x0);
        if (ret < 0) {
                TPD_INFO("%s, I2C transfer error, line=%d\n", __func__, __LINE__);
        }

        TPD_INFO("finger protect trigger fp_touch_state= %d\n", fp_touch_state);

OUT:
        kfree(raw_data);
        return fp_touch_state;
}


static u8 synaptics_get_keycode(void *chip_data)
{
        int ret = 0;
        u8 bitmap_result = 0;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        touch_i2c_write_byte(chip_info->client, 0xff, 0x02);
        ret = touch_i2c_read_byte(chip_info->client, 0x00);
        TPD_INFO("touch key int_key code = %d\n", ret);

        if (ret & 0x01)
                SET_BIT(bitmap_result, BIT_MENU);
        if (ret & 0x02)
                SET_BIT(bitmap_result, BIT_BACK);

        touch_i2c_write_byte(chip_info->client, 0xff, 0x00);
        return bitmap_result;
}

static void synaptics_finger_proctect_data_get(void * chip_data)
{
        int ret = 0, x = 0, y = 0, z = 0;
        uint8_t *raw_data = NULL;
        static uint8_t retry_time = 3;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        int TX_NUM = chip_info->hw_res->TX_NUM;
        int RX_NUM = chip_info->hw_res->RX_NUM;

        if (TX_NUM*RX_NUM*(sizeof(int16_t)) > 1800){
                TPD_INFO("%s, TX_NUM*RX_NUM*(sizeof(int16_t)>1800, There is not enough space\n", __func__);
                return;
        }

        raw_data = kzalloc(TX_NUM * SPURIOUS_FP_RX_NUM * 2 * (sizeof(uint8_t)), GFP_KERNEL);
        if (!raw_data) {
                        TPD_INFO("raw_data kzalloc error\n");
                        return;
        }

        chip_info->spuri_fp_data = kzalloc(TX_NUM*RX_NUM*(sizeof(int16_t)), GFP_KERNEL);
        if (!chip_info->spuri_fp_data) {
                TPD_INFO("chip_info->spuri_fp_data kzalloc error\n");
                ret = -ENOMEM;
                kfree(raw_data);
                raw_data = NULL;
                return;
        }

RE_TRY:
        TPD_INFO("%s retry_time=%d line=%d\n", __func__, retry_time, __LINE__);
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x1);
        if (ret < 0) {
                TPD_INFO("%s, I2C transfer error\n", __func__);
                kfree(raw_data);
                raw_data = NULL;
                return;
        }
        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0X02); /*forcecal*/
        ret = checkCMD_for_finger(chip_info);
        if (ret < 0) {
                if (retry_time) {
                        TPD_INFO("checkCMD_for_finger error line=%d\n", __LINE__);
                        retry_time--;
                        goto RE_TRY;
                }
        }

        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE, 0x7c);/*select report type 0x02*/
        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+1, 0x00);/*set LSB*/
        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+2, 0x00);/*set MSB*/
        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x01);/*get report*/

        ret = checkCMD_for_finger(chip_info);
        if (ret < 0) {
                if (retry_time) {
                        TPD_INFO("checkCMD_for_finger error line=%d\n", __LINE__);
                        retry_time--;
                        goto RE_TRY;
                }
        }

        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F54_ANALOG_DATA_BASE+3, TX_NUM*SPURIOUS_FP_RX_NUM*2, raw_data);         /*read data*/
        if (ret < 0) {
                if (retry_time) {
                        TPD_INFO("%s touch_i2c_read_block error\n", __func__);
                        retry_time--;
                        goto RE_TRY;
                }
        }

        for (x = 0; x < TX_NUM; x++) {
                printk("[%2d]: ", x);
                for (y = 0; y < SPURIOUS_FP_RX_NUM; y++) {
                        z = SPURIOUS_FP_RX_NUM*x + y;
                        chip_info->spuri_fp_data[z] = (raw_data[z*2 + 1] << 8) | raw_data[z *2];
                        printk("%5d, ", chip_info->spuri_fp_data[z]);
                }
                printk("\n");
        }

        TPD_INFO("%s F54_ANALOG_COMMAND_BASE=0x%x set 0, \n", __func__, chip_info->reg_info.F54_ANALOG_COMMAND_BASE); /*add for Prevent TP failure*/
        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0);

        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x0);   /* page 0*/
        if (ret < 0) {
                TPD_INFO("%s, I2C transfer error\n", __func__);
        }

        kfree(raw_data);
}

static void synaptics_health_report(void *chip_data, struct monitor_data *mon_data)
{
        int ret = 0, i = 0;
        int data_length = 0;
        uint8_t log_data[255];
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;
        struct health_info *health_info = (struct health_info *)log_data;
        struct health_info *health_local = &(chip_info->health_zone.health_info);
        if (!chip_info->health_zone.health_support) {
                return;
        }
        ret = touch_i2c_write_byte(chip_info->client, 0xff, (uint8_t)(chip_info->health_zone.loglength_addr >> 8));        /* page 4*/
        data_length = touch_i2c_read_byte(chip_info->client, (uint8_t)(chip_info->health_zone.loglength_addr & 0xFF));
        if (data_length < 1 || data_length > 255) {
                return;
        } else {
                ret = touch_i2c_read_block(chip_info->client, ((uint8_t)(chip_info->health_zone.loglength_addr & 0xFF) + 1), data_length, log_data);         /*read log*/
                TPD_DEBUG("data_length = %d\n", data_length);
                if (health_info->grip_count != 0 && health_local->grip_count != health_info->grip_count) {
                        mon_data->grip_report++;
                }
                if (health_info->baseline_err != 0 && health_local->baseline_err != health_info->baseline_err) {
                        switch(health_info->baseline_err) {
                                case BASE_NEGATIVE_FINGER:
                                        mon_data->reserve1++;
                                        break;
                                case BASE_MUTUAL_SELF_CAP:
                                        mon_data->reserve2++;
                                        break;
                                case BASE_ENERGY_RATIO:
                                        mon_data->reserve3++;
                                        break;
                                case BASE_RXABS_BASELINE:
                                        mon_data->reserve4++;
                                        break;
                                case BASE_TXABS_BASELINE:
                                        mon_data->reserve5++;
                                        break;
                                default:
                                        break;
                        }
                }
                if (health_info->noise_state >= 2 && health_local->noise_state != health_info->noise_state) {
                        mon_data->noise_count++;
                }
                if (health_info->shield_mode != 0 && health_local->shield_mode != health_info->shield_mode) {
                        switch(health_info->shield_mode) {
                                case SHIELD_PALM:
                                        mon_data->shield_palm++;
                                        break;
                                case SHIELD_GRIP:
                                        mon_data->shield_edge++;
                                        break;
                                case SHIELD_METAL:
                                        mon_data->shield_metal++;
                                        break;
                                case SHIELD_MOISTURE:
                                        mon_data->shield_water++;
                                        break;
                                case SHIELD_ESD:
                                        mon_data->shield_esd++;
                                        break;
                                default:
                                        break;
                        }
                }
                if (health_info->reset_reason != 0) {
                        switch(health_info->reset_reason) {
                                case RST_HARD:
                                        mon_data->hard_rst++;
                                        break;
                                case RST_INST:
                                        mon_data->inst_rst++;
                                        break;
                                case RST_PARITY:
                                        mon_data->parity_rst++;
                                        break;
                                case RST_WD:
                                        mon_data->wd_rst++;
                                        break;
                                case RST_OTHER:
                                        mon_data->other_rst++;
                                        break;
                    }
                    touch_i2c_write_byte(chip_info->client, 0xff, 0x04);        /* page 4*/
                    ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL13);
                    touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL13, ret | 0x80);        /* page 4*/
                    TPD_INFO("chip_info->reg_info.F51_CUSTOM_CTRL13= %x  ret=%x\n", chip_info->reg_info.F51_CUSTOM_CTRL_BASE + 0x0C, ret | 0x80);
                }
                memcpy(health_local, health_info, sizeof(struct health_info));
                if (tp_debug != 0) {
                        for (i = 0; i< data_length; i++) {
                                printk(" [0x%x], ", log_data[i]);
                        }
                }
                TPD_INFO("\n");
        }

        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x0);        /* page 0*/
}

static void synaptics_gesture_rate(struct seq_file *s, u16* coord_arg, void *chip_data)
{
        int ret = 0;
        int loop = 0;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;
        char *coord = (char*)coord_arg;
        if (!(coord_arg && chip_info))
                return;
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x00);        /* page 4*/
        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL00);        /* page 4*/
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F01_RMI_CTRL00, ret | 0x08);        /* page 4*/
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x04);        /* page 4*/
        if (coord_arg[0] == DouTap){
                ret = touch_i2c_write_word(chip_info->client, 0x25, coord_arg[1]);         /*write length*/
                ret = touch_i2c_write_byte(chip_info->client, 0x28, 2);         /*Data Function*/
        } else if(coord_arg[0] == DouSwip) {
                ret = touch_i2c_write_word(chip_info->client, 0x25, coord_arg[1] + coord_arg[105]);            /*write length*/
                ret = touch_i2c_write_byte(chip_info->client, 0x28, 4);         /*Data Function*/
        } else {
                ret = touch_i2c_write_word(chip_info->client, 0x25, coord_arg[1]);         /*write length*/
                ret = touch_i2c_write_byte(chip_info->client, 0x28, 1);         /*Data Function*/
        }

        for (loop = 0; loop < 2*coord_arg[1]; loop++) {
                ret = touch_i2c_write_byte(chip_info->client, 0x27, *(coord+loop+6));         /*write coordinate*/
        }
        if (coord_arg[0] == DouSwip) {
                for(loop = 0; loop <2*coord_arg[105]; loop++) {
                        ret = touch_i2c_write_byte(chip_info->client, 0x27, *(coord+loop+4+210));         /*write coordinate*/
                }
        }
        TPD_DETAIL("coord[0]:%u %u %u %u %u\n", coord_arg[0], coord_arg[1], coord_arg[3], coord_arg[4], coord_arg[5]);
        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x00);        /* page 4*/
}

static void synaptics_register_info_read(void * chip_data, uint16_t register_addr, uint8_t * result, uint8_t length)
{
        int ret = 0;
        uint8_t l_tmp = 0, h_tmp = 0;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        l_tmp = register_addr & 0xFF;   /*address*/
        h_tmp = register_addr >> 8;         /*page*/

        if (h_tmp > 4) {
                TPD_INFO("register_addr error!\n");
                return;
        }
        ret = touch_i2c_write_byte(chip_info->client, 0xff, h_tmp);        /*set page*/
        ret = touch_i2c_read_block(chip_info->client, l_tmp, length, result);         /*read data*/
}

static int synaptics_get_usb_state(void)
{
        return 0;
}

static int synaptics_get_face_state(void * chip_data)
{
    int state = -1, ret = -1;
    struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

    touch_i2c_write_byte(chip_info->client, 0xff, 0x04);        /*set page*/
    state = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_DATA_BASE+0x05);
    touch_i2c_write_byte(chip_info->client, 0xff, 0x00);        /*set page*/

    if (0x80 == state) {
        ret = 0;
    } else if (0x81 == state) {
        ret = 0x01;
    } else if ((0x83 == state) || (0x85 == state)) {
        ret = 0x11;
    }
    if (ret >= 0)
        TPD_INFO("ps value : 0x%02x\n", state);

    //reset for rotation, screen color black to white, regard as near
    if ((ret > 0) && time_before(jiffies, chip_info->rotation_changed_time + HZ) && chip_info->rotation_changed_time) {
        TPD_INFO("reset fd for ret:0x%02x\n", ret);
        synaptics_enable_face_detect(chip_info, false);
        mdelay(2);
        synaptics_enable_face_detect(chip_info, true);
        ret = 0;
    }
    return ret;
}

static void synaptics_set_touch_direction(void *chip_data, uint8_t dir)
{
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        chip_info->touch_direction = dir;
}

static uint8_t synaptics_get_touch_direction(void *chip_data)
{
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        return chip_info->touch_direction;
}

static void synaptics_screenon_fingerprint_info(void *chip_data, struct fp_underscreen_info *fp_tpinfo)
{
        int ret = 0;
        uint8_t buf[10];
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x0);
        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.F12_2D_DATA04, 5, &(buf[0]));      /*get gesture type*/
        fp_tpinfo->touch_state = buf[0];

        touch_i2c_write_byte(chip_info->client, 0xff, 0x4);
        touch_i2c_read_block(chip_info->client, chip_info->reg_info.F51_CUSTOM_DATA_BASE, 8, &(buf[0]));
        fp_tpinfo->x = (buf[1] << 8) | buf[0];
        fp_tpinfo->y = (buf[3] << 8) | buf[2];
        fp_tpinfo->area_rate = (buf[5] << 8) | buf[4];

        if (FINGERPRINT_DOWN_DETECT == fp_tpinfo->touch_state) {
                chip_info->is_fp_down = true;
        } else if(FINGERPRINT_UP_DETECT == fp_tpinfo->touch_state) {
                chip_info->is_fp_down = false;

                if (chip_info->force_update_needed) {
                        chip_info->force_update_needed = false;
                        touch_i2c_write_byte(chip_info->client, 0xff, 0x01);        /* page 1*/
                        touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F54_ANALOG_COMMAND_BASE, 0x04);    /* force update*/
                        checkCMD(chip_info, 30);
                        TPD_INFO("do force update\n");
                }
        }
        touch_i2c_write_byte(chip_info->client, 0xff, 0x0);
}

static void synaptics_specific_resume_operate(void *chip_data)
{
    struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

    chip_info->filter_state = false;
}

static void synaptics_enable_gesture_mask(void *chip_data, uint32_t enable)
{
        int ret = -1, gesture_mask = 0;
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        touch_i2c_write_byte(chip_info->client, 0xff, 0x4);
        gesture_mask = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL20);
        if (enable) {
                gesture_mask &= 0x01;   /* enable all gesture*/
        } else {
                gesture_mask |= 0x80;   /* mask all gesture */
        }
        ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL20, gesture_mask);
        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL20);
        TPD_INFO("%s :F51_CUSTOM_CTRL20 is %d\n", __func__, ret);
        touch_i2c_write_byte(chip_info->client, 0xff, 0x0);

        return;
}

static int synaptics_set_report_point_first(void *chip_data, uint32_t enable)
{
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;
        int ret = 0;

        touch_i2c_write_byte(chip_info->client, 0xff, 0x04);//set page 4
        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL13);
        if (ret < 0) {
                touch_i2c_write_byte(chip_info->client, 0xff, 0x00);        /* page 0*/
                TPD_INFO("failed for get F51_CUSTOM_CTRL13\n");
                return ret;
        }

        if (enable) {//enable game mode control flag
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL13, (ret | 0x04));
                if (ret < 0) {
                        touch_i2c_write_byte(chip_info->client, 0xff, 0x00);		/* page 0*/
                        TPD_INFO("%s failed for game mode control\n", __func__);
                        return ret;
                }
        } else {//disable game mode control flag
                ret = touch_i2c_write_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL13, (ret & 0xFB));
                if (ret < 0) {
                        TPD_INFO("%s failed for disable game mode control\n", __func__);
                        touch_i2c_write_byte(chip_info->client, 0xff, 0x00);		/* page 0*/
                        return ret;
                }
        }

        ret = touch_i2c_write_byte(chip_info->client, 0xff, 0x00);        /* page 0*/
        TPD_INFO("%s state: %d %s\n", __func__, enable, ret < 0 ? "failed":"success");
        return ret;
}

static int synaptics_get_report_point_first(void *chip_data)
{
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;
        int ret = 0;

        touch_i2c_write_byte(chip_info->client, 0xff, 0x04);//set page 4
        ret = touch_i2c_read_byte(chip_info->client, chip_info->reg_info.F51_CUSTOM_CTRL13);
        if (ret < 0) {
            TPD_INFO("failed for get F51_CUSTOM_CTRL13\n");
            touch_i2c_write_byte(chip_info->client, 0xff, 0x00);//set page 0
            return ret;
        }
        touch_i2c_write_byte(chip_info->client, 0xff, 0x00);//set page 0
        return ((ret & 0x04) >>2);
}

static struct oppo_touchpanel_operations synaptics_ops = {
        .ftm_process                = synaptics_ftm_process,
        .get_vendor                 = synaptics_get_vendor,
        .get_chip_info              = synaptics_get_chip_info,
        .reset                      = synaptics_reset,
        .power_control              = synaptics_power_control,
        .fw_check                   = synaptics_fw_check,
        .fw_update                  = synaptics_fw_update,
        .trigger_reason             = synaptics_trigger_reason,
        .u32_trigger_reason         = synaptics_u32_trigger_reason,
        .get_touch_points           = synaptics_get_touch_points,
        .get_gesture_info           = synaptics_get_gesture_info,
        .mode_switch                = synaptics_mode_switch,
        .get_keycode                = synaptics_get_keycode,
        .spurious_fp_check          = synaptics_spurious_fp_check,
        .finger_proctect_data_get   = synaptics_finger_proctect_data_get,
        .health_report              = synaptics_health_report,
#if GESTURE_COORD_GET
        .get_gesture_coord          = synaptics_get_gesture_coord,
#endif
        .register_info_read         = synaptics_register_info_read,
        .get_usb_state              = synaptics_get_usb_state,
        .get_face_state             = synaptics_get_face_state,
        .enable_fingerprint         = synaptics_enable_fingerprint_underscreen,
        .enable_gesture_mask        = synaptics_enable_gesture_mask,
        .set_touch_direction        = synaptics_set_touch_direction,
        .get_touch_direction        = synaptics_get_touch_direction,
        .screenon_fingerprint_info  = synaptics_screenon_fingerprint_info,
        .specific_resume_operate    = synaptics_specific_resume_operate,
        .set_report_point_first     = synaptics_set_report_point_first,
        .get_report_point_first     = synaptics_get_report_point_first,
};

static void synaptics_set_touchfilter_state(void *chip_data, uint8_t state)
{
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        chip_info->filter_state = state;
}

static uint8_t synaptics_get_touchfilter_state(void *chip_data)
{
        struct chip_data_s3706 *chip_info = (struct chip_data_s3706 *)chip_data;

        return chip_info->filter_state;
}

static struct synaptics_proc_operations synaptics_proc_ops = {
        .auto_test         = synaptics_auto_test,
        .set_touchfilter_state  = synaptics_set_touchfilter_state,
        .get_touchfilter_state  = synaptics_get_touchfilter_state,
};

static struct debug_info_proc_operations debug_info_proc_ops = {
        .limit_read        = synaptics_limit_read,
        .delta_read        = synaptics_delta_read,
        .baseline_read = synaptics_baseline_read,
        .reserve_read  = synaptics_reserve_read,
        .RT251                 = synaptics_RT251_read,
        .RT76                  = synaptics_RT76_read,
        .main_register_read = synaptics_main_register_read,
        .gesture_rate       = synaptics_gesture_rate,
};

static int synaptics_tp_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
#ifdef CONFIG_SYNAPTIC_RED
        struct remotepanel_data *premote_data = NULL;
#endif

        struct chip_data_s3706 *chip_info;
        struct touchpanel_data *ts = NULL;
        int ret = -1;

        TPD_INFO("%s  is called\n", __func__);

        if (tp_register_times > 0) {
                TPD_INFO("TP driver have success loaded %d times, exit\n", tp_register_times);
                return -1;
        }

        /*step1:Alloc chip_info*/
        chip_info = kzalloc(sizeof(struct chip_data_s3706), GFP_KERNEL);
        if (chip_info == NULL) {
                TPD_INFO("chip info kzalloc error\n");
                ret = -ENOMEM;
                return ret;
        }
        memset(chip_info, 0, sizeof(*chip_info));
        g_chip_info = chip_info;

        /*step2:Alloc common ts*/
        ts = common_touch_data_alloc();
        if (ts == NULL) {
                TPD_INFO("ts kzalloc error\n");
                goto ts_malloc_failed;
        }
        memset(ts, 0, sizeof(*ts));

        /*step3:binding client && dev for easy operate*/
        chip_info->client = client;
        chip_info->syna_ops = &synaptics_proc_ops;
        ts->debug_info_ops = &debug_info_proc_ops;
        ts->client = client;
        ts->irq = client->irq;
        i2c_set_clientdata(client, ts);
        ts->dev = &client->dev;
        ts->chip_data = chip_info;
        chip_info->hw_res = &ts->hw_res;
        chip_info->filter_state = 0;
        chip_info->touch_direction = VERTICAL_SCREEN;
        chip_info->is_fp_down = false;
        chip_info->force_update_needed = false;

        /*step4:file_operations callback binding*/
        ts->ts_ops = &synaptics_ops;

        /*step5:register common touch*/
        ret = register_common_touch_device(ts);
        if (ret < 0) {
                goto err_register_driver;
        }
        chip_info->fw_corner_limit_support = of_property_read_bool(ts->dev->of_node, "fw_corner_limit_support");
        chip_info->rt155_fdreplace_rt59_support = of_property_read_bool(ts->dev->of_node, "rt155_fdreplace_rt59_support");
        chip_info->report_120hz_support = of_property_read_bool(ts->dev->of_node, "report_120hz_support");

        /*step6: collect data for supurious_fp_touch*/
        if (ts->spurious_fp_support) {
                mutex_lock(&ts->mutex);
                synaptics_finger_proctect_data_get(chip_info);
                mutex_unlock(&ts->mutex);
        }


        /*step7:create synaptics related proc files*/
        synaptics_create_proc(ts, chip_info->syna_ops);

        /*step8:Chip Related function*/
#ifdef CONFIG_SYNAPTIC_RED
        premote_data = remote_alloc_panel_data();
        chip_info->premote_data = premote_data;
        if (premote_data) {
                premote_data->client                = client;
                premote_data->input_dev         = ts->input_dev;
                premote_data->pmutex                = &ts->mutex;
                premote_data->irq_gpio          = ts->hw_res.irq_gpio;
                premote_data->irq                   = client->irq;
                premote_data->enable_remote = &(chip_info->enable_remote);
                register_remote_device(premote_data);
        }
#endif

        TPD_INFO("%s, probe normal end\n", __func__);

        return 0;

err_register_driver:
        common_touch_data_free(ts);
        ts = NULL;

ts_malloc_failed:
        kfree(chip_info);
        chip_info = NULL;
        ret = -1;

        TPD_INFO("%s, probe error\n", __func__);

        return ret;
}

static int synaptics_tp_remove(struct i2c_client *client)
{
        struct touchpanel_data *ts = i2c_get_clientdata(client);

        TPD_INFO("%s is called\n", __func__);
#ifdef CONFIG_SYNAPTIC_RED
        unregister_remote_device();
#endif
        kfree(ts);

        return 0;
}

static int synaptics_i2c_suspend(struct device *dev)
{
        struct touchpanel_data *ts = dev_get_drvdata(dev);

        TPD_INFO("%s: is called\n", __func__);
        tp_i2c_suspend(ts);

        return 0;
}

static int synaptics_i2c_resume(struct device *dev)
{
        struct touchpanel_data *ts = dev_get_drvdata(dev);

        TPD_INFO("%s is called\n", __func__);
        tp_i2c_resume(ts);

        return 0;
}

static const struct i2c_device_id tp_id[] = {
        { TPD_DEVICE, 0 },
        { }
};

static struct of_device_id tp_match_table[] = {
        { .compatible = TPD_DEVICE, },
        { },
};

static const struct dev_pm_ops tp_pm_ops = {
#ifdef CONFIG_FB
        .suspend = synaptics_i2c_suspend,
        .resume = synaptics_i2c_resume,
#endif
};

static struct i2c_driver tp_i2c_driver = {
        .probe          = synaptics_tp_probe,
        .remove         = synaptics_tp_remove,
        .id_table   = tp_id,
        .driver         = {
                .name   = TPD_DEVICE,
                .of_match_table =  tp_match_table,
                .pm = &tp_pm_ops,
        },
};

static int __init tp_driver_init(void)
{
        TPD_INFO("%s is called\n", __func__);

#ifdef CONFIG_MACH_MT6885
        if (!tp_judge_ic_match(TPD_DEVICE))
            return -1;
#endif

        if (i2c_add_driver(&tp_i2c_driver)!= 0) {
                TPD_INFO("unable to add i2c driver.\n");
                return -1;
        }
        return 0;
}

/* should never be called */
static void __exit tp_driver_exit(void)
{
        i2c_del_driver(&tp_i2c_driver);
        return;
}
#ifdef CONFIG_TOUCHPANEL_LATE_INIT
late_initcall(tp_driver_init);
#else
module_init(tp_driver_init);
#endif
module_exit(tp_driver_exit);

MODULE_DESCRIPTION("Touchscreen Driver");
MODULE_LICENSE("GPL");
