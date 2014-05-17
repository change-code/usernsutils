#!/usr/bin/env python3

import os
import sys
import socket
import tty
import pty
import array
import signal
import termios
import fcntl

STDIN_FILENO = 0
STDOUT_FILENO = 1
STDERR_FILENO = 2


def main(path):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(path)

    master, slave = os.openpty()
    fds = [slave, slave, slave]
    s.sendmsg(
        [b'\x00'],
        [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", fds))])
    os.close(slave)
    s.close()

    def setwinsz():
        win = array.array('h', [0, 0, 0, 0])
        fcntl.ioctl(STDOUT_FILENO, termios.TIOCGWINSZ, win)
        fcntl.ioctl(master, termios.TIOCSWINSZ, win)

    setwinsz()

    try:
        mode = tty.tcgetattr(STDIN_FILENO)
        tty.setraw(STDIN_FILENO)
        restore = 1
    except tty.error:
        restore = 0

    def sigwinch(signum, frame):
        setwinsz()

    signal.signal(signal.SIGWINCH, sigwinch)

    try:
        pty._copy(master)
    except (IOError, OSError):
        if restore:
            tty.tcsetattr(STDIN_FILENO, tty.TCSAFLUSH, mode)

    os.close(master)


if __name__ == '__main__':
    main(sys.argv[1])
