/***************************************************************
** Copyright (C),  2020,  OPPO Mobile Comm Corp.,  Ltd
** VENDOR_EDIT
** File : oppo_display_panel_common.c
** Description : oppo display panel common feature
** Version : 1.0
** Date : 2020/06/13
** Author : Li.Sheng@MULTIMEDIA.DISPLAY.LCD
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  Li.Sheng       2020/06/13        1.0           Build this moudle
******************************************************************/
#ifndef _OPPO_DISPLAY_PANEL_COMMON_H_
#define _OPPO_DISPLAY_PANEL_COMMON_H_

#include <linux/err.h>
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_ctrl.h"
#include "dsi_ctrl_hw.h"
#include "dsi_drm.h"
#include "dsi_clk.h"
#include "dsi_pwr.h"
#include "sde_dbg.h"
#include "oppo_dsi_support.h"

struct panel_id
{
	uint32_t DA;
	uint32_t DB;
	uint32_t DC;
};

struct panel_info
{
	char version[32];
	char manufacture[32];
};

struct panel_serial_number
{
	uint32_t date;		 /*year:month:day:hour  8bit uint*/
	uint32_t precise_time; /* minute:second:reserved:reserved 8bit uint*/
};

int oppo_display_panel_get_id(void *buf);
int oppo_display_panel_get_max_brightness(void *buf);
int oppo_display_panel_set_max_brightness(void *buf);
int oppo_display_panel_get_vendor(void *buf);
int oppo_display_panel_get_ccd_check(void *buf);
int oppo_display_panel_get_serial_number(void *buf);
int oplus_display_get_panel_parameters(struct dsi_panel *panel, struct dsi_parser_utils *utils);
#endif /*_OPPO_DISPLAY_PANEL_COMMON_H_*/
