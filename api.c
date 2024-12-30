#include "main.h"

extern struct localhost *lhost;

// socket
int nsocket(int domain, int type, int protocol) {
  int fd = get_fd_from_bitmap();
  if (fd == -1)
    rte_panic("no available fd.");
  struct localhost *lh =
      (struct localhost *)rte_malloc("lh", sizeof(struct localhost), 0);

  lh->fd = fd;
  if (type == SOCK_DGRAM)
    lh->protocol = IPPROTO_UDP;
  else if (type == SOCK_STREAM)
    lh->protocol = IPPROTO_TCP;

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

  return fd;
}

int nbind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  struct localhost *host = find_host_by_fd(lhost, sockfd);
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
  struct localhost *host = find_host_by_fd(lhost, sockfd);
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
  struct localhost *host = find_host_by_fd(lhost, sockfd);
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
  struct localhost *host = find_host_by_fd(lhost, fd);
  if (host == NULL)
    return -1;

  if (host->rcvbuf)
    rte_free(host->rcvbuf);
  if (host->sndbuf)
    rte_free(host->sndbuf);
  del_node(host);
  rte_free(host);
  return 0;
}

int nlisten(int sockfd, int backlog) { return 0; }
ssize_t nrecv(int sockfd, void *buf, size_t len, int flags) { return 0; }
ssize_t nsend(int sockfd, const void *buf, size_t len, int flags) { return 0; }

int naccept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) { return 0; }
