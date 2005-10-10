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
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/ioctl.h>
#include <asm/timex.h>
#include <linux/vzcalluser.h>

#include "vzerror.h"
#include "res.h"
#include "env.h"
#include "dist.h"
#include "exec.h"
#include "logger.h"
#include "util.h"
#include "script.h"
#include "iptables.h"

#define ENVRETRY	3
#define VZCTLDEV	"/dev/vzctl"

static _syscall1(long, setluid, uid_t, uid);
static int env_stop(vps_handler *h, envid_t veid, char *root, int stop_mode);

int vz_env_create_data_ioctl(vps_handler *h,
	struct vzctl_env_create_data *data)
{
	int errcode;
	int retry = 0;

	do {
		if (retry)
			sleep(1);
		errcode = ioctl(h->vzfd, VZCTL_ENV_CREATE_DATA, data);
	} while (errcode < 0 && errno == EBUSY && retry++ < ENVRETRY);

	return errcode;
}

int vz_env_create_ioctl(vps_handler *h, envid_t veid, int flags)
{
	struct vzctl_env_create env_create;
	int errcode;
	int retry = 0;

	memset(&env_create, 0, sizeof(env_create));
	env_create.veid = veid;
	env_create.flags = flags;
	do {
		if (retry)
			sleep(1);
		errcode = ioctl(h->vzfd, VZCTL_ENV_CREATE, &env_create);
	} while (errcode < 0 && errno == EBUSY && retry++ < ENVRETRY);

	return errcode;
}

/** Allocate and inittialize VPS handler.
 *
 * @param veid		VPS id.
 * @return		handler or NULL on error.
 */
vps_handler *vz_open(envid_t veid)
{
	int vzfd;
	vps_handler *h = NULL;

        if ((vzfd = open(VZCTLDEV, O_RDWR)) < 0) {
                logger(0, errno, "Unable to open %s", VZCTLDEV);
                logger(0, 0, "Please check that vzdev kernel module is loaded"
                        " and you have sufficient permissions"
                        " to access the file.");
		return NULL;
        }
	h = calloc(1, sizeof(*h));
	if (h == NULL) 
		goto err;
	h->vzfd = vzfd;
        if (vz_env_create_ioctl(h, 0, 0) < 0 &&
		(errno == ENOSYS || errno == EPERM))
        {
                logger(0, 0, "Your kernel lacks support for virtual"
                                " environments or modules not loaded");
		goto err;
	}
	return h;

err:
	if (h != NULL)
		free(h);
	close(vzfd);
	return NULL;
}

/** Close VPS handler.
 *
 * @param h		VPS handler.
 */
void vz_close(vps_handler *h)
{
	if (h == NULL)
		return;
	close(h->vzfd);
	free(h);
}

/** Get VPS status.
 *
 * @param h		VPS handler.
 * @param veid		VPS id.
 * @return		1 - VPS is running
 *			0 - VPS is stopped.
 */
int vps_is_run(vps_handler *h, envid_t veid)
{
	int ret;
	int errno;

	ret = vz_env_create_ioctl(h, veid, VE_TEST);

	if (ret < 0 && (errno == ESRCH || errno == ENOTTY))
		return 0;
	else if (ret < 0)
		logger(0, errno, "error on vz_env_create_ioctl(VE_TEST)");
        return 1;
}

static void vps_close_fd()
{
        int i;

	for (i = 0; i < FOPEN_MAX; i++)
		close(i);
}

/** Change root to specified directory
 *
 * @param		VPS root
 * @return		0 on success
 */
int vz_chroot(char *root)
{
	int i;
	sigset_t sigset;
	struct sigaction act;

	if (root == NULL) {
		logger(0, 0, "vz_chroot: VPS root is not specified");	
		return VZ_VE_ROOT_NOTSET;
	}
	if (chdir(root)) {
		logger(0, errno, "unable to change dir to %s",
			root);
		return VZ_RESOURCE_ERROR;
	}
	if (chroot(root)) {
		logger(0, errno, "chroot %s failed", root);
		return VZ_RESOURCE_ERROR;
	}
	setsid();
	sigemptyset(&sigset);
	sigprocmask(SIG_SETMASK, &sigset, NULL);
	sigemptyset(&act.sa_mask);
	act.sa_handler = SIG_DFL;
	act.sa_flags = 0;
	for (i = 1; i <= NSIG; ++i)
		sigaction(i, &act, NULL);
	return 0;
}

