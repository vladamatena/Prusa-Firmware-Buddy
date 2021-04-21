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
#include "lwesp_sockets.h"

osThreadId httpcTaskHandle;

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
    printf("LWIP TCP HELLO SERVER TEST\n");
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

    printf("CONNECTION CLOSED\n");
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

void StartWebServerTask(void const *argument) {
    ap_entry_t ap = { "SSID", "password" };
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
        esp_sys_thread_create(NULL, "netconn_client", (esp_sys_thread_fn)netconn_client_thread, NULL, 512, ESP_SYS_THREAD_PRIO);
    }

    lwesp_mode_t mode = LWESP_MODE_STA_AP;

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
            _dbg("sta mode test ok");
			break;
        } else {
            _dbg("sta mode test FAIL, mode: %d", mode);
        }
        osDelay(1000);
        lwesp_set_wifi_mode(LWESP_MODE_STA, NULL, NULL, 0);
    }
    
    socket_listen_test_lwesp();
}

