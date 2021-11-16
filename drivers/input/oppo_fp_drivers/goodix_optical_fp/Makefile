ifeq ($(CONFIG_OPPO_FINGERPRINT_QCOM),y)
ccflags-y += -DQCOM_PLATFORM
obj-y  += gf_spi.o gf_platform.o gf_netlink.o

else ifeq ($(CONFIG_OPPO_FINGERPRINT_MTK),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
subdir-ccflags-y += -I$(srctree)/drivers/spi/mediatek/$(MTK_PLATFORM)
subdir-ccflags-y += -I$(srctree)/drivers/spi/trustzone/spi/Tlspi/inc

ifneq ($(filter kernel-4.19, $(LINUX_KERNEL_VERSION)),)
include $(srctree)/drivers/tee/gud/Makefile.include
else ifneq ($(filter kernel-4.14 kernel-4.9, $(LINUX_KERNEL_VERSION)),)
include $(srctree)/drivers/misc/mediatek/gud/Makefile.include
endif

ccflags-y += -DMTK_PLATFORM

obj-y  += gf_tee.o
gf_tee-y := gf_spi_tee.o gf_platform_mtk.o gf_netlink.o
endif
