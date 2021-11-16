/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "oplus_sensor_devinfo.h"
#define CLOSE_PD  1
#define CLOSE_PD_CONDITION 2

extern int oplus_press_cali_data_init(void);
extern void oplus_press_cali_data_clean(void);

struct sensor_info * g_chip = NULL;

struct proc_dir_entry *sensor_proc_dir = NULL;
static struct oplus_als_cali_data *gdata = NULL;

static char* als_feature[] = {
    "als-type",
    "is-unit-device",
    "is-als-dri",
    "als-factor",
    "is_als_initialed",
    "als_buffer_length",
    "normalization_value",
    "use_lb_algo"
};


__attribute__((weak)) void oplus_device_dir_redirect(struct sensor_info * chip)
{
    pr_info("%s oplus_device_dir_redirect \n", __func__);
};

__attribute__((weak)) unsigned int get_serialID()
{
    return 0;
};
static void is_need_close_pd(struct sensor_hw* hw, struct device_node *ch_node)
{
    int rc = 0;
    int value = 0;
    int di = 0;
    int sn_size = 0;
    uint32_t *specific_sn = NULL;
    hw->feature.feature[2] = 0;
    rc = of_property_read_u32(ch_node, "is_need_close_pd", &value);

    if (!rc) {
        if (CLOSE_PD == value) {
            hw->feature.feature[2] = CLOSE_PD;
        } else if (CLOSE_PD_CONDITION == value) {
            sn_size = of_property_count_elems_of_size(ch_node, "sn_number", sizeof(uint32_t));
            pr_info("sn size %d\n", sn_size);
            specific_sn = (uint32_t *)kzalloc(sizeof(uint32_t) * sn_size, GFP_KERNEL);

            if (!specific_sn) {
                pr_err("%s kzalloc failed!\n", __func__);
                return;
            }

            of_property_read_u32_array(ch_node, "sn_number", specific_sn, sn_size);


            for (di = 0; di < sn_size; di++) {
                if (specific_sn[di] == get_serialID()) {
                    hw->feature.feature[2] = CLOSE_PD;
                    break;
                }
            }

            kfree(specific_sn);
        }
    }

}

static void parse_physical_sensor_common_dts(struct sensor_hw* hw, struct device_node *ch_node)
{
    int rc = 0;
    uint32_t chip_value = 0;
    rc = of_property_read_u32(ch_node, "sensor-name", &chip_value);

    if (rc) {
        hw->sensor_name = 0;
    } else {
        hw->sensor_name = chip_value;
    }

    rc = of_property_read_u32(ch_node, "bus-number", &chip_value);

    if (rc) {
        hw->bus_number = DEFAULT_CONFIG;//read from registry
    } else {
        hw->bus_number = chip_value;
    }

    rc = of_property_read_u32(ch_node, "sensor-direction", &chip_value);

    if (rc) {
        hw->direction = DEFAULT_CONFIG;//read from registry
    } else {
        hw->direction = chip_value;
    }

    rc = of_property_read_u32(ch_node, "irq-number", &chip_value);

    if (rc) {
        hw->irq_number = DEFAULT_CONFIG;//read from registry
    } else {
        hw->irq_number = chip_value;
    }

}

