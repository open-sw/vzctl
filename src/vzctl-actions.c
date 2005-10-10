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
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <asm/timex.h>
#include <linux/vzcalluser.h>
#include <errno.h>
#include <signal.h>

#include "vzctl.h"
#include "vzctl_param.h"
#include "env.h"
#include "logger.h"
#include "exec.h"
#include "config.h"
#include "vzerror.h"
#include "create.h"
#include "util.h"
#include "lock.h"
#include "modules.h"

extern struct mod_action g_action;
extern int do_enter(vps_handler *h, envid_t veid, char *root);

int parse_opt(envid_t veid, int argc, char *argv[], struct option *opt,
	vps_param *param)
{
	int c, ret;

	while (1) {
		int option_index = -1;

		c = getopt_long(argc, argv, PARAM_LINE, opt, &option_index);
		if (c == -1)
			break;
		if (c == '?')
			return VZ_INVALID_PARAMETER_VALUE;
		ret = vps_parse_opt(veid, param, c, optarg, &g_action);
		if (!ret)
			continue;
		else if (ret == ERR_INVAL) {
			if (option_index < 0) {
				logger(0, 0, "Bad parameter for"
					" -%c : %s", c, optarg);
				return VZ_INVALID_PARAMETER_VALUE;
			} else {
				logger(0, 0, "Bad parameter for"
					" --%s: %s",
					opt[option_index].name, optarg);
				return VZ_INVALID_PARAMETER_VALUE;
			}
		} else if (ret == ERR_UNK) {
			if (option_index < 0) {
				logger(0, 0, "Invalid option -%c", c);
				return VZ_INVALID_PARAMETER_SYNTAX;
			} else {
				logger(0, 0, "Invalid option --%s",
					opt[option_index].name);
				return VZ_INVALID_PARAMETER_SYNTAX;
			}
                }
	}
	if (optind < argc) {
		printf ("non-option ARGV-elements: ");
		while (optind < argc)
			printf ("%s ", argv[optind++]);
		printf ("\n");
		return VZ_INVALID_PARAMETER_SYNTAX;
        }
	return 0;
}

static int parse_start_opt(int argc, char **argv, vps_opt *opt)
{
	int c, ret = 0;
	struct option start_options[] = {
	{"force", no_argument, NULL, PARAM_FORCE},
	{"skip_ve_setup",no_argument, NULL, PARAM_SKIP_VE_SETUP},

	{ NULL, 0, NULL, 0 }
};

	while (1) {
		c = getopt_long (argc, argv, "", start_options, NULL); 
		if (c == -1)
			break;
		switch (c) {
		case PARAM_FORCE:
			opt->start_force = YES;
			break;
		case PARAM_SKIP_VE_SETUP:
			opt->skip_setup = YES;
			break;
		default:
			ret = VZ_INVALID_PARAMETER_SYNTAX;
			break;
		}
	}
	return ret;
}

static int start(vps_handler *h, envid_t veid, vps_param *g_p,
	vps_param *cmd_p)
{
	int ret;

	if ((ret = check_ub(&g_p->res.ub)))
		return ret;
	if (g_p->opt.start_disabled == YES &&
		cmd_p->opt.start_force != YES)
	{
		logger(0, 0, "VPS start disabled");
		return VZ_VE_START_DISABLED;
	}
	if (vps_is_run(h, veid)) {
		logger(0, 0, "VPS is already running");
		return VZ_VE_RUNNING;
	}
	ret = vps_start(h, veid, g_p,
		cmd_p->opt.skip_setup == YES ? SKIP_SETUP : 0, &g_action);
	return ret;
}

static int parse_stop_opt(int argc, char **argv, vps_opt *opt)
{
	int c, ret = 0;
	struct option stop_options[] = {
	{"fast",	no_argument, NULL, PARAM_FAST},
	{ NULL, 0, NULL, 0 }
};

	while (1) {
		c = getopt_long (argc, argv, "", stop_options, NULL); 
		if (c == -1)
			break;
		switch (c) {
		case PARAM_FAST:
			opt->fast_kill = YES;
			break;
		default		:
			ret = VZ_INVALID_PARAMETER_SYNTAX;
			break;
		}
	}
	return ret;
}

