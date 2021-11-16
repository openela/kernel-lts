/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include <linux/syscalls.h>
#include <linux/unistd.h>

extern struct proc_dir_entry *sensor_proc_dir;
struct oplus_press_cali_data {
    int offset;
    struct proc_dir_entry       *proc_oplus_press;
};
static struct oplus_press_cali_data *gdata = NULL;

static ssize_t press_offset_read_proc(struct file *file, char __user *buf,
    size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;

    if (!gdata) {
        return -ENOMEM;
    }

    len = sprintf(page, "%d", gdata->offset);

    if (len > *off) {
        len -= *off;
    } else {
        len = 0;
    }

    if (copy_to_user(buf, page, (len < count ? len : count))) {
        return -EFAULT;
    }

    *off += len < count ? len : count;
    return (len < count ? len : count);
}
static ssize_t press_offset_write_proc(struct file *file, const char __user *buf,
    size_t count, loff_t *off)

{
    char page[256] = {0};
    int input = 0;

    if (!gdata) {
        return -ENOMEM;
    }


    if (count > 256) {
        count = 256;
    }

    if (count > *off) {
        count -= *off;
    } else {
        count = 0;
    }

    if (copy_from_user(page, buf, count)) {
        return -EFAULT;
    }

    *off += count;

    if (sscanf(page, "%d", &input) != 1) {
        count = -EINVAL;
        return count;
    }

    if (input != gdata->offset) {
        gdata->offset = input;
    }

    return count;
}
static struct file_operations press_offset_fops = {
    .read = press_offset_read_proc,
    .write = press_offset_write_proc,
};

int oplus_press_cali_data_init(void)
{
    int rc = 0;
    struct proc_dir_entry *pentry;

    struct oplus_press_cali_data *data = NULL;

    pr_info("%s call\n", __func__);
    if (gdata) {
        printk("%s:just can be call one time\n", __func__);
        return 0;
    }

    data = kzalloc(sizeof(struct oplus_press_cali_data), GFP_KERNEL);

    if (data == NULL) {
        rc = -ENOMEM;
        printk("%s:kzalloc fail %d\n", __func__, rc);
        return rc;
    }

    gdata = data;
    gdata->offset = 0;

    if (gdata->proc_oplus_press) {
        printk("proc_oplus_press has alread inited\n");
        return 0;
    }

    gdata->proc_oplus_press =  proc_mkdir("pressure_cali", sensor_proc_dir);

    if (!gdata->proc_oplus_press) {
        pr_err("can't create proc_oplus_press proc\n");
        rc = -EFAULT;
        goto exit;
    }

    pentry = proc_create("offset", 0666, gdata->proc_oplus_press,
            &press_offset_fops);

    if (!pentry) {
        pr_err("create offset proc failed.\n");
        rc = -EFAULT;
        goto exit;
    }

exit:
    if (rc < 0) {
        kfree(gdata);
        gdata = NULL;
    }

    return rc;
}

void oplus_press_cali_data_clean(void)
{
    if (gdata) {
        kfree(gdata);
        gdata = NULL;
    }
}

//call init api in oplus_sensor_devinfo.c
//due to kernel module only permit one module_init entrance in one .ko
//module_init(oplus_press_cali_data_init);
//module_exit(oplus_press_cali_data_clean);
MODULE_DESCRIPTION("custom version");
MODULE_LICENSE("GPL v2");
