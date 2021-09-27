/*
 * wui.h
 * \brief main interface functions for Web User Interface (WUI) thread
 *
 *  Created on: Dec 12, 2019
 *      Author: joshy
  *  Modify on 09/17/2021
 *      Author: Marek Mosna <marek.mosna[at]prusa3d.cz>
*/

#include "wui.h"

#include "marlin_client.h"
#include "wui_api.h"
#include "ethernetif.h"
#include "stm32f4xx_hal.h"

#include "sntp_client.h"
#include "dbg.h"

#include "lwip/altcp_tcp.h"
#include "esp_tcp.h"
#include "httpd.h"
#include "main.h"

#include "netdev.h"

#include <string.h>
#include "eeprom.h"
#include "variant8.h"

#include "lwip/netif.h"
#include "lwip/tcpip.h"

#define LOOP_EVT_TIMEOUT           500UL
#define IS_TIME_TO_CHECK_ESP(time) (((time) % 1000) == 0)

extern RNG_HandleTypeDef hrng;

osMessageQDef(networkMbox, 16, NULL);
osMessageQId networkMbox_id;

static variant8_t prusa_link_api_key;

const char *wui_generate_api_key(char *api_key, uint32_t length) {
    static char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789,.-#'?!";
    const uint32_t charset_length = sizeof(charset) / sizeof(char);
    uint32_t i = 0;

    while (i < length - 1) {
        uint32_t random = 0;
        HAL_StatusTypeDef status = HAL_RNG_GenerateRandomNumber(&hrng, &random);
        if (HAL_OK == status) {
            api_key[i++] = charset[random % charset_length];
        }
    }
    api_key[i] = 0;
    return api_key;
}

void wui_store_api_key(char *api_key, uint32_t length) {
    variant8_t *p_prusa_link_api_key = &prusa_link_api_key;
    variant8_done(&p_prusa_link_api_key);
    prusa_link_api_key = variant8_init(VARIANT8_PCHAR, length, api_key);
    eeprom_set_var(EEVAR_PL_API_KEY, prusa_link_api_key);
}

void StartWebServerTask(void const *argument) {
    uint32_t esp_check_counter = 1;
    _dbg("wui starts");

    networkMbox_id = osMessageCreate(osMessageQ(networkMbox), NULL);
    if (networkMbox_id == NULL) {
        _dbg("networkMbox was not created");
        return;
    }

    struct netif *wlif = malloc(sizeof(struct netif));
    memset(wlif, 0, sizeof(struct netif));

    //esp_lwip_init(wlif);
    //_dbg("ESP LWIP INIT DONE");

    static ip4_addr_t ipaddr;
    static ip4_addr_t netmask;
    static ip4_addr_t gw;

    ipaddr.addr = 0;
    netmask.addr = 0;
    gw.addr = 0;

    _dbg("WLIF add");

    extern uint8_t esp_lwip_init(struct netif * netif);

    struct netif *ret = netif_add(wlif, &ipaddr, &netmask, &gw, NULL,
        &esp_lwip_init, &tcpip_input);

    netdev_init();

    prusa_link_api_key = eeprom_get_var(EEVAR_PL_API_KEY);
    if (!strcmp(variant8_get_pch(prusa_link_api_key), "")) {
        char api_key[PL_API_KEY_SIZE] = { 0 };
        wui_generate_api_key(api_key, PL_API_KEY_SIZE);
        wui_store_api_key(api_key, PL_API_KEY_SIZE);
    }

    if (variant8_get_ui8(eeprom_get_var(EEVAR_PL_RUN)) == 1) {
        httpd_init();
    }

    for (;;) {
        osEvent evt = osMessageGet(networkMbox_id, LOOP_EVT_TIMEOUT);
        if (evt.status == osEventMessage) {
            switch (evt.value.v) {
            case EVT_TCPIP_INIT_FINISHED:
                netdev_set_up(NETDEV_ETH_ID);
                break;
            case EVT_LWESP_INIT_FINISHED:
                netdev_set_up(NETDEV_ESP_ID);
                break;
            case EVT_NETDEV_INIT_FINISHED(NETDEV_ETH_ID, 0):
                if (netdev_get_ip_obtained_type() == NETDEV_DHCP) {
                    netdev_set_dhcp(NETDEV_ETH_ID);
                } else {
                    netdev_set_static(NETDEV_ETH_ID);
                }
                break;
            case EVT_NETDEV_INIT_FINISHED(NETDEV_ESP_ID, 0):
                if (netdev_get_ip_obtained_type() == NETDEV_DHCP) {
                    netdev_set_dhcp(NETDEV_ESP_ID);
                } else {
                    netdev_set_static(NETDEV_ESP_ID);
                }
                break;
            default:
                break;
            }
        }

        if (netdev_get_status(NETDEV_ESP_ID) == NETDEV_NETIF_DOWN && IS_TIME_TO_CHECK_ESP(esp_check_counter * LOOP_EVT_TIMEOUT)) {
            netdev_check_link(NETDEV_ESP_ID);
            esp_check_counter = 0;
        } else {
            ++esp_check_counter;
        }
    }
}

const char *wui_get_api_key() {
    return variant8_get_pch(prusa_link_api_key);
}

struct altcp_pcb *prusa_alloc(void *arg, uint8_t ip_type) {
    uint32_t active_device_id = netdev_get_active_id();
    if (active_device_id == NETDEV_ETH_ID)
        return altcp_tcp_new_ip_type(ip_type);
    else if (active_device_id == NETDEV_ESP_ID) {
        return altcp_esp_new_ip_type(ip_type);
    } else {
        return NULL;
    }

    /*
    struct netif *wlif = malloc(sizeof(struct netif));
    memset(wlif, 0, sizeof(struct netif));

    //esp_lwip_init(wlif);
    //_dbg("ESP LWIP INIT DONE");

    static ip4_addr_t ipaddr;
    static ip4_addr_t netmask;
    static ip4_addr_t gw;

    ipaddr.addr = 0;
    netmask.addr = 0;
    gw.addr = 0;

    _dbg("WLIF add");
    struct netif *ret = netif_add(wlif, &ipaddr, &netmask, &gw, NULL, &esp_lwip_init, &tcpip_input);

    if(ret) {
        _dbg("WLIF ADDED");
    } else {
        _dbg("WLIF FAILED TO ADD");
        return;
    }

    // Registers the default network interface
    netif_set_default(wlif);
    _dbg("WLIF now default");

    netif_set_up(wlif);
    _dbg("WLIF now up");

    dhcp_start(wlif);
    _dbg("WLIF running DHCP");


    osDelay(5000);


    // Test throughtput with TCP connection
    _dbg("Create TCP socket");
    int sockfd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    _dbg("Sock FD: %d", sockfd);
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(struct sockaddr_in));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("10.42.0.1");
    servaddr.sin_port = htons(5005);

    if (lwip_connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0) {
        _dbg("TCP connection failed...");
    }


    _dbg("Sending 1MB to the TCP connection");
    #define PACKET_SIZE (256)
    char *buff[PACKET_SIZE];
    for(uint i = 0; i < 4096; i++) {
        _dbg("Sending packet %d", i);
        lwip_write(sockfd, buff, PACKET_SIZE);
    }
    lwip_close(sockfd);

    for(uint i = 0;; i++) {
        osDelay(1000);
    }

*/
}
