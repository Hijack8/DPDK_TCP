#include "main.h"
#include <rte_tcp.h>

extern struct tcp_streams *tcp_list;
extern struct ring_buffer *proto_ring;
extern struct rte_mempool *mbuf_pool;

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

struct tcp_stream *tcp_stream_create(uint32_t sip, uint32_t dip, uint16_t sport,
                                     uint16_t dport) {
  struct tcp_stream *new_stream =
      rte_malloc("tcp_stream", sizeof(struct tcp_stream), 0);
  if (new_stream == NULL)
    return NULL;
  new_stream->sip = sip;
  new_stream->dip = dip;
  new_stream->sport = sport;
  new_stream->dport = dport;
  new_stream->proto = IPPROTO_TCP;
  new_stream->rcv_nxt = 0;
  new_stream->status = TCP_STATUS_LISTEN;
  new_stream->sndbuf = rte_ring_create("send", RING_SIZE, rte_socket_id(), 0);
  if (new_stream->sndbuf == NULL) {
    RTE_LOG(INFO, APP, "Cannot create the tcp stream sndbuf \n");
    rte_free(new_stream);
    return NULL;
  }

  new_stream->rcvbuf = rte_ring_create("recv", RING_SIZE, rte_socket_id(), 0);
  if (new_stream->rcvbuf == NULL) {
    RTE_LOG(INFO, APP, "Cannot create the tcp stream rcvbuf \n");
    rte_free(new_stream->sndbuf);
    rte_free(new_stream);
    return NULL;
  }

  uint32_t next_seed = time(NULL);
  new_stream->snd_nxt = rand_r(&next_seed) % TCP_MAX_SEQ;

  // rte_memcpy(new_stream->localmac, my_addr, RTE_ETHER_ADDR_LEN);
  tcp_add_head(new_stream);
  return new_stream;
}

