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
#include "oppo_display_panel_common.h"

int oppo_debug_max_brightness = 0;

extern int dsi_display_read_panel_reg(struct dsi_display *display, u8 cmd,
	void *data, size_t len);

int oppo_display_panel_get_id(void *buf)
{
	struct dsi_display *display = get_main_display();
	int ret = 0;
	unsigned char read[30];
	struct panel_id *panel_rid = buf;

	if (get_oppo_display_power_status() == OPPO_DISPLAY_POWER_ON) {
		if (display == NULL) {
			printk(KERN_INFO "oppo_display_get_panel_id and main display is null");
			ret = -1;
			return ret;
		}

		ret = dsi_display_read_panel_reg(display, 0xDA, read, 1);

		if (ret < 0) {
			pr_err("failed to read DA ret=%d\n", ret);
			return -EINVAL;
		}

		panel_rid->DA = (uint32_t)read[0];

		ret = dsi_display_read_panel_reg(display, 0xDB, read, 1);

		if (ret < 0) {
			pr_err("failed to read DB ret=%d\n", ret);
			return -EINVAL;
		}

		panel_rid->DB = (uint32_t)read[0];

		ret = dsi_display_read_panel_reg(display, 0xDC, read, 1);

		if (ret < 0) {
			pr_err("failed to read DC ret=%d\n", ret);
			return -EINVAL;
		}

		panel_rid->DC = (uint32_t)read[0];

	} else {
		printk(KERN_ERR
			"%s oppo_display_get_panel_id, but now display panel status is not on\n",
			__func__);
		return -EINVAL;
	}

	return ret;
}

int oppo_display_panel_get_max_brightness(void *buf)
{
	uint32_t *max_brightness = buf;

	(*max_brightness) = oppo_debug_max_brightness;

	return 0;
}

int oppo_display_panel_set_max_brightness(void *buf)
{
	uint32_t *max_brightness = buf;

	oppo_debug_max_brightness = (*max_brightness);

	return 0;
}

int oppo_display_panel_get_vendor(void *buf)
{
	struct panel_info *p_info = buf;
	struct dsi_display *display = get_main_display();
	char *vendor = (char *)display->panel->oppo_priv.vendor_name;
	char *manu_name = (char *)display->panel->oppo_priv.manufacture_name;

	if (!display || !display->panel ||
		!display->panel->oppo_priv.vendor_name ||
		!display->panel->oppo_priv.manufacture_name) {
		pr_err("failed to config lcd proc device");
		return -EINVAL;
	}

	memcpy(p_info->version, vendor,
		strlen(vendor) > 31 ? 31 : (strlen(vendor) + 1));
	memcpy(p_info->manufacture, manu_name,
		strlen(manu_name) > 31 ? 31 : (strlen(manu_name) + 1));

	return 0;
}

int oppo_display_panel_get_ccd_check(void *buf)
{
	struct dsi_display *display = get_main_display();
	struct mipi_dsi_device *mipi_device;
	int rc = 0;
	unsigned int *ccd_check = buf;

	if (!display || !display->panel) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (get_oppo_display_power_status() != OPPO_DISPLAY_POWER_ON) {
		printk(KERN_ERR"%s display panel in off status\n", __func__);
		return -EFAULT;
	}

	if (display->panel->panel_mode != DSI_OP_CMD_MODE) {
		pr_err("only supported for command mode\n");
		return -EFAULT;
	}

	if (!(display && display->panel->oppo_priv.vendor_name) ||
		!strcmp(display->panel->oppo_priv.vendor_name, "NT37800")) {
		(*ccd_check) = 0;
		goto end;
	}

	mipi_device = &display->panel->mipi_device;

	mutex_lock(&display->display_lock);
	mutex_lock(&display->panel->panel_lock);

	if (!dsi_panel_initialized(display->panel)) {
		rc = -EINVAL;
		goto unlock;
	}

	rc = dsi_display_cmd_engine_enable(display);

	if (rc) {
		pr_err("%s, cmd engine enable failed\n", __func__);
		goto unlock;
	}

	/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	}

	if (!strcmp(display->panel->oppo_priv.vendor_name, "AMB655UV01")) {
		{
			char value[] = { 0x5A, 0x5A };
			rc = mipi_dsi_dcs_write(mipi_device, 0xF0, value, sizeof(value));
		}

		 {
			char value[] = { 0x44, 0x50 };
			rc = mipi_dsi_dcs_write(mipi_device, 0xE7, value, sizeof(value));
		}
		usleep_range(1000, 1100);

		 {
			char value[] = { 0x03 };
			rc = mipi_dsi_dcs_write(mipi_device, 0xB0, value, sizeof(value));
		}

	} else {
		{
			char value[] = { 0x5A, 0x5A };
			rc = mipi_dsi_dcs_write(mipi_device, 0xF0, value, sizeof(value));
		}

		{
			char value[] = { 0x02 };
			rc = mipi_dsi_dcs_write(mipi_device, 0xB0, value, sizeof(value));
		}

		 {
			char value[] = { 0x44, 0x50 };
			rc = mipi_dsi_dcs_write(mipi_device, 0xCC, value, sizeof(value));
		}
		usleep_range(1000, 1100);

		 {
			char value[] = { 0x05 };
			rc = mipi_dsi_dcs_write(mipi_device, 0xB0, value, sizeof(value));
		}
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_OFF);
	}

	dsi_display_cmd_engine_disable(display);

	mutex_unlock(&display->panel->panel_lock);
	mutex_unlock(&display->display_lock);

	if (!strcmp(display->panel->oppo_priv.vendor_name, "AMB655UV01")) {
		 {
			unsigned char read[10];

			rc = dsi_display_read_panel_reg(display, 0xE1, read, 1);

			pr_err("read ccd_check value = 0x%x rc=%d\n", read[0], rc);
			(*ccd_check) = read[0];
		}

	} else {
		 {
			unsigned char read[10];

			rc = dsi_display_read_panel_reg(display, 0xCC, read, 1);

			pr_err("read ccd_check value = 0x%x rc=%d\n", read[0], rc);
			(*ccd_check) = read[0];
		}
	}

	mutex_lock(&display->display_lock);
	mutex_lock(&display->panel->panel_lock);

	if (!dsi_panel_initialized(display->panel)) {
		rc = -EINVAL;
		goto unlock;
	}

	rc = dsi_display_cmd_engine_enable(display);

	if (rc) {
		pr_err("%s, cmd engine enable failed\n", __func__);
		goto unlock;
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	}

	 {
		char value[] = { 0xA5, 0xA5 };
		rc = mipi_dsi_dcs_write(mipi_device, 0xF0, value, sizeof(value));
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_OFF);
	}

	dsi_display_cmd_engine_disable(display);
