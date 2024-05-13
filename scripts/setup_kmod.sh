#!/bin/bash
# run with sudo

# set up the ksched module
rmmod ksched
rm /dev/ksched

if [[ "$1x" = "nouintrx" ]]; then
  insmod $(dirname $0)/../ksched/build/ksched.ko nouintr=1
else
  insmod $(dirname $0)/../ksched/build/ksched.ko
fi

mknod /dev/ksched c 280 0
chmod uga+rwx /dev/ksched