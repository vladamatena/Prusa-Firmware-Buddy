/*
 * wui.h
 * \brief main interface functions for Web User Interface (WUI) thread
 *
 *  Created on: Dec 12, 2019
 *      Author: joshy
 */

#include "wui.h"
#include "wui_vars.h"
#include "marlin_client.h"
#include "wui_api.h"
#include "lwip.h"
#include "ethernetif.h"
#include <string.h>
#include "sntp_client.h"
#include "httpc/httpc.h"
#include "dbg.h"
#include "lwesp/lwesp.h"
#include "stm32_port.h"

osThreadId httpcTaskHandle;
#include "sockets.h" // LwIP sockets wrapper
#include "lwesp_sockets.h"

#define WUI_NETIF_SETUP_DELAY  1000
#define WUI_COMMAND_QUEUE_SIZE WUI_WUI_MQ_CNT // maximal number of messages at once in WUI command messageQ

// WUI thread mutex for updating marlin vars
osMutexDef(wui_thread_mutex);
osMutexId(wui_thread_mutex_id);

static marlin_vars_t *wui_marlin_vars;
wui_vars_t wui_vars;                              // global vriable for data relevant to WUI
static char wui_media_LFN[FILE_NAME_MAX_LEN + 1]; // static buffer for gcode file name

extern UART_HandleTypeDef huart6;

static void wui_marlin_client_init(void) {
    wui_marlin_vars = marlin_client_init(); // init the client
    // force update variables when starts
    marlin_client_set_event_notify(MARLIN_EVT_MSK_DEF - MARLIN_EVT_MSK_FSM, NULL);
    marlin_client_set_change_notify(MARLIN_VAR_MSK_DEF | MARLIN_VAR_MSK_WUI, NULL);
    if (wui_marlin_vars) {
        wui_marlin_vars->media_LFN = wui_media_LFN;
    }
}

static void sync_with_marlin_server(void) {
    if (wui_marlin_vars) {
        marlin_client_loop();
    } else {
        return;
    }
    osMutexWait(wui_thread_mutex_id, osWaitForever);
    for (int i = 0; i < 4; i++) {
        wui_vars.pos[i] = wui_marlin_vars->pos[i];
    }
    wui_vars.temp_nozzle = wui_marlin_vars->temp_nozzle;
    wui_vars.temp_bed = wui_marlin_vars->temp_bed;
    wui_vars.target_nozzle = wui_marlin_vars->target_nozzle;
    wui_vars.target_bed = wui_marlin_vars->target_bed;
    wui_vars.fan_speed = wui_marlin_vars->fan_speed;
    wui_vars.print_speed = wui_marlin_vars->print_speed;
    wui_vars.flow_factor = wui_marlin_vars->flow_factor;
    wui_vars.print_dur = wui_marlin_vars->print_duration;
    wui_vars.sd_precent_done = wui_marlin_vars->sd_percent_done;
    wui_vars.sd_printing = wui_marlin_vars->sd_printing;
    wui_vars.time_to_end = wui_marlin_vars->time_to_end;
    wui_vars.print_state = wui_marlin_vars->print_state;
    if (marlin_change_clr(MARLIN_VAR_FILENAME)) {
        strlcpy(wui_vars.gcode_name, wui_marlin_vars->media_LFN, FILE_NAME_MAX_LEN);
    }

    osMutexRelease(wui_thread_mutex_id);
}

static void update_eth_changes(void) {
    wui_lwip_link_status(); // checks plug/unplug status and take action
    wui_lwip_sync_gui_lan_settings();
}

