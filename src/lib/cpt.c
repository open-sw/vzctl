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
#include <stdio.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/cpt_ioctl.h>
#include <linux/vzcalluser.h>
#include <string.h>
#include <sys/types.h>
#include <sys/param.h>
#include <unistd.h>
#include <sys/wait.h>

#include "cpt.h"
#include "env.h"
#include "exec.h"
#include "script.h"
#include "config.h"
#include "vzerror.h"
#include "logger.h"
#include "util.h"

int cpt_cmd(vps_handler *h, envid_t veid, int action, cpt_param *param)
{
	int fd;
	list_head_t iplist;
	int err, ret = 0;
	const char *file;

	list_head_init(&iplist);
	if (!vps_is_run(h, veid)) {
		logger(0, 0, "VPS is not running");
		return VZ_VE_NOT_RUNNING;
	}
	if (action == CMD_CHKPNT) {
		file = PROC_CPT;
		err = VZ_CHKPNT_ERROR;
	} else if (action == CMD_RESTORE) {
		file = PROC_RST;
		err = VZ_RESTORE_ERROR;
	} else {
		logger(0, 0, "cpt_cmd: Unsupported cmd");
		return -1;
	}
	if ((fd = open(file, O_RDWR)) < 0) {
		if (errno == ENOENT)
			logger(0, errno, "Error: No checkpointing"
				" support, unable to open %s", file);
		else
			logger(0, errno, "Unable to open %s", file);
		return err;
	}
	if ((ret = ioctl(fd, CPT_JOIN_CONTEXT, param->ctx ? : veid)) < 0) {
		logger(0, errno, "cannot join cpt context %d", param->ctx);
		goto err;
	}
	switch (param->cmd) {
	case CMD_KILL:
		logger(0, 0, "Killing...");
		if ((ret = ioctl(fd, CPT_KILL, 0)) < 0) {
			logger(0, errno, "cannot kill VPS");
			goto err;
		}
		break;
	case CMD_RESUME:
		logger(0, 0, "Resuming...");
		if ((ret = ioctl(fd, CPT_RESUME, 0)) < 0) {
			logger(0, errno, "cannot resume VPS");
			goto err;
		}
		break;
	}
	if (!param->ctx) {
		logger(2, 0, "\tput context");
		if ((ret = ioctl(fd, CPT_PUT_CONTEXT, 0)) < 0) {
			logger(0, errno, "cannot put context");
			goto err;
		}
	}
err:
	close(fd);
	return ret ? err : 0;
}

int real_chkpnt(int cpt_fd, envid_t veid, char *root, cpt_param *param,
	int cmd)
{
	int ret, len;
	char buf[PIPE_BUF];
	int err_p[2];

	if ((ret = vz_chroot(root)))
		return VZ_CHKPNT_ERROR;
	if (pipe(err_p) < 0) {
		logger(0, errno, "Can't create pipe");
		return VZ_CHKPNT_ERROR;
	}
	fcntl(err_p[0], F_SETFL, O_NONBLOCK);
	fcntl(err_p[1], F_SETFL, O_NONBLOCK);
	if (ioctl(cpt_fd, CPT_SET_ERRORFD, err_p[1]) < 0) {
		logger(0, errno, "Can't set errorfd");
		return VZ_CHKPNT_ERROR;
	}
	close(err_p[1]);
	if (cmd == CMD_CHKPNT || cmd == CMD_SUSPEND) {
		logger(0, 0, "\tsuspend...");
		if (ioctl(cpt_fd, CPT_SUSPEND, 0) < 0) {
			logger(0, errno, "Can not suspend VPS");
			goto err_out;
		}
	}
	if (cmd == CMD_CHKPNT || cmd == CMD_DUMP) {
		logger(0, 0, "\tdump...");
		if (ioctl(cpt_fd, CPT_DUMP, 0) < 0) {
			logger(0, errno, "Can not dump VPS");
			if (cmd == CMD_CHKPNT)
				if (ioctl(cpt_fd, CPT_RESUME, 0) < 0)
					logger(0, errno, "Can not resume VPS");
			if (cmd == CMD_DUMP && !param->ctx)
				if (ioctl(cpt_fd, CPT_PUT_CONTEXT, 0) < 0)
					logger(0, errno, "Can not put context");
			goto err_out;
		}
	}
	if (cmd == CMD_CHKPNT) {
		logger(0, 0, "\tkill...");
		if (ioctl(cpt_fd, CPT_KILL, 0) < 0) {
			logger(0, errno, "Can not kill VPS");
			goto err_out;
		}
	}
	if (cmd == CMD_SUSPEND && !param->ctx) {
		logger(0, 0, "\tget context...");
		if (ioctl(cpt_fd, CPT_GET_CONTEXT, veid) < 0) {
			logger(0, errno, "Can not get context");
			goto err_out;
		}
	}
	close(err_p[0]);
	return 0;
err_out:
	if ((len = read(err_p[0], buf, PIPE_BUF)) > 0) {
		logger(0, 0, "Checkpointing failed");
		len = write(STDERR_FILENO, buf, len);
		fflush(stderr);	
	}
	close(err_p[0]);
	return VZ_CHKPNT_ERROR;
}

