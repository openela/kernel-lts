// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 * File: - arch.c
 * Description: which platform you use.
 * Version: 1.0
 *
 *-------------------------------------------------------------------
 */

#include <linux/string.h>
#include <linux/bug.h>
#include "platform.h"

static bool arch_of_qcom(void)
{
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	return true;
#endif
	return false;
}

static bool arch_of_mtk(void)
{
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_MTK
	return true;
#endif
	return false;
}

bool arch_of(const char *arch)
{
	if (!strncasecmp(arch, "qcom", strlen("qcom")))
		return arch_of_qcom();

	if (!strncasecmp(arch, "mtk", strlen("mtk")))
		return arch_of_mtk();

	BUG();

	return false;
}
