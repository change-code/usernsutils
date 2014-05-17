#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
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
static int flags = 0;
static char *hostname = NULL;

#define OPT_HOSTNAME 1



static struct option options[] = {
  {"hostname",     required_argument, NULL, OPT_HOSTNAME},

  {"verbose",      no_argument,       NULL, 'v'},
  {"help",         no_argument,       NULL, 'h'},
  {NULL, no_argument, NULL, 0}
};



void show_usage(char const *name) {
  printf("Usage: %s [options] [--] [command]\n", name);
  printf("\n"
	 "      --hostname=NAME        set hostname to NAME\n"
	 "\n"
	 "  -v, --verbose              verbose\n"
	 "  -h, --help                 print help message and exit\n"
	 );
  exit(0);
}



void unshare_user();
int spawn_process(char *const argv[]);



int main(int argc, char *const argv[]) {
  int opt, index;

  while((opt = getopt_long(argc, argv, "+hv", options, &index)) != -1) {
    switch(opt) {
    case '?':
      goto err;

    case 'h':
      show_usage(argv[0]);

    case OPT_HOSTNAME:
      hostname = optarg;
      break;

    case 'v':
      verbose = 1;
      break;

    default:
      flags |= opt;
    }
  }

  if (optind >= argc) {
    fprintf(stderr, "%s: command expected\n", argv[0]);
    goto err;
  }

  unshare_user();

  int status;

  pid_t pid = spawn_process(argv+optind);

  close(STDIN_FILENO);
  close(STDOUT_FILENO);

  PERROR(==-1, waitpid, pid, &status, 0);

  if(WIFSIGNALED(status)) {
    return WTERMSIG(status)+128;
  } else {
    return WEXITSTATUS(status);
  }

  return 0;

err:
  fprintf(stderr, "Try '%s --help'\n", argv[0]);
  exit(EXIT_FAILURE);
}



void write_map(pid_t pid, char *uid_or_gid, char *mapping) {
  char path[PATH_MAX] = {0};

  snprintf(path, PATH_MAX, "/proc/%ld/%s_map", (long)pid, uid_or_gid);

  int fd = open(path, O_RDWR);

  if (fd == -1) {
    fprintf(stderr, "open %s: %s\n", path, strerror(errno));
    exit(EXIT_FAILURE);
  }

  int len = strlen(mapping);

  if(write(fd, mapping, len) != len) {
    fprintf(stderr, "write %s: %s\n", path, strerror(errno));
    exit(EXIT_FAILURE);
  }

  close(fd);
}



void unshare_user() {
  char mapping[25] = {0};

  uid_t uid = geteuid();
  gid_t gid = getegid();

  PERROR(,unshare, CLONE_NEWUSER);

  pid_t pid = getpid();

  VERBOSE("setting uid/gid map\n");

  snprintf(mapping, sizeof(mapping), "0 %ld 1", (long)uid);
  write_map(pid, "uid", mapping);

  snprintf(mapping, sizeof(mapping), "0 %ld 1", (long)gid);
  write_map(pid, "gid", mapping);
}


int do_clone(int(*child)(void*), int flags, void *arg) {
  long pagesize = sysconf(_SC_PAGESIZE);
  void *stack = alloca(pagesize);

  return clone(child,
               stack+pagesize, /* XXX: magic number do not know why */
	       flags|SIGCHLD|CLONE_CHILD_SETTID|CLONE_CHILD_CLEARTID,
	       arg);
}


void post_unshare() {
  VERBOSE("mounting new procfs\n");
  PERROR(==-1, mount, "proc", "/proc", "proc", 0, NULL);

  VERBOSE("mounting new mqueue\n");
  PERROR(==-1, mount, "mqueue", "/dev/mqueue", "mqueue", 0, NULL);

  if (hostname) {
    VERBOSE("setting new hostname\n");
    PERROR(==-1, sethostname, hostname, strlen(hostname));
  }
}


int do_exec(char **argv) {
  post_unshare();
  VERBOSE("start execing '%s'\n", argv[0]);
  PERROR(==-1, execvp, argv[0], argv);
  return EXIT_FAILURE;
}


int init(void *arg) {
  pid_t pid = -1;
  PERROR(==-1, pid = fork);

  if (pid == 0) {
    return do_exec((char **)arg);
  }

  for(;;) {
    pid_t child_pid = -1;
    int status;

    child_pid = waitpid(-1, &status, 0);

    if (child_pid == pid) {
      if (WIFEXITED(status)) {
        if(WIFSIGNALED(status)) {
          return WTERMSIG(status)+128;
        } else {
          return WEXITSTATUS(status);
        }
      }
    }

    if (child_pid == -1) {
      fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
    }
  }
}


int spawn_process(char *const argv[]) {
  int clone_flags = CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWPID;
  pid_t pid = -1;
  PERROR(== -1, pid = do_clone, init, clone_flags, (void*)argv);
  return pid;
}
