/*
 * aw8697.c   aw8697 haptic module
 *
 * Version: v1.3.6
 *
 * Copyright (c) 2018 AWINIC Technology CO., LTD
 *
 *  Author: Nick Li <liweilei@awinic.com.cn>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/syscalls.h>
#include <linux/power_supply.h>
#include "aw8697.h"
#include "aw8697_reg.h"
#include "aw8697_config.h"
#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/11/17,  Add for config trig1 */
#include <linux/proc_fs.h>
#include <linux/pm_qos.h>
#include <linux/vmalloc.h>
#include <soc/oppo/device_info.h>
#include <soc/oppo/oppo_project.h>
#endif

/******************************************************
 *
 * Marco
 *
 ******************************************************/
#define AW8697_I2C_NAME "aw8697_haptic_0832"
#define AW8697_HAPTIC_NAME "aw8697_haptic_0832"

#define AW8697_VERSION "v1.3.6"


#define AWINIC_RAM_UPDATE_DELAY

#define AW_I2C_RETRIES 10 //vincent modify for QC's sugestion 20190113
#define AW_I2C_RETRY_DELAY 2
#define AW_READ_CHIPID_RETRIES 5
#define AW_READ_CHIPID_RETRY_DELAY 2

#define AW8697_MAX_DSP_START_TRY_COUNT    10



#define AW8697_MAX_FIRMWARE_LOAD_CNT 20

/******************************************************
 *
 * variable
 *
 ******************************************************/
#define PM_QOS_VALUE_VB 400
static struct pm_qos_request pm_qos_req_vb;

static uint8_t AW8697_HAPTIC_RAM_VBAT_COMP_GAIN;

#define AW8697_RTP_NAME_MAX        64
//static char *aw8697_ram_name = "aw8697_haptic.bin";
//
static char aw8697_ram_name[5][30] ={ 
{"aw8697_haptic_170.bin"},
{"aw8697_haptic_170.bin"},
{"aw8697_haptic_170.bin"},
{"aw8697_haptic_170.bin"},
{"aw8697_haptic_170.bin"},
};

static char aw8697_long_sound_rtp_name[5][30] ={ 
    {"aw8697_long_sound_168.bin"},
    {"aw8697_long_sound_170.bin"},
    {"aw8697_long_sound_173.bin"},
    {"aw8697_long_sound_175.bin"},
};

static char aw8697_old_steady_test_rtp_name_0815[11][60] ={ 
    {"aw8697_old_steady_test_RTP_52_160Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_162Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_164Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_166Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_168Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_170Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_172Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_174Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_176Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_178Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_180Hz.bin"},
};

static char aw8697_high_temp_high_humidity_0815[11][60] ={ 
    {"aw8697_high_temp_high_humidity_channel_RTP_51_160Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_162Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_164Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_166Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_168Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_170Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_172Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_174Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_176Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_178Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_180Hz.bin"},
};
    
static char aw8697_old_steady_test_rtp_name_0832[11][60] ={ 
    {"aw8697_old_steady_test_RTP_52_225Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_226Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_227Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_228Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_229Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_230Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_231Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_232Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_233Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_234Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_235Hz.bin"},
};

   
static char aw8697_high_temp_high_humidity_0832[11][60] ={ 
    {"aw8697_high_temp_high_humidity_channel_RTP_51_225Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_226Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_227Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_228Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_229Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_230Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_231Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_232Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_233Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_234Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_235Hz.bin"},
};
static char aw8697_ringtone_rtp_f0_170_name[][AW8697_RTP_NAME_MAX] ={ 
        {"aw8697_rtp.bin"}, 
        {"aw8697_Hearty_channel_RTP_1_170.bin"},
        {"aw8697_Instant_channel_RTP_2_170.bin"},
        {"aw8697_Music_channel_RTP_3_170.bin"},
        {"aw8697_Percussion_channel_RTP_4_170.bin"},
        {"aw8697_Ripple_channel_RTP_5_170.bin"},
        {"aw8697_Bright_channel_RTP_6_170.bin"},
        {"aw8697_Fun_channel_RTP_7_170.bin"},
        {"aw8697_Glittering_channel_RTP_8_170.bin"},
        {"aw8697_Granules_channel_RTP_9_170.bin"},
        {"aw8697_Harp_channel_RTP_10_170.bin"},
        {"aw8697_Impression_channel_RTP_11_170.bin"},
        {"aw8697_Ingenious_channel_RTP_12_170.bin"},
        {"aw8697_Joy_channel_RTP_13_170.bin"},
        {"aw8697_Overtone_channel_RTP_14_170.bin"},
        {"aw8697_Receive_channel_RTP_15_170.bin"},
        {"aw8697_Splash_channel_RTP_16_170.bin"},
    
        {"aw8697_About_School_RTP_17_170.bin"},
        {"aw8697_Bliss_RTP_18_170.bin"},
        {"aw8697_Childhood_RTP_19_170.bin"},
        {"aw8697_Commuting_RTP_20_170.bin"},
        {"aw8697_Dream_RTP_21_170.bin"},
        {"aw8697_Firefly_RTP_22_170.bin"},
        {"aw8697_Gathering_RTP_23_170.bin"},
        {"aw8697_Gaze_RTP_24_170.bin"},
        {"aw8697_Lakeside_RTP_25_170.bin"},
        {"aw8697_Lifestyle_RTP_26_170.bin"},
        {"aw8697_Memories_RTP_27_170.bin"},
        {"aw8697_Messy_RTP_28_170.bin"},
        {"aw8697_Night_RTP_29_170.bin"},
        {"aw8697_Passionate_Dance_RTP_30_170.bin"},
        {"aw8697_Playground_RTP_31_170.bin"},
        {"aw8697_Relax_RTP_32_170.bin"},
        {"aw8697_Reminiscence_RTP_33_170.bin"},
        {"aw8697_Silence_From_Afar_RTP_34_170.bin"},
        {"aw8697_Silence_RTP_35_170.bin"},
        {"aw8697_Stars_RTP_36_170.bin"},
        {"aw8697_Summer_RTP_37_170.bin"},
        {"aw8697_Toys_RTP_38_170.bin"},
        {"aw8697_Travel_RTP_39_170.bin"},
        {"aw8697_Vision_RTP_40_170.bin"},
    
        {"aw8697_reserved.bin"},
        {"aw8697_reserved.bin"},
        {"aw8697_reserved.bin"},
        {"aw8697_reserved.bin"},
        {"aw8697_reserved.bin"},
        {"aw8697_reserved.bin"},
    
        {"aw8697_reserved.bin"},
        {"aw8697_Simple_channel_RTP_48_170.bin"},
        {"aw8697_Pure_RTP_49_170.bin"},
        {"barca_alarm_ring_RTP_120_170.bin"},
        {"barca_incoming_ring_RTP_121_170.bin"},
        {"barca_notice_ring_RTP_122_170.bin"},
};
static char aw8697_rtp_name[][AW8697_RTP_NAME_MAX] = {
    {"aw8697_rtp.bin"}, 
#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/11/17,  Add for config trig1 */
    {"aw8697_Hearty_channel_RTP_1.bin"},
    {"aw8697_Instant_channel_RTP_2.bin"},
    {"aw8697_Music_channel_RTP_3.bin"},
    {"aw8697_Percussion_channel_RTP_4.bin"},
    {"aw8697_Ripple_channel_RTP_5.bin"},
    {"aw8697_Bright_channel_RTP_6.bin"},
    {"aw8697_Fun_channel_RTP_7.bin"},
    {"aw8697_Glittering_channel_RTP_8.bin"},
    {"aw8697_Granules_channel_RTP_9.bin"},
    {"aw8697_Harp_channel_RTP_10.bin"},
    {"aw8697_Impression_channel_RTP_11.bin"},
    {"aw8697_Ingenious_channel_RTP_12.bin"},
    {"aw8697_Joy_channel_RTP_13.bin"},
    {"aw8697_Overtone_channel_RTP_14.bin"},
    {"aw8697_Receive_channel_RTP_15.bin"},
    {"aw8697_Splash_channel_RTP_16.bin"},

    {"aw8697_About_School_RTP_17.bin"},
    {"aw8697_Bliss_RTP_18.bin"},
    {"aw8697_Childhood_RTP_19.bin"},
    {"aw8697_Commuting_RTP_20.bin"},
    {"aw8697_Dream_RTP_21.bin"},
    {"aw8697_Firefly_RTP_22.bin"},
    {"aw8697_Gathering_RTP_23.bin"},
    {"aw8697_Gaze_RTP_24.bin"},
    {"aw8697_Lakeside_RTP_25.bin"},
    {"aw8697_Lifestyle_RTP_26.bin"},
    {"aw8697_Memories_RTP_27.bin"},
    {"aw8697_Messy_RTP_28.bin"},
    {"aw8697_Night_RTP_29.bin"},
    {"aw8697_Passionate_Dance_RTP_30.bin"},
    {"aw8697_Playground_RTP_31.bin"},
    {"aw8697_Relax_RTP_32.bin"},
    {"aw8697_Reminiscence_RTP_33.bin"},
    {"aw8697_Silence_From_Afar_RTP_34.bin"},
    {"aw8697_Silence_RTP_35.bin"},
    {"aw8697_Stars_RTP_36.bin"},
    {"aw8697_Summer_RTP_37.bin"},
    {"aw8697_Toys_RTP_38.bin"},
    {"aw8697_Travel_RTP_39.bin"},
    {"aw8697_Vision_RTP_40.bin"},

    {"aw8697_waltz_channel_RTP_41.bin"},
    {"aw8697_cut_channel_RTP_42.bin"},
    {"aw8697_clock_channel_RTP_43.bin"},
    {"aw8697_long_sound_channel_RTP_44.bin"},
    {"aw8697_short_channel_RTP_45.bin"},
    {"aw8697_two_error_remaind_RTP_46.bin"},

    {"aw8697_kill_program_RTP_47.bin"},
    {"aw8697_Simple_channel_RTP_48.bin"},
    {"aw8697_Pure_RTP_49.bin"},
    {"aw8697_reserved_sound_channel_RTP_50.bin"},

    {"aw8697_high_temp_high_humidity_channel_RTP_51.bin"},

    {"aw8697_old_steady_test_RTP_52.bin"},
    {"aw8697_listen_pop_53.bin"},
    {"aw8697_desk_7_RTP_54.bin"},
    {"aw8697_nfc_10_RTP_55.bin"},
    {"aw8697_vibrator_remain_12_RTP_56.bin"},
    {"aw8697_notice_13_RTP_57.bin"},
    {"aw8697_third_ring_14_RTP_58.bin"},
    {"aw8697_reserved_59.bin"},

    {"aw8697_honor_fisrt_kill_RTP_60.bin"},
    {"aw8697_honor_two_kill_RTP_61.bin"},
    {"aw8697_honor_three_kill_RTP_62.bin"},
    {"aw8697_honor_four_kill_RTP_63.bin"},
    {"aw8697_honor_five_kill_RTP_64.bin"},
    {"aw8697_honor_three_continu_kill_RTP_65.bin"},
    {"aw8697_honor_four_continu_kill_RTP_66.bin"},
    {"aw8697_honor_unstoppable_RTP_67.bin"},
    {"aw8697_honor_thousands_kill_RTP_68.bin"},
    {"aw8697_honor_lengendary_RTP_69.bin"},


    {"aw8697_reserved_70.bin"},
    {"aw8697_reserved_71.bin"},
    {"aw8697_reserved_72.bin"},
    {"aw8697_reserved_73.bin"},
    {"aw8697_reserved_74.bin"},
    {"aw8697_reserved_75.bin"},
    {"aw8697_reserved_76.bin"},
    {"aw8697_reserved_77.bin"},
    {"aw8697_reserved_78.bin"},
    {"aw8697_reserved_79.bin"},

    {"aw8697_reserved_80.bin"},
    {"aw8697_reserved_81.bin"},
    {"aw8697_reserved_82.bin"},
    {"aw8697_reserved_83.bin"},
    {"aw8697_reserved_84.bin"},
    {"aw8697_reserved_85.bin"},
    {"aw8697_reserved_86.bin"},
    {"aw8697_reserved_87.bin"},
    {"aw8697_reserved_88.bin"},
    {"aw8697_reserved_89.bin"},

    {"aw8697_reserved_90.bin"},
    {"aw8697_reserved_91.bin"},
    {"aw8697_reserved_92.bin"},
    {"aw8697_reserved_93.bin"},
    {"aw8697_reserved_94.bin"},
    {"aw8697_reserved_95.bin"},
    {"aw8697_reserved_96.bin"},
    {"aw8697_reserved_97.bin"},
    {"aw8697_reserved_98.bin"},
    {"aw8697_reserved_99.bin"},

    {"aw8697_soldier_first_kill_RTP_100.bin"},
    {"aw8697_soldier_second_kill_RTP_101.bin"},
    {"aw8697_soldier_third_kill_RTP_102.bin"},
    {"aw8697_soldier_fourth_kill_RTP_103.bin"},
    {"aw8697_soldier_fifth_kill_RTP_104.bin"},
    {"aw8697_reserved_105.bin"},
    {"aw8697_reserved_106.bin"},
    {"aw8697_reserved_107.bin"},
    {"aw8697_reserved_108.bin"},
    {"aw8697_reserved_109.bin"},

    {"aw8697_reserved_110.bin"},
    {"aw8697_reserved_111.bin"},
    {"aw8697_reserved_112.bin"},
    {"aw8697_reserved_113.bin"},
    {"aw8697_reserved_114.bin"},
    {"aw8697_reserved_115.bin"},
    {"aw8697_reserved_116.bin"},
    {"aw8697_reserved_117.bin"},
    {"aw8697_reserved_118.bin"},
    {"aw8697_reserved_119.bin"},

    {"aw8697_Miss_RTP_120.bin"},
    {"aw8697_Scenic_RTP_121.bin"},
    {"aw8697_reserved_122.bin"},
/* used for coloros 7 */
    {"aw8697_Appear_channel_RTP_coloros7_123.bin"},
    {"aw8697_Miss_RTP_coloros7_124.bin"},
    {"aw8697_Music_channel_RTP_coloros7_125.bin"},
    {"aw8697_Percussion_channel_RTP_coloros7_126.bin"},
    {"aw8697_Ripple_channel_RTP_coloros7_127.bin"},
    {"aw8697_Bright_channel_RTP_coloros7_128.bin"},
    {"aw8697_Fun_channel_RTP_coloros7_129.bin"},
    {"aw8697_Glittering_channel_RTP_coloros7_130.bin"},
    {"aw8697_Harp_channel_RTP_coloros7_131.bin"},
    {"aw8697_Overtone_channel_RTP_coloros7_132.bin"},
    {"aw8697_Simple_channel_RTP_coloros7_133.bin"},

    {"aw8697_Seine_past_RTP_coloros7_134.bin"},
    {"aw8697_Classical_ring_RTP_coloros7_135.bin"},
    {"aw8697_Long_for_RTP_coloros7_136.bin"},
    {"aw8697_Romantic_RTP_coloros7_137.bin"},
    {"aw8697_Bliss_RTP_coloros7_138.bin"},
    {"aw8697_Dream_RTP_coloros7_139.bin"},
    {"aw8697_Relax_RTP_coloros7_140.bin"},
    {"aw8697_Joy_channel_RTP_coloros7_141.bin"},
    {"aw8697_weather_wind_RTP_coloros7_142.bin"},
    {"aw8697_weather_cloudy_RTP_coloros7_143.bin"},
    {"aw8697_weather_thunderstorm_RTP_coloros7_144.bin"},
    {"aw8697_weather_default_RTP_coloros7_145.bin"},
    {"aw8697_weather_sunny_RTP_coloros7_146.bin"},
    {"aw8697_weather_smog_RTP_coloros7_147.bin"},
    {"aw8697_weather_snow_RTP_coloros7_148.bin"},
    {"aw8697_weather_rain_RTP_coloros7_149.bin"},

/* used for coloros 7 end*/
#endif
    {"aw8697_rtp_lighthouse.bin"},
    {"aw8697_rtp_silk.bin"},
};
static char aw8697_ram_name_19065[5][30] ={
    {"aw8697_haptic_235.bin"},
    {"aw8697_haptic_235.bin"},
    {"aw8697_haptic_235.bin"},
    {"aw8697_haptic_235.bin"},
    {"aw8697_haptic_235.bin"},
};

static char aw8697_rtp_name_19065[][AW8697_RTP_NAME_MAX] = {
    {"aw8697_rtp.bin"}, 
#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/11/17,  Add for config trig1 */
    {"aw8697_Hearty_channel_RTP_1.bin"},
    {"aw8697_Instant_channel_RTP_2_234Hz.bin"},
    {"aw8697_Music_channel_RTP_3.bin"},
    {"aw8697_Percussion_channel_RTP_4.bin"},
    {"aw8697_Ripple_channel_RTP_5.bin"},
    {"aw8697_Bright_channel_RTP_6.bin"},
    {"aw8697_Fun_channel_RTP_7.bin"},
    {"aw8697_Glittering_channel_RTP_8.bin"},
    {"aw8697_Granules_channel_RTP_9_234Hz.bin"},
    {"aw8697_Harp_channel_RTP_10.bin"},
    {"aw8697_Impression_channel_RTP_11.bin"},
    {"aw8697_Ingenious_channel_RTP_12_234Hz.bin"},
    {"aw8697_Joy_channel_RTP_13_234Hz.bin"},
    {"aw8697_Overtone_channel_RTP_14.bin"},
    {"aw8697_Receive_channel_RTP_15_234Hz.bin"},
    {"aw8697_Splash_channel_RTP_16_234Hz.bin"},

    {"aw8697_About_School_RTP_17_234Hz.bin"},
    {"aw8697_Bliss_RTP_18.bin"},
    {"aw8697_Childhood_RTP_19_234Hz.bin"},
    {"aw8697_Commuting_RTP_20_234Hz.bin"},
    {"aw8697_Dream_RTP_21.bin"},
    {"aw8697_Firefly_RTP_22_234Hz.bin"},
    {"aw8697_Gathering_RTP_23.bin"},
    {"aw8697_Gaze_RTP_24_234Hz.bin"},
    {"aw8697_Lakeside_RTP_25_234Hz.bin"},
    {"aw8697_Lifestyle_RTP_26.bin"},
    {"aw8697_Memories_RTP_27_234Hz.bin"},
    {"aw8697_Messy_RTP_28_234Hz.bin"},
    {"aw8697_Night_RTP_29_234Hz.bin"},
    {"aw8697_Passionate_Dance_RTP_30_234Hz.bin"},
    {"aw8697_Playground_RTP_31_234Hz.bin"},
    {"aw8697_Relax_RTP_32_234Hz.bin"},
    {"aw8697_Reminiscence_RTP_33.bin"},
    {"aw8697_Silence_From_Afar_RTP_34_234Hz.bin"},
    {"aw8697_Silence_RTP_35_234Hz.bin"},
    {"aw8697_Stars_RTP_36_234Hz.bin"},
    {"aw8697_Summer_RTP_37_234Hz.bin"},
    {"aw8697_Toys_RTP_38_234Hz.bin"},
    {"aw8697_Travel_RTP_39.bin"},
    {"aw8697_Vision_RTP_40.bin"},

    {"aw8697_waltz_channel_RTP_41_234Hz.bin"},
    {"aw8697_cut_channel_RTP_42_234Hz.bin"},
    {"aw8697_clock_channel_RTP_43_234Hz.bin"},
    {"aw8697_long_sound_channel_RTP_44_230Hz.bin"},
    {"aw8697_short_channel_RTP_45_234Hz.bin"},
    {"aw8697_two_error_remaind_RTP_46_234Hz.bin"},

    {"aw8697_kill_program_RTP_47_234Hz.bin"},
    {"aw8697_Simple_channel_RTP_48.bin"},
    {"aw8697_Pure_RTP_49_234Hz.bin"},
    {"aw8697_reserved_sound_channel_RTP_50.bin"},

    {"aw8697_high_temp_high_humidity_channel_RTP_51.bin"},

    {"aw8697_old_steady_test_RTP_52.bin"},
    {"aw8697_listen_pop_53_230Hz.bin"},
    {"aw8697_desk_7_RTP_54_234Hz.bin"},
    {"aw8697_nfc_10_RTP_55_234Hz.bin"},
    {"aw8697_vibrator_remain_12_RTP_56_234Hz.bin"},
    {"aw8697_notice_13_RTP_57.bin"},
    {"aw8697_third_ring_14_RTP_58.bin"},
    {"aw8697_reserved_59.bin"},

    {"aw8697_honor_fisrt_kill_RTP_60_234Hz.bin"},
    {"aw8697_honor_two_kill_RTP_61_234Hz.bin"},
    {"aw8697_honor_three_kill_RTP_62_234Hz.bin"},
    {"aw8697_honor_four_kill_RTP_63_234Hz.bin"},
    {"aw8697_honor_five_kill_RTP_64_234Hz.bin"},
    {"aw8697_honor_three_continu_kill_RTP_65_234Hz.bin"},
    {"aw8697_honor_four_continu_kill_RTP_66_234Hz.bin"},
    {"aw8697_honor_unstoppable_RTP_67_234Hz.bin"},
    {"aw8697_honor_thousands_kill_RTP_68_234Hz.bin"},
    {"aw8697_honor_lengendary_RTP_69_234Hz.bin"},


    {"aw8697_reserved_70.bin"},
    {"aw8697_reserved_71.bin"},
    {"aw8697_reserved_72.bin"},
    {"aw8697_reserved_73.bin"},
    {"aw8697_reserved_74.bin"},
    {"aw8697_reserved_75.bin"},
    {"aw8697_reserved_76.bin"},
    {"aw8697_reserved_77.bin"},
    {"aw8697_reserved_78.bin"},
    {"aw8697_reserved_79.bin"},

    {"aw8697_reserved_80.bin"},
    {"aw8697_reserved_81.bin"},
    {"aw8697_reserved_82.bin"},
    {"aw8697_reserved_83.bin"},
    {"aw8697_reserved_84.bin"},
    {"aw8697_reserved_85.bin"},
    {"aw8697_reserved_86.bin"},
    {"aw8697_reserved_87.bin"},
    {"aw8697_reserved_88.bin"},
    {"aw8697_reserved_89.bin"},

    {"aw8697_reserved_90.bin"},
    {"aw8697_reserved_91.bin"},
    {"aw8697_reserved_92.bin"},
    {"aw8697_reserved_93.bin"},
    {"aw8697_reserved_94.bin"},
    {"aw8697_reserved_95.bin"},
    {"aw8697_reserved_96.bin"},
    {"aw8697_reserved_97.bin"},
    {"aw8697_reserved_98.bin"},
    {"aw8697_reserved_99.bin"},

    {"aw8697_soldier_first_kill_RTP_100_234Hz.bin"},
    {"aw8697_soldier_second_kill_RTP_101_234Hz.bin"},
    {"aw8697_soldier_third_kill_RTP_102_234Hz.bin"},
    {"aw8697_soldier_fourth_kill_RTP_103_234Hz.bin"},
    {"aw8697_soldier_fifth_kill_RTP_104_234Hz.bin"},
    {"aw8697_reserved_105.bin"},
    {"aw8697_reserved_106.bin"},
    {"aw8697_reserved_107.bin"},
    {"aw8697_reserved_108.bin"},
    {"aw8697_reserved_109.bin"},

    {"aw8697_reserved_110.bin"},
    {"aw8697_reserved_111.bin"},
    {"aw8697_reserved_112.bin"},
    {"aw8697_reserved_113.bin"},
    {"aw8697_reserved_114.bin"},
    {"aw8697_reserved_115.bin"},
    {"aw8697_reserved_116.bin"},
    {"aw8697_reserved_117.bin"},
    {"aw8697_reserved_118.bin"},
    {"aw8697_reserved_119.bin"},

    {"aw8697_Miss_RTP_120.bin"},
    {"aw8697_Scenic_RTP_121_234Hz.bin"},
    {"aw8697_reserved_122.bin"},
/* used for coloros 7 */
    {"aw8697_Appear_channel_RTP_coloros7_123_234Hz.bin"},
    {"aw8697_Miss_RTP_coloros7_124_234Hz.bin"},
    {"aw8697_Music_channel_RTP_coloros7_125_234Hz.bin"},
    {"aw8697_Percussion_channel_RTP_coloros7_126_234Hz.bin"},
    {"aw8697_Ripple_channel_RTP_coloros7_127_234Hz.bin"},
    {"aw8697_Bright_channel_RTP_coloros7_128_234Hz.bin"},
    {"aw8697_Fun_channel_RTP_coloros7_129_234Hz.bin"},
    {"aw8697_Glittering_channel_RTP_coloros7_130_234Hz.bin"},
    {"aw8697_Harp_channel_RTP_coloros7_131_234Hz.bin"},
    {"aw8697_Overtone_channel_RTP_coloros7_132_234Hz.bin"},
    {"aw8697_Simple_channel_RTP_coloros7_133_234Hz.bin"},

    {"aw8697_Seine_past_RTP_coloros7_134_234Hz.bin"},
    {"aw8697_Classical_ring_RTP_coloros7_135_234Hz.bin"},
    {"aw8697_Long_for_RTP_coloros7_136_234Hz.bin"},
    {"aw8697_Romantic_RTP_coloros7_137_234Hz.bin"},
    {"aw8697_Bliss_RTP_coloros7_138_234Hz.bin"},
    {"aw8697_Dream_RTP_coloros7_139_234Hz.bin"},
    {"aw8697_Relax_RTP_coloros7_140_234Hz.bin"},
    {"aw8697_Joy_channel_RTP_coloros7_141_234Hz.bin"},
    {"aw8697_weather_wind_RTP_coloros7_142_234Hz.bin"},
    {"aw8697_weather_cloudy_RTP_coloros7_143_234Hz.bin"},
    {"aw8697_weather_thunderstorm_RTP_coloros7_144_234Hz.bin"},
    {"aw8697_weather_default_RTP_coloros7_145_234Hz.bin"},
    {"aw8697_weather_sunny_RTP_coloros7_146_234Hz.bin"},
    {"aw8697_weather_smog_RTP_coloros7_147_234Hz.bin"},
    {"aw8697_weather_snow_RTP_coloros7_148_234Hz.bin"},
    {"aw8697_weather_rain_RTP_coloros7_149_234Hz.bin"},

#endif
    {"aw8697_rtp_lighthouse.bin"},
    {"aw8697_rtp_silk_19081.bin"},
};

