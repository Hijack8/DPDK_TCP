#include "main.h"

struct localhost *lhost = NULL;

uint32_t local_ip;
uint32_t gateway_ip;

const int dpdk_port_id = 0;
const int n_lcores = 4;
int lcores[10];

struct rte_timer arp_timer;
struct rte_hash *mac_hash;

struct time_val
{
  uint8_t val[32];
};

uint8_t broadcast_addr[RTE_ETHER_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t my_addr[RTE_ETHER_ADDR_LEN];

uint8_t arp_table[ARP_TABLE_SIZE][RTE_ETHER_ADDR_LEN];
uint32_t arp_table_curr_n;

struct rte_mempool *mbuf_pool;

struct new_icmp_hdr
{
  uint8_t icmp_type;      /* ICMP packet type. */
  uint8_t icmp_code;      /* ICMP packet code. */
  rte_be16_t icmp_cksum;  /* ICMP packet checksum. */
  rte_be16_t icmp_ident;  /* ICMP packet identifier. */
  rte_be16_t icmp_seq_nb; /* ICMP packet sequence number. */
  struct time_val icmp_timestamp;
  uint8_t padding[40];
} __rte_packed;

struct ring_buffer *eth_ring;
struct ring_buffer *proto_ring;
struct ring_buffer *tcp_ring;

void app_init_hash()
{
  /* init hash func */
  char name[10] = "hash_name";
  struct rte_hash_parameters hash_params = {
      .name = name,
      .entries = ARP_TABLE_SIZE,
      .key_len = sizeof(uint32_t),
      .hash_func = rte_hash_crc,
      .hash_func_init_val = 0,
  };
  mac_hash = rte_hash_create(&hash_params);
}

void app_init_rings()
{
  eth_ring = (struct ring_buffer *)rte_malloc("eth_ring", sizeof(struct ring_buffer), 0);
  proto_ring = (struct ring_buffer *)rte_malloc("proto_ring", sizeof(struct ring_buffer), 0);
  tcp_ring = (struct ring_buffer *)rte_malloc("tcp_ring", sizeof(struct ring_buffer), 0);
  eth_ring->in = rte_ring_create("eth_ring_in", RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);
  eth_ring->out = rte_ring_create("eth_ring_out", RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);
  proto_ring->in = rte_ring_create("proto_ring_in", RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);
  proto_ring->out = rte_ring_create("proto_ring_out", RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);
  tcp_ring->in = rte_ring_create("tcp_ring_in", RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);
  tcp_ring->out = rte_ring_create("tcp_ring_out", RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);
  if (eth_ring->in == NULL || eth_ring->out == NULL ||
      proto_ring->in == NULL || proto_ring->out == NULL ||
      tcp_ring->in == NULL || tcp_ring->out == NULL)
  {
    rte_panic("Create ring failed \n");
  }
}

void app_init_lcores()
{
  int n_lcore = 0;
  for (int i = 0; i < n_lcores; i++)
  {
    if (!rte_lcore_is_enabled(i))
      continue;
    lcores[i] = i;
  }
}

/* Ethernet device configuration */
static struct rte_eth_conf port_conf_default = {
    .txmode = {
        .mq_mode = RTE_ETH_MQ_TX_NONE,
    },
};

void app_init_ports()
{
  uint16_t nb_sys_ports = rte_eth_dev_count_avail(); //
  if (nb_sys_ports == 0)
  {
    rte_exit(EXIT_FAILURE, "No Supported eth found\n");
  }

  struct rte_eth_dev_info dev_info;
  rte_eth_dev_info_get(dpdk_port_id, &dev_info); //

  const int num_rx_queues = 1;
  const int num_tx_queues = 1;
  struct rte_eth_conf port_conf = port_conf_default;
  rte_eth_dev_configure(dpdk_port_id, num_rx_queues, num_tx_queues, &port_conf);

  if (rte_eth_rx_queue_setup(dpdk_port_id, 0, 1024,
                             rte_eth_dev_socket_id(dpdk_port_id), NULL, mbuf_pool) < 0)
  {

    rte_exit(EXIT_FAILURE, "Could not setup RX queue\n");
  }

#if ENABLE_SEND
  struct rte_eth_txconf txq_conf = dev_info.default_txconf;
  txq_conf.offloads = port_conf.rxmode.offloads;
  if (rte_eth_tx_queue_setup(dpdk_port_id, 0, 1024,
                             rte_eth_dev_socket_id(dpdk_port_id), &txq_conf) < 0)
  {

    rte_exit(EXIT_FAILURE, "Could not setup TX queue\n");
  }
#endif

  if (rte_eth_dev_start(dpdk_port_id) < 0)
  {
    rte_exit(EXIT_FAILURE, "Could not start\n");
  }
  rte_eth_promiscuous_enable(dpdk_port_id);
}

static void print_addr(struct rte_ether_addr *addr)
{
  printf("MAC Address : %02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 "\n",
         addr->addr_bytes[0], addr->addr_bytes[1], addr->addr_bytes[2],
         addr->addr_bytes[3], addr->addr_bytes[4], addr->addr_bytes[5]);
}

void print_pkt(struct rte_mbuf *m)
{
  printf("recv a pkt :\n");
  struct rte_ether_hdr *hdr;
  hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
  print_addr(&hdr->src_addr);
  print_addr(&hdr->dst_addr);
  printf("==========:\n");
}

int app_main_loop_nic()
{
  RTE_LOG(INFO, APP, "lcore %d is doing nic tx/rx.\n", rte_lcore_id());
  int port = dpdk_port_id;
  struct rte_mbuf *bufs[BURST_SIZE];
  struct rte_mbuf *bufs_tx[BURST_SIZE];
  int n_bufs = 0;
  int n_bufs_tx = 0;
  while (1)
  {
    if (n_bufs < BURST_SIZE - 8)
    {
      int nb_rx = rte_eth_rx_burst(port, 0, bufs + n_bufs, 8);
      n_bufs += nb_rx;
    }
    if (n_bufs != 0)
    {
      int ret = rte_ring_enqueue_burst(eth_ring->in, (void **)bufs, n_bufs, NULL);
      if (ret > 0 && ret < n_bufs)
      {
        rte_memcpy(bufs, bufs + ret, (n_bufs - ret) * sizeof(struct rte_mbuf *));
      }
      n_bufs -= ret;
    }

    int nb_rd_tx = rte_ring_dequeue_burst(eth_ring->out, (void **)bufs_tx, 8, NULL);
    n_bufs_tx += nb_rd_tx;
    if (n_bufs_tx > 0)
    {
      int ret = rte_eth_tx_burst(port, 0, bufs_tx, n_bufs_tx);
      if (ret != n_bufs_tx)
      {
        rte_memcpy(bufs_tx, bufs_tx + ret, (n_bufs_tx - ret) * sizeof(struct rte_mbuf *));
      }
      n_bufs_tx -= ret;
    }
  }
  return 0;
}

int find_arp_table(uint32_t ip_addr)
{
  int index = rte_hash_lookup(mac_hash, &ip_addr);
  if (index >= 0 && index < ARP_TABLE_SIZE)
  {
    return index;
  }
  return -1;
}

int append_arp_table(uint32_t ip_addr, uint8_t *mac_addr)
{
  if (find_arp_table(ip_addr) == -1)
  {
    int index = rte_hash_add_key(mac_hash, &ip_addr);
    RTE_LOG(INFO, APP, "Now know the MAC of the IP %d.%d.%d.%d \n", ip_addr & 0xFF, (ip_addr >> 8) & 0xFF, (ip_addr >> 16) & 0xFF, (ip_addr >> 24) & 0xFF);
    if (index < 0)
      rte_panic("mac hash add key error \n");
    rte_memcpy(arp_table[index], mac_addr, RTE_ETHER_ADDR_LEN);
    return 0;
  }
  return -1;
}

int fill_ether_hdr(struct rte_mbuf *m, uint8_t *src_mac, uint8_t *dst_mac, uint16_t type)
{
  struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
  // if (eth_hdr->ether_type != RTE_GTP_TYPE_IPV4)
  //   return 0;
  struct rte_ipv4_hdr *ipv4_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
  rte_memcpy(eth_hdr->dst_addr.addr_bytes, dst_mac, RTE_ETHER_ADDR_LEN);
  rte_memcpy(eth_hdr->src_addr.addr_bytes, src_mac, RTE_ETHER_ADDR_LEN);
  eth_hdr->ether_type = rte_cpu_to_be_16(type);
  return 0;
}

void update_timer()
{
  static uint64_t prev_tsc = 0, cur_tsc;
  uint64_t diff_tsc;

  cur_tsc = rte_rdtsc();
  diff_tsc = cur_tsc - prev_tsc;
  if (diff_tsc > TIMER_CYCLES)
  {
    rte_timer_manage();
    prev_tsc = cur_tsc;
  }
}

struct rte_mbuf *encode_arp(uint16_t arp_opcode, uint8_t *dst_mac, uint32_t target_ip, uint32_t source_ip)
{
  struct rte_mbuf *arp_pkt = rte_pktmbuf_alloc(mbuf_pool);
  if (arp_pkt == NULL)
    rte_panic("alloc an arp pkt failed \n");
  int pkt_size = sizeof(struct rte_ether_hdr) + sizeof(struct rte_arp_hdr);
  arp_pkt->pkt_len = pkt_size;
  arp_pkt->data_len = pkt_size;

  struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(arp_pkt, struct rte_ether_hdr *);
  rte_memcpy(eth_hdr->src_addr.addr_bytes, my_addr, RTE_ETHER_ADDR_LEN);
  // if (!strncmp((const char *)dst_mac, (const char *)broadcast_addr, RTE_ETHER_ADDR_LEN))
  // {
  //   // should not be broadcast
  //   uint8_t mac[RTE_ETHER_ADDR_LEN] = {0x0};
  //   rte_memcpy(eth_hdr->dst_addr.addr_bytes, mac, RTE_ETHER_ADDR_LEN);
  // }
  // else
  // {
  rte_memcpy(eth_hdr->dst_addr.addr_bytes, dst_mac, RTE_ETHER_ADDR_LEN);
  // }

  eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP);

  struct rte_arp_hdr *arp_hdr = rte_pktmbuf_mtod_offset(arp_pkt, struct rte_arp_hdr *, sizeof(struct rte_ether_hdr));
  arp_hdr->arp_hardware = rte_cpu_to_be_16(1); // 2 bytes need to change the order of bytes
  arp_hdr->arp_protocol = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
  arp_hdr->arp_hlen = RTE_ETHER_ADDR_LEN; // 1 byte
  arp_hdr->arp_plen = sizeof(uint32_t);
  arp_hdr->arp_opcode = rte_cpu_to_be_16(arp_opcode);

  rte_memcpy(arp_hdr->arp_data.arp_sha.addr_bytes, my_addr, RTE_ETHER_ADDR_LEN);
  rte_memcpy(arp_hdr->arp_data.arp_tha.addr_bytes, dst_mac, RTE_ETHER_ADDR_LEN);

  arp_hdr->arp_data.arp_sip = source_ip;
  arp_hdr->arp_data.arp_tip = target_ip;
  return arp_pkt;
}

