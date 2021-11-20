// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************
* Copyright (c)  2008- 2020  Oplus. All rights reserved..
* VENDOR_EDIT
* File       : oplus_kevent_test.c
* Description: For kevent action upload upload to user layer test
* Version   : 1.0
* Date        : 2019-12-19
* Author    :
* TAG         :
****************************************************************/
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/kernel.h>

#include <linux/oplus_kevent.h>
#include <linux/genhd.h>

#ifdef CONFIG_OPLUS_KEVENT_TEST
#define MAX_PAYLOAD_LENGTH  2560
static char *str = NULL;
/*
struct payload
{
	uid_t uid;
}__attribute__((packed));


static int atoi(const char *str)
{
	int num = 0;
	if (strlen(str) < 1)
	{
		return -1;
	}

	while (*str <= '9' && *str >= '0')
	{
		num = num * 10 + *str - '0';
		str++;
	}
	return num;
}

int kevent_proc_show(struct seq_file *m, void *v)
{
	struct kernel_packet_info *user_msg_info;
	struct payload *payload_uid;
	char log_tag[32] = "2100001";
	char event_id[32] = "user_location";
	void* buffer = NULL;
	int size;
	int value;

	seq_printf(m, "%s", str);

	size = sizeof(struct kernel_packet_info) + sizeof(struct payload);
	printk(KERN_INFO "kevent_send_to_user, kevent_proc_show:size=%d\n", size);
	buffer = kmalloc(size, GFP_ATOMIC);
	memset(buffer, 0, size);
	user_msg_info = (struct kernel_packet_info *)buffer;
	user_msg_info->type = 0;

	memcpy(user_msg_info->log_tag, log_tag, strlen(log_tag) + 1);
	memcpy(user_msg_info->event_id, event_id, strlen(event_id) + 1);

	user_msg_info->payload_length = sizeof(struct payload);

	payload_uid = (struct payload *)(buffer + sizeof(struct kernel_packet_info));

	value = atoi(str);

	payload_uid->uid = value;
	printk(KERN_INFO "kevent_send_to_user, kevent_proc_show:value=%d\n", value);
	printk(KERN_INFO "kevent_send_to_user, kevent_proc_show:payload_uid->uid=%d\n", payload_uid->uid);

	//do_mount("/dev/block/bootdevice/by-name/system", "/system", "ext4", ~MS_RDONLY, NULL);

	kevent_send_to_user(user_msg_info);

	msleep(20);

	kfree(buffer);

	return 0;
}
*/

static int kevent_proc_show(struct seq_file *m, void *v)
{
	struct kernel_packet_info *user_msg_info;
	char log_tag[32] = "2100001";
	char event_id[32] = "user_location";
	void* buffer = NULL;
	int len, size;

	seq_printf(m, "%s", str);

	len = strlen(str);

	size = sizeof(struct kernel_packet_info) + len + 1;

	buffer = kmalloc(size, GFP_ATOMIC);
	memset(buffer, 0, size);
	user_msg_info = (struct kernel_packet_info *)buffer;
	user_msg_info->type = 2;

	memcpy(user_msg_info->log_tag, log_tag, strlen(log_tag) + 1);
	memcpy(user_msg_info->event_id, event_id, strlen(event_id) + 1);

	user_msg_info->payload_length = len + 1;
	memcpy(user_msg_info->payload, str, len + 1);

	//kevent_send_to_user(user_msg_info);
	msleep(20);
	kfree(buffer);
	return 0;
}

/*
static int kevent_proc_show(struct seq_file *m, void *v)
{
	struct kernel_packet_info *user_msg_info;
	char log_tag[32] = "2100001";
	char event_id[32] = "user_location";
	void* buffer = NULL;
	int len, size;

	seq_printf(m, "%s", str);

	len = strlen(str);

	size = sizeof(struct kernel_packet_info) + len + 1;
	printk(KERN_INFO "kevent_send_to_user, kevent_proc_show:size=%d\n", size);

	buffer = kmalloc(size, GFP_ATOMIC);
	memset(buffer, 0, size);
	user_msg_info = (struct kernel_packet_info *)buffer;
	user_msg_info->type = 1;

	memcpy(user_msg_info->log_tag, log_tag, strlen(log_tag) + 1);
	memcpy(user_msg_info->event_id, event_id, strlen(event_id) + 1);

	user_msg_info->payload_length = len + 1;
	memcpy(user_msg_info->payload, str, len + 1);

	kevent_send_to_user(user_msg_info);
	msleep(20);
	kfree(buffer);
	return 0;
}
*/
/*
* file_operations->open
*/
static int kevent_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, kevent_proc_show, NULL);
}

