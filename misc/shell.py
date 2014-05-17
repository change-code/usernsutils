#!/usr/bin/env python3

import os
import tty
import termios
import fcntl
import pty

STDIN_FILENO = 0
STDOUT_FILENO = 1
STDERR_FILENO = 2



master, slave = os.openpty()
master, slave, os.ttyname(slave)

pid = os.fork()

if pid > 0: # parent
    os.close(slave)

    try:
        mode = tty.tcgetattr(STDIN_FILENO)
        tty.setraw(STDIN_FILENO)
        restore = 1
    except tty.error:
        restore = 0

    try:
        pty._copy(master)
    except (IOError, OSError):
        if restore:
            tty.tcsetattr(STDIN_FILENO, tty.TCSAFLUSH, mode)

    os.close(master)

else: # child
    os.close(master)

    try:
        fd = os.open("/dev/tty", os.O_RDWR | os.O_NOCTTY)
    except OSError:
        pass
    else:
        try:
            fcntl.ioctl(fd, termios.TIOCNOTTY, '')
        except:
            pass
        os.close(fd)

    os.setsid()

    fcntl.ioctl(slave, termios.TIOCSCTTY, '')

    os.dup2(slave, STDIN_FILENO)
    os.dup2(slave, STDOUT_FILENO)
    os.dup2(slave, STDERR_FILENO)
    os.execv("/bin/bash", ["/bin/bash"])
