// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt)    KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/timer.h>
#include <linux/err.h>
#include <soc/oppo/oppo_project.h>

#include "gf_spi.h"

#if defined(USE_SPI_BUS)
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#elif defined(USE_PLATFORM_BUS)
#include <linux/platform_device.h>
#endif

static int vreg_setup(struct gf_dev *goodix_fp, fp_power_info_t *pwr_info,
        bool enable)
{
    int rc;
    struct regulator *vreg;
    struct device *dev = &goodix_fp->spi->dev;
    const char *name = NULL;

    if (NULL == pwr_info || NULL == pwr_info->vreg_config.name) {
        pr_err("pwr_info is NULL\n");
        return -EINVAL;
    }
    vreg = pwr_info->vreg;
    name = pwr_info->vreg_config.name;
    pr_err("Regulator %s vreg_setup,enable=%d \n", name, enable);
    if (enable) {
        if (!vreg) {
            vreg = regulator_get(dev, name);
            if (IS_ERR(vreg)) {
                pr_err("Unable to get  %s\n", name);
                return PTR_ERR(vreg);
            }
        }
        if (regulator_count_voltages(vreg) > 0) {
            rc = regulator_set_voltage(vreg, pwr_info->vreg_config.vmin,
                    pwr_info->vreg_config.vmax);
            if (rc) {
                pr_err("Unable to set voltage on %s, %d\n", name, rc);
            }
        }
        rc = regulator_set_load(vreg, pwr_info->vreg_config.ua_load);
        if (rc < 0) {
            pr_err("Unable to set current on %s, %d\n", name, rc);
        }
        rc = regulator_enable(vreg);
        if (rc) {
            pr_err("error enabling %s: %d\n", name, rc);
            regulator_put(vreg);
            vreg = NULL;
        }
        pwr_info->vreg = vreg;
    } else {
        if (vreg) {
            if (regulator_is_enabled(vreg)) {
                regulator_disable(vreg);
                pr_err("disabled %s\n", name);
            }
            regulator_put(vreg);
            pwr_info->vreg = NULL;
        } else {
            pr_err("disable vreg is null \n");
        }
        rc = 0;
    }
    return rc;
}

void gf_cleanup_pwr_list(struct gf_dev* gf_dev) {
    unsigned index = 0;
    pr_err("%s cleanup", __func__);
    for (index = 0; index < gf_dev->power_num; index++) {
        if (gf_dev->pwr_list[index].pwr_type == FP_POWER_MODE_GPIO) {
            gpio_free(gf_dev->pwr_list[index].pwr_gpio);
        }
        pr_info("---- free pwr ok  with mode = %d, index = %d  ----\n",
                gf_dev->pwr_list[index].pwr_type, index);
        memset(&(gf_dev->pwr_list[index]), 0, sizeof(fp_power_info_t));
    }
}