int vps_chkpnt(vps_handler *h, envid_t veid, vps_param *vps_p, int cmd,
	cpt_param *param)
{
	list_head_t iplist;
	int dump_fd = -1;
	char dumpfile[PATH_LEN];
	int cpt_fd, pid, ret;
	int status;
	char *root = vps_p->res.fs.root;

	list_head_init(&iplist);
	ret = VZ_CHKPNT_ERROR;
	if (root == NULL) {
		logger(0, 0, "VPS root is not set");
		return VZ_VE_ROOT_NOTSET;
	}
	if (!vps_is_run(h, veid)) {
		logger(0, 0, "Unable to setup chackpoint, VPS is not running");
		return VZ_VE_NOT_RUNNING;
	}
	logger(0, 0, "Setup checkpoint...");
	if ((cpt_fd = open(PROC_CPT, O_RDWR)) < 0) {
		if (errno == ENOENT)
			logger(0, errno, "Error: No checkpointing"
				" support, unable to open " PROC_CPT);
		else
			logger(0, errno, "Unable to open " PROC_CPT);
		return VZ_CHKPNT_ERROR;
	}
	if (param->dumpfile == NULL) {
		if (cmd == CMD_DUMP) {
			logger(0,  0, "Error: dumpfile is not specified.");
			goto err;
		}
		snprintf(dumpfile, sizeof(dumpfile), "%s/"DEF_DUMPFILE,
			param->dumpdir != NULL ? param->dumpdir : DEF_DUMPDIR,
			veid);
	}
	if ((param->dumpfile != NULL || !param->ctx) && 
		(cmd == CMD_CHKPNT ||
		 cmd == CMD_DUMP))
	{
		dump_fd = open(param->dumpfile ? : dumpfile,
			O_CREAT|O_TRUNC|O_RDWR, 0600);
		if (dump_fd < 0) {
			logger(0, errno, "cannot create dump file %s",
				param->dumpfile ? : dumpfile);
			goto err;
		}
	}
	if (cmd > CMD_SUSPEND) {
		logger(0, 0, "\tjoin context..");
		if (ioctl(cpt_fd, CPT_JOIN_CONTEXT,
			param->ctx ? : veid) < 0)
		{
			logger(0, errno, "cannot join cpt context");
			goto err;
		}
	} else {
		if (ioctl(cpt_fd, CPT_SET_VEID, veid) < 0) {
			logger(0, errno, "cannot set veid");
			goto err;
		}
	}
	if (cmd == CMD_CHKPNT || cmd == CMD_DUMP) {
		if (ioctl(cpt_fd, CPT_SET_DUMPFD, dump_fd) < 0) {
			logger(0, errno, "cannot set dump file");
			goto err;
		}
	}
	if (param->cpu_flags) {
		logger(0, 0, "\tset cpu flags..");
		if (ioctl(cpt_fd, CPT_SET_CPU_FLAGS, param->cpu_flags) < 0) {
			logger(0, errno, "cannot set cpu flags");
			goto err;
		}
	}
	if ((ret = vz_setluid(veid)))
		goto err;
	if ((pid = fork()) < 0) {
		logger(0, errno, "Can't fork");
		goto err;
	} else if (pid == 0) {
		ret = real_chkpnt(cpt_fd, veid, root, param, cmd);
		exit(ret);
	}
	while ((ret = waitpid(pid, &status, 0)) == -1)
		if (errno != EINTR)
			break;
	ret = WEXITSTATUS(status);
	if (ret)
		goto err;
	if (cmd == CMD_CHKPNT) {
		/* Clear VPS network configuration */
		get_vps_ip(h, veid, &iplist);
		run_net_script(veid, DEL, &iplist, STATE_RUNNING,
			vps_p->res.net.skip_arpdetect);
		free_str_param(&iplist);
		vps_umount(h, veid, root, 0);
	}
	ret = 0;
	logger(0, 0, "Checkpointing completed succesfully");
err:
	if (ret) {
		ret = VZ_CHKPNT_ERROR;
		logger(0, 0, "Checkpointing failed");
	}
	if (dump_fd != -1)
		close(dump_fd);
	 close(cpt_fd);

	return ret;
}

