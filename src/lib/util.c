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

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "util.h"
#include "logger.h"

char *unescapestr(char *src)
{
	char *p1, *p2;
	int fl;

	if (src == NULL)
		return NULL;
	p2 = src;
	p1 = p2;
	fl = 0;
	while (*p2) {
		if (*p2 == '\\' && !fl)	{
			fl = 1;
			p2++;
		} else {
			*p1 = *p2;
	                p1++; p2++;
			fl = 0;
		}
	}
	*p1 = 0;

	return src;
}

char *parse_line(char *str, char *ltoken, int lsz)
{
	char *sp = str;
	char *ep, *p;
	int len;

	unescapestr(str);
	while (*sp && isspace(*sp)) sp++;
	if (!*sp || *sp == '#')
		return NULL;
	ep = sp + strlen(sp) - 1;
	while (isspace(*ep) && ep >= sp) *ep-- = '\0';
	if (*ep == '"')
		*ep = 0;
	if (!(p = strchr(sp, '=')))
		return NULL;
	len = p - sp;
	if (len >= lsz)
		return NULL;
	strncpy(ltoken, sp, len);
	ltoken[len] = 0;
	if (*(++p) == '"')
		p++;

	return p;
}

/*
	1 - exist
	0 - does't exist
	-1 - error
*/
int stat_file(const char *file)
{
	struct stat st;

	if (stat(file, &st)) {
		if (errno != ENOENT) 
			return -1;
		return 0;
	}
	return 1;
}

int make_dir(char *path, int full)
{
	char buf[4096];
	char *ps, *p;
	int len;

	if (path == NULL)
		return 0;

	ps = path + 1;
	while ((p = strchr(ps, '/'))) {
		len = p - path + 1;
		snprintf(buf, len, "%s", path);
		ps = p + 1;
		if (!stat_file(buf)) {
			if (mkdir(buf, 0755)) {
				logger(0, errno, "Can't create directory %s",
					buf);
				return 1;
			}
		}
	}
	if (!full)
		return 0;
	if (!stat_file(path)) {
		if (mkdir(path, 0755)) {
			logger(0, errno, "Can't create directory %s", path);
			return 1;
		}
	}
	return 0;
}

int parse_int(const char *str, int *val)
{
	char *tail;

	errno = 0;
	*val = (int)strtol(str, (char **)&tail, 10);
	if (*tail != '\0' || errno == ERANGE)
		return 1;
	return 0;
}

int parse_ul(const char *str, unsigned long *val)
{
	char *tail;

	errno = 0;
	*val = (int)strtoul(str, (char **)&tail, 10);
	if (*tail != '\0' || errno == ERANGE)
		return ERR_INVAL;
	return 0;
}

void str_tolower(const char *from, char *to)
{
        if (from == NULL || to == NULL)
                return;
        while ((*to++ = tolower(*from++)));
}

void str_toupper(const char *from, char *to)
{
        if (from == NULL || to == NULL)
                return;
        while ((*to++ = toupper(*from++)));
}

int check_var(const void *val, const char *message)
{
        if (val != NULL)
                return 0;
        logger(0, 0, "%s", message);

        return 1;
}

int cp_file(char *dst, char *src)
{
	int fd_src, fd_dst, len, ret = 0;
	struct stat st;
	char buf[4096];

	if (stat(src, &st) < 0) {
		logger(0, errno, "Unable to stat %s", src);
		return -1;
	}
	len = st.st_size;
	if ((fd_src = open(src, O_RDONLY)) < 0) {
		logger(0, errno, "Unable to open %s", src);
		return -1;
	}
	if ((fd_dst = open(dst, O_CREAT|O_RDWR, st.st_mode)) < 0) {
		logger(0, errno, "Unable to open %s", dst);
		close(fd_src);
		return -1;
	}
	while(1) {
		ret = read(fd_src, buf, sizeof(buf));
		if (!ret)
			break;
		else if (ret < 0) {
			logger(0, errno, "Unable to read from %s", src);
			ret = -1;
			break;
		}
		if (write(fd_dst, buf, len) < 0) {
			logger(0, errno, "Unable write to %s", dst);
			ret = -1;
			break;
		}
	}
	close(fd_src);
	close(fd_dst);

	return ret;
}

void get_vps_conf_path(envid_t veid, char *buf, int len)
{
	snprintf(buf, len, VPS_CONF_DIR "/%d.conf", veid);
}

char *arg2str(char **arg)
{
        char **p;
        char *str, *sp;
        int len = 0;

	if (arg == NULL)
		return NULL;
        p = arg;
        while (*p)
                len += strlen(*p++) + 1;
        if ((str = (char *)malloc(len + 1)) == NULL)
                return NULL;
        p = arg;
        sp = str;
        while (*p)
                sp += sprintf(sp, "%s ", *p++);

        return str;
}

void free_arg(char **arg)
{
	while (*arg) free(*arg++);
}

inline double max(double val1, double val2)
{
	return (val1 > val2) ? val1 : val2;
}

