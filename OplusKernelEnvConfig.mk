# Copyright (C), 2008-2030, OPPO Mobile Comm Corp., Ltd
### All rights reserved.
###
### File: - OplusKernelEnvConfig.mk
### Description:
###     you can get the oplus feature variables set in android side in this file
###     this file will add global macro for common oplus added feature
###     BSP team can do customzation by referring the feature variables
### Version: 1.0
### Date: 2020-03-18
### Author: Liang.Sun
###
### ------------------------------- Revision History: ----------------------------
### <author>                        <date>       <version>   <desc>
### ------------------------------------------------------------------------------
### Liang.Sun@TECH.Build              2020-03-18   1.0         Create this moudle
##################################################################################

ALLOWED_MCROS := \
OPLUS_FEATURE_STORAGE_TOOL  \
OPLUS_FEATURE_UFSPLUS  \
OPLUS_FEATURE_QCOM_PMICWD  \
OPLUS_FEATURE_CHG_BASIC  \
OPLUS_FEATURE_CONNFCSOFT  \
OPLUS_FEATURE_AGINGTEST  \
OPLUS_FEATURE_SENSOR_SMEM  \
OPLUS_FEATURE_TP_BASIC  \
OPLUS_FEATURE_DUMPDEVICE  \
OPLUS_FEATURE_MULTI_FREEAREA  \
OPLUS_FEATURE_AUDIO_FTM \
OPLUS_FEATURE_KTV \
OPLUS_FEATURE_SAU  \
OPLUS_BUG_COMPATIBILITY \
OPLUS_BUG_STABILITY \
OPLUS_BUG_DEBUG \
OPLUS_ARCH_EXTENDS \
VENDOR_EDIT \
OPLUS_FEATURE_POWERINFO_STANDBY \
OPLUS_FEATURE_POWERINFO_STANDBY_DEBUG  \
OPLUS_FEATURE_POWERINFO_RPMH \
OPLUS_FEATURE_POWERINFO_FTM \
OPLUS_FEATURE_ADSP_RECOVERY \
OPLUS_FEATURE_MODEM_MINIDUMP

$(foreach myfeature,$(ALLOWED_MCROS),\
         $(eval KBUILD_CFLAGS += -D$(myfeature)) \
         $(eval KBUILD_CPPFLAGS += -D$(myfeature)) \
         $(eval CFLAGS_KERNEL += -D$(myfeature)) \
         $(eval CFLAGS_MODULE += -D$(myfeature)) \
)

# BSP team can do customzation by referring the feature variables

ifeq ($(OPLUS_FEATURE_SECURE_GUARD),yes)
export CONFIG_OPLUS_SECURE_GUARD=y
KBUILD_CFLAGS += -DCONFIG_OPLUS_SECURE_GUARD
KBUILD_CPPFLAGS += -DCONFIG_OPLUS_SECURE_GUARD
CFLAGS_KERNEL += -DCONFIG_OPLUS_SECURE_GUARD
CFLAGS_MODULE += -DCONFIG_OPLUS_SECURE_GUARD
endif

ifeq ($(OPLUS_FEATURE_QCOM_PMICWD),yes)
export OPLUS_FEATURE_QCOM_PMICWD=yes
export CONFIG_OPLUS_FEATURE_QCOM_PMICWD=y
KBUILD_CFLAGS += -DCONFIG_OPLUS_FEATURE_QCOM_PMICWD
endif

ifeq ($(OPLUS_FEATURE_SECURE_KEVENTUPLOAD),yes)
export CONFIG_OPLUS_KEVENT_UPLOAD=y
KBUILD_CFLAGS += -DCONFIG_OPLUS_KEVENT_UPLOAD
KBUILD_CPPFLAGS += -DCONFIG_OPLUS_KEVENT_UPLOAD
CFLAGS_KERNEL += -DCONFIG_OPLUS_KEVENT_UPLOAD
CFLAGS_MODULE += -DCONFIG_OPLUS_KEVENT_UPLOAD
endif

# Yuwei.Zhang@MULTIMEDIA.DISPLAY.LCD, 2020/09/25, sepolicy for aod ramless
ifeq ($(OPLUS_FEATURE_AOD_RAMLESS),yes)
KBUILD_CFLAGS += -DOPLUS_FEATURE_AOD_RAMLESS
KBUILD_CPPFLAGS += -DOPLUS_FEATURE_AOD_RAMLESS
CFLAGS_KERNEL += -DOPLUS_FEATURE_AOD_RAMLESS
CFLAGS_MODULE += -DOPLUS_FEATURE_AOD_RAMLESS
endif

#Chao.Zhang@MULTIMEDIA.DISPLAY.LCD, 2020/10/31, add for hdr10 feature
ifeq ($(OPLUS_FEATURE_HDR10),yes)
KBUILD_CFLAGS += -DOPLUS_FEATURE_HDR10
KBUILD_CPPFLAGS += -DOPLUS_FEATURE_HDR10
endif

#Chao.Zhang@MULTIMEDIA.DISPLAY.LCD, 2020/10/23, add for DP MAX20328
ifeq ($(OPLUS_FEATURE_DP_MAX20328),yes)
KBUILD_CFLAGS += -DOPLUS_FEATURE_DP_MAX20328
endif

#xupengcheng@MULTIMEDIA.DISPLAY.LCD, 2020/11/11, add for cabc feature
ifeq ($(OPLUS_FEATURE_LCD_CABC),yes)
KBUILD_CFLAGS += -DOPLUS_FEATURE_LCD_CABC
endif


#xupengcheng@MULTIMEDIA.DISPLAY.LCD, 2020/12/10, add for 19696 TPS65132
ifeq ($(OPLUS_FEATURE_TPS65132),yes)
KBUILD_CFLAGS += -DOPLUS_FEATURE_TPS65132
endif

#xupengcheng@MULTIMEDIA.DISPLAY.LCD, 2020/12/29, add for samsung 90fps Global HBM backlight issue
ifeq ($(OPLUS_FEATURE_90FPS_GLOBAL_HBM),yes)
KBUILD_CFLAGS += -DOPLUS_FEATURE_90FPS_GLOBAL_HBM
KBUILD_CPPFLAGS += -DOPLUS_FEATURE_90FPS_GLOBAL_HBM
endif
