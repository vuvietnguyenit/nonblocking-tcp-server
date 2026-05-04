// Program implement a TCP server can handle multiple concurrency
// connections by non-blocking call approach. So, this program just need to use
// only one thread with minimum resource usage to handle multiple requests at
// the same time. No vibe-coding, just use 12 hours to implement it.
// This program was providing some flags that you will use to simulate
// non-blocking:
// - Send flag FAST from client if you want to get response immediately.
// - Send flag SLOW from client to get a slow request (this response will take
// 10 seconds to send back to client).
// - Send other message. It will reply to client message: "Holla".
// - If you want to kill server. Send KILL from client :D.
// To help more easily to test, let create client by using tool like `nc`,
// `telnet` or something like this. I will provide the usage in README.md
// Author: Vu Nguyen
// License: MIT

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <fcntl.h>

#define MAX_FDS 4
#define PAYLOAD_SIZE 32

typedef enum { STATE_READING, STATE_WAITING, STATE_WRITING } conn_state_t;

typedef struct {
  int fd;
  conn_state_t state;
  char reqbuf[PAYLOAD_SIZE];
  int reqlen;
  char respbuf[PAYLOAD_SIZE];
  int resplen;
  long ready_at_ms;
  char ip[INET_ADDRSTRLEN];
  int port;
} connection_t;

connection_t conn[MAX_FDS];
struct pollfd pfds[MAX_FDS];

long now_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void trim_newline(char *s) {
  int len = strlen(s);
  while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
    s[len - 1] = '\0';
    len--;
  }
}

void handle_request(int i, int *is_server_alive) {
  connection_t *c = &conn[i];
  trim_newline(c->reqbuf);
  if (strcmp(c->reqbuf, "FAST") == 0) {
    snprintf(c->respbuf, sizeof(c->respbuf), "> This is fast response\n");
    c->state = STATE_WRITING;
    c->resplen = strlen(c->respbuf);
    pfds[i].revents = POLLOUT;
  } else if (strcmp(c->reqbuf, "SLOW") == 0) {
    snprintf(c->respbuf, sizeof(c->respbuf), "> This is slow response\n");
    c->state = STATE_WAITING;
    c->ready_at_ms = now_ms() + 10000; // send back after 10 seconds
    c->resplen = strlen(c->respbuf);
  } else if (strcmp(c->reqbuf, "KILL") == 0) {
    // Handle graceful kill server by client :)))). Because I cannot find
    // reasonable case to stop server. So, client send KILL will kill
    // server :D
    *is_server_alive = 0;
  } else {
    snprintf(c->respbuf, sizeof(c->respbuf), "> Holla\n");
    c->state = STATE_WRITING;
    c->resplen = strlen(c->respbuf);
    pfds[i].revents = POLLOUT;
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: simple_tcp_epoll.o <IP> <PORT>");
    exit(EXIT_FAILURE);
  }
  char *ipaddr = argv[1];
  char *port = argv[2];
  struct sockaddr_in server_addr, client_addr;
  socklen_t client_len = sizeof(client_addr);
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(atoi(port));
  //   try to convert input IP address first
  if (inet_pton(AF_INET, ipaddr, &server_addr.sin_addr.s_addr) != 1) {
    perror("convert ip input failed");
    exit(EXIT_FAILURE);
  }
  int sfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sfd == -1) {
    perror("cannot create socket");
    exit(EXIT_FAILURE);
  }
  set_nonblocking(sfd);
  int opt = 1;
  setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  // bind socket
  if (bind(sfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
    perror("cannot bind socket");
    exit(EXIT_FAILURE);
  }
  listen(sfd, 5);

  nfds_t nfds = 1;

  // add server socket
  pfds[0].fd = sfd;
  pfds[0].events = POLLIN;
  printf("Listen TCP server at %s %s\n", ipaddr, port);
  int is_server_alive = 1;
  while (is_server_alive) {

    long now = now_ms();
    int ready = poll(pfds, nfds, 1000);
    if (ready == -1) {
      perror("poll");
      exit(EXIT_FAILURE);
    }

    for (int i = 0; i < nfds; i++) {
      if (pfds[i].revents & POLLIN) {
        if (pfds[i].fd == sfd) {
          // handle new connection
          int client_fd;
          client_fd = accept(sfd, (struct sockaddr *)&client_addr, &client_len);
          if (client_fd == -1) {
            perror("refuse connection");
            close(client_fd);
            continue;
          }
          set_nonblocking(client_fd);
          // verify that client is not violate LIMIT
          if (nfds > MAX_FDS) {
            close(client_fd);
            continue;
          }
          pfds[nfds].fd = client_fd;
          pfds[nfds].events = POLLIN;

          // this step above just try to convert information of client to human
          // readable information, it it failed, just skip it
          char ipstring[INET_ADDRSTRLEN];
          const char *ipstr = inet_ntop(AF_INET, &client_addr.sin_addr.s_addr,
                                        ipstring, INET_ADDRSTRLEN);
          if (ipstr == NULL) {
            perror("cannot convert IP to string");
            continue;
          }
          // need write it as client connection fd, because the old one isn't
          // contains client info
          connection_t *new_conn = &conn[nfds];
          new_conn->fd = client_fd;
          new_conn->state = STATE_READING;
          strcpy(new_conn->ip, ipstr);
          new_conn->port = client_addr.sin_port;
          printf("Got new connection from %s:%d\n", ipstring,
                 ntohs(client_addr.sin_port));
          nfds++;
        } else {
          // handle client fd: revents = 1 & fd != sfd (client fd)
          connection_t *client_conn = &conn[i];
          int c = pfds[i].fd;
          client_conn->fd = c;

          int n = read(c, client_conn->reqbuf, PAYLOAD_SIZE);
          if (n == -1) {
            fprintf(stderr, "cannot read data from cis_server_alivelient");
            close(c);
            nfds--;
            i--;
            continue;
          }
          if (n == 0) {
            // EOF
            printf("client disconnected\n");
            close(c);

            // delete client from pfds array
            pfds[i] = pfds[i - 1];
            nfds--;
            i--;
            continue;
          }
          client_conn->reqlen = n;
          // write(0, client_conn.reqbuf, client_conn.reqlen = n);
          handle_request(i, &is_server_alive);
        }
      }
      if (pfds[i].revents & POLLOUT) {
        // print to console
        printf("< [%s:%d] :: %s\n", conn[i].ip, conn[i].port, conn[i].reqbuf);
        // send back to client
        write(conn[i].fd, conn[i].respbuf, conn[i].resplen);
        // when we done to write data, mark this fd by ready to get input data
        pfds[i].revents = POLLIN;
      }
    }
    // handle slow response send back to client, need to process from index = 1,
    // because index = 0 is server fd
    for (int i = 1; i <= nfds; i++) {
      connection_t *c = &conn[i];
      if (c->state == STATE_WAITING && now >= c->ready_at_ms) {
        c->state = STATE_WRITING;
        printf("< [%s:%d] :: %s\n", conn[i].ip, conn[i].port, conn[i].reqbuf);
        write(c->fd, c->respbuf, c->resplen);
        c->state = STATE_READING;
      }
    }
  }
  // If server dead, broadcast to all client know about it is dead and close
  // socket server
  char *dead_server_resp = "SERVER IS KILLED BY CLIENT :D";
  for (int i = 1; i <= nfds; i++) {
    write(pfds[i].fd, dead_server_resp, strlen(dead_server_resp));
  }
  close(sfd);
  return 0;
}