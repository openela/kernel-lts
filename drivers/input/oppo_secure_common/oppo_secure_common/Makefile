TARGET_PRODUCT=msmnile
ifeq ($(TARGET_PRODUCT),sdm845)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=845
else ifeq ($(TARGET_PRODUCT),sdm670)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=670
else ifeq ($(TARGET_PRODUCT),sdm710)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=710
else ifeq ($(TARGET_PRODUCT),sdm660_64)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=660
else ifeq ($(TARGET_PRODUCT),msmnile)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=855
else ifeq ($(TARGET_PRODUCT),trinket)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=6125
else ifneq ($(filter msm8953_64 S88051AA1, $(TARGET_PRODUCT)),)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=8953
else ifeq ($(TARGET_PRODUCT),msm8952_64)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=8976
else ifeq ($(TARGET_PRODUCT),atoll)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=7125
else ifeq ($(TARGET_PRODUCT),sm6150)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=7150
else ifeq ($(TARGET_PRODUCT),lito)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=7250
else ifeq ($(TARGET_PRODUCT),kona)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=8250
else ifneq (,$(findstring oppo6771,$(TARGET_PRODUCT)))
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=6771
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_export_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/module
else ifneq (,$(findstring oppo6779,$(TARGET_PRODUCT)))
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=6779
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_export_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/module
else ifeq ($(shell echo $(TARGET_PRODUCT) | sed -e 's/_1.*//g'),full_oppo6763)
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=6763
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_export_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/module
else ifneq (,$(findstring oppo6885,$(TARGET_PRODUCT)))
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=6885
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_export_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/module
else ifneq (,$(findstring oppo6873,$(TARGET_PRODUCT)))
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=6885
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_export_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/module
else ifneq (,$(findstring oppo6889,$(TARGET_PRODUCT)))
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=6885
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_export_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/module
else ifneq (,$(findstring oppo6853,$(TARGET_PRODUCT)))
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=6885
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_export_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/module
else ifneq (,$(findstring oppo6769,$(TARGET_PRODUCT)))
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=6768
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_export_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/module
else ifneq (,$(findstring oppo6785,$(TARGET_PRODUCT)))
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=6785
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_export_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/module
else
DEFS_PLATFORM = -DCONFIG_OPPO_BSP_SECCOM_PLATFORM=0
endif

ifneq ($(TARGET_PRODUCT),qssi)
ccflags-y += $(DEFS_PLATFORM)
obj-y     +=  oppo_secure_common.o
endif

