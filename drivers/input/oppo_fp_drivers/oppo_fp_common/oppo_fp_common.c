// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <soc/oppo/oppo_project.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/of_gpio.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include "../include/oppo_fp_common.h"
extern char *saved_command_line;

#if defined(MTK_PLATFORM)
// #include <sec_boot_lib.h>
#include <linux/uaccess.h>
#elif defined(QCOM_PLATFORM)
#include <linux/uaccess.h>
#else
#include <soc/qcom/smem.h>
#endif

#define FP_GPIO_PREFIX_NODE    "oplus,fp_gpio_"
#define FP_GPIO_NUM_NODE       "oplus,fp_gpio_num"
#define FP_ID_VALUE_NODE       "oplus,fp-id"
#define FP_VENDOR_CHIP_NODE    "vendor-chip"
#define FP_CHIP_NAME_NODE      "chip-name"
#define FP_ENG_MENU_NODE       "eng-menu"

#define CHIP_UNKNOWN           "unknown"
#define ENGINEER_MENU_DEFAULT  "-1,-1"

static struct proc_dir_entry *fp_id_dir = NULL;
static struct proc_dir_entry *lcd_type_dir = NULL;
static char *fp_id_name = "fp_id";
static char *lcd_type = "lcd_type";
static struct proc_dir_entry *oppo_fp_common_dir = NULL;
static char *oppo_fp_common_dir_name = "oppo_fp_common";

static char fp_manu[FP_ID_MAX_LENGTH] = CHIP_UNKNOWN; /* the length of this string should be less than FP_ID_MAX_LENGTH */
static char lcd_manu[FP_ID_MAX_LENGTH] = CHIP_UNKNOWN; /* the length of this string should be less than FP_ID_MAX_LENGTH */

static struct fp_data *fp_data_ptr = NULL;
char g_engineermode_menu_config[ENGINEER_MENU_SELECT_MAXLENTH] = ENGINEER_MENU_DEFAULT;

static DEFINE_MUTEX(opticalfp_handler_lock);
static opticalfp_handler g_opticalfp_irq_handler = NULL;

static int fp_gpio_parse_parent_dts(struct fp_data *fp_data)
{
    int ret = FP_OK;
    int fp_id_index = 0;
    struct device *dev = NULL;
    struct device_node *np = NULL;

    if (!fp_data || !fp_data->dev) {
        ret = -FP_ERROR_GENERAL;
        goto exit;
    }
    dev = fp_data->dev;
    np = dev->of_node;

    ret = of_property_read_u32(np, FP_GPIO_NUM_NODE, &(fp_data->fp_id_amount));
    if (ret) {
        dev_err(fp_data->dev, "the param %s is not found !\n", FP_GPIO_NUM_NODE);
        ret = -FP_ERROR_GENERAL;
        goto exit;
    }

    if(fp_data->fp_id_amount > MAX_ID_AMOUNT) {
        dev_err(fp_data->dev, "id amount (%d)is illegal !\n", fp_data->fp_id_amount);
        ret = -FP_ERROR_GENERAL;
        goto exit;
    }

    dev_info(fp_data->dev, "fp_id_amount: %d\n", fp_data->fp_id_amount);

    for (fp_id_index = 0; fp_id_index < fp_data->fp_id_amount; fp_id_index++) {
        char fp_gpio_current_node[FP_ID_MAX_LENGTH] = {0};
        snprintf(fp_gpio_current_node, FP_ID_MAX_LENGTH - 1, "%s%d", FP_GPIO_PREFIX_NODE, fp_id_index);
        dev_info(fp_data->dev, "fp_gpio_current_node: %s\n", fp_gpio_current_node);
        fp_data->gpio_index[fp_id_index] = of_get_named_gpio(np, fp_gpio_current_node, 0);
        if (fp_data->gpio_index[fp_id_index] < 0) {
            dev_err(fp_data->dev, "the param %s is not found !\n", fp_gpio_current_node);
            ret = -FP_ERROR_GENERAL;
            goto exit;
        }
        fp_data->fp_id[fp_id_index] = gpio_get_value(fp_data->gpio_index[fp_id_index]);
        dev_info(fp_data->dev, "gpio_index: %d,fp_id: %d\n", fp_data->gpio_index[fp_id_index], fp_data->fp_id[fp_id_index]);
    }

exit:
    return ret;
}


static ssize_t fp_id_node_read(struct file *file, char __user *buf, size_t count, loff_t *pos) {
    char page[FP_ID_MAX_LENGTH] = { 0 };
    char *p = page;
    int len = 0;

    p += snprintf(p, FP_ID_MAX_LENGTH - 1, "%s", fp_manu);
    len = p - page;
    if (len > *pos) {
        len -= *pos;
    }
    else {
        len = 0;
    }

    if (copy_to_user(buf, page, len < count ? len  : count)) {
        return -EFAULT;
    }

    *pos = *pos + (len < count ? len  : count);

    return len < count ? len  : count;
}

