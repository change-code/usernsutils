#include "global.h"


static struct option options[] = {
  {"help",         no_argument,       NULL, 'h'},

  {NULL,           no_argument,       NULL, 0}
};


static void show_usage() {
  printf("Usage: %s %s [options] protocol port\n", executable, cmd_name);
  printf("\n"
	 "  -h, --help                 print help message and exit\n"
	 );
  exit(0);
}


static void epoll_set(int poll_fd, int op, int fd, uint32_t events) {
  struct epoll_event event = {
    .events = events,
    .data = {
      .fd = fd
    }
  };

  PERROR(==-1, epoll_ctl, poll_fd, op, fd, &event);
}


static void set_nonblocking(int fd) {
  int flag = fcntl(fd, F_GETFL);
  fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}


static int handle_connection(int in_fd, int out_fd) {
  struct sockaddr_in dst;
  socklen_t optlen = sizeof(dst);
  PERROR(==-1, getsockopt, in_fd, SOL_IP, SO_ORIGINAL_DST, &dst, &optlen);
  PERROR(==-1, connect, out_fd, &dst, sizeof(dst));

  int poll_fd;
  PERROR(==-1, poll_fd = epoll_create, 1);

  set_nonblocking(in_fd);
  set_nonblocking(out_fd);

  uint32_t in_fd_events = EPOLLIN;
  uint32_t out_fd_events = EPOLLIN;

  epoll_set(poll_fd, EPOLL_CTL_ADD, in_fd, in_fd_events);
  epoll_set(poll_fd, EPOLL_CTL_ADD, out_fd, out_fd_events);

  /* read from out_fd, write to in_fd */
  char in_buffer[4096] = {0};
  ssize_t in_start = 0;
  ssize_t in_end = 0;

  /* read from in_fd, write to out_fd */
  char out_buffer[4096] = {0};
  ssize_t out_start = 0;
  ssize_t out_end = 0;

  struct epoll_event event = {0};

  for(;;) {
    int nfds;
    PERROR(==-1, nfds = epoll_wait, poll_fd, &event, 1, -1);
    if (!nfds) {
      continue;
    }

    fprintf(stderr, "%d: %08x\n", event.data.fd, event.events);

    if (event.events & EPOLLIN) {
      if (event.data.fd == in_fd) {
        ssize_t received;
        PERROR(==-1, received = recv, in_fd, out_buffer+out_end, sizeof(out_buffer)-out_end, 0);
        out_end += received;
        in_fd_events &= ~EPOLLIN;
        if (received) {
          out_fd_events |= EPOLLOUT;
        }
      } else if (event.data.fd == out_fd) {
        ssize_t received;
        PERROR(==-1, received = recv, out_fd, in_buffer+in_end, sizeof(in_buffer)-in_end, 0);
        in_end += received;
        out_fd_events &= ~EPOLLIN;
        if (received) {
          in_fd_events |= EPOLLOUT;
        }
      }
    } else if (event.events & EPOLLOUT) {
      if (event.data.fd == in_fd) {
        ssize_t sent;
        PERROR(==-1, sent = send, in_fd, in_buffer+in_start, in_end-in_start, 0);
        in_start += sent;

        if (in_start == in_end) {
          in_start = 0;
          in_end = 0;
          in_fd_events &= ~EPOLLOUT;
          out_fd_events |= EPOLLIN;
        }

      } else if (event.data.fd == out_fd) {
        ssize_t sent;
        PERROR(==-1, sent = send, out_fd, out_buffer+out_start, out_end-out_start, 0);
        out_start += sent;

        if (out_start == out_end) {
          out_start = 0;
          out_end = 0;
          out_fd_events &= ~EPOLLOUT;
          in_fd_events |= EPOLLIN;
        }
      }
    }

    epoll_set(poll_fd, EPOLL_CTL_MOD, in_fd, in_fd_events);
    epoll_set(poll_fd, EPOLL_CTL_MOD, out_fd, out_fd_events);

    if ((!in_fd_events) && (!out_fd_events)) {
      break;
    }

  }

  return 0;
}


