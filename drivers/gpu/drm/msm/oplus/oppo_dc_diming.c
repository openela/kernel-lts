/***************************************************************
** Copyright (C),  2020,  OPPO Mobile Comm Corp.,  Ltd
** VENDOR_EDIT
** File : oppo_dc_diming.c
** Description : oppo dc_diming feature
** Version : 1.0
** Date : 2020/04/15
** Author : Qianxu@MM.Display.LCD Driver
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   Qianxu         2020/04/15        1.0           Build this moudle
******************************************************************/

#include "oppo_display_private_api.h"
#include "oppo_dc_diming.h"
#include "oppo_onscreenfingerprint.h"
#include "oppo_aod.h"


int oppo_dimlayer_bl = 0;
int oppo_dimlayer_bl_enabled = 0;
int oppo_datadimming_v3_skip_frame = 2;
int oppo_panel_alpha = 0;
int oppo_underbrightness_alpha = 0;
static struct dsi_panel_cmd_set oppo_priv_seed_cmd_set;

extern int oppo_dimlayer_bl_on_vblank;
extern int oppo_dimlayer_bl_off_vblank;
extern int oppo_dimlayer_bl_delay;
extern int oppo_dimlayer_bl_delay_after;
extern int oppo_dimlayer_bl_enable_v2;
extern int oppo_dimlayer_bl_enable_v2_real;
extern int oppo_dimlayer_bl_enable_v3;
extern int oppo_dimlayer_bl_enable_v3_real;
extern int oppo_datadimming_vblank_count;
extern atomic_t oppo_datadimming_vblank_ref;
extern int oppo_fod_on_vblank;
extern int oppo_fod_off_vblank;
extern bool oppo_skip_datadimming_sync;
extern int oppo_dimlayer_hbm_vblank_count;
extern atomic_t oppo_dimlayer_hbm_vblank_ref;
extern int oppo_dc2_alpha;
extern int oppo_seed_backlight;
extern int oppo_panel_alpha;
extern oppo_dc_v2_on;
extern ktime_t oppo_backlight_time;
#ifdef OPLUS_FEATURE_AOD_RAMLESS
/* Yuwei.Zhang@MULTIMEDIA.DISPLAY.LCD, 2020/09/25, sepolicy for aod ramless */
extern int oppo_display_mode;
#endif /* OPLUS_FEATURE_AOD_RAMLESS */

static struct oppo_brightness_alpha brightness_seed_alpha_lut_dc[] = {
	{0, 0xff},
	{1, 0xfc},
	{2, 0xfb},
	{3, 0xfa},
	{4, 0xf9},
	{5, 0xf8},
	{6, 0xf7},
	{8, 0xf6},
	{10, 0xf4},
	{15, 0xf0},
	{20, 0xea},
	{30, 0xe0},
	{45, 0xd0},
	{70, 0xbc},
	{100, 0x98},
	{120, 0x80},
	{140, 0x70},
	{160, 0x58},
	{180, 0x48},
	{200, 0x30},
	{220, 0x20},
	{240, 0x10},
	{260, 0x00},
};

/*Jiasong.ZhongPSW.MM.Display.LCD.Stable,2020-09-17 add for DC backlight */
int dsi_panel_parse_oppo_dc_config(struct dsi_panel *panel)
{
	int rc = 0;
	int i;
	u32 length = 0;
	u32 count = 0;
	u32 size = 0;
	u32 *arr_32 = NULL;
	const u32 *arr;
	struct dsi_parser_utils *utils = &panel->utils;
	struct oppo_brightness_alpha *seq;

	arr = utils->get_property(utils->data, "oppo,dsi-dc-brightness", &length);
	if (!arr) {
		DSI_ERR("[%s] oppo,dsi-dc-brightness  not found\n", panel->name);
		return -EINVAL;
	}

	if (length & 0x1) {
		DSI_ERR("[%s] oppo,dsi-dc-brightness length error\n", panel->name);
		return -EINVAL;
	}

	DSI_DEBUG("RESET SEQ LENGTH = %d\n", length);
	length = length / sizeof(u32);
	size = length * sizeof(u32);

	arr_32 = kzalloc(size, GFP_KERNEL);
	if (!arr_32) {
		rc = -ENOMEM;
		goto error;
	}

	rc = utils->read_u32_array(utils->data, "oppo,dsi-dc-brightness",
					arr_32, length);
	if (rc) {
		DSI_ERR("[%s] cannot read dsi-dc-brightness\n", panel->name);
		goto error_free_arr_32;
	}

	count = length / 2;
	size = count * sizeof(*seq);
	seq = kzalloc(size, GFP_KERNEL);
	if (!seq) {
		rc = -ENOMEM;
		goto error_free_arr_32;
	}

	panel->dc_ba_seq = seq;
	panel->dc_ba_count = count;

	for (i = 0; i < length; i += 2) {
		seq->brightness = arr_32[i];
		seq->alpha = arr_32[i + 1];
		seq++;
	}

error_free_arr_32:
	kfree(arr_32);
error:
	return rc;
}

