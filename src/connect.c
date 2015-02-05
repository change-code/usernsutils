#include "global.h"


static char *opt_name = NULL;


static struct option options[] = {
  {"name",         required_argument, NULL, 'n'},
  {"help",         no_argument,       NULL, 'h'},

  {NULL,           no_argument,       NULL, 0}
};


static void show_usage() {
  printf("Usage: %s %s [options] [--] [command]\n", executable, cmd_name);
  printf("\n"
         "  -n, --name=NAME            namespace to connect"
         "\n"
	 "  -h, --help                 print help message and exit\n"
	 );
  exit(0);
}


static void makeraw(struct termios *mode) {
  mode->c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  mode->c_oflag &= ~OPOST;
  mode->c_cflag &= ~(CSIZE | PARENB);
  mode->c_cflag |= CS8;
  mode->c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  mode->c_cc[VMIN] = 1;
  mode->c_cc[VTIME] = 0;
}


static int do_connect(int fd) {
  int master, slave;
  PERROR(==-1, openpty, &master, &slave, NULL, NULL, NULL);

  send_fd(fd, slave);
  close(slave);
  close(fd);

  void setwinsz() {
    struct winsize win;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &win);
    ioctl(master, TIOCSWINSZ, &win);
  }

  setwinsz();

  struct termios old_mode;
  PERROR(==-1, tcgetattr, STDIN_FILENO, &old_mode);

  struct termios mode;
  PERROR(==-1, tcgetattr, STDIN_FILENO, &mode);
  makeraw(&mode);
  PERROR(==-1, tcsetattr, STDIN_FILENO, TCSAFLUSH, &mode);

  void sigwinch_handler(int signum __attribute__((unused))) {
    setwinsz();
  }

  signal(SIGWINCH, sigwinch_handler);

  void resettty() {
    PERROR(==-1, tcsetattr, STDIN_FILENO, TCSAFLUSH, &old_mode);
    signal(SIGWINCH, SIG_DFL);
  }
  atexit(resettty);

  for(;;) {
    fd_set rfds;
    struct timeval tv;
    char buffer[1024];
    int nfd;
    ssize_t read_count;
    ssize_t write_count;

    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    FD_SET(master, &rfds);
    tv.tv_sec = 5;
    tv.tv_usec = 0;

    RETRY_ON_INTR(nfd = select, master+1, &rfds, NULL, NULL, &tv);
    if (nfd == -1) {
      perror("select");
      exit(EXIT_FAILURE);
    }

    if (FD_ISSET(STDIN_FILENO, &rfds)) {
      RETRY_ON_INTR(read_count = read, STDIN_FILENO, buffer, 1024);

      if (read_count == -1) {
        close(master);

        if (errno != EIO) {
          perror("read from stdin");
        }
        break;
      }

      while(read_count) {
        RETRY_ON_INTR(write_count = write, master, buffer, read_count);

        if (write_count == -1) {
          if (errno != EIO) {
            perror("write to master");
          }
          break;
        }

        read_count -= write_count;
      }
    }

    if (FD_ISSET(master, &rfds)) {
      RETRY_ON_INTR(read_count = read, master, buffer, 1024);

      if (read_count == -1) {
        if (errno != EIO) {
          perror("read from master");
        }
        break;
      }

      while(read_count) {
        RETRY_ON_INTR(write_count = write, STDOUT_FILENO, buffer, read_count);

        if (write_count == -1) {
          close(master);

          if (errno != EIO) {
            perror("write to stdout");
          }
          break;
        }

        read_count -= write_count;
      }
    }
  }

  return EXIT_FAILURE;
}


int cmd_connect(int argc, char *const argv[]) {
  int opt, index;

  while((opt = getopt_long(argc, argv, "+n:h", options, &index)) != -1) {
    switch(opt) {
    case '?':
      goto err;

    case 'h':
      show_usage();
      break;

    case 'n':
      opt_name = optarg;
      break;

    default:
      break;
    }
  }

  BADOPT(!opt_name, "missing name\n");

  char *rundir = getenv("XDG_RUNTIME_DIR");
  ERROR(!rundir, "environment XDG_RUNTIME_DIR is not set\n");

  char socket_path[PATH_MAX] = {0};
  snprintf(socket_path, PATH_MAX, "%s/userns/%s/nshd", rundir, opt_name);

  int fd = -1;
  PERROR(==-1, fd = socket, AF_UNIX, SOCK_STREAM, 0);

  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);

  PERROR(==-1, connect, fd, &addr, sizeof(addr));

  return do_connect(fd);
err:
  fprintf(stderr, "Try '%s %s --help'\n", executable, cmd_name);
  exit(EXIT_FAILURE);
}