int gf_parse_pwr_list(struct gf_dev* gf_dev) {
    int ret = 0;
    struct device *dev = &gf_dev->spi->dev;
    struct device_node *np = dev->of_node;
    struct device_node *child = NULL;
    unsigned child_node_index = 0;
    int ldo_param_amount = 0;
    const char *node_name = NULL;
    fp_power_info_t *pwr_list = gf_dev->pwr_list;
    /* pwr list init */
    gf_cleanup_pwr_list(gf_dev);

    /* parse each node */
    for_each_available_child_of_node(np, child) {
        if (child_node_index >= FP_MAX_PWR_LIST_LEN) {
            pr_err("too many nodes");
            ret = -FP_ERROR_GENERAL;
            goto exit;
        }

        /* get type of this power */
        ret = of_property_read_u32(child, FP_POWER_NODE, &(pwr_list[child_node_index].pwr_type));
        if (ret) {
            pr_err("failed to request %s, ret = %d\n", FP_POWER_NODE, ret);
            goto exit;
        }

        pr_info("read power type of index %d, type : %u\n", child_node_index, pwr_list[child_node_index].pwr_type);
        switch(pwr_list[child_node_index].pwr_type) {
        case FP_POWER_MODE_LDO:
            /* read ldo supply name */
            ret = of_property_read_string(child, FP_POWER_NAME_NODE, &(pwr_list[child_node_index].vreg_config.name));
            if (ret) {
                pr_err("the param %s is not found !\n", FP_POWER_NAME_NODE);
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }
            pr_debug("get ldo node name %s", pwr_list[child_node_index].vreg_config.name);

            /* read ldo config name */
            ret = of_property_read_string(child, FP_POWER_CONFIG, &node_name);
            if (ret) {
                pr_err("the param %s is not found !\n", FP_POWER_CONFIG);
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }
            pr_debug("get config node name %s", node_name);

            ldo_param_amount = of_property_count_elems_of_size(np, node_name, sizeof(u32));
            pr_debug("get ldo_param_amount %d", ldo_param_amount);
            if(ldo_param_amount != LDO_PARAM_AMOUNT) {
                pr_err("failed to request size %s\n", node_name);
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }

            ret = of_property_read_u32_index(np, node_name, LDO_VMAX_INDEX, &(pwr_list[child_node_index].vreg_config.vmax));
            if (ret) {
                pr_err("failed to request %s(%d), rc = %u\n", node_name, LDO_VMAX_INDEX, ret);
                goto exit;
            }

            ret = of_property_read_u32_index(np, node_name, LDO_VMIN_INDEX, &(pwr_list[child_node_index].vreg_config.vmin));
            if (ret) {
                pr_err("failed to request %s(%d), rc = %u\n", node_name, LDO_VMIN_INDEX, ret);
                goto exit;
            }

            ret = of_property_read_u32_index(np, node_name, LDO_UA_INDEX, &(pwr_list[child_node_index].vreg_config.ua_load));
            if (ret) {
                pr_err("failed to request %s(%d), rc = %u\n", node_name, LDO_UA_INDEX, ret);
                goto exit;
            }

            pr_info("%s size = %d, ua=%d, vmax=%d, vmin=%d\n", node_name, ldo_param_amount,
                    pwr_list[child_node_index].vreg_config.ua_load,
                    pwr_list[child_node_index].vreg_config.vmax,
                    pwr_list[child_node_index].vreg_config.vmin);
            break;
        case FP_POWER_MODE_GPIO:
            /* read GPIO name */
            ret = of_property_read_string(child, FP_POWER_NAME_NODE, &node_name);
            if (ret) {
                pr_err("the param %s is not found !\n", FP_POWER_NAME_NODE);
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }
            pr_info("get config node name %s", node_name);

            /* get gpio by name */
            gf_dev->pwr_list[child_node_index].pwr_gpio = of_get_named_gpio(np, node_name, 0);
            pr_debug("end of_get_named_gpio %s, pwr_gpio: %d!\n", node_name, pwr_list[child_node_index].pwr_gpio);
            if (gf_dev->pwr_list[child_node_index].pwr_gpio < 0) {
                pr_err("falied to get goodix_pwr gpio!\n");
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }

            /* get poweron-level of gpio */
            pr_info("get poweron level: %s", FP_POWERON_LEVEL_NODE);
            ret = of_property_read_u32(child, FP_POWERON_LEVEL_NODE, &pwr_list[child_node_index].poweron_level);
            if (ret) {
                /* property of poweron-level is not config, by default set to 1 */
                pwr_list[child_node_index].poweron_level = 1;
            } else {
                if (pwr_list[child_node_index].poweron_level != 0) {
                    pwr_list[child_node_index].poweron_level = 1;
                }
            }
            pr_info("gpio poweron level: %d\n", pwr_list[child_node_index].poweron_level);

            ret = devm_gpio_request(dev, pwr_list[child_node_index].pwr_gpio, node_name);
            if (ret) {
                pr_err("failed to request %s gpio, ret = %d\n", node_name, ret);
                goto exit;
            }
            gpio_direction_output(pwr_list[child_node_index].pwr_gpio, (pwr_list[child_node_index].poweron_level == 0 ? 1: 0));
            pr_err("set goodix_pwr %u output %d \n", child_node_index, pwr_list[child_node_index].poweron_level);
            break;
        default:
            pr_err("unknown type %u\n", pwr_list[child_node_index].pwr_type);
            ret = -FP_ERROR_GENERAL;
            goto exit;
        }

        /* get delay time of this power */
        ret = of_property_read_u32(child, FP_POWER_DELAY_TIME, &pwr_list[child_node_index].delay);
        if (ret) {
            pr_err("failed to request %s, ret = %d\n", FP_POWER_DELAY_TIME, ret);
            goto exit;
        }
        child_node_index++;
    }
    gf_dev->power_num = child_node_index;
exit:
    if (ret) {
        gf_cleanup_pwr_list(gf_dev);
    }
    return ret;
}

