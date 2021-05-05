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

<<<<<<< HEAD
osThreadId httpcTaskHandle;
=======
#include "sockets.h" // LwIP sockets wrapper
#include "lwesp_sockets.h"
>>>>>>> f561cf12 (WIP: LwESP sockets)

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
    // SOCKET LISTENING TEST
    int listenfd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        printf("FAILED TO CREATE LISTENING SOCKET\n");
        return;
    }
    printf("SOCKET CREATED\n");

    struct sockaddr_in servaddr, clientaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(5000);
  
    if ((lwip_bind(listenfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) {
        printf("BIND FAILED\n");
        return;
    }
    
    printf("SOCKET BOUND\n");
  
    if ((lwip_listen(listenfd, 5)) != 0) {
        printf("LISTEN FAILED\n");
        return;
    }
    
    printf("SOCKET LISTENING\n");
    
    socklen_t len = sizeof(clientaddr);
  
    int connectionfd = lwip_accept(listenfd, (struct sockaddr *)&clientaddr, &len);
    if (connectionfd < 0) {
        printf("ACCEPT FAILED\n");
        return;
    }
    
    printf("CONNECTION ACCEPTED\n");

    const char buff[] = "Hello\n";
    lwip_write(connectionfd, buff, sizeof(buff));
  
    lwip_close(connectionfd);
  
    // After chatting close the socket
    lwip_close(listenfd);
}

void socket_listen_test_lwesp() {
    // SOCKET LISTENING TEST
    int listenfd = lwesp_socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        printf("FAILED TO CREATE LISTENING SOCKET\n");
        return;
    }
    printf("SOCKET CREATED\n");

    struct sockaddr_in servaddr, clientaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(5000);
  
    if ((lwesp_bind(listenfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) {
        printf("BIND FAILED\n");
        return;
    }
    
    printf("SOCKET BOUND\n");
  
    if ((lwesp_listen(listenfd, 5)) != 0) {
        printf("LISTEN FAILED\n");
        return;
    }
    
    printf("SOCKET LISTENING\n");
    
    socklen_t len = sizeof(clientaddr);
  
    int connectionfd = lwesp_accept(listenfd, (struct sockaddr *)&clientaddr, &len);
    if (connectionfd < 0) {
        printf("ACCEPT FAILED\n");
        return;
    }
    
    printf("CONNECTION ACCEPTED\n");

    const char buff[] = "Hello\n";
    lwesp_write(connectionfd, buff, sizeof(buff));
  
    lwesp_close(connectionfd);
  
    // After chatting close the socket
    lwesp_close(listenfd);
}

void StartWebServerTask(void const *argument) {
    // get settings from ini file
    osDelay(1000);
    printf("wui starts");
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

    // STA CONNECTED TEST
    for (;;) {
        update_eth_changes();
        sync_with_marlin_server();
        lwesp_get_wifi_mode(&mode, NULL, NULL, 0);
        if (mode == LWESP_MODE_STA) {
            printf("test ok");
        }
        lwesp_get_wifi_mode(&mode, NULL, NULL, 0);
        if (mode == LWESP_MODE_STA) {
            printf("sta mode test ok");
			break;
        }
        osDelay(1000);
    }
    
    socket_listen_test_lwip();
}

