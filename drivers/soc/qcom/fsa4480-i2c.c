/* Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/soc/qcom/fsa4480-i2c.h>

#ifdef OPLUS_ARCH_EXTENDS
/*Jianfeng.Qiu@PSW.MM.AudioDriver.HeadsetDet, 2019/07/31,
 * Add for fsa4480 headset detection interrupt.
 */
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#endif /* OPLUS_ARCH_EXTENDS */

#define FSA4480_I2C_NAME	"fsa4480-driver"

#ifdef OPLUS_ARCH_EXTENDS
/*Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/11/13, Add for support HL5280*/
#define HL5280_DEVICE_REG_VALUE 0x49

#define FSA4480_DEVICE_ID  0x00
#endif /* OPLUS_ARCH_EXTENDS */
#define FSA4480_SWITCH_SETTINGS 0x04
#define FSA4480_SWITCH_CONTROL  0x05
#ifdef OPLUS_ARCH_EXTENDS
/*Jianfeng.Qiu@PSW.MM.AudioDriver.HeadsetDet, 2019/09/20, Add for status0 register*/
#define FSA4480_SWITCH_STATUS0  0x06
#endif /* OPLUS_ARCH_EXTENDS */
#define FSA4480_SWITCH_STATUS1  0x07
#define FSA4480_SLOW_L          0x08
#define FSA4480_SLOW_R          0x09
#define FSA4480_SLOW_MIC        0x0A
#define FSA4480_SLOW_SENSE      0x0B
#define FSA4480_SLOW_GND        0x0C
#define FSA4480_DELAY_L_R       0x0D
#define FSA4480_DELAY_L_MIC     0x0E
#define FSA4480_DELAY_L_SENSE   0x0F
#define FSA4480_DELAY_L_AGND    0x10
#ifdef OPLUS_ARCH_EXTENDS
/*Jianfeng.Qiu@PSW.MM.AudioDriver.HeadsetDet, 2019/09/20, Add for open auto mic DET*/
#define FSA4480_FUN_EN          0x12
#define FSA4480_JACK_STATUS     0x17
#endif /* OPLUS_ARCH_EXTENDS */
#define FSA4480_RESET           0x1E

#ifdef OPLUS_ARCH_EXTENDS
/*Jianfeng.Qiu@PSW.MM.AudioDriver.HeadsetDet, 2019/07/31, Add for log*/
#undef dev_dbg
#define dev_dbg dev_info
#endif /* OPLUS_ARCH_EXTENDS */

#ifdef OPLUS_ARCH_EXTENDS
/*Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/11/13, Add for support HL5280*/
enum switch_vendor {
    FSA4480 = 0,
    HL5280
};
#endif /* OPLUS_ARCH_EXTENDS */

struct fsa4480_priv {
	struct regmap *regmap;
	struct device *dev;
	struct power_supply *usb_psy;
	struct notifier_block psy_nb;
	atomic_t usbc_mode;
	struct work_struct usbc_analog_work;
	struct blocking_notifier_head fsa4480_notifier;
	struct mutex notification_lock;
	#ifdef OPLUS_ARCH_EXTENDS
	/*Jianfeng.Qiu@PSW.MM.AudioDriver.HeadsetDet, 2019/07/31,
	 * Add for fsa4480 headset detection interrupt.
	 */
	unsigned int hs_det_pin;
	#endif /* OPLUS_ARCH_EXTENDS */
	#ifdef OPLUS_ARCH_EXTENDS
	/*Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/11/13, Add for support HL5280*/
	enum switch_vendor vendor;
	#endif /* OPLUS_ARCH_EXTENDS */
};

struct fsa4480_reg_val {
	u16 reg;
	u8 val;
};

static const struct regmap_config fsa4480_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = FSA4480_RESET,
};

static const struct fsa4480_reg_val fsa_reg_i2c_defaults[] = {
	{FSA4480_SLOW_L, 0x00},
	{FSA4480_SLOW_R, 0x00},
	{FSA4480_SLOW_MIC, 0x00},
	{FSA4480_SLOW_SENSE, 0x00},
	{FSA4480_SLOW_GND, 0x00},
	{FSA4480_DELAY_L_R, 0x00},
	{FSA4480_DELAY_L_MIC, 0x00},
	{FSA4480_DELAY_L_SENSE, 0x00},
	{FSA4480_DELAY_L_AGND, 0x09},
	{FSA4480_SWITCH_SETTINGS, 0x98},
};