static void parse_magnetic_sensor_dts(struct sensor_hw* hw, struct device_node *ch_node)
{
    int value = 0;
    int rc = 0;
    int di = 0;
    int soft_default_para[18] = {10000, 0, 0, 0, 0, 0, 0, 0, 10000, 0, 0, 0, 0, 0, 0, 0, 10000, 0};
    //set default defaut mag
    memcpy((void *)&hw->feature.parameter[0], (void *)&soft_default_para[0], sizeof(soft_default_para));
    rc = of_property_read_u32(ch_node, "parameter-number", &value);
    if (!rc && value > 0 && value < PARAMETER_NUM) {
        rc = of_property_read_u32_array(ch_node,
                "soft-mag-parameter", &hw->feature.parameter[0], value);
        for (di = 0; di < value; di++) {
            SENSOR_DEVINFO_DEBUG("soft magnetic parameter[%d] : %d\n", di,
                hw->feature.parameter[di]);
            }
        return;
    } else if (rc) {
        int prj_id = 0;
        int prj_dir[5];
        int modem_type_id = 0;
        int modem_type_dir[5];
        struct device_node *node = ch_node;
        struct device_node *ch_node_mag = NULL;
        for_each_child_of_node(node, ch_node_mag) {
            if (ch_node_mag == NULL) {
                SENSOR_DEVINFO_DEBUG(" the mag_para use default parametyers");
                return;
                }
            rc = of_property_read_u32(ch_node_mag, "projects-num", &value);
            if (!rc) {
                prj_id = get_project();
                SENSOR_DEVINFO_DEBUG("get that project is %d", prj_id);
                rc = of_property_read_u32_array(ch_node_mag,
                            "match-projects", &prj_dir[0], value);
                for (di = 0; di < value; di++) {
                    SENSOR_DEVINFO_DEBUG(" which get there are %d projects", prj_dir[di]);
                    if (prj_dir[di] == prj_id) {
                    rc = of_property_read_u32(ch_node_mag, "parameter-number", &value);
                        if (!rc && value > 0 && value < PARAMETER_NUM) {
                        rc = of_property_read_u32_array(ch_node_mag,
                            "soft-mag-parameter", &hw->feature.parameter[0], value);
                        for (di = 0; di < value; di++) {
                            SENSOR_DEVINFO_DEBUG("soft magnetic parameter[%d] : %d\n", di,
                                hw->feature.parameter[di]);
                            }
                            return;
                        } else {
                            pr_info("parse soft magnetic parameter failed!\n");
                        }
                    }
                    else
                        continue;
                }
            }else if(!of_property_read_u32(ch_node_mag, "modem-type-num", &value)) {
                modem_type_id = get_Modem_Version();
                SENSOR_DEVINFO_DEBUG("get that modem-type is %d", modem_type_id);
                rc = of_property_read_u32_array(ch_node_mag,
                            "match-modem-type", &modem_type_dir[0], value);
                for (di = 0; di < value; di++) {
                    SENSOR_DEVINFO_DEBUG(" which get there are %d modem-type", modem_type_dir[di]);
                    if (modem_type_dir[di] == modem_type_id) {
                    rc = of_property_read_u32(ch_node_mag, "parameter-number", &value);
                        if (!rc && value > 0 && value < PARAMETER_NUM) {
                        rc = of_property_read_u32_array(ch_node_mag,
                            "soft-mag-parameter", &hw->feature.parameter[0], value);
                        for (di = 0; di < value; di++) {
                            SENSOR_DEVINFO_DEBUG("soft magnetic parameter[%d] : %d\n", di,
                                hw->feature.parameter[di]);
                            }
                            return;
                        } else {
                            pr_info("parse soft magnetic parameter failed!\n");
                        }
                    }
                    else
                        continue;
                }
            }
        }
    } else {
        pr_info("parse soft magnetic parameter failed!\n");
    }
}

