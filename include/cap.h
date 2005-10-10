/*
 * Copyright (C) 2000-2005 SWsoft. All rights reserved.
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined by Trolltech AS of Norway and appearing in the file
 * LICENSE.QPL included in the packaging of this file.
 *
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
#ifndef _CAP_H_
#define _CAP_H_

#include <linux/types.h>

typedef __u32 cap_t;

#define CAP_TO_MASK(x) (1 << (x))
#define cap_raise(c, flag) (c |=  CAP_TO_MASK(flag))
#define cap_lower(c, flag) (c &=  CAP_TO_MASK(flag))

/* Data structure for capability mask see /usr/include/linux/capability.h
 */
typedef struct {
	unsigned long on;
	unsigned long off;
} cap_param;

/** Add capability name to capability mask.
 *
 * @param name		capability name.
 * @param mask		capability mask.
 * @return		0 on success.
 */
int get_cap_mask(char *name, unsigned long *mask);

/** Apply capability mask to VPS.
 *
 * @param veid		VPS id.
 * @param cap		capability mask.
 * @return		0 on success.
 */
int vps_set_cap(int veid, cap_param *cap);

/** Merge capabilities and return in string format.
 *
 * @param new		new capability mask.
 * @param old		old capamility mask.
 * @param buf		merged capabilities in string format.
 * @return
 */
void build_cap_str(cap_param *new, cap_param *old, char *buf, int len);

#endif