int sde_connector_update_backlight(struct drm_connector *connector, bool post)
{
	struct sde_connector *c_conn = to_sde_connector(connector);
	struct dsi_display *dsi_display;

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI)
		return 0;

	dsi_display = c_conn->display;
	if (!dsi_display || !dsi_display->panel || !dsi_display->panel->cur_mode) {
		SDE_ERROR("Invalid params(s) dsi_display %pK, panel %pK\n",
				dsi_display,
				((dsi_display) ? dsi_display->panel : NULL));
		return -EINVAL;
	}

	if (!connector->state || !connector->state->crtc)
		return 0;

	if (oppo_dimlayer_bl != oppo_dimlayer_bl_enabled) {
		struct sde_connector *c_conn = to_sde_connector(connector);
		struct drm_crtc *crtc = connector->state->crtc;
		struct dsi_panel *panel = dsi_display->panel;
		int bl_lvl = dsi_display->panel->bl_config.bl_level;
		u32 current_vblank;
		int on_vblank = 0;
		int off_vblank = 0;
		int vblank = 0;
		int ret = 0;
		int vblank_get = -EINVAL;
		int on_delay = 0, on_delay_after = 0;
		int off_delay = 0, off_delay_after = 0;
		int delay = 0, delay_after = 0;

		if (sde_crtc_get_fingerprint_mode(crtc->state)) {
			oppo_dimlayer_bl_enabled = oppo_dimlayer_bl;
			goto done;
		}

		if (bl_lvl < panel->cur_mode->priv_info->fod_th_brightness) {
				on_vblank = panel->cur_mode->priv_info->fod_on_vblank;
				off_vblank = panel->cur_mode->priv_info->fod_off_vblank;
				on_delay = panel->cur_mode->priv_info->fod_on_delay;
				off_delay = panel->cur_mode->priv_info->fod_off_delay;
		} else {
				on_vblank = panel->cur_mode->priv_info->fod_on_vblank_above_th;
				off_vblank = panel->cur_mode->priv_info->fod_off_vblank_above_th;
				on_delay = panel->cur_mode->priv_info->fod_on_delay_above_th;
				off_delay = panel->cur_mode->priv_info->fod_off_delay_above_th;
		}

		if (oppo_dimlayer_bl_on_vblank != INT_MAX)
			on_vblank = oppo_dimlayer_bl_on_vblank;

		if (oppo_dimlayer_bl_off_vblank != INT_MAX)
			off_vblank = oppo_dimlayer_bl_off_vblank;


		if (oppo_dimlayer_bl) {
			vblank = on_vblank;
			delay = on_delay;
			delay_after = on_delay_after;
		} else {
			vblank = off_vblank;
			delay = off_delay;
			delay_after = off_delay_after;
		}

		if (oppo_dimlayer_bl_delay >= 0)
			delay = oppo_dimlayer_bl_delay;

		if (oppo_dimlayer_bl_delay_after >= 0)
			delay_after = oppo_dimlayer_bl_delay_after;

		vblank_get = drm_crtc_vblank_get(crtc);
		if (vblank >= 0) {
			if (!post) {
				oppo_dimlayer_bl_enabled = oppo_dimlayer_bl;
				current_vblank = drm_crtc_vblank_count(crtc);
				ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
						current_vblank != drm_crtc_vblank_count(crtc),
						msecs_to_jiffies(34));
				current_vblank = drm_crtc_vblank_count(crtc) + vblank;
				if (delay > 0)
					usleep_range(delay, delay + 100);
				_sde_connector_update_bl_scale_(c_conn);
				if (delay_after)
					usleep_range(delay_after, delay_after + 100);
				if (vblank > 0) {
					ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
							current_vblank == drm_crtc_vblank_count(crtc),
							msecs_to_jiffies(17 * 3));
				}
			}
		} else {
			if (!post) {
				current_vblank = drm_crtc_vblank_count(crtc);
				ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
						current_vblank != drm_crtc_vblank_count(crtc),
						msecs_to_jiffies(34));
			} else {
				if (vblank < -1) {
					current_vblank = drm_crtc_vblank_count(crtc) + 1 - vblank;
					ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
							current_vblank == drm_crtc_vblank_count(crtc),
							msecs_to_jiffies(17 * 3));
				}
				oppo_dimlayer_bl_enabled = oppo_dimlayer_bl;

				if (delay > 0)
					usleep_range(delay, delay + 100);
				_sde_connector_update_bl_scale_(c_conn);
				if (delay_after)
					usleep_range(delay_after, delay_after + 100);
			}
		}
		if (!vblank_get)
			drm_crtc_vblank_put(crtc);
	}

	if (oppo_dimlayer_bl_enable_v2 != oppo_dimlayer_bl_enable_v2_real) {
		struct sde_connector *c_conn = to_sde_connector(connector);

		oppo_dimlayer_bl_enable_v2_real = oppo_dimlayer_bl_enable_v2;
		_sde_connector_update_bl_scale_(c_conn);
	}

