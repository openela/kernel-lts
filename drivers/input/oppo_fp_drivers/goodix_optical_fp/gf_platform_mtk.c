// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/timer.h>
#include <linux/err.h>
#include <soc/oppo/oppo_project.h>

#include "gf_spi_tee.h"

#if defined(USE_SPI_BUS)
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#elif defined(USE_PLATFORM_BUS)
#include <linux/platform_device.h>
#endif

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

//static struct pinctrl *gf_irq_pinctrl = NULL;
//static struct pinctrl_state *gf_irq_no_pull = NULL;
int g_cs_gpio_disable;

#ifndef USED_GPIO_PWR
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
    pr_info("Regulator %s vreg_setup,enable=%d \n", name, enable);

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
        }
        pr_err("disable vreg is null \n");
        rc = 0;
    }
    return rc;
}
#endif

void gf_cleanup_pwr_list(struct gf_dev* gf_dev) {
    unsigned index = 0;
    pr_info("%s cleanup power list", __func__);
    for (index = 0; index < gf_dev->power_num; index++) {
        if (gf_dev->pwr_list[index].pwr_type == FP_POWER_MODE_GPIO) {
            if (gpio_is_valid(gf_dev->irq_gpio)) {
                gpio_free(gf_dev->pwr_list[index].pwr_gpio);
                pr_info("remove pwr_gpio success\n");
            }
        }
        if (gf_dev->pwr_list[index].pwr_type == FP_POWER_MODE_LDO)
            memset(&(gf_dev->pwr_list[index]), 0, sizeof(fp_power_info_t));
    }
}

