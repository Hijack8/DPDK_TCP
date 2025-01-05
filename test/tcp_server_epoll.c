#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#define BUFFER_SIZE 1024
#define MAX_EVENTS 100

int main() {

  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd == -1) {
    return -1;
  }

  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(struct sockaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port = htons(9999);
  bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

  listen(listenfd, 10);

  // The actual size is managed by the kernel
  int epollfd = epoll_create(1);

  struct epoll_event ev;
  struct epoll_event epoll_events[MAX_EVENTS];

  ev.events = EPOLLIN;
  ev.data.fd = (uint64_t)listenfd;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &ev) < 0) {
    return -1;
  }
  char buff[BUFFER_SIZE] = {0};
  while (1) {

    int nfs = epoll_wait(epollfd, epoll_events, MAX_EVENTS, -1);

    for (int i = 0; i < nfs; i++) {
      if (epoll_events[i].data.fd == listenfd) {
        struct sockaddr_in client;
        socklen_t len = sizeof(client);
        int connfd = accept(listenfd, (struct sockaddr *)&client, &len);
        struct epoll_event ev;
        ev.data.fd = connfd;
        ev.events = EPOLLIN;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, connfd, &ev) < 0) {
          return -1;
        }
      } else {
        int connfd = epoll_events[i].data.fd;
        int n = recv(connfd, buff, BUFFER_SIZE, 0); // block
        if (n > 0) {
          send(connfd, buff, n, 0);
        } else if (n == 0) {
          close(connfd);
        } else { // nonblock
        }
      }
    }
  }
  close(listenfd);
}
