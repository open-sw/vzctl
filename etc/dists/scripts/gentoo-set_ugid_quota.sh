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
# This script configures quota startup script inside VPS
#
# Parameters are passed in environment variables.
# Required parameters:
#   MINOR	- root device minor number 
#   MAJOR	- root device major number
SCRIPTANAME='/etc/init.d/vzquota'
DEFAULT="/etc/runlevels/default/vzquota"

if [ -z "$MAJOR" ]; then
	rm -f ${SCRIPTANAME} > /dev/null 2>&1
	rm -f ${DEFAULT} > /dev/null 2>&1
	rm -f /etc/mtab > /dev/null 2>&1
	ln -sf /proc/mounts /etc/mtab
	exit 0
fi

echo -e '#!/sbin/runscript

start() {
        rm -f /dev/simfs
        mknod /dev/simfs b 130 64
        rm -f /etc/mtab >/dev/null 2>&1
        echo "/dev/simfs / reiserfs rw,usrquota,grpquota 0 0" > /etc/mtab
        mnt=`grep -v " / " /proc/mounts`
        if [ $? == 0 ]; then
                echo "$mnt" >> /etc/mtab
        fi
	quotaon -aug
        return
}

stop() {
        return
}

' > ${SCRIPTANAME} || {
	echo "Unable to create ${SCRIPTNAME}"
	exit 1
}
chmod 755 ${SCRIPTANAME}

ln -sf ${SCRIPTANAME} ${DEFAULT}

exit 0