/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
** VENDOR_EDIT
** File : oplus_display_panel_cabc.c
** Description : oplus display panel cabc feature
** Version : 1.0
** Date : 2020/07/06
** Author : xupengcheng@MULTIMEDIA.DISPLAY.LCD
**
** ------------------------------- Revision History: -----------
**  <author>		<data>		<version >		<desc>
**  Li.Sheng	   2020/07/06		1.0		   Build this feature
******************************************************************/
#ifdef OPLUS_FEATURE_LCD_CABC
#include "oplus_display_panel_cabc.h"
#include "oppo_dsi_support.h"

int cabc_mode = 0;
DEFINE_MUTEX(oplus_cabc_lock);

int dsi_panel_cabc_video(struct dsi_panel *panel) {
	int rc = 0;

	if (!panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	if (!dsi_panel_initialized(panel)) {
		rc = -EINVAL;
		goto error;
	}

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_VIDEO);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_CABC_VIDEO cmds, rc=%d\n",
		       panel->name, rc);
	}

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_cabc_image(struct dsi_panel *panel) {
	int rc = 0;

	if (!panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	if (!dsi_panel_initialized(panel)) {
		rc = -EINVAL;
		goto error;
	}

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_IMAGE);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_CABC_IMAGE cmds, rc=%d\n",
		       panel->name, rc);
	}

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_cabc_ui(struct dsi_panel *panel) {
	int rc = 0;

	if (!panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	if (!dsi_panel_initialized(panel)) {
		rc = -EINVAL;
		goto error;
	}

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_UI);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_CABC_UI cmds, rc=%d\n",
		       panel->name, rc);
	}

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_cabc_off(struct dsi_panel *panel) {
	int rc = 0;

	if (!panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	if (!dsi_panel_initialized(panel)) {
		rc = -EINVAL;
		goto error;
	}

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_OFF);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_CABC_OFF cmds, rc=%d\n",
		       panel->name, rc);
	}

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_display_cabc_video(struct dsi_display *display) {
	int rc = 0;
	if (!display || !display->panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

		/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	}

	rc = dsi_panel_cabc_video(display->panel);
		if (rc) {
			pr_err("[%s] failed to dsi_panel_cabc_video, rc=%d\n",
			       display->name, rc);
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_OFF);
	}
	mutex_unlock(&display->display_lock);
	return rc;
}

int dsi_display_cabc_image(struct dsi_display *display) {
	int rc = 0;
	if (!display || !display->panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

		/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	}

	rc = dsi_panel_cabc_image(display->panel);
		if (rc) {
			pr_err("[%s] failed to dsi_panel_cabc_image, rc=%d\n",
			       display->name, rc);
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_OFF);
	}
	mutex_unlock(&display->display_lock);
	return rc;
}

int dsi_display_cabc_ui(struct dsi_display *display) {
	int rc = 0;
	if (!display || !display->panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

		/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	}

	rc = dsi_panel_cabc_ui(display->panel);
		if (rc) {
			pr_err("[%s] failed to dsi_panel_cabc_ui, rc=%d\n",
			       display->name, rc);
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_OFF);
	}
	mutex_unlock(&display->display_lock);
	return rc;
}

int dsi_display_cabc_off(struct dsi_display *display) {
	int rc = 0;
	if (!display || !display->panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

		/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	}

	rc = dsi_panel_cabc_off(display->panel);
		if (rc) {
			pr_err("[%s] failed to dsi_panel_cabc_off, rc=%d\n",
			       display->name, rc);
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_OFF);
	}
	mutex_unlock(&display->display_lock);
	return rc;
}

int __oplus_display_set_cabc(int mode) {
	mutex_lock(&oplus_cabc_lock);
	if(mode != cabc_mode) {
		cabc_mode = mode;
	}
	mutex_unlock(&oplus_cabc_lock);
	return 0;
}
#endif /*OPLUS_FEATURE_LCD_CABC*/
