#define _GNU_SOURCE
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>



#define PERROR(condition, func, ...)		\
  if( (func(__VA_ARGS__))condition) {		\
    perror(#func"("#__VA_ARGS__")");		\
    exit(EXIT_FAILURE);				\
  }						\



#define VERBOSE(...)				\
  if (verbose) {				\
    fprintf(stderr, __VA_ARGS__);		\
  }						\



static int verbose = 0;
static char *socket_path = NULL;
static char *shell = NULL;


static struct option options[] = {
  {"listen",       required_argument, NULL, 'l'},
  {"shell",        required_argument, NULL, 's'},

  {"verbose",      no_argument,       NULL, 'v'},
  {"help",         no_argument,       NULL, 'h'},
  {NULL, no_argument, NULL, 0}
};



void show_usage(char const *name) {
  printf("Usage: %s [options] [--] [command]\n", name);
  printf("\n"
	 "  -l, --listen=PATH          PATH to unix domain socket\n"
	 "  -s, --shell=SHELL          path to shell\n"
	 "\n"
	 "  -v, --verbose              verbose\n"
	 "  -h, --help                 print help message and exit\n"
	 );
  exit(0);
}



void handle_connection(int fd);
void cleanup();
void sigint_handler(int signum);


int main(int argc, char *const argv[]) {
  int opt, index;

  while((opt = getopt_long(argc, argv, "+l:s:hv", options, &index)) != -1) {
    switch(opt) {
    case '?':
      goto err;

    case 'h':
      show_usage(argv[0]);
      break;

    case 'l':
      socket_path = optarg;
      break;

    case 's':
      shell = optarg;
      break;

    case 'v':
      verbose = 1;
      break;

    default:
      break;
    }
  }

  if (!socket_path) {
    fprintf(stderr, "listen path expected\n");
    goto err;
  }

  if (!shell) {
    shell = getenv("SHELL");
  }

  if (!shell) {
    shell = "/bin/bash";
  }

  int listen_fd = -1;

  PERROR(==-1, listen_fd = socket, AF_UNIX, SOCK_STREAM, 0);

  struct sockaddr_un addr = {.sun_family = AF_UNIX};

  if(strlen(socket_path) > sizeof(addr.sun_path) - 1) {
    fprintf(stderr, "socket path too long\n");
    exit(EXIT_FAILURE);
  }

  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);
  PERROR(==-1, bind, listen_fd, &addr, sizeof(addr));

  signal(SIGINT, sigint_handler);
  signal(SIGCHLD, SIG_IGN);

  PERROR(==-1, listen, listen_fd, SOMAXCONN);

  for(;;) {
    int fd = -1;

    struct sockaddr_un peer;
    socklen_t size;
    pid_t pid;

    PERROR(==-1, fd = accept, listen_fd, &peer, &size);
    PERROR(==-1, pid = fork);

    if (pid == 0) {
      close(listen_fd);
      signal(SIGINT, SIG_DFL);
      signal(SIGCHLD, SIG_DFL);
      handle_connection(fd);
      break;
    }

    close(fd);
  }

  exit(EXIT_FAILURE);
err:
  fprintf(stderr, "Try '%s --help'\n", argv[0]);
  exit(EXIT_FAILURE);
}



void cleanup(){
  VERBOSE("unlinking '%s'\n", socket_path);
  unlink(socket_path);
}


void sigint_handler(int signum) {
  cleanup();

  signal(SIGINT, SIG_DFL);
  raise(SIGINT);
}


void handle_connection(int fd) {
  char control[CMSG_SPACE(sizeof(int))];
  char n = 0;

  struct iovec iov = {.iov_base = &n, .iov_len = 1};
  struct msghdr msg = {
    .msg_name = NULL,
    .msg_namelen = 0,
    .msg_iov = &iov,
    .msg_iovlen = 1,
    .msg_control = control,
    .msg_controllen = CMSG_LEN(sizeof(int)),
    .msg_flags = 0,
  };
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  *((int *)CMSG_DATA(cmsg)) = -1;

  PERROR(==-1, recvmsg, fd, &msg, 0);

  cmsg = CMSG_FIRSTHDR(&msg);

  if (cmsg==NULL ||
      cmsg->cmsg_level != SOL_SOCKET ||
      cmsg->cmsg_type != SCM_RIGHTS) {
    fprintf(stderr, "cmsg: bad message!\n");
    exit(EXIT_FAILURE);
  }

  int slave = *((int *)CMSG_DATA(cmsg));
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

      printf("EXIT pid=%d status=%d\n", child_pid, code);
      break;
    }
    return;
  }

  int close_fds[3] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};

  int i;
  for(i=0; i<3; i++) {
    close(close_fds[i]);
    dup2(slave, close_fds[i]);
  }
  close(slave);

  char *const argv[] = {shell, NULL};
  PERROR(==-1, execvp, shell, argv);
}
