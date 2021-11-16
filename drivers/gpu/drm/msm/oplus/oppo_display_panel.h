/***************************************************************
** Copyright (C),  2020,  OPPO Mobile Comm Corp.,  Ltd
** VENDOR_EDIT
** File : oppo_display_panel.h
** Description : oppo display panel char dev  /dev/oppo_panel
** Version : 1.0
** Date : 2020/06/13
** Author : Li.Sheng@MULTIMEDIA.DISPLAY.LCD
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  Li.Sheng       2020/06/13        1.0           Build this moudle
******************************************************************/
#ifndef _OPPO_DISPLAY_PANEL_H_
#define _OPPO_DISPLAY_PANEL_H_

#include <linux/fs.h>
#include <linux/printk.h>
#include <linux/device.h>
#include <asm/ioctl.h>
#include <linux/err.h>
#include <linux/notifier.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include "oppo_display_panel_power.h"
#include "oppo_display_panel_seed.h"
#include "oppo_display_panel_common.h"
#include "oppo_ffl.h"
#include "oppo_aod.h"
#include "oppo_dc_diming.h"

#define OPPO_PANEL_NAME "oppo_display"
#define OPPO_PANEL_CLASS_NAME "oppo_display_class"

#define OPPO_PANEL_IOCTL_BASE			'o'

#define PANEL_IO(nr)			_IO(OPPO_PANEL_IOCTL_BASE, nr)
#define PANEL_IOR(nr, type)		_IOR(OPPO_PANEL_IOCTL_BASE, nr, type)
#define PANEL_IOW(nr, type)		_IOW(OPPO_PANEL_IOCTL_BASE, nr, type)
#define PANEL_IOWR(nr, type)		_IOWR(OPPO_PANEL_IOCTL_BASE, nr, type)

#define PANEL_IOCTL_NR(n)       _IOC_NR(n)
#define PANEL_IOCTL_SIZE(n)		_IOC_SIZE(n)

typedef int oppo_panel_feature(void *data);

static dev_t dev_num = 0;
static struct class *panel_class;
static struct device *panel_dev;
static int panel_ref = 0;
static struct cdev panel_cdev;

struct panel_ioctl_desc {
	unsigned int cmd;
	oppo_panel_feature *func;
	const char *name;
};

/*oppo ioctl case start*/
#define PANEL_COMMOND_BASE 0x00
#define PANEL_COMMOND_MAX  0x15

#define PANEL_IOCTL_SET_POWER				  PANEL_IOW(0x01, struct panel_vol_set)
#define PANEL_IOCTL_GET_POWER				  PANEL_IOWR(0x02, struct panel_vol_get)
#define PANEL_IOCTL_SET_SEED				  PANEL_IOW(0x03, unsigned int)
#define PANEL_IOCTL_GET_SEED				  PANEL_IOWR(0x04, unsigned int)
#define PANEL_IOCTL_GET_PANELID			      PANEL_IOWR(0x05, struct panel_id)
#define PANEL_IOCTL_SET_FFL				      PANEL_IOW(0x06, unsigned int)
#define PANEL_IOCTL_GET_FFL				      PANEL_IOWR(0x07, unsigned int)
#define PANEL_IOCTL_SET_AOD				      PANEL_IOW(0x08, unsigned int)
#define PANEL_IOCTL_GET_AOD				      PANEL_IOWR(0x09, unsigned int)
#define PANEL_IOCTL_SET_MAX_BRIGHTNESS		  PANEL_IOW(0x0A, unsigned int)
#define PANEL_IOCTL_GET_MAX_BRIGHTNESS		  PANEL_IOWR(0x0B, unsigned int)
#define PANEL_IOCTL_GET_PANELINFO			  PANEL_IOWR(0x0C, struct panel_info)
#define PANEL_IOCTL_GET_CCD				      PANEL_IOWR(0x0D, unsigned int)
#define PANEL_IOCTL_GET_SERIAL_NUMBER		  PANEL_IOWR(0x0E, struct panel_serial_number)
#define PANEL_IOCTL_SET_HBM				      PANEL_IOW(0x0F, unsigned int)
#define PANEL_IOCTL_GET_HBM				      PANEL_IOWR(0x10, unsigned int)
#define PANEL_IOCTL_SET_DIM_ALPHA			  PANEL_IOW(0x11, unsigned int)
#define PANEL_IOCTL_GET_DIM_ALPHA			  PANEL_IOWR(0x12, unsigned int)
#define PANEL_IOCTL_SET_DIM_DC_ALPHA		  PANEL_IOW(0x13, unsigned int)
#define PANEL_IOCTL_GET_DIM_DC_ALPHA		  PANEL_IOWR(0x14, unsigned int)
/*oppo ioctl case end*/

#endif /*_OPPO_DISPLAY_PANEL_H_*/