static int stop(vps_handler *h, envid_t veid, vps_param *g_p, vps_param *cmd_p)
{
	int ret;
	int stop_mode;

	if (cmd_p->opt.fast_kill == YES)
		stop_mode = M_KILL;
	else
		stop_mode = M_HALT;
	ret = vps_stop(h, veid, g_p, stop_mode, 0, &g_action);

	return ret;
}

static int restart(vps_handler *h, envid_t veid, vps_param *g_p,
	vps_param *cmd_p)
{
	int ret;

	logger(0, 0, "Restarting VPS");
	if (vps_is_run(h, veid) && (ret = stop(h, veid, g_p, cmd_p)))
		return ret;
	ret = start(h, veid, g_p, cmd_p);

	return ret;
}

static int parse_create_opt(envid_t veid, int argc, char **argv,
	vps_param *param)
{
	int ret;
	struct option *opt;
	struct option create_options[] = {
	{"ostemplate",	required_argument, NULL, PARAM_OSTEMPLATE},
	{"pkgver",      required_argument, NULL, PARAM_PKGVER},
	{"pkgset",      required_argument, NULL, PARAM_PKGSET},
	{"config",	required_argument, NULL, PARAM_CONFIG},
	{"private",	required_argument, NULL, PARAM_PRIVATE},
	{"root",	required_argument, NULL, PARAM_ROOT},
	{"ipadd",	required_argument, NULL, PARAM_IP_ADD},
	{"hostname",	required_argument, NULL, PARAM_HOSTNAME},
	{ NULL, 0, NULL, 0 }
};
	
	opt = mod_make_opt(create_options, &g_action, NULL);
	if (opt == NULL)
		return -1;
	ret = parse_opt(veid, argc, argv, opt, param);
	free(opt);

	return ret;
}

static int create(vps_handler *h, envid_t veid, vps_param *vps_p,
	vps_param *cmd_p)
{
	int ret;

	ret = vps_create(h, veid, vps_p, cmd_p, &g_action);

	return ret;
}

static int destroy(vps_handler *h, envid_t veid, vps_param *g_p,
	vps_param *cmd_p)
{
	return vps_destroy(h, veid, &g_p->res.fs);
}

int check_set_mode(vps_handler *h, envid_t veid, int setmode, int apply,
	vps_res *new_res, vps_res *old_res)
{
	int err = 0;

	/* Check parameters that can't be set on running VE */
	if (new_res->cap.on || new_res->cap.off) {
		logger(0, 0, "Unable to set capability on running VPS");
		if (setmode == SET_RESTART)
			goto restart_ve;
		else if (setmode != SET_IGNORE)
			err = -1;
	}
	if (new_res->env.ipt_mask) {
		if (!old_res->env.ipt_mask ||
			new_res->env.ipt_mask != old_res->env.ipt_mask)
		{
			logger(0, 0, "Unable to set iptables on running VPS");
			if (setmode == SET_RESTART)
				goto restart_ve;
			else if (setmode != SET_IGNORE)
				err = -1;
		}
	}
	if (err && setmode == SET_NONE) {
		logger(0, 0, "WARNING: Some of the parameters"
			" could not be applied to a running VPS.\n"
			"\tPlease consider using --setmode option");
	}
	return err;

restart_ve:
	return 1;
}

static void merge_apply_param(vps_param *old, vps_param *new, char *cfg)
{
#define FREE_STR(x)	if ((x) != NULL) { free(x); x = NULL;}

	FREE_STR(new->res.fs.root_orig)
	FREE_STR(new->res.fs.private_orig)
	FREE_STR(new->res.tmpl.ostmpl)
	FREE_STR(new->res.tmpl.dist)	
	FREE_STR(new->res.misc.hostname)
#undef FREE_STR
	free_str_param(&new->res.net.ip);
        if (new->opt.origin_sample == NULL)
                new->opt.origin_sample = strdup(cfg);
	merge_vps_param(old, new);
}

