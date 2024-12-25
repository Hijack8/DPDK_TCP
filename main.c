#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>
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

#define APP_MAX_LCORES 10
#define BURST_SIZE 10
#define ENABLE_SEND 1

const int dpdk_port_id = 0;
const int n_lcores = 1;
int lcores[10];

void app_init_lcores() {
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

void app_init_ports(struct rte_mempool *mbuf_pool) {
	uint16_t nb_sys_ports= rte_eth_dev_count_avail(); //
	if (nb_sys_ports == 0) {
		rte_exit(EXIT_FAILURE, "No Supported eth found\n");
	}

	struct rte_eth_dev_info dev_info;
	rte_eth_dev_info_get(dpdk_port_id, &dev_info); //
	
	const int num_rx_queues = 1;
	const int num_tx_queues = 1;
	struct rte_eth_conf port_conf = port_conf_default;
	rte_eth_dev_configure(dpdk_port_id, num_rx_queues, num_tx_queues, &port_conf);


	if (rte_eth_rx_queue_setup(dpdk_port_id, 0 , 1024, 
		rte_eth_dev_socket_id(dpdk_port_id),NULL, mbuf_pool) < 0) {

		rte_exit(EXIT_FAILURE, "Could not setup RX queue\n");

	}
	
#if ENABLE_SEND
	struct rte_eth_txconf txq_conf = dev_info.default_txconf;
	txq_conf.offloads = port_conf.rxmode.offloads;
	if (rte_eth_tx_queue_setup(dpdk_port_id, 0 , 1024, 
		rte_eth_dev_socket_id(dpdk_port_id), &txq_conf) < 0) {
		
		rte_exit(EXIT_FAILURE, "Could not setup TX queue\n");
		
	}
#endif

	if (rte_eth_dev_start(dpdk_port_id) < 0 ) {
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

void print_pkt(struct rte_mbuf* m) {
  printf("recv a pkt :\n");
  struct rte_ether_hdr *hdr;
  hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
  print_addr(&hdr -> src_addr);
  print_addr(&hdr -> dst_addr);
  printf("==========:\n");
}

/* Main loop executed by each lcore */
int app_main_loop(void *)
{
	uint32_t lcore_id = rte_lcore_id();

	int port = dpdk_port_id;

	struct rte_mbuf *bufs[BURST_SIZE];
	while (1)
	{
		int nb_rx = rte_eth_rx_burst(port, 0, bufs, 8);

		if (nb_rx == 0)
			continue;
    for (int i = 0; i < nb_rx; i ++) {
      print_pkt(bufs[i]);
      rte_pktmbuf_free(bufs[i]);
    }
	}
	return 0;
}


void app_finish() {
	int ret = rte_eth_dev_stop(dpdk_port_id);
	if (ret != 0)
		printf("rte_eth_dev_stop err \n");
}

void app_init() {
  struct rte_mempool* mbuf_pool = rte_pktmbuf_pool_create("app_pool", (1 << 15) - 1, 256, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
  if (mbuf_pool == NULL) {
    rte_panic("alloc mempool failed");
  }

  app_init_lcores();
  app_init_ports(mbuf_pool);
  
}


int main(int argc, char* argv[]) {
  int ret;  
  ret = rte_eal_init(argc, argv);
  if (ret < 0) 
      rte_panic("Cannot init EAL \n");
  argc -= ret;
  argv += ret;

  app_init();

  rte_eal_mp_remote_launch(app_main_loop, NULL, CALL_MAIN);

  app_finish();

  rte_eal_mp_wait_lcore();
  // clean up the EAL
  rte_eal_cleanup();
}
