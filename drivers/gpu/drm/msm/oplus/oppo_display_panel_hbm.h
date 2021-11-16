/***************************************************************
** Copyright (C),  2020,  OPPO Mobile Comm Corp.,  Ltd
** VENDOR_EDIT
** File : oppo_display_panel_hbm.h
** Description : oppo display panel hbm feature
** Version : 1.0
** Date : 2020/07/06
** Author : Li.Sheng@MULTIMEDIA.DISPLAY.LCD
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  Li.Sheng       2020/07/06        1.0           Build this feature
******************************************************************/
#ifndef _OPPO_DISPLAY_PANEL_HBM_H_
#define _OPPO_DISPLAY_PANEL_HBM_H_

#include <linux/err.h>
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_ctrl.h"
#include "dsi_ctrl_hw.h"
#include "dsi_drm.h"
#include "dsi_clk.h"
#include "dsi_pwr.h"
#include "sde_dbg.h"

int oppo_display_get_hbm_mode(void);
int __oppo_display_set_hbm(int mode);
int dsi_display_hbm_off(struct dsi_display *display);
int dsi_display_normal_hbm_on(struct dsi_display *display);
int dsi_display_hbm_on(struct dsi_display *display);
int dsi_panel_normal_hbm_on(struct dsi_panel *panel);
int dsi_panel_hbm_off(struct dsi_panel *panel);
int dsi_panel_hbm_on(struct dsi_panel *panel);
int oppo_display_panel_set_hbm(void *buf);
int oppo_display_panel_get_hbm(void *buf);
#endif /*_OPPO_DISPLAY_PANEL_HBM_H_*/
