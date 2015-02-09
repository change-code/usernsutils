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

  char arg_unix_listen[PATH_MAX+17] = {0};
  PERROR(<0, snprintf, arg_unix_listen, sizeof(arg_unix_listen), "UNIX-LISTEN:%s/userns/%s/telnetd,fork", rundir, name);

  char *command = (optind >= argc)?getenv("SHELL"):argv[optind];
  if (!command) {
    command = "/bin/sh";
  }

  char arg_exec[PATH_MAX+36] = {0};
  PERROR(<0, snprintf, arg_exec, sizeof(arg_exec), "EXEC:%s,pty,setsid,setpgid,stderr,ctty", command);

  PERROR(==-1, execlp, "socat", "socat", arg_unix_listen, arg_exec, NULL);
  exit(EXIT_FAILURE);
err:
  fprintf(stderr, "Try '%s %s --help'\n", executable, cmd_name);
  exit(EXIT_FAILURE);
}
