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
#ifndef _UTIL_H_
#define	_UTIL_H_

#include "types.h"
#define PROCMEM		"/proc/meminfo"
#define PROCTHR		"/proc/sys/kernel/threads-max"

char *parse_line(char *str, char *ltoken, int lsz);
int stat_file(const char *file);
int make_dir(char *path, int full);
int parse_int(const char *str, int *val);
int parse_ul(const char *str, unsigned long *val);
int check_var(const void *val, const char *message);
int cp_file(char *dst, char *src);
void get_vps_conf_path(envid_t veid, char *buf, int len);
char *arg2str(char **arg);
void free_arg(char **arg);
unsigned long min_ul(unsigned long val1, unsigned long val2);
int yesno2id(const char *str);
int get_ipaddr(const char *ip_str, unsigned int *ip);
char *get_ipname(unsigned int ip);
char *subst_VEID(envid_t veid, char *src);
int get_pagesize();
int get_mem(unsigned long long *mem);
int get_thrmax(int *thrmax);
int get_swap(unsigned long long *swap);
int get_lowmem(unsigned long long *mem);
unsigned long max_ul(unsigned long val1, unsigned long val2);
void str_tolower(const char *from, char *to);
char *get_file_name(char *str);
const char *get_vps_state_str(int vps_state);

#endif