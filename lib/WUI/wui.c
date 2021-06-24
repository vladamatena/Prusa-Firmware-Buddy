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
#include "esp/esp.h"
#include "stm32_port.h"

#include "esp.h"

#include "sockets.h" // LwIP sockets wrapper
#include "sockets/lwesp_sockets.h"

osThreadId httpcTaskHandle;

extern void netconn_client_thread(void const *arg);

#define WUI_NETIF_SETUP_DELAY  1000
#define WUI_COMMAND_QUEUE_SIZE WUI_WUI_MQ_CNT // maximal number of messages at once in WUI command messageQ

// WUI thread mutex for updating marlin vars
osMutexDef(wui_thread_mutex);
osMutexId(wui_thread_mutex_id);

static marlin_vars_t *wui_marlin_vars;
wui_vars_t wui_vars;                              // global vriable for data relevant to WUI
static char wui_media_LFN[FILE_NAME_MAX_LEN + 1]; // static buffer for gcode file name

static void wui_marlin_client_init(void) {
    wui_marlin_vars = marlin_client_init(); // init the client
    // force update variables when starts
    marlin_client_set_event_notify(MARLIN_EVT_MSK_DEF - MARLIN_EVT_MSK_FSM, NULL);
    marlin_client_set_change_notify(MARLIN_VAR_MSK_DEF | MARLIN_VAR_MSK_WUI, NULL);
    if (wui_marlin_vars) {
        wui_marlin_vars->media_LFN = wui_media_LFN;
    }
}

// static void sync_with_marlin_server(void) {
//     if (wui_marlin_vars) {
//         marlin_client_loop();
//     } else {
//         return;
//     }
//     osMutexWait(wui_thread_mutex_id, osWaitForever);
//     for (int i = 0; i < 4; i++) {
//         wui_vars.pos[i] = wui_marlin_vars->pos[i];
//     }
//     wui_vars.temp_nozzle = wui_marlin_vars->temp_nozzle;
//     wui_vars.temp_bed = wui_marlin_vars->temp_bed;
//     wui_vars.target_nozzle = wui_marlin_vars->target_nozzle;
//     wui_vars.target_bed = wui_marlin_vars->target_bed;
//     wui_vars.fan_speed = wui_marlin_vars->fan_speed;
//     wui_vars.print_speed = wui_marlin_vars->print_speed;
//     wui_vars.flow_factor = wui_marlin_vars->flow_factor;
//     wui_vars.print_dur = wui_marlin_vars->print_duration;
//     wui_vars.sd_precent_done = wui_marlin_vars->sd_percent_done;
//     wui_vars.sd_printing = wui_marlin_vars->sd_printing;
//     wui_vars.time_to_end = wui_marlin_vars->time_to_end;
//     wui_vars.print_state = wui_marlin_vars->print_state;
//     if (marlin_change_clr(MARLIN_VAR_FILENAME)) {
//         strlcpy(wui_vars.gcode_name, wui_marlin_vars->media_LFN, FILE_NAME_MAX_LEN);
//     }

//     osMutexRelease(wui_thread_mutex_id);
// }

// static void update_eth_changes(void) {
//     wui_lwip_link_status(); // checks plug/unplug status and take action
//     wui_lwip_sync_gui_lan_settings();
// }

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

void socket_connect_test_lwesp() {
    _dbg("LWESP TCP HELLO CLIENT TEST\n");

    int clientfd = lwesp_socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1) {
        _dbg("FAILED TO CREATE CLIENT SOCKET\n");
        return;
    }
    _dbg("SOCKET CREATED\n");

    struct sockaddr_in serveraddr;
    bzero(&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = inet_addr("10.42.0.1");
    serveraddr.sin_port = htons(5000);
  
    if(lwesp_connect(clientfd, (const struct sockaddr *)&serveraddr, sizeof(serveraddr)) != espOK) {
        _dbg("FAILED TO CONNECT TO SERVER");
        return;
    }
    _dbg("SOCKET CONNECTED");

    char buff[20];


    int msgLen = sprintf(buff, "Hello, fd:%d\n", clientfd);
    ssize_t writen = lwesp_write(clientfd, buff, msgLen);
    if (writen != msgLen) {
        _dbg("FAILED TO WRITE: %d bytes", msgLen);
        return;
    }

    _dbg("READING 20 bytes FROM SOCKET");
    size_t read = lwesp_read(clientfd, buff, sizeof(buff));
    if(read != sizeof(buff)) {
        _dbg("FAILED TO READ");
        return;
    }
    _dbg("Read: %s", buff);




    if(lwesp_close(clientfd) != espOK) {
        _dbg("FAILED TO CLOSE");
        return;
    }

    _dbg("SOCKET CLOSED");
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
    
    for(;;) {
        socklen_t len = sizeof(clientaddr);  
        int connectionfd = lwesp_accept(listenfd, (struct sockaddr *)&clientaddr, &len);
        if (connectionfd < 0) {
            _dbg("ACCEPT FAILED\n");
            return;
        }
        
        _dbg("CONNECTION ACCEPTED\n");

        char buff[20];

        lwesp_read(connectionfd, buff, sizeof(buff));
        _dbg("Read: %s", buff);


        int msgLen = sprintf(buff, "Hello, fd:%d\n", connectionfd);
        lwesp_write(connectionfd, buff, msgLen);
    
        lwesp_close(connectionfd);
    }
  
    // After chatting close the socket
    lwesp_close(listenfd);

    _dbg("CONNECTION CLOSED\n");
}

