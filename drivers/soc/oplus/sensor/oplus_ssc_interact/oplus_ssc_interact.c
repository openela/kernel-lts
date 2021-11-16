/******************************************************************
** Copyright (C), 2004-2017, OPPO Mobile Comm Corp., Ltd.
** VENDOR_EDIT
** File: - oppo_ssc_interactive.c
** Description: Source file for send lcm info to slpi.
** Version: 1.0
** Date : 2020/04/25
** Author: tangjh@PSW.BSP.Sensor
**
** --------------------------- Revision History: ---------------------
* <version>    <date>        <author>                      <desc>
* Revision 1.0      2020/04/25        tangjh@PSW.BSP.Sensor       Created, send lcm info to slpi
*******************************************************************/

#define pr_fmt(fmt) "<ssc_interactive>" fmt

#include <linux/init.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include "oplus_ssc_interact.h"



#define FIFO_SIZE 32
#define LB_TO_HB_THRD    150

//static DECLARE_KFIFO_PTR(test, struct fifo_frame);

int register_lcdinfo_notifier(struct notifier_block *nb);
int unregister_lcdinfo_notifier(struct notifier_block *nb);

static struct ssc_interactive *g_ssc_cxt = NULL;

static void ssc_interactive_set_fifo(uint8_t type, uint16_t data)
{
    struct fifo_frame fifo_fm;
    struct ssc_interactive *ssc_cxt = g_ssc_cxt;
    int ret =0;
    pr_info("type= %u, data=%d\n", type, data);
    memset(&fifo_fm, 0, sizeof(struct fifo_frame));
    fifo_fm.type = type;
    fifo_fm.data = data;
    ret = kfifo_in_spinlocked(&ssc_cxt->fifo, &fifo_fm, 1, &ssc_cxt->fifo_lock);
    if(ret != 1) {
        pr_err("kfifo is full\n");
    }
    wake_up_interruptible(&ssc_cxt->wq);
}


static void ssc_interactive_set_dc_mode(uint16_t dc_mode)
{
    struct ssc_interactive *ssc_cxt = g_ssc_cxt;
    spin_lock(&ssc_cxt->rw_lock);
    if(dc_mode == ssc_cxt->a_info.dc_mode){
        //pr_info("dc_mode=%d is the same\n", dc_mode);
        spin_unlock(&ssc_cxt->rw_lock);
        return;
    }
    pr_info("start dc_mode=%d\n", dc_mode);
    ssc_cxt->a_info.dc_mode = dc_mode;
    spin_unlock(&ssc_cxt->rw_lock);


    ssc_interactive_set_fifo(LCM_DC_MODE_TYPE, dc_mode);
}


static void ssc_interactive_set_brightness(uint16_t brigtness)
{
    struct ssc_interactive *ssc_cxt = g_ssc_cxt;
    spin_lock(&ssc_cxt->rw_lock);
//    if(brigtness > LB_TO_HB_THRD)
//        brigtness = 1023;
    if(brigtness < ssc_cxt->m_dvb_coef.dvb1) {
        brigtness = ssc_cxt->m_dvb_coef.dvb1 - 1;
    } else if(brigtness < ssc_cxt->m_dvb_coef.dvb2) {
        brigtness = ssc_cxt->m_dvb_coef.dvb2 - 1;
    } else if(brigtness < ssc_cxt->m_dvb_coef.dvb3) {
        brigtness = ssc_cxt->m_dvb_coef.dvb3 - 1;
    } else if (brigtness >= ssc_cxt->m_dvb_coef.dvb_l2h) {
        brigtness = 1023;
    } else {
        //do noting
        spin_unlock(&ssc_cxt->rw_lock);
        return;
    }


    if(brigtness == ssc_cxt->a_info.brightness){
        //pr_info("brigtness=%d is the same\n", brigtness);
        spin_unlock(&ssc_cxt->rw_lock);
        return;
    }
    pr_info("brigtness=%d brightness=%d\n", brigtness, ssc_cxt->a_info.brightness);

    ssc_cxt->a_info.brightness = brigtness;
    spin_unlock(&ssc_cxt->rw_lock);

    ssc_interactive_set_fifo(LCM_BRIGHTNESS_TYPE, brigtness);
}

