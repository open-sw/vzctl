#!/bin/bash
# Copyright (C) 2000-2005 SWsoft. All rights reserved.
#
# This file may be distributed under the terms of the Q Public License
# as defined by Trolltech AS of Norway and appearing in the file
# LICENSE.QPL included in the packaging of this file.
#
# This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
# WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
#
# This script configure IP alias(es) inside RedHat like VE.
#
# Parameters are passed in environment variables.
# Required parameters:
#   IP_ADDR       - IP address(es) to add
#                   (several addresses should be divided by space)
# Optional parameters:
#   VE_STATE      - state of VE; could be one of:
#                     starting | stopping | running | stopped
#   IPDELALL	  - delete all old interfaces
#
VENET_DEV=venet0
VENET_DEV_CFG=ifcfg-$VENET_DEV

FAKEGATEWAY=191.255.255.1
FAKEGATEWAYNET=191.255.255.0

IFCFG_DIR=/etc/sysconfig/network-scripts
IFCFG=${IFCFG_DIR}/ifcfg-${VENET_DEV}
NETFILE=/etc/sysconfig/network
HOSTFILE=/etc/hosts

function fix_ifup()
{
	file="/sbin/ifup"

	[ -f "${file}" ] || return 0
	[ "x${VE_STATE}" != "xstarting" ] && return 0

	if grep -q 'if \[ "\${DEVICE}" = "lo" \]; then' ${file} 2>/dev/null
	then
		cp -fp ${file} ${file}.$$ || return 1
		/bin/sed -e 's/if \[ "\${DEVICE}" = "lo" \]; then/if \[ "${IPADDR}" = "127.0.0.1" \]; then/g' < ${file} > ${file}.$$ && \
			mv -f ${file}.$$ ${file}
		rm -f ${file}.$$ 2>/dev/null
	fi
}

function setup_network()
{
	local routefile=/etc/sysconfig/network-scripts/route-${VENET_DEV}

	# Set up venet0 main interface as 127.0.0.1
	mkdir -p ${IFCFG_DIR}
	echo "DEVICE=${VENET_DEV}
ONBOOT=yes
IPADDR=127.0.0.1
NETMASK=255.255.255.0
NETWORK=${FAKEGATEWAYNET}
BROADCAST=0.0.0.0" > $IFCFG || error "Can't write to file $IFCFG" $VZ_FS_NO_DISK_SPACE

	# Set /etc/sysconfig/network
	put_param $NETFILE NETWORKING yes
	put_param $NETFILE GATEWAY ${FAKEGATEWAY}

	# Set up /etc/hosts
	if [ ! -f ${HOSTFILE} ]; then
		echo "127.0.0.1 localhost.localdomain localhost" > $HOSTFILE
	fi
	fix_ifup
}

function create_config()
{
	local ip=$1
	local ifnum=$2

	echo "DEVICE=${VENET_DEV}:${ifnum}
IPADDR=${ip}
NETMASK=255.255.255.255" > ${IFCFG_DIR}/bak/${VENET_DEV_CFG}:${ifnum} || \
	error "Unable to create interface config file" ${VZ_FS_NO_DISK_SPACE}
}

function get_all_aliasid()
{
	IFNUM=-1
	
	cd ${IFCFG_DIR} || return 1
	IFNUMLIST=`ls -1 bak/${VENET_DEV_CFG}:* 2>/dev/null | \
		sed "s/.*${VENET_DEV_CFG}://"`
}

function get_aliasid_by_ip()
{
	local ip=$1
	local idlist

	cd ${IFCFG_DIR} || return 1
	IFNUM=`grep -l "IPADDR=${ip}$" ${VENET_DEV_CFG}:* | head -n 1 | \
		sed -e 's/.*:\([0-9]*\)$/\1/'`
}

function get_free_aliasid()
{
	local found=

	[ -z "${IFNUMLIST}" ] && get_all_aliasid
	while test -z ${found}; do
		let IFNUM=IFNUM+1
		echo "${IFNUMLIST}" | grep -q -E "^${IFNUM}$" 2>/dev/null || \
			found=1
	done
}

function backup_configs()
{
	local delall=$1

	rm -rf ${IFCFG_DIR}/bak/ >/dev/null 2>&1
	mkdir -p ${IFCFG_DIR}/bak
	[ -n "${delall}" ] && return 0

	cd ${IFCFG_DIR} || return 1
	if ls ${VENET_DEV_CFG}:* > /dev/null 2>&1; then
		cp -rf ${VENET_DEV_CFG}:* ${IFCFG_DIR}/bak/ || \
			error "Unable to backup intrface config files" ${VZ_FS_NO_DISK_SPACE}
	fi
}

function move_configs()
{
	cd ${IFCFG_DIR} || return 1
	rm -rf ${VENET_DEV_CFG}:*
	mv -f bak/* ${IFCFG_DIR}/ >/dev/null 2>&1 
	rm -rf ${IFCFG_DIR}/bak
}

function add_ip()
{
	local ip
	local new_ips 

	# In case we are starting VE
	if [ "x${VE_STATE}" = "xstarting" ]; then
		# Remove all VENET config files
		rm -f ${IFCFG_DIR}/${VENET_DEV_CFG}:* >/dev/null 2>&1
		setup_network
	fi
	backup_configs ${IPDELALL}
	new_ips="${IP_ADDR}"
	if [ "x${IPDELALL}" = "xyes" ]; then
		new_ips=
		for ip in ${IP_ADDR}; do
			get_aliasid_by_ip "${ip}"
			if [ -n "${IFNUM}" ]; then
				# ip already exists just create it in bak
				create_config "${ip}" "${IFNUM}"
			else
				new_ips="${new_ips} ${ip}"
			fi
		done
	fi
	for ip in ${new_ips}; do
		get_free_aliasid
		create_config "${ip}" "${IFNUM}"
	done
	move_configs
	if [ "x${VE_STATE}" = "xrunning" ]; then
		# synchronyze config files & interfaces
		cd /etc/sysconfig/network-scripts && ./ifup-aliases ${VENET_DEV}
	fi
}

add_ip
exit 0
# end of script