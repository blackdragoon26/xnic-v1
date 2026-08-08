// SPDX-License-Identifier: BSD-3-Clause
#include <getopt.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_mbuf.h>

#define BURST_SIZE 32
#define NUM_MBUFS 8191
#define RX_DESC 128
#define TX_DESC 128

static volatile sig_atomic_t force_quit;

static void handle_signal(int signum)
{
	(void)signum;
	force_quit = 1;
}

static void swap_l2(struct rte_mbuf *mbuf)
{
	struct rte_ether_hdr *header;
	struct rte_ether_addr temporary;

	if (rte_pktmbuf_data_len(mbuf) < sizeof(*header))
		return;
	header = rte_pktmbuf_mtod(mbuf, struct rte_ether_hdr *);
	rte_ether_addr_copy(&header->dst_addr, &temporary);
	rte_ether_addr_copy(&header->src_addr, &header->dst_addr);
	rte_ether_addr_copy(&temporary, &header->src_addr);
}

static int parse_uint(const char *text, unsigned int *value)
{
	char *end;
	unsigned long parsed;

	parsed = strtoul(text, &end, 0);
	if (!*text || *end || parsed > UINT32_MAX)
		return -1;
	*value = (unsigned int)parsed;
	return 0;
}

int main(int argc, char **argv)
{
	struct rte_eth_conf port_conf = {0};
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf tx_conf;
	struct rte_mempool *pool;
	struct rte_mbuf *packets[BURST_SIZE];
	uint64_t rx_packets = 0;
	uint64_t tx_packets = 0;
	uint64_t dropped = 0;
	unsigned int idle_limit = 0;
	unsigned int idle_polls = 0;
	unsigned int port_value = 0;
	unsigned int tx_offer_limit = 0;
	uint16_t rx_desc = RX_DESC;
	uint16_t tx_desc = TX_DESC;
	uint16_t port;
	int option;
	int result;

	result = rte_eal_init(argc, argv);
	if (result < 0)
		rte_exit(EXIT_FAILURE, "EAL initialization failed\n");
	argc -= result;
	argv += result;
	optind = 1;
	while ((option = getopt(argc, argv, "p:i:t:")) != -1) {
		switch (option) {
		case 'p':
			if (parse_uint(optarg, &port_value))
				rte_exit(EXIT_FAILURE, "invalid port: %s\n", optarg);
			break;
		case 'i':
			if (parse_uint(optarg, &idle_limit))
				rte_exit(EXIT_FAILURE, "invalid idle limit: %s\n", optarg);
			break;
		case 't':
			if (parse_uint(optarg, &tx_offer_limit))
				rte_exit(EXIT_FAILURE, "invalid TX offer limit: %s\n",
					 optarg);
			break;
		default:
			rte_exit(EXIT_FAILURE,
				 "usage: EAL_ARGS -- [-p port] [-i idle_polls] "
				 "[-t tx_offer_limit]\n");
		}
	}
	if (port_value > UINT16_MAX)
		rte_exit(EXIT_FAILURE, "port is outside uint16 range\n");
	port = (uint16_t)port_value;
	if (!rte_eth_dev_is_valid_port(port))
		rte_exit(EXIT_FAILURE, "DPDK port %u is unavailable\n", port);

	pool = rte_pktmbuf_pool_create("XNIC_MBUF_POOL", NUM_MBUFS, 0, 0,
				       RTE_MBUF_DEFAULT_BUF_SIZE,
				       rte_socket_id());
	if (!pool)
		rte_exit(EXIT_FAILURE, "mempool creation failed\n");
	result = rte_eth_dev_info_get(port, &dev_info);
	if (result)
		rte_exit(EXIT_FAILURE, "port info failed: %d\n", result);
	result = rte_eth_dev_configure(port, 1, 1, &port_conf);
	if (result)
		rte_exit(EXIT_FAILURE, "port configure failed: %d\n", result);
	result = rte_eth_dev_adjust_nb_rx_tx_desc(port, &rx_desc, &tx_desc);
	if (result)
		rte_exit(EXIT_FAILURE, "descriptor adjustment failed: %d\n", result);
	result = rte_eth_rx_queue_setup(port, 0, rx_desc,
					rte_eth_dev_socket_id(port), NULL, pool);
	if (result)
		rte_exit(EXIT_FAILURE, "RX queue setup failed: %d\n", result);
	tx_conf = dev_info.default_txconf;
	tx_conf.offloads = port_conf.txmode.offloads;
	result = rte_eth_tx_queue_setup(port, 0, tx_desc,
					rte_eth_dev_socket_id(port), &tx_conf);
	if (result)
		rte_exit(EXIT_FAILURE, "TX queue setup failed: %d\n", result);
	result = rte_eth_dev_start(port);
	if (result)
		rte_exit(EXIT_FAILURE, "port start failed: %d\n", result);
	rte_eth_promiscuous_enable(port);

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	printf("xnic-forwarder port=%u idle_limit=%u\n", port, idle_limit);
	while (!force_quit && (!idle_limit || idle_polls < idle_limit)) {
		uint16_t received;
		uint16_t offered;
		uint16_t sent;
		uint16_t index;

		received = rte_eth_rx_burst(port, 0, packets, BURST_SIZE);
		if (!received) {
			idle_polls++;
			rte_delay_us_sleep(1000);
			continue;
		}
		idle_polls = 0;
		rx_packets += received;
		for (index = 0; index < received; index++)
			swap_l2(packets[index]);
		offered = received;
		if (tx_offer_limit && offered > tx_offer_limit)
			offered = (uint16_t)tx_offer_limit;
		sent = rte_eth_tx_burst(port, 0, packets, offered);
		tx_packets += sent;
		if (sent < received) {
			dropped += received - sent;
			for (index = sent; index < received; index++)
				rte_pktmbuf_free(packets[index]);
		}
	}

	rte_eth_dev_stop(port);
	rte_eth_dev_close(port);
	rte_eal_cleanup();
	printf("XNIC_DPDK_RESULT rx=%" PRIu64 " tx=%" PRIu64
	       " dropped=%" PRIu64 " signal=%d\n",
	       rx_packets, tx_packets, dropped, force_quit != 0);
	return dropped ? EXIT_FAILURE : EXIT_SUCCESS;
}
