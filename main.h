#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <getopt.h>

#include <rte_memory.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_common.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_prefetch.h>
#include <rte_ethdev.h>
#include <rte_hash.h>
#include <rte_hash_crc.h>
#include <rte_spinlock.h>
#include <rte_timer.h>

#define MAKE_IPV4_ADDR(a, b, c, d) (a + (b << 8) + (c << 16) + (d << 24))
#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

#define APP_MAX_LCORES 10
#define BURST_SIZE 256
#define ENABLE_SEND 1
#define RING_SIZE 1024
#define ARP_TABLE_SIZE 10
#define TIMER_CYCLES 1200000000ULL

struct localhost
{
    int fd;

    uint32_t localip;
    uint8_t localmac[RTE_ETHER_ADDR_LEN];
    uint16_t localport;

    uint8_t protocol;
    struct rte_ring *sndbuf;
    struct rte_ring *rcvbuf;

    struct localhost *prev;
    struct localhost *next;

    pthread_cond_t cond;
    pthread_mutex_t mutex;
};

struct offload
{

    uint32_t sip;
    uint32_t dip;

    uint16_t sport;
    uint16_t dport;

    int protocol;

    unsigned char *data;
    uint16_t length;
};

struct ring_buffer
{
    struct rte_ring *in;
    struct rte_ring *out;
};

int udp_server_entry(__attribute__((unused)) void *arg);
int nsocket(int domain, int type, int protocol);

int nbind(int sockfd, const struct sockaddr *addr,
          socklen_t addrlen);

ssize_t nrecvfrom(int sockfd, void *buf, size_t len, int flags,
                  struct sockaddr *src_addr, socklen_t *addrlen);

ssize_t nsendto(int sockfd, const void *buf, size_t len, int flags,
                const struct sockaddr *dest_addr, socklen_t addrlen);

int nclose(int fd);

int process_udp(struct rte_mbuf *m);

void add_to_head(struct localhost *lh);

struct localhost *find_host_by_fd(struct localhost *lhost, int sockfd);
struct localhost *find_host_by_ip_port(struct localhost *lhost, uint32_t ip, uint16_t port);

void del_node(struct localhost *host);

void udp_out(void);

void fill_ip_hdr(struct rte_mbuf *m, uint16_t len, uint32_t src_ip, uint32_t dst_ip, uint8_t proto_id);

void print_ip_port(uint32_t ip, uint16_t port);