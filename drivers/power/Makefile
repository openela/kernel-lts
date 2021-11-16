KBUILD_CFLAGS +=   -DVENDOR_EDIT
KBUILD_CPPFLAGS += -DVENDOR_EDIT
CFLAGS_KERNEL +=   -DVENDOR_EDIT
CFLAGS_MODULE +=   -DVENDOR_EDIT
obj-$(CONFIG_POWER_AVS)		+= avs/
obj-$(CONFIG_POWER_RESET)	+= reset/
obj-$(CONFIG_POWER_SUPPLY)	+= supply/

#ifdef OPLUS_FEATURE_CHG_BASIC /* Jianchao.Shi@BSP.CHG.Basic, 2018/01/12, sjc Add for charging */
obj-$(CONFIG_OPLUS_SM8150R_CHARGER)	+= oppo/
#endif

#ifdef VENDOR_EDIT /* Jianchao.Shi@BSP.CHG.Basic, 2018/01/12, sjc Add for charging */
obj-$(CONFIG_OPPO_SM7125R_CHARGER)	+= oppo/
#endif

#ifdef VENDOR_EDIT /* Jianchao.Shi@BSP.CHG.Basic, 2018/01/12, sjc Add for charging */
obj-$(CONFIG_OPLUS_SM7150R_CHARGER)	+= oppo/
#endif