#define AW8697_RTP_NAME_INDEX_REALME_START 161
#define AW8697_RTP_NAME_INDEX_REALME_END 550
//Only used for realme, rtp index range is 500-550 
static char aw8697_rtp_name_realme[][AW8697_RTP_NAME_MAX] = {
    {"aw8697_realme_its_realme_RTP_500.bin"}, 
    {"aw8697_realme_tune_RTP_501.bin"},
    {"aw8697_realme_jingle_RTP_502.bin"},
};

static int aw8697_is_rtp_index_valid(struct aw8697 *aw8697, unsigned int rtp_file_num)
{
    if (aw8697->device_id == 815) {
        return rtp_file_num < (sizeof(aw8697_rtp_name)/AW8697_RTP_NAME_MAX) ? 1 : 0;
    } else {
        return rtp_file_num < (sizeof(aw8697_rtp_name_19065)/AW8697_RTP_NAME_MAX) ? 1 : 
                    ((rtp_file_num>=AW8697_RTP_NAME_INDEX_REALME_START) && 
                    (rtp_file_num<AW8697_RTP_NAME_INDEX_REALME_START+sizeof(aw8697_rtp_name_realme)/AW8697_RTP_NAME_MAX)?1:0);
    }
}

static char* aw8697_get_rtp_name(struct aw8697 *aw8697, unsigned int rtp_file_num)
{
    if(!aw8697_is_rtp_index_valid(aw8697, rtp_file_num))
    {
        pr_err("%s: invalid rtp_file_num = %d\n", __func__, rtp_file_num);
        if (aw8697->device_id == 815) {
            return aw8697_rtp_name[0];
        } else {
            return aw8697_rtp_name_19065[0];
        }
    }

    if( rtp_file_num >= AW8697_RTP_NAME_INDEX_REALME_START && rtp_file_num <= AW8697_RTP_NAME_INDEX_REALME_END)
    {
        return aw8697_rtp_name_realme[rtp_file_num - AW8697_RTP_NAME_INDEX_REALME_START];
    }else if (aw8697->device_id == 815) {
        return aw8697_rtp_name[rtp_file_num];
    } else {
        return aw8697_rtp_name_19065[rtp_file_num];
    }
}

static struct aw8697_container *aw8697_rtp = NULL;
static struct aw8697 *g_aw8697;
#define  AW8697_CONTAINER_DEFAULT_SIZE  (2 * 1024 * 1024)   ////  2M
static int aw8697_container_size = AW8697_CONTAINER_DEFAULT_SIZE;

static int aw8697_container_init(int size)
{
    if (!aw8697_rtp || size > aw8697_container_size) {
        if (aw8697_rtp) {
            vfree(aw8697_rtp);
        }
        aw8697_rtp = vmalloc(size);
        if (!aw8697_rtp) {
            pr_err("%s: error allocating memory\n", __func__);
            return -1;
        }
        aw8697_container_size = size;
    }

    memset(aw8697_rtp, 0, size);

    return 0;
}

/******************************************************
 *
 * functions
 *
 ******************************************************/
static void aw8697_interrupt_clear(struct aw8697 *aw8697);
static int aw8697_haptic_trig_enable_config(struct aw8697 *aw8697);
static int aw8697_haptic_juge_RTP_is_going_on(struct aw8697 *aw8697);


 /******************************************************
 *
 * aw8697 i2c write/read
 *
 ******************************************************/
static int aw8697_i2c_write(struct aw8697 *aw8697,
         unsigned char reg_addr, unsigned char reg_data)
{
    int ret = -1;
    unsigned char cnt = 0;

    if (reg_addr == 0x04 || reg_addr == 0x05) {
        pr_debug("%s: reg_addr_0x%02x = 0x%02x\n", __func__, reg_addr, reg_data);
    }
    while(cnt < AW_I2C_RETRIES) {
        ret = i2c_smbus_write_byte_data(aw8697->i2c, reg_addr, reg_data);
        if(ret < 0) {
            pr_err("%s: i2c_write cnt=%d error=%d\n", __func__, cnt, ret);
        } else {
            break;
        }
        cnt ++;
        msleep(AW_I2C_RETRY_DELAY);
    }

    return ret;
}

static int aw8697_i2c_read(struct aw8697 *aw8697,
        unsigned char reg_addr, unsigned char *reg_data)
{
    int ret = -1;
    unsigned char cnt = 0;

    while(cnt < AW_I2C_RETRIES) {
        ret = i2c_smbus_read_byte_data(aw8697->i2c, reg_addr);
        if(ret < 0) {
            pr_err("%s: i2c_read cnt=%d error=%d\n", __func__, cnt, ret);
        } else {
            *reg_data = ret;
            break;
        }
        cnt ++;
        msleep(AW_I2C_RETRY_DELAY);
    }

    return ret;
}

static int aw8697_i2c_write_bits(struct aw8697 *aw8697,
         unsigned char reg_addr, unsigned int mask, unsigned char reg_data)
{
    unsigned char reg_val = 0;

    aw8697_i2c_read(aw8697, reg_addr, &reg_val);
    reg_val &= mask;
    reg_val |= reg_data;
    aw8697_i2c_write(aw8697, reg_addr, reg_val);

    return 0;
}

static int aw8697_i2c_writes(struct aw8697 *aw8697,
        unsigned char reg_addr, unsigned char *buf, unsigned int len)
{
    int ret = -1;
    unsigned char *data;

    data = kmalloc(len+1, GFP_KERNEL);
    if (data == NULL) {
        pr_err("%s: can not allocate memory\n", __func__);
        return  -ENOMEM;
    }

    data[0] = reg_addr;
    memcpy(&data[1], buf, len);

    ret = i2c_master_send(aw8697->i2c, data, len+1);
    if (ret < 0) {
        pr_err("%s: i2c master send error\n", __func__);
    }

    kfree(data);

    return ret;
}

static int aw8697_i2c_reads(struct aw8697 *aw8697,
        unsigned char reg_addr, unsigned char *buf, unsigned int len)
{
    int ret = -1;

    ret = i2c_smbus_write_byte(aw8697->i2c, reg_addr);
    if (ret) {
        pr_err("%s: couldn't send request, ret=%d\n",
            __func__, ret);
        return ret;
    }
    ret = i2c_master_recv(aw8697->i2c, buf, len);
    if (ret != len) {
        pr_err("%s: couldn't read registers, return %d bytes\n",
            __func__,  ret);
        return ret;
    }

    return ret;
}

/*****************************************************
 *
 * ram update
 *
 *****************************************************/
static void aw8697_rtp_loaded(const struct firmware *cont, void *context)
{
    struct aw8697 *aw8697 = context;
    int ret = 0;
    pr_err("%s enter\n", __func__);

    if (!cont) {
        pr_err("%s: failed to read %s\n", __func__, aw8697_get_rtp_name(aw8697, aw8697->rtp_file_num));
        release_firmware(cont);
        return;
    }

    pr_err("%s: loaded %s - size: %zu\n", __func__, aw8697_get_rtp_name(aw8697, aw8697->rtp_file_num),
                    cont ? cont->size : 0);

    /* aw8697 rtp update */
    mutex_lock(&aw8697->rtp_lock);
    #ifndef VENDOR_EDIT
    /* tongfeng.Huang@BSP.CHG.Basic, 2019/03/11,  Add for alloc memory */
    aw8697_rtp = kzalloc(cont->size+sizeof(int), GFP_KERNEL);   
    if (!aw8697_rtp) {
        release_firmware(cont);
        mutex_unlock(&aw8697->rtp_lock);
        pr_err("%s: Error allocating memory\n", __func__);
        return;
    }
    #else
    ret = aw8697_container_init(cont->size+sizeof(int));
    if (ret < 0) {
        release_firmware(cont);
        mutex_unlock(&aw8697->rtp_lock);
        pr_err("%s: Error allocating memory\n", __func__);
        return;
    }
    #endif
    aw8697_rtp->len = cont->size;
    pr_err("%s: rtp size = %d\n", __func__, aw8697_rtp->len);
    memcpy(aw8697_rtp->data, cont->data, cont->size);
    release_firmware(cont);
    mutex_unlock(&aw8697->rtp_lock);

    aw8697->rtp_init = 1;
    pr_err("%s: rtp update complete\n", __func__);
}

static int aw8697_rtp_update(struct aw8697 *aw8697)
{
    pr_err("%s enter\n", __func__);

    return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
                aw8697_get_rtp_name(aw8697, aw8697->rtp_file_num), aw8697->dev, GFP_KERNEL,
                aw8697, aw8697_rtp_loaded);
}


 static void aw8697_container_update(struct aw8697 *aw8697,
        struct aw8697_container *aw8697_cont)
{
    int i = 0;
    unsigned int shift = 0;

    pr_err("%s enter\n", __func__);

    mutex_lock(&aw8697->lock);

    aw8697->ram.baseaddr_shift = 2;
    aw8697->ram.ram_shift = 4;

    /* RAMINIT Enable */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_EN);

    /* base addr */
    shift = aw8697->ram.baseaddr_shift;
    aw8697->ram.base_addr = (unsigned int)((aw8697_cont->data[0+shift]<<8) |
            (aw8697_cont->data[1+shift]));
    pr_err("%s: base_addr=0x%4x\n", __func__, aw8697->ram.base_addr);

    aw8697_i2c_write(aw8697, AW8697_REG_BASE_ADDRH, aw8697_cont->data[0+shift]);
    aw8697_i2c_write(aw8697, AW8697_REG_BASE_ADDRL, aw8697_cont->data[1+shift]);

    aw8697_i2c_write(aw8697, AW8697_REG_FIFO_AEH,
                    (unsigned char)((aw8697->ram.base_addr>>1)>>8));
    aw8697_i2c_write(aw8697, AW8697_REG_FIFO_AEL,
                    (unsigned char)((aw8697->ram.base_addr>>1)&0x00FF));
    aw8697_i2c_write(aw8697, AW8697_REG_FIFO_AFH,
                    (unsigned char)((aw8697->ram.base_addr-(aw8697->ram.base_addr>>2))>>8));
    aw8697_i2c_write(aw8697, AW8697_REG_FIFO_AFL,
                    (unsigned char)((aw8697->ram.base_addr-(aw8697->ram.base_addr>>2))&0x00FF));

    /* ram */
    shift = aw8697->ram.baseaddr_shift;
    aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRH, aw8697_cont->data[0+shift]);
    aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRL, aw8697_cont->data[1+shift]);
    shift = aw8697->ram.ram_shift;
    for(i=shift; i<aw8697_cont->len; i++) {
        aw8697_i2c_write(aw8697, AW8697_REG_RAMDATA, aw8697_cont->data[i]);
    }

#if 0
    /* ram check */
    shift = aw8697->ram.baseaddr_shift;
    aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRH, aw8697_cont->data[0+shift]);
    aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRL, aw8697_cont->data[1+shift]);
    shift = aw8697->ram.ram_shift;
    for(i=shift; i<aw8697_cont->len; i++) {
        aw8697_i2c_read(aw8697, AW8697_REG_RAMDATA, &reg_val);
        if(reg_val != aw8697_cont->data[i]) {
            pr_err("%s: ram check error addr=0x%04x, file_data=0x%02x, ram_data=0x%02x\n",
                        __func__, i, aw8697_cont->data[i], reg_val);
            return;
        }
    }
#endif

    /* RAMINIT Disable */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_OFF);

    mutex_unlock(&aw8697->lock);

    pr_err("%s exit\n", __func__);
}


static void aw8697_ram_loaded(const struct firmware *cont, void *context)
{
    struct aw8697 *aw8697 = context;
    struct aw8697_container *aw8697_fw;
    int i = 0;
    unsigned short check_sum = 0;

    pr_err("%s enter\n", __func__);

    if (!cont) {
        pr_err("%s: failed to read %s\n", __func__, aw8697_ram_name);
        release_firmware(cont);
        return;
    }

    //pr_err("%s: loaded %s - size: %zu\n", __func__, aw8697_ram_name,
    ///                cont ? cont->size : 0);
/*
    for(i=0; i<cont->size; i++) {
        pr_err("%s: addr:0x%04x, data:0x%02x\n", __func__, i, *(cont->data+i));
    }
*/

    /* check sum */
    for(i=2; i<cont->size; i++) {
        check_sum += cont->data[i];
    }
    if(check_sum != (unsigned short)((cont->data[0]<<8)|(cont->data[1]))) {
        pr_err("%s: check sum err: check_sum=0x%04x\n", __func__, check_sum);
        return;
    } else {
        pr_err("%s: check sum pass : 0x%04x\n", __func__, check_sum);
        aw8697->ram.check_sum = check_sum;
    }

    /* aw8697 ram update */
    aw8697_fw = vmalloc(cont->size+sizeof(int));
    if (!aw8697_fw) {
        release_firmware(cont);
        pr_err("%s: Error allocating memory\n", __func__);
        return;
    }
    memset(aw8697_fw, 0, cont->size+sizeof(int));
    aw8697_fw->len = cont->size;
    memcpy(aw8697_fw->data, cont->data, cont->size);
    release_firmware(cont);

    aw8697_container_update(aw8697, aw8697_fw);

    aw8697->ram.len = aw8697_fw->len;

    vfree(aw8697_fw);

    aw8697->ram_init = 1;
    pr_err("%s: fw update complete\n", __func__);

    aw8697_haptic_trig_enable_config(aw8697);

    aw8697_rtp_update(aw8697);
}

static int aw8697_ram_update(struct aw8697 *aw8697)
{
    unsigned char index = 0;
    aw8697->ram_init = 0;
    aw8697->rtp_init = 0;

    //f0 165 166,166
    // 167 168, 168
    //169 170 ,170
    //171 172, 172
    //173 174,174
    //>175 ,174
    if(aw8697->device_id == 815) {
	    if (aw8697->f0 < OPPO_F0_VAL_MIN_0815 || aw8697->f0 > OPPO_F0_VAL_MAX_0815) {
	        aw8697->f0 = OPPO_F0_VAL_TYPICAL_0815;
	    }
    } else if(aw8697->device_id == 832) {
	    if (aw8697->f0 < OPPO_F0_VAL_MIN_0832 || aw8697->f0 > OPPO_F0_VAL_MAX_0832) {
	        aw8697->f0 = OPPO_F0_VAL_TYPICAL_0832;
	    }
    }
    aw8697->haptic_real_f0 = (aw8697->f0 / 10);// get f0 from nvram
    pr_err("%s: aw8697->haptic_real_f0 [%d]\n", __func__, aw8697->haptic_real_f0);
/*
    if (aw8697->haptic_real_f0 <167)
    {
        index = 0;
    }
    else if(aw8697->haptic_real_f0 <169)
    {
        index = 1;
    }
    else if(aw8697->haptic_real_f0 <171)
    {   
        index = 2;
    }
    else if(aw8697->haptic_real_f0 <173)
    {   
        index = 3;
    }
    else
    {
        index = 4;
    }
*/
    if (aw8697->device_id == 832) {
        pr_err("%s:19065 haptic bin name  %s \n", __func__,aw8697_ram_name_19065[index]);
        return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
                    aw8697_ram_name_19065[index], aw8697->dev, GFP_KERNEL,
                    aw8697, aw8697_ram_loaded);
    } else {
        pr_err("%s:haptic bin name  %s \n", __func__,aw8697_ram_name[index]);
        return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
                    aw8697_ram_name[index], aw8697->dev, GFP_KERNEL,
                    aw8697, aw8697_ram_loaded);
    }
}

#ifdef AWINIC_RAM_UPDATE_DELAY
static void aw8697_ram_work_routine(struct work_struct *work)
{
    struct aw8697 *aw8697 = container_of(work, struct aw8697, ram_work.work);

    pr_err("%s enter\n", __func__);

    aw8697_ram_update(aw8697);

}
#endif

static int aw8697_ram_init(struct aw8697 *aw8697)
{
#ifdef AWINIC_RAM_UPDATE_DELAY
    int ram_timer_val = 5000;
    INIT_DELAYED_WORK(&aw8697->ram_work, aw8697_ram_work_routine);
    schedule_delayed_work(&aw8697->ram_work, msecs_to_jiffies(ram_timer_val));
#else
    aw8697_ram_update(aw8697);
#endif
    return 0;
}



/*****************************************************
 *
 * haptic control
 *
 *****************************************************/
static int aw8697_haptic_softreset(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);

    aw8697_i2c_write(aw8697, AW8697_REG_ID, 0xAA);
    usleep_range(3000, 3500);
    return 0;
}

static int aw8697_haptic_active(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_WORK_MODE_MASK, AW8697_BIT_SYSCTRL_ACTIVE);
    aw8697_interrupt_clear(aw8697);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
            AW8697_BIT_SYSINTM_UVLO_MASK, AW8697_BIT_SYSINTM_UVLO_EN);
    return 0;
}

static int aw8697_haptic_play_mode(struct aw8697 *aw8697, unsigned char play_mode)
{
    pr_debug("%s enter\n", __func__);

    switch(play_mode) {
        case AW8697_HAPTIC_STANDBY_MODE:
            aw8697->play_mode = AW8697_HAPTIC_STANDBY_MODE;
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
                    AW8697_BIT_SYSINTM_UVLO_MASK, AW8697_BIT_SYSINTM_UVLO_OFF);
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_WORK_MODE_MASK, AW8697_BIT_SYSCTRL_STANDBY);
            break;
        case AW8697_HAPTIC_RAM_MODE:
            aw8697->play_mode = AW8697_HAPTIC_RAM_MODE;
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_PLAY_MODE_MASK, AW8697_BIT_SYSCTRL_PLAY_MODE_RAM);
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BYPASS);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RAM_DISABLE);
            }
            aw8697_haptic_active(aw8697);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RAM_ENABLE);
                aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                        AW8697_BIT_SYSCTRL_BST_MODE_MASK&AW8697_BIT_SYSCTRL_WORK_MODE_MASK,
                        AW8697_BIT_SYSCTRL_BST_MODE_BOOST|AW8697_BIT_SYSCTRL_STANDBY);
                aw8697_haptic_active(aw8697);
            } else {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                        AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BOOST);
            }
            mdelay(2);
            break;
        case AW8697_HAPTIC_RAM_LOOP_MODE:
            aw8697->play_mode = AW8697_HAPTIC_RAM_LOOP_MODE;
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_PLAY_MODE_MASK, AW8697_BIT_SYSCTRL_PLAY_MODE_RAM);
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BYPASS);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RAM_DISABLE);
            }

           aw8697_haptic_active(aw8697);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RAM_ENABLE);
                aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                        AW8697_BIT_SYSCTRL_BST_MODE_MASK&AW8697_BIT_SYSCTRL_WORK_MODE_MASK,
                        AW8697_BIT_SYSCTRL_BST_MODE_BOOST|AW8697_BIT_SYSCTRL_STANDBY);
                aw8697_haptic_active(aw8697);
            } else {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                        AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BOOST);
            }
            mdelay(2);
            break;
        case AW8697_HAPTIC_RTP_MODE:
            aw8697->play_mode = AW8697_HAPTIC_RTP_MODE;
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_PLAY_MODE_MASK, AW8697_BIT_SYSCTRL_PLAY_MODE_RTP);
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BYPASS);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RTP_DISABLE);
            }
            aw8697_haptic_active(aw8697);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RTP_ENABLE);
                aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                        AW8697_BIT_SYSCTRL_BST_MODE_MASK&AW8697_BIT_SYSCTRL_WORK_MODE_MASK,
                        AW8697_BIT_SYSCTRL_BST_MODE_BOOST|AW8697_BIT_SYSCTRL_STANDBY);
                aw8697_haptic_active(aw8697);
            } else {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                        AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BOOST);
            }
            mdelay(2);
            break;
        case AW8697_HAPTIC_TRIG_MODE:
            aw8697->play_mode = AW8697_HAPTIC_TRIG_MODE;
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_PLAY_MODE_MASK, AW8697_BIT_SYSCTRL_PLAY_MODE_RAM);
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BYPASS);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RAM_DISABLE);
            }
            aw8697_haptic_active(aw8697);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RAM_ENABLE);
                aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                        AW8697_BIT_SYSCTRL_BST_MODE_MASK&AW8697_BIT_SYSCTRL_WORK_MODE_MASK,
                        AW8697_BIT_SYSCTRL_BST_MODE_BOOST|AW8697_BIT_SYSCTRL_STANDBY);
                aw8697_haptic_active(aw8697);
            } else {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                        AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BOOST);
            }
            mdelay(2);
            break;
        case AW8697_HAPTIC_CONT_MODE:
            aw8697->play_mode = AW8697_HAPTIC_CONT_MODE;
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_PLAY_MODE_MASK, AW8697_BIT_SYSCTRL_PLAY_MODE_CONT);
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BYPASS);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RAM_DISABLE);
            }
            aw8697_haptic_active(aw8697);
            break;
        default:
            dev_err(aw8697->dev, "%s: play mode %d err",
                    __func__, play_mode);
            break;
    }
    return 0;
}

static int aw8697_haptic_play_go(struct aw8697 *aw8697, bool flag)
{
    unsigned char reg_val = 0;
    pr_debug("%s enter\n", __func__);

    if(flag == true) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_GO,
            AW8697_BIT_GO_MASK, AW8697_BIT_GO_ENABLE);
        mdelay(2);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_GO,
            AW8697_BIT_GO_MASK, AW8697_BIT_GO_DISABLE);
    }

    aw8697_i2c_read(aw8697, AW8697_REG_GLB_STATE, &reg_val);
    //pr_err("%s reg_val[%d] \n", __func__, reg_val);
    return 0;
}

static int aw8697_set_clock(struct aw8697 *aw8697, int clock_type)
{
    if (clock_type == AW8697_HAPTIC_CLOCK_CALI_F0)
    {
        aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, aw8697->clock_system_f0_cali_lra);
    } else if (clock_type == AW8697_HAPTIC_CLOCK_CALI_OSC_STANDARD) {
        aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, aw8697->clock_standard_OSC_lra_rtim_code);
    }
    return 0;
}

#if 1

static void aw8697_haptic_reset_init(struct aw8697 *aw8697);

static int aw8697_haptic_stop_delay(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;
    unsigned int cnt = 200;

    while(cnt--) {
        aw8697_i2c_read(aw8697, AW8697_REG_GLB_STATE, &reg_val);
        //pr_err("%s wait for standby, reg glb_state=0x%02x\n",  __func__, reg_val);
        if((reg_val&0x0f) == 0x00) {
            return 0;
        }
        mdelay(2);
    }
    pr_err("%s do not enter standby automatically\n", __func__);
    aw8697_haptic_softreset(aw8697);    
    aw8697_haptic_reset_init(aw8697);

    return 0;
}
static int aw8697_haptic_stop(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);
    aw8697->play_mode = AW8697_HAPTIC_STANDBY_MODE;

    aw8697_haptic_play_go(aw8697, false);
    aw8697_haptic_stop_delay(aw8697);
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_STANDBY_MODE);
    //set clock to cali f0
    aw8697_set_clock(aw8697, AW8697_HAPTIC_CLOCK_CALI_F0);

    return 0;
}
#else
// new stop 

static int aw8697_haptic_android_stop(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_STANDBY_MODE);
    return 0;
}
#endif
static int aw8697_haptic_start(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);

    aw8697_haptic_play_go(aw8697, true);

    return 0;
}

static int aw8697_haptic_set_wav_seq(struct aw8697 *aw8697,
        unsigned char wav, unsigned char seq)
{
    aw8697_i2c_write(aw8697, AW8697_REG_WAVSEQ1+wav,
            seq);
    return 0;
}

static int aw8697_haptic_set_wav_loop(struct aw8697 *aw8697,
        unsigned char wav, unsigned char loop)
{
    unsigned char tmp = 0;

    if(wav%2) {
        tmp = loop<<0;
        aw8697_i2c_write_bits(aw8697, AW8697_REG_WAVLOOP1+(wav/2),
                AW8697_BIT_WAVLOOP_SEQNP1_MASK, tmp);
    } else {
        tmp = loop<<4;
        aw8697_i2c_write_bits(aw8697, AW8697_REG_WAVLOOP1+(wav/2),
                AW8697_BIT_WAVLOOP_SEQN_MASK, tmp);
    }

    return 0;
}
/*
static int aw8697_haptic_set_main_loop(struct aw8697 *aw8697,
        unsigned char loop)
{
    aw8697_i2c_write(aw8697, AW8697_REG_MAIN_LOOP, loop);
    return 0;
}
*/

static int aw8697_haptic_set_repeat_wav_seq(struct aw8697 *aw8697, unsigned char seq)
{
    aw8697_haptic_set_wav_seq(aw8697, 0x00, seq);
    aw8697_haptic_set_wav_loop(aw8697, 0x00, AW8697_BIT_WAVLOOP_INIFINITELY);

    return 0;
}


static int aw8697_haptic_set_bst_vol(struct aw8697 *aw8697, unsigned char bst_vol)
{
    if(bst_vol & 0xe0) {
        bst_vol = 0x1f;
    }
    aw8697_i2c_write_bits(aw8697, AW8697_REG_BSTDBG4,
            AW8697_BIT_BSTDBG4_BSTVOL_MASK, (bst_vol<<1));
    return 0;
}

static int aw8697_haptic_set_bst_peak_cur(struct aw8697 *aw8697, unsigned char peak_cur)
{
    peak_cur &= AW8697_BSTCFG_PEAKCUR_LIMIT;
    aw8697_i2c_write_bits(aw8697, AW8697_REG_BSTCFG,
            AW8697_BIT_BSTCFG_PEAKCUR_MASK, peak_cur);
    return 0;
}

static int aw8697_haptic_set_gain(struct aw8697 *aw8697, unsigned char gain)
{
    aw8697_i2c_write(aw8697, AW8697_REG_DATDBG, gain);
    return 0;
}

