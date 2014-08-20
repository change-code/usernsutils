#include "global.h"


void send_fd(int sock_fd, int fd) {
  size_t controllen = sizeof(int);
  char control[CMSG_SPACE(sizeof(int))];
  char n = 0;

  struct iovec iov = {.iov_base = &n, .iov_len = 1};
  struct msghdr msg = {
    .msg_name = NULL,
    .msg_namelen = 0,
    .msg_iov = &iov,
    .msg_iovlen = 1,
    .msg_control = control,
    .msg_controllen = CMSG_LEN(controllen),
    .msg_flags = 0,
  };

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = msg.msg_controllen;

  int fds[1] = {fd};
  memcpy((int *) CMSG_DATA(cmsg), fds, controllen);
  PERROR(==-1, sendmsg, sock_fd, &msg, 0);
}


int recv_fd(int sock_fd) {
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
  int *pfd = (int *)CMSG_DATA(cmsg);
  *pfd = -1;

  PERROR(==-1, recvmsg, sock_fd, &msg, 0);

  cmsg = CMSG_FIRSTHDR(&msg);

  ERROR((cmsg==NULL) ||
        (cmsg->cmsg_level != SOL_SOCKET) ||
        (cmsg->cmsg_type != SCM_RIGHTS),
        "cmsg: bad message!\n");

  return *pfd;
}


char *const *make_argv(int optind, int argc, char *const argv[]) {
  if (optind >= argc) {
    char *shell = getenv("SHELL");
    char **arg = malloc(sizeof(char const*) * 2);
    arg[0] = shell?shell:"/bin/sh";
    arg[1] = NULL;
    return arg;
  } else {
    return argv + optind;
  }
}
