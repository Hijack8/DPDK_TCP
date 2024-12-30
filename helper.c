
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
  for (struct localhost *hosti = lhost; hosti != NULL; hosti = hosti->next) {
    if (hosti->fd == sockfd)
      return hosti;
  }

  struct tcp_streams *tcp_list_inst = tcp_list_instance();
  for (struct tcp_stream *streami = tcp_list_inst->stream_head; streami != NULL;
       streami = streami->next) {
    if (streami->fd == sockfd)
      return streami;
  }
  return NULL;
}

void del_node(struct localhost *host) {
  host->prev->next = host->next;
  host->next->prev = host->prev;
}

void tcp_del_node(struct tcp_stream *tcp_s) {
  tcp_s->prev->next = tcp_s->next;
  tcp_s->next->prev = tcp_s->prev;
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