done:
	if (post) {
		if (oppo_datadimming_vblank_count> 0) {
			oppo_datadimming_vblank_count--;
		} else {
			while (atomic_read(&oppo_datadimming_vblank_ref) > 0) {
				drm_crtc_vblank_put(connector->state->crtc);
				atomic_dec(&oppo_datadimming_vblank_ref);
			}
		}
	}

	return 0;
}

/*xupengcheng@MULTIMEDIA.DISPLAY.LCD, 2020/12/29, add for samsung 90fps Global HBM backlight issue*/
extern u32 flag_writ;
int sde_connector_update_hbm(struct drm_connector *connector)
{
	struct sde_connector *c_conn = to_sde_connector(connector);
	struct dsi_display *dsi_display;
	struct sde_connector_state *c_state;
	int rc = 0;
	int fingerprint_mode;

	if (!c_conn) {
		SDE_ERROR("Invalid params sde_connector null\n");
		return -EINVAL;
	}

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI)
		return 0;

	c_state = to_sde_connector_state(connector->state);

	dsi_display = c_conn->display;
	if (!dsi_display || !dsi_display->panel) {
		SDE_ERROR("Invalid params(s) dsi_display %pK, panel %pK\n",
			dsi_display,
			((dsi_display) ? dsi_display->panel : NULL));
		return -EINVAL;
	}

	if (!c_conn->encoder || !c_conn->encoder->crtc ||
	    !c_conn->encoder->crtc->state) {
		return 0;
	}

	fingerprint_mode = sde_crtc_get_fingerprint_mode(c_conn->encoder->crtc->state);

	if (OPPO_DISPLAY_AOD_SCENE == get_oppo_display_scene()) {
		if (sde_crtc_get_fingerprint_pressed(c_conn->encoder->crtc->state)) {
			sde_crtc_set_onscreenfinger_defer_sync(c_conn->encoder->crtc->state, true);
		} else {
			sde_crtc_set_onscreenfinger_defer_sync(c_conn->encoder->crtc->state, false);
			fingerprint_mode = false;
		}
	} else {
		sde_crtc_set_onscreenfinger_defer_sync(c_conn->encoder->crtc->state, false);
	}

	if (fingerprint_mode != dsi_display->panel->is_hbm_enabled) {
		struct drm_crtc *crtc = c_conn->encoder->crtc;
		struct dsi_panel *panel = dsi_display->panel;
		int vblank = 0;
		u32 target_vblank, current_vblank;
		int ret;

		if (oppo_fod_on_vblank >= 0)
			panel->cur_mode->priv_info->fod_on_vblank = oppo_fod_on_vblank;
		if (oppo_fod_off_vblank >= 0)
			panel->cur_mode->priv_info->fod_off_vblank = oppo_fod_off_vblank;

		pr_err("OnscreenFingerprint mode: %s",
		       fingerprint_mode ? "Enter" : "Exit");

		dsi_display->panel->is_hbm_enabled = fingerprint_mode;
		if (fingerprint_mode) {
#ifdef OPLUS_FEATURE_AOD_RAMLESS
/* Yuwei.Zhang@MULTIMEDIA.DISPLAY.LCD, 2020/09/25, sepolicy for aod ramless */
			if (!dsi_display->panel->oppo_priv.is_aod_ramless || oppo_display_mode) {
#endif /* OPLUS_FEATURE_AOD_RAMLESS */
				mutex_lock(&dsi_display->panel->panel_lock);

				if (!dsi_display->panel->panel_initialized) {
					dsi_display->panel->is_hbm_enabled = false;
					pr_err("panel not initialized, failed to Enter OnscreenFingerprint\n");
					mutex_unlock(&dsi_display->panel->panel_lock);
					return 0;
				}
				dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
						DSI_CORE_CLK, DSI_CLK_ON);

				if (oppo_seed_backlight) {
					int frame_time_us;

					frame_time_us = mult_frac(1000, 1000, panel->cur_mode->timing.refresh_rate);
					oppo_panel_process_dimming_v2(panel, panel->bl_config.bl_level, true);
					mipi_dsi_dcs_set_display_brightness(&panel->mipi_device, panel->bl_config.bl_level);
					oppo_panel_process_dimming_v2_post(panel, true);
					usleep_range(frame_time_us, frame_time_us + 100);
				}
#ifdef OPLUS_FEATURE_AOD_RAMLESS
/* Yuwei.Zhang@MULTIMEDIA.DISPLAY.LCD, 2020/09/25, sepolicy for aod ramless */
				else if (dsi_display->panel->oppo_priv.is_aod_ramless) {
					ktime_t delta = ktime_sub(ktime_get(), oppo_backlight_time);
					s64 delta_us = ktime_to_us(delta);
					if (delta_us < 34000 && delta_us >= 0)
						usleep_range(34000 - delta_us, 34000 - delta_us + 100);
				}
#endif /* OPLUS_FEATURE_AOD_RAMLESS */

				if (OPPO_DISPLAY_AOD_SCENE != get_oppo_display_scene() &&
						dsi_display->panel->bl_config.bl_level) {
					if (dsi_display->config.panel_mode != DSI_OP_VIDEO_MODE) {
						current_vblank = drm_crtc_vblank_count(crtc);
						ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
								current_vblank != drm_crtc_vblank_count(crtc),
								msecs_to_jiffies(17));
					}
					if (!strcmp(panel->oppo_priv.vendor_name, "AMS643YE01"))
						usleep_range(4500,4600);
					vblank = panel->cur_mode->priv_info->fod_on_vblank;
					target_vblank = drm_crtc_vblank_count(crtc) + vblank;
					rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_ON);

					if (vblank) {
						ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
								target_vblank == drm_crtc_vblank_count(crtc),
								msecs_to_jiffies((vblank + 1) * 17));
						if (!ret) {
							pr_err("OnscreenFingerprint failed to wait vblank timeout target_vblank=%d current_vblank=%d\n",
									target_vblank, drm_crtc_vblank_count(crtc));
						}
					}
				} else {
					rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_ON);
				}

				dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
						DSI_CORE_CLK, DSI_CLK_OFF);

				mutex_unlock(&dsi_display->panel->panel_lock);
				if (rc) {
					pr_err("failed to send DSI_CMD_HBM_ON cmds, rc=%d\n", rc);
					return rc;
				}