static void fsa4480_usbc_update_settings(struct fsa4480_priv *fsa_priv,
		u32 switch_control, u32 switch_enable)
{
	if (!fsa_priv->regmap) {
		dev_err(fsa_priv->dev, "%s: regmap invalid\n", __func__);
		return;
	}

	regmap_write(fsa_priv->regmap, FSA4480_SWITCH_SETTINGS, 0x80);
	regmap_write(fsa_priv->regmap, FSA4480_SWITCH_CONTROL, switch_control);
	/* FSA4480 chip hardware requirement */
	usleep_range(50, 55);
	regmap_write(fsa_priv->regmap, FSA4480_SWITCH_SETTINGS, switch_enable);
}

static int fsa4480_usbc_event_changed(struct notifier_block *nb,
				      unsigned long evt, void *ptr)
{
	int ret;
	union power_supply_propval mode;
	struct fsa4480_priv *fsa_priv =
			container_of(nb, struct fsa4480_priv, psy_nb);
	struct device *dev;

	if (!fsa_priv)
		return -EINVAL;

	dev = fsa_priv->dev;
	if (!dev)
		return -EINVAL;

	if ((struct power_supply *)ptr != fsa_priv->usb_psy ||
				evt != PSY_EVENT_PROP_CHANGED)
		return 0;

	ret = power_supply_get_property(fsa_priv->usb_psy,
			POWER_SUPPLY_PROP_TYPEC_MODE, &mode);
	if (ret) {
		dev_err(dev, "%s: Unable to read USB TYPEC_MODE: %d\n",
			__func__, ret);
		return ret;
	}

	#ifdef OPLUS_ARCH_EXTENDS
	/*Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/11/13, Add for support HL5280*/
	if (fsa_priv->vendor == HL5280) {
		dev_err(fsa_priv->dev, "%s: switch chip is HL5280\n", __func__);
	}
	#endif /* OPLUS_ARCH_EXTENDS */
	dev_dbg(dev, "%s: USB change event received, supply mode %d, usbc mode %d, expected %d\n",
		__func__, mode.intval, fsa_priv->usbc_mode.counter,
		POWER_SUPPLY_TYPEC_SINK_AUDIO_ADAPTER);

	switch (mode.intval) {
	case POWER_SUPPLY_TYPEC_SINK_AUDIO_ADAPTER:
	case POWER_SUPPLY_TYPEC_NONE:
		if (atomic_read(&(fsa_priv->usbc_mode)) == mode.intval)
			break; /* filter notifications received before */
		atomic_set(&(fsa_priv->usbc_mode), mode.intval);

		dev_dbg(dev, "%s: queueing usbc_analog_work\n",
			__func__);
		pm_stay_awake(fsa_priv->dev);
		schedule_work(&fsa_priv->usbc_analog_work);
		break;
	default:
		break;
	}
	return ret;
}

