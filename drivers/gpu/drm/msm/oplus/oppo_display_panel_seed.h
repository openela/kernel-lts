/***************************************************************
** Copyright (C),  2020,  OPPO Mobile Comm Corp.,  Ltd
** VENDOR_EDIT
** File : oppo_display_panel_seed.h
** Description : oppo display panel seed feature
** Version : 1.0
** Date : 2020/06/13
** Author : Li.Sheng@MULTIMEDIA.DISPLAY.LCD
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  Li.Sheng       2020/06/13        1.0           Build this moudle
******************************************************************/
#ifndef _OPPO_DISPLAY_PANEL_SEED_H_
#define _OPPO_DISPLAY_PANEL_SEED_H_

#include <linux/err.h>
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_ctrl.h"
#include "dsi_ctrl_hw.h"
#include "dsi_drm.h"
#include "dsi_clk.h"
#include "dsi_pwr.h"
#include "sde_dbg.h"

int oppo_display_panel_get_seed(void *data);
int oppo_display_panel_set_seed(void *data);
int oppo_dsi_update_seed_mode(void);
int __oppo_display_set_seed(int mode);
int dsi_panel_seed_mode(struct dsi_panel *panel, int mode);
int dsi_panel_seed_mode_unlock(struct dsi_panel *panel, int mode);
int dsi_display_seed_mode(struct dsi_display *display, int mode);

#endif /* _OPPO_DISPLAY_PANEL_SEED_H_ */
