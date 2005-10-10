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
#ifndef _VZLIST_H
#define _VZLIST_H

#include <linux/config.h>
#if defined(CONFIG_VZ_LIST) || defined(CONFIG_VZ_LIST_MODULE)
#include <linux/vzlist.h>
#if defined(VZCTL_GET_VEIDS) && defined(VZCTL_GET_VEIPS)
#define HAVE_VZLIST_IOCTL 1
#endif
#endif

#define PROCVEINFO	"/proc/vz/veinfo"
#define PROCUBC		"/proc/user_beancounters"
#define PROCQUOTA	"/proc/vz/vzquota"
#define PROCVEINFO	"/proc/vz/veinfo"
#define PROCFSHED	"/proc/fairsched"

#define SCRIPT_DIR	"/etc/sysconfig/vz-scripts/"
#define CFG_FILE	"/etc/sysconfig/vz"
#define VZQUOTA		"/usr/sbin/vzquota"

#define MAXCPUUNITS	500000

enum {
	VE_RUNNING,
	VE_STOPPED,
	VE_MOUNTED
};

char *ve_status[]= {
	"running",
	"stopped",
	"mounted"
};

struct Cubc {
	unsigned long kmemsize[5];
	unsigned long lockedpages[5];
	unsigned long privvmpages[5];
	unsigned long shmpages[5];
	unsigned long numproc[5];
	unsigned long physpages[5];
	unsigned long vmguarpages[5];
	unsigned long oomguarpages[5];
	unsigned long numtcpsock[5];
	unsigned long numflock[5];
	unsigned long numpty[5];
	unsigned long numsiginfo[5];
	unsigned long tcpsndbuf[5];
	unsigned long tcprcvbuf[5];
	unsigned long othersockbuf[5];
	unsigned long dgramrcvbuf[5];
	unsigned long numothersock[5];
	unsigned long dcachesize[5];
	unsigned long numfile[5];
	unsigned long numiptent[5];
};

struct Cquota {
	unsigned long diskspace[3];	// 0-usage 1-softlimit 2-hardlimit
	unsigned long diskinodes[3];	// 0-usage 1-softlimit 2-hardlimit
};

struct Cla {
	double la[3];
};

struct Ccpu {
	unsigned long limit[2];		// 0-limit, 1-units
};

struct Cveinfo {
	int veid;
	char *hostname;
	char *ip;
	char *ve_private;
	char *ve_root;
	struct Cubc *ubc;
	struct Cquota *quota;
	struct Cla *la;
	struct Ccpu *cpu;
	int status;
	int hide;
};

#define RES_NONE        0
#define RES_HOSTNAME    1
#define RES_UBC         2
#define RES_QUOTA       3
#define RES_IP          4
#define RES_LA          5
#define RES_CPU         6

struct Cfield {
	char *name;
	char *hdr;
	char *hdr_fmt;
	int index;
	int res_type;
	void (* const print_fn)(struct Cveinfo *p, int index);
	int (* const sort_fn)(const void* val1, const void* val2);
};

struct Cfield_order {
	int order;
	struct Cfield_order *next;
};

#ifndef NIPQUAD
#define NIPQUAD(addr) \
	((unsigned char *)&addr)[0], \
	((unsigned char *)&addr)[1], \
	((unsigned char *)&addr)[2], \
	((unsigned char *)&addr)[3]
#endif

#endif //_VZLIST_H