static ssize_t fp_id_node_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
    size_t local_count;
    if (count <= 0) {
        return 0;
    }
    strncpy(fp_manu, CHIP_UNKNOWN, FP_ID_MAX_LENGTH - 1);

    local_count = (FP_ID_MAX_LENGTH - 1) < count ? (FP_ID_MAX_LENGTH - 1) : count;
    if (copy_from_user(fp_manu , buf, local_count) != 0) {
        dev_err(fp_data_ptr->dev, "write fp manu value fail\n");
        return -EFAULT;
    }
    fp_manu[local_count] = '\0';
    dev_info(fp_data_ptr->dev, "write fp manu = %s\n", fp_manu);
    return count;
}

static struct file_operations fp_id_node_ctrl = {
    .read = fp_id_node_read,
    .write = fp_id_node_write,
};

void opticalfp_irq_handler_register(opticalfp_handler handler) {
    if (handler) {
        mutex_lock(&opticalfp_handler_lock);
        g_opticalfp_irq_handler = handler;
        mutex_unlock(&opticalfp_handler_lock);
    } else {
        pr_err("%s handler is NULL", __func__);
    }
}

int opticalfp_irq_handler(struct fp_underscreen_info* tp_info) {
    if (g_opticalfp_irq_handler) {
        return g_opticalfp_irq_handler(tp_info);
    } else {
        return FP_UNKNOWN;
    }
}
EXPORT_SYMBOL(opticalfp_irq_handler);

static int fp_gpio_parse_child_dts(struct fp_data *fp_data)
{
    int child_node_index = 0;
    int ret = 0;
    int fpsensor_type = FP_UNKNOWN;
    int child_fp_id[MAX_ID_AMOUNT] = {0};
    bool found_matched_sensor = false;
    uint32_t child_amount = 0;
    const char *chip_name = NULL, *eng_menu = NULL;
    struct device *dev = fp_data->dev;
    struct device_node *child = NULL, *np = dev->of_node;

    for_each_available_child_of_node(np, child) {
        child_amount = of_property_count_elems_of_size(child, FP_ID_VALUE_NODE, sizeof(u32));
        if (child_amount != fp_data->fp_id_amount) {
            ret = -FP_ERROR_GENERAL;
            dev_err(fp_data->dev, "amount not equal ! \n");
            goto exit;
        }

        if (child_amount == 0) {
            found_matched_sensor = true;
        }

        for (child_node_index = 0; child_node_index < child_amount; child_node_index++) {
            ret = of_property_read_u32_index(child, FP_ID_VALUE_NODE, child_node_index, &(child_fp_id[child_node_index]));
            if (ret) {
                dev_err(fp_data->dev, "the param %s is not found !\n", FP_ID_VALUE_NODE);
                ret = -FP_ERROR_GENERAL;
                goto exit;

            }
            if (fp_data->fp_id[child_node_index] != child_fp_id[child_node_index]) {
                break;
            }
            if (child_node_index == fp_data->fp_id_amount - 1) {
                found_matched_sensor = true;
            }
        }

        if (found_matched_sensor) {
            ret = of_property_read_u32(child, FP_VENDOR_CHIP_NODE, &fpsensor_type);
            if (ret) {
                dev_err(fp_data->dev, "the param %s is not found !\n", FP_ID_VALUE_NODE);
                ret = -FP_ERROR_GENERAL;
                goto exit;

            }
            ret = of_property_read_string(child, FP_CHIP_NAME_NODE, &chip_name);
            if (ret) {
                dev_err(fp_data->dev, "the param %s is not found !\n", FP_CHIP_NAME_NODE);
                ret = -FP_ERROR_GENERAL;
                goto exit;

            }

            if (strlen(chip_name) <= 0 || strlen(chip_name) >=  FP_ID_MAX_LENGTH) {
                dev_err(fp_data->dev, "the strlen of param %s is illegal !\n", FP_CHIP_NAME_NODE);
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }

            ret = of_property_read_string(child, FP_ENG_MENU_NODE, &eng_menu);
            if (ret) {
                dev_err(fp_data->dev, "the param %s is not found !\n", FP_ENG_MENU_NODE);
                ret = -FP_ERROR_GENERAL;
                goto exit;

            }
            if (strlen(eng_menu) <= 0 || strlen(eng_menu) >=  ENGINEER_MENU_SELECT_MAXLENTH) {
                dev_err(fp_data->dev, "the strlen of param %s is illegal !\n", FP_ENG_MENU_NODE);
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }

            fp_data->fpsensor_type = (fp_vendor_t)fpsensor_type;
            strncpy(fp_manu, chip_name, FP_ID_MAX_LENGTH - 1);
            strncpy(g_engineermode_menu_config, eng_menu, ENGINEER_MENU_SELECT_MAXLENTH - 1);
            dev_info(dev, "fpsensor_type: %d, chip_name: %s, eng_menu: %s\n", fp_data->fpsensor_type, chip_name, eng_menu);
            break;
        }
    }

    if (!found_matched_sensor) {
        strncpy(fp_manu, CHIP_UNKNOWN, FP_ID_MAX_LENGTH - 1);
        ret = -FP_ERROR_GENERAL;
        dev_err(fp_data->dev, "not found sensor ! \n");
        goto exit;
    }

    return FP_OK;
exit :
    return ret;
}