static void arp_request_timer_cb(__attribute__((unused)) struct rte_timer *tim, void *arg)
{
  // RTE_LOG(INFO, APP, "ARP request \n");
  if (find_arp_table(gateway_ip) != -1)
    return;
  struct rte_mbuf *arp_pkt = encode_arp(RTE_ARP_OP_REQUEST, broadcast_addr, gateway_ip, local_ip);
  int ret = rte_ring_enqueue(eth_ring->out, arp_pkt);
  if (ret != 0)
    rte_pktmbuf_free(arp_pkt);
}

void app_set_timer()
{
  rte_timer_init(&arp_timer);
  uint64_t hz = rte_get_timer_hz();
  unsigned lcore_id = rte_lcore_id();
  rte_timer_reset(&arp_timer, hz, PERIODICAL, lcore_id, arp_request_timer_cb, mbuf_pool);
}

void ether_send(struct rte_mbuf *m)
{
  // multi producor
  rte_ring_enqueue(eth_ring->out, m);
}

void process_arp(struct rte_mbuf *m)
{
  struct rte_arp_hdr *arp_hdr = rte_pktmbuf_mtod_offset(m, struct rte_arp_hdr *, sizeof(struct rte_ether_hdr));
  if (arp_hdr->arp_data.arp_tip != local_ip)
    return;
  if (arp_hdr->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REQUEST))
  {
    RTE_LOG(INFO, APP, "recv arp request \n");
    struct rte_mbuf *arp_reply_pkt = encode_arp(
        (uint16_t)RTE_ARP_OP_REPLY,                      // ARP op
        (uint8_t *)arp_hdr->arp_data.arp_sha.addr_bytes, // arp srouce mac addr
        (uint32_t)arp_hdr->arp_data.arp_sip,             // target ip
        (uint32_t)arp_hdr->arp_data.arp_tip);            // source ip
    ether_send(arp_reply_pkt);
  }
  else if (arp_hdr->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REPLY))
  {
    // Add the mac addr to arp table
    RTE_LOG(INFO, APP, "recv arp reply \n");
    int index = find_arp_table(arp_hdr->arp_data.arp_sip);
    if (index == -1)
    {
      int ret = append_arp_table(arp_hdr->arp_data.arp_sip, arp_hdr->arp_data.arp_sha.addr_bytes);
    }
  }
}