static void parse_proximity_sensor_dts(struct sensor_hw* hw, struct device_node *ch_node)
{
    int value = 0;
    int rc = 0;
    int di = 0;
    char *param[] = {
        "low_step",
        "high_step",
        "low_limit",
        "high_limit",
        "dirty_low_step",
        "dirty_high_step",
        "ps_dirty_limit",
        "ps_ir_limit",
        "ps_adjust_min",
        "ps_adjust_max",
        "sampling_count",
        "step_max",
        "step_min",
        "step_div",
        "anti_shake_delta",
        "dynamic_cali_max",
        "raw2offset_radio",
        "offset_max",
        "offset_range_min",
        "offset_range_max",
        "force_cali_limit",
        "cali_jitter_limit",
        "cal_offset_margin",
    };
    rc = of_property_read_u32(ch_node, "ps-type", &value);

    if (!rc) {
        hw->feature.feature[0] = value;
    }

    rc = of_property_read_u32(ch_node, "ps_saturation", &value);

    if (!rc) {
        hw->feature.feature[1] = value;
    }

    is_need_close_pd(hw, ch_node);
    rc = of_property_read_u32(ch_node, "ps_factory_cali_max", &value);

    if (!rc) {
        hw->feature.feature[3] = value;
    }

    rc = of_property_read_u32(ch_node, "offset_calibration", &value);

    hw->feature.feature[4] = 0;
    if (!rc) {
        hw->feature.feature[4] = value;
        SENSOR_DEVINFO_DEBUG("offset_calibration: %d\n", hw->feature.feature[4]);
    }

    for (di = 0; di < ARRAY_SIZE(param); di++) {
        rc = of_property_read_u32(ch_node, param[di], &value);

        if (!rc) {
            hw->feature.parameter[di] = value;
        }

        SENSOR_DEVINFO_DEBUG("parameter[%d] : %d\n", di, hw->feature.parameter[di]);
    }

    rc = of_property_read_u32(ch_node, "parameter-number", &value);

    if (!rc && value > 0 && value < REG_NUM - 1) {
        hw->feature.reg[0] = value;
        rc = of_property_read_u32_array(ch_node,
                "sensor-reg", &hw->feature.reg[1], value);

        for (di = 0; di < value / 2; di++) {
            SENSOR_DEVINFO_DEBUG("sensor reg 0x%x = 0x%x\n", hw->feature.reg[di * 2 + 1],
                hw->feature.reg[2 * di + 2]);
        }
    } else {
        pr_info("parse alsps sensor reg failed\n");
    }

    SENSOR_DEVINFO_DEBUG("ps-type:%d ps_saturation:%d is_need_close_pd:%d\n",
        hw->feature.feature[0], hw->feature.feature[1], hw->feature.feature[2]);
}

static void parse_light_sensor_dts(struct sensor_hw* hw, struct device_node *ch_node)
{
    int rc = 0;
    int value = 0;
    int di = 0;

    for (di = 0; di < ARRAY_SIZE(als_feature); di++) {
        rc = of_property_read_u32(ch_node, als_feature[di], &value);

        if (!rc) {
            hw->feature.feature[di] = value;
        } else if (0 == strncmp(als_feature[di], "norm", 4)) {
            hw->feature.feature[di] = 1057;
        } else {
            pr_info("parse %s failed!", als_feature[di]);
        }

        SENSOR_DEVINFO_DEBUG("feature[%d] : %d\n", di, hw->feature.feature[di]);
    }
}

static void parse_sar_sensor_dts(struct sensor_hw* hw, struct device_node *ch_node)
{
    int di = 0;
    int rc = 0;
    int value = 0;
    int num_sum = 0;

    rc = of_property_read_u32(ch_node, "parameter-number", &value);

    if (!rc && value > 0 && value < PARAMETER_NUM) {
        num_sum = value;
        rc = of_property_read_u32_array(ch_node,
                "sensor-reg", &hw->feature.parameter[0], value);

        for (di = 0; di < value / 2; di++) {
            SENSOR_DEVINFO_DEBUG("sensor reg 0x%x = 0x%x\n", hw->feature.parameter[di * 2],
                hw->feature.parameter[2 * di + 1]);
        }
    } else {
        pr_info("parse sar sensor reg failed\n");
    }

    rc = of_property_read_u32(ch_node, "feature-number", &value);

    if (!rc && value > 0 && value < FEATURE_NUM) {
        num_sum +=  value;
        rc = of_property_read_u32_array(ch_node,"sensor-feature-reg", &hw->feature.feature[0], value);
        for (di = 0; di < value / 2; di++) {
            SENSOR_DEVINFO_DEBUG("sensor feature reg 0x%x = 0x%x\n", hw->feature.feature[di * 2],
                hw->feature.feature[2 * di + 1]);
        }
    } else {
        pr_info("parse sar sensor feature reg failed\n");
    }
    hw->feature.parameter[24] = num_sum/2;

}

