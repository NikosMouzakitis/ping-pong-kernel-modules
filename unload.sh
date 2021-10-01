#!/bin/bash
make clean

rmmod server.ko
rmmod client.ko

dmesg -C

tee /var/log/kern.log < /dev/null
tee /var/log/syslog < /dev/null