static int restrore_FN(vps_handler *h, envid_t veid, int wait_p, int err_p,
	void *data)
{
	int status, len;
	cpt_param *param = (cpt_param *) data;
	char buf[PIPE_BUF];
	int error_pipe[2];

	status = VZ_RESTORE_ERROR;
	if (param == NULL)
		goto err;
	/* Close all fds */
	close_fds(0, wait_p, err_p, h->vzfd, param->rst_fd, -1);
	if (ioctl(param->rst_fd, CPT_SET_VEID, veid) < 0) {
		logger(0, errno, "Can't set VEID %d", param->rst_fd);
		goto err;
	}
	if (pipe(error_pipe) < 0 ) {
		logger(0, errno, "Can't create pipe");
		goto err;
	}
	fcntl(error_pipe[0], F_SETFL, O_NONBLOCK);
	fcntl(error_pipe[1], F_SETFL, O_NONBLOCK);
	if (ioctl(param->rst_fd, CPT_SET_ERRORFD, error_pipe[1]) < 0) {
		logger(0, errno, "Can't set errorfd");
		goto err;
	}
	close(error_pipe[1]);
	if (ioctl(param->rst_fd, CPT_SET_LOCKFD, wait_p) < 0) {
		logger(0, errno, "Can't set lockfd");
		goto err;
	}
	if (ioctl(param->rst_fd, CPT_SET_STATUSFD, STDIN_FILENO) < 0) {
		logger(0, errno, "Can't set statusfd");
		goto err;
	}
        /* Close status descriptor to report that
         * environment is created.
         */
	close(STDIN_FILENO);
	logger(0, 0, "\tundump...");
	if (ioctl(param->rst_fd, CPT_UNDUMP, 0) < 0) {
		logger(0, errno, "Error: undump failed");
		goto err_undump;
	}
	/* Now we wait until VPS setup will be done */
	read(wait_p, &len, sizeof(len));
	if (param->cmd == CMD_RESTORE) {
		logger(0, 0, "\tresume...");
		if (ioctl(param->rst_fd, CPT_RESUME, 0)) {
			logger(0, errno, "Error: resume failed");
			goto err_undump;
		}
	} else if (param->cmd == CMD_UNDUMP && !param->ctx) {
		logger(0, 0, "\tget context...");
		if (ioctl(param->rst_fd, CPT_GET_CONTEXT, veid) < 0) {
			logger(0, 0, "cannot get context");
			goto err_undump;
		}
	}
	status = 0;
err:
	close(error_pipe[0]);
	if (status)
		write(err_p, &status, sizeof(status));
	return status;
err_undump:
	if ((len = read(error_pipe[0], buf, PIPE_BUF)) > 0) {
		logger(0, 0, "Restoring failed:");
		len = write(STDERR_FILENO, buf, len);
		fflush(stderr);	
	}
	close(error_pipe[0]);
	write(err_p, &status, sizeof(status));
	return status;
}

int vps_restore(vps_handler *h, envid_t veid, vps_param *vps_p, int cmd,
	cpt_param *param)
{
	int ret, rst_fd;
	int dump_fd = -1;
	char dumpfile[PATH_LEN];

	if (vps_is_run(h, veid)) {
		logger(0, 0, "Unable to perform restoring VPS already running");
		return VZ_VE_NOT_RUNNING;
	}
	logger(0, 0, "Restoring VPS ...");
	ret = VZ_RESTORE_ERROR;
	if ((rst_fd = open(PROC_RST, O_RDWR)) < 0) {
		if (errno == ENOENT)
			logger(0, errno, "Error: No checkpointing"
				" support, unable to open " PROC_RST);
		else
			logger(0, errno, "Unable to open " PROC_RST);
		return VZ_RESTORE_ERROR;
	}
	if (param->ctx) {
		if (ioctl(rst_fd, CPT_JOIN_CONTEXT, param->ctx) < 0) {
			logger(0, errno, "cannot join cpt context");
			goto err;
		}
	}
	if (param->dumpfile == NULL) {
		if (cmd == CMD_UNDUMP) {
			logger(0, 0, "Error: dumpfile is not specified");
			goto err;
		}

		snprintf(dumpfile, sizeof(dumpfile), "%s/"DEF_DUMPFILE,
			param->dumpdir != NULL ? param->dumpdir : DEF_DUMPDIR,
			veid);
	}
	if (param->dumpfile || !param->ctx) {
		dump_fd = open(param->dumpfile ? : dumpfile, O_RDONLY);
		if (dump_fd < 0) {
			logger(0, errno, "Unable to open %s",
				param->dumpfile ? : dumpfile);
			goto err;
		}
	}
	if (ioctl(rst_fd, CPT_SET_DUMPFD, dump_fd)) {
		logger(0, errno, "Can't set dumpfile");
		goto err;
	}
	param->rst_fd = rst_fd;
	param->cmd = cmd;
	vps_p->res.net.skip_arpdetect = 1;
	ret = vps_start_custom(h, veid, vps_p, SKIP_CONFIGURE, 
		NULL, restrore_FN, param);
	if (ret)
		goto err;
	/* Restore 2level quota links & quota device */
	if ((cmd == CMD_RESTORE || cmd == CMD_UNDUMP) &&
		vps_p->res.dq.ugidlimit != NULL && vps_p->res.dq.ugidlimit)
	{
		logger(0, 0, "Restore 2level quota");
		if (vps_execFn(h, veid, vps_p->res.fs.root, mk_quota_link, NULL,
			VE_SKIPLOCK))
		{
			logger(0, 0, "Warning: restore 2level quota links"
				" failed");
		}
	}
err:
	close(rst_fd);
	if (dump_fd != -1)
		close(dump_fd);
	if (!ret)
		logger(0, 0, "Restoring completed succesfully");
	return ret;
}
