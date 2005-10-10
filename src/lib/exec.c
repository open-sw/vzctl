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

#include <sys/ioctl.h>
#include <asm/timex.h>
#include <linux/vzcalluser.h>

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>

#include "vzerror.h"
#include "exec.h"
#include "env.h"
#include "util.h"
#include "logger.h"
#include "script.h"

static volatile sig_atomic_t alarm_flag, child_exited;
static char *argv_bash[] = {"bash", NULL};
static char *envp_bash[] = {"HOME=/", "TERM=linux",
	"PATH=/bin:/sbin:/usr/bin:/usr/sbin:.", NULL};

int vz_env_create_ioctl(vps_handler *h, envid_t veid, int flags);

int execvep(const char *path, char *const argv[], char *const envp[])
{
	if (!strchr(path, '/')) {
		char *p = getenv("PATH");

		if (!p)
			p = "/bin:/usr/bin:/sbin";
		for (; p && *p;) {
			char partial[FILENAME_MAX];
			char *p2;

			p2 = strchr(p, ':');
			if (p2) {
				size_t len = p2 - p;

				strncpy(partial, p, len);
				partial[len] = 0;
			} else {
				strcpy(partial, p);
			}
			if (strlen(partial))
				strcat(partial, "/");
			strcat(partial, path);

			execve(partial, argv, envp != NULL ? envp : envp_bash);

			if (errno != ENOENT)
				return -1;
			if (p2) {
				p = p2 + 1;
			} else {
				p = 0;
			}	
		}
		return -1;
	} else 
		return execve(path, argv, envp);
}

int set_not_blk(int fd)
{
	int oldfl, ret;

	if ((oldfl = fcntl(fd, F_GETFL)) == -1)
		return -1;
	oldfl |= O_NONBLOCK;
	ret = fcntl(fd, F_SETFL, oldfl);

	return ret;	
}

static int stdredir(int rdfd, int wrfd)
{
	int lenr, lenw, lentotal, lenremain;
	char buf[10240];
	char *p;

	if ((lenr = read(rdfd, buf, sizeof(buf)-1)) > 0) {
		lentotal = 0;
		lenremain = lenr;
		p = buf;
		while (lentotal < lenr) {
			if ((lenw = write(wrfd, p, lenremain)) < 0) {
				if (errno != EINTR)
					return -1;
			} else {
				lentotal += lenw;
				lenremain -= lenw;
				p += lenw;
			}
		}
	} else if (lenr == 0){
		return -1;
	} else {
		if (errno == EAGAIN)
			return 1;
		else if (errno != EINTR)
			return -1;
	}
	return 0;
}

static void exec_handler(int sig)
{
	child_exited = 1;
	return;
}

static void alarm_handler(int sig)
{
	alarm_flag = 1;
	return;
}