static int aw8697_haptic_set_pwm(struct aw8697 *aw8697, unsigned char mode)
{
    switch(mode) {
        case AW8697_PWM_48K:
            aw8697_i2c_write_bits(aw8697, AW8697_REG_PWMDBG,
                AW8697_BIT_PWMDBG_PWM_MODE_MASK, AW8697_BIT_PWMDBG_PWM_48K);
            break;
        case AW8697_PWM_24K:
            aw8697_i2c_write_bits(aw8697, AW8697_REG_PWMDBG,
                AW8697_BIT_PWMDBG_PWM_MODE_MASK, AW8697_BIT_PWMDBG_PWM_24K);
            break;
        case AW8697_PWM_12K:
            aw8697_i2c_write_bits(aw8697, AW8697_REG_PWMDBG,
                AW8697_BIT_PWMDBG_PWM_MODE_MASK, AW8697_BIT_PWMDBG_PWM_12K);
            break;
        default:
            break;
    }
    return 0;
}

static int aw8697_haptic_play_wav_seq(struct aw8697 *aw8697, unsigned char flag)
{
    pr_debug("%s enter\n", __func__);

    if(flag) {
        aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_RAM_MODE);
        aw8697_haptic_start(aw8697);
    }
    return 0;
}

static int aw8697_haptic_play_repeat_seq(struct aw8697 *aw8697, unsigned char flag)
{
    pr_debug("%s enter\n", __func__);

    if(flag) {
        aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_RAM_LOOP_MODE);
        aw8697_haptic_start(aw8697);
    }

    return 0;
}

/*****************************************************
 *
 * motor protect
 *
 *****************************************************/
static int aw8697_haptic_swicth_motorprotect_config(struct aw8697 *aw8697, unsigned char addr, unsigned char val)
{
    pr_debug("%s enter\n", __func__);
    if(addr == 1) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_DETCTRL,
                AW8697_BIT_DETCTRL_PROTECT_MASK, AW8697_BIT_DETCTRL_PROTECT_SHUTDOWN);
        aw8697_i2c_write_bits(aw8697, AW8697_REG_PWMPRC,
                AW8697_BIT_PWMPRC_PRC_MASK, AW8697_BIT_PWMPRC_PRC_ENABLE);
        aw8697_i2c_write_bits(aw8697, AW8697_REG_PRLVL,
                AW8697_BIT_PRLVL_PR_MASK, AW8697_BIT_PRLVL_PR_ENABLE);
    } else if (addr == 0) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_DETCTRL,
                AW8697_BIT_DETCTRL_PROTECT_MASK,  AW8697_BIT_DETCTRL_PROTECT_NO_ACTION);
        aw8697_i2c_write_bits(aw8697, AW8697_REG_PWMPRC,
                AW8697_BIT_PWMPRC_PRC_MASK, AW8697_BIT_PWMPRC_PRC_DISABLE);
        aw8697_i2c_write_bits(aw8697, AW8697_REG_PRLVL,
                AW8697_BIT_PRLVL_PR_MASK, AW8697_BIT_PRLVL_PR_DISABLE);
    } else if (addr == 0x2d){
        aw8697_i2c_write_bits(aw8697, AW8697_REG_PWMPRC,
                AW8697_BIT_PWMPRC_PRCTIME_MASK, val);
    }else if (addr == 0x3e){
        aw8697_i2c_write_bits(aw8697, AW8697_REG_PRLVL,
                AW8697_BIT_PRLVL_PRLVL_MASK, val);
    }else if (addr == 0x3f){
        aw8697_i2c_write_bits(aw8697, AW8697_REG_PRTIME,
                AW8697_BIT_PRTIME_PRTIME_MASK, val);
    } else{
        /*nothing to do;*/
    }
     return 0;
}

/*****************************************************
 *
 * os calibration
 *
 *****************************************************/
static int aw8697_haptic_os_calibration(struct aw8697 *aw8697)
{
    unsigned int cont = 2000;
    unsigned char reg_val = 0;
    pr_debug("%s enter\n", __func__);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_EN);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DETCTRL,
            AW8697_BIT_DETCTRL_DIAG_GO_MASK, AW8697_BIT_DETCTRL_DIAG_GO_ENABLE);
    while(1){
        aw8697_i2c_read(aw8697, AW8697_REG_DETCTRL, &reg_val);
        if((reg_val & 0x01) == 0 || cont ==0)
            break;
         cont--;
    }
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_OFF);
    return 0;
}
/*****************************************************
 *
 * trig config
 *
 *****************************************************/
static int aw8697_haptic_trig_param_init(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);

#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2019/01/7,  Add for disable trig */
    aw8697->trig[0].enable = 0;
#else
    aw8697->trig[0].enable = AW8697_TRG1_ENABLE;
#endif
    aw8697->trig[0].default_level = AW8697_TRG1_DEFAULT_LEVEL;
    aw8697->trig[0].dual_edge = AW8697_TRG1_DUAL_EDGE;
    aw8697->trig[0].frist_seq = AW8697_TRG1_FIRST_EDGE_SEQ;
    aw8697->trig[0].second_seq = AW8697_TRG1_SECOND_EDGE_SEQ;
#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2019/01/7,  Add for disable trig*/
    aw8697->trig[1].enable = 0;
#else
    aw8697->trig[1].enable = AW8697_TRG2_ENABLE;
#endif
    aw8697->trig[1].default_level = AW8697_TRG2_DEFAULT_LEVEL;
    aw8697->trig[1].dual_edge = AW8697_TRG2_DUAL_EDGE;
    aw8697->trig[1].frist_seq = AW8697_TRG2_FIRST_EDGE_SEQ;
    aw8697->trig[1].second_seq = AW8697_TRG2_SECOND_EDGE_SEQ;
#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2019/1/7,  Add for disable trig*/
    aw8697->trig[2].enable = 0;
#else
    aw8697->trig[2].enable = AW8697_TRG3_ENABLE;
#endif
    aw8697->trig[2].default_level = AW8697_TRG3_DEFAULT_LEVEL;
    aw8697->trig[2].dual_edge = AW8697_TRG3_DUAL_EDGE;
    aw8697->trig[2].frist_seq = AW8697_TRG3_FIRST_EDGE_SEQ;
    aw8697->trig[2].second_seq = AW8697_TRG3_SECOND_EDGE_SEQ;

    return 0;
}

static int aw8697_haptic_trig_param_config(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);

    if(aw8697->trig[0].default_level) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG1_POLAR_MASK, AW8697_BIT_TRGCFG1_TRG1_POLAR_NEG);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG1_POLAR_MASK, AW8697_BIT_TRGCFG1_TRG1_POLAR_POS);
    }
    if(aw8697->trig[1].default_level) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG2_POLAR_MASK, AW8697_BIT_TRGCFG1_TRG2_POLAR_NEG);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG2_POLAR_MASK, AW8697_BIT_TRGCFG1_TRG2_POLAR_POS);
    }
    if(aw8697->trig[2].default_level) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG3_POLAR_MASK, AW8697_BIT_TRGCFG1_TRG3_POLAR_NEG);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG3_POLAR_MASK, AW8697_BIT_TRGCFG1_TRG3_POLAR_POS);
    }

    if(aw8697->trig[0].dual_edge) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG1_EDGE_MASK, AW8697_BIT_TRGCFG1_TRG1_EDGE_POS_NEG);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG1_EDGE_MASK, AW8697_BIT_TRGCFG1_TRG1_EDGE_POS);
    }
    if(aw8697->trig[1].dual_edge) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG2_EDGE_MASK, AW8697_BIT_TRGCFG1_TRG2_EDGE_POS_NEG);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG2_EDGE_MASK, AW8697_BIT_TRGCFG1_TRG2_EDGE_POS);
    }
    if(aw8697->trig[2].dual_edge) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG3_EDGE_MASK, AW8697_BIT_TRGCFG1_TRG3_EDGE_POS_NEG);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG3_EDGE_MASK, AW8697_BIT_TRGCFG1_TRG3_EDGE_POS);
    }

    if(aw8697->trig[0].frist_seq) {
        aw8697_i2c_write(aw8697, AW8697_REG_TRG1_WAV_P, aw8697->trig[0].frist_seq);
    }
    if(aw8697->trig[0].second_seq && aw8697->trig[0].dual_edge) {
        aw8697_i2c_write(aw8697, AW8697_REG_TRG1_WAV_N, aw8697->trig[0].second_seq);
    }
    if(aw8697->trig[1].frist_seq) {
        aw8697_i2c_write(aw8697, AW8697_REG_TRG2_WAV_P, aw8697->trig[1].frist_seq);
    }
    if(aw8697->trig[1].second_seq && aw8697->trig[1].dual_edge) {
        aw8697_i2c_write(aw8697, AW8697_REG_TRG2_WAV_N, aw8697->trig[1].second_seq);
    }
    if(aw8697->trig[2].frist_seq) {
        aw8697_i2c_write(aw8697, AW8697_REG_TRG3_WAV_P, aw8697->trig[1].frist_seq);
    }
    if(aw8697->trig[2].second_seq && aw8697->trig[2].dual_edge) {
        aw8697_i2c_write(aw8697, AW8697_REG_TRG3_WAV_N, aw8697->trig[1].second_seq);
    }

    return 0;
}

static int aw8697_haptic_trig_enable_config(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG2,
            AW8697_BIT_TRGCFG2_TRG1_ENABLE_MASK, aw8697->trig[0].enable << 0);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG2,
            AW8697_BIT_TRGCFG2_TRG2_ENABLE_MASK, aw8697->trig[1].enable << 1);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG2,
            AW8697_BIT_TRGCFG2_TRG3_ENABLE_MASK, aw8697->trig[2].enable << 2);

    return 0;
}


static int aw8697_haptic_auto_boost_config(struct aw8697 *aw8697, unsigned char flag)
{
    aw8697->auto_boost = flag;
    if(flag) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                AW8697_BIT_BST_AUTO_BST_AUTOSW_MASK, AW8697_BIT_BST_AUTO_BST_AUTOMATIC_BOOST);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                AW8697_BIT_BST_AUTO_BST_AUTOSW_MASK, AW8697_BIT_BST_AUTO_BST_MANUAL_BOOST);
    }
    return 0;
}

/*****************************************************
 *
 * vbat mode
 *
 *****************************************************/
static int aw8697_haptic_cont_vbat_mode(struct aw8697 *aw8697, unsigned char flag)
{
    if(flag == AW8697_HAPTIC_CONT_VBAT_HW_COMP_MODE) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_ADCTEST,
                AW8697_BIT_ADCTEST_VBAT_MODE_MASK, AW8697_BIT_ADCTEST_VBAT_HW_COMP);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_ADCTEST,
                AW8697_BIT_ADCTEST_VBAT_MODE_MASK, AW8697_BIT_ADCTEST_VBAT_SW_COMP);
    }
    return 0;
}

static int aw8697_haptic_get_vbat(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;
    unsigned int cont = 2000;

    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_EN);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DETCTRL,
            AW8697_BIT_DETCTRL_VBAT_GO_MASK, AW8697_BIT_DETCTRL_VABT_GO_ENABLE);

    while(1){
        aw8697_i2c_read(aw8697, AW8697_REG_DETCTRL, &reg_val);
        if((reg_val & 0x02) == 0 || cont == 0)
             break;
        cont--;
    }

    aw8697_i2c_read(aw8697, AW8697_REG_VBATDET, &reg_val);
    aw8697->vbat = 6100 * reg_val / 256;
    if(aw8697->vbat > AW8697_VBAT_MAX) {
        aw8697->vbat = AW8697_VBAT_MAX;
        pr_debug("%s vbat max limit = %dmV\n", __func__, aw8697->vbat);
    }
    if(aw8697->vbat < AW8697_VBAT_MIN) {
        aw8697->vbat = AW8697_VBAT_MIN;
        pr_debug("%s vbat min limit = %dmV\n", __func__, aw8697->vbat);
    }

    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_OFF);

    return 0;
}

static int aw8697_haptic_ram_vbat_comp(struct aw8697 *aw8697, bool flag)
{
    int temp_gain = 0;

    if(flag) {
        if(aw8697->ram_vbat_comp == AW8697_HAPTIC_RAM_VBAT_COMP_ENABLE) {
            aw8697_haptic_get_vbat(aw8697);
            temp_gain = aw8697->gain * AW8697_VBAT_REFER / aw8697->vbat;
            if(temp_gain > (128*AW8697_VBAT_REFER/AW8697_VBAT_MIN)) {
                temp_gain = 128*AW8697_VBAT_REFER/AW8697_VBAT_MIN;
                pr_debug("%s gain limit=%d\n", __func__, temp_gain);
            }
            aw8697_haptic_set_gain(aw8697, temp_gain);
        } else {
            aw8697_haptic_set_gain(aw8697, aw8697->gain);
        }
    } else {
        aw8697_haptic_set_gain(aw8697, aw8697->gain);
    }
    pr_err("%s: aw8697->gain = 0x%x, temp_gain = 0x%x, aw8697->ram_vbat_comp=%d.\n", 
        __func__, aw8697->gain, temp_gain, aw8697->ram_vbat_comp);

    return 0;
}

/*****************************************************
 *
 * f0
 *
 *****************************************************/
static int aw8697_haptic_set_f0_preset(struct aw8697 *aw8697)
{
    unsigned int f0_reg = 0;

    pr_debug("%s enter\n", __func__);

    f0_reg = 1000000000/(aw8697->f0_pre*AW8697_HAPTIC_F0_COEFF);
    aw8697_i2c_write(aw8697, AW8697_REG_F_PRE_H, (unsigned char)((f0_reg>>8)&0xff));
    aw8697_i2c_write(aw8697, AW8697_REG_F_PRE_L, (unsigned char)((f0_reg>>0)&0xff));

    return 0;
}

static int aw8697_haptic_read_f0(struct aw8697 *aw8697)
{
    int ret = 0;
    unsigned char reg_val = 0;
    unsigned int f0_reg = 0;
    unsigned long f0_tmp = 0;

    pr_debug("%s enter\n", __func__);

    ret = aw8697_i2c_read(aw8697, AW8697_REG_F_LRA_F0_H, &reg_val);
    f0_reg = (reg_val<<8);
    ret = aw8697_i2c_read(aw8697, AW8697_REG_F_LRA_F0_L, &reg_val);
    f0_reg |= (reg_val<<0);
#ifndef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2019/1/18,  Add for coverity */
    f0_tmp = 1000000000/(f0_reg*AW8697_HAPTIC_F0_COEFF);
#else
    if (f0_reg != 0) {
        f0_tmp = 1000000000/(f0_reg*AW8697_HAPTIC_F0_COEFF);
    } else {
        pr_err("%s f0_tmp=%d, error  here \n", __func__, f0_tmp);
        return 0;
    }
#endif
    aw8697->f0 = (unsigned int)f0_tmp;
    pr_err("%s f0=%d\n", __func__, aw8697->f0);

    return 0;
}

static int aw8697_haptic_read_cont_f0(struct aw8697 *aw8697)
{
    int ret = 0;
    unsigned char reg_val = 0;
    unsigned int f0_reg = 0;
    unsigned long f0_tmp = 0;

    pr_debug("%s enter\n", __func__);

    ret = aw8697_i2c_read(aw8697, AW8697_REG_F_LRA_CONT_H, &reg_val);
    f0_reg = (reg_val<<8);
    ret = aw8697_i2c_read(aw8697, AW8697_REG_F_LRA_CONT_L, &reg_val);
    f0_reg |= (reg_val<<0);
    
#ifndef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2019/1/7,  Add for coverity */
    f0_tmp = 1000000000/(f0_reg*AW8697_HAPTIC_F0_COEFF);
#else
    if (f0_reg != 0) {
        f0_tmp = 1000000000/(f0_reg*AW8697_HAPTIC_F0_COEFF);
    } else {
        pr_err("%s f0_tmp=%d, error  here \n", __func__, f0_tmp);
    }
#endif
    aw8697->cont_f0 = (unsigned int)f0_tmp;
    pr_err("%s f0=%d\n", __func__, aw8697->cont_f0);

    return 0;
}

static int aw8697_haptic_read_beme(struct aw8697 *aw8697)
{
    int ret = 0;
    unsigned char reg_val = 0;

    ret = aw8697_i2c_read(aw8697, AW8697_REG_WAIT_VOL_MP, &reg_val);
    aw8697->max_pos_beme = (reg_val<<0);
    ret = aw8697_i2c_read(aw8697, AW8697_REG_WAIT_VOL_MN, &reg_val);
    aw8697->max_neg_beme = (reg_val<<0);

    pr_err("%s max_pos_beme=%d\n", __func__, aw8697->max_pos_beme);
    pr_err("%s max_neg_beme=%d\n", __func__, aw8697->max_neg_beme);

    return 0;
}



/*****************************************************
 *
 * rtp
 *
 *****************************************************/
static void aw8697_haptic_set_rtp_aei(struct aw8697 *aw8697, bool flag)
{
    if(flag) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
                AW8697_BIT_SYSINTM_FF_AE_MASK, AW8697_BIT_SYSINTM_FF_AE_EN);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
                AW8697_BIT_SYSINTM_FF_AE_MASK, AW8697_BIT_SYSINTM_FF_AE_OFF);
    }
}
/*
static void aw8697_haptic_set_rtp_afi(struct aw8697 *aw8697, bool flag)
{
    if(flag) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
                AW8697_BIT_SYSINTM_FF_AF_MASK, AW8697_BIT_SYSINTM_FF_AF_EN);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
                AW8697_BIT_SYSINTM_FF_AF_MASK, AW8697_BIT_SYSINTM_FF_AF_OFF);
    }
}
*/

static unsigned char aw8697_haptic_rtp_get_fifo_aei(struct aw8697 *aw8697)
{
    unsigned char ret;
    unsigned char reg_val;


    if(aw8697->osc_cali_flag==1){
        aw8697_i2c_read(aw8697, AW8697_REG_SYSST, &reg_val);
        reg_val &= AW8697_BIT_SYSST_FF_AES;
        ret = reg_val>>4;
    }else{
      aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
      reg_val &= AW8697_BIT_SYSINT_FF_AEI;
      ret = reg_val>>4;
    }

    return ret;
}

static unsigned char aw8697_haptic_rtp_get_fifo_afi(struct aw8697 *aw8697)
{
    unsigned char ret = 0;
    unsigned char reg_val = 0;


    if(aw8697->osc_cali_flag==1){
        aw8697_i2c_read(aw8697, AW8697_REG_SYSST, &reg_val);
        reg_val &= AW8697_BIT_SYSST_FF_AFS;
        ret = reg_val>>3;
    }else{
      aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
      reg_val &= AW8697_BIT_SYSINT_FF_AFI;
      ret = reg_val>>3;
    }

    return ret;
}

/*****************************************************
 *
 * rtp
 *
 *****************************************************/
static void aw8697_dump_rtp_regs(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;

    aw8697_i2c_read(aw8697, 0x02, &reg_val);
    pr_err("%s reg_0x02 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x03, &reg_val);
    pr_err("%s reg_0x03 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x04, &reg_val);
    pr_err("%s reg_0x04 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x05, &reg_val);
    pr_err("%s reg_0x05 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x46, &reg_val);
    pr_err("%s reg_0x46 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x30, &reg_val);
    pr_err("%s reg_0x30 = 0x%02x\n", __func__, reg_val);
}

static int aw8697_haptic_rtp_init(struct aw8697 *aw8697)
{
    unsigned int buf_len = 0;
    bool rtp_start = true;
	unsigned char glb_state_val = 0;

    pr_err("%s enter\n", __func__);
    pm_qos_add_request(&pm_qos_req_vb, PM_QOS_CPU_DMA_LATENCY, PM_QOS_VALUE_VB);

    aw8697->rtp_cnt = 0;
    mutex_lock(&aw8697->rtp_lock);
    aw8697_dump_rtp_regs(aw8697);
    aw8697_haptic_play_go(aw8697, true);
    while((!aw8697_haptic_rtp_get_fifo_afi(aw8697)) &&
            (aw8697->play_mode == AW8697_HAPTIC_RTP_MODE)) {
        pr_debug("%s rtp cnt = %d\n", __func__, aw8697->rtp_cnt);
        if (rtp_start) {
            if((aw8697_rtp->len-aw8697->rtp_cnt) < (aw8697->ram.base_addr)) {
                buf_len = aw8697_rtp->len-aw8697->rtp_cnt;
            } else {
                buf_len = (aw8697->ram.base_addr);
            }
            aw8697_i2c_writes(aw8697, AW8697_REG_RTP_DATA,
                    &aw8697_rtp->data[aw8697->rtp_cnt], buf_len);
            pr_debug("%s 111 rtp cnt = %d\n", __func__, aw8697->rtp_cnt);
            rtp_start = false;
        } else {
            if((aw8697_rtp->len-aw8697->rtp_cnt) < (aw8697->ram.base_addr>>2)) {
                buf_len = aw8697_rtp->len-aw8697->rtp_cnt;
            } else {
                buf_len = (aw8697->ram.base_addr>>2);
            }
            aw8697_i2c_writes(aw8697, AW8697_REG_RTP_DATA,
                    &aw8697_rtp->data[aw8697->rtp_cnt], buf_len);
            pr_debug("%s 222 rtp cnt = %d\n", __func__, aw8697->rtp_cnt);
        }
        aw8697->rtp_cnt += buf_len;
		aw8697_i2c_read(aw8697, AW8697_REG_GLB_STATE, &glb_state_val);
		if ((aw8697->rtp_cnt == aw8697_rtp->len) || ((glb_state_val & 0x0f) == 0x00)) {
            pr_err("%s: rtp update complete\n", __func__);
            aw8697->rtp_cnt = 0;
            aw8697_dump_rtp_regs(aw8697);
            mutex_unlock(&aw8697->rtp_lock);
            pm_qos_remove_request(&pm_qos_req_vb);
            return 0;
        }
    }
    mutex_unlock(&aw8697->rtp_lock);

    pm_qos_remove_request(&pm_qos_req_vb);
    if(aw8697->play_mode == AW8697_HAPTIC_RTP_MODE) {
        aw8697_haptic_set_rtp_aei(aw8697, true);
    }

    pr_debug("%s exit\n", __func__);

    return 0;
}


static int aw8697_clock_OSC_trim_calibration(unsigned long int theory_time, unsigned long int real_time)
{
    unsigned int real_code = 0;
    unsigned int LRA_TRIM_CODE = 0;
    unsigned int DFT_LRA_TRIM_CODE = 0;
    unsigned int Not_need_cali_threshold = 10;

    ///unsigned char real_code;

    if(theory_time == real_time )
    {
        printk("aw_osctheory_time == real_time:%ld  theory_time = %ld not need to cali\n",real_time,theory_time);
        return 0;
    }
    if(theory_time < real_time ){
        if((real_time - theory_time) > (theory_time/50 ))
        {
            printk("aw_osc(real_time - theory_time) > (theory_time/50 ) not to cali\n");
            return DFT_LRA_TRIM_CODE;
        }

        if ((real_time - theory_time) < (Not_need_cali_threshold*theory_time/10000))
        {
            printk("aw_oscmicrosecond:%ld  theory_time = %ld not need to cali\n",real_time,theory_time);
            return DFT_LRA_TRIM_CODE;
        }

        real_code = 32 + ((real_time - theory_time)* 400 )/ theory_time ;
        real_code = ((real_time - theory_time)* 4000) / theory_time;
        real_code = ((real_code%10 < 5)? 0 : 1) + real_code/10;
        real_code = 32 + real_code;
    }
    if(theory_time > real_time){
        if(( theory_time - real_time) > (theory_time/50 ))
        {
            printk("aw_osc(( theory_time - real_time) > (theory_time/50 ))  not to cali \n");
            return DFT_LRA_TRIM_CODE;
        }
        if ((theory_time - real_time) < (Not_need_cali_threshold*theory_time/10000))
        {
            printk("aw_oscmicrosecond:%ld  theory_time = %ld not need to cali\n",real_time,theory_time);
            return DFT_LRA_TRIM_CODE;
        }
        real_code = 32 - ((theory_time - real_time )* 400 )/ theory_time ;
        real_code = ((theory_time - real_time)* 4000) / theory_time ;
        real_code = ((real_code%10 < 5)? 0 : 1) + real_code/10;
        real_code = 32 - real_code;

    }

    if (real_code>31)
    {
        LRA_TRIM_CODE = real_code -32;
    }
    else
    {
        LRA_TRIM_CODE = real_code +32;
    }
    printk("aw_oscmicrosecond:%ld  theory_time = %ld real_code =0X%02X LRA_TRIM_CODE 0X%02X\n",real_time,theory_time,real_code,LRA_TRIM_CODE);

    return LRA_TRIM_CODE;
}

static int aw8697_rtp_trim_lra_calibration(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;
    unsigned int fre_val = 0;
    unsigned int theory_time = 0;
    ///unsigned int real_code = 0;
    unsigned int lra_rtim_code = 0;

    aw8697_i2c_read(aw8697, AW8697_REG_PWMDBG, &reg_val);
    fre_val = (reg_val & 0x006f )>> 5;

    if(fre_val == 3)
      theory_time = (aw8697->rtp_len / 12000) * 1000000; /*12K */
    if(fre_val == 2)
      theory_time = (aw8697->rtp_len / 24000) * 1000000; /*24K */
    if(fre_val == 1 || fre_val == 0)
      theory_time = (aw8697->rtp_len / 48000) * 1000000; /*48K */

    printk("microsecond:%ld  theory_time = %d\n",aw8697->microsecond,theory_time);
    
    lra_rtim_code = aw8697_clock_OSC_trim_calibration(theory_time,aw8697->microsecond);

    if(lra_rtim_code > 0 )
        aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, (char)lra_rtim_code);
    aw8697->clock_standard_OSC_lra_rtim_code = lra_rtim_code;
    return 0;
}

static unsigned char aw8697_haptic_osc_read_INT(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;
    aw8697_i2c_read(aw8697, AW8697_REG_DBGSTAT, &reg_val);
    return reg_val;
}

static int aw8697_rtp_osc_calibration(struct aw8697 *aw8697)
{
    const struct firmware *rtp_file;
    int ret = -1;
    unsigned int buf_len = 0;
    ///unsigned char reg_val = 0;
    unsigned char osc_int_state = 0;
    ///unsigned int  pass_cont=1;
    ///unsigned int  cyc_cont=150;
    aw8697->rtp_cnt = 0;
    aw8697->timeval_flags = 1;
    aw8697->osc_cali_flag =1;

    pr_err("%s enter\n", __func__);

    /* fw loaded */

    //vincent add for stop

    aw8697_haptic_stop(aw8697);
    aw8697_set_clock(aw8697, AW8697_HAPTIC_CLOCK_CALI_OSC_STANDARD);

    aw8697->f0_cali_flag = AW8697_HAPTIC_CALI_F0;

    
    if(aw8697->f0_cali_flag == AW8697_HAPTIC_CALI_F0) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_ANACTRL,
                AW8697_BIT_ANACTRL_LRA_SRC_MASK, AW8697_BIT_ANACTRL_LRA_SRC_REG);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_ANACTRL,
                AW8697_BIT_ANACTRL_LRA_SRC_MASK, AW8697_BIT_ANACTRL_LRA_SRC_EFUSE);
    }
    ret = request_firmware(&rtp_file,
            aw8697_get_rtp_name(aw8697, /*aw8697->rtp_file_num*/0),
            aw8697->dev);
    if(ret < 0)
    {
        pr_err("%s: failed to read %s\n", __func__,
                aw8697_get_rtp_name(aw8697, /*aw8697->rtp_file_num*/0));
        return ret;
    }
    aw8697->rtp_init = 0;
    mutex_lock(&aw8697->rtp_lock);//vincent
    #ifndef VENDOR_EDIT
    /* tongfeng.Huang@BSP.CHG.Basic, 2019/03/11,  Add for alloc memory */
    kfree(aw8697_rtp);
    aw8697_rtp = kzalloc(rtp_file->size+sizeof(int), GFP_KERNEL);
    if (!aw8697_rtp) {
        release_firmware(rtp_file);
        mutex_unlock(&aw8697->rtp_lock);//vincent
        pr_err("%s: error allocating memory\n", __func__);
        return -1;
    }
    #else
    ret = aw8697_container_init(rtp_file->size+sizeof(int));
    if (ret < 0) {
        release_firmware(rtp_file);
        mutex_unlock(&aw8697->rtp_lock);//vincent
        pr_err("%s: error allocating memory\n", __func__);
        return -1;
    }
    #endif
    aw8697_rtp->len = rtp_file->size;
    aw8697->rtp_len = rtp_file->size;
    mutex_unlock(&aw8697->rtp_lock);//vincent
    
    pr_err("%s: rtp file [%s] size = %d\n", __func__,
            aw8697_get_rtp_name(aw8697, /*aw8697->rtp_file_num*/0), aw8697_rtp->len);
    memcpy(aw8697_rtp->data, rtp_file->data, rtp_file->size);
    release_firmware(rtp_file);

    //aw8697->rtp_init = 1; //Don't enter aw8697_irq,because osc calibration use while(1) function

    /* gain */
    aw8697_haptic_ram_vbat_comp(aw8697, false);

    /* rtp mode config */
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_RTP_MODE);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_DBGCTRL,
            AW8697_BIT_DBGCTRL_INT_MODE_MASK, AW8697_BIT_DBGCTRL_INT_MODE_EDGE);
    /* haptic start */
    aw8697_haptic_start(aw8697);

    pm_qos_add_request(&pm_qos_req_vb, PM_QOS_CPU_DMA_LATENCY, PM_QOS_VALUE_VB);

    //while(aw8697->rtp_file_num > 0 && (aw8697->play_mode == AW8697_HAPTIC_RTP_MODE)){
    while(1)
    {
        if (!aw8697_haptic_rtp_get_fifo_afi(aw8697)) //not almost full
        {   
            pr_err("%s !aw8697_haptic_rtp_get_fifo_afi done aw8697->rtp_cnt= %d \n", __func__,aw8697->rtp_cnt);
            mutex_lock(&aw8697->rtp_lock);
            if((aw8697_rtp->len-aw8697->rtp_cnt) < (aw8697->ram.base_addr>>2)) 
            {
                buf_len = aw8697_rtp->len-aw8697->rtp_cnt;
            }
            else {
                buf_len = (aw8697->ram.base_addr>>2);
            }

            if (aw8697->rtp_cnt != aw8697_rtp->len)
            {
                if(aw8697->timeval_flags ==1)
                {
                    do_gettimeofday(&aw8697->start);
                    aw8697->timeval_flags = 0;
                }

                aw8697_i2c_writes(aw8697, AW8697_REG_RTP_DATA,&aw8697_rtp->data[aw8697->rtp_cnt], buf_len);
                aw8697->rtp_cnt += buf_len;     
            }
             mutex_unlock(&aw8697->rtp_lock);
                
        }

        
        osc_int_state = aw8697_haptic_osc_read_INT(aw8697);
        if(osc_int_state&AW8697_BIT_SYSINT_DONEI)
        {           
            do_gettimeofday(&aw8697->end);
            pr_err("%s vincent playback done aw8697->rtp_cnt= %d \n", __func__,aw8697->rtp_cnt);
            break;
        }

        do_gettimeofday(&aw8697->end);
        aw8697->microsecond = (aw8697->end.tv_sec - aw8697->start.tv_sec)*1000000 +
            (aw8697->end.tv_usec - aw8697->start.tv_usec);
        if (aw8697->microsecond > 6000000)
        {
            pr_err("%s vincent time out aw8697->rtp_cnt %d osc_int_state %02x\n", __func__,aw8697->rtp_cnt, osc_int_state);
            break;
        }

    }
    //ENABLE IRQ
    pm_qos_remove_request(&pm_qos_req_vb);
    aw8697->osc_cali_flag =0;
    aw8697->microsecond = (aw8697->end.tv_sec - aw8697->start.tv_sec)*1000000 +
        (aw8697->end.tv_usec - aw8697->start.tv_usec);
    /*calibration osc*/
    printk("%s 2018_microsecond:%ld \n",__func__,aw8697->microsecond);

    pr_err("%s exit\n", __func__);
    return 0;
}

