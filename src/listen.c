#include "global.h"


static struct option options[] = {
  {"help",         no_argument,       NULL, 'h'},

  {NULL,           no_argument,       NULL, 0}
};


static void show_usage() {
  printf("Usage: %s %s [options] [--] [command]\n", executable, cmd_name);
  printf("\n"
	 "  -h, --help                 print help message and exit\n"
	 );
  exit(0);
}


static int handle_connection(int fd, char *const argv[]) {
  int slave = recv_fd(fd);
  close(fd);

  int ttyfd = open("/dev/tty", O_RDWR | O_NOCTTY);
  if (ttyfd != -1) {
    PERROR(==-1, ioctl, ttyfd, TIOCNOTTY);
    close(ttyfd);
  }

  PERROR(==-1, setsid);
  PERROR(==-1, ioctl, slave, TIOCSCTTY);

  pid_t pid;
  PERROR(==-1, pid = fork);

  if (pid > 0) {
    VERBOSE("SPAWN pid=%d\n", pid);
    close(slave);

    for(;;) {
      int status = -1;
      pid_t child_pid = waitpid(pid, &status, 0);

      if (WIFSTOPPED(status)) {
        continue;
      }

      int code = 1;

      if(WIFSIGNALED(status)) {
        code = WTERMSIG(status)+128;
      } else {
        code = WEXITSTATUS(status);
      }

      VERBOSE("EXIT pid=%d status=%d\n", child_pid, code);
      return code;
    }
    return 0;
  }

  int close_fds[3] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};

  for(int i=0; i<(sizeof(close_fds)/sizeof(int)); i++) {
    close(close_fds[i]);
    dup2(slave, close_fds[i]);
  }
  close(slave);

  PERROR(==-1, execvp, argv[0], argv);
  return EXIT_FAILURE;
}


static int do_listen(int listen_fd, char *const argv[]) {
  PERROR(==-1, listen, listen_fd, SOMAXCONN);

  for(;;) {
    int fd = -1;

    struct sockaddr_un peer;
    socklen_t size = sizeof(struct sockaddr_un);
    pid_t pid;

    PERROR(==-1, fd = accept, listen_fd, &peer, &size);
    PERROR(==-1, pid = fork);

    if (pid == 0) {
      close(listen_fd);
      return handle_connection(fd, argv);
    }

    close(fd);
  }

  return 0;
}


int cmd_listen(int argc, char *const argv[]) {
  int opt, index;

  while((opt = getopt_long(argc, argv, "+h", options, &index)) != -1) {
    switch(opt) {
    case '?':
      goto err;

    case 'h':
      show_usage();
      break;

    default:
      break;
    }
  }

  char *rundir = getenv("XDG_RUNTIME_DIR");
  ERROR(!rundir, "environment XDG_RUNTIME_DIR is not set\n");

  char *name = getenv("USERNS_NAME");
  ERROR(!name, "running outside a user namespace\n");

  char socket_path[PATH_MAX] = {0};
  snprintf(socket_path, PATH_MAX, "%s/userns/%s/nshd", rundir, name);
  unlink(socket_path);

  int listen_fd = -1;
  PERROR(==-1, listen_fd = socket, AF_UNIX, SOCK_STREAM, 0);

  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);
  PERROR(==-1, bind, listen_fd, &addr, sizeof(addr));

  VERBOSE("start listening on '%s'\n", socket_path);
  return do_listen(listen_fd, make_argv(optind, argc, argv));
err:
  fprintf(stderr, "Try '%s %s --help'\n", executable, cmd_name);
  exit(EXIT_FAILURE);
}