static int fp_register_proc_fs(void)
{
    int ret = FP_OK;
    /*  make the proc /proc/fp_id  */
    fp_id_dir = proc_create(fp_id_name, 0666, NULL, &fp_id_node_ctrl);
    if (fp_id_dir == NULL) {
        ret = -FP_ERROR_GENERAL;
        goto exit;
    }

    return FP_OK;
exit :
    return ret;
}

void read_lcd_type_proc_data(void) {
    if (strstr(saved_command_line, "boe_")){
        strncpy(lcd_manu, "boe", FP_ID_MAX_LENGTH - 1);
    }else if(strstr(saved_command_line, "samsung_")){
        strncpy(lcd_manu, "samsung", FP_ID_MAX_LENGTH - 1);
    } else if (strstr(saved_command_line, "tianma_")) {
        strncpy(lcd_manu, "tm", FP_ID_MAX_LENGTH - 1);
    }else {
        strncpy(lcd_manu, CHIP_UNKNOWN, FP_ID_MAX_LENGTH - 1);
    }
}

static ssize_t lcd_type_node_read(struct file *file, char __user *buf, size_t count, loff_t *pos) {
    char page[FP_ID_MAX_LENGTH] = { 0 };
    char *p = page;
    int len = 0;

    p += snprintf(p, FP_ID_MAX_LENGTH - 1, "%s", lcd_manu);
    len = p - page;
    if (len > *pos) {
        len -= *pos;
    }
    else {
        len = 0;
    }

    if (copy_to_user(buf, page, len < count ? len  : count)) {
        return -EFAULT;
    }

    *pos = *pos + (len < count ? len  : count);

    return len < count ? len  : count;
}

static struct file_operations lcd_type_node_ctrl = {
    .read = lcd_type_node_read,
    .write = NULL,
};


static int lcd_type_register_proc_fs(void)
{
    int ret = FP_OK;
    read_lcd_type_proc_data();
    /*  make the proc /proc/lcd_type*/
    lcd_type_dir = proc_create(lcd_type, 0444, NULL, &lcd_type_node_ctrl);
    if (lcd_type_dir == NULL) {
        ret = -FP_ERROR_GENERAL;
        goto exit;
    }

    return FP_OK;
exit :
    return ret;
}

fp_vendor_t get_fpsensor_type(void)
{
    fp_vendor_t fpsensor_type = FP_UNKNOWN;

    if (NULL == fp_data_ptr) {
        pr_err("%s no device", __func__);
        return FP_UNKNOWN;
    }

    fpsensor_type = fp_data_ptr->fpsensor_type;

    return fpsensor_type;
}

static int oppo_fp_common_probe(struct platform_device *fp_dev)
{
    int ret = 0;
    struct device *dev = &fp_dev->dev;
    struct fp_data *fp_data = NULL;

    fp_data = devm_kzalloc(dev, sizeof(struct fp_data), GFP_KERNEL);
    if (fp_data == NULL) {
        dev_err(dev, "fp_data kzalloc failed\n");
        ret = -ENOMEM;
        goto exit;
    }

    fp_data->dev = dev;
    fp_data_ptr = fp_data;

    //add to get the parent dts oppo_fp_common
    ret = fp_gpio_parse_parent_dts(fp_data);
    if (ret) {
        goto exit;
    }

    //add to get the matching child dts (silead_optical, goodix_optical, etc ...)
    ret = fp_gpio_parse_child_dts(fp_data);
    if (ret) {
        goto exit;
    }

    ret = fp_register_proc_fs();
    if (ret) {
        goto exit;
    }

    ret = lcd_type_register_proc_fs();
    if (ret) {
        goto exit;
    }

    return FP_OK;

exit:

    if (oppo_fp_common_dir) {
        remove_proc_entry(oppo_fp_common_dir_name, NULL);
    }

    if (fp_id_dir) {
        remove_proc_entry(fp_id_name, NULL);
    }

    dev_err(dev, "fp_data probe failed ret = %d\n", ret);
    if (fp_data) {
        devm_kfree(dev, fp_data);
    }

    return ret;
}

static int oppo_fp_common_remove(struct platform_device *pdev)
{
    return FP_OK;
}

static struct of_device_id oppo_fp_common_match_table[] = {
    {   .compatible = "oplus,fp_common", },
    {}
};

static struct platform_driver oppo_fp_common_driver = {
    .probe = oppo_fp_common_probe,
    .remove = oppo_fp_common_remove,
    .driver = {
        .name = "oppo_fp_common",
        .owner = THIS_MODULE,
        .of_match_table = oppo_fp_common_match_table,
    },
};

static int __init oppo_fp_common_init(void)
{
    return platform_driver_register(&oppo_fp_common_driver);
}

static void __exit oppo_fp_common_exit(void)
{
    platform_driver_unregister(&oppo_fp_common_driver);
}

subsys_initcall(oppo_fp_common_init);
module_exit(oppo_fp_common_exit)