static void aw8697_op_clean_status(struct aw8697 *aw8697)
{
    aw8697->audio_ready = false;
    aw8697->haptic_ready = false;
    aw8697->pre_haptic_number = 0;
    aw8697->rtp_routine_on = 0;
    pr_err("%s enter\n", __FUNCTION__);
}


const struct firmware *aw8697_old_work_file_load_accord_f0(struct aw8697 *aw8697)
{
    const struct firmware *rtp_file;
    unsigned int f0_file_num = 1024;
    int ret = -1;

    if (aw8697->rtp_file_num == AW8697_OPPO_WAVEFORM_INDEX_OLD_STEADY
    	 || aw8697->rtp_file_num == AW8697_OPPO_WAVEFORM_INDEX_HIGH_TEMP)
    {
		if(aw8697->device_id == 815){
			if(aw8697->f0 <= 1610)
				f0_file_num = 0;
			else if(aw8697->f0 <= 1630)
				f0_file_num = 1;
			else if(aw8697->f0 <= 1650)
				f0_file_num = 2;
			else if(aw8697->f0 <= 1670)
				f0_file_num = 3;
			else if(aw8697->f0 <= 1690)
				f0_file_num = 4;
			else if(aw8697->f0 <= 1710)
				f0_file_num = 5;
			else if(aw8697->f0 <= 1730)
				f0_file_num = 6;
			else if(aw8697->f0 <= 1750)
				f0_file_num = 7;
			else if(aw8697->f0 <= 1770)
				f0_file_num = 8;
			else if(aw8697->f0 <= 1790)
				f0_file_num = 9;
			else
				f0_file_num = 10;
		} else if(aw8697->device_id == 832){
			if(aw8697->f0 <= 2255)
				f0_file_num = 0;
			else if(aw8697->f0 <= 2265)
				f0_file_num = 1;
			else if(aw8697->f0 <= 2275)
				f0_file_num = 2;
			else if(aw8697->f0 <= 2285)
				f0_file_num = 3;
			else if(aw8697->f0 <= 2295)
				f0_file_num = 4;
			else if(aw8697->f0 <= 2305)
				f0_file_num = 5;
			else if(aw8697->f0 <= 2315)
				f0_file_num = 6;
			else if(aw8697->f0 <= 2325)
				f0_file_num = 7;
			else if(aw8697->f0 <= 2335)
				f0_file_num = 8;
			else if(aw8697->f0 <= 2345)
				f0_file_num = 9;
			else
				f0_file_num = 10;
		}
		if (aw8697->rtp_file_num == AW8697_OPPO_WAVEFORM_INDEX_OLD_STEADY)
		{
			if(aw8697->device_id == 815){
				ret = request_firmware(&rtp_file,
	                    aw8697_old_steady_test_rtp_name_0815[f0_file_num],
	                    aw8697->dev);
			}else if(aw8697->device_id == 832){
				ret = request_firmware(&rtp_file,
	                    aw8697_old_steady_test_rtp_name_0832[f0_file_num],
	                    aw8697->dev);
			}
		} else {
			if(aw8697->device_id == 815){
				ret = request_firmware(&rtp_file,
	                    aw8697_high_temp_high_humidity_0815[f0_file_num],
	                    aw8697->dev);
			}else if(aw8697->device_id == 832){
				ret = request_firmware(&rtp_file,
	                    aw8697_high_temp_high_humidity_0832[f0_file_num],
	                    aw8697->dev);
			}
		}
		if(ret < 0)
        {
            pr_err("%s: failed to read id[%d],index[%d]\n", __func__, aw8697->device_id, f0_file_num);
            aw8697->rtp_routine_on = 0;
            return NULL;
        }
    	return rtp_file;
    }
    return NULL;
}

static const struct firmware *aw8697_rtp_load_file_accord_f0(struct aw8697 *aw8697)
{
    const struct firmware *rtp_file;
    unsigned int f0_file_num = 1024;
    int ret = -1;

    if (aw8697->rtp_file_num == AW8697_OPPO_WAVEFORM_INDEX_OLD_STEADY
    	 || aw8697->rtp_file_num == AW8697_OPPO_WAVEFORM_INDEX_HIGH_TEMP)
    {
        return aw8697_old_work_file_load_accord_f0(aw8697);
    }

    return NULL;  // mark by huangtongfeng

    if ((aw8697->rtp_file_num >=  RINGTONES_START_INDEX && aw8697->rtp_file_num <= RINGTONES_END_INDEX)
        || (aw8697->rtp_file_num >=  NEW_RING_START && aw8697->rtp_file_num <= NEW_RING_END)
        || aw8697->rtp_file_num == RINGTONES_SIMPLE_INDEX
        || aw8697->rtp_file_num == RINGTONES_PURE_INDEX)
    {
        if (aw8697->f0 <= 1670)
        {
            f0_file_num = aw8697->rtp_file_num;
            pr_err("%s  ringtone f0_file_num[%d]\n", __func__, f0_file_num);
            ret = request_firmware(&rtp_file,
                    aw8697_ringtone_rtp_f0_170_name[f0_file_num],
                    aw8697->dev);
            if(ret < 0)
            {
                pr_err("%s: failed to read %s\n", __func__,
                        aw8697_ringtone_rtp_f0_170_name[f0_file_num]);
                aw8697->rtp_routine_on = 0;
                return NULL;
            }
            return rtp_file;
        }
        return NULL;
    }
    else if (aw8697->rtp_file_num == AW8697_OPPO_RTP_LONG_SOUND_INDEX
        || aw8697->rtp_file_num == AW8697_OPPO_WAVEFORM_INDEX_OLD_STEADY)
    {
        if (aw8697->f0 <= 1650)
            f0_file_num = 0;
        else if (aw8697->f0 <= 1670)
            f0_file_num = 1;
        else if (aw8697->f0 <= 1700)
            f0_file_num = 2;
        else
            f0_file_num = 3;
        pr_err("%s long sound or old steady test f0_file_num[%d], aw8697->rtp_file_num[%d]\n", __func__, f0_file_num, aw8697->rtp_file_num);

        if (aw8697->rtp_file_num == AW8697_OPPO_RTP_LONG_SOUND_INDEX) {
            ret = request_firmware(&rtp_file,
                    aw8697_long_sound_rtp_name[f0_file_num],
                    aw8697->dev);
        }
        else if (aw8697->rtp_file_num == AW8697_OPPO_WAVEFORM_INDEX_OLD_STEADY){
            ret = request_firmware(&rtp_file,
                    aw8697_old_steady_test_rtp_name_0815[f0_file_num],
                    aw8697->dev);
        }
        if(ret < 0)
        {
            pr_err("%s: failed to read %s\n", __func__,
                    aw8697_long_sound_rtp_name[f0_file_num]);
            aw8697->rtp_routine_on = 0;
            return NULL;
        }
        return rtp_file;
    }
    return NULL;
}

static void aw8697_rtp_work_routine(struct work_struct *work)
{
    const struct firmware *rtp_file = NULL;
    int ret = -1;
    struct aw8697 *aw8697 = container_of(work, struct aw8697, rtp_work);

    pr_err("%s enter\n", __func__);
    aw8697->rtp_routine_on = 1;
    /* fw loaded */

    rtp_file = aw8697_rtp_load_file_accord_f0(aw8697);
    if (!rtp_file)
    {
        pr_err("%s  aw8697->rtp_file_num[%d]\n", __func__, aw8697->rtp_file_num);
        aw8697->rtp_routine_on = 1;
        ret = request_firmware(&rtp_file,
            aw8697_get_rtp_name(aw8697, aw8697->rtp_file_num),
            aw8697->dev);
        if(ret < 0)
        {
            pr_err("%s: failed to read %s\n", __func__,
                    aw8697_get_rtp_name(aw8697, aw8697->rtp_file_num));
            aw8697->rtp_routine_on = 0;
            return ;
        }
    }
    aw8697->rtp_init = 0;

    mutex_lock(&aw8697->rtp_lock);//vincent
    #ifndef VENDOR_EDIT
    /* tongfeng.Huang@BSP.CHG.Basic, 2019/03/11,  Add for alloc memory */
    kfree(aw8697_rtp);
    aw8697_rtp = kzalloc(rtp_file->size+sizeof(int), GFP_KERNEL);
    if (!aw8697_rtp) {
        release_firmware(rtp_file);
        mutex_unlock(&aw8697->rtp_lock);//vincent
        pr_err("%s: error allocating memory\n", __func__);
        return;
    }
    #else
    ret = aw8697_container_init(rtp_file->size+sizeof(int));
    if (ret < 0) {
        release_firmware(rtp_file);
        mutex_unlock(&aw8697->rtp_lock);//vincent
        pr_err("%s: error allocating memory\n", __func__);
        
        aw8697_op_clean_status(aw8697);
        aw8697->rtp_routine_on = 0;
        return;
    }
    #endif
    aw8697_rtp->len = rtp_file->size;
    

    memcpy(aw8697_rtp->data, rtp_file->data, rtp_file->size);
    mutex_unlock(&aw8697->rtp_lock);//vincent
    release_firmware(rtp_file);
    pr_err("%s: rtp file [%s] size = %d\n", __func__,
    	aw8697_get_rtp_name(aw8697, aw8697->rtp_file_num), aw8697_rtp->len);

    aw8697->rtp_init = 1;
    mutex_lock(&aw8697->lock);

    /* set clock to stand */
    aw8697_set_clock(aw8697, AW8697_HAPTIC_CLOCK_CALI_OSC_STANDARD);

    /* gain */
    aw8697_haptic_ram_vbat_comp(aw8697, false);

    /* rtp mode config */
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_RTP_MODE);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_DBGCTRL,
            AW8697_BIT_DBGCTRL_INT_MODE_MASK, AW8697_BIT_DBGCTRL_INT_MODE_EDGE);
    /* haptic start */
    //aw8697_haptic_start(aw8697);

    aw8697_haptic_rtp_init(aw8697);
    mutex_unlock(&aw8697->lock);

    aw8697_op_clean_status(aw8697);
    aw8697->rtp_routine_on = 0;
}

static void aw8697_rtp_single_cycle_routine(struct work_struct *work)
{
    struct aw8697 *aw8697 = container_of(work, struct aw8697, rtp_single_cycle_work);
    const struct firmware *rtp_file;
    int ret = -1;
    unsigned int buf_len = 0;
    unsigned char reg_val = 0;
  //  unsigned int  pass_cont=1;
    unsigned int  cyc_cont=150;
    aw8697->rtp_cnt = 0;
    aw8697->osc_cali_flag =1;

    pr_err("%s enter\n", __func__);
    printk("%s---%d\n",__func__,__LINE__);
    /* fw loaded */
    if(aw8697->rtp_loop == 0xFF){
        ret = request_firmware(&rtp_file,aw8697_get_rtp_name(aw8697, aw8697->rtp_serial[1]),aw8697->dev);
    } else{
        printk("%s A single cycle : err value\n",__func__);
    }
    if(ret < 0)
    {
        pr_err("%s: failed to read %s\n", __func__,
                aw8697_get_rtp_name(aw8697, aw8697->rtp_serial[1]));
        return;
    }
    aw8697->rtp_init = 0;
    #ifndef VENDOR_EDIT
    /* tongfeng.Huang@BSP.CHG.Basic, 2019/03/11,  Add for alloc memory */
    kfree(aw8697_rtp);
    printk("%s---%d\n",__func__,__LINE__);
    aw8697_rtp = kzalloc(rtp_file->size+sizeof(int), GFP_KERNEL);
    if (!aw8697_rtp) {
        release_firmware(rtp_file);
        pr_err("%s: error allocating memory\n", __func__);
        return;
    }
    #else
    ret = aw8697_container_init(rtp_file->size+sizeof(int));
    if (ret < 0) {
        release_firmware(rtp_file);
        pr_err("%s: error allocating memory\n", __func__);
        return;
    }
    #endif
    aw8697_rtp->len = rtp_file->size;
    pr_err("%s: rtp file [%s] size = %d\n", __func__,
            aw8697_get_rtp_name(aw8697, aw8697->rtp_serial[1]), aw8697_rtp->len);
    memcpy(aw8697_rtp->data, rtp_file->data, rtp_file->size);
    printk("%s---%d\n",__func__,__LINE__);
    release_firmware(rtp_file);

    //aw8697->rtp_init = 1; //Don't enter aw8697_irq,because osc calibration use while(1) function
    /* gain */
    aw8697_haptic_ram_vbat_comp(aw8697, false);
    printk("%s---%d\n",__func__,__LINE__);
    /* rtp mode config */
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_RTP_MODE);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_DBGCTRL,
            AW8697_BIT_DBGCTRL_INT_MODE_MASK, AW8697_BIT_DBGCTRL_INT_MODE_EDGE);
    /* haptic start */
    aw8697_haptic_start(aw8697);

    while(aw8697->rtp_cycle_flag == 1 ){
        if(!aw8697_haptic_rtp_get_fifo_afi(aw8697)){
            if((aw8697_rtp->len-aw8697->rtp_cnt) < (aw8697->ram.base_addr>>2)) {
                buf_len = aw8697_rtp->len-aw8697->rtp_cnt;
            } else {
                buf_len = (aw8697->ram.base_addr>>2);
            }

            aw8697_i2c_writes(aw8697, AW8697_REG_RTP_DATA,
                &aw8697_rtp->data[aw8697->rtp_cnt], buf_len);
            aw8697->rtp_cnt += buf_len;

            if(aw8697->rtp_cnt == aw8697_rtp->len) {
            //  pr_err("%s: rtp update complete,enter again\n", __func__);
              aw8697->rtp_cnt = 0;
            }
        }else{
                aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
                if(reg_val & AW8697_BIT_SYSST_DONES) {
                      pr_err("%s chip playback done\n", __func__);
                     break;
                }
                while(1){
                    if(aw8697_haptic_rtp_get_fifo_aei(aw8697)){
                        printk("-----%s---%d----while(1)--\n",__func__,__LINE__);
                    break;
                    }
                    cyc_cont--;
                    if(cyc_cont == 0){
                        cyc_cont = 150;
                    break;
                }
              }
            }/*else*/
        }/*while*/
    pr_err("%s exit\n", __func__);
}

static void aw8697_rtp_regroup_routine(struct work_struct *work)
{
    struct aw8697 *aw8697 = container_of(work, struct aw8697, rtp_regroup_work);
    const struct firmware *rtp_file;
    unsigned int buf_len = 0;
    unsigned char reg_val = 0;
    unsigned int  cyc_cont=150;
    int rtp_len_tmp =0;
    int aw8697_rtp_len = 0;
    int i, ret = 0;
    unsigned char *p = NULL;
    aw8697->rtp_cnt = 0;
    aw8697->osc_cali_flag =1;
    pr_err("%s enter\n", __func__);

    for(i=1;i<=aw8697->rtp_serial[0];i++){
        if((request_firmware(&rtp_file,aw8697_get_rtp_name(aw8697, aw8697->rtp_serial[i]),aw8697->dev)) < 0){
             pr_err("%s: failed to read %s\n", __func__,aw8697_get_rtp_name(aw8697, aw8697->rtp_serial[i]));
        }
        aw8697_rtp_len =rtp_len_tmp + rtp_file->size;
        rtp_len_tmp = aw8697_rtp_len;
        pr_err("%s:111 rtp file [%s] size = %d\n", __func__,
            aw8697_get_rtp_name(aw8697, aw8697->rtp_serial[i]), aw8697_rtp_len);
        release_firmware(rtp_file);
    }

    rtp_len_tmp = 0;
    aw8697->rtp_init = 0;
    #ifndef VENDOR_EDIT
    /* tongfeng.Huang@BSP.CHG.Basic, 2019/03/11,  Add for alloc memory */
    kfree(aw8697_rtp);
    aw8697_rtp = kzalloc(aw8697_rtp_len+sizeof(int), GFP_KERNEL);
    if (!aw8697_rtp) {
        pr_err("%s: error allocating memory\n", __func__);
        return;
    }
    #else
    ret = aw8697_container_init(aw8697_rtp_len+sizeof(int));
    if (ret < 0) {
        pr_err("%s: error allocating memory\n", __func__);
        return;
    }
    #endif
    aw8697_rtp->len = aw8697_rtp_len;
    for(i=1;i<=aw8697->rtp_serial[0];i++){
        if((request_firmware(&rtp_file,aw8697_get_rtp_name(aw8697, aw8697->rtp_serial[i]),aw8697->dev)) < 0){
             pr_err("%s: failed to read %s\n", __func__,aw8697_get_rtp_name(aw8697, aw8697->rtp_serial[i]));
        }
    p = &(aw8697_rtp->data[0]) + rtp_len_tmp;
    memcpy( p , rtp_file->data, rtp_file->size);
    rtp_len_tmp += rtp_file->size;
    release_firmware(rtp_file);
    pr_err("%s: rtp file [%s]\n", __func__,
        aw8697_get_rtp_name(aw8697, aw8697->rtp_serial[i]));
    }

    //for(j=0; j<aw8697_rtp_len; j++) {
    //    printk("%s: addr:%d, data:%d\n", __func__, j, aw8697_rtp->data[j]);
    //   }
    //aw8697->rtp_init = 1; //Don't enter aw8697_irq,because osc calibration use while(1) function
    /* gain */
    aw8697_haptic_ram_vbat_comp(aw8697, false);
    /* rtp mode config */
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_RTP_MODE);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DBGCTRL,
            AW8697_BIT_DBGCTRL_INT_MODE_MASK, AW8697_BIT_DBGCTRL_INT_MODE_EDGE);
    /* haptic start */
    aw8697_haptic_start(aw8697);

    while(aw8697->rtp_cycle_flag == 1 ){
        if(!aw8697_haptic_rtp_get_fifo_afi(aw8697)){
            if((aw8697_rtp->len-aw8697->rtp_cnt) < (aw8697->ram.base_addr>>2)) {
                buf_len = aw8697_rtp->len-aw8697->rtp_cnt;
            } else {
                buf_len = (aw8697->ram.base_addr>>2);
            }
            aw8697_i2c_writes(aw8697, AW8697_REG_RTP_DATA,
                &aw8697_rtp->data[aw8697->rtp_cnt], buf_len);
            aw8697->rtp_cnt += buf_len;
            if(aw8697->rtp_cnt == aw8697_rtp->len) {
                pr_err("%s: rtp update complete\n", __func__);
                aw8697->rtp_cnt = 0;
                aw8697->rtp_loop--;
                if(aw8697->rtp_loop == 0)
                    return;
            }
        }else{
                aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
                if(reg_val & AW8697_BIT_SYSST_DONES) {
                      pr_err("%s chip playback done\n", __func__);
                     break;
                }
                while(1){
                    if(aw8697_haptic_rtp_get_fifo_aei(aw8697)){
                        printk("-----%s---%d----while(1)--\n",__func__,__LINE__);
                        break;
                    }
                    cyc_cont--;
                    if(cyc_cont == 0){
                        cyc_cont = 150;
                    break;
                }
              }
            }/*else*/
        }/*while*/
    pr_err("%s exit\n", __func__);
}

static int aw8697_rtp_regroup_work(struct aw8697 *aw8697)
{
    aw8697_haptic_stop(aw8697);
    if(aw8697->rtp_serial[0] > 0){
      printk("%s---%d\n",__func__,__LINE__);
        aw8697_haptic_set_rtp_aei(aw8697, false);
        aw8697_interrupt_clear(aw8697);
            if(aw8697_is_rtp_index_valid(aw8697, aw8697->rtp_serial[0])) {
                if(aw8697->rtp_loop == 0xFF){   //if aw8697->rtp_loop = 0xff then  single cycle ;
                    aw8697->rtp_cycle_flag = 1;
                    schedule_work(&aw8697->rtp_single_cycle_work);
              } else if( (aw8697->rtp_loop > 0 ) && (aw8697->rtp_loop < 0xff) ) {
                    aw8697->rtp_cycle_flag = 1;
                    schedule_work(&aw8697->rtp_regroup_work);
              } else {
                    printk("%s---%d\n",__func__,__LINE__);
              }
            }
    } else {
      aw8697->rtp_cycle_flag  = 0;
    }
    return 0;
}

/*****************************************************
 *
 * haptic - audio
 *
 *****************************************************/
static enum hrtimer_restart aw8697_haptic_audio_timer_func(struct hrtimer *timer)
{
    struct aw8697 *aw8697 = container_of(timer, struct aw8697, haptic_audio.timer);

    pr_debug("%s enter\n", __func__);
    schedule_work(&aw8697->haptic_audio.work);

    hrtimer_start(&aw8697->haptic_audio.timer,
            ktime_set(aw8697->haptic_audio.timer_val/1000000,
                    (aw8697->haptic_audio.timer_val%1000000)*1000),
            HRTIMER_MODE_REL);
    return HRTIMER_NORESTART;
}

