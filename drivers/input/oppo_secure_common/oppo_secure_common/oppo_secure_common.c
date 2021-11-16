// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/module.h>
#include <linux/proc_fs.h>

//#ifdef OPLUS_FEATURE_SECURITY_COMMON
#if CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6763 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6771 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6885 \
|| CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6785 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6768
#include <sec_boot_lib.h>
#include <linux/uaccess.h>
#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6779
#include <linux/uaccess.h>
//#endif /*OPLUS_FEATURE_SECURITY_COMMON*/
#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 855 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6125 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 7150 \
|| CONFIG_OPPO_BSP_SECCOM_PLATFORM == 7250 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 8250 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 7125
#include <linux/soc/qcom/smem.h>
#else
#include <soc/qcom/smem.h>
#endif

#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/of_gpio.h>

#if CONFIG_OPPO_BSP_SECCOM_PLATFORM == 855 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6125 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 7150 \
|| CONFIG_OPPO_BSP_SECCOM_PLATFORM == 7250 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 8250 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 7125
#include <linux/uaccess.h>
#else
#include <asm/uaccess.h>
#endif

#include <linux/delay.h>
#include <linux/string.h>
#include <linux/err.h>
#include "../include/oppo_secure_common.h"

#define OEM_FUSE_OFF        "0"
#define OEM_FUSE_ON         "1"

#if CONFIG_OPPO_BSP_SECCOM_PLATFORM == 660 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 845 \
|| CONFIG_OPPO_BSP_SECCOM_PLATFORM == 670 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 710  /* sdm660, sdm845, sdm670, sdm710 */
#define OEM_SEC_BOOT_REG 0x780350
#define OEM_SEC_ENABLE_ANTIROLLBACK_REG 0x78019c
#define OEM_SEC_OVERRIDE_1_REG 0x7860C4
#define OEM_OVERRIDE_1_ENABLED_VALUE 0xffffffff

#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 855
#define OEM_SEC_BOOT_REG 0x7804D0
#define OEM_SEC_ENABLE_ANTIROLLBACK_REG 0x78019C
#define OEM_SEC_OVERRIDE_1_REG 0x7860C0
#define OEM_OVERRIDE_1_ENABLED_VALUE 0x1

#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6125
#define OEM_SEC_BOOT_REG 0x1B40458
#define OEM_SEC_ENABLE_ANTIROLLBACK_REG 0x1B401CC
#define OEM_SEC_OVERRIDE_1_REG 0x7860C0
#define OEM_OVERRIDE_1_ENABLED_VALUE 0x1

#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 7125
#define OEM_SEC_BOOT_REG 0x780498
#define OEM_SEC_ENABLE_ANTIROLLBACK_REG 0x7801CC
#define OEM_SEC_OVERRIDE_1_REG 0x7860C0
#define OEM_OVERRIDE_1_ENABLED_VALUE 0x1

#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 7150
#define OEM_SEC_BOOT_REG 0x780460
#define OEM_SEC_ENABLE_ANTIROLLBACK_REG 0x78019C
#define OEM_SEC_OVERRIDE_1_REG 0x7860C0
#define OEM_OVERRIDE_1_ENABLED_VALUE 0x1

#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 7250
#define OEM_SEC_BOOT_REG 0x7805E8
#define OEM_SEC_ENABLE_ANTIROLLBACK_REG 0x7801E4
#define OEM_SEC_OVERRIDE_1_REG 0x7860C0
#define OEM_OVERRIDE_1_ENABLED_VALUE 0x1

#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 8250
#define OEM_SEC_BOOT_REG 0x7805E8
#define OEM_SEC_ENABLE_ANTIROLLBACK_REG 0x7801F4
#define OEM_SEC_OVERRIDE_1_REG 0x7860C0
#define OEM_OVERRIDE_1_ENABLED_VALUE 0x1

#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 8953 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 8976  /* msm8953, 8976pro */
#define OEM_SEC_BOOT_REG 0xA0154
#endif

static struct proc_dir_entry *oppo_secure_common_dir = NULL;
static char* oppo_secure_common_dir_name = "oplus_secure_common";
static struct secure_data *secure_data_ptr = NULL;