static int apply_param_from_cfg(int veid, vps_param *param, char *cfg)
{
	char conf[STR_SIZE];
	vps_param *new;

	if (cfg == NULL)
		return 0;
	snprintf(conf, sizeof(conf), VPS_CONF_DIR "ve-%s.conf-sample", cfg);
	if (!stat_file(conf)) {
		logger(0, 0, "Sample config file does not found: %s", conf);
                return VZ_APPLY_CONFIG_ERROR;
	}
	new = init_vps_param();
	vps_parse_config(veid, conf, new, &g_action);
	merge_apply_param(param, new, cfg);
	free_vps_param(new);
	return 0;
}

static int parse_set_opt(envid_t veid, int argc, char *argv[],
	vps_param *param)
{
	int ret;
	struct option *opt;
	
	opt = mod_make_opt(get_set_opt(), &g_action, NULL);
	if (opt == NULL)
		return -1;
	ret = parse_opt(veid, argc, argv, opt, param);
	free(opt);

	return ret;
}

static int set(vps_handler *h, envid_t veid, vps_param *g_p, vps_param *vps_p,
	vps_param *cmd_p)
{
	int ret, is_run;
	dist_actions *actions = NULL;
	char *dist_name;

	ret = 0;

	cmd_p->g_param = g_p;
	if (cmd_p->opt.setmode == SET_RESTART && !cmd_p->opt.save) {
		logger(0, 0, "it's impossible to use"
			" restart mode without --save");
		return VZ_INVALID_PARAMETER_SYNTAX;
	}
	is_run = vps_is_run(h, veid);
	if (is_run) {
		if (cmd_p->res.fs.private_orig != NULL) {
			free(cmd_p->res.fs.private_orig);
			cmd_p->res.fs.private_orig = NULL;
			logger(0, 0,"Unable to change VE_PRIVATE on runing VPS");
			return VZ_VE_RUNNING;
		}
		if (cmd_p->res.fs.root_orig != NULL) {
			free(cmd_p->res.fs.root_orig);
			cmd_p->res.fs.root_orig = NULL;
			logger(0, 0, "Unable to change VE_ROOT on runing VPS");
			return VZ_VE_RUNNING;
		}
	}
	if (need_configure(&cmd_p->res) ||
		need_configure(&cmd_p->del_res) ||
		!list_empty(&cmd_p->res.misc.userpw))
	{
		actions = malloc(sizeof(*actions));
		dist_name = get_dist_name(&g_p->res.tmpl);
		if ((ret = read_dist_actions(dist_name, DIST_DIR, actions)))
                	return ret;
		if (dist_name != NULL)
			free(dist_name);
	}
	/* Setup password */
	if (!list_empty(&cmd_p->res.misc.userpw)) {
		if (!is_run)
			if ((ret = vps_start(h, veid, g_p,
				SKIP_SETUP|SKIP_ACTION_SCRIPT, NULL)))
			{
				goto err;
			}
		ret = vps_pw_configure(h, veid, actions, g_p->res.fs.root,
			&cmd_p->res.misc.userpw);
		if (!is_run)
			vps_stop(h, veid, g_p, M_KILL, SKIP_ACTION_SCRIPT,
				NULL);
		if (ret)
			goto err;
	}
	if (cmd_p->opt.apply_cfg != NULL) {
		if ((ret = apply_param_from_cfg(veid, cmd_p,
			cmd_p->opt.apply_cfg)))
		{
			goto err;
		}
	}
	/* Skip apply parameters on stopped VPS */
	if (cmd_p->opt.save && !is_run) {
		ret = mod_setup(h, veid, STATE_STOPPED, SKIP_NONE, &g_action,
			cmd_p);
		return ret;
	}
	if (is_run) {
		ret = check_set_mode(h, veid, cmd_p->opt.setmode,
			cmd_p->opt.save, &cmd_p->res, &g_p->res);
		if (ret) {
			if (ret < 0) {
				ret = VZ_VE_RUNNING;
				goto err;	
			} else {
				merge_vps_param(g_p, cmd_p);
				ret = restart(h, veid, g_p, cmd_p);
				goto err;
			}
		}
	}
	ret = vps_setup_res(h, veid, actions, &g_p->res.fs, cmd_p,
		STATE_RUNNING, SKIP_NONE, &g_action);
err:
	free_dist_actions(actions);
	if (actions != NULL) free(actions);
	
	return ret;
}

