#!/bin/sh

mount -t mqueue mqueue /dev/mqueue
mount -t tmpfs tmpfs /run
mount -t tmpfs tmpfs /tmp
mount -t devpts -o newinstance,gid=0,mode=620 devpts /dev/pts
