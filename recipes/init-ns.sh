#!/bin/sh

mount -t proc proc /proc

mount -B "${XDG_RUNTIME_DIR}/userns" /tmp
mount -t tmpfs tmpfs /run
mkdir /run/userns
mount -B /tmp /run/userns
umount /tmp
mount -t tmpfs tmpfs /tmp

mount -t tmpfs tmpfs /dev
mkdir /dev/mqueue
mount -t mqueue mqueue /dev/mqueue
mkdir /dev/pts
mount -t devpts -o newinstance,gid=0,mode=620 devpts /dev/pts

env - LANG="${LANG}" PATH="${PATH}" HOME="${HOME}" SHELL="${SHELL}" TERM="${TERM}" USERNS_NAME="${USERNS_NAME}" XDG_RUNTIME_DIR=/run "$@"
