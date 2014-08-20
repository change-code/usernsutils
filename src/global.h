#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <netinet/in.h>
#include <pty.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <linux/netfilter_ipv4.h>


#define PERROR(condition, func, ...)    \
  if ( (func(__VA_ARGS__))condition ) { \
    fprintf(stderr,                     \
            "%s: %s\n%s:%d:  %s\n",     \
            executable,                 \
            strerror(errno),            \
            __FILE__,                   \
            __LINE__,                   \
            #func"("#__VA_ARGS__")");   \
    exit(EXIT_FAILURE);                 \
  }                                     \


#define LOG(...)                                        \
  if (cmd_name) {                                       \
    fprintf(stderr, "%s %s: ", executable, cmd_name);   \
  } else {                                              \
    fprintf(stderr, "%s: ", executable);                \
  }                                                     \
  fprintf(stderr, __VA_ARGS__);                         \


#define VERBOSE(...)                            \
  if (opt_verbose) {                            \
    LOG(__VA_ARGS__);                           \
  }                                             \


#define BADOPT(condition, ...)                  \
  if (condition) {                              \
    LOG(__VA_ARGS__);                           \
    goto err;                                   \
  }                                             \


#define ERROR(condition, ...)                   \
  if (condition) {                              \
    LOG(__VA_ARGS__);                           \
    exit(EXIT_FAILURE);                         \
  }                                             \


#define RETRY_ON_INTR(func, ...)       		\
  while ( (func(__VA_ARGS__)) == -1) {		\
    if (errno != EINTR) {			\
      break;					\
    }						\
  }						\


extern char *executable;
extern char *cmd_name;
extern int opt_verbose;


extern int cmd_spawn(int argc, char *const argv[]);
extern int cmd_attach(int argc, char *const argv[]);
extern int cmd_listen(int argc, char *const argv[]);
extern int cmd_connect(int argc, char *const argv[]);
extern int cmd_socketd(int argc, char *const argv[]);
extern int cmd_proxy(int argc, char *const argv[]);


extern void send_fd(int sock_fd, int fd);
extern int recv_fd(int sock_fd);
extern char *const *make_argv(int optind, int argc, char *const argv[]);
