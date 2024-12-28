#include "main.h"

#define UDP_APP_RECV_BUFFER_SIZE 128
#define MAX_FD 1024
int bitmap[MAX_FD];

extern struct localhost *lhost;
extern uint8_t *my_addr;
extern struct ring_buffer *proto_ring;
extern struct rte_mempool *mbuf_pool;

int get_fd_from_bitmap()
{
    for (int i = 0; i < MAX_FD; i++)
    {
        if (bitmap[i] == 0)
        {
            bitmap[i] = 1;
            return i;
        }
    }
    return -1;
}

// socket
int nsocket(int domain, int type, int protocol)
{
    int fd = get_fd_from_bitmap();
    if (fd == -1)
        rte_panic("no available fd.");
    struct localhost *lh = (struct localhost *)rte_malloc("lh", sizeof(struct localhost), 0);

    lh->fd = fd;
    if (type == SOCK_DGRAM)
        lh->protocol = IPPROTO_UDP;
    else if (type == SOCK_STREAM)
        lh->protocol = IPPROTO_TCP;

    lh->rcvbuf = rte_ring_create("recv buffer", RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (lh->rcvbuf == NULL)
    {
        rte_free(lh);
        return -1;
    }
    lh->sndbuf = rte_ring_create("send buffer", RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (lh->sndbuf == NULL)
    {
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

int nbind(int sockfd, const struct sockaddr *addr,
          socklen_t addrlen)
{
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
                  struct sockaddr *src_addr, socklen_t *addrlen)
{
    struct localhost *host = find_host_by_fd(lhost, sockfd);
    if (host == NULL)
        return -1;
    struct rte_ring *ring = host->rcvbuf;
    struct offload *ofl;
    pthread_mutex_lock(&host->mutex);
    int ret;
    while ((ret = rte_ring_dequeue(ring, (void **)&ofl)) != 0)
    {
        pthread_cond_wait(&host->cond, &host->mutex);
    }
    pthread_mutex_unlock(&host->mutex);
    struct sockaddr_in *srcaddr = (struct sockaddr_in *)src_addr;
    memcpy(&(srcaddr->sin_addr.s_addr), &ofl->sip, sizeof(uint32_t));
    srcaddr->sin_port = ofl->sport;

    if (ofl->length <= len)
    {
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
    do
    {
        ret = rte_ring_enqueue(host->rcvbuf, ofl);
    } while (ret != 0);

    return len;
}

ssize_t nsendto(int sockfd, const void *buf, size_t len, int flags,
                const struct sockaddr *dest_addr, socklen_t addrlen)
{
    struct localhost *host = find_host_by_fd(lhost, sockfd);
    if (host == NULL)
        return -1;

    struct offload *ofl = (struct offload *)rte_malloc("ofl", sizeof(struct offload), 0);
    struct sockaddr_in *d_addr = (struct sockaddr_in *)rte_malloc("sockaddr", sizeof(struct sockaddr_in), 0);
    rte_memcpy(d_addr, dest_addr, sizeof(struct sockaddr_in));
    ofl->sip = host->localip;
    ofl->sport = host->localport;
    ofl->dip = d_addr->sin_addr.s_addr;
    ofl->dport = d_addr->sin_port;
    ofl->length = len;

    ofl->data = rte_malloc("data", len, 0);
    if (ofl->data == NULL)
    {
        rte_free(ofl);
        return -1;
    }
    rte_memcpy(ofl->data, buf, len);
    int ret;
    do
    {
        ret = rte_ring_enqueue(host->sndbuf, ofl);
    } while (ret != 0);

    return len;
}

int nclose(int fd)
{
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

int udp_server_entry(__attribute__((unused)) void *arg)
{
    RTE_LOG(INFO, APP, "lcore %d is doing udp server \n", rte_lcore_id());
    int connfd = nsocket(AF_INET, SOCK_DGRAM, 0);

    if (connfd == -1)
    {
        printf("sockfd failed\n");
        return -1;
    }

    struct sockaddr_in localaddr, clientaddr; // struct sockaddr
    memset(&localaddr, 0, sizeof(struct sockaddr_in));

    localaddr.sin_port = htons(8889);
    localaddr.sin_family = AF_INET;
    localaddr.sin_addr.s_addr = inet_addr("192.168.10.66"); // 0.0.0.0

    nbind(connfd, (struct sockaddr *)&localaddr, sizeof(localaddr));

    char buffer[UDP_APP_RECV_BUFFER_SIZE] = {0};
    socklen_t addrlen = sizeof(clientaddr);
    while (1)
    {

        if (nrecvfrom(connfd, buffer, UDP_APP_RECV_BUFFER_SIZE, 0,
                      (struct sockaddr *)&clientaddr, &addrlen) < 0)
        {

            continue;
        }
        else
        {

            RTE_LOG(INFO, APP, "recv from %s:%d, data:%s\n", inet_ntoa(clientaddr.sin_addr),
                    ntohs(clientaddr.sin_port), buffer);
            nsendto(connfd, buffer, strlen(buffer), 0,
                    (struct sockaddr *)&clientaddr, sizeof(clientaddr));
        }
    }

    nclose(connfd);
}

int process_udp(struct rte_mbuf *m)
{
    struct rte_ipv4_hdr *iphdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
    if (iphdr->next_proto_id != IPPROTO_UDP)
        return -1;
    struct rte_udp_hdr *udphdr = (struct rte_udp_hdr *)(iphdr + 1);

    struct localhost *host = find_host_by_ip_port(lhost, (iphdr->dst_addr), (udphdr->dst_port));
    print_ip_port(rte_be_to_cpu_32(iphdr->dst_addr), rte_be_to_cpu_16(udphdr->dst_port));
    if (host == NULL)
        return -1;

    struct offload *ofl = rte_malloc("offload", sizeof(struct offload), 0);
    if (ofl == NULL)
        return -1;

    ofl->dip = (iphdr->dst_addr);
    ofl->sip = (iphdr->src_addr);
    ofl->sport = (udphdr->src_port);
    ofl->dport = (udphdr->dst_port);
    ofl->protocol = IPPROTO_UDP;
    int len = rte_be_to_cpu_16(udphdr->dgram_len);
    ofl->data = rte_malloc("data", len, 0);
    if (ofl->data == NULL)
    {
        rte_free(ofl);
        return -1;
    }
    rte_memcpy(ofl->data, (unsigned char *)(udphdr + 1), len);
    ofl->length = len;

    pthread_mutex_lock(&host->mutex);
    int ret;
    do
    {
        ret = rte_ring_enqueue(host->rcvbuf, ofl);
    } while (ret != 0);
    pthread_cond_broadcast(&host->cond);
    pthread_mutex_unlock(&host->mutex);
    return ret;
}

struct rte_mbuf *encode_udp(struct offload *ofl)
{
    struct rte_mbuf *m = rte_pktmbuf_alloc(mbuf_pool);
    int total_len = ofl->length + sizeof(struct rte_udp_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_ether_hdr);
    if (!m)
    {
        rte_exit(EXIT_FAILURE, "rte_pktmbuf_alloc\n");
    }
    m->pkt_len = total_len;
    m->data_len = total_len;

    fill_ip_hdr(m, total_len - sizeof(struct rte_ether_hdr), ofl->sip, ofl->dip, IPPROTO_UDP);

    struct rte_udp_hdr *udp_hdr = rte_pktmbuf_mtod_offset(m, struct rte_udp_hdr *, sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
    struct rte_ipv4_hdr *ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
    udp_hdr->dst_port = (ofl->dport);
    udp_hdr->src_port = (ofl->sport);
    udp_hdr->dgram_len = rte_cpu_to_be_16(ofl->length);
    udp_hdr->dgram_cksum = 0;
    rte_memcpy((unsigned char *)(udp_hdr + 1), ofl->data, ofl->length);

    udp_hdr->dgram_cksum = rte_ipv4_udptcp_cksum(ip_hdr, udp_hdr);
    return m;
}

void udp_out(void)
{
    for (struct localhost *hosti = lhost; hosti != NULL; hosti = hosti->next)
    {
        if (hosti->protocol != IPPROTO_UDP)
        {
            continue;
        }
        struct offload *ofl;
        int ret = rte_ring_dequeue(hosti->sndbuf, (void **)&ofl);
        if (ret != 0)
            continue;
        struct rte_mbuf *m = encode_udp(ofl);
        rte_free(ofl);

        do
        {
            ret = rte_ring_enqueue(proto_ring->out, m);
        } while (ret != 0);
    }
}