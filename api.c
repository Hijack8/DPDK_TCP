#include "main.h"
#include <bits/pthreadtypes.h>
#include <netinet/in.h>
#include <pthread.h>
#include <rte_byteorder.h>
#include <rte_malloc.h>
#include <rte_memcpy.h>
#include <rte_ring.h>
#include <rte_ring_core.h>
#include <rte_tcp.h>
#include <string.h>

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

  // return the listen stream
  for (struct tcp_stream *streami = tcp_list_inst->stream_head; streami != NULL;
       streami = streami->next) {
    if (streami->dport == dport)
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

// socket -> fd
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
  if (host->protocol == IPPROTO_UDP) {
    const struct sockaddr_in *laddr = (struct sockaddr_in *)addr;
    host->localport = laddr->sin_port;
    rte_memcpy(&host->localip, &laddr->sin_addr.s_addr, sizeof(uint32_t));
    // rte_memcpy(&host->localmac, my_addr, RTE_ETHER_ADDR_LEN);
  } else if (host->protocol == IPPROTO_TCP) {
    struct tcp_stream *tcp_s = (struct tcp_stream *)host;
    const struct sockaddr_in *laddr = (struct sockaddr_in *)addr;
    tcp_s->dport = laddr->sin_port;
    rte_memcpy(&tcp_s->dip, &laddr->sin_addr.s_addr, sizeof(uint32_t));
    tcp_s->status = TCP_STATUS_CLOSED;
  } else {
    RTE_LOG(INFO, APP, "Cannot recoganize the l4 protocol. \n");
  }
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

void set_fd_from_bitmap(int fd) {
  if (fd > MAX_FD)
    return;
  bitmap[fd] = 0;
}

int nclose(int fd) {
  struct localhost *host = find_host_by_fd(fd);
  if (host == NULL)
    return -1;
  if (host->protocol == IPPROTO_TCP) {
    struct tcp_stream *tcp_s = (struct tcp_stream *)host;
    // send a FIN pkt
    struct tcp_frame *fin =
        rte_malloc("tcp_frame", sizeof(struct tcp_frame), 0);
    memset(fin, 0, sizeof(struct tcp_frame));
    fin->acknum = tcp_s->rcv_nxt;
    fin->seqnum = tcp_s->snd_nxt;
    fin->tcp_flags = RTE_TCP_FIN_FLAG | RTE_TCP_ACK_FLAG;
    fin->data = NULL;
    fin->sport = tcp_s->dport;
    fin->dport = tcp_s->sport;
    fin->windows = TCP_INITIAL_WINDOW;
    fin->hdrlen_off = 0x50;
    int ret;
    do {
      ret = rte_ring_enqueue(tcp_s->sndbuf, fin);
    } while (ret != 0);
    tcp_s->status = TCP_STATUS_LAST_ACK;
    set_fd_from_bitmap(fd);
    // if (tcp_s->rcvbuf)
    //   rte_free(tcp_s->rcvbuf);
    // if (tcp_s->sndbuf)
    //   rte_free(tcp_s->sndbuf);
    // tcp_del_node(tcp_s);
    // rte_free(tcp_s);
  } else if (host->protocol == IPPROTO_UDP) {
    if (host->rcvbuf)
      rte_free(host->rcvbuf);
    if (host->sndbuf)
      rte_free(host->sndbuf);
    del_node(host);
    rte_free(host);
    set_fd_from_bitmap(fd);
  } else {
    RTE_LOG(INFO, APP, "Cannot recognize the protocol. \n");
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

int nrecv(int sockfd, void *buf, size_t len, int flags) {
  struct tcp_stream *tcp_s = find_host_by_fd(sockfd);
  if (tcp_s == NULL || tcp_s->proto != IPPROTO_TCP)
    return -1;

  int rst_len;
  pthread_mutex_lock(&tcp_s->mutex);
  int ret;
  struct tcp_frame *tcp_pkt;

  while ((ret = rte_ring_dequeue(tcp_s->rcvbuf, (void **)&tcp_pkt)) != 0) {
    pthread_cond_wait(&tcp_s->cond, &tcp_s->mutex);
  }
  pthread_mutex_unlock(&tcp_s->mutex);

  int datalen = tcp_pkt->length;
  if (datalen > len) {
    rte_memcpy(buf, tcp_pkt->data, len);

    rte_memcpy(tcp_pkt->data, tcp_pkt->data + len, datalen - len);
    rst_len = datalen - len;
    tcp_pkt->length = rst_len;
    do {
      ret = rte_ring_enqueue(tcp_s->rcvbuf, tcp_pkt);
    } while (ret != 0);
    rte_free(tcp_pkt->data);
    rte_free(tcp_pkt);
  } else if (datalen == 0) {
    // here datalen = 0 tell us it's a FIN pkt
    rst_len = 0;
    rte_free(tcp_pkt);
  } else {
    rte_memcpy(buf, tcp_pkt->data, datalen);
    rst_len = datalen;
    rte_free(tcp_pkt->data);
    rte_free(tcp_pkt);
  }

  return rst_len;
}
ssize_t nsend(int sockfd, const void *buf, size_t len, int flags) {
  struct tcp_stream *tcp_s = find_host_by_fd(sockfd);
  if (tcp_s == NULL || tcp_s->proto != IPPROTO_TCP)
    return 0;
  struct tcp_frame *tcp_pkt =
      rte_malloc("tcp_frame", sizeof(struct tcp_frame), 0);
  if (tcp_pkt == NULL) {
    RTE_LOG(INFO, APP, "In send, tcp frame malloc failed.\n");
    return 0;
  }
  memset(tcp_pkt, 0, sizeof(struct tcp_frame));

  tcp_pkt->data = rte_malloc("data", len + 1, 0);
  if (tcp_pkt->data == NULL) {
    RTE_LOG(INFO, APP, "In send, tcp data malloc failed. \n");
    rte_free(tcp_pkt);
    return 0;
  }
  memset(tcp_pkt->data, 0, len + 1);
  rte_memcpy(tcp_pkt->data, buf, len);
  tcp_pkt->length = len;
  tcp_pkt->seqnum = tcp_s->snd_nxt;
  tcp_pkt->acknum = tcp_s->rcv_nxt;
  tcp_pkt->sport = tcp_s->dport;
  tcp_pkt->dport = tcp_s->sport;
  tcp_pkt->tcp_flags = RTE_TCP_ACK_FLAG | RTE_TCP_PSH_FLAG;
  tcp_pkt->windows = TCP_INITIAL_WINDOW;
  tcp_pkt->hdrlen_off = 0x50;

  int ret = 0;
  do {
    ret = rte_ring_enqueue(tcp_s->sndbuf, tcp_pkt);
  } while (ret != 0);

  return len;
}

struct tcp_stream *tcp_find_host_by_port(uint16_t dport) {
  struct tcp_streams *tcp_list_inst = tcp_list_instance();
  for (struct tcp_stream *streami = tcp_list_inst->stream_head; streami != NULL;
       streami = streami->next) {
    // fd have not been allocated
    if (streami->dport == dport && streami->fd == -1)
      return streami;
  }
  return NULL;
}

// get the established stream && define the fd
int naccept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  struct tcp_stream *tcp_s = find_host_by_fd(sockfd);
  if (tcp_s == NULL || tcp_s->proto != IPPROTO_TCP)
    return -1;
  pthread_mutex_lock(&tcp_s->mutex);
  struct tcp_stream *established_stream;
  while ((established_stream = tcp_find_host_by_port(tcp_s->dport)) == NULL)
    pthread_cond_wait(&tcp_s->cond, &tcp_s->mutex);
  pthread_mutex_unlock(&tcp_s->mutex);

  established_stream->fd = get_fd_from_bitmap();
  struct sockaddr_in *saddr = (struct sockaddr_in *)addr;
  saddr->sin_port = established_stream->sport;
  rte_memcpy(&saddr->sin_addr, &established_stream->sip, sizeof(uint32_t));
  return established_stream->fd;
}