#ifdef OPLUS_FEATURE_AOD_RAMLESS
/* Yuwei.Zhang@MULTIMEDIA.DISPLAY.LCD, 2020/09/25, sepolicy for aod ramless */
			}
#endif /* OPLUS_FEATURE_AOD_RAMLESS */
		} else {
			mutex_lock(&dsi_display->panel->panel_lock);

			if (!dsi_display->panel->panel_initialized) {
				dsi_display->panel->is_hbm_enabled = true;
				pr_err("panel not initialized, failed to Exit OnscreenFingerprint\n");
				mutex_unlock(&dsi_display->panel->panel_lock);
				return 0;
			}

			current_vblank = drm_crtc_vblank_count(crtc);

			ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
					current_vblank != drm_crtc_vblank_count(crtc),
					msecs_to_jiffies(17));

			oppo_skip_datadimming_sync = true;
			oppo_panel_update_backlight_unlock(panel);
			oppo_skip_datadimming_sync = false;

			vblank = panel->cur_mode->priv_info->fod_off_vblank;
			target_vblank = drm_crtc_vblank_count(crtc) + vblank;

			dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
					     DSI_CORE_CLK, DSI_CLK_ON);
			if(OPPO_DISPLAY_AOD_HBM_SCENE == get_oppo_display_scene()) {
				if (OPPO_DISPLAY_POWER_DOZE_SUSPEND == get_oppo_display_power_status() ||
				    OPPO_DISPLAY_POWER_DOZE == get_oppo_display_power_status()) {
					rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_OFF);
#ifdef OPLUS_FEATURE_AOD_RAMLESS
/* Yuwei.Zhang@MULTIMEDIA.DISPLAY.LCD, 2020/09/25, sepolicy for aod ramless */
					if (dsi_display->panel->oppo_priv.is_aod_ramless) {
						oppo_update_aod_light_mode_unlock(panel);
					}
#endif /* OPLUS_FEATURE_AOD_RAMLESS */
					set_oppo_display_scene(OPPO_DISPLAY_AOD_SCENE);
				} else {
					rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_SET_NOLP);
					/* set nolp would exit hbm, restore when panel status on hbm */
					if(panel->bl_config.bl_level > panel->bl_config.brightness_normal_max_level)
						rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER_SWITCH);
					oppo_panel_update_backlight_unlock(panel);
					if (oppo_display_get_hbm_mode()) {
						rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_ON);
					}
					set_oppo_display_scene(OPPO_DISPLAY_NORMAL_SCENE);
				}
			} else if (oppo_display_get_hbm_mode()) {
				/* Do nothing to skip hbm off */
			} else if (OPPO_DISPLAY_AOD_SCENE == get_oppo_display_scene()) {
				rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_OFF);
