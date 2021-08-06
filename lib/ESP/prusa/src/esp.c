#include "esp.h"
#include "esp/esp_includes.h"
#include "lwesp_ll_buddy.h"
#include "esp_loader.h"
#include "stm32_port.h"
#include "ff.h"

#include "dbg.h"

#define MAX_TIMEOUT_ATTEMPTS (2UL)

/**
 * \brief           List of access points found by ESP device
 */
static esp_ap_t aps[32];

/**
 * \brief           Number of valid access points in \ref aps array
 */
static size_t apf;

//static esp_sta_info_ap_t connected_ap_info;

/**
 * \brief           Event callback function for ESP stack
 * \param[in]       evt: Event information with data
 * \return          \ref espOK on success, member of \ref espr_t otherwise
 */
// static espr_t
// esp_callback_func(esp_evt_t *evt) {
//     switch (esp_evt_get_type(evt)) {
//     case ESP_EVT_AT_VERSION_NOT_SUPPORTED: {
//         esp_sw_version_t v_min, v_curr;

//         esp_get_min_at_fw_version(&v_min);
//         esp_get_current_at_fw_version(&v_curr);

//         _dbg("Current ESP8266 AT version is not supported by library!");
//         _dbg("Minimum required AT version is: %d.%d.%d", (int)v_min.major, (int)v_min.minor, (int)v_min.patch);
//         _dbg("Current AT version is: %d.%d.%d", (int)v_curr.major, (int)v_curr.minor, (int)v_curr.patch);
//         break;
//     }
//     case ESP_EVT_INIT_FINISH: {
//         _dbg("ESP_EVT_INIT_FINISH");
//         break;
//     }
//     case ESP_EVT_RESET: {
//         _dbg("ESP_EVT_RESET");
//         break;
//     }
//     case ESP_EVT_RESET_DETECTED: {
//         _dbg("ESP_EVT_RESET_DETECTED");
//         break;
//     }
//     case ESP_EVT_WIFI_GOT_IP: {
//         _dbg("ESP_EVT_WIFI_GOT_IP");
//         esp_set_wifi_mode(ESP_MODE_STA, NULL, NULL, 0);
//         break;
//     }
//     case ESP_EVT_WIFI_CONNECTED: {
//         _dbg("ESP_EVT_WIFI_CONNECTED");
//         esp_sta_get_ap_info(&connected_ap_info, NULL, NULL, 0);
//         break;
//     }
//     case ESP_EVT_WIFI_DISCONNECTED: {
//         _dbg("ESP_EVT_WIFI_DISCONNECTED");
//         break;
//     }
//     default:
//         break;
//     }
//     return espOK;
// }

uint32_t esp_present(uint32_t on) {
    // lwespr_t eres;
    // uint32_t tried = MAX_TIMEOUT_ATTEMPTS;
    // lwesp_mode_t mode = ESP_MODE_STA_AP;

    // eres = lwesp_device_set_present(on, NULL, NULL, 1);
    // while (on && mode != ESP_MODE_STA && tried) {
    //     eres = lwesp_set_wifi_mode(ESP_MODE_STA, NULL, NULL, 1);
    //     if (eres != lwespOK) {
    //         _dbg("Unable to set wifi mode : %d", eres);
    //         --tried;
    //     }
    //     lwesp_get_wifi_mode(&mode, NULL, NULL, 1);
    // }

    // if(!tried || !on) {
    //     eres = esp_device_set_present(0, NULL, NULL, 1);
    //     return 0UL;
    // }
    return 1UL;
}

uint32_t esp_is_device_presented() {
    return esp_device_is_present();
}

uint32_t esp_initialize() {
    espr_t eres;
    esp_hard_reset_device();
    eres = esp_init(NULL, 1);
    if (eres != espOK) {
        return eres;
    }
    //    eres = esp_device_set_present(0, NULL, NULL, 1);
    return eres;
}

espr_t esp_flash_initialize() {
    espr_t err = esp_ll_deinit(NULL);
    if (err != espOK) {
        return err;
    }
    esp_reconfigure_uart(ESP_CFG_AT_PORT_BAUDRATE);
    esp_set_operating_mode(ESP_FLASHING_MODE);
    loader_stm32_config_t loader_config = {
        .huart = &huart6,
        .port_io0 = GPIOE,
        .pin_num_io0 = GPIO_PIN_6,
        .port_rst = GPIOC,
        .pin_num_rst = GPIO_PIN_13,
    };
    loader_port_stm32_init(&loader_config);
    return espOK;
}

typedef struct {
    size_t address;
    const char *filename;
    size_t size;
} esp_firmware_part;

#define BOOT_ADDRESS      0x00000ul
#define USER_ADDRESS      0x01000ul
#define BLANK1_ADDRESS    0x7e000ul
#define BLANK2_ADDRESS    0xfb000ul
#define INIT_DATA_ADDRESS 0xfc000ul
#define BLANK3_ADDRESS    0xfe000ul

#define BUFFER_LENGTH 512

