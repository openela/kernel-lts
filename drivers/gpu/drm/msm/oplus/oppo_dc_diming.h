/***************************************************************
** Copyright (C),  2020,  OPPO Mobile Comm Corp.,  Ltd
** VENDOR_EDIT
** File : oppo_dc_diming.h
** Description : oppo dc_diming feature
** Version : 1.0
** Date : 2020/04/15
** Author : Qianxu@MM.Display.LCD Driver
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   Qianxu         2020/04/15        1.0           Build this moudle
******************************************************************/
#ifndef _OPPO_DC_DIMING_H_
#define _OPPO_DC_DIMING_H_

#include <drm/drm_connector.h>

#include "dsi_panel.h"
#include "dsi_defs.h"
#include "oppo_display_panel_hbm.h"

int sde_connector_update_backlight(struct drm_connector *connector, bool post);

int sde_connector_update_hbm(struct drm_connector *connector);

int oppo_seed_bright_to_alpha(int brightness);

struct dsi_panel_cmd_set * oppo_dsi_update_seed_backlight(struct dsi_panel *panel, int brightness,
				enum dsi_cmd_set_type type);
int oppo_display_panel_get_dim_alpha(void *buf);
int oppo_display_panel_set_dim_alpha(void *buf);
int oppo_display_panel_get_dim_dc_alpha(void *buf);
int dsi_panel_parse_oppo_dc_config(struct dsi_panel *panel);
#endif /*_OPPO_DC_DIMING_H_*/