static void parse_cct_sensor_dts(struct sensor_hw *hw, struct device_node *ch_node)
{
    int value = 0;
    int rc = 0;
    int di = 0;
    char *feature[] = {
        "decoupled-driver",
        "publish-sensors",
        "is-ch-dri",
        "timer-size",
        "fac-cali-sensor"
    };

    char *para[] = {
        "para-matrix",
        "atime",
        "first-atime",
        "fac-cali-atime",
        "first-again",
        "fac-cali-again",
        "fd-time",
        "fac-cali-fd-time",
        "first-fd-gain",
        "fac-cali-fd-gain"
    };

    hw->feature.feature[0] = 1;//default use decoupled driver oplus_cct

    for (di = 0; di < ARRAY_SIZE(feature); di++) {
        rc = of_property_read_u32(ch_node, feature[di], &value);

        if (!rc) {
            hw->feature.feature[di] = value;
        }

        SENSOR_DEVINFO_DEBUG("cct_feature[%d] : %d\n", di, hw->feature.feature[di]);
    }

    for (di = 0; di < ARRAY_SIZE(para); di++) {
        rc = of_property_read_u32(ch_node, para[di], &value);

        if (!rc) {
            hw->feature.parameter[di] = value;
        }

        SENSOR_DEVINFO_DEBUG("cct_parameter[%d] : %d\n", di, hw->feature.parameter[di]);
    }
}

static void parse_cct_rear_sensor_dts(struct sensor_hw *hw, struct device_node *ch_node)
{
    int value = 0;
    int rc = 0;
    int di = 0;
    char *feature[] = {
        "decoupled-driver",
        "publish-sensors",
        "is-ch-dri",
        "timer-size",
        "fac-cali-sensor"
    };

    char *para[] = {
        "para-matrix",
        "atime",
        "first-atime",
        "fac-cali-atime",
        "first-again",
        "fac-cali-again",
        "fd-time",
        "fac-cali-fd-time",
        "first-fd-gain",
        "fac-cali-fd-gain"
    };

    hw->feature.feature[0] = 1;//default use decoupled driver oplus_cct

    for (di = 0; di < ARRAY_SIZE(feature); di++) {
        rc = of_property_read_u32(ch_node, feature[di], &value);

        if (!rc) {
            hw->feature.feature[di] = value;
        }

        SENSOR_DEVINFO_DEBUG("cct_feature[%d] : %d\n", di, hw->feature.feature[di]);
    }

    for (di = 0; di < ARRAY_SIZE(para); di++) {
        rc = of_property_read_u32(ch_node, para[di], &value);

        if (!rc) {
            hw->feature.parameter[di] = value;
        }

        SENSOR_DEVINFO_DEBUG("cct_parameter[%d] : %d\n", di, hw->feature.parameter[di]);
    }
}

static void parse_each_physical_sensor_dts(struct sensor_hw* hw, struct device_node *ch_node)
{
    if (0 == strncmp(ch_node->name, "msensor", 7)) {
        parse_magnetic_sensor_dts(hw, ch_node);
    } else if (0 == strncmp(ch_node->name, "psensor", 7)) {
        parse_proximity_sensor_dts(hw, ch_node);
    } else if (0 == strncmp(ch_node->name, "lsensor", 7)) {
        parse_light_sensor_dts(hw, ch_node);
    } else if (0 == strncmp(ch_node->name, "ssensor", 7)) {
        parse_sar_sensor_dts(hw, ch_node);
    } else if (0 == strncmp(ch_node->name, "cctsensor", 7)) {
        parse_cct_sensor_dts(hw, ch_node);
    } else if (0 == strncmp(ch_node->name, "cctrsensor", 7)) {
        parse_cct_rear_sensor_dts(hw, ch_node);
    } else {
        //do nothing
    }

}