int vz_setluid(envid_t veid)
{
        if (setluid(veid) == -1) 
                return VZ_SETLUID_ERROR;
        return 0;
}

static int _env_create(vps_handler *h, envid_t veid, int wait_p, int err_p,
	void *data)
{
	struct vzctl_env_create_data env_create_data;
	struct env_create_param create_param;
	int i, fd, ret;
	vps_res *res;
	char *argv[] = {"init", NULL};
	char *envp[] = {"HOME=/", "TERM=linux", NULL};

	res = (vps_res *) data;
	memset(&create_param, 0, sizeof(create_param));
	create_param.iptables_mask = get_ipt_mask(res->env.ipt_mask);
	logger(3, 0, "Set iptables mask %d", create_param.iptables_mask);
	env_create_data.veid = veid;
	env_create_data.class_id = 0;
	env_create_data.flags = VE_CREATE | VE_EXCLUSIVE;
	env_create_data.data = &create_param;
	env_create_data.datalen = sizeof(create_param);

	ret = vz_env_create_data_ioctl(h, &env_create_data);
	if (ret < 0) {
		switch(errno) {
			case EACCES:
			/* License is not loaded */
				ret = VZ_NO_ACCES;
				break;
			case ENOTTY:
			/* Some vz modules are not present */
				ret = VZ_BAD_KERNEL;
				break;
			default:
				logger(0, errno, "env_create error");
				ret = VZ_ENVCREATE_ERROR;
				break;
		}
		goto env_err;
	}
	/* Create /fastboot to skip run fsck */
	fd = open("/fastboot", O_CREAT | O_RDONLY);
	close(fd);

	mk_reboot_script();
	if (res->dq.ugidlimit != NULL)
		mk_quota_link();
	/* Close status descriptor to report that
	 * environment is created.
	*/
	close(STDIN_FILENO);
	/* Now we wait until VPS setup will be done
	   If no error start init otherwise exit.
	*/
	if (read(wait_p, &ret, sizeof(ret)) != 0)
		return 0;
	/* close all fd */
	logger(10, 0, "Starting init");
	for (i = 0; i < FOPEN_MAX; i++) 
		if (i != err_p)
			close(i);
	execve("/sbin/init", argv, envp);
	execve("/etc/init", argv, envp);
	execve("/bin/init", argv, envp);

	ret = VZ_FS_BAD_TMPL;
	write(err_p, &ret, sizeof(ret));
env_err:
	return ret;
}

static int vz_real_env_create(vps_handler *h, envid_t veid, vps_res *res,
	int wait_p, int err_p, env_create_FN fn, void *data)
{
	int ret, pid;

	if ((ret = vz_chroot(res->fs.root)))
		return ret;
	if ((ret = vz_setluid(veid)))
		return ret;
	if ((ret = set_ublimit(h, veid, &res->ub)))
		return ret;
	/* Create another process to proper resource accounting */
	if ((pid = fork()) < 0) {
		logger(0, errno, "Unable to fork");
		return VZ_RESOURCE_ERROR;
	} else if (pid == 0) {

		if ((ret = vps_set_cap(veid, &res->cap)))
			goto env_err;
		if (fn == NULL) {
			ret = _env_create(h, veid, wait_p, err_p, (void *)res);
		} else {
			ret = fn(h, veid, wait_p, err_p, data);
		}
env_err:
		if (ret)
			write(STDIN_FILENO, &ret, sizeof(ret));
		exit(ret);
	}
	return 0;
}

