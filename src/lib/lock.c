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
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/file.h>

#include "vzerror.h"
#include "types.h"
#include "logger.h"
#include "util.h"
#include "lock.h"

/*
 * Read pid id from lock file:
 * return: -1 read error
 * 	    0 incorrect pid
 * 	   >0 pid id
 */
static int getlockpid(char *file)
{
	int fd, pid = -1;
	char buf[STR_SIZE];
	int len;

	if ((fd = open(file, O_RDONLY)) == -1)
		return -1;
	if ((len = read(fd, buf, sizeof(buf))) >= 0) {
		buf[len] = 0;
		if (sscanf(buf, "%d", &pid) != 1) {
			logger(1, 0, "Incorrect pid: %s in %s", buf, file);
			pid = 0;
		}
	}
	close(fd);

	return pid;
}

/** Lock VPS.
 * Create lock file $dir/$veid.lck.
 * @param veid		VPD id.
 * @param dir		lock directory.
 * @param status	transition status.
 * @return		0 - success
 *                      1 - locked
 *                      -1- error.
 */
int vps_lock(envid_t veid, char *dir, char *status)
{
	int fd, pid;
	char buf[STR_SIZE];
	char lockfile[STR_SIZE];
	char tmp_file[STR_SIZE];
	struct stat st;
	int retry = 0;
	int ret = -1;

	if (check_var(dir, "lockdir is not set"))
		return -1;
	if (!stat_file(dir))
		if (make_dir(dir, 1))
			return -1;
	/* Create temp lock file */
	snprintf(lockfile, sizeof(lockfile), "%s/%d.lck", dir, veid);
	snprintf(tmp_file, sizeof(tmp_file), "%sXXXXXX", lockfile);
	if ((fd = mkstemp(tmp_file)) < 0) {
		if (errno == EROFS)
			logger(0, errno, "Unable to create"
				" lock file %s, use --skiplock option",
				tmp_file);	
		else
			logger(0, errno, "Unable to create"
				" temporary lock file: %s", tmp_file);
		return -1;
	}
	snprintf(buf, sizeof(buf), "%d\n%s\n", getpid(),
		status == NULL ? "" : status);
	write(fd, buf, strlen(buf));
	close(fd);
	while (retry < 3) {
		/* vps locked */
		if (!link(tmp_file, lockfile)) {
			ret = 0;
			break;
		}
		pid = getlockpid(lockfile);
		if (pid < 0) {
			/*  Error read pid id */
			usleep(500000);
			retry++;
			continue;
		} else if (pid == 0) {
			/* incorrect pid, remove lock file */
			unlink(lockfile);
		} else {
			snprintf(buf, sizeof(buf), "/proc/%d", pid);
			if (!stat(buf, &st)) {
				ret = 1;
				break;
			} else {
				logger(0, 0, "Removing stale lock"
						" file %s", lockfile);
				unlink(lockfile);
			}
		}
		retry++;
	}
	unlink(tmp_file);
	return ret;
}

/** Unlock VPS.
 *
 * @param veid		VPS id.
 * @param dir		lock directory.
 */
void vps_unlock(envid_t veid, char *dir)
{
	char lockfile[STR_SIZE];

	snprintf(lockfile, sizeof(lockfile), "%s/%d.lck", dir, veid);
	unlink(lockfile);
}

int _lock(char *lockfile, int blk)
{
	int fd, op;

	op  = (blk) ? LOCK_EX : LOCK_EX | LOCK_NB;

	if ((fd = open(lockfile, O_CREAT | O_RDWR,
			S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)) == -1)
	{
		logger(0, errno, "Unable to create lock file %s", lockfile);
		return -1;
	}
	if (flock(fd, op)) {
		if (errno == EWOULDBLOCK) {
			close(fd);
			return -2;
		} else {
			logger(0, errno, "Error in flock()");
			close(fd);
			return -1;
		}
	}
	return fd;
}

void _unlock(int fd, char *lockfile)
{
	if (fd < 0)
		return;
	unlink(lockfile);
	if (flock(fd, LOCK_UN))
		logger(0, errno, "Error in flock(LOCK_UN)");
	close(fd);
}