inline unsigned long max_ul(unsigned long val1, unsigned long val2)
{
	return (val1 > val2) ? val1 : val2;
}

inline unsigned long min_ul(unsigned long val1, unsigned long val2)
{
	return (val1 < val2) ? val1 : val2;
}

int yesno2id(const char *str)
{
	if (!strcmp(str, "yes"))
		return YES;
	else if (!strcmp(str, "no"))
		 return NO;
	return -1;
}

int get_ipaddr(const char *ip_str, unsigned int *ip)
{
        struct in_addr addr;

        if (!inet_aton(ip_str, &addr))
                return -1;
        *ip = addr.s_addr;

        return 0;
}

char *get_ipname(unsigned int ip)
{
	struct in_addr addr;

	addr.s_addr = ip;
	return inet_ntoa(addr);
}

char *subst_VEID(envid_t veid, char *src)
{
	char *srcp;
	char str[STR_SIZE];
	char *sp, *se;
	int r, len, veidlen;
	
	if (src == NULL)
		return NULL;
	/* Skip end '/' */
	se = src + strlen(src) - 1;
	while (se != str && *se == '/') {
		*se = 0;
		se--;
	}
	if ((srcp = strstr(src, "$VEID")))
		veidlen = sizeof("$VEID") - 1;
	else if ((srcp = strstr(src, "${VEID}")))
		veidlen = sizeof("${VEID}") - 1;
	else
		return strdup(src);

	sp = str;
	se = str + sizeof(str);
	len = srcp - src; /* Length of src before $VEID */
	if (len > sizeof(str))
		return NULL;
	memcpy(str, src, len);
	sp += len;
	r = snprintf(sp, se - sp, "%d", veid);
	sp += r;
	if ((r < 0) || (sp >= se))
		return NULL;
	if (*srcp) {
		r = snprintf(sp, se - sp, "%s", srcp + veidlen);
		sp += r;
		if ((r < 0) || (sp >= se))
			return NULL;
	}
	return strdup(str);
}

int get_pagesize()
{
	long pagesize;

	if ((pagesize = sysconf(_SC_PAGESIZE)) == -1) {
		fprintf(stderr, "Unable to get page size");
		return -11;
	}
	return pagesize;
}

int get_mem(unsigned long long *mem)
{
	long pagesize;
	if ((*mem = sysconf(_SC_PHYS_PAGES)) == -1) {
		logger(0, errno, "Unable to get total phys pages");
		return -1;
	}
	if ((pagesize = get_pagesize()) < 0)
		return -1;
	*mem *= pagesize;
	return 0;
}

int get_thrmax(int *thrmax)
{
	FILE *fd;
	char str[128];

	if (thrmax == NULL)
		return 1;
	if ((fd = fopen(PROCTHR, "r")) == NULL) {
		logger(0, errno, "Unable to open " PROCTHR);
		return 1;
	}
	if (fgets(str, sizeof(str), fd) == NULL) {
		fclose(fd);
		return 1;
	}
	fclose(fd);
	if (sscanf(str, "%du", thrmax) != 1)
		return 1;
	return 0;
}

int get_swap(unsigned long long *swap)
{
	FILE *fd;
	char str[128];

	if ((fd = fopen(PROCMEM, "r")) == NULL)	{
		logger(0, errno, "Cannot open " PROCMEM);
		return -1;
	}
	while (fgets(str, sizeof(str), fd)) {
		if (sscanf(str, "SwapTotal: %llu", swap) == 1) {
			*swap *= 1024;
			fclose(fd);
			return 0;
		}
	}
	logger(0, errno, "Swap: is not found in " PROCMEM );
	fclose(fd);

	return -1;
}

int get_lowmem(unsigned long long *mem)
{
	FILE *fd;
	char str[128];

	if ((fd = fopen(PROCMEM, "r")) == NULL)	{
		logger(0, errno, "Cannot open " PROCMEM);
		return -1;
	}
	while (fgets(str, sizeof(str), fd)) {
		if (sscanf(str, "LowTotal: %llu", mem) == 1) {
			fclose(fd);
			*mem *= 1024;
			return 0;
		}
	}
	logger(0, errno, "LowTotal: is not found in" PROCMEM);
	fclose(fd);
	return -1;
}

char *get_file_name(char *str)
{
	char *p;
	int len;

	len = strlen(str) - sizeof(".conf") + 1;
	if (len <= 0)
	return NULL;
	if (strcmp(str + len, ".conf"))
		return NULL;
	if ((p = malloc(len + 1)) == NULL)
		return NULL;
	strncpy(p, str, len);
	p[len] = 0;

	return p;
}

const char *get_vps_state_str(int vps_state)
{
	const char *p = NULL;

	switch (vps_state) {
	case STATE_RUNNING:
		p = "running";
		break;
	case STATE_STARTING:
		p = "starting";
		break;
	case STATE_STOPPED:
		p = "stopped";
		break;
	case STATE_STOPPING:
		p = "stopping";
		break;
	}
	return p;
}