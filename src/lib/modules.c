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
#include <string.h>
#include <getopt.h>
#include <stdio.h>

#include "modules.h"

int mod_parse(envid_t veid, struct mod_action *action, const char *name,
	int opt, const char *rval)
{
	int i, ret;
	struct mod *mod;
	struct mod_info *mod_info;

	if (action == NULL)
		return 0;
	if (name == NULL)
		ret = ERR_UNK;	// unknown option 
	else
		ret = 0;	// skip unknown parameters in config
	for (i = 0, mod = action->mod_list; i < action->mod_count; i++, mod++) {
		mod_info = mod->mod_info;
		if (mod_info == NULL)
			continue;
		if (name != NULL && mod_info->parse_cfg != NULL) {
			if ((ret = mod_info->parse_cfg(veid, mod->data, name,
				 rval)))
			{
				return ret;
			}
		} else if (mod_info->parse_opt != NULL) {
			if ((ret = mod_info->parse_opt(veid, mod->data, opt,
				rval)))
			{
				return ret;
			}
		}
	}
	return ret;
}

static inline int opt_size(struct option *opt)
{
	int i;

	if (opt == NULL)
		return 0;
	for (i = 0; opt[i].name != NULL; i++);

	return i;	
}

struct option *mod_make_opt(struct option *opt, struct mod_action *action,
	const char *name)
{
	int size, total, i;
	struct option *mod_opt, *new = NULL;
	struct mod *mod;
	struct mod_info *mod_info;

	total = opt_size(opt);
	if (total) {
		new = malloc(sizeof(*new) * (total + 1));
		memcpy(new, opt, sizeof(*new) * total);
	}
	if (action == NULL) {
		if (new != NULL)
			memset(new + total, 0, sizeof(*new));
		return new;
	}
	for (i = 0, mod = action->mod_list; i < action->mod_count; i++, mod++) {
		mod_info = mod->mod_info;
		if (mod_info == NULL || mod_info->get_opt == NULL)
			continue;
		mod_opt = mod_info->get_opt(mod->data, name);
		size = opt_size(mod_opt);
		if (!size)
			continue;
		new = realloc(new, sizeof(*new) * (total + size + 1));
		memcpy(new + total, mod_opt, sizeof(*new) * size);
		total += size;
	}
	if (new != NULL)
		memset(new + total, 0, sizeof(*new));
	return new;
}

int mod_save_config(struct mod_action *action, list_head_t *conf)
{
	int i, ret;
	struct mod *mod;
	struct mod_info *mod_info;

	if (action == NULL)
		return 0;
	for (i = 0, mod = action->mod_list; i < action->mod_count; i++, mod++) {
		mod_info = mod->mod_info;
		if (mod_info == NULL || mod_info->store == NULL)
			continue;
		ret = mod_info->store(mod->data, conf);	
	}
	return 0;
}

int mod_setup(vps_handler *h, envid_t veid, int vps_state, skipFlags skip,
	struct mod_action *action, vps_param *param)
{
	int i, ret;
	struct mod *mod;
	struct mod_info *mod_info;

	if (action == NULL)
		return 0;
	for (i = 0, mod = action->mod_list; i < action->mod_count; i++, mod++) {
		mod_info = mod->mod_info;
		if (mod_info == NULL || mod_info->setup == NULL)
			continue;
		ret = mod_info->setup(h, veid, mod->data, vps_state, skip,
			param);
		if (ret)
			return ret;
	}
	return 0;
}

int mod_cleanup(vps_handler *h, envid_t veid, struct mod_action *action,
	vps_param *param)
{
	int i, ret;
	struct mod *mod;
	struct mod_info *mod_info;

	if (action == NULL)
		return 0;
	for (i = 0, mod = action->mod_list; i < action->mod_count; i++, mod++) {
		mod_info = mod->mod_info;
		if (mod_info == NULL || mod_info->cleanup == NULL)
			continue;
		ret = mod_info->cleanup(h, veid, mod->data, param);
	}
	return 0;
}

int is_mod_action(struct mod_info *info, char *name)
{
	char **p;

	if (info == NULL || info->actions == NULL)
		return 0;
	if (name == NULL)
		return 1;
	for (p = info->actions; *p != NULL; p++) 
		if (!strcmp(*p, name))
			return 1;
	return 0;
}

void mod_print_usage(struct mod_action *action)
{
	int i;
	struct mod *mod;
	struct mod_info *mod_info;
	const char *usage;

	if (action == NULL)
		return;
	for (i = 0, mod = action->mod_list; i < action->mod_count; i++, mod++) {
		mod_info = mod->mod_info;
		if (mod_info == NULL || mod_info->get_usage == NULL)
			continue;
		usage = mod_info->get_usage();
		if (usage != NULL)
			fprintf(stdout, "%s", usage);
	}
	return;

}