static int mount(vps_handler *h, envid_t veid, vps_param *g_p,
	vps_param *cmd_p)
{
	return vps_mount(h, veid, &g_p->res.fs, &g_p->res.dq, 0);
}

static int umount(vps_handler *h, envid_t veid, vps_param *g_p,
	vps_param *cmd_p)
{
	return vps_umount(h, veid, g_p->res.fs.root, 0);
}

static int enter(vps_handler *h, envid_t veid, vps_param *g_p,
	vps_param *cmd_p)
{
	char *root = g_p->res.fs.root;

	set_log_file(NULL);
	if (check_var(root, "VE_ROOT is not set"))
		return VZ_VE_ROOT_NOTSET;
	if (!vps_is_run(h, veid)) {
		logger(0, 0, "VPS is not running");
		return VZ_VE_NOT_RUNNING;
	}
	do_enter(h, veid, root);
	return 0;
}

static int exec(vps_handler *h, int action, envid_t veid, char *root, int argc,
	char **argv)
{
	int mode;
	char **arg = NULL;
	char *argv_bash[] = {"bash", "-c", NULL, NULL};
	char *buf = NULL;
	int i, ret;
	int len, totallen = 0;

	mode = MODE_BASH;
	if (action == ACTION_EXEC3) {
		arg = argv;
		mode = MODE_EXEC;
	} else if (argc && strcmp(argv[0], "-")) {
                for (i = 0; i < argc; i++) {
                        len = strlen(argv[i]);
                        if (len) {
                                buf = (char*)realloc(buf, totallen + len + 2);
                                sprintf(buf + totallen, "%s ", argv[i]);
                        } else {
                                /* set empty argument */
                                len = 2;
                                buf = (char*)realloc(buf, totallen + len + 2);
                                sprintf(buf + totallen, "\'\' ");
                        }
                        totallen += len + 1;
                        buf[totallen] = 0;
                }
		argv_bash[2] = buf;
		arg = argv_bash;
	}
	ret = vps_exec(h, veid, root, mode, arg, NULL, NULL, 0);

	return ret;
}

static int show_status(vps_handler *h, envid_t veid, vps_param *param)
{
	int exist = 0, mounted = 0, run = 0;
	char buf[STR_SIZE];
	fs_param *fs = &param->res.fs;

	get_vps_conf_path(veid, buf, sizeof(buf));
	if (fs->private != NULL && stat_file(fs->private) && stat_file(buf))
		exist = 1;
	mounted = vps_is_mounted(fs->root);
	run = vps_is_run(h, veid);
	printf("VPSID %d %s %s %s\n", veid,
		exist ? "exist" : "deleted",
		mounted ? "mounted" : "unmounted",
		run ? "running" : "down");
	return 0;
}

static int parse_custom_opt(envid_t veid, int argc, char **argv,
	vps_param *param, const char *name)
{
	int ret;
	struct option *opt;
	
	opt = mod_make_opt(NULL, &g_action, name);
	if (opt == NULL)
		return -1;
	ret = parse_opt(veid, argc, argv, opt, param);
	free(opt);

	return ret;
}

int parse_action_opt(envid_t veid, int action, int argc, char *argv[],
	vps_param *param, const char *name)
{
	int ret = 0;

	switch (action)	{
	case ACTION_SET:
		ret = parse_set_opt(veid, argc, argv, param);
		break;
	case ACTION_CREATE:
		ret = parse_create_opt(veid, argc, argv, param);
		break;
	case ACTION_STOP:
		ret = parse_stop_opt(argc, argv, &param->opt);
		break;
	case ACTION_START:
		ret = parse_start_opt(argc, argv, &param->opt);
		break;
	case ACTION_DESTROY:
		break;
	case ACTION_MOUNT:
		break;
	case ACTION_EXEC:
	case ACTION_EXEC2:
	case ACTION_EXEC3:
    		if (argc < 2) {
			fprintf(stderr, "No command line given for exec\n");
			ret = VZ_INVALID_PARAMETER_SYNTAX;
		}
		break;
	case ACTION_RUNSCRIPT:
    		if (argc < 2) {
			fprintf(stderr, "Script does not specified\n");
			ret = VZ_INVALID_PARAMETER_SYNTAX;
		}
		break;
	case ACTION_CUSTOM:
		ret = parse_custom_opt(veid, argc, argv, param, name);
		break;
	default :
		if ((argc - 1) > 0) {
   	                fprintf (stderr, "Invalid options: ");
		        while (--argc) fprintf(stderr, "%s ", *(++argv));
    			fprintf (stderr, "\n");
	        	ret = VZ_INVALID_PARAMETER_SYNTAX;
		}
    	}
	return ret;
}