unlock:

	mutex_unlock(&display->panel->panel_lock);
	mutex_unlock(&display->display_lock);
end:
	pr_err("[%s] ccd_check = %d\n",  display->panel->oppo_priv.vendor_name,
		(*ccd_check));
	return 0;
}

int oppo_display_panel_get_serial_number(void *buf)
{
	int ret = 0, i;
	unsigned char read[30] = {0};
	struct panel_serial_number *panel_rnum = buf;
	struct dsi_display *display = get_main_display();
	unsigned base_index = 10;

	panel_rnum->date = 0;
	panel_rnum->precise_time = 0;

	if (!display) {
		printk(KERN_INFO
			"oppo_display_get_panel_serial_number and main display is null");
		return -1;
	}

	if (get_oppo_display_power_status() != OPPO_DISPLAY_POWER_ON) {
		printk(KERN_ERR"%s display panel in off status\n", __func__);
		return ret;
	}

	/*
	 * for some unknown reason, the panel_serial_info may read dummy,
	 * retry when found panel_serial_info is abnormal.
	 */
	for (i = 0; i < 10; i++) {
		ret = dsi_display_read_panel_reg(get_main_display(), 0xA1, read, 17);

		if (ret < 0) {
			ret = scnprintf(buf, PAGE_SIZE,
					"Get panel serial number failed, reason:%d", ret);
			msleep(20);
			continue;
		}

		/*  0xA1			   11th		12th	13th	14th	15th
		 *  HEX				0x32		0x0C	0x0B	0x29	0x37
		 *  Bit		   [D7:D4][D3:D0] [D5:D0] [D5:D0] [D5:D0] [D5:D0]
		 *  exp			  3	  2	   C	   B	   29	  37
		 *  Yyyy,mm,dd	  2014   2m	  12d	 11h	 41min   55sec
		 *  panel_rnum.data[24:21][20:16] [15:8]  [7:0]
		 *  panel_rnum:precise_time					  [31:24] [23:16] [reserved]
		*/
		panel_rnum->date =
			(((read[base_index] & 0xF0) << 20)
				| ((read[base_index] & 0x0F) << 16)
				| ((read[base_index + 1] & 0x1F) << 8)
				| (read[base_index + 2] & 0x1F));

		panel_rnum->precise_time =
			(((read[base_index + 3] & 0x3F) << 24)
				| ((read[base_index + 4] & 0x3F) << 16)
				| (read[base_index + 5] << 8)
				| (read[base_index + 6]));

		if (!read[base_index]) {
			/*
			 * the panel we use always large than 2011, so
			 * force retry when year is 2011
			 */
			msleep(20);
			continue;
		}

		break;
	}

	return 0;
}

#ifdef OPLUS_BUG_STABILITY
/* xupengcheng@MULTIMEDIA.DISPLAY.LCD.Feature,2020-10-21 optimize osc adaptive */
int oplus_display_get_panel_parameters(struct dsi_panel *panel,
	struct dsi_parser_utils *utils)
{
	int ret = 0;

	if (!panel || !utils) {
		pr_err("[%s]invalid params\n", __func__);
		return -EINVAL;
	}

	panel->oppo_priv.is_osc_support = utils->read_bool(utils->data, "oplus,osc-support");
	pr_info("[%s]osc mode support: %s", __func__, panel->oppo_priv.is_osc_support ? "Yes" : "Not");

	ret = utils->read_u32(utils->data,
				"oplus,mdss-dsi-osc-clk-mode0-rate",
				&panel->oppo_priv.osc_clk_mode0_rate);
	if (ret) {
		pr_err("[%s]failed get panel parameter: oplus,mdss-dsi-osc-clk-mode0-rate\n",
		       __func__);
		panel->oppo_priv.osc_clk_mode0_rate = 0;
	}
	panel->oppo_priv.osc_clk_current_rate = panel->oppo_priv.osc_clk_mode0_rate;
	pr_err("[%s]osc_clk_mode0_rate=%d\n", __func__, panel->oppo_priv.osc_clk_mode0_rate); //temp

	ret = utils->read_u32(utils->data,
				"oplus,mdss-dsi-osc-clk-mode1-rate",
				&panel->oppo_priv.osc_clk_mode1_rate);
	if (ret) {
		pr_err("[%s]failed get panel parameter: oplus,mdss-dsi-osc-clk-mode0-rate\n",
		       __func__);
		panel->oppo_priv.osc_clk_mode1_rate = 0;
	}
	pr_err("[%s]osc_clk_mode1_rate=%d\n", __func__, panel->oppo_priv.osc_clk_mode1_rate); //temp

	return ret;
}
#endif /* OPLUS_BUG_STABILITY */