void gf_cleanup_ftm_poweroff_flag(struct gf_dev* gf_dev) {
    gf_dev->ftm_poweroff_flag = 0;
    pr_err("%s cleanup", __func__);
}

int gf_parse_ftm_poweroff_flag(struct gf_dev* gf_dev) {
    int ret = 0;
    struct device *dev = &gf_dev->spi->dev;
    struct device_node *np = dev->of_node;
    uint32_t ftm_poweroff_flag = 0;

    gf_cleanup_ftm_poweroff_flag(gf_dev);

    ret = of_property_read_u32(np, FP_FTM_POWEROFF_FLAG, &ftm_poweroff_flag);
    if (ret) {
        pr_err("failed to request %s, ret = %d\n", FP_FTM_POWEROFF_FLAG, ret);
        goto exit;
    }
    gf_dev->ftm_poweroff_flag = ftm_poweroff_flag;
    pr_err("gf_dev->ftm_poweroff_flag = %d\n", gf_dev->ftm_poweroff_flag);

exit:
    if (ret) {
        gf_cleanup_ftm_poweroff_flag(gf_dev);
    }
    return ret;
}


void gf_cleanup_notify_tpinfo_flag(struct gf_dev* gf_dev) {
    gf_dev->notify_tpinfo_flag = 0;
    pr_err("%s cleanup", __func__);
}

int gf_parse_notify_tpinfo_flag(struct gf_dev* gf_dev) {
    int ret = 0;
    struct device *dev = &gf_dev->spi->dev;
    struct device_node *np = dev->of_node;
    uint32_t notify_tpinfo_flag = 0;

    gf_cleanup_notify_tpinfo_flag(gf_dev);

    ret = of_property_read_u32(np, FP_NOTIFY_TPINFO_FLAG, &notify_tpinfo_flag);
    if (ret) {
        pr_err("failed to request %s, ret = %d\n", FP_NOTIFY_TPINFO_FLAG, ret);
        goto exit;
    }
    gf_dev->notify_tpinfo_flag = notify_tpinfo_flag;
    pr_err("gf_dev->notify_tpinfo_flag = %d\n", gf_dev->notify_tpinfo_flag);

exit:
    if (ret) {
        gf_cleanup_notify_tpinfo_flag(gf_dev);
    }
    return ret;
}

int gf_parse_dts(struct gf_dev* gf_dev)
{
    int rc = 0;
    struct device *dev = &gf_dev->spi->dev;
    struct device_node *np = dev->of_node;

    gf_dev->reset_gpio = of_get_named_gpio(np, "goodix,gpio_reset", 0);
    if (gf_dev->reset_gpio < 0) {
        pr_err("falied to get reset gpio!\n");
        return gf_dev->reset_gpio;
    }

    rc = devm_gpio_request(dev, gf_dev->reset_gpio, "goodix_reset");
    if (rc) {
        pr_err("failed to request reset gpio, rc = %d\n", rc);
        goto err_reset;
    }
    gpio_direction_output(gf_dev->reset_gpio, 0);

    gf_dev->irq_gpio = of_get_named_gpio(np, "goodix,gpio_irq", 0);
    if (gf_dev->irq_gpio < 0) {
        pr_err("falied to get irq gpio!\n");
        return gf_dev->irq_gpio;
    }

    rc = devm_gpio_request(dev, gf_dev->irq_gpio, "goodix_irq");
    if (rc) {
        pr_err("failed to request irq gpio, rc = %d\n", rc);
        goto err_irq;
    }
    gpio_direction_input(gf_dev->irq_gpio);

    rc = gf_parse_pwr_list(gf_dev);
    if (rc) {
        pr_err("failed to parse power list, rc = %d\n", rc);
        goto err_pwr;
    }
    pr_err("end gf_parse_dts !\n");

    gf_parse_notify_tpinfo_flag(gf_dev);

    pr_err("end gf_parse_dts !\n");

    return rc;

err_pwr:
    gf_cleanup_pwr_list(gf_dev);
err_irq:
    devm_gpio_free(dev, gf_dev->reset_gpio);
err_reset:
    return rc;
}