#ifdef OPLUS_FEATURE_AOD_RAMLESS
/* Yuwei.Zhang@MULTIMEDIA.DISPLAY.LCD, 2020/09/25, sepolicy for aod ramless */
				if (dsi_display->panel->oppo_priv.is_aod_ramless) {
					oppo_update_aod_light_mode_unlock(panel);
				}
#endif /* OPLUS_FEATURE_AOD_RAMLESS */
			} else {
				rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_HBM_OFF);
				if(panel->bl_config.bl_level > panel->bl_config.brightness_normal_max_level)
					rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER_SWITCH);
				dsi_panel_set_backlight(panel, panel->bl_config.bl_level);
			}
			/*xupengcheng@MULTIMEDIA.DISPLAY.LCD, 2020/12/29, add for samsung 90fps Global HBM backlight issue*/
			flag_writ = 3;
			dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
					     DSI_CORE_CLK, DSI_CLK_OFF);
			mutex_unlock(&dsi_display->panel->panel_lock);
			if (vblank) {
				ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
						target_vblank == drm_crtc_vblank_count(crtc),
						msecs_to_jiffies((vblank + 1) * 17));
				if (!ret) {
					pr_err("OnscreenFingerprint failed to wait vblank timeout target_vblank=%d current_vblank=%d\n",
							target_vblank, drm_crtc_vblank_count(crtc));
				}
			}
		}
	}

	if (oppo_dimlayer_hbm_vblank_count > 0) {
		oppo_dimlayer_hbm_vblank_count--;
	} else {
		while (atomic_read(&oppo_dimlayer_hbm_vblank_ref) > 0) {
			drm_crtc_vblank_put(connector->state->crtc);
			atomic_dec(&oppo_dimlayer_hbm_vblank_ref);
		}
	}

	return 0;
}

int oppo_seed_bright_to_alpha(int brightness)
{
	struct dsi_display *display = get_main_display();
	struct oppo_brightness_alpha *lut = NULL;
	int count = 0;
	int i = 0;
	int alpha;

	if (!display)
		return 0;

	if (oppo_panel_alpha)
		return oppo_panel_alpha;

	if (display->panel->dc_ba_seq && display->panel->dc_ba_count) {
		count = display->panel->dc_ba_count;
		lut = display->panel->dc_ba_seq;
	} else {
		count = ARRAY_SIZE(brightness_seed_alpha_lut_dc);
		lut = brightness_seed_alpha_lut_dc;
	}

	for (i = 0; i < count; i++) {
		if (lut[i].brightness >= brightness)
			break;
	}

	if (i == 0)
		alpha = lut[0].alpha;
	else if (i == count)
		alpha = lut[count - 1].alpha;
	else
		alpha = interpolate(brightness, lut[i-1].brightness,
				    lut[i].brightness, lut[i-1].alpha,
				    lut[i].alpha);

	return alpha;
}
struct dsi_panel_cmd_set *
oppo_dsi_update_seed_backlight(struct dsi_panel *panel, int brightness,
				enum dsi_cmd_set_type type)
{
	enum dsi_cmd_set_state state;
	struct dsi_cmd_desc *cmds;
	struct dsi_cmd_desc *oppo_cmd;
	u8 *tx_buf;
	int count, rc = 0;
	int i = 0;
	int k = 0;
	int alpha = oppo_seed_bright_to_alpha(brightness);
	int seed_bl_max = panel->oppo_priv.seed_bl_max;