int app_main_loop_eth()
{
  RTE_LOG(INFO, APP, "lcore %d is doing eth process.\n", rte_lcore_id());
  struct rte_mbuf *bufs_in[BURST_SIZE];
  struct rte_mbuf *bufs_out[BURST_SIZE];
  app_set_timer();
  while (1)
  {
    // the pkts from low level (NIC)
    int ret = rte_ring_dequeue(eth_ring->in, (void **)bufs_in);
    if (ret == 0)
    {
      struct rte_mbuf *m = bufs_in[0];
      struct rte_ether_hdr *hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);

      if (hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP))
      {
        process_arp(m);
      }
      else if (hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))
      {
        do
        {
          ret = rte_ring_enqueue(proto_ring->in, m);
        } while (ret != 0);
      }

      rte_pktmbuf_free(m);
    }
    // the pkts from high level (IP)
    ret = rte_ring_dequeue(proto_ring->out, (void **)bufs_out);
    if (ret == 0)
    {
      struct rte_mbuf *m = bufs_out[0];
      int index = find_arp_table(gateway_ip);
      if (index != -1)
      {
        int succ = fill_ether_hdr(m, my_addr, arp_table[index], RTE_ETHER_TYPE_IPV4);

        if (succ != 0)
          rte_pktmbuf_free(m); // maybe no arp table for this ip
        else
        {
          int res;
          do
          {
            res = rte_ring_enqueue(eth_ring->out, m);
          } while (res != 0);
        }
      }
      else
        rte_pktmbuf_free(m);
    }
    update_timer();
  }
  return 0;
}