void socket_udp_client_test_lwesp() {
    _dbg("LWESP UDP HELLO TEST\n");
    int sockfd = lwesp_socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        _dbg("FAILED TO CREATE SOCKET\n");
        return;
    }
    _dbg("SOCKET CREATED\n");

    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("10.42.0.1");
    servaddr.sin_port = htons(5000);

    int conres = lwesp_connect(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr));
    if(conres != 0) {
        _dbg("CONNECT RETURNED: %d\n", conres);
    }
  
    for(;;) {
        static const size_t BUF_SIZE = 20;
        char buff[20] = "Hello world 1234567\n";

        int send = lwesp_sendto(sockfd, buff, BUF_SIZE, 0, (const struct sockaddr *)&servaddr, sizeof(servaddr));
        if (send < 0) {
            _dbg("SENDTO FAILED: %d\n", send);
        }

        _dbg("RECIEVE");
        struct sockaddr fromaddr;
        socklen_t fromlen;
        int recvd = lwesp_recvfrom(sockfd, buff, BUF_SIZE, 0, &fromaddr, &fromlen);
        if(recvd < 0) {
            _dbg("RECV FAILED: %d", recvd);
        } else {
            buff[recvd] = 0;
            _dbg("RECEIVED: %d bytes", recvd);
            _dbg("RECEIVED: %s", buff);
        }
    }
  
    lwesp_close(sockfd);

    _dbg("CONNECTION CLOSED\n");
}

void socket_udp_server_test_lwesp() {
    _dbg("LWESP UDP SERVER TEST\n");
    int sockfd = lwesp_socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        _dbg("FAILED TO CREATE SOCKET\n");
        return;
    }
    _dbg("SOCKET CREATED\n");

    struct sockaddr_in servaddr, otheraddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(5000);

    bzero(&otheraddr, sizeof(otheraddr));
  
  
    if ((lwesp_bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) {
        _dbg("BIND FAILED\n");
        return;
    }
    
    _dbg("SOCKET BOUND\n");
  
    
    for(;;) {
        static const size_t BUF_SIZE = 20;
        char buff[20];

        socklen_t len = 0;
        //int recvd = lwesp_recvfrom(sockfd, buff, BUF_SIZE, 0, (struct sockaddr *)&otheraddr, &len);
        int recvd = lwesp_recv(sockfd, buff, BUF_SIZE, 0);
        if (recvd < 0) {
            _dbg("RECVFROM FAILED\n");
        }


        _dbg("Received: %d bytes", recvd);
        _dbg("Received client addr len: %d bytes", len);
        _dbg("Received: %s", buff);

        int send = lwesp_sendto(sockfd, buff, recvd, 0, (const struct sockaddr *)&otheraddr, sizeof(otheraddr));
        if (send < 0) {
            _dbg("SENDTO FAILED: %d\n", send);
        }
    }
  
    // After chatting close the socket
    lwesp_close(sockfd);

    _dbg("CONNECTION CLOSED\n");
}

