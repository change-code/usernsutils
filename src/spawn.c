#include "global.h"


#define OPT_USERNS 0
#define OPT_NETNS 1


static char* opt_name = NULL;
static char* opt_domain = NULL;
static int opt_userns = 0;
static int opt_netns = 0;
static char *opt_netns_name = NULL;
static int netns_fd = -1;
static FILE *pid_file = NULL;


static struct option options[] = {
  {"name",         required_argument, NULL, 'n'},
  {"domain",       optional_argument, NULL, 'd'},
  {"user",         no_argument,       NULL, OPT_USERNS},
  {"net",          optional_argument, NULL, OPT_NETNS},
  {"help",         no_argument,       NULL, 'h'},

  {NULL,           no_argument,       NULL, 0}
};


static void show_usage() {
  printf("Usage: %s %s [options] [--] [command]\n", executable, cmd_name);
  printf("\n"
         "  -n, --name=NAME            name of the namespace\n"
         "  -d, --domain=DOMAIN        domain of the namespace\n"
         "      --user                 new USER namespace\n"
         "      --net[=NETNS]          new NET namespace, or use NETNS\n"
         "\n"
         "  -h, --help                 print help message and exit\n"
         );
  exit(0);
}


static void write_map(pid_t pid, char *uid_or_gid, char *mapping) {
  char path[PATH_MAX] = {0};
  snprintf(path, PATH_MAX, "/proc/%ld/%s_map", (long)pid, uid_or_gid);
  int fd = -1;
  PERROR(==-1, fd = open, path, O_RDWR);
  int len = strlen(mapping);
  PERROR(!=len, write, fd, mapping, len);
  close(fd);
}


static void deny_setgroups(pid_t pid) {
  char path[PATH_MAX] = {0};
  snprintf(path, PATH_MAX, "/proc/%ld/setgroups", (long)pid);
  char deny[] = "deny";
  int fd = -1;
  PERROR(==-1, fd = open, path, O_RDWR);
  int len = strlen(deny);
  PERROR(!=len, write, fd, deny, len);
  close(fd);
}


static void unshare_user() {
  char mapping[25] = {0};

  uid_t uid = geteuid();
  gid_t gid = getegid();

  PERROR(,unshare, CLONE_NEWUSER);

  pid_t pid = getpid();

  VERBOSE("setting uid/gid map\n");
  deny_setgroups(pid);

  snprintf(mapping, sizeof(mapping), "0 %ld 1", (long)uid);
  write_map(pid, "uid", mapping);

  snprintf(mapping, sizeof(mapping), "0 %ld 1", (long)gid);
  write_map(pid, "gid", mapping);
}


static int ns_main(void *arg) {
  fclose(pid_file);

  if (opt_netns_name) {
    PERROR(==-1, setns, netns_fd, CLONE_NEWNET);
    close(netns_fd);
  }

  setenv("USERNS_NAME", opt_name, 1);
  VERBOSE("setting new hostname\n");
  PERROR(==-1, sethostname, opt_name, strlen(opt_name));

  setenv("USERNS_DOMAIN", opt_domain, 1);
  VERBOSE("setting new domainname\n");
  PERROR(==-1, setdomainname, opt_domain, strlen(opt_domain));

  pid_t pid = -1;
  PERROR(==-1, pid = fork);

  if (pid == 0) {
    char **argv = (char **)arg;
    VERBOSE("exec '%s'\n", argv[0]);
    PERROR(==-1, execvp, argv[0], argv);
    return EXIT_FAILURE;
  }

  for(;;) {
    pid_t child_pid = -1;
    int status;

    child_pid = waitpid(-1, &status, 0);

    if (child_pid == pid) {
      if (WIFSTOPPED(status)) {
        continue;
      }

      if(WIFSIGNALED(status)) {
        return WTERMSIG(status)+128;
      } else {
        return WEXITSTATUS(status);
      }
    }

    if (child_pid == -1) {
      fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
    }
  }

  return EXIT_FAILURE;
}


static int spawn_process(char *const argv[]) {
  int flags = CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID;

  if (opt_netns && (!opt_netns_name)) {
    flags |= CLONE_NEWNET;
  }

  long pagesize = sysconf(_SC_PAGESIZE);
  void *stack = alloca(pagesize);

  pid_t pid = -1;
  PERROR(== -1,
         pid = clone,
         ns_main,
         stack+pagesize, /* XXX: magic number do not know why */
         flags|SIGCHLD|CLONE_CHILD_SETTID|CLONE_CHILD_CLEARTID,
         (void*)argv);

  fprintf(pid_file, "%ld", (long)pid);
  fflush(pid_file);

  if (opt_netns_name) {
    close(netns_fd);
  }

  return pid;
}


int cmd_spawn(int argc, char *const argv[]) {
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

    case 'd':
      opt_domain = optarg;
      break;

    case OPT_USERNS:
      opt_userns = 1;
      break;

    case OPT_NETNS:
      opt_netns = 1;
      opt_netns_name = optarg;
      break;

    default:
      break;
    }
  }

  BADOPT(!opt_name, "missing name\n");

  opt_domain = (opt_domain)?opt_domain:getenv("USERNS_DOMAIN");
  opt_domain = (opt_domain)?opt_domain:"localdomain";

  char *rundir = getenv("XDG_RUNTIME_DIR");
  ERROR(!rundir, "environment XDG_RUNTIME_DIR is not set\n");

  int dirfd = -1;
  PERROR(==-1, dirfd = open, rundir, O_PATH|O_DIRECTORY|O_NOFOLLOW);

  char const* dirs[] = {"userns", opt_name};

  for(size_t i=0; i<sizeof(dirs)/sizeof(char const*); i++) {
    PERROR(==-1 && (errno != EEXIST), mkdirat, dirfd, dirs[i], 0700);
    int subdirfd = -1;
    PERROR(==-1, subdirfd = openat, dirfd, dirs[i], O_PATH|O_DIRECTORY|O_NOFOLLOW);
    close(dirfd);
    dirfd = subdirfd;
  }

  int pid_fd = 1;
  PERROR(==-1, pid_fd = openat, dirfd, "pid", O_CREAT|O_WRONLY, 0700);
  close(dirfd);

  PERROR(==-1, flock, pid_fd, LOCK_EX|LOCK_NB);
  pid_file = fdopen(pid_fd, "w");

  if (opt_netns_name) {
    char netns_fd_path[PATH_MAX] = {0};
    snprintf(netns_fd_path, PATH_MAX, "/var/run/netns/%s", opt_netns_name);
    netns_fd = open(netns_fd_path, O_RDONLY);
    BADOPT(netns_fd == -1, "cannot open netns named '%s'\n", opt_netns_name);
  }

  if (opt_userns) {
    unshare_user();
  }

  pid_t pid = spawn_process(make_argv(optind, argc, argv));

  close(STDIN_FILENO);
  close(STDOUT_FILENO);

  for(;;) {
    int status;
    PERROR(==-1, waitpid, pid, &status, 0);

    if (WIFSTOPPED(status)) {
      continue;
    }

    if (WIFSIGNALED(status)) {
      return WTERMSIG(status) + 128;
    } else {
      return WEXITSTATUS(status);
    }
  }

  return EXIT_FAILURE;
err:
  fprintf(stderr, "Try '%s %s --help'\n", executable, cmd_name);
  exit(EXIT_FAILURE);
}