static int vps_real_exec(vps_handler *h, envid_t veid, char *root,
	int exec_mode, char *const argv[], char *const envp[], char *std_in,
	int timeout)
{
	int ret, pid, status;
	int in[2], out[2], err[2], st[2];
	int fl = 0;
	struct sigaction act;	

	if (pipe(in) < 0 ||
		pipe(out) < 0 ||
		pipe(err) < 0 ||
		pipe(st) < 0)
	{
		logger(0, errno, "Unable to create pipe");
		return VZ_RESOURCE_ERROR;
	}
	/* Set non block mode */
	set_not_blk(out[0]);
	set_not_blk(err[0]);
	/* Setup alarm handler */
	if (timeout) {
		alarm_flag = 0;
		act.sa_flags = 0;
		sigemptyset(&act.sa_mask);
		act.sa_handler = alarm_handler;
		sigaction(SIGALRM, &act, NULL);
                alarm(timeout);
        }
	child_exited = 0;
        sigemptyset(&act.sa_mask);
        act.sa_flags = SA_NOCLDSTOP;
        act.sa_handler = exec_handler;
        sigaction(SIGCHLD, &act, NULL);

        act.sa_handler = SIG_IGN;
	act.sa_flags = 0;
        sigaction(SIGPIPE, &act, NULL);

	if ((ret = vz_setluid(veid)))
		return ret;
	if ((pid = fork()) < 0) {
		logger(0, errno, "Unable to fork");
		ret = VZ_RESOURCE_ERROR;
		goto err;
	} else if (pid == 0) {
		int i;

		close(0); close(1); close(2);
		dup2(in[0], STDIN_FILENO);
		dup2(out[1], STDOUT_FILENO);
		dup2(err[1], STDERR_FILENO);

		close(in[0]); close(out[1]); close(err[1]);
		close(in[1]); close(out[0]); close(err[0]);
		close(st[0]);
		fcntl(st[1], F_SETFD, FD_CLOEXEC);

		/* Logging can not be used after chroot */
		if ((ret = vz_chroot(root)))
			goto  env_err;
		ret = vz_env_create_ioctl(h, veid, VE_ENTER);
		if (ret < 0) {
			if (errno == ESRCH) 
	                        ret = VZ_VE_NOT_RUNNING;
			else
				ret = VZ_ENVCREATE_ERROR;
			goto env_err;
		}
		/* Close all fds */
		for (i = 3; i < FOPEN_MAX; i++) {
			if (i != st[1])
				close(i);
		}
		if (exec_mode == MODE_EXEC && argv != NULL) {
			execvep(argv[0], argv, envp != NULL ? envp : envp_bash);
		} else {
			execve("/bin/bash", argv != NULL ? argv : argv_bash,
					envp != NULL ? envp : envp_bash);
			execve("/bin/sh", argv != NULL ? argv : argv_bash,
					envp != NULL ? envp : envp_bash);
		}
		ret = VZ_FS_BAD_TMPL;
env_err:
		write(st[1], &ret, sizeof(ret));
		exit(ret);
	}
	close(st[1]);
	close(out[1]); close(err[1]); close(in[0]);
	while ((ret = read(st[0], &ret, sizeof(ret))) == -1)
		if (errno != EINTR)
			break;
	if (ret)
		goto err;
	if (std_in != NULL) {
		if (write(in[1], std_in, strlen(std_in)) < 0) {
			ret = VZ_COMMAND_EXECUTION_ERROR;
			/* Flush fd */
			while (stdredir(out[0], STDOUT_FILENO) == 0);
			while (stdredir(err[0], STDERR_FILENO) == 0);
			goto err;
		}
		close(in[1]);
		/* do not set STDIN_FILENO in select() */
		fl = 4;
	}
	while(!child_exited) {
		int n;
		fd_set rd_set;

		if (timeout && alarm_flag) {
			logger(0, 0, "Execution timeout expired");
			kill(pid, SIGTERM);
			alarm(0);
			break;
		}
		if ((fl & 3) == 3) {
			/* all fd are closed */
			close(in[1]);
			break;
		}
		FD_ZERO(&rd_set);
		if (!(fl & 4))
			FD_SET(STDIN_FILENO, &rd_set);
		if (!(fl & 1))
			FD_SET(out[0], &rd_set);
		if (!(fl & 2))
			FD_SET(err[0], &rd_set);
		n = select(FD_SETSIZE, &rd_set, NULL, NULL, NULL);
		if (n > 0) {
			if (FD_ISSET(out[0], &rd_set))
				if (stdredir(out[0], STDOUT_FILENO) < 0) {
					fl |= 1;
					close(out[0]);
				}
			if (FD_ISSET(err[0], &rd_set))
				if (stdredir(err[0], STDERR_FILENO) < 0) {
					fl |= 2;
					close(err[0]);
				}	
			if (FD_ISSET(STDIN_FILENO, &rd_set))
				if (stdredir(STDIN_FILENO, in[1]) < 0) {
					fl |= 4;
					close(in[1]);
				}	
		} else if (n < 0 && errno != EINTR) {
			logger(0, errno, "Error in select()");
			close(out[0]);
			close(err[0]);
			break;
		}
	}
	/* Flush fds */
	if (!(fl & 1)) {
		while (stdredir(out[0], STDOUT_FILENO) == 0);
	}
	if (!(fl & 2)) {
		while (stdredir(err[0], STDERR_FILENO) == 0);
	}
	while ((ret = waitpid(pid, &status, 0)) == -1)
		if (errno != EINTR)
			break;
	if (ret == pid) {
		ret = VZ_SYSTEM_ERROR;
		if (WIFEXITED(status))
			ret = WEXITSTATUS(status);
		else if (WIFSIGNALED(status)) {
			logger(0, 0, "Got signal %d",
				WTERMSIG(status));
			if (timeout && alarm_flag)
				ret = VZ_EXEC_TIMEOUT;
		}
	} else if (pid == -1 && errno != EINTR) {
		ret = VZ_SYSTEM_ERROR;
		logger(0, errno, "Error in waitpid()");
	}

err:
	close(st[0]); close(st[1]);
	close(out[0]); close(out[1]);
	close(err[0]); close(err[1]);
	close(in[0]); close(in[1]);

	return ret;
}

