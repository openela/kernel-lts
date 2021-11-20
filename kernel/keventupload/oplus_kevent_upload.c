// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************
* Copyright (c)  2008- 2020  Oplus. All rights reserved.
* VENDOR_EDIT
* File       : oplus_kevent_upload.c
* Description: for kevent action upload,root action upload to user layer
* Version   : 1.0
* Date        : 2019-12-19
* Author    :
* TAG         :
****************************************************************/
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <net/net_namespace.h>
#include <linux/proc_fs.h>
#include <net/sock.h>
#include <linux/vmalloc.h>
#include <linux/version.h>
#include <linux/uaccess.h>

#include <linux/oplus_kevent.h>

#ifdef CONFIG_OPLUS_KEVENT_UPLOAD

static struct sock *netlink_fd = NULL;
static volatile u32 kevent_pid;

/* send to user space */
int kevent_send_to_user(struct kernel_packet_info *userinfo)
{
	int ret, size, size_use;
	unsigned int o_tail;
	struct sk_buff *skbuff;
	struct nlmsghdr *nlh;
	struct kernel_packet_info *packet;

	/* protect payload too long problem*/
	if (userinfo->payload_length >= 2048) {
		printk(KERN_ERR "kevent_send_to_user: payload_length out of range\n");
		return -1;
	}

	size = NLMSG_SPACE(sizeof(struct kernel_packet_info) + userinfo->payload_length);

	/*allocate new buffer cache */
	skbuff = alloc_skb(size, GFP_ATOMIC);
	if (skbuff == NULL) {
		printk(KERN_ERR "kevent_send_to_user: skbuff alloc_skb failed\n");
		return -1;
	}

	/* fill in the data structure */
	nlh = nlmsg_put(skbuff, 0, 0, 0, size - sizeof(*nlh), 0);
	if (nlh == NULL) {
		printk(KERN_ERR "nlmsg_put failaure\n");
		nlmsg_free(skbuff);
		return -1;
	}

	o_tail = skbuff->tail;

	size_use = sizeof(struct kernel_packet_info) + userinfo->payload_length;

	/* use struct kernel_packet_info for data of nlmsg */
	packet = NLMSG_DATA(nlh);
	memset(packet, 0, size_use);

	/* copy the payload content */
	memcpy(packet, userinfo, size_use);

	//compute nlmsg length
	nlh->nlmsg_len = skbuff->tail - o_tail;

	/* set control field,sender's pid */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0))
	NETLINK_CB(skbuff).pid = 0;
#else
	NETLINK_CB(skbuff).portid = 0;
#endif

	NETLINK_CB(skbuff).dst_group = 0;

	/* send data */
	ret = netlink_unicast(netlink_fd, skbuff, kevent_pid, MSG_DONTWAIT);
	if(ret < 0){
		//printk(KERN_ERR "kevent_send_to_user:netlink_unicast: can not unicast skbuff\n");
		printk(KERN_ERR "kevent_send_to_user:netlink_unicast: can not unicast skbuff, ret is %d \n", ret);
		return 1;
	}

	return 0;
}

EXPORT_SYMBOL(kevent_send_to_user);

/* kernel receive message from user space */
void kernel_kevent_receive(struct sk_buff *__skbbr)
{
	struct sk_buff *skbu;
	struct nlmsghdr *nlh = NULL;

	skbu = skb_get(__skbbr);

	if (skbu->len >= sizeof(struct nlmsghdr)) {
		nlh = (struct nlmsghdr *)skbu->data;
		if((nlh->nlmsg_len >= sizeof(struct nlmsghdr))
			&& (__skbbr->len >= nlh->nlmsg_len)){
			kevent_pid = nlh->nlmsg_pid;
			printk(KERN_ERR "[KEVENT_UPLOAD]kevent_pid is %u ..\n", kevent_pid);
		}
	}
	kfree_skb(skbu);
}

int __init netlink_kevent_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.groups = 0,
		.input  = kernel_kevent_receive,
	};

	netlink_fd = netlink_kernel_create(&init_net, NETLINK_OPLUS_KEVENT, &cfg);
	if (!netlink_fd) {
		printk(KERN_ERR "[KEVENT_UPLOAD]Can not create a netlink socket\n");
		return -1;
	}
	return 0;
}

void __exit netlink_kevent_exit(void)
{
	sock_release(netlink_fd->sk_socket);
}

module_init(netlink_kevent_init);
module_exit(netlink_kevent_exit);
MODULE_LICENSE("GPL");

#endif /* CONFIG_OPLUS_KEVENT_UPLOAD */