/*
* file_operations->write
*/
/*
static ssize_t kevent_proc_write(struct file *file, const char __user *buffer, size_t count, loff_t *f_pos)
{
	char *tmp = kzalloc((count + 1), GFP_KERNEL);
	if (!tmp) {
		return -ENOMEM;
	}

	if (copy_from_user(tmp, buffer, count)) {
		kfree(tmp);
		return -EFAULT;
	}

	str = tmp;

	return count;
}
*/
/*
static ssize_t kevent_proc_write(struct file *file, const char __user *buffer, size_t count, loff_t *f_pos)
{
	char *str = "/dev/block/platform/soc/c0c4000.sdhci/by-name";
	char *tmp = kzalloc(strlen(str) + count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	memcpy(tmp, str, strlen(str));

	if (copy_from_user(tmp + strlen(str), buffer, count)) {
		kfree(tmp);
		return -EFAULT;
	}

	printk("device name=%s\n", tmp);
	do_mount(tmp, buffer, "ext4", ~MS_RDONLY, NULL);

	kfree(tmp);

	return count;
}
*/
static ssize_t kevent_proc_write(struct file *file, const char __user *buffer, size_t count, loff_t *f_pos)
{
	char *tmp = kzalloc((count + 1), GFP_KERNEL);
	char testbuf[32];
	int n = 0;
	struct block_device *bdev = NULL;
	if (!tmp) {
		return -ENOMEM;
	}

	if (put_user('\0', (char __user *)buffer + count - 1)) {
		return -EFAULT;
	}

	if (copy_from_user(tmp, buffer, count)) {
		kfree(tmp);
		return -EFAULT;
	}

	n = strlen(tmp);
	memcpy(testbuf, tmp, n);

	printk(KERN_INFO "kevent_send_to_user,before kevent_proc_write call do_mount\n");
	do_mount("/dev/block/platform/soc/c0c4000.sdhci/by-name/system", buffer, "ext4", ~MS_RDONLY, NULL);

	bdev = lookup_bdev("/dev/block/platform/soc/c0c4000.sdhci/by-name/vendor");
	bdev = lookup_bdev("/dev/block/mmcblk0p67");
	bdev = lookup_bdev("/dev/block/bootdevice/by-name/vendor");
	printk(KERN_INFO "kevent_send_to_user,after kevent_proc_write call do_mount\n");

	str = tmp;
	return count;
}

/*
* file_operations->read
*/
/*
static ssize_t kevent_proc_read(struct file *file, const char __user *buffer, size_t count, loff_t *f_pos)
{
	char *tmp = kzalloc((count + 1), GFP_KERNEL);
	if (!tmp) {
		return -ENOMEM;
	}

	if (copy_to_user(tmp, buffer, count)) {
		kfree(tmp);
		count=-EFAULT;
	}

	str = tmp;
	return count;
}
*/
static struct file_operations kevent_fops = {
	.open = kevent_proc_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = kevent_proc_write,
};

static int __init kevent_init(void)
{
	struct proc_dir_entry* file;

	/* create proc and file_operations */
	file = proc_create("kevent", 0666, NULL, &kevent_fops);
	if (!file) {
		return -ENOMEM;
	}

	str = kzalloc((MAX_PAYLOAD_LENGTH + 1), GFP_KERNEL);
	memset(str, 0, MAX_PAYLOAD_LENGTH);
	strcpy(str, "kevent");

	return 0;
}

static void __exit kevent_exit(void)
{
	remove_proc_entry("kevent", NULL);
	kfree(str);
}

module_init(kevent_init);
module_exit(kevent_exit);

MODULE_LICENSE("GPL");
#endif /* CONFIG_OPLUS_KEVENT_TEST */

