#include "global.h"


static struct option options[] = {
  {"help",         no_argument,       NULL, 'h'},

  {NULL,           no_argument,       NULL, 0}
};


static void show_usage() {
  printf("Usage: %s %s [options] name\n", executable, cmd_name);
  printf("\n"
	 "  -h, --help                 print help message and exit\n"
	 );
  exit(0);
}

int cmd_connect(int argc, char *const argv[]) {
  int opt, index;

  while((opt = getopt_long(argc, argv, "+n:h", options, &index)) != -1) {
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

  ERROR(optind >= argc, "missing name\n");
  char *rundir = getenv("XDG_RUNTIME_DIR");
  ERROR(!rundir, "environment XDG_RUNTIME_DIR is not set\n");

  char arg_unix_connect[PATH_MAX+13] = {0};
  PERROR(<0, snprintf, arg_unix_connect, sizeof(arg_unix_connect), "UNIX-CONNECT:%s/userns/%s/telnetd", rundir, argv[optind]);

  PERROR(==-1, execlp, "socat", "socat", "-,raw,echo=0", arg_unix_connect, NULL);
  exit(EXIT_FAILURE);
err:
  fprintf(stderr, "Try '%s %s --help'\n", executable, cmd_name);
  exit(EXIT_FAILURE);
}