int vz_env_create(vps_handler *h, envid_t veid, vps_res *res,
	int wait_p[2], int err_p[2], env_create_FN fn, void *data)
{
	int ret, pid, errcode;
	int status_p[2];
	struct sigaction act, actold;

	if (check_var(res->fs.root, "VE_ROOT is not set"))
		return VZ_VE_ROOT_NOTSET;
	if (pipe(status_p) < 0) {
		logger(0, errno, "Can not create pipe");
		return VZ_RESOURCE_ERROR;
	}
	sigaction(SIGCHLD, NULL, &actold);
       	sigemptyset(&act.sa_mask);
        act.sa_handler = SIG_IGN;
       	act.sa_flags = SA_NOCLDSTOP;
        sigaction(SIGCHLD, &act, NULL);

	if ((pid = fork()) < 0) {
		logger(0, errno, "Can not fork");
		ret =  VZ_RESOURCE_ERROR;
		goto err;
	} else if (pid == 0) {
		dup2(status_p[1], STDIN_FILENO);
		close(status_p[0]);
		close(status_p[1]);
		fcntl(STDIN_FILENO, F_SETFD, FD_CLOEXEC);
		fcntl(err_p[1], F_SETFD, FD_CLOEXEC);
		close(err_p[0]);
		fcntl(wait_p[0], F_SETFD, FD_CLOEXEC);
		close(wait_p[1]);

		ret = vz_real_env_create(h, veid, res, wait_p[0], err_p[1], fn,
			data);
		if (ret)
			write(STDIN_FILENO, &ret, sizeof(ret));
		exit(ret);
	}
	/* Wait for enviroment created */
	close(status_p[1]);
	close(wait_p[0]);
	close(err_p[1]);
	ret = read(status_p[0], &errcode, sizeof(errcode));
	if (ret) {
		ret = errcode;
		switch(ret) {
		case VZ_NO_ACCES:
			logger(0,0, "Permission denied");
			break;
		case VZ_BAD_KERNEL:
			logger(0, 0, "Invalid kernel, or some kernel"
				" modules are not loaded");
			break;
		case VZ_SET_CAP:
			logger(0, 0, "Unable to set capability");
			break;
		case VZ_RESOURCE_ERROR:
			logger(0, 0, "Not enough resources"
				" to start environment");
			break;
		}
        }
err:
	close(status_p[1]);
	close(status_p[0]);
	sigaction(SIGCHLD, &actold, NULL);

	return ret;
}

static void fix_numiptent(ub_param *ub)
{
	unsigned long min_ipt;
        ub_res *res;

	res = get_ub_res(ub, UB_IPTENTRIES);
	if (res == NULL)
		return;
	min_ipt = min_ul(res->limit[0], res->limit[1]);
        if (min_ipt < MIN_NUMIPTENT) {
		logger(0, 0, "Warning: NUMIPTENT %d:%d is less"
			" than minimally allowable value, set to %d:%d",
                        res->limit[0], res->limit[1],
                        MIN_NUMIPTENT, MIN_NUMIPTENT);
		res->limit[0] = MIN_NUMIPTENT;
		res->limit[1] = MIN_NUMIPTENT;
	}
}

static void fix_cpu(cpu_param *cpu)
{
	unsigned long *units;

	if (cpu->units == NULL && cpu->weight == NULL) {
		units = malloc(sizeof(*units));
		*cpu->units = UNLCPUUNITS;
	}
}