void gf_cleanup(struct gf_dev *gf_dev)
{
    pr_info("[info] %s\n",__func__);
    if (gpio_is_valid(gf_dev->irq_gpio))
    {
        gpio_free(gf_dev->irq_gpio);
        pr_info("remove irq_gpio success\n");
    }
    if (gpio_is_valid(gf_dev->reset_gpio))
    {
        gpio_free(gf_dev->reset_gpio);
        pr_info("remove reset_gpio success\n");
    }

    gf_cleanup_pwr_list(gf_dev);
}

/*power on auto during boot, no need fp driver power on*/
int gf_power_on(struct gf_dev* gf_dev)
{
    int rc = 0;
    unsigned index = 0;

    for (index = 0; index < gf_dev->power_num; index++) {
        switch (gf_dev->pwr_list[index].pwr_type) {
        case FP_POWER_MODE_LDO:
            rc = vreg_setup(gf_dev, &(gf_dev->pwr_list[index]), true);
            pr_info("---- power on ldo ----\n");
            break;
        case FP_POWER_MODE_GPIO:
            gpio_set_value(gf_dev->pwr_list[index].pwr_gpio, gf_dev->pwr_list[index].poweron_level);
            pr_info("set pwr_gpio %d\n", gf_dev->pwr_list[index].poweron_level);
            break;
        case FP_POWER_MODE_AUTO:
            pr_info("[%s] power on auto, no need power on again\n", __func__);
            break;
        case FP_POWER_MODE_NOT_SET:
        default:
            rc = -1;
            pr_info("---- power on mode not set !!! ----\n");
            break;
        }
        if (rc) {
            pr_err("---- power on failed with mode = %d, index = %d, rc = %d ----\n",
                    gf_dev->pwr_list[index].pwr_type, index, rc);
            break;
        } else {
            pr_info("---- power on ok with mode = %d, index = %d  ----\n",
                    gf_dev->pwr_list[index].pwr_type, index);
        }
        msleep(gf_dev->pwr_list[index].delay);
    }

    msleep(30);
    return rc;
}

/*power off auto during shut down, no need fp driver power off*/
int gf_power_off(struct gf_dev* gf_dev)
{
    int rc = 0;
    unsigned index = 0;

    for (index = 0; index < gf_dev->power_num; index++) {
        switch (gf_dev->pwr_list[index].pwr_type) {
        case FP_POWER_MODE_LDO:
            rc = vreg_setup(gf_dev, &(gf_dev->pwr_list[index]), false);
            pr_info("---- power on ldo ----\n");
            break;
        case FP_POWER_MODE_GPIO:
            gpio_set_value(gf_dev->pwr_list[index].pwr_gpio, (gf_dev->pwr_list[index].poweron_level == 0 ? 1 : 0));
            pr_info("set pwr_gpio %d\n", (gf_dev->pwr_list[index].poweron_level == 0 ? 1 : 0));
            break;
        case FP_POWER_MODE_AUTO:
            pr_info("[%s] power on auto, no need power on again\n", __func__);
            break;
        case FP_POWER_MODE_NOT_SET:
        default:
            rc = -1;
            pr_info("---- power on mode not set !!! ----\n");
            break;
        }
        if (rc) {
            pr_err("---- power off failed with mode = %d, index = %d, rc = %d ----\n",
                    gf_dev->pwr_list[index].pwr_type, index, rc);
            break;
        } else {
            pr_info("---- power off ok with mode = %d, index = %d  ----\n",
                    gf_dev->pwr_list[index].pwr_type, index);
        }
    }

    msleep(30);
    return rc;
}

int gf_hw_reset(struct gf_dev *gf_dev, unsigned int delay_ms)
{
    if (gf_dev == NULL)
    {
        pr_info("Input buff is NULL.\n");
        return -1;
    }
    //gpio_direction_output(gf_dev->reset_gpio, 1);
    gpio_set_value(gf_dev->reset_gpio, 0);
    mdelay(5);
    gpio_set_value(gf_dev->reset_gpio, 1);
    mdelay(delay_ms);
    return 0;
}

int gf_irq_num(struct gf_dev *gf_dev)
{
    if(gf_dev == NULL) {
        pr_info("Input buff is NULL.\n");
        return -1;
    } else {
        return gpio_to_irq(gf_dev->irq_gpio);
    }
}