/** Execute command inside VPS.
 *
 * @param h		VPS handler.
 * @param veid		VPS id.
 * @param root		VPS root.
 * @param exec_mode	execution mode (MODE_EXEC, MODE_BASH).
 * @param arg		argv array.
 * @param envp		command environment array.
 * @param std_in	read command from buffer stdin point to.
 * @param timeout	execution timeout, 0 - unlimited.
 * @return		0 on success.
 */
int vps_exec(vps_handler *h, envid_t veid, char *root, int exec_mode, 
	char *const argv[], char *const envp[], char *std_in, int timeout)
{
	int pid, ret, status;

	if (check_var(root, "VPS root is not set"))
		return VZ_VE_ROOT_NOTSET;
	if (!vps_is_run(h, veid)) {
		logger(0, 0, "VPS is not running");
		return VZ_VE_NOT_RUNNING;
	}
	if ((pid = fork()) < 0) {
		logger(0, errno, "Can not fork");
		return VZ_RESOURCE_ERROR;
	} else if (pid == 0) {
		ret = vps_real_exec(h, veid, root, exec_mode, argv, envp,
			std_in, timeout);
		exit(ret);
	}
	while ((ret = waitpid(pid, &status, 0)) == -1)
		if (errno != EINTR)
			break;
	if (ret == pid) {
		ret = VZ_SYSTEM_ERROR;
		if (WIFEXITED(status))
			ret = WEXITSTATUS(status);
		else if (WIFSIGNALED(status)) {
			logger(0, 0, "Got signal %d", WTERMSIG(status));
		}
	} else if (pid < 0) {
		ret = VZ_SYSTEM_ERROR;
		logger(0, errno, "Error in waitpid()");
	}
	return ret;
}

/** Read script and execute it in VPS.
 *
 * @param h		VPS handler.
 * @param veid		VPS id.
 * @param root		VPS root.
 * @param arg		argv array.
 * @param envp		command environment array.
 * @param fname		script file name
 * @paran func		function file name
 * @param timeout	execution timeout, 0 - unlimited.
 * @return		0 on success.
 */
int vps_exec_script(vps_handler *h, envid_t veid, char *root,
	char *const argv[], char *const envp[], const char *fname, char *func,
	int timeout)
{
	int len, ret;
	char *script = NULL;

	if ((len = read_script(fname, func, &script)) < 0)
		return -1;
	logger(1, 0, "Running VPS script: %s", fname);
	ret = vps_exec(h, veid, root, MODE_BASH, argv, envp, script, timeout);
	if (script != NULL)
		free(script);
	return ret;
}


int vps_run_script(vps_handler *h, envid_t veid, char *script, vps_param *vps_p)
{
	int is_run;
	int rd_p[2], wr_p[2];
	int ret, retry;
	char *argv[2];
	char *root = vps_p->res.fs.root;

	if (!stat_file(script))	{
		logger(0, 0, "Script not found: %s", script);
		return VZ_NOSCRIPT;
	}
	if (pipe(rd_p) || pipe(wr_p)) {
		logger(0, errno, "Unable to create pipe");
		return VZ_RESOURCE_ERROR;
	}
	if (check_var(root, "VE_ROOT is not set"))
		return VZ_VE_ROOT_NOTSET;
	if (check_var(vps_p->res.fs.private, "VE_PRIVATE is not set"))
		return VZ_VE_PRIVATE_NOTSET;
	if (!stat_file(vps_p->res.fs.private)) {
		logger(0, 0, "VPS private area %s does not exist",
			vps_p->res.fs.private);
                return VZ_FS_NOPRVT;	
	}
	if (!(is_run = vps_is_run(h, veid))) {
		if (!vps_is_mounted(root)) {
			if ((ret = fsmount(veid, &vps_p->res.fs,
				&vps_p->res.dq )))
			{
				return ret;
			}
		}
		if ((ret = vz_env_create(h, veid, &vps_p->res, rd_p, wr_p,
			NULL, NULL)))
		{
			return ret;
		}
	}
	argv[0] = script;
	argv[1] = NULL;
	ret = vps_exec_script(h, veid, root, argv, NULL, script,
		NULL, 0);
	if (!is_run) {
		write(rd_p[1], &ret, sizeof(ret));
		retry = 0;
		while (retry++ < 10 && vps_is_run(h, veid))
			usleep(500000);
		fsumount(veid, root);
	}
	return ret;
}