esp_firmware_part firmware_set[] = {
    // { .address = BOOT_ADDRESS, .filename = "/boot_v1.7.bin", .size = 0 },
    { .address = USER_ADDRESS, .filename = "/user1.1024.new.2.bin", .size = 0 },
    { .address = BLANK1_ADDRESS, .filename = "/blank.bin", .size = 0 },
    { .address = BLANK2_ADDRESS, .filename = "/blank.bin", .size = 0 },
    { .address = INIT_DATA_ADDRESS, .filename = "/esp_init_data_default_v08.bin", .size = 0 },
    { .address = BLANK3_ADDRESS, .filename = "/blank.bin", .size = 0 }
};

espr_t esp_flash() {
    espr_t flash_init_res = esp_flash_initialize();
    if (flash_init_res != espOK) {
        return flash_init_res;
    }
    esp_loader_connect_args_t config = ESP_LOADER_CONNECT_DEFAULT();
    _dbg("ESP boot connect");
    if (ESP_LOADER_SUCCESS != esp_loader_connect(&config)) {
        _dbg("ESP boot connect failed");
        return espERR;
    }

    for (esp_firmware_part *current_part = &firmware_set[0]; current_part < &firmware_set[5]; current_part++) {
        _dbg("ESP Start flash %s", current_part->filename);
        FIL file_descriptor;
        if (f_open(&file_descriptor, current_part->filename, FA_READ) != FR_OK) {
            _dbg("ESP flash: Unable to open file %s", current_part->filename);
            break;
        }

        if (esp_loader_flash_start(current_part->address, current_part->size, BUFFER_LENGTH) != ESP_LOADER_SUCCESS) {
            _dbg("ESP flash: Unable to start flash on address %0xld", current_part->address);
            f_close(&file_descriptor);
            break;
        }

        UINT readBytes = 0;
        uint8_t buffer[BUFFER_LENGTH];
        uint32_t readCount = 0;

        while (1) {
            FRESULT res = f_read(&file_descriptor, buffer, sizeof(buffer), &readBytes);
            readCount += readBytes;
            _dbg("ESP read data %ld", readCount);
            if (res != FR_OK) {
                _dbg("ESP flash: Unable to read file %s", current_part->filename);
                readBytes = 0;
            }
            if (readBytes > 0) {
                if (esp_loader_flash_write(buffer, readBytes) != ESP_LOADER_SUCCESS) {
                    _dbg("ESP flash write FAIL");
                }
            } else {
                _dbg("File finished");
                f_close(&file_descriptor);
                break;
            }
        }
    }
    return espOK;
}

static void baudrate_change_evt(espr_t res, void *arg) {
    if (res != espOK) {
        _dbg("ESP baudrate change failed !!!");
        return;
    }
    uint32_t baudrate = (uint32_t)arg;
    _dbg("ESP baudrate change success, reconfiguring UART for %d", baudrate);
    esp_reconfigure_uart(baudrate);
}

uint32_t esp_set_baudrate(const uint32_t baudrate) {
    return esp_set_at_baudrate(baudrate, baudrate_change_evt, (void *)baudrate, 1);
}

uint32_t esp_connect_to_AP(const ap_entry_t *preferead_ap) {
    espr_t eres;
    uint32_t tried = MAX_TIMEOUT_ATTEMPTS;

    /*
     * Scan for network access points
     * In case we have access point,
     * try to connect to known AP
     */
    do {

        /* Scan for access points visible to ESP device */
        if ((eres = esp_sta_list_ap(NULL, aps, ESP_ARRAYSIZE(aps), &apf, NULL, NULL, 1)) == espOK) {
            tried = 0;
            /* Process array of preferred access points with array of found points */
            for (size_t i = 0; i < ESP_ARRAYSIZE(aps); i++) {
                if (!strcmp(aps[i].ssid, preferead_ap->ssid)) {
                    tried = 1;
                    _dbg("Connecting to \"%s\" network...", preferead_ap->ssid);
                    /* Try to join to access point */
                    if ((eres = esp_sta_join(preferead_ap->ssid, preferead_ap->pass, NULL, 0, NULL, NULL, 1)) == espOK) {
                        esp_ip_t ip, gw, mask;
                        esp_sta_getip(&ip, &gw, &mask, 0, NULL, NULL, 1);
                        esp_sta_copy_ip(&ip, NULL, NULL);
                        _dbg("Connected to %s network!", preferead_ap->ssid);
                        _dbg("Station IP address: %d.%d.%d.%d",
                            (int)ip.ip[0], (int)ip.ip[1], (int)ip.ip[2], (int)ip.ip[3]);
                        _dbg("Station gateway address: %d.%d.%d.%d",
                            (int)gw.ip[0], (int)gw.ip[1], (int)gw.ip[2], (int)gw.ip[3]);
                        _dbg("Station mask address: %d.%d.%d.%d",
                            (int)mask.ip[0], (int)mask.ip[1], (int)mask.ip[2], (int)mask.ip[3]);
                        return (uint32_t)espOK;
                    } else {
                        _dbg("Connection error: %d", (int)eres);
                    }
                }
            }
            if (!tried) {
                _dbg("No access points available with preferred SSID: %s!", preferead_ap->ssid);
            }
        } else if (eres == espERRNODEVICE) {
            _dbg("Device is not present!");
            break;
        } else {
            _dbg("Error on WIFI scan procedure!");
        }
    } while (esp_is_device_presented());
    return (uint32_t)espERR;
}
