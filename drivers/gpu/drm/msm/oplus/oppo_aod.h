/***************************************************************
** Copyright (C),  2020,  OPPO Mobile Comm Corp.,  Ltd
** VENDOR_EDIT
** File : oppo_aod.h
** Description : oppo aod feature
** Version : 1.0
** Date : 2020/04/23
** Author : Qianxu@MM.Display.LCD Driver
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   Qianxu         2020/04/23        1.0           Build this moudle
******************************************************************/
#ifndef _OPPO_AOD_H_
#define _OPPO_AOD_H_

#include "dsi_display.h"
#include "oppo_dsi_support.h"

int dsi_display_aod_on(struct dsi_display *display);

int dsi_display_aod_off(struct dsi_display *display);

int oppo_update_aod_light_mode_unlock(struct dsi_panel *panel);

int oppo_update_aod_light_mode(void);

int oppo_panel_set_aod_light_mode(void *buf);
int oppo_panel_get_aod_light_mode(void *buf);
int __oppo_display_set_aod_light_mode(int mode);

#endif /* _OPPO_AOD_H_ */