void socket_listen_test_lwip() {
    _dbg("LWIP TCP HELLO SERVER TEST\n");
    int listenfd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        _dbg("FAILED TO CREATE LISTENING SOCKET\n");
        return;
    }
    _dbg("SOCKET CREATED\n");

    struct sockaddr_in servaddr, clientaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(5000);
  
    if ((lwip_bind(listenfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) {
        _dbg("BIND FAILED\n");
        return;
    }
    
    _dbg("SOCKET BOUND\n");
  
    if ((lwip_listen(listenfd, 5)) != 0) {
        printf("LISTEN FAILED\n");
        return;
    }
    
    _dbg("SOCKET LISTENING\n");
    
    socklen_t len = sizeof(clientaddr);
  
    int connectionfd = lwip_accept(listenfd, (struct sockaddr *)&clientaddr, &len);
    if (connectionfd < 0) {
        printf("ACCEPT FAILED\n");
        return;
    }
    
    _dbg("CONNECTION ACCEPTED\n");

    const char buff[] = "Hello\n";
    lwip_write(connectionfd, buff, sizeof(buff));
  
    lwip_close(connectionfd);
  
    // After chatting close the socket
    lwip_close(listenfd);

    _dbg("CONNECTION CLOSED\n");
}

void socket_listen_test_lwesp() {
    _dbg("LWESP TCP HELLO SERVER TEST\n");
    int listenfd = lwesp_socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        _dbg("FAILED TO CREATE LISTENING SOCKET\n");
        return;
    }
    _dbg("SOCKET CREATED\n");

    struct sockaddr_in servaddr, clientaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(5000);
  
    if ((lwesp_bind(listenfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) {
        _dbg("BIND FAILED\n");
        return;
    }
    
    _dbg("SOCKET BOUND\n");
  
    if ((lwesp_listen(listenfd, 5)) != 0) {
        _dbg("LISTEN FAILED\n");
        return;
    }
    
    _dbg("SOCKET LISTENING\n");
    
    socklen_t len = sizeof(clientaddr);
  
    int connectionfd = lwesp_accept(listenfd, (struct sockaddr *)&clientaddr, &len);
    if (connectionfd < 0) {
        _dbg("ACCEPT FAILED\n");
        return;
    }
    
    _dbg("CONNECTION ACCEPTED\n");

    const char buff[] = "Hello\n";
    lwesp_write(connectionfd, buff, sizeof(buff));
  
    lwesp_close(connectionfd);
  
    // After chatting close the socket
    lwesp_close(listenfd);

    _dbg("CONNECTION CLOSED\n");
}

void netconn_listen_test() {
    lwespr_t res;
    lwesp_netconn_p server, client;
    lwesp_pbuf_p p;

    /* Create netconn for server */
    server = lwesp_netconn_new(LWESP_NETCONN_TYPE_TCP);
    if (server == NULL) {
        _dbg("Cannot create server netconn!\r\n");
    }

    /* Bind it to port 5000 */
    res = lwesp_netconn_bind(server, 5000);
    if (res != lwespOK) {
        _dbg("Cannot bind server\r\n");
        goto out;
    }

    /* Start listening for incoming connections with maximal 1 client */
    res = lwesp_netconn_listen_with_max_conn(server, 1);
    if (res != lwespOK) {
        _dbg("Cannot listen server\r\n");
        goto out;
    }

    /* Unlimited loop */
    while (1) {
        /* Accept new client */
        res = lwesp_netconn_accept(server, &client);
        if (res != lwespOK) {
            break;
        }
        _dbg("New client accepted!\r\n");
        while (1) {
            /* Receive data */
            res = lwesp_netconn_receive(client, &p);
            if (res == lwespOK) {
                _dbg("Data received!\r\n");
                lwesp_pbuf_free(p);
            } else {
                _dbg("Netconn receive returned: %d\r\n", (int)res);
                if (res == lwespCLOSED) {
                    _dbg("Connection closed by client\r\n");
                    break;
                }
            }
        }
        /* Delete client */
        if (client != NULL) {
            lwesp_netconn_delete(client);
            client = NULL;
        }
    }
    /* Delete client */
    if (client != NULL) {
        lwesp_netconn_delete(client);
        client = NULL;
    }

out:
    printf("Terminating netconn thread!\r\n");
    if (server != NULL) {
        lwesp_netconn_delete(server);
    }
    lwesp_sys_thread_terminate(NULL);
}

#define NETCONN_HOST        "10.42.0.1"
#define NETCONN_PORT        5000

static const char
request_header[] = ""
                   "GET / HTTP/1.1\r\n"
                   "Host: " NETCONN_HOST "\r\n"
                   "Connection: close\r\n"
                   "\r\n";


void netconn_client_test() {
    lwespr_t res;
    lwesp_pbuf_p pbuf;
    lwesp_netconn_p client;

    /*
     * First create a new instance of netconn
     * connection and initialize system message boxes
     * to accept received packet buffers
     */
    client = lwesp_netconn_new(LWESP_NETCONN_TYPE_TCP);
    if (client != NULL) {
        /*
         * Connect to external server as client
         * with custom NETCONN_CONN_HOST and CONN_PORT values
         *
         * Function will block thread until we are successfully connected (or not) to server
         */
        res = lwesp_netconn_connect(client, NETCONN_HOST, NETCONN_PORT);
        if (res == lwespOK) {                     /* Are we successfully connected? */
            _dbg("Connected to " NETCONN_HOST "\r\n");
            res = lwesp_netconn_write(client, request_header, sizeof(request_header) - 1);    /* Send data to server */
            if (res == lwespOK) {
                res = lwesp_netconn_flush(client);    /* Flush data to output */
            }
            if (res == lwespOK) {                 /* Were data sent? */
                _dbg("Data were successfully sent to server\r\n");

                /*
                 * Since we sent HTTP request,
                 * we are expecting some data from server
                 * or at least forced connection close from remote side
                 */
                do {
                    /*
                     * Receive single packet of data
                     *
                     * Function will block thread until new packet
                     * is ready to be read from remote side
                     *
                     * After function returns, don't forgot the check value.
                     * Returned status will give you info in case connection
                     * was closed too early from remote side
                     */
                    res = lwesp_netconn_receive(client, &pbuf);
                    if (res == lwespCLOSED) {     /* Was the connection closed? This can be checked by return status of receive function */
                        _dbg("Connection closed by remote side...\r\n");
                        break;
                    } else if (res == lwespTIMEOUT) {
                        _dbg("Netconn timeout while receiving data. You may try multiple readings before deciding to close manually\r\n");
                    }

                    if (res == lwespOK && pbuf != NULL) { /* Make sure we have valid packet buffer */
                        /*
                         * At this point read and manipulate
                         * with received buffer and check if you expect more data
                         *
                         * After you are done using it, it is important
                         * you free the memory otherwise memory leaks will appear
                         */
                        _dbg("Received new data packet of %d bytes\r\n", (int)lwesp_pbuf_length(pbuf, 1));
                        lwesp_pbuf_free(pbuf);    /* Free the memory after usage */
                        pbuf = NULL;
                    }
                } while (1);
            } else {
                _dbg("Error writing data to remote host!\r\n");
            }

            /*
             * Check if connection was closed by remote server
             * and in case it wasn't, close it manually
             */
            if (res != lwespCLOSED) {
                lwesp_netconn_close(client);
            }
        } else {
            _dbg("Cannot connect to remote host %s:%d!, res: %d\r\n", NETCONN_HOST, NETCONN_PORT, res);
        }
        lwesp_netconn_delete(client);             /* Delete netconn structure */
    }

    lwesp_sys_thread_terminate(NULL);             /* Terminate current thread */
}

void StartWebServerTask(void const *argument) {
    // get settings from ini file
    osDelay(1000);
    _dbg("wui starts\r\n");
    if (load_ini_file(&wui_eth_config)) {
        save_eth_params(&wui_eth_config);
    }
    wui_eth_config.var_mask = ETHVAR_MSK(ETHVAR_LAN_FLAGS);
    load_eth_params(&wui_eth_config);
    // mutex for passing marlin variables to tcp thread
    wui_thread_mutex_id = osMutexCreate(osMutex(wui_thread_mutex));
    // marlin client initialization for WUI
    wui_marlin_client_init();
    // LwIP related initalizations
    MX_LWIP_Init(&wui_eth_config);
    //    http_server_init();
    sntp_client_init();
    osDelay(WUI_NETIF_SETUP_DELAY); // wait for all settings to take effect
    // Initialize the thread for httpc
    osThreadDef(httpcTask, StarthttpcTask, osPriorityNormal, 0, 512);
    httpcTaskHandle = osThreadCreate(osThread(httpcTask), NULL);

    lwesp_mode_t mode = LWESP_MODE_STA_AP;
    // lwesp stuffs
    if (lwesp_init(NULL, 1) != lwespOK) {
        printf("Cannot initialize LwESP!\r\n");
    } else {
        printf("LwESP initialized!\r\n");
    }

    lwesp_mode_t mode = LWESP_MODE_STA_AP;

    // AP MODE TEST
    lwesp_set_wifi_mode(LWESP_MODE_AP, NULL, NULL, 0);
    for (;;) {
        update_eth_changes();
        sync_with_marlin_server();
        lwesp_get_wifi_mode(&mode, NULL, NULL, 0);
        if (mode == LWESP_MODE_AP) {
            _dbg("sta mode test ok");
			break;
        } else {
            _dbg("ap mode test FAIL, mode: %d", mode);
        }
        osDelay(1000);
    }

    // sta connect to ap
    lwespr_t err = lwesp_sta_join("esptest", "lwesp8266", NULL, NULL, NULL, 1);
    if(err == lwespOK) {
        _dbg("sta join ok\n");

        lwesp_ip_t ip;
        uint8_t is_dhcp;

        printf("Connected to network!\r\n");

        lwesp_sta_copy_ip(&ip, NULL, NULL, &is_dhcp);
        _dbg("STATION IP: %d.%d.%d.%d", ip.ip[0], ip.ip[1], ip.ip[2], ip.ip[3]);
        printf("; Is DHCP: %d\r\n", (int)is_dhcp);
    } else {
        _dbg("AP join FAILED, res: %d", err);
    }

    /*lwesp_ip_t ip, gw, nm;
    lwesp_ap_getip(&ip, &gw, &nm, NULL, NULL, 1);
    _dbg("AP IP: %d.%d.%d.%d", ip.ip[0], ip.ip[1], ip.ip[2], ip.ip[3]);*/


    netconn_client_test();

    netconn_listen_test();
    
    socket_listen_test_lwesp();
}