static int fsa4480_usbc_analog_setup_switches(struct fsa4480_priv *fsa_priv)
{
	int rc = 0;
	union power_supply_propval mode;
	struct device *dev;
	#ifdef OPLUS_ARCH_EXTENDS
	/*Jianfeng.Qiu@PSW.MM.AudioDriver.HeadsetDet, 2019/09/20, Add for get status*/
	unsigned int switch_status = 0;
	unsigned int jack_status = 0;
	#endif /* OPLUS_ARCH_EXTENDS */
	if (!fsa_priv)
		return -EINVAL;
	dev = fsa_priv->dev;
	if (!dev)
		return -EINVAL;

	mutex_lock(&fsa_priv->notification_lock);
	/* get latest mode again within locked context */
	rc = power_supply_get_property(fsa_priv->usb_psy,
			POWER_SUPPLY_PROP_TYPEC_MODE, &mode);
	if (rc) {
		dev_err(dev, "%s: Unable to read USB TYPEC_MODE: %d\n",
			__func__, rc);
		goto done;
	}
	dev_info(dev, "%s: setting GPIOs active = %d\n",
		__func__, mode.intval != POWER_SUPPLY_TYPEC_NONE);

	#ifdef OPLUS_ARCH_EXTENDS
	/*Jianfeng.Qiu@PSW.MM.AudioDriver.HeadsetDet, 2019/07/31, Add for log*/
	dev_info(dev, "%s: USB mode %d\n", __func__, mode.intval);
	#endif /* OPLUS_ARCH_EXTENDS */
	switch (mode.intval) {
	/* add all modes FSA should notify for in here */
	case POWER_SUPPLY_TYPEC_SINK_AUDIO_ADAPTER:
		/* activate switches */
		fsa4480_usbc_update_settings(fsa_priv, 0x00, 0x9F);
		#ifdef OPLUS_ARCH_EXTENDS
		/*Jianfeng.Qiu@PSW.MM.AudioDriver.HeadsetDet, 2019/09/20, Add for open auto mic DET*/
		usleep_range(1000, 1005);
		regmap_write(fsa_priv->regmap, FSA4480_FUN_EN, 0x45);
		usleep_range(4000, 4005);
		dev_info(dev, "%s: set reg[0x%x] done.\n", __func__, FSA4480_FUN_EN);

		regmap_read(fsa_priv->regmap, FSA4480_JACK_STATUS, &jack_status);
		dev_info(dev, "%s: reg[0x%x]=0x%x.\n", __func__, FSA4480_JACK_STATUS, jack_status);
		if (jack_status & 0x2) {
			//for 3 pole, mic switch to SBU2
			dev_info(dev, "%s: set mic to sbu2 for 3 pole.\n", __func__);
			fsa4480_usbc_update_settings(fsa_priv, 0x00, 0x9F);
			usleep_range(4000, 4005);
		}

		regmap_read(fsa_priv->regmap, FSA4480_SWITCH_STATUS0, &switch_status);
		dev_info(dev, "%s: reg[0x%x]=0x%x.\n", __func__, FSA4480_SWITCH_STATUS0, switch_status);
		regmap_read(fsa_priv->regmap, FSA4480_SWITCH_STATUS1, &switch_status);
		dev_info(dev, "%s: reg[0x%x]=0x%x.\n", __func__, FSA4480_SWITCH_STATUS1, switch_status);
		#endif /* OPLUS_ARCH_EXTENDS */

		/* notify call chain on event */
		blocking_notifier_call_chain(&fsa_priv->fsa4480_notifier,
		mode.intval, NULL);
		#ifdef OPLUS_ARCH_EXTENDS
		/*Jianfeng.Qiu@PSW.MM.AudioDriver.HeadsetDet, 2019/07/31,
		 * Add for fsa4480 headset detection interrupt.
		 */
		if (gpio_is_valid(fsa_priv->hs_det_pin)) {
			dev_info(dev, "%s: set hs_det_pin to low.\n", __func__);
			gpio_direction_output(fsa_priv->hs_det_pin, 0);
		}
		#endif /* OPLUS_ARCH_EXTENDS */
		break;
	case POWER_SUPPLY_TYPEC_NONE:
		#ifdef OPLUS_ARCH_EXTENDS
		/*Jianfeng.Qiu@PSW.MM.AudioDriver.HeadsetDet, 2019/07/31,
		 * Add for fsa4480 headset detection interrupt.
		 */
		if (gpio_is_valid(fsa_priv->hs_det_pin)) {
			dev_info(dev, "%s: set hs_det_pin to high.\n", __func__);
			gpio_direction_output(fsa_priv->hs_det_pin, 1);
		}
		#endif /* OPLUS_ARCH_EXTENDS */
		/* notify call chain on event */
		blocking_notifier_call_chain(&fsa_priv->fsa4480_notifier,
				POWER_SUPPLY_TYPEC_NONE, NULL);

		/* deactivate switches */
		fsa4480_usbc_update_settings(fsa_priv, 0x18, 0x98);
		break;
	default:
		/* ignore other usb connection modes */
		break;
	}

done:
	mutex_unlock(&fsa_priv->notification_lock);
	return rc;
}

/*
 * fsa4480_reg_notifier - register notifier block with fsa driver
 *
 * @nb - notifier block of fsa4480
 * @node - phandle node to fsa4480 device
 *
 * Returns 0 on success, or error code
 */
int fsa4480_reg_notifier(struct notifier_block *nb,
			 struct device_node *node)
{
	int rc = 0;
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct fsa4480_priv *fsa_priv;

	if (!client)
		return -EINVAL;