static int tcp_proxy(int port, int socketd_fd) {
  int get_new_out_fd() {
    char sock_type = SOCK_STREAM;
    PERROR(==-1, send, socketd_fd, &sock_type, sizeof(sock_type), 0);
    return recv_fd(socketd_fd);
  }

  int listen_fd = -1;
  PERROR(==-1, listen_fd = socket, AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port = htons(port),
    .sin_addr = {
      .s_addr = htonl(INADDR_LOOPBACK)
    }};

  PERROR(==-1, bind, listen_fd, &addr, sizeof(addr));
  PERROR(==-1, listen, listen_fd, SOMAXCONN);

  for(;;) {
    int fd;
    struct sockaddr_in peer;
    socklen_t size = sizeof(struct sockaddr_in);
    PERROR(==-1, fd = accept, listen_fd, &peer, &size);
    int out_fd = get_new_out_fd();

    pid_t pid;
    PERROR(==-1, pid = fork);

    if (pid == 0) {
      close(listen_fd);
      close(socketd_fd);
      return handle_connection(fd, out_fd);
    }

    close(fd);
    close(out_fd);
  }

  return 0;
}


struct udp_entry {
  struct sockaddr_in addr;
  int out_fd;
  unsigned long long last_access; // 0 means empty
};


#define TABLE_SIZE  8


static int is_same_addr(struct sockaddr_in const *a, struct sockaddr_in const *b) {
  return ((a->sin_family == b->sin_family) &&
          (a->sin_port == b->sin_port) &&
          (a->sin_addr.s_addr == b->sin_addr.s_addr));
}


static void send_back(struct sockaddr_in *src, struct sockaddr_in *dst, char const *buf, ssize_t buflen) {
  int fd;
  PERROR(==-1, fd = socket, AF_INET, SOCK_DGRAM, 0);
  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  setsockopt(fd, SOL_IP, IP_TRANSPARENT, &opt, sizeof(opt));
  int ttl = 255;
  setsockopt(fd, SOL_IP, IP_TTL, &ttl, sizeof(ttl));
  PERROR(==-1, bind, fd, src, sizeof(struct sockaddr_in));
  PERROR(==-1, sendto, fd, buf, buflen, 0, dst, sizeof(struct sockaddr_in));
  close(fd);
}


