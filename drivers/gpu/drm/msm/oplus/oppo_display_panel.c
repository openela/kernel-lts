/***************************************************************
** Copyright (C),  2020,  OPPO Mobile Comm Corp.,  Ltd
** VENDOR_EDIT
** File : oppo_display_panel.c
** Description : oppo display panel char dev  /dev/oppo_panel
** Version : 1.0
** Date : 2020/06/13
** Author : Li.Sheng@MULTIMEDIA.DISPLAY.LCD
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  Li.Sheng       2020/06/13        1.0           Build this moudle
******************************************************************/
#include "oppo_display_panel.h"

#define PANEL_IOCTL_DEF(ioctl, _func) \
	[PANEL_IOCTL_NR(ioctl)] = {		\
		.cmd = ioctl,			\
		.func = _func,			\
		.name = #ioctl,			\
	}

static const struct panel_ioctl_desc panel_ioctls[] = {
	PANEL_IOCTL_DEF(PANEL_IOCTL_SET_POWER, oppo_display_panel_set_pwr),
	PANEL_IOCTL_DEF(PANEL_IOCTL_GET_POWER, oppo_display_panel_get_pwr),
	PANEL_IOCTL_DEF(PANEL_IOCTL_SET_SEED, oppo_display_panel_set_seed),
	PANEL_IOCTL_DEF(PANEL_IOCTL_GET_SEED, oppo_display_panel_get_seed),
	PANEL_IOCTL_DEF(PANEL_IOCTL_GET_PANELID, oppo_display_panel_get_id),
	PANEL_IOCTL_DEF(PANEL_IOCTL_SET_FFL, oppo_display_panel_set_ffl),
	PANEL_IOCTL_DEF(PANEL_IOCTL_GET_FFL, oppo_display_panel_get_ffl),
	PANEL_IOCTL_DEF(PANEL_IOCTL_SET_AOD, oppo_panel_set_aod_light_mode),
	PANEL_IOCTL_DEF(PANEL_IOCTL_GET_AOD, oppo_panel_get_aod_light_mode),
	PANEL_IOCTL_DEF(PANEL_IOCTL_SET_MAX_BRIGHTNESS, oppo_display_panel_set_max_brightness),
	PANEL_IOCTL_DEF(PANEL_IOCTL_GET_MAX_BRIGHTNESS, oppo_display_panel_get_max_brightness),
	PANEL_IOCTL_DEF(PANEL_IOCTL_GET_PANELINFO, oppo_display_panel_get_vendor),
	PANEL_IOCTL_DEF(PANEL_IOCTL_GET_CCD, oppo_display_panel_get_ccd_check),
	PANEL_IOCTL_DEF(PANEL_IOCTL_GET_SERIAL_NUMBER, oppo_display_panel_get_serial_number),
	PANEL_IOCTL_DEF(PANEL_IOCTL_SET_HBM, oppo_display_panel_set_hbm),
	PANEL_IOCTL_DEF(PANEL_IOCTL_GET_HBM, oppo_display_panel_get_hbm),
	PANEL_IOCTL_DEF(PANEL_IOCTL_SET_DIM_ALPHA, oppo_display_panel_set_dim_alpha),
	PANEL_IOCTL_DEF(PANEL_IOCTL_GET_DIM_ALPHA, oppo_display_panel_get_dim_alpha),
	PANEL_IOCTL_DEF(PANEL_IOCTL_SET_DIM_DC_ALPHA, oppo_display_panel_set_dim_alpha),
	PANEL_IOCTL_DEF(PANEL_IOCTL_GET_DIM_DC_ALPHA, oppo_display_panel_get_dim_dc_alpha),
};

static int panel_open(struct inode *inode, struct file *filp)
{
	if (panel_ref) {
		pr_err("%s panel has already open\n", __func__);
		return -1;
	}

	++panel_ref;
	try_module_get(THIS_MODULE);

	return 0;
}

static ssize_t panel_read(struct file *filp, char __user *buffer,
		size_t count, loff_t *offset)
{
	pr_err("%s\n", __func__);
	return count;
}

static ssize_t panel_write(struct file *file, const char __user *buffer,
		size_t count, loff_t *f_pos)
{
	pr_err("%s\n", __func__);
	return count;
}

