#
#Makefile for oppo project info
#
# include $(srctree)/drivers/misc/mediatek/Makefile.custom
#ifdef OPLUS_BUG_STABILITY
#wen.luo@BSP.Kernel.Stability, 2018/12/24,  for enable KASAN, use GCC complile tool, add flag -Wno-misleading-indentation
ifeq ($(CONFIG_KASAN),y)
subdir-ccflags-y += -Wno-format
endif
#endif /*OPLUS_BUG_STABILITY*/
ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
obj-$(CONFIG_OPLUS_FEATURE_OPROJECT) 	+= qcom/oplus_project.o 
else
obj-$(CONFIG_OPLUS_FEATURE_OPROJECT) 	+= mtk/oplus_project.o mtk/oplus_project_oldcdt.o
endif
ifeq ($(CONFIG_MTK_SECURITY_SW_SUPPORT), y)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/asf_export_inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/masp/asfv2/module
endif
