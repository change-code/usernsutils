#!/usr/bin/env bash

HERE="$(dirname $(readlink -f ${BASH_SOURCE[0]}))"

FILE_BINDS=()
FILE_COUNT=0

bind_mount() {
  local source=$(readlink -f "$1")
  if [ ! -e "$2" ]; then
    if [ -d "${source}" ]; then
      mkdir -p "$2"
    else
      mkdir -p $(dirname "$2")
      touch "$2"
    fi
  fi
  mount -B "${source}" "$2"
}

bind_begin() {
  BIND_DIR="$1"
  mount -t tmpfs tmpfs "$1"
}

bind_file() {
  case $# in
    1)
      cat > "${BIND_DIR}/${FILE_COUNT}"
    ;;
    2)
      bind_mount "$2" "${BIND_DIR}/${FILE_COUNT}"
    ;;
    *)
      return 1
    ;;
  esac

  FILE_BINDS[FILE_COUNT]="$1"
  ((FILE_COUNT+=1))
}

bind_end() {
  local i=0
  for ((i=0;i<FILE_COUNT;i++)); do
    bind_mount "${BIND_DIR}/${i}" "${FILE_BINDS[i]}"
  done
}

reset_env() {
  export -n $(export -p | grep -Po '(?<=^declare -x )[^=]+')
  export $@
}

bind_begin /mnt

bind_file /run/userns       "${XDG_RUNTIME_DIR}/userns"

bind_file /etc/passwd       <<EOF
root:x:0:0:tty:/root:/bin/bash
EOF

bind_file /etc/group        <<EOF
tty:x:0:root
EOF

bind_file /etc/hostname     <<EOF
${USERNS_NAME}.${USERNS_DOMAIN}
EOF

bind_file /etc/hosts        <<EOF
127.0.0.1 ${USERNS_NAME}.${USERNS_DOMAIN} ${USERNS_NAME}
EOF

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t tmpfs tmpfs /run
mount -t tmpfs tmpfs /tmp
mount -t mqueue mqueue /dev/mqueue
mount -t devpts -o newinstance,gid=0,mode=600 devpts /dev/pts
mount -B /dev/pts/ptmx /dev/ptmx

bind_end

hostname -F /etc/hostname

reset_env                          \
  USERNS_NAME="${USERNS_NAME}"     \
  USERNS_DOMAIN="${USERNS_DOMAIN}" \
  XDG_RUNTIME_DIR=/run             \
  LANG="${LANG}"                   \
  PATH="${PATH}"                   \
  HOME="${HOME}"                   \
  SHELL="${SHELL}"                 \
  TERM="${TERM}"                   \

exec "$@"