long panel_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned int in_size, out_size, drv_size, ksize;
	unsigned int nr = PANEL_IOCTL_NR(cmd);
	char static_data[128];
	char *kdata = NULL;
	const struct panel_ioctl_desc *ioctl = NULL;
	oppo_panel_feature *func = NULL;
	int retcode = -EINVAL;

	if ((nr >= PANEL_COMMOND_MAX) || (nr <= PANEL_COMMOND_BASE)) {
		pr_err("%s invalid cmd\n", __func__);
		return retcode;
	}

	ioctl = &panel_ioctls[nr];
	func = ioctl->func;
	if (unlikely(!func)) {
		pr_err("%s no function\n", __func__);
		retcode = -EINVAL;
		return retcode;
	}

	in_size = out_size = drv_size = PANEL_IOCTL_SIZE(cmd);
	if ((cmd & ioctl->cmd & IOC_IN) == 0) {
		in_size = 0;
	}
	if ((cmd & ioctl->cmd & IOC_OUT) == 0) {
		out_size = 0;
	}
	ksize = max(max(in_size, out_size), drv_size);

	pr_err("%s pid = %d, cmd = %s\n", __func__, task_pid_nr(current), ioctl->name);

	if (ksize <= sizeof(static_data)) {
		kdata = static_data;
	} else {
		kdata = kmalloc(sizeof(ksize), GFP_KERNEL);
		if (!kdata) {
			retcode = -ENOMEM;
			goto err_panel;
		}
	}

	if (copy_from_user(kdata, (void __user *)arg, in_size) != 0) {
		retcode = -EFAULT;
		goto err_panel;
	}

	if (ksize > in_size) {
		memset(kdata+in_size, 0, ksize-in_size);
	}
	retcode = func(kdata);  /*any lock here?*/

	if (copy_to_user((void __user *)arg, kdata, out_size) != 0) {
		retcode = -EFAULT;
		goto err_panel;
	}

err_panel:
	if (!ioctl) {
		pr_err("%s invalid ioctl\n", __func__);
	}
	if (kdata != static_data) {
		kfree(kdata);
	}
	if (retcode) {
		pr_err("%s pid = %d, retcode = %s\n", __func__, task_pid_nr(current), retcode);
	}
	return retcode;
}

int panel_release(struct inode *inode, struct file *filp)
{
	--panel_ref;
	module_put(THIS_MODULE);
	pr_err("%s\n", __func__);

	return 0;
}

static const struct file_operations panel_ops =
{
	.owner              = THIS_MODULE,
	.open               = panel_open,
	.release            = panel_release,
	.unlocked_ioctl     = panel_ioctl,
	.compat_ioctl       = panel_ioctl,
	.read               = panel_read,
	.write              = panel_write,
};

static int __init oppo_display_panel_init()
{
	int rc = 0;

	printk("%s\n", __func__);

	rc = alloc_chrdev_region(&dev_num, 0, 1, OPPO_PANEL_NAME);
	if (rc < 0) {
		pr_err("%s: failed to alloc chrdev region\n", __func__);
		return rc;
	}

	panel_class = class_create(THIS_MODULE, OPPO_PANEL_CLASS_NAME);
	if (IS_ERR(panel_class)) {
		pr_err("%s class create error\n", __func__);
		goto err_class_create;
	}

	cdev_init(&panel_cdev, &panel_ops);
	rc = cdev_add(&panel_cdev, dev_num, 1);
	if (rc < 0) {
		pr_err("%s: failed to add cdev\n", __func__);
		goto err_cdev_add;
	}

	panel_dev = device_create(panel_class, NULL, dev_num, NULL, OPPO_PANEL_NAME);
	if (IS_ERR(panel_dev)) {
		pr_err("%s device create error\n", __func__);
		goto err_device_create;
	}
	return 0;

err_device_create:
	cdev_del(&panel_cdev);
err_cdev_add:
	class_destroy(panel_class);
err_class_create:
	unregister_chrdev_region(dev_num, 1);

	return rc;
}

void __exit oppo_display_panel_exit(void)
{
	pr_err("%s\n", __func__);

	cdev_del(&panel_cdev);
	device_destroy(panel_class, dev_num);
	class_destroy(panel_class);
	unregister_chrdev_region(dev_num, 1);
}

module_init(oppo_display_panel_init);
module_exit(oppo_display_panel_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Lisheng <>");
