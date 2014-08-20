#include "global.h"


char *executable = NULL;
char *cmd_name = NULL;
int opt_verbose = 0;


static struct option options[] = {
  {"verbose",      no_argument,       NULL, 'v'},
  {"help",         no_argument,       NULL, 'h'},
  {NULL,           no_argument,       NULL, 0}
};


static void show_usage() {
  printf("Usage: %s [options] [--] <command> [<args>]\n", executable);
  printf("\n"
         "  -v, --verbose              verbose\n"
         "  -h, --help                 print help message and exit\n"
         );
  exit(0);
}


struct command {
  char *const cmd_name;
  int (*cmd_func)(int argc, char *const argv[]);
};


static struct command commands[] = {
  {"spawn",    cmd_spawn},
  {"attach",   cmd_attach},
  {"listen",   cmd_listen},
  {"connect",  cmd_connect},
  {"socketd",  cmd_socketd},
  {"proxy",    cmd_proxy},
};


int main(int argc, char *const argv[]) {
  executable = argv[0];

  int opt, index;
  while((opt = getopt_long(argc, argv, "+hv", options, &index)) != -1) {
    switch(opt) {
    case '?':
      goto err;

    case 'h':
      show_usage();
      break;

    case 'v':
      opt_verbose = 1;
      break;

    default:
      break;
    }
  }

  if (optind >= argc) {
    goto err;
  }

  int (*cmd)(int argc, char *const argv[]) = NULL;

  for(int i=0; i<(sizeof(commands)/sizeof(struct command)); i++) {
    if (strcmp(commands[i].cmd_name, argv[optind])) {
      continue;
    }

    cmd_name = commands[i].cmd_name;
    cmd = commands[i].cmd_func;
    break;
  }

  BADOPT(!cmd, "'%s' is not a valid command\n", argv[optind]);
  int offset = optind;
  optind = 1;
  return cmd(argc-offset, argv+offset);
err:
  fprintf(stderr, "Try '%s --help'\n", executable);
  exit(EXIT_FAILURE);
}
