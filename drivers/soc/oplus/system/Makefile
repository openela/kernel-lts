# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2018-2020 Oplus. All rights reserved.
#
# Makefile for the OPLUS Linux Kernel system team specific device drivers.
#

obj-y 	+= platform.o
obj-$(CONFIG_OPLUS_FEATURE_OPROJECT) 	+= oplus_project/
ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
obj-$(CONFIG_OPLUS_FEATURE_PROJECTINFO)	+=  project_info/
endif
