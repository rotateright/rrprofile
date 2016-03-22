#!/bin/sh

###############################################################################
# Environment / Portability
###############################################################################

export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin
umask 022

###############################################################################
# Global Variables
###############################################################################

PRODUCT=Zoom

ROTATERIGHT_DIR="/opt/rotateright"
KMOD_NAME=""

if [ "${UID}" = "" ]; then
	UID=`id -u`
fi

###############################################################################
# Local Functions
###############################################################################

usage()
{
	echo "Usage: $0 [path to product]"
}

###############################################################################
# Main
###############################################################################

if [ -d "${ROTATERIGHT_DIR}/${PRODUCT}" ]; then
	ROTATERIGHT_DIR="${ROTATERIGHT_DIR}/${PRODUCT}"
fi

if [ $# -eq 0 ]; then
	echo "Uninstalling from path: ${ROTATERIGHT_DIR}"
elif [ $# -eq 1 ]; then
	echo "Uninstalling from path: $1"
	ROTATERIGHT_DIR=$1
else
	echo "Error: Wrong number of arguments"
	usage
	exit 2
fi

curr_dir=`basename $PWD`
echo ${curr_dir} | grep rrnotify >/dev/null
rrnotify_ret_val=$?
echo ${curr_dir} | grep rrprofile >/dev/null
rrprofile_ret_val=$?
echo ${curr_dir} | grep oprofile >/dev/null
oprofile_ret_val=$?

if [ $rrnotify_ret_val -eq 0 ]; then
	KMOD_NAME=rrnotify
elif [ $rrprofile_ret_val -eq 0 ]; then
	KMOD_NAME=rrprofile
elif [ $oprofile_ret_val -eq 0 ]; then
	KMOD_NAME=oprofile
else
	echo "Error: Failed to locate driver."
	usage
	exit 2
fi

# Test for root
if [ "$UID" -eq 0 ]; then
	# stop the driver if it is loaded
	if [ -f /dev/${KMOD_NAME}/buffer ]; then
		INIT_DIR=${ROTATERIGHT_DIR}/etc/init.d
		if [ -f ${INIT_DIR}/${KMOD_NAME} ]; then
			${INIT_DIR}/${KMOD_NAME} stop;
			if [ $? -ne 0 ]; then
				echo "Warning: Failed to stop the ${KMOD_NAME} driver."
			else
			    if [ -f /dev/${KMOD_NAME}/buffer ]; then
				echo "Warning: The ${KMOD_NAME} driver is still loaded. Please reboot."
			    else
				echo "Successfully unloaded the ${KMOD_NAME} driver."
			    fi
			fi
		fi
	else
	    echo "Verified that the ${KMOD_NAME} driver is not loaded."
	fi

	# always - in case SELinux status has been changed since install
	rm -f ${ROTATERIGHT_DIR}/etc/init.d/${KMOD_NAME}
	rm -f /etc/init.d/${KMOD_NAME}

	if [ $KMOD_NAME != "oprofile" ]; then
		rm -fr /lib/modules/`uname -r`/extra/${KMOD_NAME}
	fi

	if [ -f /sbin/depmod ]; then
		/sbin/depmod
	fi

	# Always exit successfully
	exit 0
else
	echo "Error: You must be root to uninstall the ${KMOD_NAME} driver."
	exit 2
fi