	if (type != DSI_CMD_SEED_MODE0 &&
		type != DSI_CMD_SEED_MODE1 &&
		type != DSI_CMD_SEED_MODE2 &&
		type != DSI_CMD_SEED_MODE3 &&
		type != DSI_CMD_SEED_MODE4 &&
		type != DSI_CMD_SEED_OFF) {
		return NULL;
	}

	if (type == DSI_CMD_SEED_OFF)
		type = DSI_CMD_SEED_MODE0;

	cmds = panel->cur_mode->priv_info->cmd_sets[type].cmds;
	count = panel->cur_mode->priv_info->cmd_sets[type].count;
	state = panel->cur_mode->priv_info->cmd_sets[type].state;

	oppo_cmd = kmemdup(cmds, sizeof(*cmds) * count, GFP_KERNEL);
	if (!oppo_cmd) {
		rc = -ENOMEM;
		goto error;
	}

	for (i = 0; i < count; i++)
		oppo_cmd[i].msg.tx_buf = NULL;

	for (i = 0; i < count; i++) {
		u32 size;

		size = oppo_cmd[i].msg.tx_len * sizeof(u8);

		oppo_cmd[i].msg.tx_buf = kmemdup(cmds[i].msg.tx_buf, size, GFP_KERNEL);
		if (!oppo_cmd[i].msg.tx_buf) {
			rc = -ENOMEM;
			goto error;
		}
	}

	for (i = 0; i < count; i++) {
		if (oppo_cmd[i].msg.tx_len != 0x16)
			continue;
		tx_buf = (u8 *)oppo_cmd[i].msg.tx_buf;
		for (k = 0; k < oppo_cmd[i].msg.tx_len; k++) {
			if (k == 0) {
				continue;
			}
			tx_buf[k] = tx_buf[k] * (seed_bl_max - alpha) / seed_bl_max;
		}
	}

	if (oppo_priv_seed_cmd_set.cmds) {
		for (i = 0; i < oppo_priv_seed_cmd_set.count; i++)
			kfree(oppo_priv_seed_cmd_set.cmds[i].msg.tx_buf);
		kfree(oppo_priv_seed_cmd_set.cmds);
	}

	oppo_priv_seed_cmd_set.cmds = oppo_cmd;
	oppo_priv_seed_cmd_set.count = count;
	oppo_priv_seed_cmd_set.state = state;
	oppo_dc2_alpha = alpha;

	return &oppo_priv_seed_cmd_set;

error:
	if (oppo_cmd) {
		for (i = 0; i < count; i++)
			kfree(oppo_cmd[i].msg.tx_buf);
		kfree(oppo_cmd);
	}
	return ERR_PTR(rc);
}

int oppo_display_panel_get_dim_alpha(void *buf) {
	unsigned int *temp_alpha = buf;
	struct dsi_display *display = get_main_display();

	if (!display->panel->is_hbm_enabled ||
		(get_oppo_display_power_status() != OPPO_DISPLAY_POWER_ON)) {
		(*temp_alpha) = 0;
		return 0;
	}

	(*temp_alpha) = oppo_underbrightness_alpha;
	return 0;
}

int oppo_display_panel_set_dim_alpha(void *buf) {
	unsigned int *temp_alpha = buf;

	(*temp_alpha) = oppo_panel_alpha;

	return 0;
}

int oppo_display_panel_get_dim_dc_alpha(void *buf) {
	int ret = 0;
	unsigned int *temp_dim_alpha = buf;
	struct dsi_display *display = get_main_display();

	if (display->panel->is_hbm_enabled ||
		get_oppo_display_power_status() != OPPO_DISPLAY_POWER_ON) {
		ret = 0;
	}
	if (oppo_dc2_alpha != 0) {
		ret = oppo_dc2_alpha;
	} else if (oppo_underbrightness_alpha != 0) {
		ret = oppo_underbrightness_alpha;
	} else if (oppo_dimlayer_bl_enable_v3_real) {
		ret = 1;
	}

	(*temp_dim_alpha) = ret;
	return 0;
}
