
#include "epoll.h"
#include "main.h"

extern struct localhost *lhost;
extern struct tcp_streams *tcp_list;

void add_to_head(struct localhost *lh) {
  printf("add to head \n");
  if (lhost == NULL) {
    lhost = lh;
    lhost->prev = NULL;
    lhost->next = NULL;
    return;
  }
  lh->next = lhost->next;
  lh->prev = lhost;
  struct localhost *tmp = lhost->next;
  lhost->next = lh;
  if (tmp != NULL)
    tmp->prev = lh;
  lhost = lh;
  if (lhost == NULL)
    printf("lhost == NULL \n");
  return;
}

void *find_host_by_fd(int sockfd) {
  // for udp
  for (struct localhost *hosti = lhost; hosti != NULL; hosti = hosti->next) {
    if (hosti->fd == sockfd)
      return hosti;
  }

  // for tcp
  struct tcp_streams *tcp_list_inst = tcp_list_instance();
  for (struct tcp_stream *streami = tcp_list_inst->stream_head; streami != NULL;
       streami = streami->next) {
    if (streami->fd == sockfd)
      return streami;
  }

  // for epoll
  if (tcp_list_inst->ep != NULL && sockfd == tcp_list_inst->ep->fd) {
    return tcp_list_inst->ep;
  }
  return NULL;
}

void del_node(struct localhost *host) {
  if (host->prev)
    host->prev->next = host->next;
  if (host->next)
    host->next->prev = host->prev;
  if (lhost == host)
    lhost = host->next;
  host->next = host->prev = NULL;
}

void tcp_del_node(struct tcp_stream *tcp_s) {
  if (tcp_s->prev)
    tcp_s->prev->next = tcp_s->next;
  if (tcp_s->next)
    tcp_s->next->prev = tcp_s->prev;
  if (tcp_s == tcp_list->stream_head)
    tcp_list->stream_head = tcp_s->next;
  tcp_s->next = tcp_s->prev = NULL;
}

struct localhost *find_host_by_ip_port(uint32_t ip, uint16_t port) {
  for (struct localhost *hosti = lhost; hosti != NULL; hosti = hosti->next) {
    print_ip_port(hosti->localip, hosti->localport);
    print_ip_port(ip, port);
    if (hosti->localip == ip && hosti->localport == port)
      return hosti;
  }
  return NULL;
}

void print_ip_port(uint32_t ip, uint16_t port) {
  printf("%d.%d.%d.%d:%d\n", ip >> 24 & 0xFF, ip >> 16 & 0xFF, ip >> 8 & 0xFF,
         ip & 0xFF, port);
}