int gf_parse_pwr_list(struct gf_dev* gf_dev)
{
    int ret = 0;
    struct device *dev = &gf_dev->spi->dev;
    struct device_node *np = dev->of_node;
    struct device_node *child = NULL;
    unsigned child_node_index = 0;
    int ldo_param_amount = 0;
    const char *node_name = NULL;
    fp_power_info_t *pwr_list = gf_dev->pwr_list;

    gf_cleanup_pwr_list(gf_dev);

    for_each_available_child_of_node(np, child) {
        if (child_node_index >= FP_MAX_PWR_LIST_LEN) {
            pr_err("too many nodes");
            ret = -FP_ERROR_GENERAL;
            goto exit;
        }
        //get power mode for dts
        ret = of_property_read_u32(child, FP_POWER_NODE, &(pwr_list[child_node_index].pwr_type));
        if (ret) {
            pr_err("failed to request %s, ret = %d\n", FP_POWER_NODE, ret);
            goto exit;
        }

        switch(pwr_list[child_node_index].pwr_type) {
        case FP_POWER_MODE_LDO:
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
                    pwr_list[child_node_index].vreg_config.vmax);
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
            pr_err("failed to request %s, ret = %d\n", FP_POWER_NODE, ret);
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

int gf_parse_dts(struct gf_dev* gf_dev)
{
	int rc = 0;
	struct device_node *node = NULL;
	struct platform_device *pdev = NULL;
    gf_dev->cs_gpio_set = false;
    gf_dev->pinctrl = NULL;
    gf_dev->pstate_spi_6mA = NULL;
    gf_dev->pstate_default = NULL;
    gf_dev->pstate_cs_func = NULL;
    gf_dev->pstate_irq_no_pull = NULL;
    gf_dev->is_optical = true;
    g_cs_gpio_disable = 0;

	node = of_find_compatible_node(NULL, NULL, "goodix,goodix_fp");
	if (node) {
		pdev = of_find_device_by_node(node);
		if (pdev == NULL) {
            pr_err("[err] %s can not find device by node \n", __func__);
            return -1;
        }
    } else {
        pr_err("[err] %s can not find compatible node \n", __func__);
        return -1;
    }

    /*determine if it's optical*/
    if (FP_GOODIX_5658 == get_fpsensor_type() || FP_GOODIX_3626 == get_fpsensor_type()) { //add new fpsensor if needed
        gf_dev->is_optical = false;
    }
    else {
        gf_dev->is_optical = true;
    }
    pr_err("is_optical: %d\n", gf_dev->is_optical);

    rc = of_property_read_u32(node, "gf,cs_gpio_disable", &g_cs_gpio_disable);
    if (rc) {
        dev_err(&pdev->dev, "failed to request gf,cs_gpio_disable, ret = %d\n", rc);
        g_cs_gpio_disable = 0;
    }

    /*get clk pinctrl resource*/
    gf_dev->pinctrl = devm_pinctrl_get(&pdev->dev);
    if (IS_ERR(gf_dev->pinctrl)) {
        dev_err(&pdev->dev, "can not get the gf pinctrl");
        return PTR_ERR(gf_dev->pinctrl);
    }

    if (g_cs_gpio_disable != 1) {
        gf_dev->pstate_cs_func = pinctrl_lookup_state(gf_dev->pinctrl, "gf_cs_func");
        if (IS_ERR(gf_dev->pstate_cs_func)) {
            dev_err(&pdev->dev, "Can't find gf_cs_func pinctrl state\n");
            return PTR_ERR(gf_dev->pstate_cs_func);
        }
    }

    if(gf_dev->is_optical == true) {
        gf_dev->pstate_spi_6mA  = pinctrl_lookup_state(gf_dev->pinctrl, "gf_spi_drive_6mA");
        if (IS_ERR(gf_dev->pstate_spi_6mA)) {
            dev_err(&pdev->dev, "Can't find gf_spi_drive_6mA pinctrl state\n");
            return PTR_ERR(gf_dev->pstate_spi_6mA);
        }
        pinctrl_select_state(gf_dev->pinctrl, gf_dev->pstate_spi_6mA);

        gf_dev->pstate_default = pinctrl_lookup_state(gf_dev->pinctrl, "default");
        if (IS_ERR(gf_dev->pstate_default)) {
            dev_err(&pdev->dev, "Can't find default pinctrl state\n");
            return PTR_ERR(gf_dev->pstate_default);
        }
        pinctrl_select_state(gf_dev->pinctrl, gf_dev->pstate_default);
    }
    else {
        gf_dev->pstate_irq_no_pull = pinctrl_lookup_state(gf_dev->pinctrl, "goodix_irq_no_pull");
        if (IS_ERR(gf_dev->pstate_irq_no_pull)) {
            dev_err(&pdev->dev, "Can't find irq_no_pull pinctrl state\n");
            // return PTR_ERR(gf_dev->pstate_irq_no_pull);
        } else {
            pinctrl_select_state(gf_dev->pinctrl, gf_dev->pstate_irq_no_pull);
        }
    }
	/*get reset resource*/
	gf_dev->reset_gpio = of_get_named_gpio(pdev->dev.of_node, "goodix,gpio_reset", 0);
	if (!gpio_is_valid(gf_dev->reset_gpio)) {
		pr_info("RESET GPIO is invalid.\n");
		return -1;
	}

	rc = gpio_request(gf_dev->reset_gpio, "goodix_reset");
	if (rc) {
		dev_err(&gf_dev->spi->dev, "Failed to request RESET GPIO. rc = %d\n", rc);
		return -1;
	}

	gpio_direction_output(gf_dev->reset_gpio, 0);

	msleep(3);

    /*get cs resource*/
    if (g_cs_gpio_disable != 1) {
        gf_dev->cs_gpio = of_get_named_gpio(pdev->dev.of_node, "goodix,gpio_cs", 0);
        if (!gpio_is_valid(gf_dev->cs_gpio)) {
            pr_info("CS GPIO is invalid.\n");
            return -1;
        }
        rc = gpio_request(gf_dev->cs_gpio, "goodix_cs");
        if (rc) {
            dev_err(&gf_dev->spi->dev, "Failed to request CS GPIO. rc = %d\n", rc);
            return -1;
        }
        gpio_direction_output(gf_dev->cs_gpio, 0);
        gf_dev->cs_gpio_set = true;
    }

    /*get irq resourece*/
    gf_dev->irq_gpio = of_get_named_gpio(pdev->dev.of_node, "goodix,gpio_irq", 0);
	if (!gpio_is_valid(gf_dev->irq_gpio)) {
		pr_info("IRQ GPIO is invalid.\n");
		return -1;
	}

	rc = gpio_request(gf_dev->irq_gpio, "goodix_irq");
	if (rc) {
		dev_err(&gf_dev->spi->dev, "Failed to request IRQ GPIO. rc = %d\n", rc);
		return -1;
	}
	gpio_direction_input(gf_dev->irq_gpio);

    rc = gf_parse_pwr_list(gf_dev);
    if (rc) {
        pr_err("failed to parse power list, rc = %d\n", rc);
        gf_cleanup_pwr_list(gf_dev);
        return rc;
    }

	return 0;
}

void gf_cleanup(struct gf_dev* gf_dev)
{
	pr_info("[info] %s\n",__func__);
	if (gpio_is_valid(gf_dev->irq_gpio))
	{
		gpio_free(gf_dev->irq_gpio);
		pr_info("remove irq_gpio success\n");
	}
    if (gpio_is_valid(gf_dev->cs_gpio)) {
        gpio_free(gf_dev->cs_gpio);
        pr_info("remove cs_gpio success\n");
    }
	if (gpio_is_valid(gf_dev->reset_gpio))
	{
		gpio_free(gf_dev->reset_gpio);
		pr_info("remove reset_gpio success\n");
	}

    gf_cleanup_pwr_list(gf_dev);
}

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
	if(gf_dev == NULL) {
		pr_info("Input buff is NULL.\n");
		return -1;
	}

	gpio_set_value(gf_dev->reset_gpio, 0);
	mdelay(20);
	gpio_set_value(gf_dev->reset_gpio, 1);
        if (gf_dev->cs_gpio_set) {
                pr_info("---- pull CS up and set CS from gpio to func ----");
                gpio_set_value(gf_dev->cs_gpio, 1);
                pinctrl_select_state(gf_dev->pinctrl, gf_dev->pstate_cs_func);
                gf_dev->cs_gpio_set = false;
        }
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

