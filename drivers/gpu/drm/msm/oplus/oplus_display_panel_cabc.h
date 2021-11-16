/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
** VENDOR_EDIT
** File : oplus_display_panel_cabc.h
** Description : oplus display panel cabc feature
** Version : 1.0
** Date : 2020/07/06
** Author : Li.Sheng@MULTIMEDIA.DISPLAY.LCD
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  Li.Sheng       2020/07/06        1.0           Build this feature
******************************************************************/
#ifndef _OPLUS_DISPLAY_PANEL_CABC_H_
#define _OPLUS_DISPLAY_PANEL_CABC_H_
#ifdef OPLUS_FEATURE_LCD_CABC
#include <linux/err.h>
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_ctrl.h"
#include "dsi_ctrl_hw.h"
#include "dsi_drm.h"
#include "dsi_clk.h"
#include "dsi_pwr.h"
#include "sde_dbg.h"

int dsi_panel_cabc_video(struct dsi_panel *panel);
int dsi_panel_cabc_image(struct dsi_panel *panel);
int dsi_panel_cabc_ui(struct dsi_panel *panel);
int dsi_panel_cabc_off(struct dsi_panel *panel);
int dsi_display_cabc_video(struct dsi_display *display);
int dsi_display_cabc_image(struct dsi_display *display);
int dsi_display_cabc_ui(struct dsi_display *display);
int dsi_display_cabc_off(struct dsi_display *display);
int __oplus_display_set_cabc(int mode);
#endif /*OPLUS_FEATURE_LCD_CABC*/
#endif /*_OPLUS_DISPLAY_PANEL_CABC_H_*/