static void parse_pickup_sensor_dts(struct sensor_algorithm *algo, struct device_node *ch_node)
{
    int rc = 0;
    int value = 0;
    rc = of_property_read_u32(ch_node, "is-need-prox", &value);

    if (!rc) {
        algo->feature[0] = value;
    }

    rc = of_property_read_u32(ch_node, "prox-type", &value);

    if (!rc) {
        algo->parameter[0] = value;
    }

    SENSOR_DEVINFO_DEBUG("is-need-prox: %d, prox-type: %d\n",
        algo->feature[0], algo->parameter[0]);
}

static void parse_lux_aod_sensor_dts(struct sensor_algorithm *algo, struct device_node *ch_node)
{
    int rc = 0;
    int value = 0;
    rc = of_property_read_u32(ch_node, "thrd-low", &value);

    if (!rc) {
        algo->parameter[0] = value;
    }

    rc = of_property_read_u32(ch_node, "thrd-high", &value);

    if (!rc) {
        algo->parameter[1] = value;
    }

    rc = of_property_read_u32(ch_node, "als-type", &value);

    if (!rc) {
        algo->parameter[2] = value;
    }

    SENSOR_DEVINFO_DEBUG("thrd-low: %d, thrd-high: %d, als-type: %d\n",
        algo->parameter[0], algo->parameter[1], algo->parameter[2]);

}

static void parse_fp_display_sensor_dts(struct sensor_algorithm *algo, struct device_node *ch_node)
{
    int rc = 0;
    int value = 0;
    rc = of_property_read_u32(ch_node, "prox-type", &value);

    if (!rc) {
        algo->parameter[0] = value;
    }

    SENSOR_DEVINFO_DEBUG("prox-type :%d\n", algo->parameter[0]);
}

static void parse_camera_protect_sensor_dts(struct sensor_algorithm *algo, struct device_node *ch_node)
{
    int rc = 0;
    int value = 0;
    rc = of_property_read_u32(ch_node, "layout_offset_x", &value);
    if (!rc) {
        algo->parameter[0] = value;
    }

    rc = of_property_read_u32(ch_node, "layout_offset_y", &value);
    if (!rc) {
        algo->parameter[1] = value;
    }

    rc = of_property_read_u32(ch_node, "init_pin", &value);
    if (!rc) {
        algo->parameter[2] = value;
    }

    rc = of_property_read_u32(ch_node, "is_chip_pin", &value);
    if (!rc) {
        algo->parameter[3] = value;
    }
    SENSOR_DEVINFO_DEBUG("layout_offset_x: %d, layout_offset_y: %d, init_pin: %d, is_chip_pin: %d\n",
        algo->parameter[0], algo->parameter[1], algo->parameter[2], algo->parameter[3]);
}

static void parse_each_virtual_sensor_dts(struct sensor_algorithm *algo, struct device_node * ch_node)
{
    if (0 == strncmp(ch_node->name, "pickup", 6)) {
        parse_pickup_sensor_dts(algo, ch_node);
    } else if (0 == strncmp(ch_node->name, "lux_aod", 6)) {
        parse_lux_aod_sensor_dts(algo, ch_node);
    } else if (0 == strncmp(ch_node->name, "fp_display", 6)) {
        parse_fp_display_sensor_dts(algo, ch_node);
    } else if (0 == strncmp(ch_node->name, "camera_protect", 6)) {
        parse_camera_protect_sensor_dts(algo, ch_node);
    } else {
        // do nothing
    }

}

