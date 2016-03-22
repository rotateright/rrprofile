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
# Main
###############################################################################

if [ -d "${ROTATERIGHT_DIR}/${PRODUCT}" ]; then
	ROTATERIGHT_DIR="${ROTATERIGHT_DIR}/${PRODUCT}"
fi

if [ $# -eq 0 ]; then
	echo "Installing to path: ${ROTATERIGHT_DIR}"
elif [ $# -eq 1 ]; then
	echo "Installing to path: $1"
	ROTATERIGHT_DIR=$1
else
	echo "Error: Wrong number of arguments"
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
	exit 2
fi

# Test for root
if [ "$UID" -eq 0 ]; then
	if [ $KMOD_NAME != "oprofile" ]; then
		# Check if kmod is built
		if [ ! -f ${KMOD_NAME}.ko ]; then 
			echo "Error: ${KMOD_NAME}.ko is not found."
			exit 2
		fi
		rm -rf /lib/modules/`uname -r`/extra/${KMOD_NAME}
		rm -f /lib/modules/`uname -r`/extra/${KMOD_NAME}.ko

		# Install
		mkdir -p /lib/modules/`uname -r`/extra/${KMOD_NAME}
		cp ${KMOD_NAME}.ko /lib/modules/`uname -r`/extra/${KMOD_NAME}/${KMOD_NAME}.ko
	fi
	/sbin/depmod

	# Install startup scripts into ${ROTATERIGHT_DIR}
	INIT_DIR=${ROTATERIGHT_DIR}/etc/init.d
	mkdir -p ${INIT_DIR}
	cp common/${KMOD_NAME}-generic ${INIT_DIR}/${KMOD_NAME}
	${INIT_DIR}/${KMOD_NAME} status 2&> /dev/null
	if [ $? -eq 0 ]; then
		${INIT_DIR}/${KMOD_NAME} stop
		sleep 1
	fi
#	${INIT_DIR}/${KMOD_NAME} start
#	if [ $? -ne 0 ]; then
#		echo "Error: Failed to start ${KMOD_NAME}."
#		exit 2
#	fi
else
	echo "Error: You must be root to install the ${KMOD_NAME} driver."
	exit 2
fi
