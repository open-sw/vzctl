/*
 *  Copyright (C) 2000-2006 SWsoft. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>

#include "list.h"
#include "logger.h"
#include "config.h"
#include "vzerror.h"
#include "util.h"
#include "script.h"
#include "lock.h"
#include "modules.h"
#include "create.h"

#define BACKUP		0
#define DESTR		1

static int destroydir(char *dir);

int del_dir(char *dir)
{
	int ret;
	char *argv[4];

	argv[0] = "/bin/rm";
	argv[1] = "-rf";
	argv[2] = dir;
	argv[3] = NULL;
	ret = run_script("/bin/rm", argv, NULL, 0);

	return ret;
}

int vps_destroy_dir(envid_t veid, char *dir)
{
        int ret;

	if (!quota_ctl(veid, QUOTA_STAT)) {
		if ((ret = quota_off(veid, 0)))
			if ((ret = quota_off(veid, 1)))
				return ret;
	}
	quota_ctl(veid, QUOTA_DROP);	
	if ((ret = destroydir(dir)))
		return ret;
        return 0;
}

static char *get_destroy_root(char *dir)
{
	struct stat st;
	int id, len;
	char *p, *prev;
	char tmp[STR_SIZE];

	if (stat(dir, &st) < 0)
		return NULL;
	id = st.st_dev;
	p = dir + strlen(dir) - 1;
	prev = p;
	while (p > dir) {
		while (p > dir && (*p == '/' || *p == '.')) p--;
		while (p > dir && *p != '/') p--;
		if (p <= dir)
			break;
		len = p - dir + 1;
		strncpy(tmp, dir, len);
		tmp[len] = 0;
		if (stat(tmp, &st) < 0)
			return NULL;
		if (id != st.st_dev)
			break;
		prev = p;
	}
	len = prev - dir;
	if (len) {
		strncpy(tmp, dir, len);
		tmp[len] = 0;
		return strdup(tmp);
	}
	return NULL;
}

char *maketmpdir(const char *dir)
{
        char buf[STR_SIZE];
        char *tmp;
        char *tmp_dir;
	int len;

        snprintf(buf, sizeof(buf), "%s/XXXXXXX", dir);
	if ((tmp = mkdtemp(buf)) == NULL) {
		logger(0, errno, "Error in mkdtemp(%s)", buf);
		return NULL;
	}
	len = strlen(dir);
        tmp_dir = (char *)malloc(strlen(tmp) - len);
	if (tmp_dir == NULL)
		return NULL;
        strcpy(tmp_dir, tmp + len + 1);

        return tmp_dir;
}

static void _destroydir(char *root)
{
	char buf[STR_SIZE];
	struct stat st;
	struct dirent *ep;
	DIR *dp;
	int del, ret;

	do {
		if (!(dp = opendir(root)))
			return;
		del = 0;
		while ((ep = readdir(dp))) {
			if (!strcmp(ep->d_name, ".") ||
				!strcmp(ep->d_name, ".."))
			{
				continue;
			}
			snprintf(buf, sizeof(buf), "%s/%s", root, ep->d_name);
			if (stat(buf, &st)) 
				continue;
                	if (!S_ISDIR(st.st_mode))
				continue;
			snprintf(buf, sizeof(buf), "rm -rf %s/%s",
				root, ep->d_name);
			ret = system(buf);
			if (ret == -1 || WEXITSTATUS(ret))
				sleep(10);
			del = 1;
		}
		closedir(dp);
	} while(del);
}

static int destroydir(char *dir)
{
	char buf[STR_SIZE];
	char tmp[STR_SIZE];
	char *root;
	char *tmp_nm;
	int fd_lock, pid;
	struct sigaction act, actold;
	int ret = 0;
	struct stat st;

	if (stat(dir, &st)) {
		if (errno != ENOENT) {
			logger(0, errno, "Unable to stat %s", dir);
			return -1;
		}
		return 0;
	}
	if (!S_ISDIR(st.st_mode)) {
		logger(0, 0, "Warning: VPS private area is not a directory");
		if (unlink(dir)) {
			logger(0, errno, "Unable to unlink %s", dir);
			return -1;
		}
		return 0;
	}
	root = get_destroy_root(dir);
	if (root == NULL) {
		logger(0, 0, "Unable to get root for %s", dir);
		return -1;
	}
	snprintf(tmp, sizeof(buf), "%s/tmp", root);
	free(root);
	if (!stat_file(tmp)) {
		if (mkdir(tmp, 0755)) {
			logger(0, errno, "Can't create tmp dir %s", tmp);
			return VZ_FS_DEL_PRVT;
		}
	}
	/* First move to del */
	if ((tmp_nm = maketmpdir(tmp)) == NULL)	{
		logger(0, 0, "Unable to generate temporary name in %s", tmp);
		return VZ_FS_DEL_PRVT;
	}
	snprintf(buf, sizeof(tmp), "%s/%s", tmp, tmp_nm);
	free(tmp_nm);
	if (rename(dir, buf)) {
		logger(0, errno, "Can't move %s -> %s", dir, buf);
		rmdir(buf);
		return VZ_FS_DEL_PRVT;
	}
	snprintf(buf, sizeof(buf), "%s/rm.lck", tmp);
	if ((fd_lock = _lock(buf, 0)) == -2) {
		/* Already locked */
		_unlock(fd_lock, NULL);
		return 0;
	} else if (fd_lock == -1)
		return VZ_FS_DEL_PRVT;
	
	sigaction(SIGCHLD, NULL, &actold);
	sigemptyset(&act.sa_mask);
	act.sa_handler = SIG_IGN;
	act.sa_flags = SA_NOCLDSTOP;
	sigaction(SIGCHLD, &act, NULL);

	if (!(pid = fork())) {
		int fd;

		setsid();
		fd = open("/dev/null", O_WRONLY);
		if (fd != -1) {
			close(0);
			close(1);
			close(2);
			dup2(fd, 1);
			dup2(fd, 2);
		}
		_destroydir(tmp);
		_unlock(fd_lock, buf);
		exit(0);
	} else if (pid < 0)  {
		logger(0, errno, "destroydir: Unable to fork");
		ret = VZ_RESOURCE_ERROR;
	}
	sleep(1);
	sigaction(SIGCHLD, &actold, NULL);
	return ret;
}

int vps_destroy(vps_handler *h, envid_t veid, fs_param *fs)
{
	int ret;
	
	if (check_var(fs->private, "VE_PRIVATE is not set"))
		return VZ_VE_PRIVATE_NOTSET;
	if (check_var(fs->root, "VE_ROOT is not set"))
		return VZ_VE_ROOT_NOTSET;
	if (vps_is_mounted(fs->root)) {
		logger(0, 0, "VPS is currently mounted (umount first)");
                return VZ_FS_MOUNTED;
	}
	logger(0, 0, "Destroying VPS private area: %s", fs->private);
	if ((ret = vps_destroy_dir(veid, fs->private)))
		return ret;
	move_config(veid, BACKUP);
	rmdir(fs->root);
	logger(0, 0, "VPS private area was destroyed");

	return 0;
}