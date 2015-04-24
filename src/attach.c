#include "global.h"


static char *opt_name = NULL;
static int flags = 0;


static struct option options[] = {
  {"name",         required_argument, NULL, 'n'},

  {"user",         no_argument,       NULL, CLONE_NEWUSER},
  {"uts",          no_argument,       NULL, CLONE_NEWUTS},
  {"ipc",          no_argument,       NULL, CLONE_NEWIPC},
  {"pid",          no_argument,       NULL, CLONE_NEWPID},
  {"net",          no_argument,       NULL, CLONE_NEWNET},
  {"mount",        no_argument,       NULL, CLONE_NEWNS},

  {"verbose",      no_argument,       NULL, 'v'},
  {"help",         no_argument,       NULL, 'h'},
  {NULL,           no_argument,       NULL, 0}
};


static void show_usage() {
  printf("Usage: %s %s [options] [--] [command]\n", executable, cmd_name);
  printf("\n"
         "  -n, --name=NAME            name of the namespace\n"
         "\n"
         "      --user                 attach USER namespace\n"
	 "      --uts                  attach UTS namespace\n"
	 "      --ipc                  attach IPC namespace\n"
         "      --pid                  attach PID namespace\n"
         "      --net                  attach NET namespace\n"
         "      --mount                attach MOUNT namespace\n"
         "\n"
         "  -h, --help                 print help message and exit\n"
         );
  exit(0);
}


static int attach(char const *pid_str, char *const argv[]) {
  static const int mask[] = {
    CLONE_NEWUSER,
    CLONE_NEWUTS,
    CLONE_NEWIPC,
    CLONE_NEWPID,
    CLONE_NEWNET,
    CLONE_NEWNS
  };

  static char const* filename[] = {
      "/proc/%s/ns/user",
      "/proc/%s/ns/uts",
      "/proc/%s/ns/ipc",
      "/proc/%s/ns/pid",
      "/proc/%s/ns/net",
      "/proc/%s/ns/mnt",
  };

  for(int i=0;i<6;i++) {
    if (!(flags & mask[i])) {
      continue;
    }

    char ns_path[PATH_MAX] = {0};
    snprintf(ns_path, PATH_MAX, filename[i], pid_str);

    int fd = -1;
    PERROR(==-1, fd = open, ns_path, O_RDONLY);
    PERROR(==-1, setns, fd, 0);
    close(fd);
  }

  if (!(flags & mask[3])) {
    PERROR(==-1, execvp, argv[0], argv);
    return EXIT_FAILURE;
  } else {
    pid_t pid = -1;
    PERROR(==-1, pid = fork);

    if (pid == 0) {
      PERROR(==-1, execvp, argv[0], argv);
      return EXIT_FAILURE;
    }

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
  }
}


int cmd_attach(int argc, char *const argv[]) {
  int opt, index;

  while((opt = getopt_long(argc, argv, "+n:h", options, &index)) != -1) {
    switch(opt) {
    case '?':
      goto err;

    case 'n':
      opt_name = optarg;
      break;

    case 'h':
      show_usage();
      break;

    default:
      flags |= opt;
      break;
    }
  }

  BADOPT(!opt_name, "missing name\n");
  setenv("USERNS_NAME", opt_name, 1);

  char *rundir = getenv("XDG_RUNTIME_DIR");
  ERROR(!rundir, "environment XDG_RUNTIME_DIR is not set\n");

  char pid_filename[PATH_MAX] = {0};
  snprintf(pid_filename, PATH_MAX, "%s/userns/%s/pid", rundir, opt_name);

  int pid_fd = -1;
  PERROR(==-1, pid_fd = open, pid_filename, O_RDONLY);

  ERROR(flock(pid_fd, LOCK_EX|LOCK_NB) != -1, "namespace '%s' has gone\n", opt_name);
  ERROR(errno != EWOULDBLOCK, "flock: %s\n", strerror(errno));

  FILE *pid_file = fdopen(pid_fd, "r");
  fseek(pid_file, 0, SEEK_END);
  size_t size = ftell(pid_file);
  rewind(pid_file);
  char *pid_str = alloca(size+1);
  PERROR(==size, fread, pid_str, size, 1, pid_file);
  pid_str[size] = 0;

  errno = 0;
  char *endptr = NULL;
  strtol(pid_str, &endptr, 10);
  ERROR(errno || (endptr == pid_str), "bad pid '%s'\n", pid_str);

  return attach(pid_str, make_argv(optind, argc, argv));
err:
  fprintf(stderr, "Try '%s %s --help'\n", executable, cmd_name);
  exit(EXIT_FAILURE);
}