int vps_start_custom(vps_handler *h, envid_t veid, vps_param *param,
	skipFlags skip, struct mod_action *mod,
	env_create_FN fn, void *data)
{
	int wait_p[2];
	int err_p[2];
	int ret;
	char buf[64];
	char *dist_name;
	struct sigaction act;
	vps_res *res = &param->res;
	dist_actions *actions = NULL;

	actions = malloc(sizeof(*actions));
	dist_name = get_dist_name(&res->tmpl);
	if ((ret = read_dist_actions(dist_name, DIST_DIR, actions)))
		return ret;
	if (dist_name != NULL)
		free(dist_name);
	if (check_var(res->fs.root, "VE_ROOT is not set"))
		return VZ_VE_ROOT_NOTSET;
	if (vps_is_run(h, veid)) {
		logger(0, 0, "VPS is already running");
		return VZ_VE_RUNNING;
	}
	logger(0, 0, "Starting VPS ...");
	if (vps_is_mounted(res->fs.root)) {
		/* if VPS mounted umount first, to cleanup mount state */
		vps_umount(h, veid, res->fs.root, skip);
	}
	if (!vps_is_mounted(res->fs.root)) {
		/* increase quota to perform setup */
		quouta_inc(&res->dq, 100);	
		if ((ret = vps_mount(h, veid, &res->fs, &res->dq, skip)))
			return ret;
		quouta_inc(&res->dq, -100);
	}
	if (pipe(wait_p) < 0) {
		logger(0, errno, "Can not create pipe");
		return VZ_RESOURCE_ERROR;
	}
	if (pipe(err_p) < 0) {
		close(wait_p[0]);
		close(wait_p[1]);
		logger(0, errno, "Can not create pipe");
		return VZ_RESOURCE_ERROR;
	}
	sigemptyset(&act.sa_mask);
	act.sa_handler = SIG_IGN;
	act.sa_flags = 0;
	sigaction(SIGPIPE, &act, NULL);
	fix_numiptent(&res->ub);
	fix_cpu(&res->cpu);
	if ((ret = vz_env_create(h, veid, res, wait_p, err_p, fn, data)))
		goto err;
	if ((ret = vps_setup_res(h, veid, actions, &res->fs, param,
		STATE_STARTING, skip, mod)))
	{
		goto err;
	}
	if (!(skip & SKIP_ACTION_SCRIPT)) {
		snprintf(buf, sizeof(buf), VPS_CONF_DIR "%d.%s", veid,
			START_PREFIX);	
		if (stat_file(buf)) {
			if (vps_exec_script(h, veid, res->fs.root, NULL, NULL,
				buf, NULL, 0))
			{
				ret = VZ_ACTIONSCRIPT_ERROR;
				goto err;
			}
		}
	}
	/* Close fd to start /sbin/init */
	if (close(wait_p[1]))
		logger(0, errno, "Unable to close fd to start init");
err:
	free_dist_actions(actions);
	if (ret) {
		int err;
		/* Kill enviroment */
		logger(0, 0, "VPS start failed");
		write(wait_p[1], &err, sizeof(err));
	} else {
		if (!read(err_p[0], &ret, sizeof(ret))) {
                        logger(0, 0, "VPS start in progress...");
		} else {
			if (ret == VZ_FS_BAD_TMPL)
				logger(0, 0, "Unable to start init, probably"
					" incorrect template");
                        logger(0, 0, "VPS start failed");
                }
	}
	if (ret) {
		if (vps_is_run(h, veid))
			env_stop(h, veid, res->fs.root, M_KILL);
		vps_quotaoff(veid, &res->dq);
		if (vps_is_mounted(res->fs.root))
			vps_umount(h, veid, res->fs.root, skip);
	}
	close(wait_p[0]);
	close(wait_p[1]);
	close(err_p[0]);
	close(err_p[1]);

	return ret;
}

/** Sart and configure VPS.
 *
 * @param h		VPS handler.
 * @param veid		VPS id.
 * @param res		VPS resourses.
 * @param d_actions	distribution specific sctions.
 * @param skip		flags to skip VPS setup (SKIP_SETUP|SKIP_ACTION_SCRIPT)
 * @param action	modules list, used to call setup() callback
 * @return		0 on success.
 */
int vps_start(vps_handler *h, envid_t veid, vps_param *param,
	skipFlags skip, struct mod_action *mod)
{
	return vps_start_custom(h, veid, param, skip, mod, NULL, NULL);
}

static int real_env_stop(vps_handler *h, envid_t veid, char *vps_root,
	int stop_mode)
{
	int ret;

	if ((ret = vz_chroot(vps_root)))
		return ret;
	if ((ret = vz_setluid(veid)))
		return ret;
	if ((ret = vz_env_create_ioctl(h, veid, VE_ENTER)) < 0) {
		if (errno == ESRCH)
			return 0;
		logger(0, errno, "VPS enter failed");
		return ret;
	}
	vps_close_fd();
	switch (stop_mode) {
		case M_REBOOT:
		{
			char *argv[] = {"reboot", NULL};
			execvep(argv[0], argv, NULL);
			break;
		}
		case M_HALT:
		{
			char *argv[] = {"halt", NULL};
			execvep(argv[0], argv, NULL);
			break;
		}
		case M_KILL:
		{
			kill(-1, SIGTERM);
			sleep(1);
                        kill(1, SIGKILL);
			break;
		}
	}
	return 0;
}