void netconn_listen_test() {
    uint32_t res;
    esp_netconn_p server, client;
    esp_pbuf_p p;

    /* Create netconn for server */
    server = esp_netconn_new(ESP_NETCONN_TYPE_TCP);
    if (server == NULL) {
        _dbg("Cannot create server netconn!\r\n");
    }

    /* Bind it to port 5000 */
    res = esp_netconn_bind(server, 5000);
    if (res != 0) {
        _dbg("Cannot bind server\r\n");
        goto out;
    }

    /* Start listening for incoming connections with maximal 1 client */
    res = esp_netconn_listen_with_max_conn(server, 1);
    if (res != 0) {
        _dbg("Cannot listen server\r\n");
        goto out;
    }

    /* Unlimited loop */
    while (1) {
        /* Accept new client */
        res = esp_netconn_accept(server, &client);
        if (res != espOK) {
            break;
        }
        _dbg("New client accepted!\r\n");
        while (1) {
            /* Receive data */
            res = esp_netconn_receive(client, &p);
            if (res == espOK) {
                _dbg("Data received!\r\n");
                esp_pbuf_free(p);
            } else {
                _dbg("Netconn receive returned: %d\r\n", (int)res);
                if (res == espCLOSED) {
                    _dbg("Connection closed by client\r\n");
                    break;
                }
            }

            static const char request_header[] = "@@@SERVER RESPONSE DATA###";
            res = esp_netconn_write(client, request_header, sizeof(request_header) - 1);    /* Send data to client */
            if (res == espOK) {
                res = esp_netconn_flush(client);    /* Flush data to output */
            }
        }
        /* Delete client */
        if (client != NULL) {
            esp_netconn_delete(client);
            client = NULL;
        }
    }
    /* Delete client */
    if (client != NULL) {
        esp_netconn_delete(client);
        client = NULL;
    }

out:
    printf("Terminating netconn thread!\r\n");
    if (server != NULL) {
        esp_netconn_delete(server);
    }
    esp_sys_thread_terminate(NULL);
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
    espr_t res;
    esp_pbuf_p pbuf;
    esp_netconn_p client;

    _dbg("Netconn client test");

    /*
     * First create a new instance of netconn
     * connection and initialize system message boxes
     * to accept received packet buffers
     */
    client = esp_netconn_new(ESP_NETCONN_TYPE_TCP);
    if (client != NULL) {
        /*
         * Connect to external server as client
         * with custom NETCONN_CONN_HOST and CONN_PORT values
         *
         * Function will block thread until we are successfully connected (or not) to server
         */
        res = esp_netconn_connect(client, NETCONN_HOST, NETCONN_PORT);
        if (res == espOK) {                     /* Are we successfully connected? */
            _dbg("Connected to " NETCONN_HOST "\r\n");
            res = esp_netconn_write(client, request_header, sizeof(request_header) - 1);    /* Send data to server */
            if (res == espOK) {
                res = esp_netconn_flush(client);    /* Flush data to output */
            }
            if (res == espOK) {                 /* Were data sent? */
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
                    res = esp_netconn_receive(client, &pbuf);
                    if (res == espCLOSED) {     /* Was the connection closed? This can be checked by return status of receive function */
                        _dbg("Connection closed by remote side...\r\n");
                        break;
                    } else if (res == espTIMEOUT) {
                        _dbg("Netconn timeout while receiving data. You may try multiple readings before deciding to close manually\r\n");
                    }

                    if (res == espOK && pbuf != NULL) { /* Make sure we have valid packet buffer */
                        /*
                         * At this point read and manipulate
                         * with received buffer and check if you expect more data
                         *
                         * After you are done using it, it is important
                         * you free the memory otherwise memory leaks will appear
                         */
                        _dbg("Received new data packet of %d bytes\r\n", (int)esp_pbuf_length(pbuf, 1));
                        esp_pbuf_free(pbuf);    /* Free the memory after usage */
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
            if (res != espCLOSED) {
                esp_netconn_close(client);
            }
        } else {
            _dbg("Cannot connect to remote host %s:%d!, res: %d\r\n", NETCONN_HOST, NETCONN_PORT, res);
        }
        esp_netconn_delete(client);             /* Delete netconn structure */
    }

    esp_sys_thread_terminate(NULL);             /* Terminate current thread */
}

void StartWebServerTask(void const *argument) {
    ap_entry_t ap = { "esptest", "lwesp8266" };
    uint32_t res;

    // get settings from ini file
    osDelay(1000);
    _dbg("wui starts");
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

    // lwesp stuffs
    res = esp_initialize();
    _dbg("LwESP initialized with result = %ld", res);
    LWIP_UNUSED_ARG(res);

    if (!esp_connect_to_AP(&ap)) {
        _dbg("LwESP connect to AP %s!", ap.ssid);
        //esp_sys_thread_create(NULL, "netconn_client", (esp_sys_thread_fn)netconn_client_thread, NULL, 512, ESP_SYS_THREAD_PRIO);

        //netconn_client_test();
        socket_udp_client_test_lwesp();
        //socket_udp_server_test_lwesp();
        //socket_connect_test_lwesp();
        //socket_listen_test_lwesp();

    } else {
        _dbg("AP connect FAILED");
    }

    /*// MODE TEST
    lwesp_mode_t mode = LWESP_MODE_STA_AP;
    lwesp_set_wifi_mode(LWESP_MODE_STA, NULL, NULL, 0);
    for (;;) {
        update_eth_changes();
        sync_with_marlin_server();

        lwesp_get_wifi_mode(&mode, NULL, NULL, 0);
        if (mode == LWESP_MODE_STA) {
            _dbg("sta mode test ok");
			break;
        } else {
            _dbg("ap mode test FAIL, mode: %d", mode);
        }
        osDelay(1000);
    }*/
/*
    // sta connect to ap
    lwespr_t err = lwesp_sta_join("esptest", "lwesp8266", NULL, NULL, NULL, 1);
    if(err == lwespOK) {
        _dbg("sta join ok\n");

        esp_ip_t ip;
        uint8_t is_dhcp;

        _dbg("Connected to network!\r\n");

        lwesp_sta_copy_ip(&ip, NULL, NULL, &is_dhcp);
        _dbg("STATION IP: %d.%d.%d.%d", ip.ip[0], ip.ip[1], ip.ip[2], ip.ip[3]);
        _dbg("; Is DHCP: %d\r\n", (int)is_dhcp);
    } else {
        _dbg("AP join FAILED, res: %d", err);
    }*/

    /*esp_ip_t ip, gw, nm;
    lwesp_ap_getip(&ip, &gw, &nm, NULL, NULL, 1);
    _dbg("AP IP: %d.%d.%d.%d", ip.ip[0], ip.ip[1], ip.ip[2], ip.ip[3]);*/


  
    
 //   socket_listen_test_lwesp();
}