static ssize_t ssc_interactive_write(struct file *file, const char __user * buf,
                size_t count, loff_t * ppos)
{
    struct ssc_interactive *ssc_cxt = g_ssc_cxt;
    pr_info("ssc_interactive_write start\n");
    if (count > *ppos) {
        count -= *ppos;
    } else
        count = 0;

    *ppos += count;
    wake_up_interruptible(&ssc_cxt->wq);
    return count;
}

static unsigned int ssc_interactive_poll(struct file *file, struct poll_table_struct *pt)
{
    unsigned int ptr = 0;
    int count = 0;
    struct ssc_interactive *ssc_cxt = g_ssc_cxt;

    poll_wait(file, &ssc_cxt->wq, pt);
    spin_lock(&ssc_cxt->fifo_lock);
    count = kfifo_len(&ssc_cxt->fifo);
    spin_unlock(&ssc_cxt->fifo_lock);
    if (count > 0) {
        ptr |= POLLIN | POLLRDNORM;
    }
    return ptr;
}

static ssize_t ssc_interactive_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    size_t read = 0;
    int fifo_count = 0;
    int ret;
    struct ssc_interactive *ssc_cxt = g_ssc_cxt;

    if(count !=0 && count < sizeof(struct fifo_frame)){
        pr_err("err count %lu\n", count);
        return -EINVAL;
    }
    while((read + sizeof(struct fifo_frame)) <= count ){
        struct fifo_frame fifo_fm;
        spin_lock(&ssc_cxt->fifo_lock);
        fifo_count = kfifo_len(&ssc_cxt->fifo);
        spin_unlock(&ssc_cxt->fifo_lock);

        if(fifo_count <= 0){
            break;
        }
        ret = kfifo_out(&ssc_cxt->fifo, &fifo_fm, 1);
        if(copy_to_user(buf+read, &fifo_fm, sizeof(struct fifo_frame))){
            pr_err("copy_to_user failed \n");
            return -EFAULT;
        }
        read += sizeof(struct fifo_frame);
    }
    *ppos += read;
    return read;
}

static int ssc_interactive_release (struct inode *inode, struct file *file)
{
    pr_info("%s\n", __func__);
    return 0;
}

static const struct file_operations under_mdevice_fops = {
    .owner  = THIS_MODULE,
    .read   = ssc_interactive_read,
    .write        = ssc_interactive_write,
    .poll        = ssc_interactive_poll,
    .llseek = generic_file_llseek,
    .release = ssc_interactive_release,
};

static ssize_t brightness_store(struct device *dev,
        struct device_attribute *attr, const char *buf,
        size_t count)
{
    uint8_t type = 0 ;
    uint16_t data =0;
    int err = 0;

    err = sscanf(buf, "%hhu %hu", &type, &data);
    pr_info("brightness_store2 start = %s, brightness =%d\n", buf, data);
    if (err < 0) {
        pr_err("brightness_store error: err = %d\n", err);
        return err;
    }
    if(type != LCM_BRIGHTNESS_TYPE){
        pr_err("brightness_store type not match  = %d\n", type);
        return count;
    }


    ssc_interactive_set_brightness(data);

    pr_info("brightness_store = %s, brightness =%d\n", buf, data);

    return count;
}

static ssize_t brightness_show(struct device *dev,
                   struct device_attribute *attr, char *buf)
{
    struct ssc_interactive *ssc_cxt = g_ssc_cxt;
    uint16_t brightness = 0;

    spin_lock(&ssc_cxt->rw_lock);
    brightness = ssc_cxt->a_info.brightness;
    spin_unlock(&ssc_cxt->rw_lock);

    pr_info("brightness_show brightness=  %d\n", brightness);

    return sprintf(buf, "%d\n", brightness);
}


static ssize_t dc_mode_store(struct device *dev,
        struct device_attribute *attr, const char *buf,
        size_t count)
{
    uint8_t type = 0 ;
    uint16_t data =0;
    int err = 0;

    err = sscanf(buf, "%hhu %hu", &type, &data);
    if (err < 0) {
        pr_err("dc_mode_store error: err = %d\n", err);
        return err;
    }
    if(type != LCM_DC_MODE_TYPE){
        pr_err("dc_mode_store type not match  = %d\n", type);
        return count;
    }
    ssc_interactive_set_dc_mode(data);
    return count;
}