secure_type_t get_secureType(void)
{
        secure_type_t secureType = SECURE_BOOT_UNKNOWN;

#if CONFIG_OPPO_BSP_SECCOM_PLATFORM == 660 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 845 \
|| CONFIG_OPPO_BSP_SECCOM_PLATFORM == 670 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 710 \
|| CONFIG_OPPO_BSP_SECCOM_PLATFORM == 855 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6125 \
|| CONFIG_OPPO_BSP_SECCOM_PLATFORM == 7150 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 7250 \
|| CONFIG_OPPO_BSP_SECCOM_PLATFORM == 8250 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 7125
/* sdm660, sdm845, sdm670, sdm710, sdm855 sm6125 sm7125 sm7150 sm7250 sm8250 */

        void __iomem *oem_config_base;
        uint32_t secure_oem_config1 = 0;
        uint32_t secure_oem_config2 = 0;
        oem_config_base = ioremap(OEM_SEC_BOOT_REG, 4);
        secure_oem_config1 = __raw_readl(oem_config_base);
        iounmap(oem_config_base);
        pr_err("secure_oem_config1 0x%x\n", secure_oem_config1);

        oem_config_base = ioremap(OEM_SEC_ENABLE_ANTIROLLBACK_REG, 4);
        secure_oem_config2 = __raw_readl(oem_config_base);
        iounmap(oem_config_base);
        pr_err("secure_oem_config2 0x%x\n", secure_oem_config2);

        if (secure_oem_config1 == 0) {
                secureType = SECURE_BOOT_OFF;
        } else if (secure_oem_config2 == 0) {
                secureType = SECURE_BOOT_ON_STAGE_1;
        } else {
                secureType = SECURE_BOOT_ON_STAGE_2;
        }

#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 8953 /*msm8953*/

        void __iomem *oem_config_base;
        uint32_t secure_oem_config1 = 0;
        oem_config_base = ioremap(OEM_SEC_BOOT_REG, 4);
        secure_oem_config1 = __raw_readl(oem_config_base);
        iounmap(oem_config_base);
        pr_err("secure_oem_config1 0x%x\n", secure_oem_config1);

        if (secure_oem_config1 == 0) {
                secureType = SECURE_BOOT_OFF;
        } else {
                secureType = SECURE_BOOT_ON;
        }

#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 8976 /*msm8976pro*/

        void __iomem *oem_config_base;
        uint32_t secure_oem_config1 = 0;
        oem_config_base = ioremap(OEM_SEC_BOOT_REG, 4);
        secure_oem_config1 = __raw_readl(oem_config_base);
        iounmap(oem_config_base);
        pr_err("secure_oem_config1 0x%x\n", secure_oem_config1);

        if (secure_oem_config1 == 0) {
                secureType = SECURE_BOOT_OFF;
        } else {
                secureType = SECURE_BOOT_ON;
        }

#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6763

        if (g_hw_sbcen == 0) {
                secureType = SECURE_BOOT_OFF;
        } else {
                secureType = SECURE_BOOT_ON;
        }

#elif CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6771

        if (g_hw_sbcen == 0) {
                secureType = SECURE_BOOT_OFF;
        } else {
                secureType = SECURE_BOOT_ON;
        }

#elif  CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6779

        if (strstr(saved_command_line, "androidboot.sbootstate=on")) {
                secureType = SECURE_BOOT_ON;
        } else {
                secureType = SECURE_BOOT_OFF;
        }

#elif  CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6885

        if (g_hw_sbcen == 0) {
                secureType = SECURE_BOOT_OFF;
        } else {
                secureType = SECURE_BOOT_ON;
        }

#elif  CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6785

        if (g_hw_sbcen == 0) {
                secureType = SECURE_BOOT_OFF;
        } else {
                secureType = SECURE_BOOT_ON;
        }

#elif  CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6768

        if (g_hw_sbcen == 0) {
                secureType = SECURE_BOOT_OFF;
        } else {
                secureType = SECURE_BOOT_ON;
        }
#endif

        return secureType;
}

static ssize_t secureType_read_proc(struct file *file, char __user *buf,
                size_t count, loff_t *off)
{
        char page[256] = {0};
        int len = 0;
        secure_type_t secureType = get_secureType();

        len = sprintf(page, "%d", secureType);

        if (len > *off) {
                len -= *off;
        }
        else {
                len = 0;
        }
        if (copy_to_user(buf, page, (len < count ? len : count))) {
                return -EFAULT;
        }
        *off += len < count ? len : count;
        return (len < count ? len : count);
}

static struct file_operations secureType_proc_fops = {
        .read = secureType_read_proc,
};