static int udp_proxy(int port, int socketd_fd) {
  int get_new_out_fd() {
    char sock_type = SOCK_DGRAM;
    PERROR(==-1, send, socketd_fd, &sock_type, sizeof(sock_type), 0);
    return recv_fd(socketd_fd);
  }

  int listen_fd = -1;
  PERROR(==-1, listen_fd = socket, AF_INET, SOCK_DGRAM, 0);
  int opt = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  setsockopt(listen_fd, SOL_IP, IP_TRANSPARENT, &opt, sizeof(opt));
  setsockopt(listen_fd, SOL_IP, IP_ORIGDSTADDR, &opt, sizeof(opt));

  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port = htons(port),
    .sin_addr = {
      .s_addr = htonl(INADDR_LOOPBACK)
    }};

  PERROR(==-1, bind, listen_fd, &addr, sizeof(addr));

  struct udp_entry table[TABLE_SIZE];
  memset(table, 0, sizeof(table));

  int entry_count = 0;
  unsigned long long counter = 0;

  struct udp_entry *find_entry_by_addr(struct sockaddr_in const *addr) {
    for(int i=0; i<TABLE_SIZE; i++) {
      if (table[i].last_access == 0) {
        continue;
      }

      if (is_same_addr(addr, &(table[i].addr))) {
        return &table[i];
      }
    }

    return NULL;
  }

  struct udp_entry *find_entry_by_out_fd(int fd) {
    for(int i=0; i<TABLE_SIZE; i++) {
      if (table[i].last_access == 0) {
        continue;
      }

      if (table[i].out_fd == fd) {
        return &table[i];
      }
    }

    return NULL;
  }

  struct udp_entry *find_first_empty_entry() {
    for(int i=0; i<TABLE_SIZE; i++) {
      if (table[i].last_access == 0) {
        return &table[i];
      }
    }

    return NULL;
  }

  struct udp_entry *find_least_recent_accessed_entry() {
    int min_index = 0;
    unsigned long long min_access = table[0].last_access;

    for(int i=1; i<TABLE_SIZE; i++) {
      if (table[i].last_access == 0) {
        continue;
      }

      if (table[i].last_access < min_access) {
        min_access = table[i].last_access;
        min_index = i;
      }
    }

    return &table[min_index];
  }

  int poll_fd;
  PERROR(==-1, poll_fd = epoll_create, 1);

  epoll_set(poll_fd, EPOLL_CTL_ADD, listen_fd, EPOLLIN);

  struct epoll_event event = {0};

  for(;;) {
    int nfds;
    PERROR(==-1, nfds = epoll_wait, poll_fd, &event, 1, -1);
    if (!nfds) {
      continue;
    }

    if (event.data.fd == listen_fd) {
      char control[CMSG_SPACE(sizeof(struct sockaddr_in))];
      char buf[4096];
      struct sockaddr_in src = {0};

      struct iovec iov = {.iov_base = &buf, .iov_len = sizeof(buf)};
      struct msghdr msg = {
        .msg_name = &src,
        .msg_namelen = sizeof(src),
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = control,
        .msg_controllen = CMSG_LEN(sizeof(struct sockaddr_in)),
        .msg_flags = 0,
      };

      ssize_t recvlen;
      PERROR(==-1, recvlen = recvmsg, listen_fd, &msg, 0);
      struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);

      ERROR((cmsg==NULL) ||
            (cmsg->cmsg_level != SOL_IP) ||
            (cmsg->cmsg_type != IP_ORIGDSTADDR),
            "cmsg: bad message!\n");

      struct sockaddr_in *dst = (struct sockaddr_in *)CMSG_DATA(cmsg);
      struct udp_entry *entry = find_entry_by_addr(&src);

      if (!entry) {
        if (entry_count >= TABLE_SIZE) {
          entry = find_least_recent_accessed_entry();
          PERROR(==-1, epoll_ctl, poll_fd, EPOLL_CTL_DEL, entry->out_fd, NULL);
          close(entry->out_fd);
        } else {
          entry = find_first_empty_entry();
          entry_count += 1;
          ERROR(entry==NULL, "cannot find empty entry");
        }

        entry->out_fd = get_new_out_fd();
        epoll_set(poll_fd, EPOLL_CTL_ADD, entry->out_fd, EPOLLIN);
        entry->addr.sin_family = src.sin_family;
        entry->addr.sin_port = src.sin_port;
        entry->addr.sin_addr.s_addr = src.sin_addr.s_addr;
      }

      counter += 1;
      entry->last_access = counter;
      sendto(entry->out_fd, buf, recvlen, 0, dst, sizeof(struct sockaddr_in));
    } else {
      struct udp_entry *entry = find_entry_by_out_fd(event.data.fd);
      ERROR(entry==NULL, "cannot find entry");
      counter += 1;
      entry->last_access = counter;

      char buf[4096];
      struct sockaddr_in src;
      ssize_t recvlen;
      socklen_t addr_len = sizeof(struct sockaddr_in);
      PERROR(==-1, recvlen = recvfrom, entry->out_fd, buf, sizeof(buf), 0, &src, &addr_len);

      send_back(&src, &(entry->addr), buf, recvlen);
    }
  }

  return 0;
}


struct proto {
  char *const proto_name;
  int (*proto_func)(int port, int fd);
};


static struct proto protos[] = {
  {"tcp",  tcp_proxy},
  {"udp",  udp_proxy},
};


int cmd_proxy(int argc, char *const argv[]) {
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

  BADOPT(argc-optind < 2, "Too few arguments\n");
  BADOPT(argc-optind > 2, "Too many arguments\n");

  char *rundir = getenv("XDG_RUNTIME_DIR");
  ERROR(!rundir, "environment XDG_RUNTIME_DIR is not set\n");

  char *name = getenv("USERNS_NAME");
  ERROR(!name, "running outside a user namespace\n");

  char const *port_str = argv[optind+1];

  errno = 0;
  char *endptr = NULL;
  int port = strtol(port_str, &endptr, 10);
  ERROR(errno || (endptr == port_str), "bad port number '%s'\n", port_str);

  int (*proxy)(int port, int fd) = NULL;
  for(size_t i=0; i<(sizeof(protos)/sizeof(struct proto)); i++) {
    if (strcmp(protos[i].proto_name, argv[optind])) {
      continue;
    }

    proxy = protos[i].proto_func;
    break;
  }

  BADOPT(!proxy, "protocol must be tcp or udp, not '%s'\n", argv[optind]);

  char socket_path[PATH_MAX] = {0};
  snprintf(socket_path, PATH_MAX, "%s/userns/%s/socketd", rundir, name);

  int fd = -1;
  PERROR(==-1, fd = socket, AF_UNIX, SOCK_STREAM, 0);

  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);

  PERROR(==-1, connect, fd, &addr, sizeof(addr));

  return proxy(port, fd);
err:
  fprintf(stderr, "Try '%s %s --help'\n", executable, cmd_name);
  exit(EXIT_FAILURE);
}