void fill_ip_hdr(struct rte_mbuf *m, uint16_t len, uint32_t src_ip, uint32_t dst_ip, uint8_t proto_id)
{
  struct rte_ipv4_hdr *ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));

  ip_hdr->version_ihl = 0x45;
  ip_hdr->type_of_service = 0;
  ip_hdr->total_length = rte_cpu_to_be_16(len);
  // rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) + sizeof(struct new_icmp_hdr));
  ip_hdr->packet_id = 0;
  ip_hdr->fragment_offset = 0;
  ip_hdr->time_to_live = 64; // ttl = 64
  ip_hdr->next_proto_id = proto_id;
  ip_hdr->src_addr = src_ip;
  ip_hdr->dst_addr = dst_ip;

  ip_hdr->hdr_checksum = 0;
  ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);
}

uint16_t icmp_checksum(uint16_t *addr, int count)
{
  register long sum = 0;

  while (count > 1)
  {

    sum += *(unsigned short *)addr++;
    count -= 2;
  }

  if (count > 0)
  {
    sum += *(unsigned char *)addr;
  }

  while (sum >> 16)
  {
    sum = (sum & 0xffff) + (sum >> 16);
  }

  return ~sum;
}

struct rte_mbuf *encode_icmp(uint8_t *src_mac, uint8_t *dst_mac, uint32_t src_ip, uint32_t dst_ip, uint16_t id, uint16_t seqnb)
{
  struct rte_mbuf *icmp_pkt = rte_pktmbuf_alloc(mbuf_pool);
  int total_size = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct new_icmp_hdr);
  icmp_pkt->data_len = total_size;
  icmp_pkt->pkt_len = total_size;
  fill_ether_hdr(icmp_pkt, src_mac, dst_mac, RTE_ETHER_TYPE_IPV4);
  fill_ip_hdr(icmp_pkt, sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr), src_ip, dst_ip, IPPROTO_ICMP);

  struct new_icmp_hdr *icmp_hdr = rte_pktmbuf_mtod_offset(icmp_pkt, struct new_icmp_hdr *, sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
  icmp_hdr->icmp_type = RTE_IP_ICMP_ECHO_REPLY;
  icmp_hdr->icmp_code = 0;
  icmp_hdr->icmp_ident = id;
  icmp_hdr->icmp_seq_nb = seqnb;
  gettimeofday((void *)&icmp_hdr->icmp_timestamp, NULL);

  icmp_hdr->icmp_cksum = 0;
  icmp_hdr->icmp_cksum = icmp_checksum((uint16_t *)icmp_hdr, sizeof(struct new_icmp_hdr));
  return icmp_pkt;
}