static int env_stop(vps_handler *h, envid_t veid, char *root, int stop_mode)
{
	struct sigaction act, actold;
	int i, pid, ret = 0;

	sigaction(SIGCHLD, NULL, &actold);
	sigemptyset(&act.sa_mask);
	act.sa_handler = SIG_IGN;
	act.sa_flags = SA_NOCLDSTOP;
        sigaction(SIGCHLD, &act, NULL);

	logger(0, 0, "Stopping VPS ...");
	if (stop_mode == M_KILL)
		goto kill_vps;
	if ((pid = fork()) < 0) {
		logger(0, errno, "Can not fork");
		ret = VZ_RESOURCE_ERROR;
		goto out;
	} else if (pid == 0) {
		ret = real_env_stop(h, veid, root, stop_mode);
		exit(ret);
	}
	for (i = 0; i < MAX_SHTD_TM; i++) {
		sleep(1);
		if (!vps_is_run(h, veid)) {
			ret = 0;
			goto out;
		}
	}

kill_vps:
        if ((pid = fork()) < 0) {
		ret = VZ_RESOURCE_ERROR;
		logger(0, errno, "Can not fork");
		goto err;
	
	} else if (pid == 0) {
		ret = real_env_stop(h, veid, root, M_KILL);
		exit(ret);
	}
	ret = VZ_STOP_ERROR;	
	for (i = 0; i < MAX_SHTD_TM; i++) {
		usleep(500000);
		if (!vps_is_run(h, veid)) {
			ret = 0;
			break;
		}
	}
out:
	if (ret)
		logger(0, 0, "Unable to stop VPS, operation timed out");
	else
		logger(0, 0, "VPS was stopped");
err:
	sigaction(SIGCHLD, &actold, NULL);
	return ret;
}

/** Stop VPS.
 *
 * @param h		VPS handler.
 * @param veid		VPS id.
 * @param res		VPS resourses.
 * @param stop_mode	stop mode one of (M_REBOOT M_HALT M_KILL).
 * @param skip		flags to skip run action script (SKIP_ACTION_SCRIPT)
 * @param action	modules list, used to call cleanup() callback.
 * @return		0 on success.
 */
int vps_stop(vps_handler *h, envid_t veid, vps_param *param, int stop_mode,
	skipFlags skip, struct mod_action *action)
{
	int ret;
	list_head_t ips;
	char buf[64];
	vps_res *res = &param->res;

	list_head_init(&ips);
	if (check_var(res->fs.root, "VE_ROOT is not set"))
		return VZ_VE_ROOT_NOTSET;
	if (!vps_is_run(h, veid)) {
		logger(0, 0, "Unable to stop VPS is not running");
		return 0;
	}
	if (!(skip & SKIP_ACTION_SCRIPT)) {
		snprintf(buf, sizeof(buf), VPS_CONF_DIR "%d.%s", veid,
			STOP_PREFIX);	
		if (stat_file(buf)) {
			if (vps_exec_script(h, veid, res->fs.root, NULL, NULL,
				buf, NULL, 0))
			{
				return VZ_ACTIONSCRIPT_ERROR;
			}
		}
	}
	get_vps_ip(h, veid, &ips);
	if ((ret = env_stop(h, veid, res->fs.root, stop_mode)))
		return ret;
	mod_cleanup(h, veid, action, param);
	if (!res->net.skip_route_cleanup)
		run_net_script(veid, DEL, &ips, STATE_RUNNING);
	ret = vps_umount(h, veid, res->fs.root, skip);
	/* Clear VPS network configuration*/
	ret = run_pre_script(veid, VPS_STOP);
	free_str_param(&ips);

	return ret;
}