void process_tcp(struct rte_mbuf *m) {
  struct rte_ipv4_hdr *iphdr = rte_pktmbuf_mtod_offset(
      m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
  if (iphdr->next_proto_id != IPPROTO_TCP)
    return;
  struct rte_tcp_hdr *tcphdr = (struct rte_tcp_hdr *)(iphdr + 1);

  uint16_t tcpcksum = tcphdr->cksum;
  tcphdr->cksum = 0;
  uint16_t mycksum = rte_ipv4_udptcp_cksum(iphdr, tcphdr);
  if ((tcpcksum) != mycksum) {
    RTE_LOG(INFO, APP, "tcphdr cksum = %x , my cksum = %x \n", tcphdr->cksum,
            mycksum);
    return;
  }

  struct tcp_stream *tcp_s = tcp_find_host_by_ip_port(
      iphdr->src_addr, iphdr->dst_addr, tcphdr->src_port, tcphdr->dst_port);
  if (tcp_s == NULL) {
    tcp_s = tcp_stream_create(iphdr->src_addr, iphdr->dst_addr,
                              tcphdr->src_port, tcphdr->dst_port);
    if (tcp_s == NULL)
      return;
  }
  switch (tcp_s->status) {
  case TCP_STATUS_CLOSED:
    break;
  case TCP_STATUS_LISTEN: // server waiting syn
    tcp_handle_listen(tcp_s, tcphdr);
    break;
  case TCP_STATUS_SYN_RCVD: // server recv syn
    tcp_handle_syn_rcvd(tcp_s, tcphdr);
    break;
  case TCP_STATUS_SYN_SENT: // client send a syn
    tcp_handle_syn_send(tcp_s, tcphdr);
    break;
  case TCP_STATUS_ESTABLISHED:
    tcp_handle_established(tcp_s, tcphdr);
    break;
  case TCP_STATUS_FIN_WAIT_1:
    break;
  case TCP_STATUS_FIN_WAIT_2:
    break;
  case TCP_STATUS_CLOSING:
    break;
  case TCP_STATUS_TIME_WAIT:
    break;
  case TCP_STATUS_CLOSE_WAIT:
    break;
  case TCP_STATUS_LAST_ACK:
    break;
  }
}

void tcp_handle_listen(struct tcp_stream *tcp_s, struct rte_tcp_hdr *tcphdr) {
  uint8_t tcp_flgs = tcphdr->tcp_flags;
  if ((tcp_flgs & RTE_TCP_SYN_FLAG) != 0) {
    // return syn + ack
    RTE_LOG(INFO, APP, "Recv SYN \n");
    struct tcp_frame *tcp_pd =
        rte_malloc("tcp_frame", sizeof(struct tcp_frame), 0);
    tcp_pd->sport = tcphdr->dst_port;
    tcp_pd->dport = tcphdr->src_port;
    tcp_pd->seqnum = tcp_s->snd_nxt;
    tcp_pd->acknum = (rte_be_to_cpu_32(tcphdr->sent_seq) + 1);
    tcp_pd->hdrlen_off = 0x50;
    tcp_pd->tcp_flags = (RTE_TCP_ACK_FLAG | RTE_TCP_SYN_FLAG);
    tcp_pd->windows = TCP_INITIAL_WINDOW;

    tcp_pd->data = NULL;
    tcp_pd->length = 0;

    int ret;
    do {
      ret = rte_ring_enqueue(tcp_s->sndbuf, tcp_pd);
    } while (ret != 0);
    tcp_s->status = TCP_STATUS_SYN_RCVD;
  }
}
void tcp_handle_syn_rcvd(struct tcp_stream *tcp_s, struct rte_tcp_hdr *tcphdr) {
  if (tcphdr->tcp_flags & RTE_TCP_ACK_FLAG) {
    RTE_LOG(INFO, APP, "Recv ACK, turn to establish \n");
    if (tcp_s->status == TCP_STATUS_SYN_RCVD) {
      uint32_t acknum = rte_be_to_cpu_32(tcphdr->recv_ack);
      if (acknum == tcp_s->snd_nxt + 1) {
        // TODO
      }
      tcp_s->status = TCP_STATUS_ESTABLISHED;
    }
  }
}
void tcp_handle_syn_send(struct tcp_stream *tcp_s, struct rte_tcp_hdr *tcphdr) {
}
void tcp_handle_established(struct tcp_stream *tcp_s,
                            struct rte_tcp_hdr *tcphdr) {
  if (tcphdr->tcp_flags & RTE_TCP_SYN_FLAG) {
    RTE_LOG(INFO, APP, "Established: recv syn pkt. \n");
  } else if (tcphdr->tcp_flags & RTE_TCP_PSH_FLAG) {
  }
  uint8_t hdrlen = tcphdr->data_off & 0xf0;
  hdrlen >>= 4;
  uint8_t *payload = (uint8_t *)(tcphdr) + hdrlen * 4;
  RTE_LOG(INFO, APP, "payload: %s \n", payload);
}

void tcp_add_head(struct tcp_stream *sp) {
  struct tcp_streams *tcp_list_inst = tcp_list_instance();
  tcp_list_inst->count++;
  if (tcp_list_inst->stream_head == NULL) {
    tcp_list_inst->stream_head = sp;
    sp->prev = NULL;
    sp->next = NULL;
    return;
  }
  sp->prev = tcp_list_inst->stream_head;
  sp->next = tcp_list_inst->stream_head->next;
  sp->prev->next = sp;
  if (sp->next != NULL) {
    sp->next->prev = sp;
  }
  tcp_list_inst->stream_head = sp;
}

struct rte_mbuf *encode_tcp(struct tcp_frame *tcp_pd, uint32_t sip,
                            uint32_t dip) {
  struct rte_mbuf *m = rte_pktmbuf_alloc(mbuf_pool);
  int total_len = tcp_pd->length + sizeof(struct rte_ether_hdr) +
                  sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_tcp_hdr) +
                  tcp_pd->optlen * sizeof(uint32_t);
  m->data_len = total_len;
  m->pkt_len = total_len;

  fill_ip_hdr(m, total_len - sizeof(struct rte_ether_hdr), dip, sip,
              IPPROTO_TCP);

  struct rte_ipv4_hdr *iphdr = rte_pktmbuf_mtod_offset(
      m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
  struct rte_tcp_hdr *tcphdr = rte_pktmbuf_mtod_offset(
      m, struct rte_tcp_hdr *,
      sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
  tcphdr->src_port = tcp_pd->sport;
  tcphdr->dst_port = tcp_pd->dport;
  tcphdr->sent_seq = rte_be_to_cpu_32(tcp_pd->seqnum);
  tcphdr->recv_ack = rte_be_to_cpu_32(tcp_pd->acknum);
  tcphdr->data_off = tcp_pd->hdrlen_off;
  tcphdr->tcp_flags = tcp_pd->tcp_flags;
  tcphdr->rx_win = tcp_pd->windows;
  tcphdr->cksum = tcp_pd->tcp_urp;

  if (tcp_pd->data != NULL) {
    uint8_t *payload =
        (uint8_t *)(tcphdr + 1) + tcp_pd->optlen * sizeof(uint32_t);
    rte_memcpy(payload, tcp_pd->data, tcp_pd->length);
  }

  tcphdr->cksum = 0;
  tcphdr->cksum = rte_ipv4_udptcp_cksum(iphdr, tcphdr);
  return m;
}

void tcp_out(void) {
  struct tcp_streams *tcp_list = tcp_list_instance();
  for (struct tcp_stream *streami = tcp_list->stream_head; streami != NULL;
       streami = streami->next) {
    int ret;
    struct tcp_frame *tcp_pd;
    ret = rte_ring_dequeue(streami->sndbuf, (void **)&tcp_pd);
    if (ret != 0)
      continue;
    struct rte_mbuf *m = encode_tcp(tcp_pd, streami->sip, streami->dip);
    rte_free(tcp_pd);
    do {
      ret = rte_ring_enqueue(proto_ring->out, m);
    } while (ret != 0);
  }
}

#define BUFFER_SIZE 1024
int tcp_server_entry(void *arg) {

  int listenfd = nsocket(AF_INET, SOCK_STREAM, 0);
  if (listenfd == -1) {
    return -1;
  }

  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(struct sockaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port = htons(9999);
  nbind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

  nlisten(listenfd, 10);

  while (1) {

    struct sockaddr_in client;
    socklen_t len = sizeof(client);
    int connfd = naccept(listenfd, (struct sockaddr *)&client, &len);

    char buff[BUFFER_SIZE] = {0};
    while (1) {

      int n = nrecv(connfd, buff, BUFFER_SIZE, 0); // block
      if (n > 0) {
        printf("recv: %s\n", buff);
        nsend(connfd, buff, n, 0);

      } else if (n == 0) {

        nclose(connfd);
        break;
      } else { // nonblock
      }
    }
  }
  nclose(listenfd);
}
