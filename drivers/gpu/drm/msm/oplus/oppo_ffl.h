/***************************************************************
** Copyright (C),  2020,  OPPO Mobile Comm Corp.,  Ltd
** VENDOR_EDIT
** File : oppo_ffl.h
** Description : oppo ffl feature
** Version : 1.0
** Date : 2020/04/23
** Author : Qianxu@MM.Display.LCD Driver
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   Qianxu         2020/04/23        1.0           Build this moudle
******************************************************************/
#ifndef _OPPO_FFL_H_
#define _OPPO_FFL_H_

#include <linux/kthread.h>


void oppo_ffl_set(int enable);

void oppo_ffl_setting_thread(struct kthread_work *work);

void oppo_start_ffl_thread(void);

void oppo_stop_ffl_thread(void);

int oppo_ffl_thread_init(void);

void oppo_ffl_thread_exit(void);

int oppo_display_panel_set_ffl(void *buf);
int oppo_display_panel_get_ffl(void *buf);

#endif /* _OPPO_FFL_H_ */