static void aw8697_haptic_audio_work_routine(struct work_struct *work)
{
    struct aw8697 *aw8697 = container_of(work, struct aw8697, haptic_audio.work);
    int rtp_is_going_on = 0;

    pr_debug("%s enter\n", __func__);
    //vincent rtp priority game
    

    // activate vincent 
    rtp_is_going_on = aw8697_haptic_juge_RTP_is_going_on(aw8697);
    if (rtp_is_going_on)
    {
        return ;
    }


    //vincent rtp priority game


    mutex_lock(&aw8697->haptic_audio.lock);
    memcpy(&aw8697->haptic_audio.ctr,
            &aw8697->haptic_audio.data[aw8697->haptic_audio.cnt],
            sizeof(struct haptic_ctr));
    pr_debug("%s: cnt=%d, cmd=%d, play=%d, wavseq=%d, loop=%d, gain=%d\n",
            __func__,
            aw8697->haptic_audio.cnt,
            aw8697->haptic_audio.ctr.cmd,
            aw8697->haptic_audio.ctr.play,
            aw8697->haptic_audio.ctr.wavseq,
            aw8697->haptic_audio.ctr.loop,
            aw8697->haptic_audio.ctr.gain);
    mutex_unlock(&aw8697->haptic_audio.lock);
    if(aw8697->haptic_audio.ctr.cmd == 0x01) {
        mutex_lock(&aw8697->lock);
        aw8697_haptic_stop(aw8697);
        mutex_unlock(&aw8697->lock);
        if(aw8697->haptic_audio.ctr.play == 0x01) {
            pr_err("%s: haptic_audio_play_start\n", __func__);
            mutex_lock(&aw8697->lock);
            aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_RAM_MODE);

            aw8697_haptic_set_wav_seq(aw8697, 0x00,
                    aw8697->haptic_audio.ctr.wavseq);
            aw8697_haptic_set_wav_seq(aw8697, 0x01, 0x00);

            aw8697_haptic_set_wav_loop(aw8697, 0x00,
                    aw8697->haptic_audio.ctr.loop);

            aw8697_haptic_set_gain(aw8697,
                    aw8697->haptic_audio.ctr.gain);

            aw8697_haptic_start(aw8697);
            mutex_unlock(&aw8697->lock);
        }
        mutex_lock(&aw8697->haptic_audio.lock);
        memset(&aw8697->haptic_audio.data[aw8697->haptic_audio.cnt],
                0, sizeof(struct haptic_ctr));
        mutex_unlock(&aw8697->haptic_audio.lock);
    }

    mutex_lock(&aw8697->haptic_audio.lock);
    aw8697->haptic_audio.cnt ++;
    if(aw8697->haptic_audio.data[aw8697->haptic_audio.cnt].cmd == 0) {
        aw8697->haptic_audio.cnt = 0;
        pr_debug("%s: haptic play buffer restart\n", __func__);
    }
    mutex_unlock(&aw8697->haptic_audio.lock);

}


/*****************************************************
 *
 * haptic cont
 *
 *****************************************************/
static int aw8697_haptic_cont(struct aw8697 *aw8697)
{
    pr_err("%s enter\n", __func__);

    /* work mode */
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_CONT_MODE);

    /* preset f0 */
    aw8697->f0_pre = aw8697->f0;
    aw8697_haptic_set_f0_preset(aw8697);

    /* lpf */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DATCTRL,
            AW8697_BIT_DATCTRL_FC_MASK, AW8697_BIT_DATCTRL_FC_1000HZ);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DATCTRL,
            AW8697_BIT_DATCTRL_LPF_ENABLE_MASK, AW8697_BIT_DATCTRL_LPF_ENABLE);

    /* cont config */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_ZC_DETEC_MASK, AW8697_BIT_CONT_CTRL_ZC_DETEC_ENABLE);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_WAIT_PERIOD_MASK, AW8697_BIT_CONT_CTRL_WAIT_1PERIOD);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_MODE_MASK, AW8697_BIT_CONT_CTRL_BY_GO_SIGNAL);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_EN_CLOSE_MASK, AW8697_CONT_PLAYBACK_MODE);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_F0_DETECT_MASK, AW8697_BIT_CONT_CTRL_F0_DETECT_DISABLE);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_O2C_MASK, AW8697_BIT_CONT_CTRL_O2C_DISABLE);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_AUTO_BRK_MASK, AW8697_BIT_CONT_CTRL_AUTO_BRK_ENABLE);

    /* TD time */
    aw8697_i2c_write(aw8697, AW8697_REG_TD_H, (unsigned char)(aw8697->cont_td>>8));
    aw8697_i2c_write(aw8697, AW8697_REG_TD_L, (unsigned char)(aw8697->cont_td>>0));
    aw8697_i2c_write(aw8697, AW8697_REG_TSET, 0x12);

    /* zero cross */
    aw8697_i2c_write(aw8697, AW8697_REG_ZC_THRSH_H, (unsigned char)(aw8697->cont_zc_thr>>8));
    aw8697_i2c_write(aw8697, AW8697_REG_ZC_THRSH_L, (unsigned char)(aw8697->cont_zc_thr>>0));

    /* bemf */
    aw8697_i2c_write(aw8697, AW8697_REG_BEMF_VTHH_H, 0x10);
    aw8697_i2c_write(aw8697, AW8697_REG_BEMF_VTHH_L, 0x08);
    aw8697_i2c_write(aw8697, AW8697_REG_BEMF_VTHL_H, 0x03);
    aw8697_i2c_write(aw8697, AW8697_REG_BEMF_VTHL_L, 0xf8);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_BEMF_NUM,
            AW8697_BIT_BEMF_NUM_BRK_MASK, aw8697->cont_num_brk);
    aw8697_i2c_write(aw8697, AW8697_REG_TIME_NZC, 0x23);    // 35*171us=5.985ms

    /* f0 driver level */
    aw8697_i2c_write(aw8697, AW8697_REG_DRV_LVL, aw8697->cont_drv_lvl);
    aw8697_i2c_write(aw8697, AW8697_REG_DRV_LVL_OV, aw8697->cont_drv_lvl_ov);

    /* cont play go */
    aw8697_haptic_play_go(aw8697, true);

    return 0;
}

static int aw8697_dump_f0_registers(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;

    aw8697_i2c_read(aw8697, 0x04, &reg_val);
    pr_err("%s reg_0x04 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x46, &reg_val);
    pr_err("%s reg_0x46 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x48, &reg_val);
    pr_err("%s reg_0x48 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x49, &reg_val);
    pr_err("%s reg_0x49 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x4A, &reg_val);
    pr_err("%s reg_0x4A = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x68, &reg_val);
    pr_err("%s reg_0x68 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x69, &reg_val);
    pr_err("%s reg_0x69 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x6a, &reg_val);
    pr_err("%s reg_0x6a = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x6b, &reg_val);
    pr_err("%s reg_0x6b = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x6D, &reg_val);
    pr_err("%s reg_0x6D = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x6f, &reg_val);
    pr_err("%s reg_0x6f = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x70, &reg_val);
    pr_err("%s reg_0x70 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x71, &reg_val);
    pr_err("%s reg_0x71 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x7D, &reg_val);
    pr_err("%s reg_0x7D = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x7E, &reg_val);
    pr_err("%s reg_0x7E = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x7F, &reg_val);
    pr_err("%s reg_0x7F = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x36, &reg_val);
    pr_err("%s reg_0x36 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x5b, &reg_val);
    pr_err("%s reg_0x5b = 0x%02x\n", __func__, reg_val);

    return 0;
}

/*****************************************************
 *
 * haptic f0 cali
 *
 *****************************************************/
static int aw8697_haptic_get_f0(struct aw8697 *aw8697)
{
    int ret = 0;
    unsigned char i = 0;
    unsigned char reg_val = 0;
    unsigned char f0_pre_num = 0;
    unsigned char f0_wait_num = 0;
    unsigned char f0_repeat_num = 0;
    unsigned char f0_trace_num = 0;
    unsigned int t_f0_ms = 0;
    unsigned int t_f0_trace_ms = 0;
    unsigned int f0_cali_cnt = 50;

    pr_err("%s enter\n", __func__);

    aw8697->f0 = aw8697->f0_pre;

    /* f0 calibrate work mode */
    aw8697_haptic_stop(aw8697);
    aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, 0);
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_CONT_MODE);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_EN_CLOSE_MASK, AW8697_BIT_CONT_CTRL_OPEN_PLAYBACK);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_F0_DETECT_MASK, AW8697_BIT_CONT_CTRL_F0_DETECT_ENABLE);

    /* LPF */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DATCTRL,
            AW8697_BIT_DATCTRL_FC_MASK, AW8697_BIT_DATCTRL_FC_1000HZ);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DATCTRL,
            AW8697_BIT_DATCTRL_LPF_ENABLE_MASK, AW8697_BIT_DATCTRL_LPF_ENABLE);

    /* LRA OSC Source */
    if(aw8697->f0_cali_flag == AW8697_HAPTIC_CALI_F0) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_ANACTRL,
                AW8697_BIT_ANACTRL_LRA_SRC_MASK, AW8697_BIT_ANACTRL_LRA_SRC_REG);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_ANACTRL,
                AW8697_BIT_ANACTRL_LRA_SRC_MASK, AW8697_BIT_ANACTRL_LRA_SRC_EFUSE);
    }

    /* preset f0 */
    aw8697_haptic_set_f0_preset(aw8697);

    /* beme config */
    aw8697_i2c_write(aw8697, AW8697_REG_BEMF_VTHH_H, 0x10);
    aw8697_i2c_write(aw8697, AW8697_REG_BEMF_VTHH_L, 0x08);
    aw8697_i2c_write(aw8697, AW8697_REG_BEMF_VTHL_H, 0x03);
    aw8697_i2c_write(aw8697, AW8697_REG_BEMF_VTHL_L, 0xf8);

    /* f0 driver level */
    aw8697_i2c_write(aw8697, AW8697_REG_DRV_LVL, aw8697->cont_drv_lvl);

    /* f0 trace parameter */
    f0_pre_num = 0x05;
    f0_wait_num = 0x03;
    f0_repeat_num = 0x02;
    f0_trace_num = 0x0f;
    aw8697_i2c_write(aw8697, AW8697_REG_NUM_F0_1, (f0_pre_num<<4)|(f0_wait_num<<0));
    aw8697_i2c_write(aw8697, AW8697_REG_NUM_F0_2, (f0_repeat_num<<0));
    aw8697_i2c_write(aw8697, AW8697_REG_NUM_F0_3, (f0_trace_num<<0));

    /* clear aw8697 interrupt */
    ret = aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
    aw8697_dump_f0_registers(aw8697);   // add by huangtongfeng

    /* play go and start f0 calibration */
    aw8697_haptic_play_go(aw8697, true);

    /* f0 trace time */
    t_f0_ms = 1000*10/aw8697->f0_pre;
    t_f0_trace_ms = t_f0_ms * (f0_pre_num + f0_wait_num + (f0_trace_num+f0_wait_num)*(f0_repeat_num-1));
    msleep(t_f0_trace_ms);

    for(i=0; i<f0_cali_cnt; i++) {
        ret = aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
        /* f0 calibrate done */
        if(reg_val & 0x01) {
            aw8697_haptic_read_f0(aw8697);
            aw8697_haptic_read_beme(aw8697);
            break;
        }
        msleep(10);
        pr_err("%s f0 cali sleep 10ms\n", __func__);
    }

    if(i == f0_cali_cnt) {
        ret = -1;
    } else {
        ret = 0;
    }
    aw8697_dump_f0_registers(aw8697);   // add by huangtongfeng

    /* restore default config */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_EN_CLOSE_MASK, AW8697_CONT_PLAYBACK_MODE);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_F0_DETECT_MASK, AW8697_BIT_CONT_CTRL_F0_DETECT_DISABLE);

    return ret;
}

static int aw8697_haptic_f0_calibration(struct aw8697 *aw8697)
{
    int ret = 0;
    unsigned char reg_val = 0;
    unsigned int f0_limit = 0;
    char f0_cali_lra = 0;
    int f0_cali_step = 0;
    int f0_dft_step = 0;

    pr_err("%s enter\n", __func__);

    aw8697->f0_cali_flag = AW8697_HAPTIC_CALI_F0;
    aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, 0);
    if(aw8697_haptic_get_f0(aw8697)) {
        pr_err("%s get f0 error, user defafult f0\n", __func__);
    } else {
        /* max and min limit */
        f0_limit = aw8697->f0;
        pr_err("%s get f0=%d\n", __func__, f0_limit);

#ifdef VENDOR_EDIT
/* Hang.Zhao@PSW.BSP.CHG.Basic,2019/10/25, Modify for different haptics */
        if (aw8697->device_id == 832) {
            if(aw8697->f0*100 < AW8697_0832_HAPTIC_F0_PRE*(100-AW8697_0832_HAPTIC_F0_CALI_PERCEN)) {
                f0_limit = AW8697_0832_HAPTIC_F0_PRE*(100-AW8697_0832_HAPTIC_F0_CALI_PERCEN)/100;
            }
            if(aw8697->f0*100 > AW8697_0832_HAPTIC_F0_PRE*(100+AW8697_0832_HAPTIC_F0_CALI_PERCEN)) {
                f0_limit = AW8697_0832_HAPTIC_F0_PRE*(100+AW8697_0832_HAPTIC_F0_CALI_PERCEN)/100;
            }
        } else {
            if(aw8697->f0*100 < AW8697_0815_HAPTIC_F0_PRE*(100-AW8697_0815_HAPTIC_F0_CALI_PERCEN)) {
                f0_limit = AW8697_0815_HAPTIC_F0_PRE*(100-AW8697_0815_HAPTIC_F0_CALI_PERCEN)/100;
            }
            if(aw8697->f0*100 > AW8697_0815_HAPTIC_F0_PRE*(100+AW8697_0815_HAPTIC_F0_CALI_PERCEN)) {
                f0_limit = AW8697_0815_HAPTIC_F0_PRE*(100+AW8697_0815_HAPTIC_F0_CALI_PERCEN)/100;
            }
        }
#else
        if(aw8697->f0*100 < AW8697_HAPTIC_F0_PRE*(100-AW8697_HAPTIC_F0_CALI_PERCEN)) {
            f0_limit = AW8697_HAPTIC_F0_PRE*(100-AW8697_HAPTIC_F0_CALI_PERCEN)/100;
        }
        if(aw8697->f0*100 > AW8697_HAPTIC_F0_PRE*(100+AW8697_HAPTIC_F0_CALI_PERCEN)) {
            f0_limit = AW8697_HAPTIC_F0_PRE*(100+AW8697_HAPTIC_F0_CALI_PERCEN)/100;
        }
#endif

        /* calculate cali step */
        f0_cali_step = 10000*((int)f0_limit-(int)aw8697->f0_pre)/((int)f0_limit*25);
        pr_debug("%s f0_cali_step=%d\n", __func__, f0_cali_step);

        /* get default cali step */
        aw8697_i2c_read(aw8697, AW8697_REG_TRIM_LRA, &reg_val);
        if(reg_val & 0x20) {
            f0_dft_step = reg_val - 0x40;
        } else {
            f0_dft_step = reg_val;
        }
        pr_debug("%s f0_dft_step=%d\n", __func__, f0_dft_step);

        /* get new cali step */
        f0_cali_step += f0_dft_step;
        pr_debug("%s f0_cali_step=%d\n", __func__, f0_cali_step);

        if(f0_cali_step > 31) {
            f0_cali_step = 31;
        } else if(f0_cali_step < -32) {
            f0_cali_step = -32;
        }
        f0_cali_lra = (char)f0_cali_step;
        pr_debug("%s f0_cali_lra=%d\n", __func__, f0_cali_lra);

        /* get cali step complement code*/
        if(f0_cali_lra < 0) {
            f0_cali_lra += 0x40;
        }
        pr_debug("%s reg f0_cali_lra=%d\n", __func__, f0_cali_lra);

        /* update cali step */
        aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, (char)f0_cali_lra);
        aw8697_i2c_read(aw8697, AW8697_REG_TRIM_LRA, &reg_val);
        aw8697->clock_system_f0_cali_lra = f0_cali_lra;
        pr_err("%s final trim_lra=0x%02x\n", __func__, reg_val);
    }

    if(aw8697_haptic_get_f0(aw8697)) {
        pr_err("%s get f0 error, user defafult f0\n", __func__);
    }

    /* restore default work mode */
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_STANDBY_MODE);
    aw8697->play_mode = AW8697_HAPTIC_RAM_MODE;
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_PLAY_MODE_MASK, AW8697_BIT_SYSCTRL_PLAY_MODE_RAM);
    aw8697_haptic_stop(aw8697);

    return ret;
}

/*****************************************************
 *
 * haptic fops
 *
 *****************************************************/
static int aw8697_file_open(struct inode *inode, struct file *file)
{
    if (!try_module_get(THIS_MODULE))
        return -ENODEV;

    file->private_data = (void*)g_aw8697;

    return 0;
}

static int aw8697_file_release(struct inode *inode, struct file *file)
{
    file->private_data = (void*)NULL;

    module_put(THIS_MODULE);

    return 0;
}

static long aw8697_file_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct aw8697 *aw8697 = (struct aw8697 *)file->private_data;

    int ret = 0;

    dev_info(aw8697->dev, "%s: cmd=0x%x, arg=0x%lx\n",
              __func__, cmd, arg);

    mutex_lock(&aw8697->lock);

    if(_IOC_TYPE(cmd) != AW8697_HAPTIC_IOCTL_MAGIC) {
        dev_err(aw8697->dev, "%s: cmd magic err\n",
                __func__);
        mutex_unlock(&aw8697->lock);
        return -EINVAL;
    }

    switch (cmd) {
        default:
            dev_err(aw8697->dev, "%s, unknown cmd\n", __func__);
            break;
    }

    mutex_unlock(&aw8697->lock);

    return ret;
}

static ssize_t aw8697_file_read(struct file* filp, char* buff, size_t len, loff_t* offset)
{
    struct aw8697 *aw8697 = (struct aw8697 *)filp->private_data;
    int ret = 0;
    int i = 0;
    unsigned char reg_val = 0;
    unsigned char *pbuff = NULL;

    mutex_lock(&aw8697->lock);

    dev_info(aw8697->dev, "%s: len=%zu\n", __func__, len);

    switch(aw8697->fileops.cmd)
    {
        case AW8697_HAPTIC_CMD_READ_REG:
            pbuff = (unsigned char *)kzalloc(len, GFP_KERNEL);
            if(pbuff != NULL) {
                for(i=0; i<len; i++) {
                    aw8697_i2c_read(aw8697, aw8697->fileops.reg+i, &reg_val);
                    pbuff[i] = reg_val;
                }
                for(i=0; i<len; i++) {
                    dev_info(aw8697->dev, "%s: pbuff[%d]=0x%02x\n",
                            __func__, i, pbuff[i]);
                }
                ret = copy_to_user(buff, pbuff, len);
                if(ret) {
                    dev_err(aw8697->dev, "%s: copy to user fail\n", __func__);
                }
                kfree(pbuff);
            } else {
                dev_err(aw8697->dev, "%s: alloc memory fail\n", __func__);
            }
            break;
        default:
            dev_err(aw8697->dev, "%s, unknown cmd %d \n", __func__, aw8697->fileops.cmd);
            break;
    }

    mutex_unlock(&aw8697->lock);


    return len;
}

static ssize_t aw8697_file_write(struct file* filp, const char* buff, size_t len, loff_t* off)
{
    struct aw8697 *aw8697 = (struct aw8697 *)filp->private_data;
    int i = 0;
    int ret = 0;
    unsigned char *pbuff = NULL;

    pbuff = (unsigned char *)kzalloc(len, GFP_KERNEL);
    if(pbuff == NULL) {
        dev_err(aw8697->dev, "%s: alloc memory fail\n", __func__);
        return len;
    }
    ret = copy_from_user(pbuff, buff, len);
    if(ret) {
#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2019/1/7,  Add for coverity*/
        if(pbuff != NULL) {
            kfree(pbuff);
        }
#endif
        dev_err(aw8697->dev, "%s: copy from user fail\n", __func__);
        return len;
    }

    for(i=0; i<len; i++) {
        dev_info(aw8697->dev, "%s: pbuff[%d]=0x%02x\n",
                __func__, i, pbuff[i]);
    }

    mutex_lock(&aw8697->lock);

    aw8697->fileops.cmd = pbuff[0];

    switch(aw8697->fileops.cmd)
    {
        case AW8697_HAPTIC_CMD_READ_REG:
            if(len == 2) {
                aw8697->fileops.reg = pbuff[1];
            } else {
                dev_err(aw8697->dev, "%s: read cmd len %zu err\n", __func__, len);
            }
            break;
        case AW8697_HAPTIC_CMD_WRITE_REG:
            if(len > 2) {
                for(i=0; i<len-2; i++) {
                    dev_info(aw8697->dev, "%s: write reg0x%02x=0x%02x\n",
                            __func__, pbuff[1]+i, pbuff[i+2]);
                    aw8697_i2c_write(aw8697, pbuff[1]+i, pbuff[2+i]);
                }
            } else {
                dev_err(aw8697->dev, "%s: write cmd len %zu err\n", __func__, len);
            }
            break;
        default:
            dev_err(aw8697->dev, "%s, unknown cmd %d \n", __func__, aw8697->fileops.cmd);
            break;
    }

    mutex_unlock(&aw8697->lock);

    if(pbuff != NULL) {
        kfree(pbuff);
    }
    return len;
}

static struct file_operations fops =
{
    .owner = THIS_MODULE,
    .read = aw8697_file_read,
    .write = aw8697_file_write,
    .unlocked_ioctl = aw8697_file_unlocked_ioctl,
    .open = aw8697_file_open,
    .release = aw8697_file_release,
};

static struct miscdevice aw8697_haptic_misc =
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = AW8697_HAPTIC_NAME,
    .fops = &fops,
};

static int aw8697_haptic_init(struct aw8697 *aw8697)
{
    int ret = 0;
    unsigned char i = 0;
    unsigned char reg_val = 0;

    pr_err("%s enter\n", __func__);

    ret = misc_register(&aw8697_haptic_misc);
    if(ret) {
        dev_err(aw8697->dev,  "%s: misc fail: %d\n", __func__, ret);
        return ret;
    }

    /* haptic audio */
    aw8697->haptic_audio.delay_val = 20833;
    aw8697->haptic_audio.timer_val = 20833;

    hrtimer_init(&aw8697->haptic_audio.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    aw8697->haptic_audio.timer.function = aw8697_haptic_audio_timer_func;
    INIT_WORK(&aw8697->haptic_audio.work, aw8697_haptic_audio_work_routine);

    mutex_init(&aw8697->haptic_audio.lock);

    aw8697_op_clean_status(aw8697);
     mutex_init(&aw8697->rtp_lock);
     //vincent
    /* haptic init */
    mutex_lock(&aw8697->lock);

    aw8697->activate_mode = AW8697_HAPTIC_ACTIVATE_CONT_MODE;

    ret = aw8697_i2c_read(aw8697, AW8697_REG_WAVSEQ1, &reg_val);
    aw8697->index = reg_val & 0x7F;
    ret = aw8697_i2c_read(aw8697, AW8697_REG_DATDBG, &reg_val);
    aw8697->gain = reg_val & 0xFF;
    ret = aw8697_i2c_read(aw8697, AW8697_REG_BSTDBG4, &reg_val);
#ifdef VENDOR_EDIT
/* Hang.Zhao@PSW.BSP.CHG.Basic,2019/11/6, Modify for haptic vmax */
    aw8697->vmax = AW8697_HAPTIC_HIGH_LEVEL_REG_VAL;
#else
    aw8697->vmax = (reg_val>>1)&0x1F;
#endif
    for(i=0; i<AW8697_SEQUENCER_SIZE; i++) {
        ret = aw8697_i2c_read(aw8697, AW8697_REG_WAVSEQ1+i, &reg_val);
        aw8697->seq[i] = reg_val;
    }

    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_STANDBY_MODE);

    aw8697_haptic_set_pwm(aw8697, AW8697_PWM_24K);

    aw8697_i2c_write(aw8697, AW8697_REG_BSTDBG1, 0x30);
    aw8697_i2c_write(aw8697, AW8697_REG_BSTDBG2, 0xeb);
    aw8697_i2c_write(aw8697, AW8697_REG_BSTDBG3, 0xd4);
    aw8697_i2c_write(aw8697, AW8697_REG_TSET, 0x12);
    aw8697_i2c_write(aw8697, AW8697_REG_R_SPARE, 0x68);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_ANADBG,
            AW8697_BIT_ANADBG_IOC_MASK, AW8697_BIT_ANADBG_IOC_4P65A);

    aw8697_haptic_set_bst_peak_cur(aw8697, AW8697_DEFAULT_PEAKCUR);

    aw8697_haptic_swicth_motorprotect_config(aw8697, 0x00, 0x00);

    aw8697_haptic_auto_boost_config(aw8697, false);

    aw8697_haptic_trig_param_init(aw8697);
    aw8697_haptic_trig_param_config(aw8697);

   aw8697_haptic_os_calibration(aw8697);

    aw8697_haptic_cont_vbat_mode(aw8697,
            AW8697_HAPTIC_CONT_VBAT_HW_COMP_MODE);
    aw8697->ram_vbat_comp = AW8697_HAPTIC_RAM_VBAT_COMP_ENABLE;

    mutex_unlock(&aw8697->lock);


    /* f0 calibration */
    mutex_lock(&aw8697->lock);
