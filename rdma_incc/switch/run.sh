#!/bin/bash

. $(dirname $0)/utils.sh

set -e 

modules="bf_kdrv|bf_knet|bf_kpkt"

if ! lsmod | grep -Eq "^($modules)\b"; then
  echo "Error: none of [${modules//|/, }] is loaded" >&2
  echo "To load the driver, run:"
  echo '$SDE_INSTALL/bin/bf_kdrv_mod_load $SDE_INSTALL'
  exit 1
fi


if ( [ $# -ne 2 ] && [ $# -ne 1 ] ) || [ $1 == "-h" ] || [ $1 == "--help" ];
then 
	echo_e "Usage: run.sh <P4_PROGRAM_NAME> <BFRT_PRELOAD_FILE>"
	exit 1
fi


DIR=`cd $(dirname $0); pwd`
PROGRAM=$1

if [ $# -eq 2 ]; then
    if ! [ -f "$2" ]; then
        echo_e "Controller file does not exist."
        exit 1
    fi
    BFRT_PRELOAD_FILE=`cd $(dirname $2); pwd`/$(basename $2)
fi



echo_i "Find and kill previous process."

$DIR/kill.sh $PROGRAM

sleep 0.1

set +e
check_port=`netstat -tuln | grep :7777`
set -e

if [ -n "`pgrep bf_switchd`" ] || [ -n "$check_port" ]; then 
    echo_e "Switch is being used by another program."
    exit 1
fi

echo_i "Boot switch in the background."

echo_r "$SDE/run_switchd.sh -p $PROGRAM > /tmp/switchd.log 2>&1 &"

set +e
for i in {1..10}; do 
    sleep 1
    kill -0 $! >/dev/null 2>&1 # check existence
    retval=$?
    if [ "$retval" -ne "0" ]; then
        echo_e "Error on run_switchd.sh"
        cat /tmp/switchd.log
        exit 1
    fi
done
set -e

if [ $# -eq 2 ]; then
    echo_i "Boot controller... "
    echo_r "$SDE/run_bfshell.sh -b $BFRT_PRELOAD_FILE > /tmp/bfrt.log 2>&1"
    retval=$?
    if [ "$retval" -ne "0" ]; then
        echo_e "Error on run_bfshell.sh"
        cat /tmp/bfrt.log
        exit 1
    fi
fi 

echo_i "Done."