static ssize_t secureSNBound_read_proc(struct file *file, char __user *buf,
                size_t count, loff_t *off)
{
        char page[256] = {0};
        int len = 0;
        secure_device_sn_bound_state_t secureSNBound_state = SECURE_DEVICE_SN_BOUND_UNKNOWN;

#if CONFIG_OPPO_BSP_SECCOM_PLATFORM == 660 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 845 \
|| CONFIG_OPPO_BSP_SECCOM_PLATFORM == 670 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 710 /* sdm660, sdm845, sdm670, sdm710 */

        void __iomem *oem_config_base;
        uint32_t secure_override1_config = 0;
        oem_config_base = ioremap(OEM_SEC_OVERRIDE_1_REG, 4);
        secure_override1_config = __raw_readl(oem_config_base);
        iounmap(oem_config_base);
        dev_info(secure_data_ptr->dev,"secure_override1_config 0x%x\n", secure_override1_config);

        if (get_secureType() == SECURE_BOOT_ON_STAGE_2 && secure_override1_config != OEM_OVERRIDE_1_ENABLED_VALUE) {
                secureSNBound_state = SECURE_DEVICE_SN_BOUND_OFF; /*secure stage2 devices not bind serial number*/
        } else {
                secureSNBound_state = SECURE_DEVICE_SN_BOUND_ON;  /*secure stage2 devices bind serial number*/
        }


#endif

#if CONFIG_OPPO_BSP_SECCOM_PLATFORM == 855 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 6125 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 7150 \
|| CONFIG_OPPO_BSP_SECCOM_PLATFORM == 7250 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 8250 || CONFIG_OPPO_BSP_SECCOM_PLATFORM == 7125
        if (get_secureType() == SECURE_BOOT_ON_STAGE_2) {
                secureSNBound_state = SECURE_DEVICE_SN_BOUND_OFF;
        }
#endif

        len = sprintf(page, "%d", secureSNBound_state);

        if (len > *off) {
                len -= *off;
        }
        else {
                len = 0;
        }

        if (copy_to_user(buf, page, (len < count ? len : count))) {
                return -EFAULT;
        }
        *off += len < count ? len : count;
        return (len < count ? len : count);
}


static struct file_operations secureSNBound_proc_fops = {
        .read = secureSNBound_read_proc,
};

static int secure_register_proc_fs(struct secure_data *secure_data)
{
        struct proc_dir_entry *pentry;

        /*  make the dir /proc/oppo_secure_common  */
        oppo_secure_common_dir =  proc_mkdir(oppo_secure_common_dir_name, NULL);
        if(!oppo_secure_common_dir) {
                dev_err(secure_data->dev,"can't create oppo_secure_common_dir proc\n");
                return SECURE_ERROR_GENERAL;
        }

        /*  make the proc /proc/oppo_secure_common/secureType  */
        pentry = proc_create("secureType", 0664, oppo_secure_common_dir, &secureType_proc_fops);
        if(!pentry) {
                dev_err(secure_data->dev,"create secureType proc failed.\n");
                return SECURE_ERROR_GENERAL;
        }

        /*  make the proc /proc/oppo_secure_common/secureSNBound  */
        pentry = proc_create("secureSNBound", 0444, oppo_secure_common_dir, &secureSNBound_proc_fops);
        if(!pentry) {
                dev_err(secure_data->dev,"create secureSNBound proc failed.\n");
                return SECURE_ERROR_GENERAL;
        }

        return SECURE_OK;
}

static int oppo_secure_common_probe(struct platform_device *secure_dev)
{
        int ret = 0;
        struct device *dev = &secure_dev->dev;
        struct secure_data *secure_data = NULL;

        secure_data = devm_kzalloc(dev,sizeof(struct secure_data), GFP_KERNEL);
        if (secure_data == NULL) {
                dev_err(dev,"secure_data kzalloc failed\n");
                ret = -ENOMEM;
                goto exit;
        }

        secure_data->dev = dev;
        secure_data_ptr = secure_data;

        ret = secure_register_proc_fs(secure_data);
        if (ret) {
                goto exit;
        }
        return SECURE_OK;

exit:

        if (oppo_secure_common_dir) {
                remove_proc_entry(oppo_secure_common_dir_name, NULL);
        }

        dev_err(dev,"secure_data probe failed ret = %d\n",ret);
        if (secure_data) {
                devm_kfree(dev, secure_data);
        }

        return ret;
}

static int oppo_secure_common_remove(struct platform_device *pdev)
{
        return SECURE_OK;
}

static struct of_device_id oppo_secure_common_match_table[] = {
        {   .compatible = "oppo,secure_common", },
        {}
};

static struct platform_driver oppo_secure_common_driver = {
        .probe = oppo_secure_common_probe,
        .remove = oppo_secure_common_remove,
        .driver = {
                .name = "oplus_secure_common",
                .owner = THIS_MODULE,
                .of_match_table = oppo_secure_common_match_table,
        },
};

static int __init oppo_secure_common_init(void)
{
        return platform_driver_register(&oppo_secure_common_driver);
}

static void __exit oppo_secure_common_exit(void)
{
        platform_driver_unregister(&oppo_secure_common_driver);
}

fs_initcall(oppo_secure_common_init);
module_exit(oppo_secure_common_exit)
