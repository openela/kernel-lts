/***************************************************************
** Copyright (C),  2018,  OPPO Mobile Comm Corp.,  Ltd
** VENDOR_EDIT
** File : oppo_dsi_support.h
** Description : display driver private management
** Version : 1.0
** Date : 2018/03/17
** Author : Jie.Hu@PSW.MM.Display.Stability
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   Hu.Jie          2018/03/17        1.0           Build this moudle
******************************************************************/
#ifndef _OPPO_DSI_SUPPORT_H_
#define _OPPO_DSI_SUPPORT_H_

#include <linux/err.h>
#include <linux/string.h>
#include <linux/notifier.h>

/* A hardware display blank change occurred */
#define OPPO_DISPLAY_EVENT_BLANK			0x01

/* A hardware display blank early change occurred */
#define OPPO_DISPLAY_EARLY_EVENT_BLANK		0x02


enum oppo_display_support_list {
	OPPO_SAMSUNG_S6E3FC2X01_DISPLAY_FHD_DSC_CMD_PANEL = 0,
	OPPO_SAMSUNG_S6E8FC1X01_DISPALY_FHD_PLUS_VIDEO_PANEL = 1,
	OPPO_BOE_HX83112A_FHD_PLUS_VIDEO_PANEL = 2,
	OPPO_HUAXING_HX83112A_DISPALY_FHD_PLUS_VIDEO_PANEL = 3,
	OPPO_HLT_HX83112A_DISPALY_FHD_PLUS_VIDEO_PANEL = 4,
	OPPO_TM_NT36672A_DISPALY_FHD_PLUS_VIDEO_PANEL = 5,
	OPPO_SAMSUNG_AMS644VK01_DISPALY_FHD_PLUS_CMD_PANEL = 6,
	OPPO_SAMSUNG_AMB655UV01_DISPALY_FHD_PLUS_90FPS_PANEL = 7,
	OPPO_SAMSUNG_AMS643XF01_DISPALY_FHD_PLUS_CMD_PANEL = 8,
	/* Chao.Zhang@PSW.MM.DISPLAY.LCD, 20200914, add for 19031 lcd bringup */
	OPPO_SAMSUNG_AMS641RW01_DISPLAY_FHD_PLUS_CMD_PANEL = 9,
	OPPO_SAMSUNG_AMS641RW01_DISPLAY_FHD_PLUS_61FPS_PANEL = 10,
	OPPO_SAMSUNG_AMS655UE01_DISPLAY_FHD_PLUS_61FPS_PANEL = 11,
	OPPO_SAMSUNG_AMS641RW16_DISPLAY_FHD_PLUS_CMD_PANEL = 12,
	/* Chao.Zhang@MULTIMEDIA.DISPLAY.LCD, 2020/09/25, add for 19081 LCD */
	OPPO_SAMSUNG_SOFEF03F_M_DISPLAY_FHD_DSC_CMD_PANEL = 13,
	OPPO_DISPLAY_UNKNOW,
};

enum oppo_display_power_status {
	OPPO_DISPLAY_POWER_OFF = 0,
	OPPO_DISPLAY_POWER_DOZE,
	OPPO_DISPLAY_POWER_ON,
	OPPO_DISPLAY_POWER_DOZE_SUSPEND,
	OPPO_DISPLAY_POWER_ON_UNKNOW,
};

enum oppo_display_scene {
	OPPO_DISPLAY_NORMAL_SCENE = 0,
	OPPO_DISPLAY_NORMAL_HBM_SCENE,
	OPPO_DISPLAY_AOD_SCENE,
	OPPO_DISPLAY_AOD_HBM_SCENE,
	OPPO_DISPLAY_UNKNOW_SCENE,
};

enum oppo_display_feature {
	OPPO_DISPLAY_HDR = 0,
	OPPO_DISPLAY_SEED,
	OPPO_DISPLAY_HBM,
	OPPO_DISPLAY_LBR,
	OPPO_DISPLAY_AOD,
	OPPO_DISPLAY_ULPS,
	OPPO_DISPLAY_ESD_CHECK,
	OPPO_DISPLAY_DYNAMIC_MIPI,
	OPPO_DISPLAY_PARTIAL_UPDATE,
	OPPO_DISPLAY_FEATURE_MAX,
};

typedef struct panel_serial_info
{
	int reg_index;
	uint64_t year;
	uint64_t month;
	uint64_t day;
	uint64_t hour;
	uint64_t minute;
	uint64_t second;
	uint64_t reserved[2];
} PANEL_SERIAL_INFO;


typedef struct oppo_display_notifier_event {
	enum oppo_display_power_status status;
	void *data;
}OPPO_DISPLAY_NOTIFIER_EVENT;

int oppo_display_register_client(struct notifier_block *nb);

int oppo_display_unregister_client(struct notifier_block *nb);

void notifier_oppo_display_early_status(enum oppo_display_power_status power_status);

void notifier_oppo_display_status(enum oppo_display_power_status power_status);

bool is_oppo_correct_display(enum oppo_display_support_list lcd_name);

bool is_silence_reboot(void);

int set_oppo_display_vendor(const char * display_name);

void set_oppo_display_power_status(enum oppo_display_power_status power_status);

enum oppo_display_power_status get_oppo_display_power_status(void);

void set_oppo_display_scene(enum oppo_display_scene display_scene);

enum oppo_display_scene get_oppo_display_scene(void);

bool is_oppo_display_support_feature(enum oppo_display_feature feature_name);

int oppo_display_get_resolution(unsigned int *xres, unsigned int *yres);

#endif /* _OPPO_DSI_SUPPORT_H_ */

