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


static int handle_connection(int fd) {
  for(;;) {
    char sock_type = 0;
    ssize_t recv_len;
    PERROR(==-1, recv_len = recv, fd, &sock_type, 1, 0);

    if (recv_len == 0) {
      break;
    }

    ERROR((sock_type != SOCK_STREAM)&&(sock_type != SOCK_DGRAM), "bad socket type\n");
    int sock_fd = -1;
    PERROR(==-1, sock_fd = socket, AF_INET, sock_type, 0);
    send_fd(fd, sock_fd);
    close(sock_fd);
  }

  close(fd);
  return 0;
}


static int socketd(int listen_fd) {
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
      return handle_connection(fd);
    }

    close(fd);
  }

  return EXIT_FAILURE;
}


int cmd_socketd(int argc, char *const argv[]) {
  opt_name = getenv("USERNS_NAME");

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
  snprintf(socket_path, PATH_MAX, "%s/userns/%s/socketd", rundir, opt_name);
  unlink(socket_path);

  int listen_fd = -1;
  PERROR(==-1, listen_fd = socket, AF_UNIX, SOCK_STREAM, 0);

  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);
  PERROR(==-1, bind, listen_fd, &addr, sizeof(addr));

  VERBOSE("start listening on '%s'\n", socket_path);
  return socketd(listen_fd);
err:
  fprintf(stderr, "Try '%s %s --help'\n", executable, cmd_name);
  exit(EXIT_FAILURE);
}
