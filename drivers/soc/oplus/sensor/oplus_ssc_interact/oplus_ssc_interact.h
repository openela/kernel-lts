
#ifndef __OPLUS_SSC_INTERACTIVE_H__
#define __OPLUS_SSC_INTERACTIVE_H__

#include <linux/miscdevice.h>
#include <linux/kfifo.h>
#ifdef CONFIG_ARM
#include <linux/sched.h>
#else
#include <linux/wait.h>
#endif
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/param.h>
#include <linux/notifier.h>



enum {
    NONE_TYPE = 0,
    LCM_DC_MODE_TYPE,
    LCM_BRIGHTNESS_TYPE,
    MAX_INFO_TYPE,
};


enum {
    LCM_DC_OFF    = 0,
    LCM_DC_ON    = 1
};


struct als_info{
    uint16_t brightness;
    uint16_t dc_mode;
};

struct fifo_frame{
    uint8_t type;
    uint16_t data;
};

struct dvb_coef{
    uint16_t dvb1;
    uint16_t dvb2;
    uint16_t dvb3;
    uint16_t dvb4;
    uint16_t dvb_l2h;
    uint16_t dvb_h2l;

};

struct ssc_interactive{

    struct als_info   a_info;
    struct miscdevice mdev;
    DECLARE_KFIFO_PTR(fifo, struct fifo_frame);
    //struct kfifo fifo;
    spinlock_t   fifo_lock;
    spinlock_t   rw_lock;
    wait_queue_head_t wq;
    struct notifier_block nb;
    struct dvb_coef  m_dvb_coef;
};


#endif