void process_icmp(struct rte_mbuf *m)
{
  struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
  struct rte_ipv4_hdr *ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
  struct new_icmp_hdr *icmp_hdr = rte_pktmbuf_mtod_offset(m, struct new_icmp_hdr *, sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
  if (icmp_hdr->icmp_type == RTE_IP_ICMP_ECHO_REPLY)
  {
    // TODO
    return;
  }
  else if (icmp_hdr->icmp_type == RTE_IP_ICMP_ECHO_REQUEST)
  {
    RTE_LOG(INFO, APP, "reply an icmp \n");
    struct rte_mbuf *m = encode_icmp(my_addr, eth_hdr->src_addr.addr_bytes, ip_hdr->dst_addr, ip_hdr->src_addr, icmp_hdr->icmp_ident, icmp_hdr->icmp_seq_nb);
    int ret;
    do
    {
      rte_ring_enqueue(proto_ring->out, m);
    } while (ret != 0);
  }
}

int app_main_loop_proto()
{
  RTE_LOG(INFO, APP, "lcore %d is doing ip \n", rte_lcore_id());
  struct rte_mbuf *bufs_in[BURST_SIZE];
  struct rte_mbuf *bufs_out[BURST_SIZE];
  while (1)
  {
    int ret = rte_ring_dequeue(proto_ring->in, (void **)bufs_in);
    if (ret == 0)
    {
      struct rte_mbuf *m = bufs_in[0];
      struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
      struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
      if (ip_hdr->next_proto_id == IPPROTO_ICMP)
      {
        RTE_LOG(INFO, APP, "recv an ICMP pkt. \n");
        process_icmp(m);
      }
      else if (ip_hdr->next_proto_id == IPPROTO_UDP)
      {
        RTE_LOG(INFO, APP, "recv an UDP pkt. \n");
        process_udp(m);
      }
      else if (ip_hdr->next_proto_id == IPPROTO_TCP)
      {
        RTE_LOG(INFO, APP, "recv an TCP pkt. \n");
      }
      rte_pktmbuf_free(m);
    }

    udp_out();
  }
  return 0;
}

/* Main loop executed by each lcore */
int app_main_loop(void *)
{
  uint32_t lcore_id = rte_lcore_id();

  int port = dpdk_port_id;

  if (lcore_id == 0)
  {
    app_main_loop_nic();
  }
  else if (lcore_id == 1)
  {
    app_main_loop_eth();
  }
  else if (lcore_id == 2)
  {
    app_main_loop_proto();
  }
  else if (lcore_id == 3)
  {
    udp_server_entry(NULL);
  }
  return 0;
}

void app_finish()
{
  int ret = rte_eth_dev_stop(dpdk_port_id);
  if (ret != 0)
    rte_panic("rte_eth_dev_stop err \n");
}

void app_init_mac()
{
  rte_eth_macaddr_get(dpdk_port_id, (struct rte_ether_addr *)my_addr);
}

void app_init_ip()
{
  local_ip = MAKE_IPV4_ADDR(192, 168, 10, 66);
  gateway_ip = MAKE_IPV4_ADDR(192, 168, 10, 2);
}

void app_init()
{
  mbuf_pool = rte_pktmbuf_pool_create("app_pool", (1 << 15) - 1, 256, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
  if (mbuf_pool == NULL)
  {
    rte_panic("alloc mempool failed");
  }

  app_init_lcores();
  app_init_ports();
  app_init_rings();
  app_init_mac();
  app_init_ip();
  app_init_hash();
}

int main(int argc, char *argv[])
{
  int ret;
  ret = rte_eal_init(argc, argv);
  if (ret < 0)
    rte_panic("Cannot init EAL \n");
  argc -= ret;
  argv += ret;

  app_init();

  rte_timer_subsystem_init();
  rte_eal_mp_remote_launch(app_main_loop, NULL, CALL_MAIN);

  app_finish();
  int lcore;
  RTE_LCORE_FOREACH_WORKER(lcore)
  {
    if (rte_eal_wait_lcore(lcore) < 0)
      return -1;
  }
  // clean up the EALgg
  rte_eal_cleanup();
}