#ifdef VENDOR_EDIT
/* Hang.Zhao@PSW.BSP.CHG.Basic,2019/10/25, Modify for different haptics */
    if (aw8697->device_id == 832) {//19065
        aw8697->f0_pre = AW8697_0832_HAPTIC_F0_PRE;
        aw8697->cont_drv_lvl = AW8697_0832_HAPTIC_CONT_DRV_LVL;
        aw8697->cont_drv_lvl_ov = AW8697_0832_HAPTIC_CONT_DRV_LVL_OV;
        aw8697->cont_td = AW8697_0832_HAPTIC_CONT_TD;
        aw8697->cont_zc_thr = AW8697_0832_HAPTIC_CONT_ZC_THR;
        aw8697->cont_num_brk = AW8697_0832_HAPTIC_CONT_NUM_BRK;
    } else {
        aw8697->f0_pre = AW8697_0815_HAPTIC_F0_PRE;
        aw8697->cont_drv_lvl = AW8697_0815_HAPTIC_CONT_DRV_LVL;
        aw8697->cont_drv_lvl_ov = AW8697_0815_HAPTIC_CONT_DRV_LVL_OV;
        aw8697->cont_td = AW8697_0815_HAPTIC_CONT_TD;
        aw8697->cont_zc_thr = AW8697_0815_HAPTIC_CONT_ZC_THR;
        aw8697->cont_num_brk = AW8697_0815_HAPTIC_CONT_NUM_BRK;
    }
    pr_err("%s get f0_pre=%d\n", __func__, aw8697->f0_pre);
#endif
#ifndef VENDOR_EDIT
    /* tongfeng.Huang@BSP.CHG.Basic, 2019/1/7,  Add for disable F0  in kernel*/
    aw8697_haptic_f0_calibration(aw8697);
#endif
    mutex_unlock(&aw8697->lock);

    return ret;
}



/*****************************************************
 *
 * vibrator
 *
 *****************************************************/
#ifdef TIMED_OUTPUT
static int aw8697_vibrator_get_time(struct timed_output_dev *dev)
{
    struct aw8697 *aw8697 = container_of(dev, struct aw8697, to_dev);

    if (hrtimer_active(&aw8697->timer)) {
        ktime_t r = hrtimer_get_remaining(&aw8697->timer);
        return ktime_to_ms(r);
    }

    return 0;
}

static void aw8697_vibrator_enable( struct timed_output_dev *dev, int value)
{
    struct aw8697 *aw8697 = container_of(dev, struct aw8697, to_dev);
    unsigned char reg_val = 0;

    mutex_lock(&aw8697->lock);

    pr_debug("%s enter\n", __func__);
    /*RTP mode do not allow other vibrate begign*/
    if(aw8697->play_mode == AW8697_HAPTIC_RTP_MODE)
    {
         aw8697_i2c_read(aw8697, AW8697_REG_GLB_STATE, &reg_val);
         if((reg_val&0x0f) != 0x00) {
             pr_err("%s RTP on && not stop,return\n", __func__);
             mutex_unlock(&aw8697->lock);
             return;
         }
    }
    /*RTP mode do not allow other vibrate end*/

    aw8697_haptic_stop(aw8697);

    if (value > 0) {
        aw8697_haptic_ram_vbat_comp(aw8697, false);
        aw8697_haptic_play_wav_seq(aw8697, value);
    }

    mutex_unlock(&aw8697->lock);

    pr_debug("%s exit\n", __func__);
}

#else
static enum led_brightness aw8697_haptic_brightness_get(struct led_classdev *cdev)
{
    struct aw8697 *aw8697 =
        container_of(cdev, struct aw8697, cdev);

    return aw8697->amplitude;
}

static void aw8697_haptic_brightness_set(struct led_classdev *cdev,
                enum led_brightness level)
{
    int rtp_is_going_on = 0;
    struct aw8697 *aw8697 =  container_of(cdev, struct aw8697, cdev);

    // activate vincent 
    rtp_is_going_on = aw8697_haptic_juge_RTP_is_going_on(aw8697);
    if (rtp_is_going_on)
    {
        return ;
    }

    aw8697->amplitude = level;

    mutex_lock(&aw8697->lock);

    aw8697_haptic_stop(aw8697);
    if (aw8697->amplitude > 0) {
        aw8697_haptic_ram_vbat_comp(aw8697, false);
        aw8697_haptic_play_wav_seq(aw8697, aw8697->amplitude);
    }

    mutex_unlock(&aw8697->lock);

}
#endif

static ssize_t aw8697_state_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif

    return snprintf(buf, PAGE_SIZE, "%d\n", aw8697->state);
}

static ssize_t aw8697_state_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    return count;
}

static ssize_t aw8697_duration_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ktime_t time_rem;
    s64 time_ms = 0;

    if (hrtimer_active(&aw8697->timer)) {
        time_rem = hrtimer_get_remaining(&aw8697->timer);
        time_ms = ktime_to_ms(time_rem);
    }

    return snprintf(buf, PAGE_SIZE, "%lld\n", time_ms);
}

static ssize_t aw8697_duration_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

#ifdef VENDOR_EDIT
    /* tongfeng.Huang@BSP.CHG.Basic, 2018/11/17,  Add for mv voltage */
    pr_err("%s: value=%d\n", __FUNCTION__, val);
#endif
    /* setting 0 on duration is NOP for now */
    if (val <= 0)
        return count;

    aw8697->duration = val;

    return count;
}

static ssize_t aw8697_activate_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif

    /* For now nothing to show */
    return snprintf(buf, PAGE_SIZE, "%d\n", aw8697->state);
}
//  vincent 

static int aw8697_haptic_juge_RTP_is_going_on(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;
    unsigned char rtp_state = 0;
    

    aw8697_i2c_read(aw8697, AW8697_REG_SYSCTRL, &reg_val);
    if((reg_val&AW8697_BIT_SYSCTRL_PLAY_MODE_RTP)&&(!(reg_val&AW8697_BIT_SYSCTRL_STANDBY)))
    {
            rtp_state = 1;// is going on
    }
    if (aw8697->rtp_routine_on) {
        pr_err("%s:rtp_routine_on\n", __func__);
        rtp_state = 1;/*is going on*/
    }
    pr_err("%s AW8697_REG_SYSCTRL 0x04==%02x rtp_state=%d\n", __func__,reg_val,rtp_state);

    return rtp_state;
}

static ssize_t aw8697_activate_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;
    int rtp_is_going_on = 0;

    // activate vincent 
    rtp_is_going_on = aw8697_haptic_juge_RTP_is_going_on(aw8697);
    if (rtp_is_going_on)
    {
        return count;
    }
    // activate vincent 
    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    if (val != 0 && val != 1)
        return count;

    pr_err("%s: value=%d\n", __FUNCTION__, val);
    #if 0 //add by zhengzhiwei@rm.bsp.vib to solve fingerprint wakeup delay.
    //vincent
    if (0 == val)
    {
        mdelay(10);
    }
    //vincent
    #endif
    hrtimer_cancel(&aw8697->timer);

    aw8697->state = val;
#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/12/27,  Add for vib */
    if (aw8697->state)
    {
        pr_err("%s: aw8697->gain=0x%02x\n", __FUNCTION__, aw8697->gain);
        if (aw8697->gain >= AW8697_HAPTIC_RAM_VBAT_COMP_GAIN) {
            aw8697->gain = AW8697_HAPTIC_RAM_VBAT_COMP_GAIN;
        }
        mutex_lock(&aw8697->lock);

        if (aw8697->device_id == 815)
            aw8697_haptic_set_gain(aw8697, aw8697->gain);
        aw8697_haptic_set_repeat_wav_seq(aw8697, AW8697_OPPO_WAVEFORM_INDEX_SINE_CYCLE);

        mutex_unlock(&aw8697->lock);
        cancel_work_sync(&aw8697->vibrator_work);
        //schedule_work(&aw8697->vibrator_work);
        queue_work(system_highpri_wq, &aw8697->vibrator_work);
    } else {
        mutex_lock(&aw8697->lock);
        aw8697_haptic_stop(aw8697);
        mutex_unlock(&aw8697->lock);
    }
#endif

    return count;
}

static ssize_t aw8697_activate_mode_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif

    return snprintf(buf, PAGE_SIZE, "activate_mode=%d\n", aw8697->activate_mode);
}

static ssize_t aw8697_activate_mode_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    mutex_lock(&aw8697->lock);
    aw8697->activate_mode = val;
    mutex_unlock(&aw8697->lock);
    return count;
}

static ssize_t aw8697_index_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned char reg_val = 0;
    aw8697_i2c_read(aw8697, AW8697_REG_WAVSEQ1, &reg_val);
    aw8697->index = reg_val;

    return snprintf(buf, PAGE_SIZE, "%d\n", aw8697->index);
}

static ssize_t aw8697_index_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    pr_debug("%s: value=%d\n", __FUNCTION__, val);

    mutex_lock(&aw8697->lock);
    aw8697->index = val;
    aw8697_haptic_set_repeat_wav_seq(aw8697, aw8697->index);
    mutex_unlock(&aw8697->lock);
    return count;
}

static ssize_t aw8697_vmax_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif

    return snprintf(buf, PAGE_SIZE, "0x%02x\n", aw8697->vmax);
}

static ssize_t aw8697_vmax_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    pr_err("%s: value=%d\n", __FUNCTION__, val);

    mutex_lock(&aw8697->lock);
#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/11/17,  Add for mv voltage */
    if (val <= 255) {
        aw8697->gain = (val * AW8697_HAPTIC_RAM_VBAT_COMP_GAIN) / 255;
    } else if (val <= AW8697_HAPTIC_LOW_LEVEL_VOL) {
        aw8697->vmax = AW8697_HAPTIC_LOW_LEVEL_REG_VAL;
        aw8697->gain = 0x16;
    } else if (val <= 1200) {
        aw8697->vmax = AW8697_HAPTIC_MEDIUM_LEVEL_REG_VAL;
        aw8697->gain = 0x40;
    } else if (val <= AW8697_HAPTIC_MEDIUM_LEVEL_VOL) {
        aw8697->vmax = 0x1c;
        aw8697->gain = 0x40;
    } else if (val <= 2000) {
        aw8697->vmax = 0x0E;
        aw8697->gain = 0x80;
    } else {
        aw8697->vmax = AW8697_HAPTIC_HIGH_LEVEL_REG_VAL;
        aw8697->gain = 0x80;
    }

    if (val == 2550) {  // for old test only
        aw8697->gain = AW8697_HAPTIC_RAM_VBAT_COMP_GAIN;
    }
    //aw8697->vmax = AW8697_HAPTIC_HIGH_LEVEL_REG_VAL;
    //aw8697->gain = 0x80;
    aw8697_haptic_set_gain(aw8697, aw8697->gain);
    aw8697_haptic_set_bst_vol(aw8697, aw8697->vmax);
#else
    aw8697->vmax = val;
    aw8697_haptic_set_bst_vol(aw8697, aw8697->vmax);
#endif
    mutex_unlock(&aw8697->lock);
    pr_err("%s:aw8697->gain[0x%x], aw8697->vmax[0x%x] end\n", __FUNCTION__, aw8697->gain, aw8697->vmax);
    return count;
}

static ssize_t aw8697_gain_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif

    return snprintf(buf, PAGE_SIZE, "0x%02x\n", aw8697->gain);
}

static ssize_t aw8697_gain_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    pr_err("%s: value=%d\n", __FUNCTION__, val);

    mutex_lock(&aw8697->lock);
    aw8697->gain = val;
    aw8697_haptic_set_gain(aw8697, aw8697->gain);
    mutex_unlock(&aw8697->lock);
    return count;
}

static ssize_t aw8697_seq_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    size_t count = 0;
    unsigned char i = 0;
    unsigned char reg_val = 0;

    for(i=0; i<AW8697_SEQUENCER_SIZE; i++) {
        aw8697_i2c_read(aw8697, AW8697_REG_WAVSEQ1+i, &reg_val);
        count += snprintf(buf+count, PAGE_SIZE-count,
                "seq%d: 0x%02x\n", i+1, reg_val);
        aw8697->seq[i] |= reg_val;
    }
    return count;
}

static ssize_t aw8697_seq_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[2] = {0, 0};

    if(2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
        pr_debug("%s: seq%d=0x%x\n", __FUNCTION__, databuf[0], databuf[1]);
        mutex_lock(&aw8697->lock);
        aw8697->seq[databuf[0]] = (unsigned char)databuf[1];
        aw8697_haptic_set_wav_seq(aw8697, (unsigned char)databuf[0],
                aw8697->seq[databuf[0]]);
        mutex_unlock(&aw8697->lock);
    }
    return count;
}

static ssize_t aw8697_loop_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    size_t count = 0;
    unsigned char i = 0;
    unsigned char reg_val = 0;

    for(i=0; i<AW8697_SEQUENCER_LOOP_SIZE; i++) {
        aw8697_i2c_read(aw8697, AW8697_REG_WAVLOOP1+i, &reg_val);
        aw8697->loop[i*2+0] = (reg_val>>4)&0x0F;
        aw8697->loop[i*2+1] = (reg_val>>0)&0x0F;

        count += snprintf(buf+count, PAGE_SIZE-count,
                "seq%d loop: 0x%02x\n", i*2+1, aw8697->loop[i*2+0]);
        count += snprintf(buf+count, PAGE_SIZE-count,
                "seq%d loop: 0x%02x\n", i*2+2, aw8697->loop[i*2+1]);
    }
      count += snprintf(buf+count, PAGE_SIZE-count,
              "loop: 0x%02x\n", aw8697->rtp_loop);
    return count;
}

static ssize_t aw8697_loop_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[2] = {0, 0};
    unsigned int val = 0;
    int rc = 0;
    aw8697->rtp_loop = 0;
    if(2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
        pr_debug("%s: seq%d loop=0x%x\n", __FUNCTION__, databuf[0], databuf[1]);
        mutex_lock(&aw8697->lock);
        aw8697->loop[databuf[0]] = (unsigned char)databuf[1];
        aw8697_haptic_set_wav_loop(aw8697, (unsigned char)databuf[0],
                aw8697->loop[databuf[0]]);
        mutex_unlock(&aw8697->lock);

    }else{
        rc = kstrtouint(buf, 0, &val);
        if (rc < 0)
            return rc;
        aw8697->rtp_loop = val;
        printk("%s  aw8697->rtp_loop = 0x%x",__func__,aw8697->rtp_loop);
    }

    return count;
}

static ssize_t aw8697_rtp_num_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    unsigned char i = 0;
  //  unsigned char reg_val = 0;
    for(i = 0; i < AW8697_RTP_NUM; i ++) {
        len += snprintf(buf+len, PAGE_SIZE-len, "num: %d, serial:%d \n",i, aw8697->rtp_serial[i]);
    }
    return len;
}

static ssize_t aw8697_rtp_num_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    /*datebuf[0] is connect number */
    /*databuf[1]- databuf[x] is sequence and number*/
    unsigned int databuf[AW8697_RTP_NUM] = {0,0,0,0,0,0}; /*custom modify it,if you want*/
    unsigned int val = 0;
    int rc = 0;

    if( AW8697_RTP_NUM  == sscanf(buf, "%x %x %x %x %x %x", &databuf[0], &databuf[1],
     &databuf[2], &databuf[3], &databuf[4], &databuf[5])) {
       for(val = 0 ;val < AW8697_RTP_NUM ; val ++ ){
            printk("%s: databuf = %d \n", __FUNCTION__, databuf[val]);
            aw8697->rtp_serial[val] = (unsigned char)databuf[val];
      }
    } else {
        rc = kstrtouint(buf, 0, &val);
        if (rc < 0)
            return rc;
        if(val ==0)
            aw8697->rtp_serial[0] = 0;
    }
    aw8697_rtp_regroup_work(aw8697);
    return count;
}

static ssize_t aw8697_reg_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    unsigned char i = 0;
    unsigned char reg_val = 0;
    for(i = 0; i < AW8697_REG_MAX; i ++) {
        if(!(aw8697_reg_access_0832[i]&REG_RD_ACCESS))
           continue;
        aw8697_i2c_read(aw8697, i, &reg_val);
        len += snprintf(buf+len, PAGE_SIZE-len, "reg:0x%02x=0x%02x \n", i, reg_val);
    }
    return len;
}

static ssize_t aw8697_reg_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[2] = {0, 0};

    if(2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
        aw8697_i2c_write(aw8697, (unsigned char)databuf[0], (unsigned char)databuf[1]);
    }

    return count;
}

static ssize_t aw8697_rtp_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "rtp play: %d\n", aw8697->rtp_cnt);

    return len;
}

static ssize_t aw8697_rtp_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;
    int rtp_is_going_on = 0;
    static bool mute = false;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;
    pr_err("%s: val [%d] \n", __func__, val);

    if (val == 1025) {
        mute = true;
        return count;
    } else if(val == 1026) {
        mute = false;
        return count;
    }

    mutex_lock(&aw8697->lock);
    /*OP add for juge rtp on begin*/
    rtp_is_going_on = aw8697_haptic_juge_RTP_is_going_on(aw8697);
    if (rtp_is_going_on && (val == AUDIO_READY_STATUS))
    {
        pr_err("%s: seem audio status rtp[%d]\n", __FUNCTION__,val);
        mutex_unlock(&aw8697->lock);
        return count;
    }
    /*OP add for juge rtp on end*/
    if ((val >=  RINGTONES_START_INDEX && val <= RINGTONES_END_INDEX)
        || (val >=  NEW_RING_START && val <= NEW_RING_END)
        || val == RINGTONES_SIMPLE_INDEX
        || val == RINGTONES_PURE_INDEX
        || val == AUDIO_READY_STATUS
        || (val >=  AW8697_RTP_NAME_INDEX_REALME_START && val <= AW8697_RTP_NAME_INDEX_REALME_END)) {
        if (val == AUDIO_READY_STATUS)
           aw8697->audio_ready = true;
        else
           aw8697->haptic_ready = true;
       pr_err("%s:audio[%d]and haptic[%d] ready\n", __FUNCTION__,
                        aw8697->audio_ready, aw8697->haptic_ready);
       if (aw8697->haptic_ready && !aw8697->audio_ready) {
           aw8697->pre_haptic_number = val;
       }
       if (!aw8697->audio_ready || !aw8697->haptic_ready) {
           mutex_unlock(&aw8697->lock);
           return count;
       }
    }
    if (val == AUDIO_READY_STATUS && aw8697->pre_haptic_number) {
        pr_err("pre_haptic_number:%d\n",aw8697->pre_haptic_number);
       val = aw8697->pre_haptic_number;
    }
    if (!val) {
      aw8697_op_clean_status(aw8697);
    }
    aw8697_haptic_stop(aw8697);

    aw8697_haptic_set_rtp_aei(aw8697, false);
    aw8697_interrupt_clear(aw8697);
    mutex_unlock(&aw8697->lock);
    if(aw8697_is_rtp_index_valid(aw8697, val)) {
        aw8697->rtp_file_num = val;
        if(val) {
            //schedule_work(&aw8697->rtp_work);
            queue_work(system_unbound_wq, &aw8697->rtp_work);
        }
    } else {
        pr_err("%s: rtp_file_num 0x%02x over max value \n", __func__, aw8697->rtp_file_num);
    }

    return count;
}

static ssize_t aw8697_ram_update_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    //struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    //struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    //struct led_classdev *cdev = dev_get_drvdata(dev);
    //struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "sram update mode\n");
    return len;
}

static ssize_t aw8697_ram_update_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    if(val) {
        aw8697_ram_update(aw8697);
    }
    return count;
}

static ssize_t aw8697_f0_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;

    mutex_lock(&aw8697->lock);
    aw8697->f0_cali_flag = AW8697_HAPTIC_CALI_F0;//AW8697_HAPTIC_LRA_F0;
    aw8697_haptic_get_f0(aw8697);
    mutex_unlock(&aw8697->lock);

#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/11/17,  Add for old test */
    len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", aw8697->f0);
#else
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697 lra f0 = %d\n", aw8697->f0);
#endif
    return len;
}

static ssize_t aw8697_f0_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    pr_err("%s:  f0 = %d\n", __FUNCTION__, val);


    if(aw8697->device_id == 815) {
        aw8697->f0 = val;
	    if (aw8697->f0 < OPPO_F0_VAL_MIN_0815 || aw8697->f0 > OPPO_F0_VAL_MAX_0815) {
	        aw8697->f0 = OPPO_F0_VAL_TYPICAL_0815;
	    }
    } else if(aw8697->device_id == 832) {
        aw8697->f0 = val;
	    if (aw8697->f0 < OPPO_F0_VAL_MIN_0832 || aw8697->f0 > OPPO_F0_VAL_MAX_0832) {
	        aw8697->f0 = OPPO_F0_VAL_TYPICAL_0832;
	    }
    }
    aw8697_ram_update(aw8697);

    return count;
}

static ssize_t aw8697_cali_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    mutex_lock(&aw8697->lock);
    aw8697->f0_cali_flag = AW8697_HAPTIC_CALI_F0;
    aw8697_haptic_get_f0(aw8697);
    mutex_unlock(&aw8697->lock);
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697 cali f0 = %d\n", aw8697->f0);
    return len;
}

static ssize_t aw8697_cali_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    if(val) {
        mutex_lock(&aw8697->lock);
        aw8697->clock_system_f0_cali_lra = 0;
        aw8697_haptic_f0_calibration(aw8697);
        mutex_unlock(&aw8697->lock);
    }
    return count;
}

static ssize_t aw8697_cont_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    aw8697_haptic_read_cont_f0(aw8697);
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697 cont f0 = %d\n", aw8697->cont_f0);
    return len;
}

static ssize_t aw8697_cont_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;
    mutex_lock(&aw8697->lock);
    aw8697_haptic_stop(aw8697);
    if(val) {
        aw8697_haptic_cont(aw8697);
    }
    mutex_unlock(&aw8697->lock);
    return count;
}


static ssize_t aw8697_cont_td_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697 cont delay time = 0x%04x\n", aw8697->cont_td);
    return len;
}

static ssize_t aw8697_cont_td_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[1] = {0};
    if(1 == sscanf(buf, "%x", &databuf[0])) {
        aw8697->cont_td = databuf[0];
        aw8697_i2c_write(aw8697, AW8697_REG_TD_H, (unsigned char)(databuf[0]>>8));
        aw8697_i2c_write(aw8697, AW8697_REG_TD_L, (unsigned char)(databuf[0]>>0));
    }
    return count;
}

static ssize_t aw8697_cont_drv_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697 cont drv level = %d\n", aw8697->cont_drv_lvl);
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697 cont drv level overdrive= %d\n", aw8697->cont_drv_lvl_ov);
    return len;
}

static ssize_t aw8697_cont_drv_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[2] = {0, 0};
    if(2 == sscanf(buf, "%d %d", &databuf[0], &databuf[1])) {
        aw8697->cont_drv_lvl = databuf[0];
        aw8697_i2c_write(aw8697, AW8697_REG_DRV_LVL, aw8697->cont_drv_lvl);
        aw8697->cont_drv_lvl_ov = databuf[1];
        aw8697_i2c_write(aw8697, AW8697_REG_DRV_LVL_OV, aw8697->cont_drv_lvl_ov);
    }
    return count;
}

static ssize_t aw8697_cont_num_brk_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697 cont break num = %d\n", aw8697->cont_num_brk);
    return len;
}

static ssize_t aw8697_cont_num_brk_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[1] = {0};
    if(1 == sscanf(buf, "%d", &databuf[0])) {
        aw8697->cont_num_brk = databuf[0];
        if(aw8697->cont_num_brk > 7) {
            aw8697->cont_num_brk = 7;
        }
        aw8697_i2c_write_bits(aw8697, AW8697_REG_BEMF_NUM,
                AW8697_BIT_BEMF_NUM_BRK_MASK, aw8697->cont_num_brk);
    }
    return count;
}

static ssize_t aw8697_cont_zc_thr_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697 cont zero cross thr = 0x%04x\n", aw8697->cont_zc_thr);
    return len;
}

static ssize_t aw8697_cont_zc_thr_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[1] = {0};
    if(1 == sscanf(buf, "%x", &databuf[0])) {
        aw8697->cont_zc_thr = databuf[0];
        aw8697_i2c_write(aw8697, AW8697_REG_ZC_THRSH_H, (unsigned char)(databuf[0]>>8));
        aw8697_i2c_write(aw8697, AW8697_REG_ZC_THRSH_L, (unsigned char)(databuf[0]>>0));
    }
    return count;
}

static ssize_t aw8697_vbat_monitor_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;

    mutex_lock(&aw8697->lock);
    aw8697_haptic_stop(aw8697);
    aw8697_haptic_get_vbat(aw8697);
    len += snprintf(buf+len, PAGE_SIZE-len, "vbat=%dmV\n", aw8697->vbat);
    mutex_unlock(&aw8697->lock);

    return len;
}

static ssize_t aw8697_vbat_monitor_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    return count;
}

static ssize_t aw8697_lra_resistance_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    unsigned char reg_val = 0;

    mutex_lock(&aw8697->lock);
    aw8697_haptic_stop(aw8697);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_EN);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BYPASS);


    aw8697_i2c_write_bits(aw8697, AW8697_REG_ANACTRL,
            AW8697_BIT_ANACTRL_HD_PD_MASK, AW8697_BIT_ANACTRL_HD_HZ_EN);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_D2SCFG,
            AW8697_BIT_D2SCFG_CLK_ADC_MASK, AW8697_BIT_D2SCFG_CLK_ASC_1P5MHZ);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_DETCTRL,
            AW8697_BIT_DETCTRL_RL_OS_MASK, AW8697_BIT_DETCTRL_RL_DETECT);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DETCTRL,
            AW8697_BIT_DETCTRL_DIAG_GO_MASK, AW8697_BIT_DETCTRL_DIAG_GO_ENABLE);
    msleep(3);
    aw8697_i2c_read(aw8697, AW8697_REG_RLDET, &reg_val);
    aw8697->lra = 298 * reg_val;

#ifdef VENDOR_EDIT
    /* tongfeng.Huang@BSP.CHG.Basic, 2018/11/17,  Add for old test */
    len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", aw8697->lra);
#else
    len += snprintf(buf+len, PAGE_SIZE-len, "r_lra=%dmohm\n", aw8697->lra);
#endif

    aw8697_i2c_write_bits(aw8697, AW8697_REG_ANACTRL,
            AW8697_BIT_ANACTRL_HD_PD_MASK, AW8697_BIT_ANACTRL_HD_PD_EN);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_D2SCFG,
            AW8697_BIT_D2SCFG_CLK_ADC_MASK, AW8697_BIT_D2SCFG_CLK_ASC_6MHZ);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_OFF);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BOOST);
    mutex_unlock(&aw8697->lock);

    return len;
}