static ssize_t dc_mode_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct ssc_interactive *ssc_cxt = g_ssc_cxt;
    uint16_t dc_mode = 0;

    spin_lock(&ssc_cxt->rw_lock);
    dc_mode = ssc_cxt->a_info.dc_mode;
    spin_unlock(&ssc_cxt->rw_lock);

    pr_info("dc_mode_show dc_mode=  %d\n", dc_mode);

    return snprintf(buf, PAGE_SIZE, "%d\n", dc_mode);
}


DEVICE_ATTR(brightness, 0644, brightness_show, brightness_store);
DEVICE_ATTR(dc_mode, 0644, dc_mode_show, dc_mode_store);



static struct attribute *ssc_interactive_attributes[] = {
    &dev_attr_brightness.attr,
    &dev_attr_dc_mode.attr,
    NULL
};



static struct attribute_group ssc_interactive_attribute_group = {
    .attrs = ssc_interactive_attributes
};

static int lcdinfo_callback(struct notifier_block *nb, unsigned long event,
        void *data)
{

    int val = 0;
    if (!data) {
        return 0;
    }
    val = *(int*)data;
    switch(event) {
        case LCM_DC_MODE_TYPE:
            ssc_interactive_set_dc_mode(val);
            break;
        case LCM_BRIGHTNESS_TYPE:
            ssc_interactive_set_brightness(val);
            break;
        default:
            break;

    }
    return 0;
}

static int __init ssc_interactive_init(void)
{
    int err = 0;
    struct ssc_interactive *ssc_cxt = kzalloc(sizeof(*ssc_cxt), GFP_KERNEL);

    g_ssc_cxt = ssc_cxt;
    if(ssc_cxt == NULL){
        pr_err("kzalloc ssc_cxt failed\n");
        err = -ENOMEM;
        goto alloc_ssc_cxt_failed;
    }
    err = kfifo_alloc(&ssc_cxt->fifo, FIFO_SIZE, GFP_KERNEL);
    if(err){
        pr_err("kzalloc kfifo failed\n");
        err = -ENOMEM;
        goto alloc_fifo_failed;

    }


    spin_lock_init(&ssc_cxt->fifo_lock);
    spin_lock_init(&ssc_cxt->rw_lock);

    init_waitqueue_head(&ssc_cxt->wq);

    memset(&ssc_cxt->mdev, 0 ,sizeof(struct miscdevice));
    ssc_cxt->mdev.minor = MISC_DYNAMIC_MINOR;
    ssc_cxt->mdev.name = "ssc_interactive";
    ssc_cxt->mdev.fops = &under_mdevice_fops;
    if(misc_register(&ssc_cxt->mdev) != 0){
        pr_err("misc_register  mdev failed\n");
        err = -ENODEV;
        goto register_mdevice_failed;
    }
    ssc_cxt->nb.notifier_call = lcdinfo_callback;

    register_lcdinfo_notifier(&ssc_cxt->nb);
    err = sysfs_create_group(&ssc_cxt->mdev.this_device->kobj, &ssc_interactive_attribute_group);
    if (err < 0) {
        pr_err("unable to create ssc_interactive_attribute_group file err=%d\n",err);
        goto sysfs_create_failed;
    }

    ssc_cxt->m_dvb_coef.dvb1         = 180;
    ssc_cxt->m_dvb_coef.dvb2         = 250;
    ssc_cxt->m_dvb_coef.dvb3         = 320;
    ssc_cxt->m_dvb_coef.dvb_l2h    = 350;
    ssc_cxt->m_dvb_coef.dvb_h2l    = 320;


    pr_info("ssc_interactive_init success!\n");

    return 0;
sysfs_create_failed:
    misc_deregister(&ssc_cxt->mdev);
register_mdevice_failed:
    kfifo_free(&ssc_cxt->fifo);
alloc_fifo_failed:
    kfree(ssc_cxt);
alloc_ssc_cxt_failed:
    return err;
}

static void __exit ssc_interactive_exit(void)
{
    struct ssc_interactive *ssc_cxt = g_ssc_cxt;
    sysfs_remove_group(&ssc_cxt->mdev.this_device->kobj, &ssc_interactive_attribute_group);
    unregister_lcdinfo_notifier(&ssc_cxt->nb);
    misc_deregister(&ssc_cxt->mdev);
    kfifo_free(&ssc_cxt->fifo);
    kfree(ssc_cxt);
    ssc_cxt = NULL;
    g_ssc_cxt = NULL;
}

module_init(ssc_interactive_init);
module_exit(ssc_interactive_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JiangHua.Tang <>");