static void oplus_sensor_parse_dts(struct platform_device *pdev)
{
    struct device_node *node = pdev->dev.of_node;
    struct sensor_info * chip = platform_get_drvdata(pdev);
    int rc = 0;
    int value = 0;
    bool is_virtual_sensor = false;
    struct device_node *ch_node = NULL;
    int sensor_type = 0;
    int sensor_index = 0;
    struct sensor_hw *hw = NULL;
    struct sensor_algorithm *algo = NULL;
    pr_info("start \n");

    for_each_child_of_node(node, ch_node) {
        is_virtual_sensor = false;

        if (of_property_read_bool(ch_node, "is-virtual-sensor")) {
            is_virtual_sensor = true;
        }

        rc = of_property_read_u32(ch_node, "sensor-type", &value);

        if (rc || (is_virtual_sensor && value >= SENSOR_ALGO_NUM)
            || value >= SENSORS_NUM) {
            pr_info("parse sensor type failed!\n");
            continue;
        } else {
            sensor_type = value;
        }

        if (!is_virtual_sensor) {
            chip->s_vector[sensor_type].sensor_id = sensor_type;
            rc = of_property_read_u32(ch_node, "sensor-index", &value);

            if (rc || value >= SOURCE_NUM) {
                pr_info("parse sensor index failed!\n");
                continue;
            } else {
                sensor_index = value;
            }

            hw = &chip->s_vector[sensor_type].hw[sensor_index];
            parse_physical_sensor_common_dts(hw, ch_node);
            SENSOR_DEVINFO_DEBUG("chip->s_vector[%d].hw[%d] : sensor-name %d, \
                    bus-number %d, sensor-direction %d, \
                    irq-number %d\n",
                sensor_type, sensor_index,
                chip->s_vector[sensor_type].hw[sensor_index].sensor_name,
                chip->s_vector[sensor_type].hw[sensor_index].bus_number,
                chip->s_vector[sensor_type].hw[sensor_index].direction,
                chip->s_vector[sensor_type].hw[sensor_index].irq_number);
            parse_each_physical_sensor_dts(hw, ch_node);
        } else {
            chip->a_vector[sensor_type].sensor_id = sensor_type;
            SENSOR_DEVINFO_DEBUG("chip->a_vector[%d].sensor_id : sensor_type %d",
                sensor_type, chip->a_vector[sensor_type].sensor_id, sensor_type);
            algo = &chip->a_vector[sensor_type];
            parse_each_virtual_sensor_dts(algo, ch_node);
        }
    }//for_each_child_of_node

    rc = of_property_read_u32(node, "als-row-coe", &value);

    if (rc) {
        gdata->row_coe = 1000;
    } else {
        gdata->row_coe = value;
    }

    oplus_device_dir_redirect(chip);
}