static ssize_t aw8697_lra_resistance_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    return count;
}

static ssize_t aw8697_auto_boost_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;

    len += snprintf(buf+len, PAGE_SIZE-len, "auto_boost=%d\n", aw8697->auto_boost);

    return len;
}


static ssize_t aw8697_auto_boost_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    mutex_lock(&aw8697->lock);
    aw8697_haptic_stop(aw8697);
    aw8697_haptic_auto_boost_config(aw8697, val);
    mutex_unlock(&aw8697->lock);

    return count;
}

static ssize_t aw8697_prctmode_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    unsigned char reg_val = 0;

    aw8697_i2c_read(aw8697, AW8697_REG_RLDET, &reg_val);

    len += snprintf(buf+len, PAGE_SIZE-len, "prctmode=%d\n", reg_val&0x20);
    return len;
}


static ssize_t aw8697_prctmode_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    #ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
    #else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
    #endif
    unsigned int databuf[2] = {0, 0};
    unsigned int addr=0;
    unsigned int val=0;
    if(2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
        addr = databuf[0];
        val=databuf[1];
        mutex_lock(&aw8697->lock);
        aw8697_haptic_swicth_motorprotect_config(aw8697, addr, val);
        mutex_unlock(&aw8697->lock);
   }
    return count;
}

static ssize_t aw8697_trig_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    unsigned char i = 0;
    for(i=0; i<AW8697_TRIG_NUM; i++) {
        len += snprintf(buf+len, PAGE_SIZE-len,
            "trig%d: enable=%d, default_level=%d, dual_edge=%d, frist_seq=%d, second_seq=%d\n",
            i+1, aw8697->trig[i].enable, aw8697->trig[i].default_level, aw8697->trig[i].dual_edge,
            aw8697->trig[i].frist_seq, aw8697->trig[i].second_seq);
    }

    return len;
}

static ssize_t aw8697_trig_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    #ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
    #else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
    #endif
    unsigned int databuf[6] = {0};
    if(sscanf(buf, "%d %d %d %d %d %d",
            &databuf[0], &databuf[1], &databuf[2], &databuf[3], &databuf[4], &databuf[5])) {
        pr_debug("%s: %d, %d, %d, %d, %d, %d\n", __func__,
            databuf[0], databuf[1], databuf[2], databuf[3], databuf[4], databuf[5]);
        if(databuf[0] > 3) {
            databuf[0] = 3;
        }
        if(databuf[0] > 0) {
            databuf[0] -= 1;
        }
        aw8697->trig[databuf[0]].enable = databuf[1];
        aw8697->trig[databuf[0]].default_level = databuf[2];
        aw8697->trig[databuf[0]].dual_edge = databuf[3];
        aw8697->trig[databuf[0]].frist_seq = databuf[4];
        aw8697->trig[databuf[0]].second_seq = databuf[5];
        mutex_lock(&aw8697->lock);
        aw8697_haptic_trig_param_config(aw8697);
        aw8697_haptic_trig_enable_config(aw8697);
        mutex_unlock(&aw8697->lock);
   }
    return count;
}

static ssize_t aw8697_ram_vbat_comp_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;

    len += snprintf(buf+len, PAGE_SIZE-len, "ram_vbat_comp=%d\n", aw8697->ram_vbat_comp);

    return len;
}


static ssize_t aw8697_ram_vbat_comp_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    mutex_lock(&aw8697->lock);
    if(val) {
        aw8697->ram_vbat_comp = AW8697_HAPTIC_RAM_VBAT_COMP_ENABLE;
    } else {
        aw8697->ram_vbat_comp = AW8697_HAPTIC_RAM_VBAT_COMP_DISABLE;
    }
    mutex_unlock(&aw8697->lock);

    return count;
}

#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2019/02/19,  Add for old test */
static ssize_t aw8697_f0_data_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    
    len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", aw8697->clock_system_f0_cali_lra);
    return len;
}

static ssize_t aw8697_f0_data_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    pr_err("%s:  f0 = %d\n", __FUNCTION__, val);

	aw8697->clock_system_f0_cali_lra = val;

    mutex_lock(&aw8697->lock);
	aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, aw8697->clock_system_f0_cali_lra);
    mutex_unlock(&aw8697->lock);

    return count;
}


static ssize_t aw8697_osc_cali_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    
    printk("aw8697_osc_cali_show: 2018_microsecond:%ld \n",aw8697->microsecond);
    len += snprintf(buf+len, PAGE_SIZE-len, "%ld\n", aw8697->microsecond);

    return len;
}
#endif

static ssize_t aw8697_osc_cali_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;

    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    mutex_lock(&aw8697->lock);
    if(val == 3){
        aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, 0);
        aw8697->clock_standard_OSC_lra_rtim_code = 0;
        aw8697_rtp_osc_calibration(aw8697);
        aw8697_rtp_trim_lra_calibration(aw8697);
    }
   if(val == 1)
      aw8697_rtp_osc_calibration(aw8697);

    mutex_unlock(&aw8697->lock);

    return count;
}

static ssize_t aw8697_haptic_audio_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", aw8697->haptic_audio.cnt);
    return len;
}

static ssize_t aw8697_haptic_audio_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[6] = {0};

    if(6 == sscanf(buf, "%d %d %d %d %d %d", &databuf[0], &databuf[1], &databuf[2],
            &databuf[3], &databuf[4], &databuf[5])) {
        pr_debug("%s: cnt=%d, cmd=%d, play=%d, wavseq=%d, loop=%d, gain=%d\n",
                __func__, databuf[0], databuf[1], databuf[2], databuf[3],
                databuf[4], databuf[5]);
        mutex_lock(&aw8697->haptic_audio.lock);
        aw8697->haptic_audio.data[(unsigned char)databuf[0]].cmd = (unsigned char)databuf[1];
        aw8697->haptic_audio.data[(unsigned char)databuf[0]].play = (unsigned char)databuf[2];
        aw8697->haptic_audio.data[(unsigned char)databuf[0]].wavseq = (unsigned char)databuf[3];
        aw8697->haptic_audio.data[(unsigned char)databuf[0]].loop = (unsigned char)databuf[4];
        aw8697->haptic_audio.data[(unsigned char)databuf[0]].gain = (unsigned char)databuf[5];
        mutex_unlock(&aw8697->haptic_audio.lock);

        if(aw8697->haptic_audio.data[aw8697->haptic_audio.cnt].cmd == 0xff) {
            pr_err("%s: haptic_audio stop\n", __func__);
            if(hrtimer_active(&aw8697->haptic_audio.timer)) {
                pr_err("%s: cancel haptic_audio_timer\n", __func__);
                hrtimer_cancel(&aw8697->haptic_audio.timer);
                aw8697->haptic_audio.cnt = 0;
                aw8697_haptic_set_gain(aw8697, 0x80);
            }
        } else {
            if(hrtimer_active(&aw8697->haptic_audio.timer)) {
            } else {
                pr_err("%s: start haptic_audio_timer\n", __func__);
                hrtimer_start(&aw8697->haptic_audio.timer,
                        ktime_set(aw8697->haptic_audio.delay_val/1000000,
                                (aw8697->haptic_audio.delay_val%1000000)*1000),
                        HRTIMER_MODE_REL);
            }
        }
    }
    return count;
}


static ssize_t aw8697_haptic_audio_time_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "haptic_audio.delay_val=%dus\n", aw8697->haptic_audio.delay_val);
    len += snprintf(buf+len, PAGE_SIZE-len, "haptic_audio.timer_val=%dus\n", aw8697->haptic_audio.timer_val);
    return len;
}

static ssize_t aw8697_haptic_audio_time_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[2] = {0};

    if(2 == sscanf(buf, "%d %d", &databuf[0], &databuf[1])) {
        aw8697->haptic_audio.delay_val = databuf[0];
        aw8697->haptic_audio.timer_val = databuf[1];
    }
    return count;
}

#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/11/17,  Add for old test */
static void oppo_motor_old_test_work(struct work_struct *work)
{
    struct aw8697 *aw8697 = container_of(work, struct aw8697, motor_old_test_work);

    pr_err("%s: motor_old_test_mode = %d. aw8697->gain[0x%02x]\n", __func__, aw8697->motor_old_test_mode, aw8697->gain);

    if (aw8697->motor_old_test_mode == MOTOR_OLD_TEST_TRANSIENT) {
        mutex_lock(&aw8697->lock);

        aw8697_haptic_stop(aw8697);
        aw8697->gain = 0x80;
        aw8697_haptic_set_gain(aw8697, aw8697->gain);
        aw8697_haptic_set_bst_vol(aw8697, AW8697_HAPTIC_HIGH_LEVEL_REG_VAL);
        aw8697_haptic_set_wav_seq(aw8697, 0, AW8697_OPPO_WAVEFORM_INDEX_TRANSIENT);
        aw8697_haptic_set_wav_loop(aw8697, 0, 0);
        aw8697_haptic_play_wav_seq(aw8697, 1);

        mutex_unlock(&aw8697->lock);
    } else if (aw8697->motor_old_test_mode == MOTOR_OLD_TEST_STEADY) {
        mutex_lock(&aw8697->lock);
        aw8697_haptic_stop(aw8697);
        aw8697->gain = 0x80;
        aw8697_haptic_set_gain(aw8697, aw8697->gain);
        aw8697_haptic_set_bst_vol(aw8697, AW8697_HAPTIC_HIGH_LEVEL_REG_VAL);
        aw8697_haptic_set_rtp_aei(aw8697, false);
        aw8697_interrupt_clear(aw8697);
        mutex_unlock(&aw8697->lock);
        if(aw8697_is_rtp_index_valid(aw8697, AW8697_OPPO_WAVEFORM_INDEX_OLD_STEADY)) {
            aw8697->rtp_file_num = AW8697_OPPO_WAVEFORM_INDEX_OLD_STEADY;
            if(AW8697_OPPO_WAVEFORM_INDEX_OLD_STEADY) {
                //schedule_work(&aw8697->rtp_work);
                queue_work(system_unbound_wq, &aw8697->rtp_work);
            }
        } else {
            pr_err("%s: rtp_file_num 0x%02x over max value \n", __func__, aw8697->rtp_file_num);
        }
    } else if (aw8697->motor_old_test_mode == MOTOR_OLD_TEST_HIGH_TEMP_HUMIDITY){
        mutex_lock(&aw8697->lock);
        aw8697_haptic_stop(aw8697);
        aw8697->gain = 0x80;
        aw8697_haptic_set_gain(aw8697, aw8697->gain);
        aw8697_haptic_set_bst_vol(aw8697, AW8697_HAPTIC_HIGH_LEVEL_REG_VAL);
        aw8697_haptic_set_rtp_aei(aw8697, false);
        aw8697_interrupt_clear(aw8697);
        mutex_unlock(&aw8697->lock);
        if(aw8697_is_rtp_index_valid(aw8697, AW8697_OPPO_WAVEFORM_INDEX_HIGH_TEMP)) {
            aw8697->rtp_file_num = AW8697_OPPO_WAVEFORM_INDEX_HIGH_TEMP;
            if(AW8697_OPPO_WAVEFORM_INDEX_HIGH_TEMP) {
                //schedule_work(&aw8697->rtp_work);
                queue_work(system_unbound_wq, &aw8697->rtp_work);
            }
        } else {
            pr_err("%s: rtp_file_num 0x%02x over max value \n", __func__, aw8697->rtp_file_num);
        }
    } else if (aw8697->motor_old_test_mode == MOTOR_OLD_TEST_LISTEN_POP){
        mutex_lock(&aw8697->lock);
        aw8697_haptic_stop(aw8697);
        aw8697->gain = 0x80;
        aw8697_haptic_set_gain(aw8697, aw8697->gain);
        aw8697_haptic_set_bst_vol(aw8697, AW8697_HAPTIC_HIGH_LEVEL_REG_VAL);
        aw8697_haptic_set_rtp_aei(aw8697, false);
        aw8697_interrupt_clear(aw8697);
        mutex_unlock(&aw8697->lock);
        if(aw8697_is_rtp_index_valid(aw8697, AW8697_OPPO_WAVEFORM_INDEX_LISTEN_POP)) {
            aw8697->rtp_file_num = AW8697_OPPO_WAVEFORM_INDEX_LISTEN_POP;
            if(AW8697_OPPO_WAVEFORM_INDEX_LISTEN_POP) {
                //schedule_work(&aw8697->rtp_work);
                queue_work(system_unbound_wq, &aw8697->rtp_work);
            }
        } else {
            pr_err("%s: rtp_file_num 0x%02x over max value \n", __func__, aw8697->rtp_file_num);
        }
    } else {
        aw8697->motor_old_test_mode = 0;
        mutex_lock(&aw8697->lock);
        aw8697_haptic_stop(aw8697);
        //aw8697_haptic_android_stop(aw8697);
        mutex_unlock(&aw8697->lock);
    }
}


static ssize_t aw8697_motor_old_test_show(struct device *dev, struct device_attribute *attr,
                char *buf)
{
    return 0;
}

static ssize_t aw8697_motor_old_test_store(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif

    unsigned int databuf[1] = {0};

    if(1 == sscanf(buf, "%x", &databuf[0])) {
        if(databuf[0] == 0) {
            cancel_work_sync(&aw8697->motor_old_test_work);
            mutex_lock(&aw8697->lock);
            aw8697_haptic_stop(aw8697);
            mutex_unlock(&aw8697->lock);
        } else if(databuf[0] <= MOTOR_OLD_TEST_ALL_NUM) {
            cancel_work_sync(&aw8697->motor_old_test_work);
            aw8697->motor_old_test_mode = databuf[0];
            pr_err("%s: motor_old_test_mode = %d.\n", __func__, aw8697->motor_old_test_mode);
            schedule_work(&aw8697->motor_old_test_work);
        }
    }

    return count;
}

static ssize_t aw8697_waveform_index_show(struct device *dev, struct device_attribute *attr,
                char *buf)
{
    return 0;
}

static ssize_t aw8697_waveform_index_store(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[1] = {0};

    //aw8697->vmax = AW8697_HAPTIC_HIGH_LEVEL_REG_VAL;
    //aw8697->gain = 0x80;
    //aw8697_haptic_set_gain(aw8697, aw8697->gain);
    //aw8697_haptic_set_bst_vol(aw8697, aw8697->vmax);

    if(1 == sscanf(buf, "%x", &databuf[0])) {
        pr_err("%s: waveform_index = %d\n", __FUNCTION__, databuf[0]);
        mutex_lock(&aw8697->lock);
        aw8697->seq[0] = (unsigned char)databuf[0];
        aw8697_haptic_set_wav_seq(aw8697, 0, aw8697->seq[0]);
        aw8697_haptic_set_wav_seq(aw8697, 1, 0);
        aw8697_haptic_set_wav_loop(aw8697, 0, 0);
        mutex_unlock(&aw8697->lock);
    }
    return count;
}

static ssize_t aw8697_osc_data_show(struct device *dev, struct device_attribute *attr,
                char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif

    unsigned char lra_rtim_code = 0;
    ssize_t len = 0;

    mutex_lock(&aw8697->lock);
    aw8697_i2c_read(aw8697, AW8697_REG_TRIM_LRA, &lra_rtim_code);
    mutex_unlock(&aw8697->lock);
    
    pr_err("%s: lra_rtim_code = %d, code=%d\n", __FUNCTION__, lra_rtim_code, aw8697->clock_standard_OSC_lra_rtim_code);

    len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", aw8697->clock_standard_OSC_lra_rtim_code);
    return len;

}

static ssize_t aw8697_osc_data_store(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[1] = {0};
    //unsigned int lra_rtim_code = 0;

    if(1 == sscanf(buf, "%d", &databuf[0])) {
        pr_err("%s: lra_rtim_code = %d\n", __FUNCTION__, databuf[0]);
		aw8697->clock_standard_OSC_lra_rtim_code = databuf[0];
        mutex_lock(&aw8697->lock);
        if(databuf[0] > 0 )
            aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, (char)databuf[0]);
        mutex_unlock(&aw8697->lock);
    }
    return count;
}

static ssize_t aw8697_haptic_ram_test_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    unsigned int ram_test_result = 0;
    //len += snprintf(buf+len, PAGE_SIZE-len, "aw8697->ram_test_flag_0=%d  0:pass,!0= failed\n", aw8697->ram_test_flag_0 );
    //len += snprintf(buf+len, PAGE_SIZE-len, "aw8697->ram_test_flag_1=%d  0:pass,!0= failed\n", aw8697->ram_test_flag_1 );
    if (aw8697->ram_test_flag_0 != 0 || aw8697->ram_test_flag_1 != 0) {
        ram_test_result = 1;  // failed
        len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", ram_test_result);
    } else {
        ram_test_result = 0;  // pass
        len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", ram_test_result);
    }
    return len;
}

static ssize_t aw8697_haptic_ram_test_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    struct aw8697_container *aw8697_ramtest;
    int i ,j= 0;
    unsigned int val = 0;
    int rc = 0;
 //   unsigned int databuf[2] = {0};
    unsigned int start_addr;
 //   unsigned int end_addr;
    unsigned int tmp_len,retries;
    //unsigned char reg_val = 0;
    char *pbuf = NULL;
    pr_err("%s enter\n", __func__);

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;
    start_addr = 0;
    aw8697->ram_test_flag_0 = 0;
    aw8697->ram_test_flag_1 = 0;
    tmp_len = 1024 ; // /*1K*/
    retries= 8;  /*tmp_len * retries  =8*1024*/
    aw8697_ramtest = kzalloc(tmp_len*sizeof(char) +sizeof(int), GFP_KERNEL);
    if (!aw8697_ramtest) {
        pr_err("%s: error allocating memory\n", __func__);
        return count ;
    }
    pbuf = kzalloc(tmp_len*sizeof(char), GFP_KERNEL);
    if (!pbuf) {
        pr_err("%s: Error allocating memory\n", __func__);
        kfree(aw8697_ramtest);
        return count;
    }
    aw8697_ramtest->len = tmp_len;
    //printk("%s  --%d   aw8697_ramtest->len =%d \n",__func__,__LINE__,aw8697_ramtest->len);

    if (val ==1){
            /* RAMINIT Enable */
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_EN);
        for(j=0;j<retries;j++){


             /*test 1   start*/
             memset(aw8697_ramtest->data, 0xff, aw8697_ramtest->len);
             memset(pbuf, 0x00, aw8697_ramtest->len);
             /* write ram 1 test */
             aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRH, start_addr>>8);
             aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRL, start_addr&0x00FF);

             aw8697_i2c_writes(aw8697, AW8697_REG_RAMDATA,
                            aw8697_ramtest->data, aw8697_ramtest->len);

             /* read ram 1 test */
             aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRH, start_addr>>8);
             aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRL, start_addr&0x00FF);

             aw8697_i2c_reads(aw8697, AW8697_REG_RAMDATA, pbuf, aw8697_ramtest->len);

                 for(i=0;i<aw8697_ramtest->len ;i++){
                    if(pbuf[i]  != 0xff ){
                     //   printk("%s---%d---pbuf[%d]= 0x%x\n",__func__,__LINE__,i,pbuf[i]);
                        aw8697->ram_test_flag_1++;
                    }
                 }
             /*test 1  end*/
            /*test 0 start*/
            memset(aw8697_ramtest->data, 0x00, aw8697_ramtest->len);
            memset(pbuf, 0xff, aw8697_ramtest->len);

            /* write ram 0 test */
            aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRH, start_addr>>8);
            aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRL, start_addr&0x00FF);

             aw8697_i2c_writes(aw8697, AW8697_REG_RAMDATA,
                            aw8697_ramtest->data, aw8697_ramtest->len);

             /* read ram 0 test */
             aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRH, start_addr>>8);
             aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRL, start_addr&0x00FF);

             aw8697_i2c_reads(aw8697, AW8697_REG_RAMDATA, pbuf, aw8697_ramtest->len);

                for(i=0;i<aw8697_ramtest->len ;i++){
                    if(pbuf[i]  != 0 ){
                      //  printk("%s---%d---pbuf[%d]= 0x%x\n",__func__,__LINE__,i,pbuf[i]);
                         aw8697->ram_test_flag_0++;
                     }
                 } 

             /*test 0 end*/
             start_addr += tmp_len;
        }
        /* RAMINIT Disable */
         aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                     AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_OFF);
    }
    kfree(aw8697_ramtest);
    kfree(pbuf);
    pbuf = NULL;
    pr_err("%s exit\n", __func__);
    return count;
}

#endif

#ifdef VENDOR_EDIT
/* Hang.Zhao@PSW.BSP.CHG.Basic,2019/10/25, Modify for different haptics */
static ssize_t aw8697_device_id_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif

    return snprintf(buf, PAGE_SIZE, "%d\n", aw8697->device_id);
}

static ssize_t aw8697_device_id_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    return count;
}
#endif

static DEVICE_ATTR(state, S_IWUSR | S_IRUGO, aw8697_state_show, aw8697_state_store);
static DEVICE_ATTR(duration, S_IWUSR | S_IRUGO, aw8697_duration_show, aw8697_duration_store);
static DEVICE_ATTR(activate, S_IWUSR | S_IRUGO, aw8697_activate_show, aw8697_activate_store);
static DEVICE_ATTR(activate_mode, S_IWUSR | S_IRUGO, aw8697_activate_mode_show, aw8697_activate_mode_store);
static DEVICE_ATTR(index, S_IWUSR | S_IRUGO, aw8697_index_show, aw8697_index_store);
static DEVICE_ATTR(vmax, S_IWUSR | S_IRUGO, aw8697_vmax_show, aw8697_vmax_store);
static DEVICE_ATTR(gain, S_IWUSR | S_IRUGO, aw8697_gain_show, aw8697_gain_store);
static DEVICE_ATTR(seq, S_IWUSR | S_IRUGO, aw8697_seq_show, aw8697_seq_store);
static DEVICE_ATTR(loop, S_IWUSR | S_IRUGO, aw8697_loop_show, aw8697_loop_store);
static DEVICE_ATTR(register, S_IWUSR | S_IRUGO, aw8697_reg_show, aw8697_reg_store);
static DEVICE_ATTR(rtp, S_IWUSR | S_IRUGO, aw8697_rtp_show, aw8697_rtp_store);
static DEVICE_ATTR(ram_update, S_IWUSR | S_IRUGO, aw8697_ram_update_show, aw8697_ram_update_store);
static DEVICE_ATTR(f0, S_IWUSR | S_IRUGO, aw8697_f0_show, aw8697_f0_store);
static DEVICE_ATTR(cali, S_IWUSR | S_IRUGO, aw8697_cali_show, aw8697_cali_store);
static DEVICE_ATTR(cont, S_IWUSR | S_IRUGO, aw8697_cont_show, aw8697_cont_store);
static DEVICE_ATTR(cont_td, S_IWUSR | S_IRUGO, aw8697_cont_td_show, aw8697_cont_td_store);
static DEVICE_ATTR(cont_drv, S_IWUSR | S_IRUGO, aw8697_cont_drv_show, aw8697_cont_drv_store);
static DEVICE_ATTR(cont_num_brk, S_IWUSR | S_IRUGO, aw8697_cont_num_brk_show, aw8697_cont_num_brk_store);
static DEVICE_ATTR(cont_zc_thr, S_IWUSR | S_IRUGO, aw8697_cont_zc_thr_show, aw8697_cont_zc_thr_store);
static DEVICE_ATTR(vbat_monitor, S_IWUSR | S_IRUGO, aw8697_vbat_monitor_show, aw8697_vbat_monitor_store);
static DEVICE_ATTR(lra_resistance, S_IWUSR | S_IRUGO, aw8697_lra_resistance_show, aw8697_lra_resistance_store);
static DEVICE_ATTR(auto_boost, S_IWUSR | S_IRUGO, aw8697_auto_boost_show, aw8697_auto_boost_store);
static DEVICE_ATTR(prctmode, S_IWUSR | S_IRUGO, aw8697_prctmode_show, aw8697_prctmode_store);
static DEVICE_ATTR(trig, S_IWUSR | S_IRUGO, aw8697_trig_show, aw8697_trig_store);
static DEVICE_ATTR(ram_vbat_comp, S_IWUSR | S_IRUGO, aw8697_ram_vbat_comp_show, aw8697_ram_vbat_comp_store);
#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2019/02/19,  Add for old test */
static DEVICE_ATTR(osc_cali, S_IWUSR | S_IRUGO, aw8697_osc_cali_show, aw8697_osc_cali_store);
#endif

static DEVICE_ATTR(rtp_num, S_IWUSR | S_IRUGO, aw8697_rtp_num_show, aw8697_rtp_num_store);
static DEVICE_ATTR(haptic_audio, S_IWUSR | S_IRUGO, aw8697_haptic_audio_show, aw8697_haptic_audio_store);
static DEVICE_ATTR(haptic_audio_time, S_IWUSR | S_IRUGO, aw8697_haptic_audio_time_show, aw8697_haptic_audio_time_store);
#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/11/17,  Add for device file */
static DEVICE_ATTR(motor_old, S_IWUSR | S_IRUGO, aw8697_motor_old_test_show, aw8697_motor_old_test_store);
static DEVICE_ATTR(waveform_index, S_IWUSR | S_IRUGO, aw8697_waveform_index_show, aw8697_waveform_index_store);
static DEVICE_ATTR(osc_data, S_IWUSR | S_IRUGO, aw8697_osc_data_show, aw8697_osc_data_store);
static DEVICE_ATTR(ram_test, S_IWUSR | S_IRUGO, aw8697_haptic_ram_test_show, aw8697_haptic_ram_test_store);
static DEVICE_ATTR(f0_data, S_IWUSR | S_IRUGO, aw8697_f0_data_show, aw8697_f0_data_store);
#endif
#ifdef VENDOR_EDIT
/* Hang.Zhao@PSW.BSP.CHG.Basic,2019/10/25, Modify for different haptics */
static DEVICE_ATTR(device_id, S_IWUSR | S_IRUGO, aw8697_device_id_show, aw8697_device_id_store);
#endif

