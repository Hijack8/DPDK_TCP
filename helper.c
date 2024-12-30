
#include "main.h"

extern struct localhost *lhost;

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

struct localhost *find_host_by_fd(struct localhost *lhost, int sockfd) {
  for (struct localhost *hosti = lhost; hosti != NULL; hosti = hosti->next) {
    if (hosti->fd == sockfd)
      return hosti;
  }
  return NULL;
}

void del_node(struct localhost *host) {
  host->prev->next = host->next;
  host->next->prev = host->prev;
}

struct localhost *find_host_by_ip_port(struct localhost *lhost, uint32_t ip,
                                       uint16_t port) {
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
