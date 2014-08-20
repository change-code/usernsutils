#include "global.h"


static char *opt_name = NULL;
static int flags = 0;


static struct option options[] = {
  {"name",         required_argument, NULL, 'n'},

  {"uts",          no_argument,       NULL, CLONE_NEWUTS},
  {"ipc",          no_argument,       NULL, CLONE_NEWIPC},
  {"user",         no_argument,       NULL, CLONE_NEWUSER},
  {"net",          no_argument,       NULL, CLONE_NEWNET},

  {"verbose",      no_argument,       NULL, 'v'},
  {"help",         no_argument,       NULL, 'h'},
  {NULL,           no_argument,       NULL, 0}
};


static void show_usage() {
  printf("Usage: %s %s [options] [--] [command]\n", executable, cmd_name);
  printf("\n"
         "  -n, --name=NAME            name of the namespace\n"
         "\n"
	 "      --uts                  attach UTS namespace\n"
	 "      --ipc                  attach IPC namespace\n"
         "      --user                 attach USER namespace\n"
         "      --net                  attach NET namespace\n"
         "\n"
         "  -h, --help                 print help message and exit\n"
         );
  exit(0);
}


static int attach(char const *pid_str, char *const argv[]) {
  static const int mask[] = {
    CLONE_NEWUSER,
    CLONE_NEWNS,
    CLONE_NEWUTS,
    CLONE_NEWIPC,
    CLONE_NEWPID,
    CLONE_NEWNET
  };

  static char const* filename[] = {
      "/proc/%s/ns/user",
      "/proc/%s/ns/mnt",
      "/proc/%s/ns/uts",
      "/proc/%s/ns/ipc",
      "/proc/%s/ns/pid",
      "/proc/%s/ns/net",
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

  PERROR(==-1, execvp, argv[0], argv);
  return EXIT_FAILURE;
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
  char *pid_str = malloc(size+1);

  PERROR(==size, fread, pid_str, size, 1, pid_file);

  errno = 0;
  char *endptr = NULL;
  strtol(pid_str, &endptr, 10);
  ERROR(errno || (endptr == pid_str), "bad pid '%s'\n", pid_str);

  return attach(pid_str, make_argv(optind, argc, argv));
err:
  fprintf(stderr, "Try '%s %s --help'\n", executable, cmd_name);
  exit(EXIT_FAILURE);
}