	fsa_priv = (struct fsa4480_priv *)i2c_get_clientdata(client);
	if (!fsa_priv)
		return -EINVAL;

	rc = blocking_notifier_chain_register
				(&fsa_priv->fsa4480_notifier, nb);
	if (rc)
		return rc;

	/*
	 * as part of the init sequence check if there is a connected
	 * USB C analog adapter
	 */
	dev_dbg(fsa_priv->dev, "%s: verify if USB adapter is already inserted\n",
		__func__);
	rc = fsa4480_usbc_analog_setup_switches(fsa_priv);

	return rc;
}
EXPORT_SYMBOL(fsa4480_reg_notifier);

/*
 * fsa4480_unreg_notifier - unregister notifier block with fsa driver
 *
 * @nb - notifier block of fsa4480
 * @node - phandle node to fsa4480 device
 *
 * Returns 0 on pass, or error code
 */
int fsa4480_unreg_notifier(struct notifier_block *nb,
			     struct device_node *node)
{
	int rc = 0;
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct fsa4480_priv *fsa_priv;
	struct device *dev;
	union power_supply_propval mode;

	if (!client)
		return -EINVAL;

	fsa_priv = (struct fsa4480_priv *)i2c_get_clientdata(client);
	if (!fsa_priv)
		return -EINVAL;
	dev = fsa_priv->dev;
	if (!dev)
		return -EINVAL;

	mutex_lock(&fsa_priv->notification_lock);
	/* get latest mode within locked context */
	rc = power_supply_get_property(fsa_priv->usb_psy,
			POWER_SUPPLY_PROP_TYPEC_MODE, &mode);
	if (rc) {
		dev_dbg(dev, "%s: Unable to read USB TYPEC_MODE: %d\n",
			__func__, rc);
		goto done;
	}
	/* Do not reset switch settings for usb digital hs */
	if (mode.intval != POWER_SUPPLY_TYPEC_SINK)
		fsa4480_usbc_update_settings(fsa_priv, 0x18, 0x98);
	rc = blocking_notifier_chain_unregister
					(&fsa_priv->fsa4480_notifier, nb);
done:
	mutex_unlock(&fsa_priv->notification_lock);
	return rc;
}
EXPORT_SYMBOL(fsa4480_unreg_notifier);

static int fsa4480_validate_display_port_settings(struct fsa4480_priv *fsa_priv)
{
	u32 switch_status = 0;

	regmap_read(fsa_priv->regmap, FSA4480_SWITCH_STATUS1, &switch_status);

	if ((switch_status != 0x23) && (switch_status != 0x1C)) {
		pr_err("AUX SBU1/2 switch status is invalid = %u\n",
				switch_status);
		return -EIO;
	}

	return 0;
}
/*
 * fsa4480_switch_event - configure FSA switch position based on event
 *
 * @node - phandle node to fsa4480 device
 * @event - fsa_function enum
 *
 * Returns int on whether the switch happened or not
 */
int fsa4480_switch_event(struct device_node *node,
			 enum fsa_function event)
{
	int switch_control = 0;
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct fsa4480_priv *fsa_priv;

	if (!client)
		return -EINVAL;

	fsa_priv = (struct fsa4480_priv *)i2c_get_clientdata(client);
	if (!fsa_priv)
		return -EINVAL;
	if (!fsa_priv->regmap)
		return -EINVAL;

	#ifdef OPLUS_ARCH_EXTENDS
	/*Jianfeng.Qiu@PSW.MM.AudioDriver.HeadsetDet, 2019/07/31, Add for log*/
	pr_info("%s - switch event: %d\n", __func__, event);
	#endif /* VENDOR_EDIT */