static struct attribute *aw8697_vibrator_attributes[] = {
    &dev_attr_state.attr,
    &dev_attr_duration.attr,
    &dev_attr_activate.attr,
    &dev_attr_activate_mode.attr,
    &dev_attr_index.attr,
    &dev_attr_vmax.attr,
    &dev_attr_gain.attr,
    &dev_attr_seq.attr,
    &dev_attr_loop.attr,
    &dev_attr_register.attr,
    &dev_attr_rtp.attr,
    &dev_attr_ram_update.attr,
    &dev_attr_f0.attr,
    &dev_attr_cali.attr,
    &dev_attr_cont.attr,
    &dev_attr_cont_td.attr,
    &dev_attr_cont_drv.attr,
    &dev_attr_cont_num_brk.attr,
    &dev_attr_cont_zc_thr.attr,
    &dev_attr_vbat_monitor.attr,
    &dev_attr_lra_resistance.attr,
    &dev_attr_auto_boost.attr,
    &dev_attr_prctmode.attr,
    &dev_attr_trig.attr,
    &dev_attr_ram_vbat_comp.attr,
    &dev_attr_osc_cali.attr,
    &dev_attr_rtp_num.attr,
    &dev_attr_haptic_audio.attr,
    &dev_attr_haptic_audio_time.attr,
#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/11/17,  Add for device file */
    &dev_attr_motor_old.attr,
    &dev_attr_waveform_index.attr,
    &dev_attr_osc_data.attr,
    &dev_attr_ram_test.attr,
    &dev_attr_f0_data.attr,
#endif
#ifdef VENDOR_EDIT
/* Hang.Zhao@PSW.BSP.CHG.Basic,2019/10/25, Modify for different haptics */
    &dev_attr_device_id.attr,
#endif
    NULL
};

static struct attribute_group aw8697_vibrator_attribute_group = {
    .attrs = aw8697_vibrator_attributes
};

static enum hrtimer_restart aw8697_vibrator_timer_func(struct hrtimer *timer)
{
    struct aw8697 *aw8697 = container_of(timer, struct aw8697, timer);

    pr_debug("%s enter\n", __func__);
    aw8697->state = 0;
    //schedule_work(&aw8697->vibrator_work);
    queue_work(system_highpri_wq, &aw8697->vibrator_work);

    return HRTIMER_NORESTART;
}

static void aw8697_vibrator_work_routine(struct work_struct *work)
{
      unsigned int val = 0;
    struct aw8697 *aw8697 = container_of(work, struct aw8697, vibrator_work);

#ifdef VENDOR_EDIT
    /* tongfeng.Huang@BSP.CHG.Basic, 2018/12/27,  Add for  vibration run in ram mode */
    aw8697->activate_mode = AW8697_HAPTIC_ACTIVATE_RAM_MODE;
    pr_err("%s enter, aw8697->state[%d], aw8697->activate_mode[%d], aw8697->ram_vbat_comp[%d]\n", 
                __func__, aw8697->state, aw8697->activate_mode, aw8697->ram_vbat_comp);
#endif
    mutex_lock(&aw8697->lock);
    aw8697_haptic_stop(aw8697);
    mutex_unlock(&aw8697->lock);
    //aw8697_haptic_android_stop(aw8697);
    if(aw8697->state) {
        mutex_lock(&aw8697->lock);
        if(aw8697->activate_mode == AW8697_HAPTIC_ACTIVATE_RAM_MODE) {

            if (aw8697->device_id == 832)
                aw8697_haptic_ram_vbat_comp(aw8697, false);
            else
                aw8697_haptic_ram_vbat_comp(aw8697, true);

            aw8697_haptic_play_repeat_seq(aw8697, true);
        } else if(aw8697->activate_mode == AW8697_HAPTIC_ACTIVATE_CONT_MODE) {
            aw8697_haptic_cont(aw8697);
        }
        mutex_unlock(&aw8697->lock);
        val = aw8697->duration;
        /* run ms timer */
        hrtimer_start(&aw8697->timer,
                  ktime_set(val / 1000, (val % 1000) * 1000000),
                  HRTIMER_MODE_REL);
    }
}

static int aw8697_vibrator_init(struct aw8697 *aw8697)
{
    int ret = 0;

    pr_err("%s enter\n", __func__);

#ifdef TIMED_OUTPUT
    aw8697->to_dev.name = "vibrator";
    aw8697->to_dev.get_time = aw8697_vibrator_get_time;
    aw8697->to_dev.enable = aw8697_vibrator_enable;

    ret = timed_output_dev_register(&(aw8697->to_dev));
    if ( ret < 0){
        dev_err(aw8697->dev, "%s: fail to create timed output dev\n",
                __func__);
        return ret;
    }
    ret = sysfs_create_group(&aw8697->to_dev.dev->kobj, &aw8697_vibrator_attribute_group);
    if (ret < 0) {
        dev_err(aw8697->dev, "%s error creating sysfs attr files\n", __func__);
        return ret;
    }
#else
    aw8697->cdev.name = "vibrator";
    aw8697->cdev.brightness_get = aw8697_haptic_brightness_get;
    aw8697->cdev.brightness_set = aw8697_haptic_brightness_set;

    ret = devm_led_classdev_register(&aw8697->i2c->dev, &aw8697->cdev);
    if (ret < 0){
        dev_err(aw8697->dev, "%s: fail to create led dev\n",
                __func__);
        return ret;
    }
    ret = sysfs_create_group(&aw8697->cdev.dev->kobj, &aw8697_vibrator_attribute_group);
    if (ret < 0) {
        dev_err(aw8697->dev, "%s error creating sysfs attr files\n", __func__);
        return ret;
     }
#endif
    hrtimer_init(&aw8697->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    aw8697->timer.function = aw8697_vibrator_timer_func;
    INIT_WORK(&aw8697->vibrator_work, aw8697_vibrator_work_routine);

    INIT_WORK(&aw8697->rtp_work, aw8697_rtp_work_routine);

    INIT_WORK(&aw8697->rtp_single_cycle_work, aw8697_rtp_single_cycle_routine);
    INIT_WORK(&aw8697->rtp_regroup_work, aw8697_rtp_regroup_routine);
    mutex_init(&aw8697->lock);

    return 0;
}




/******************************************************
 *
 * irq
 *
 ******************************************************/
static void aw8697_interrupt_clear(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;
    aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
    pr_err("%s: reg SYSINT=0x%x\n", __func__, reg_val);
}

static void aw8697_interrupt_setup(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;

    pr_err("%s enter\n", __func__);

    aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
    pr_err("%s: reg SYSINT=0x%x\n", __func__, reg_val);

    /* edge int mode */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DBGCTRL,
            AW8697_BIT_DBGCTRL_INT_MODE_MASK, AW8697_BIT_DBGCTRL_INT_MODE_EDGE);

    /* int enable */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
            AW8697_BIT_SYSINTM_BSTERR_MASK, AW8697_BIT_SYSINTM_BSTERR_OFF);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
            AW8697_BIT_SYSINTM_OV_MASK, AW8697_BIT_SYSINTM_OV_EN);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
            AW8697_BIT_SYSINTM_UVLO_MASK, AW8697_BIT_SYSINTM_UVLO_EN);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
            AW8697_BIT_SYSINTM_OCD_MASK, AW8697_BIT_SYSINTM_OCD_EN);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
            AW8697_BIT_SYSINTM_OT_MASK, AW8697_BIT_SYSINTM_OT_EN);
}

static irqreturn_t aw8697_irq(int irq, void *data)
{
    struct aw8697 *aw8697 = data;
    unsigned char reg_val = 0;
    unsigned char dbg_val = 0;
    unsigned int buf_len = 0;
	unsigned char glb_state_val = 0;

    pr_debug("%s enter\n", __func__);
    pm_qos_add_request(&pm_qos_req_vb, PM_QOS_CPU_DMA_LATENCY, PM_QOS_VALUE_VB);

    aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
    pr_debug("%s: reg SYSINT=0x%x\n", __func__, reg_val);
    aw8697_i2c_read(aw8697, AW8697_REG_DBGSTAT, &dbg_val);
    pr_debug("%s: reg DBGSTAT=0x%x\n", __func__, dbg_val);

    if(reg_val & AW8697_BIT_SYSINT_OVI) {
        //aw8697_op_clean_status(aw8697);
        pr_debug("%s chip ov int error\n", __func__);
    }
    if(reg_val & AW8697_BIT_SYSINT_UVLI) {
        aw8697_op_clean_status(aw8697);
        pr_err("%s chip uvlo int error\n", __func__);
    }
    if(reg_val & AW8697_BIT_SYSINT_OCDI) {
        aw8697_op_clean_status(aw8697);
        pr_err("%s chip over current int error\n", __func__);
    }
    if(reg_val & AW8697_BIT_SYSINT_OTI) {
        aw8697_op_clean_status(aw8697);
        pr_err("%s chip over temperature int error\n", __func__);
    }
    if(reg_val & AW8697_BIT_SYSINT_DONEI) {
        aw8697_op_clean_status(aw8697);
        pr_err("%s chip playback done\n", __func__);
    }

    if(reg_val & AW8697_BIT_SYSINT_FF_AEI) {
        pr_debug("%s: aw8697 rtp fifo almost empty int\n", __func__);
        if(aw8697->rtp_init) {
            while((!aw8697_haptic_rtp_get_fifo_afi(aw8697)) &&
                    (aw8697->play_mode == AW8697_HAPTIC_RTP_MODE)) {
                pr_debug("%s: aw8697 rtp mode fifo update, cnt=%d\n",
                        __func__, aw8697->rtp_cnt);
                if (!aw8697_rtp) {
                   pr_err("%s:aw8697_rtp is null break\n",
                        __func__);
                   break;
                }
                mutex_lock(&aw8697->rtp_lock);//vincent 
                if((aw8697_rtp->len-aw8697->rtp_cnt) < (aw8697->ram.base_addr>>2)) {
                    buf_len = aw8697_rtp->len-aw8697->rtp_cnt;
                } else {
                    buf_len = (aw8697->ram.base_addr>>2);
                }
                aw8697_i2c_writes(aw8697, AW8697_REG_RTP_DATA,
                        &aw8697_rtp->data[aw8697->rtp_cnt], buf_len);
                aw8697->rtp_cnt += buf_len;
				aw8697_i2c_read(aw8697, AW8697_REG_GLB_STATE, &glb_state_val);
				if ((aw8697->rtp_cnt == aw8697_rtp->len) || ((glb_state_val & 0x0f) == 0x00)) {
                    aw8697_op_clean_status(aw8697);
                    pr_err("%s: rtp update complete\n", __func__);
                    aw8697_haptic_set_rtp_aei(aw8697, false);
                    aw8697->rtp_cnt = 0;
                    aw8697->rtp_init = 0;
                    //
                    mutex_unlock(&aw8697->rtp_lock);//vincent
                    break;
                }
                //
                mutex_unlock(&aw8697->rtp_lock);//vincent 
            }
        } else {
            pr_err("%s: aw8697 rtp init = %d, init error\n", __func__, aw8697->rtp_init);
        }
    } else if(reg_val & AW8697_BIT_SYSINT_FF_AFI) {
        pr_debug("%s: aw8697 rtp mode fifo full interrupter\n", __func__);
    }

    if(aw8697->play_mode != AW8697_HAPTIC_RTP_MODE) {
        aw8697_haptic_set_rtp_aei(aw8697, false);
    }
    pm_qos_remove_request(&pm_qos_req_vb);

    //aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
    //pr_debug("%s: reg SYSINT=0x%x\n", __func__, reg_val);
    //aw8697_i2c_read(aw8697, AW8697_REG_SYSST, &reg_val);
    //pr_debug("%s: reg SYSST=0x%x\n", __func__, reg_val);

    pr_debug("%s exit\n", __func__);

    return IRQ_HANDLED;
}

/*****************************************************
 *
 * device tree
 *
 *****************************************************/
static int aw8697_parse_dt(struct device *dev, struct aw8697 *aw8697,
        struct device_node *np) {
    aw8697->reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);
    if (aw8697->reset_gpio < 0) {
        dev_err(dev, "%s: no reset gpio provided, will not HW reset device\n", __func__);
        return -1;
    } else {
        dev_info(dev, "%s: reset gpio provided ok\n", __func__);
    }
    aw8697->irq_gpio =  of_get_named_gpio(np, "irq-gpio", 0);
    if (aw8697->irq_gpio < 0) {
        dev_err(dev, "%s: no irq gpio provided.\n", __func__);
    } else {
        dev_info(dev, "%s: irq gpio provided ok.\n", __func__);
    }
#ifdef VENDOR_EDIT
/* Hang.Zhao@PSW.BSP.CHG.Basic,2019/10/25, Modify for different haptics */
    if (of_property_read_u32(np, "qcom,device_id", &aw8697->device_id))
        aw8697->device_id = 815;
    dev_info(dev, "%s: aw8697->device_id=%d\n", __func__, aw8697->device_id);
#endif
    return 0;
}

static int aw8697_hw_reset(struct aw8697 *aw8697)
{
    pr_err("%s enter\n", __func__);

    if (aw8697 && gpio_is_valid(aw8697->reset_gpio)) {
        gpio_set_value_cansleep(aw8697->reset_gpio, 0);
        usleep_range(1000, 2000);
        gpio_set_value_cansleep(aw8697->reset_gpio, 1);
        usleep_range(3500, 4000);
    } else {
        if (aw8697)
           dev_err(aw8697->dev, "%s:  failed\n", __func__);
    }
    return 0;
}

static void aw8697_haptic_reset_init(struct aw8697 *aw8697)
{
   // int ret = 0;

    unsigned char reg_val = 0;

    pr_err("%s enter software reset\n", __func__);

   
    /* haptic init */
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_STANDBY_MODE);
    aw8697_haptic_set_pwm(aw8697, AW8697_PWM_24K);
    aw8697_i2c_write(aw8697, AW8697_REG_BSTDBG1, 0x30);
    aw8697_i2c_write(aw8697, AW8697_REG_BSTDBG2, 0xeb);
    aw8697_i2c_write(aw8697, AW8697_REG_BSTDBG3, 0xd4);
    aw8697_i2c_write(aw8697, AW8697_REG_TSET, 0x12);
    aw8697_i2c_write(aw8697, AW8697_REG_R_SPARE, 0x68);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_ANADBG,
            AW8697_BIT_ANADBG_IOC_MASK, AW8697_BIT_ANADBG_IOC_4P65A);

    aw8697_haptic_set_bst_peak_cur(aw8697, AW8697_DEFAULT_PEAKCUR);
    aw8697_haptic_swicth_motorprotect_config(aw8697, 0x00, 0x00);
    aw8697_haptic_auto_boost_config(aw8697, false);
    aw8697_haptic_trig_param_init(aw8697);
    aw8697_haptic_trig_param_config(aw8697);
   aw8697_haptic_os_calibration(aw8697);
    aw8697_haptic_cont_vbat_mode(aw8697,
            AW8697_HAPTIC_CONT_VBAT_HW_COMP_MODE);
    aw8697->ram_vbat_comp = AW8697_HAPTIC_RAM_VBAT_COMP_ENABLE;

    aw8697_i2c_read(aw8697, AW8697_REG_TRIM_LRA, &reg_val);
    aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, reg_val);
}

/*****************************************************
 *
 * check chip id
 *
 *****************************************************/
static int aw8697_read_chipid(struct aw8697 *aw8697)
{
    int ret = -1;
    unsigned char cnt = 0;
    unsigned char reg = 0;

    while(cnt < AW_READ_CHIPID_RETRIES) {
        /* hardware reset */
        aw8697_hw_reset(aw8697);

        ret = aw8697_i2c_read(aw8697, AW8697_REG_ID, &reg);
        if (ret < 0) {
            dev_err(aw8697->dev, "%s: failed to read register AW8697_REG_ID: %d\n", __func__, ret);
        }
        switch (reg) {
        case AW8697_CHIPID:
            pr_err("%s aw8697 detected\n", __func__);
            aw8697->chipid = AW8697_CHIPID;
            //aw8697->flags |= AW8697_FLAG_SKIP_INTERRUPTS;
            aw8697_haptic_softreset(aw8697);
            return 0;
        default:
            pr_err("%s unsupported device revision (0x%x)\n", __func__, reg );
            break;
        }
        cnt ++;

        msleep(AW_READ_CHIPID_RETRY_DELAY);
    }

    return -EINVAL;
}

/******************************************************
 *
 * sys group attribute: reg
 *
 ******************************************************/
static ssize_t aw8697_i2c_reg_store(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
    struct aw8697 *aw8697 = dev_get_drvdata(dev);

    unsigned int databuf[2] = {0, 0};

    if(2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
        aw8697_i2c_write(aw8697, (unsigned char)databuf[0], (unsigned char)databuf[1]);
    }

    return count;
}

static ssize_t aw8697_i2c_reg_show(struct device *dev, struct device_attribute *attr,
                char *buf)
{
    struct aw8697 *aw8697 = dev_get_drvdata(dev);
    ssize_t len = 0;
    unsigned char i = 0;
    unsigned char reg_val = 0;
    for(i = 0; i < AW8697_REG_MAX; i ++) {
        if(!(aw8697_reg_access_0832[i]&REG_RD_ACCESS))
           continue;
        aw8697_i2c_read(aw8697, i, &reg_val);
        len += snprintf(buf+len, PAGE_SIZE-len, "reg:0x%02x=0x%02x \n", i, reg_val);
    }
    return len;
}
static ssize_t aw8697_i2c_ram_store(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
    struct aw8697 *aw8697 = dev_get_drvdata(dev);

    unsigned int databuf[1] = {0};

    if(1 == sscanf(buf, "%x", &databuf[0])) {
        if(1 == databuf[0]) {
            aw8697_ram_update(aw8697);
        }
    }

    return count;
}

static ssize_t aw8697_i2c_ram_show(struct device *dev, struct device_attribute *attr,
                char *buf)
{
    struct aw8697 *aw8697 = dev_get_drvdata(dev);
    ssize_t len = 0;
    unsigned int i = 0;
    unsigned char reg_val = 0;

    aw8697_haptic_stop(aw8697);
    /* RAMINIT Enable */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_EN);

    aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRH, (unsigned char)(aw8697->ram.base_addr>>8));
    aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRL, (unsigned char)(aw8697->ram.base_addr&0x00ff));
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697_haptic_ram:\n");
    for(i=0; i<aw8697->ram.len; i++) {
        aw8697_i2c_read(aw8697, AW8697_REG_RAMDATA, &reg_val);
        len += snprintf(buf+len, PAGE_SIZE-len, "0x%02x,", reg_val);
    }
    len += snprintf(buf+len, PAGE_SIZE-len, "\n");
    /* RAMINIT Disable */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_OFF);

    return len;
}

static DEVICE_ATTR(reg, S_IWUSR | S_IRUGO, aw8697_i2c_reg_show, aw8697_i2c_reg_store);
static DEVICE_ATTR(ram, S_IWUSR | S_IRUGO, aw8697_i2c_ram_show, aw8697_i2c_ram_store);

static struct attribute *aw8697_attributes[] = {
    &dev_attr_reg.attr,
    &dev_attr_ram.attr,
    NULL
};

static struct attribute_group aw8697_attribute_group = {
    .attrs = aw8697_attributes
};

/******************************************************
 *
 * i2c driver
 *
 ******************************************************/
static int aw8697_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
    struct aw8697 *aw8697;
    struct device_node *np = i2c->dev.of_node;
    int irq_flags = 0;
    int ret = -1;
    pr_err("0832 %s enter\n", __func__);
    AW8697_HAPTIC_RAM_VBAT_COMP_GAIN = 0x80;

    if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
        dev_err(&i2c->dev, "check_functionality failed\n");
        return -EIO;
    }

    aw8697 = devm_kzalloc(&i2c->dev, sizeof(struct aw8697), GFP_KERNEL);
    if (aw8697 == NULL)
        return -ENOMEM;

    aw8697->dev = &i2c->dev;
    aw8697->i2c = i2c;

    i2c_set_clientdata(i2c, aw8697);

    /* aw8697 rst & int */
    if (np) {
        ret = aw8697_parse_dt(&i2c->dev, aw8697, np);
        if (ret) {
            dev_err(&i2c->dev, "%s: failed to parse device tree node\n", __func__);
            goto err_parse_dt;
        }
    } else {
        aw8697->reset_gpio = -1;
        aw8697->irq_gpio = -1;
    }

    if (gpio_is_valid(aw8697->reset_gpio)) {
        ret = devm_gpio_request_one(&i2c->dev, aw8697->reset_gpio,
            GPIOF_OUT_INIT_LOW, "aw8697_rst");
        if (ret){
            dev_err(&i2c->dev, "%s: rst request failed\n", __func__);
            goto err_reset_gpio_request;
        }
    }

    if (gpio_is_valid(aw8697->irq_gpio)) {
        ret = devm_gpio_request_one(&i2c->dev, aw8697->irq_gpio,
            GPIOF_DIR_IN, "aw8697_int");
        if (ret){
            dev_err(&i2c->dev, "%s: int request failed\n", __func__);
            goto err_irq_gpio_request;
        }
    }

    /* aw8697 chip id */
    ret = aw8697_read_chipid(aw8697);
    if (ret < 0) {
        dev_err(&i2c->dev, "%s: aw8697_read_chipid failed ret=%d\n", __func__, ret);
        goto err_id;
    }

// add by huangtongfeng for vmalloc mem
    ret = aw8697_container_init(aw8697_container_size);
    if (ret < 0) {
        dev_err(&i2c->dev, "%s: aw8697_rtp alloc memory failed \n", __func__);
    }
// add end

    /* aw8697 irq */
    if (gpio_is_valid(aw8697->irq_gpio) &&
        !(aw8697->flags & AW8697_FLAG_SKIP_INTERRUPTS)) {
        /* register irq handler */
        aw8697_interrupt_setup(aw8697);
        irq_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
        ret = devm_request_threaded_irq(&i2c->dev,
                    gpio_to_irq(aw8697->irq_gpio),
                    NULL, aw8697_irq, irq_flags,
                    "aw8697", aw8697);
        if (ret != 0) {
            dev_err(&i2c->dev, "%s: failed to request IRQ %d: %d\n",
                    __func__, gpio_to_irq(aw8697->irq_gpio), ret);
            goto err_irq;
        }
    } else {
        dev_info(&i2c->dev, "%s skipping IRQ registration\n", __func__);
        /* disable feature support if gpio was invalid */
        aw8697->flags |= AW8697_FLAG_SKIP_INTERRUPTS;
    }

    dev_set_drvdata(&i2c->dev, aw8697);

    ret = sysfs_create_group(&i2c->dev.kobj, &aw8697_attribute_group);
    if (ret < 0) {
        dev_info(&i2c->dev, "%s error creating sysfs attr files\n", __func__);
        goto err_sysfs;
    }

    g_aw8697 = aw8697;

    aw8697_vibrator_init(aw8697);

    aw8697_haptic_init(aw8697);

    aw8697_ram_init(aw8697);
#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/11/17,  Add for old test */
    INIT_WORK(&aw8697->motor_old_test_work, oppo_motor_old_test_work);
    aw8697->motor_old_test_mode = 0;
#endif

    pr_err("%s probe completed successfully!\n", __func__);

    return 0;

err_sysfs:
    devm_free_irq(&i2c->dev, gpio_to_irq(aw8697->irq_gpio), aw8697);
err_irq:
err_id:
    if (gpio_is_valid(aw8697->irq_gpio))
        devm_gpio_free(&i2c->dev, aw8697->irq_gpio);
err_irq_gpio_request:
    if (gpio_is_valid(aw8697->reset_gpio))
        devm_gpio_free(&i2c->dev, aw8697->reset_gpio);
err_reset_gpio_request:
err_parse_dt:
    devm_kfree(&i2c->dev, aw8697);
    aw8697 = NULL;
    return ret;
}

static int aw8697_i2c_remove(struct i2c_client *i2c)
{
    struct aw8697 *aw8697 = i2c_get_clientdata(i2c);

    pr_err("%s enter\n", __func__);

    sysfs_remove_group(&i2c->dev.kobj, &aw8697_attribute_group);

    devm_free_irq(&i2c->dev, gpio_to_irq(aw8697->irq_gpio), aw8697);

    if (gpio_is_valid(aw8697->irq_gpio))
        devm_gpio_free(&i2c->dev, aw8697->irq_gpio);
    if (gpio_is_valid(aw8697->reset_gpio))
        devm_gpio_free(&i2c->dev, aw8697->reset_gpio);

    devm_kfree(&i2c->dev, aw8697);
    aw8697 = NULL;

    return 0;
}


static int __maybe_unused aw8697_suspend(struct device *dev)
{
    
    int ret = 0;
     struct aw8697 *aw8697 = dev_get_drvdata(dev);
     mutex_lock(&aw8697->lock);
     aw8697_haptic_stop(aw8697);
     mutex_unlock(&aw8697->lock);

    return ret;
}

static int __maybe_unused aw8697_resume(struct device *dev)
{
    
    int ret = 0;

    
    return ret;
}


static SIMPLE_DEV_PM_OPS(aw8697_pm_ops, aw8697_suspend, aw8697_resume);

static const struct i2c_device_id aw8697_i2c_id[] = {
    { AW8697_I2C_NAME, 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, aw8697_i2c_id);

static struct of_device_id aw8697_dt_match[] = {
    { .compatible = "awinic,aw8697_haptic,19696" },
    { },
};

static struct i2c_driver aw8697_i2c_driver = {
    .driver = {
        .name = AW8697_I2C_NAME,
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(aw8697_dt_match),
        .pm = &aw8697_pm_ops, // vincent add for QC's sugesstion suspend and resume 20190113
    },
    .probe = aw8697_i2c_probe,
    .remove = aw8697_i2c_remove,
    .id_table = aw8697_i2c_id,
};


static int __init aw8697_i2c_init(void)
{
    int ret = 0;

    pr_err("0832 aw8697 driver version %s\n", AW8697_VERSION);

    ret = i2c_add_driver(&aw8697_i2c_driver);
    if(ret){
        pr_err("fail to add aw8697 device into i2c\n");
        return ret;
    }

    return 0;
}
//late_initcall(aw8697_i2c_init);
module_init(aw8697_i2c_init);


static void __exit aw8697_i2c_exit(void)
{
    i2c_del_driver(&aw8697_i2c_driver);
}
module_exit(aw8697_i2c_exit);


MODULE_DESCRIPTION("AW8697 Haptic Driver");
MODULE_LICENSE("GPL v2");
