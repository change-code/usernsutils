#!/bin/sh

mount -t proc proc /proc

mount -B "${XDG_RUNTIME_DIR}/userns" /mnt
mount -t tmpfs tmpfs /run
mkdir /run/userns
mount -M /mnt /run/userns

mount -t tmpfs tmpfs /tmp

mount -t mqueue mqueue /dev/mqueue
mount -t devpts -o newinstance,gid=0,mode=600 devpts /dev/pts
mount -B /dev/pts/ptmx /dev/ptmx

SRCDIR=$(readlink -f "$(dirname ${BASH_SOURCE})")
ETC_FILES="passwd group"

for filename in ${ETC_FILES}; do
    mount -B "${SRCDIR}/${filename}" "/etc/${filename}"
done

exec env - LANG="${LANG}" PATH="${PATH}" HOME="${HOME}" SHELL="${SHELL}" TERM="${TERM}" USERNS_NAME="${USERNS_NAME}" XDG_RUNTIME_DIR=/run "$@"