	switch (event) {
	case FSA_MIC_GND_SWAP:
		regmap_read(fsa_priv->regmap, FSA4480_SWITCH_CONTROL,
				&switch_control);
		if ((switch_control & 0x07) == 0x07)
			switch_control = 0x0;
		else
			switch_control = 0x7;
		fsa4480_usbc_update_settings(fsa_priv, switch_control, 0x9F);
		break;
	case FSA_USBC_ORIENTATION_CC1:
		fsa4480_usbc_update_settings(fsa_priv, 0x18, 0xF8);
		return fsa4480_validate_display_port_settings(fsa_priv);
	case FSA_USBC_ORIENTATION_CC2:
		fsa4480_usbc_update_settings(fsa_priv, 0x78, 0xF8);
		return fsa4480_validate_display_port_settings(fsa_priv);
	case FSA_USBC_DISPLAYPORT_DISCONNECTED:
		fsa4480_usbc_update_settings(fsa_priv, 0x18, 0x98);
		break;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL(fsa4480_switch_event);

#ifdef OPLUS_ARCH_EXTENDS
/*Jianfeng.Qiu@PSW.MM.AudioDriver.HeadsetDet, 2019/07/31,
 * Add for fsa4480 headset detection interrupt.
 */
static int fsa4480_parse_dt(struct fsa4480_priv *fsa_priv,
	struct device *dev)
{
    struct device_node *dNode = dev->of_node;
    int ret = 0;

    if (dNode == NULL)
        return -ENODEV;

	if (!fsa_priv) {
		pr_err("%s: fsa_priv is NULL\n", __func__);
		return -ENOMEM;
	}

	fsa_priv->hs_det_pin = of_get_named_gpio(dNode,
	        "fsa4480,hs-det-gpio", 0);
	if (!gpio_is_valid(fsa_priv->hs_det_pin)) {
	    pr_warning("%s: hs-det-gpio in dt node is missing\n", __func__);
	    return -ENODEV;
	}
	ret = gpio_request(fsa_priv->hs_det_pin, "fsa4480_hs_det");
	if (ret) {
		pr_warning("%s: hs-det-gpio request fail\n", __func__);
		return ret;
	}

	gpio_direction_output(fsa_priv->hs_det_pin, 1);

	return ret;
}
#endif /* OPLUS_ARCH_EXTENDS */

static void fsa4480_usbc_analog_work_fn(struct work_struct *work)
{
	struct fsa4480_priv *fsa_priv =
		container_of(work, struct fsa4480_priv, usbc_analog_work);
        #ifdef OPLUS_FEATURE_DP_MAX20328
        /* xiang.fei@PSW.MM.AudioDriver.HeadsetDet, 2019/07/13, Add for max20328 */
        #ifdef CONFIG_QCOM_MAX20328_I2C
            if (!gpio_is_valid(fsa_priv->hs_det_pin)) {
               return;
            }
        #endif
        #endif /* OPLUS_FEATURE_DP_MAX20328 */
	if (!fsa_priv) {
		pr_err("%s: fsa container invalid\n", __func__);
		return;
	}
	fsa4480_usbc_analog_setup_switches(fsa_priv);
	pm_relax(fsa_priv->dev);
}

static void fsa4480_update_reg_defaults(struct regmap *regmap)
{
	u8 i;

	for (i = 0; i < ARRAY_SIZE(fsa_reg_i2c_defaults); i++)
		regmap_write(regmap, fsa_reg_i2c_defaults[i].reg,
				   fsa_reg_i2c_defaults[i].val);
}

static int fsa4480_probe(struct i2c_client *i2c,
			 const struct i2c_device_id *id)
{
	struct fsa4480_priv *fsa_priv;
	int rc = 0;
	#ifdef OPLUS_ARCH_EXTENDS
	/*Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/11/13, Add for support HL5280*/
	unsigned int reg_value = 0;
	#endif /* OPLUS_ARCH_EXTENDS */

	fsa_priv = devm_kzalloc(&i2c->dev, sizeof(*fsa_priv),
				GFP_KERNEL);
	if (!fsa_priv)
		return -ENOMEM;

	fsa_priv->dev = &i2c->dev;

	#ifdef OPLUS_ARCH_EXTENDS
	/*Jianfeng.Qiu@PSW.MM.AudioDriver.HeadsetDet, 2019/07/31,
	 * Add for fsa4480 headset detection interrupt.
	 */
	fsa4480_parse_dt(fsa_priv, &i2c->dev);
	#endif /* OPLUS_ARCH_EXTENDS */

	fsa_priv->usb_psy = power_supply_get_by_name("usb");
	if (!fsa_priv->usb_psy) {
		rc = -EPROBE_DEFER;
		dev_dbg(fsa_priv->dev,
			"%s: could not get USB psy info: %d\n",
			__func__, rc);
		goto err_data;
	}

	fsa_priv->regmap = devm_regmap_init_i2c(i2c, &fsa4480_regmap_config);
	if (IS_ERR_OR_NULL(fsa_priv->regmap)) {
		dev_err(fsa_priv->dev, "%s: Failed to initialize regmap: %d\n",
			__func__, rc);
		if (!fsa_priv->regmap) {
			rc = -EINVAL;
			goto err_supply;
		}
		rc = PTR_ERR(fsa_priv->regmap);
		goto err_supply;
	}

	fsa4480_update_reg_defaults(fsa_priv->regmap);

	#ifdef OPLUS_ARCH_EXTENDS
	/*Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/11/13, Add for support HL5280*/
	regmap_read(fsa_priv->regmap, FSA4480_DEVICE_ID, &reg_value);
	dev_err(fsa_priv->dev, "%s: device id reg value: 0x%x\n", __func__, reg_value);
	if (HL5280_DEVICE_REG_VALUE == reg_value) {
		dev_err(fsa_priv->dev, "%s: switch chip is HL5280\n", __func__);
		fsa_priv->vendor = HL5280;
	} else {
		dev_err(fsa_priv->dev, "%s: switch chip is FSA4480\n", __func__);
		fsa_priv->vendor = FSA4480;
	}
	#endif /* OPLUS_ARCH_EXTENDS */

	fsa_priv->psy_nb.notifier_call = fsa4480_usbc_event_changed;
	fsa_priv->psy_nb.priority = 0;
	rc = power_supply_reg_notifier(&fsa_priv->psy_nb);
	if (rc) {
		dev_err(fsa_priv->dev, "%s: power supply reg failed: %d\n",
			__func__, rc);
		goto err_supply;
	}

	mutex_init(&fsa_priv->notification_lock);
	i2c_set_clientdata(i2c, fsa_priv);

	INIT_WORK(&fsa_priv->usbc_analog_work,
		  fsa4480_usbc_analog_work_fn);

	fsa_priv->fsa4480_notifier.rwsem =
		(struct rw_semaphore)__RWSEM_INITIALIZER
		((fsa_priv->fsa4480_notifier).rwsem);
	fsa_priv->fsa4480_notifier.head = NULL;

	return 0;

err_supply:
	power_supply_put(fsa_priv->usb_psy);
err_data:
	#ifdef OPLUS_ARCH_EXTENDS
	/*Jianfeng.Qiu@PSW.MM.AudioDriver.HeadsetDet, 2019/07/31,
	 * Add for fsa4480 headset detection interrupt.
	 */
	if (gpio_is_valid(fsa_priv->hs_det_pin)) {
		gpio_free(fsa_priv->hs_det_pin);
	}
	#endif /* OPLUS_ARCH_EXTENDS */
	devm_kfree(&i2c->dev, fsa_priv);
	return rc;
}

static int fsa4480_remove(struct i2c_client *i2c)
{
	struct fsa4480_priv *fsa_priv =
			(struct fsa4480_priv *)i2c_get_clientdata(i2c);

	if (!fsa_priv)
		return -EINVAL;

	fsa4480_usbc_update_settings(fsa_priv, 0x18, 0x98);
	cancel_work(&fsa_priv->usbc_analog_work);
	pm_relax(fsa_priv->dev);
	/* deregister from PMI */
	power_supply_unreg_notifier(&fsa_priv->psy_nb);
	power_supply_put(fsa_priv->usb_psy);
	mutex_destroy(&fsa_priv->notification_lock);
	dev_set_drvdata(&i2c->dev, NULL);

	return 0;
}

static const struct of_device_id fsa4480_i2c_dt_match[] = {
	{
		.compatible = "qcom,fsa4480-i2c",
	},
	{}
};

static struct i2c_driver fsa4480_i2c_driver = {
	.driver = {
		.name = FSA4480_I2C_NAME,
		.of_match_table = fsa4480_i2c_dt_match,
	},
	.probe = fsa4480_probe,
	.remove = fsa4480_remove,
};

static int __init fsa4480_init(void)
{
	int rc;

	rc = i2c_add_driver(&fsa4480_i2c_driver);
	if (rc)
		pr_err("fsa4480: Failed to register I2C driver: %d\n", rc);

	return rc;
}
module_init(fsa4480_init);

static void __exit fsa4480_exit(void)
{
	i2c_del_driver(&fsa4480_i2c_driver);
}
module_exit(fsa4480_exit);

MODULE_DESCRIPTION("FSA4480 I2C driver");
MODULE_LICENSE("GPL v2");
