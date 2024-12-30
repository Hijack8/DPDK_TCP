#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <rte_common.h>
#include <rte_debug.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_hash.h>
#include <rte_hash_crc.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_memory.h>
#include <rte_mempool.h>
#include <rte_per_lcore.h>
#include <rte_prefetch.h>
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

struct localhost {
  int fd;
  uint8_t protocol;

  uint32_t localip;
  uint8_t localmac[RTE_ETHER_ADDR_LEN];
  uint16_t localport;

  struct rte_ring *sndbuf;
  struct rte_ring *rcvbuf;

  struct localhost *prev;
  struct localhost *next;

  pthread_cond_t cond;
  pthread_mutex_t mutex;
};

struct offload {

  uint32_t sip;
  uint32_t dip;

  uint16_t sport;
  uint16_t dport;

  int protocol;

  unsigned char *data;
  uint16_t length;
};

struct ring_buffer {
  struct rte_ring *in;
  struct rte_ring *out;
};

#define TCP_OPTION_LENGTH 10

#define TCP_MAX_SEQ 4294967295

#define TCP_INITIAL_WINDOW 14600

// for tcp
typedef enum TCP_STATUS {

  TCP_STATUS_CLOSED = 0,
  TCP_STATUS_LISTEN,
  TCP_STATUS_SYN_RCVD,
  TCP_STATUS_SYN_SENT,
  TCP_STATUS_ESTABLISHED,

  TCP_STATUS_FIN_WAIT_1,
  TCP_STATUS_FIN_WAIT_2,
  TCP_STATUS_CLOSING,
  TCP_STATUS_TIME_WAIT,

  TCP_STATUS_CLOSE_WAIT,
  TCP_STATUS_LAST_ACK

} TCP_STATUS;

struct tcp_stream { // tcb control block

  int fd;
  uint8_t proto;

  uint32_t sip;
  uint32_t dip;

  uint16_t sport;
  uint16_t dport;

  uint8_t localmac[RTE_ETHER_ADDR_LEN];

  uint32_t snd_nxt; // seqnum
  uint32_t rcv_nxt; // acknum

  TCP_STATUS status;

  struct rte_ring *sndbuf;
  struct rte_ring *rcvbuf;

  struct tcp_stream *prev;
  struct tcp_stream *next;
  pthread_cond_t cond;
  pthread_mutex_t mutex;
};

struct tcp_streams {
  int count;
  struct tcp_stream *stream_head;
};

struct tcp_frame {

  uint16_t sport;
  uint16_t dport;
  uint32_t seqnum; // cpu
  uint32_t acknum; // cpu
  uint8_t hdrlen_off;
  uint8_t tcp_flags;
  uint16_t windows;
  uint16_t cksum;
  uint16_t tcp_urp;

  int optlen;
  uint32_t option[TCP_OPTION_LENGTH];

  unsigned char *data;
  int length;
};

int udp_server_entry(__attribute__((unused)) void *arg);
int nsocket(int domain, int type, int protocol);

int nbind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

ssize_t nrecvfrom(int sockfd, void *buf, size_t len, int flags,
                  struct sockaddr *src_addr, socklen_t *addrlen);

ssize_t nsendto(int sockfd, const void *buf, size_t len, int flags,
                const struct sockaddr *dest_addr, socklen_t addrlen);

int nclose(int fd);

int process_udp(struct rte_mbuf *m);

void add_to_head(struct localhost *lh);

void *find_host_by_fd(int sockfd);
struct localhost *find_host_by_ip_port(uint32_t ip, uint16_t port);

void del_node(struct localhost *host);

void tcp_del_node(struct tcp_stream *tcp_s);
void udp_out(void);

void fill_ip_hdr(struct rte_mbuf *m, uint16_t len, uint32_t src_ip,
                 uint32_t dst_ip, uint8_t proto_id);

void print_ip_port(uint32_t ip, uint16_t port);

void process_tcp(struct rte_mbuf *m);

void tcp_handle_listen(struct tcp_stream *tcp_s, struct rte_tcp_hdr *tcphdr);
void tcp_handle_syn_rcvd(struct tcp_stream *tcp_s, struct rte_tcp_hdr *tcphdr);
void tcp_handle_syn_send(struct tcp_stream *tcp_s, struct rte_tcp_hdr *tcphdr);
void tcp_handle_established(struct tcp_stream *tcp_s,
                            struct rte_tcp_hdr *tcphdr);
void tcp_out(void);

void tcp_add_head(struct tcp_stream *sp);

int tcp_server_entry(void *arg);

int get_fd_from_bitmap();

int nlisten(int sockfd, int backlog);
ssize_t nrecv(int sockfd, void *buf, size_t len, int flags);
ssize_t nsend(int sockfd, const void *buf, size_t len, int flags);
int naccept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

struct tcp_streams *tcp_list_instance();
struct tcp_stream *tcp_find_host_by_ip_port(uint32_t sip, uint32_t dip,
                                            uint16_t sport, uint16_t dport);