static ssize_t als_type_read_proc(struct file *file, char __user *buf,
    size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;

    if (!g_chip) {
        return -ENOMEM;
    }

    len = sprintf(page, "%d", g_chip->s_vector[OPLUS_LIGHT].hw[0].feature.feature[0]);

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

static struct file_operations als_type_fops = {
    .read = als_type_read_proc,
};

static ssize_t red_max_lux_read_proc(struct file *file, char __user *buf,
    size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;

    if (!gdata) {
        return -ENOMEM;
    }

    len = sprintf(page, "%d", gdata->red_max_lux);

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
static ssize_t red_max_lux_write_proc(struct file *file, const char __user *buf,
    size_t count, loff_t *off)

{
    char page[256] = {0};
    unsigned int input = 0;

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

    if (sscanf(page, "%u", &input) != 1) {
        count = -EINVAL;
        return count;
    }

    if (input != gdata->red_max_lux) {
        gdata->red_max_lux = input;
    }

    return count;
}
static struct file_operations red_max_lux_fops = {
    .read = red_max_lux_read_proc,
    .write = red_max_lux_write_proc,
};
static ssize_t white_max_lux_read_proc(struct file *file, char __user *buf,
    size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;

    if (!gdata) {
        return -ENOMEM;
    }

    len = sprintf(page, "%d", gdata->white_max_lux);

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
static ssize_t white_max_lux_write_proc(struct file *file, const char __user *buf,
    size_t count, loff_t *off)

{
    char page[256] = {0};
    unsigned int input = 0;

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

    if (sscanf(page, "%u", &input) != 1) {
        count = -EINVAL;
        return count;
    }

    if (input != gdata->white_max_lux) {
        gdata->white_max_lux = input;
    }

    return count;
}
static struct file_operations white_max_lux_fops = {
    .read = white_max_lux_read_proc,
    .write = white_max_lux_write_proc,
};
static ssize_t blue_max_lux_read_proc(struct file *file, char __user *buf,
    size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;

    if (!gdata) {
        return -ENOMEM;
    }

    len = sprintf(page, "%d", gdata->blue_max_lux);

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
static ssize_t blue_max_lux_write_proc(struct file *file, const char __user *buf,
    size_t count, loff_t *off)

{
    char page[256] = {0};
    unsigned int input = 0;

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

    if (sscanf(page, "%u", &input) != 1) {
        count = -EINVAL;
        return count;
    }

    if (input != gdata->blue_max_lux) {
        gdata->blue_max_lux = input;
    }

    return count;
}
static struct file_operations blue_max_lux_fops = {
    .read = blue_max_lux_read_proc,
    .write = blue_max_lux_write_proc,
};
static ssize_t green_max_lux_read_proc(struct file *file, char __user *buf,
    size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;

    if (!gdata) {
        return -ENOMEM;
    }

    len = sprintf(page, "%d", gdata->green_max_lux);

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
static ssize_t green_max_lux_write_proc(struct file *file, const char __user *buf,
    size_t count, loff_t *off)

{
    char page[256] = {0};
    unsigned int input = 0;

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

    if (sscanf(page, "%u", &input) != 1) {
        count = -EINVAL;
        return count;
    }

    if (input != gdata->green_max_lux) {
        gdata->green_max_lux = input;
    }

    return count;
}
static struct file_operations green_max_lux_fops = {
    .read = green_max_lux_read_proc,
    .write = green_max_lux_write_proc,
};
static ssize_t cali_coe_read_proc(struct file *file, char __user *buf,
    size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;

    if (!gdata) {
        return -ENOMEM;
    }

    len = sprintf(page, "%d", gdata->cali_coe);

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
static ssize_t cali_coe_write_proc(struct file *file, const char __user *buf,
    size_t count, loff_t *off)

{
    char page[256] = {0};
    unsigned int input = 0;

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

    if (sscanf(page, "%u", &input) != 1) {
        count = -EINVAL;
        return count;
    }

    if (input != gdata->cali_coe) {
        gdata->cali_coe = input;
    }

    return count;
}
static struct file_operations cali_coe_fops = {
    .read = cali_coe_read_proc,
    .write = cali_coe_write_proc,
};
static ssize_t row_coe_read_proc(struct file *file, char __user *buf,
    size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;

    if (!gdata) {
        return -ENOMEM;
    }

    len = sprintf(page, "%d", gdata->row_coe);

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
static ssize_t row_coe_write_proc(struct file *file, const char __user *buf,
    size_t count, loff_t *off)

{
    char page[256] = {0};
    unsigned int input = 0;

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

    if (sscanf(page, "%u", &input) != 1) {
        count = -EINVAL;
        return count;
    }

    if (input != gdata->row_coe) {
        gdata->row_coe = input;
    }

    return count;
}
static struct file_operations row_coe_fops = {
    .read = row_coe_read_proc,
    .write = row_coe_write_proc,
};

static int oplus_als_cali_data_init()
{
    int rc = 0;
    struct proc_dir_entry *pentry;

    pr_info("%s call\n", __func__);

    if (gdata->proc_oplus_als) {
        printk("proc_oplus_als has alread inited\n");
        return 0;
    }

    gdata->proc_oplus_als =  proc_mkdir("als_cali", sensor_proc_dir);

    if (!gdata->proc_oplus_als) {
        pr_err("can't create proc_oplus_als proc\n");
        rc = -EFAULT;
        return rc;
    }

    pentry = proc_create("red_max_lux", 0666, gdata->proc_oplus_als,
            &red_max_lux_fops);

    if (!pentry) {
        pr_err("create red_max_lux proc failed.\n");
        rc = -EFAULT;
        return rc;
    }

    pentry = proc_create("green_max_lux", 0666, gdata->proc_oplus_als,
            &green_max_lux_fops);

    if (!pentry) {
        pr_err("create green_max_lux proc failed.\n");
        rc = -EFAULT;
        return rc;
    }

    pentry = proc_create("blue_max_lux", 0666, gdata->proc_oplus_als,
            &blue_max_lux_fops);

    if (!pentry) {
        pr_err("create blue_max_lux proc failed.\n");
        rc = -EFAULT;
        return rc;
    }

    pentry = proc_create("white_max_lux", 0666, gdata->proc_oplus_als,
            &white_max_lux_fops);

    if (!pentry) {
        pr_err("create white_max_lux proc failed.\n");
        rc = -EFAULT;
        return rc;
    }

    pentry = proc_create("cali_coe", 0666, gdata->proc_oplus_als,
            &cali_coe_fops);

    if (!pentry) {
        pr_err("create cali_coe proc failed.\n");
        rc = -EFAULT;
        return rc;
    }

    pentry = proc_create("row_coe", 0666, gdata->proc_oplus_als,
            &row_coe_fops);

    if (!pentry) {
        pr_err("create row_coe proc failed.\n");
        rc = -EFAULT;
        return rc;
    }

    pentry = proc_create("als_type", 0666, gdata->proc_oplus_als,
            &als_type_fops);

    if (!pentry) {
        pr_err("create als_type_fops proc failed.\n");
        rc = -EFAULT;
        return rc;
    }

    return 0;
}

static int oplus_devinfo_probe(struct platform_device *pdev)
{
    struct sensor_info * chip = NULL;
    size_t smem_size = 0;
    void *smem_addr = NULL;
    int rc = 0;
    struct oplus_als_cali_data *data = NULL;

    pr_info("%s call\n", __func__);

    smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY,
            SMEM_SENSOR,
            &smem_size);

    if (IS_ERR(smem_addr)) {
        pr_err("unable to acquire smem SMEM_SENSOR entry\n");
        return -ENOMEM;
    }

    chip = (struct sensor_info *)(smem_addr);

    if (chip == ERR_PTR(-EPROBE_DEFER)) {
        chip = NULL;
        pr_err("unable to acquire entry\n");
        return -ENOMEM;
    }

    memset(chip, 0, sizeof(struct sensor_info));

    if (gdata) {
        printk("%s:just can be call one time\n", __func__);
        return 0;
    }

    data = kzalloc(sizeof(struct oplus_als_cali_data), GFP_KERNEL);

    if (data == NULL) {
        rc = -ENOMEM;
        printk("%s:kzalloc fail %d\n", __func__, rc);
        return rc;
    }

    gdata = data;

    platform_set_drvdata(pdev, chip);

    oplus_sensor_parse_dts(pdev);

    g_chip = chip;

    pr_info("%s success\n", __func__);

    sensor_proc_dir = proc_mkdir("sensor", NULL);
    if (!sensor_proc_dir) {
        pr_err("can't create proc_sensor proc\n");
        rc = -EFAULT;
        return rc;
    }
    oplus_press_cali_data_init();
    rc = oplus_als_cali_data_init();

    if (rc < 0) {
        kfree(gdata);
        gdata = NULL;
    }

    return 0;
}

static int oplus_devinfo_remove(struct platform_device *pdev)
{
    if (gdata) {
        kfree(gdata);
        gdata = NULL;
    }

    oplus_press_cali_data_clean();

    return 0;
}

static const struct of_device_id of_drv_match[] = {
    { .compatible = "oplus,sensor-devinfo"},
    {},
};
MODULE_DEVICE_TABLE(of, of_drv_match);

static struct platform_driver _driver = {
    .probe      = oplus_devinfo_probe,
    .remove     = oplus_devinfo_remove,
    .driver     = {
    .name       = "sensor_devinfo",
    .of_match_table = of_drv_match,
    },
};

static int __init oplus_devinfo_init(void)
{
    pr_info("oplus_devinfo_init call\n");

    platform_driver_register(&_driver);
    return 0;
}

core_initcall(oplus_devinfo_init);

MODULE_DESCRIPTION("sensor devinfo");
MODULE_LICENSE("GPL");