int run_action(envid_t veid, int action, vps_param *g_p, vps_param *vps_p,
	vps_param *cmd_p, int argc, char **argv, int skiplock)
{
	vps_handler *h = NULL;
	int ret, lock_id = -1;
	struct sigaction act;
	char fname[STR_SIZE];

	if ((h = vz_open(veid)) == NULL)
		return VZ_BAD_KERNEL;
	if (action != ACTION_EXEC && 
		action != ACTION_EXEC2 &&
		action != ACTION_EXEC3 &&
		action != ACTION_ENTER &&
		action != ACTION_STATUS)
	{
		if (skiplock != YES) {
			lock_id = vps_lock(veid, g_p->opt.lockdir, "");
			if (lock_id > 0) {
				logger(0, 0, "VPS already locked");
				ret = VZ_LOCKED;
				goto err;
			} else if (lock_id < 0) {
				logger(0, 0, "Unable to lock VPS");
				ret = VZ_SYSTEM_ERROR;
				goto err;
			}
		}
		sigemptyset(&act.sa_mask);
		act.sa_handler = SIG_IGN;
		act.sa_flags = 0;
		sigaction(SIGINT, &act, NULL);
	}
        switch (action) {
	case ACTION_CREATE:
		ret = create(h, veid, g_p, cmd_p);
		break;
	case ACTION_DESTROY:
		ret = destroy(h, veid, g_p, cmd_p);
		break;
	case ACTION_MOUNT:
		ret = mount(h, veid, g_p, cmd_p);
		break;
	case ACTION_UMOUNT:
		ret = umount(h, veid, g_p, cmd_p);
		break;
	case ACTION_START:
		ret = start(h, veid, g_p, cmd_p);
		break;
	case ACTION_STOP:
		ret = stop(h, veid, g_p, cmd_p);
		break;
	case ACTION_RESTART:
		ret = restart(h, veid, g_p, cmd_p);
		break;
	case ACTION_SET:
		if (veid == 0) {
			ret = hn_set_cpu(&cmd_p->res.cpu);
			if (cmd_p->opt.save == YES) 
				logger(0, 0, "Warning: --save option"
					" not take affect for VPS0");
			break;
		}
		ret = set(h, veid, g_p, vps_p, cmd_p);
		if (cmd_p->opt.save) {
			get_vps_conf_path(veid, fname, sizeof(fname));
			vps_save_config(veid, fname, cmd_p, vps_p, &g_action);
			logger(0, 0, "Saved parameters for VPS %d", veid);
		} else {
			if (list_empty(&cmd_p->res.misc.userpw)) {
				logger(0, 0, "WARNING: Settings were not saved"
				" and will be resetted to original values on"
				" next start (use --save flag)");
			}
		}
		break;
	case ACTION_STATUS:
		ret = show_status(h, veid, g_p);
		break;
	case ACTION_ENTER:
		ret = enter(h, veid, g_p, cmd_p);
		break;
	case ACTION_EXEC:
	case ACTION_EXEC2:
	case ACTION_EXEC3:
		ret = exec(h, action, veid, g_p->res.fs.root, argc, argv);
		if (ret && action == ACTION_EXEC)
			ret = VZ_COMMAND_EXECUTION_ERROR;
		break;
	case ACTION_RUNSCRIPT:
		ret = vps_run_script(h, veid, argv[0], g_p);
		break;
	case ACTION_CUSTOM:
		ret = mod_setup(h, veid, 0, 0, &g_action, g_p);
		break;
	}
err:
	/* Unlock VPS in case lock taken */
	if (skiplock != YES && !lock_id)
		vps_unlock(veid, g_p->opt.lockdir);
	vz_close(h);
	return ret;
}