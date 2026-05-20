//
// Created by tswaehn on 12/17/24.
//

#include <stdio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_config.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ethernet_mgmt.h>
#include <zephyr/sys/printk.h>
#include <zephyr/net/socket.h>
#include <string.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/random/random.h>


#include <zephyr/logging/log.h>

#include "zephyr/net/dhcpv4.h"

/* Nordic Event Manager */

LOG_MODULE_REGISTER(network, LOG_LEVEL_DBG);


#include "network.h"

#define STACK_SIZE      2024
#define THREAD_PRIORITY K_PRIO_COOP(2)


static void network_loop();

K_THREAD_DEFINE(network_thread, STACK_SIZE, network_loop, NULL, NULL, NULL,
                THREAD_PRIORITY, K_USER, -1);


static struct net_mgmt_event_callback l2_cb;
static struct net_mgmt_event_callback l3_ipv4_cb;

static uint32_t dhcp_start_time = 0;
static bool dhcp_has_ip = false;

static void my_network_handler(struct net_mgmt_event_callback *cb,
                               uint64_t mgmt_event,
                               struct net_if *iface) {
    switch (mgmt_event) {
        case NET_EVENT_IF_UP:
            LOG_INF("Interface is up; starting DHCPv4");
            dhcp_start_time = k_uptime_get_32();
            net_dhcpv4_start(iface);
            break;
        case NET_EVENT_IF_DOWN: {
            char empty[] = "";

            LOG_INF("Interface down; network disconnected");
            break;
        }
        case NET_EVENT_IPV4_ADDR_DEL: {

            LOG_INF("IPv4 address removed; network disconnected");
            break;
        }
        case NET_EVENT_IPV4_ADDR_ADD: {
            dhcp_has_ip = true;
            int i = 0;
            struct net_if_config *cfg;

            cfg = net_if_get_config(iface);
            if (!cfg) {
                return;
            }

            for (i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
                char buf[NET_IPV4_ADDR_LEN];

                /* For DHCP-assigned IPv4, type is NET_ADDR_DHCP */
                if (cfg->ip.ipv4->unicast[i].ipv4.addr_type != NET_ADDR_DHCP) {
                    continue;
                }

                LOG_INF("Your address: %s",
                        net_addr_ntop(AF_INET,
                            &cfg->ip.ipv4->unicast[i].ipv4.address.in_addr,
                            buf, sizeof(buf)));

                LOG_INF("Your netmask: %s",
                        net_addr_ntop(AF_INET,
                            &cfg->ip.ipv4->unicast[i].netmask,
                            buf, sizeof(buf)));
            }
            break;
        }
        default:
            break;
    }
}

static void generate_local_mac(uint8_t mac[6])
{
	sys_rand_get(mac, 6);

	/* Set locally administered bit and clear multicast bit */
	mac[0] |= 0x02;
	mac[0] &= 0xFE;
}


static void print_mac(const uint8_t mac[6])
{
	LOG_INF("Ethernet MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
	       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}


void start_network() {
    /* Register for interface and IPv4 address events to manage DHCP/display */

    net_mgmt_init_event_callback(&l2_cb, my_network_handler, NET_EVENT_IF_BASE);
    net_mgmt_add_event_callback(&l2_cb);

    net_mgmt_init_event_callback(&l3_ipv4_cb, my_network_handler, (NET_EVENT_IPV4_BASE | NET_MGMT_COMMAND_MASK));
    net_mgmt_add_event_callback(&l3_ipv4_cb);

    k_thread_start(network_thread);
}

#define LISTEN_PORT 4242
#define BACKLOG 5
static char echo_buffer[50000];


void network_loop() {
    int server_fd, client_fd;
    struct sockaddr_in addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    struct net_if *iface;

    printk("Starting Zephyr Network...\n");

    /* Get the default network interface */
    iface = net_if_get_default();

    if (!iface) {
        printk("No network interface found!\n");
        while (1) {
            k_sleep(K_SECONDS(10));
        }
    }

    const struct net_in_addr static_addr = {
        .s4_addr = {192, 168, 0, 100 }
    };

    const struct net_in_addr netmask = {
        .s4_addr = {255, 255, 255, 0 }
    };

    uint32_t retry_count = 30;
    while (1) {
        printk("waiting for DHCP IP\n");
        k_sleep(K_SECONDS(1));

        if (dhcp_has_ip) {
            printk("DHCP IP acquired\n");
            break;
        }

        retry_count--;
        if (retry_count==0) {
            printk("DHCP timed out; using static IP\n");

            iface = net_if_get_default();

            net_if_ipv4_addr_add(iface, &static_addr, NET_ADDR_MANUAL, 0);
            net_if_ipv4_set_netmask_by_addr(iface, &static_addr, &netmask);

            char buf[NET_IPV4_ADDR_LEN];

            LOG_INF("Your address: %s", net_addr_ntop(AF_INET, &static_addr,
                        buf, sizeof(buf)));

            LOG_INF("Your netmask: %s",
                    net_addr_ntop(AF_INET,
                        &netmask,
                        buf, sizeof(buf)));

            break;
        }
    }

    LOG_INF("starting socket");

    while (true) {
        // Create a TCP socket
        server_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server_fd < 0) {
            LOG_ERR("Failed to create socket");
            return;
        }

        addr.sin_family = AF_INET;
        addr.sin_port = htons(LISTEN_PORT);
        addr.sin_addr.s_addr = INADDR_ANY;

        // Bind the socket
        if (zsock_bind(server_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
            LOG_ERR("Bind failed");
            zsock_close(server_fd);
            return;
        }

        // Listen for incoming connections
        if (zsock_listen(server_fd, BACKLOG) < 0) {
            LOG_ERR("Listen failed");
            zsock_close(server_fd);
            return;
        }

        LOG_INF("TCP Echo Server listening on port %d", LISTEN_PORT);

        while (1) {
            // Accept a client connection
            client_fd = zsock_accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
            if (client_fd < 0) {
                LOG_ERR("Accept failed");
                continue;
            }

            LOG_INF("Client connected");

            while (1) {
                int received = zsock_recv(client_fd, echo_buffer, sizeof(echo_buffer) - 1, 0);
                if (received <= 0) {
                    LOG_INF("Client disconnected");
                    break;
                }

                echo_buffer[received] = '\0';
                LOG_INF("Received: %s", echo_buffer);

                // Echo the message back
                zsock_send(client_fd, echo_buffer, received, 0);
            }

            zsock_close(client_fd);
        }

        zsock_close(server_fd);
    }
}

