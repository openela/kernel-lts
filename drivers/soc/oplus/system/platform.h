/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C), 2020, Oplus., All rights reserved.
 * File: - arch.c
 * Description: which platform you use.
 * Version: 1.0
 *-------------------------------------------------------------------
 */

#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#include <linux/string.h>
#include <linux/bug.h>

/*
* arch_of("mtk") or arch_of("qcom")
*/
bool arch_of(const char *arch);

#endif
