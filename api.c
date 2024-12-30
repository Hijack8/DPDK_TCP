#include "main.h"
#include <bits/pthreadtypes.h>
#include <netinet/in.h>
#include <pthread.h>
#include <rte_malloc.h>
#include <rte_ring.h>
#include <rte_ring_core.h>

extern struct localhost *lhost;
extern struct tcp_streams *tcp_list;

struct tcp_streams *tcp_list_instance() {
  if (tcp_list == NULL) {
    tcp_list = rte_malloc("tcp_streams", sizeof(struct tcp_streams), 0);
    tcp_list->count = 0;
    tcp_list->stream_head = NULL;
    return tcp_list;
  }
  return tcp_list;
}

struct tcp_stream *tcp_find_host_by_ip_port(uint32_t sip, uint32_t dip,
                                            uint16_t sport, uint16_t dport) {
  struct tcp_streams *tcp_list_inst = tcp_list_instance();
  for (struct tcp_stream *streami = tcp_list_inst->stream_head; streami != NULL;
       streami = streami->next) {
    if (streami->sip == sip && streami->dip == dip && streami->sport == sport &&
        streami->dport == dport)
      return streami;
  }
  return NULL;
}

#define MAX_FD 1024
int bitmap[MAX_FD];

int get_fd_from_bitmap() {
  for (int i = 0; i < MAX_FD; i++) {
    if (bitmap[i] == 0) {
      bitmap[i] = 1;
      return i;
    }
  }
  return -1;
}
// socket
int nsocket(int domain, int type, int protocol) {
  int fd = get_fd_from_bitmap();
  if (fd == -1)
    rte_panic("no available fd.");

  if (type == SOCK_DGRAM) {
    struct localhost *lh =
        (struct localhost *)rte_malloc("lh", sizeof(struct localhost), 0);

    lh->fd = fd;

    lh->rcvbuf = rte_ring_create("recv buffer", RING_SIZE, rte_socket_id(),
                                 RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (lh->rcvbuf == NULL) {
      rte_free(lh);
      return -1;
    }
    lh->sndbuf = rte_ring_create("send buffer", RING_SIZE, rte_socket_id(),
                                 RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (lh->sndbuf == NULL) {
      rte_free(lh->rcvbuf);
      rte_free(lh);
      return -1;
    }

    pthread_cond_t cond_t = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t mutex_t = PTHREAD_MUTEX_INITIALIZER;
    memcpy(&lh->cond, &cond_t, sizeof(pthread_cond_t));
    memcpy(&lh->mutex, &mutex_t, sizeof(pthread_mutex_t));

    add_to_head(lh);

    lh->protocol = IPPROTO_UDP;
  } else if (type == SOCK_STREAM) {
    struct tcp_stream *tcp_s =
        rte_malloc("tcp_stream", sizeof(struct tcp_stream), 0);
    if (tcp_s == NULL) {
      return -1;
    }
    tcp_s->fd = fd;
    tcp_s->rcvbuf = rte_ring_create("recv buffer", RING_SIZE, rte_socket_id(),
                                    RING_F_SP_ENQ | RING_F_SC_DEQ);
    tcp_s->next = tcp_s->prev = NULL;
    if (tcp_s->rcvbuf == NULL) {
      rte_free(tcp_s);
      return -1;
    }
    tcp_s->sndbuf = rte_ring_create("send buffer", RING_SIZE, rte_socket_id(),
                                    RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (tcp_s->rcvbuf == NULL) {
      rte_free(tcp_s->rcvbuf);
      rte_free(tcp_s);
      return -1;
    }
    tcp_s->proto = IPPROTO_TCP;
    pthread_cond_t blank_cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t blank_mutex = PTHREAD_MUTEX_INITIALIZER;
    rte_memcpy(&tcp_s->cond, &blank_cond, sizeof(pthread_cond_t));
    rte_memcpy(&tcp_s->mutex, &blank_mutex, sizeof(pthread_mutex_t));
    tcp_add_head(tcp_s);
  }

  return fd;
}

int nbind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  struct localhost *host = find_host_by_fd(sockfd);
  if (host == NULL)
    return -1;
  const struct sockaddr_in *laddr = (struct sockaddr_in *)addr;
  host->localport = laddr->sin_port;
  rte_memcpy(&host->localip, &laddr->sin_addr.s_addr, sizeof(uint32_t));
  print_ip_port(host->localip, host->localport);
  // rte_memcpy(&host->localmac, my_addr, RTE_ETHER_ADDR_LEN);
  return 0;
}

ssize_t nrecvfrom(int sockfd, void *buf, size_t len, int flags,
                  struct sockaddr *src_addr, socklen_t *addrlen) {
  struct localhost *host = find_host_by_fd(sockfd);
  if (host == NULL)
    return -1;
  struct rte_ring *ring = host->rcvbuf;
  struct offload *ofl;
  pthread_mutex_lock(&host->mutex);
  int ret;
  while ((ret = rte_ring_dequeue(ring, (void **)&ofl)) != 0) {
    pthread_cond_wait(&host->cond, &host->mutex);
  }
  pthread_mutex_unlock(&host->mutex);
  struct sockaddr_in *srcaddr = (struct sockaddr_in *)src_addr;
  memcpy(&(srcaddr->sin_addr.s_addr), &ofl->sip, sizeof(uint32_t));
  srcaddr->sin_port = ofl->sport;

  if (ofl->length <= len) {
    rte_memcpy(buf, ofl->data, ofl->length);
    rte_free(ofl->data);
    rte_free(ofl);
    return ret;
  }

  rte_memcpy(buf, ofl->data, len);

  unsigned char *ptr = rte_malloc("remain", ofl->length - len, 0);
  rte_memcpy(ptr, ofl->data + len, ofl->length - len);

  rte_free(ofl->data);
  ofl->data = ptr;
  ofl->length -= len;
  do {
    ret = rte_ring_enqueue(host->rcvbuf, ofl);
  } while (ret != 0);

  return len;
}

ssize_t nsendto(int sockfd, const void *buf, size_t len, int flags,
                const struct sockaddr *dest_addr, socklen_t addrlen) {
  struct localhost *host = find_host_by_fd(sockfd);
  if (host == NULL)
    return -1;

  struct offload *ofl =
      (struct offload *)rte_malloc("ofl", sizeof(struct offload), 0);
  struct sockaddr_in *d_addr = (struct sockaddr_in *)rte_malloc(
      "sockaddr", sizeof(struct sockaddr_in), 0);
  rte_memcpy(d_addr, dest_addr, sizeof(struct sockaddr_in));
  ofl->sip = host->localip;
  ofl->sport = host->localport;
  ofl->dip = d_addr->sin_addr.s_addr;
  ofl->dport = d_addr->sin_port;
  ofl->length = len;

  ofl->data = rte_malloc("data", len, 0);
  if (ofl->data == NULL) {
    rte_free(ofl);
    return -1;
  }
  rte_memcpy(ofl->data, buf, len);
  int ret;
  do {
    ret = rte_ring_enqueue(host->sndbuf, ofl);
  } while (ret != 0);

  return len;
}

int nclose(int fd) {
  struct localhost *host = find_host_by_fd(fd);
  if (host == NULL)
    return -1;
  if (host->protocol == IPPROTO_TCP) {
    struct tcp_stream *tcp_s = (struct tcp_stream *)host;
    if (tcp_s->rcvbuf)
      rte_free(tcp_s->rcvbuf);
    if (tcp_s->sndbuf)
      rte_free(tcp_s->sndbuf);
    tcp_del_node(tcp_s);
    rte_free(tcp_s);
  } else if (host->protocol == IPPROTO_UDP) {
    if (host->rcvbuf)
      rte_free(host->rcvbuf);
    if (host->sndbuf)
      rte_free(host->sndbuf);
    del_node(host);
    rte_free(host);
  }
  return 0;
}

// listen is for tcp
int nlisten(int sockfd, int backlog) {
  void *hostinfo = find_host_by_fd(sockfd);
  if (hostinfo == NULL)
    return -1;
  struct tcp_stream *stream = (struct tcp_stream *)hostinfo;
  if (stream->proto == IPPROTO_TCP) {
    stream->status = TCP_STATUS_LISTEN;
  }
  return 0;
}

ssize_t nrecv(int sockfd, void *buf, size_t len, int flags) { return 0; }
ssize_t nsend(int sockfd, const void *buf, size_t len, int flags) { return 0; }

int naccept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  struct tcp_stream *tcp_s = find_host_by_fd(sockfd);
  if (tcp_s == NULL || tcp_s->proto != IPPROTO_TCP)
    return -1;
  pthread_mutex_lock(&tcp_s->mutex);

  pthread_mutex_unlock(&tcp_s->mutex);
  return 0;
}
