#include "esp/esp.h"
#include "esp/esp_mem.h"
#include "esp/esp_input.h"
#include "system/esp_ll.h"
#include "lwesp_ll_buddy.h"
#include "main.h"
#include "stm32_port.h"
#include "dbg.h"
#include "ff.h"

#include "dbg.h"
#include "lwip/def.h"
#include "lwip/ethip6.h"
#include "lwip/etharp.h"

/*
 * UART and other pin configuration for ESP01 module
 *
 * UART:                USART6
 * STM32 TX (ESP RX):   GPIOC, GPIO_PIN_6
 * STM32 RX (ESP TX):   GPIOC, GPIO_PIN_7
 * RESET:               GPIOC, GPIO_PIN_13
 * GPIO0:               GPIOE, GPIO_PIN_6
 * GPIO2:               not connected
 * CH_PD:               connected to board 3.3 V
 *
 * UART_DMA:           DMA2
 * UART_RX_STREAM      STREAM_1
 * UART_TX_STREAM      STREAM_6
 */

//Thread stuffs
static osThreadId UartBufferThread_id;
//message queue stuffs
osMessageQDef(uartBufferMbox, 16, NULL);
static osMessageQId uartBufferMbox_id;

static uint32_t esp_working_mode;
static uint32_t initialized;

uint8_t dma_buffer_rx[RX_BUFFER_LEN];

void esp_set_operating_mode(uint32_t mode) {
    esp_working_mode = mode;
}

uint32_t esp_get_operating_mode(void) {
    return esp_working_mode;
}

void esp_receive_data(UART_HandleTypeDef *huart) {
    ESP_UNUSED(huart);
    if (uartBufferMbox_id != NULL) {
        uint32_t message = 0;
        osMessagePut(uartBufferMbox_id, message, 0);
    }
}

static size_t old_dma_pos = 0;

/**
 * \brief           USART data processing
 */
void StartUartBufferThread(void const *arg) {
    size_t pos = 0;

    ESP_UNUSED(arg);

    while (1) {
        /* Wait for the event message from DMA or USART */
        /* There is 100ms max wait time to ensure some small standalone message
           does not get stuck in DMA buffer for too long. */
        osMessageGet(uartBufferMbox_id, 100);

        /* Read data */
        uint32_t dma_bytes_left = __HAL_DMA_GET_COUNTER(huart6.hdmarx); // no. of bytes left for buffer full
        pos = sizeof(dma_buffer_rx) - dma_bytes_left;
        if (pos != old_dma_pos && esp_get_operating_mode() == ESP_RUNNING_MODE) {
            if (pos > old_dma_pos) {
                esp_input_process(&dma_buffer_rx[old_dma_pos], pos - old_dma_pos);
            } else {
                esp_input_process(&dma_buffer_rx[old_dma_pos], sizeof(dma_buffer_rx) - old_dma_pos);
                if (pos > 0) {
                    esp_input_process(&dma_buffer_rx[0], pos);
                }
            }
            old_dma_pos = pos;
            if (old_dma_pos == sizeof(dma_buffer_rx)) {
                old_dma_pos = 0;
            }
        }
    }
}

void esp_hard_reset_device() {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    esp_delay(10);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART6 && (huart->ErrorCode & HAL_UART_ERROR_NE || huart->ErrorCode & HAL_UART_ERROR_FE)) {
        __HAL_UART_DISABLE_IT(huart, UART_IT_IDLE);
        HAL_UART_DeInit(huart);
        old_dma_pos = 0;
        if (HAL_UART_Init(huart) != HAL_OK) {
            Error_Handler();
        }
        if (HAL_UART_Receive_DMA(huart, (uint8_t *)dma_buffer_rx, RX_BUFFER_LEN) != HAL_OK) {
            Error_Handler();
        }
        __HAL_UART_ENABLE_IT(huart, UART_IT_IDLE);
    }
}

/**
 * \brief           Send data to ESP device
 * \param[in]       data: Pointer to data to send
 * \param[in]       len: Number of bytes to send
 * \return          Number of bytes sent
 */
size_t
esp_transmit_data(const void *data, size_t len) {
    if (esp_get_operating_mode() != ESP_RUNNING_MODE) {
        return 0;
    }

    for (size_t i = 0; i < len; ++i) {
        HAL_UART_Transmit(&huart6, (uint8_t *)(data + i), 1, 10);
    }
    return len;
}

espr_t esp_reconfigure_uart(const uint32_t baudrate) {
    huart6.Init.BaudRate = baudrate;
    int hal_uart_res = HAL_UART_Init(&huart6);
    if (hal_uart_res != HAL_OK) {
        _dbg("ESP LL: HAL_UART_Init failed with: %d", hal_uart_res);
        return espERR;
    }

    int hal_dma_res = HAL_UART_Receive_DMA(&huart6, (uint8_t *)dma_buffer_rx, RX_BUFFER_LEN);
    if (hal_dma_res != HAL_OK) {
        _dbg("ESP LL: HAL_UART_Receive_DMA failed with: %d", hal_dma_res);
        return espERR;
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

espr_t esp_set_baudrate(const uint32_t baudrate) {
    return esp_set_at_baudrate(baudrate, baudrate_change_evt, (void *)baudrate, 1);
}

/**
 * \brief           Callback function called from initialization process
 */
espr_t
esp_ll_init(esp_ll_t *ll) {
#if !ESP_CFG_MEM_CUSTOM
    static uint8_t memory[ESP_MEM_SIZE];
    esp_mem_region_t mem_regions[] = {
        { memory, sizeof(memory) }
    };

    if (!initialized) {
        esp_mem_assignmemory(mem_regions, ESP_ARRAYSIZE(mem_regions)); /* Assign memory for allocations */
    }
#endif /* !ESP_CFG_MEM_CUSTOM */
    if (!initialized) {
        ll->send_fn = esp_transmit_data; /* Set callback function to send data */

        /* Create mbox and start thread */
        if (uartBufferMbox_id == NULL) {
            uartBufferMbox_id = osMessageCreate(osMessageQ(uartBufferMbox), NULL);
            if (uartBufferMbox_id == NULL) {
                _dbg("ESP LL: failed to create UART buffer mbox");
                return espERR;
            }
        }
        if (UartBufferThread_id == NULL) {
            osThreadDef(UartBufferThread, StartUartBufferThread, osPriorityNormal, 0, 512);
            UartBufferThread_id = osThreadCreate(osThread(UartBufferThread), NULL);
            if (UartBufferThread_id == NULL) {
                _dbg("ESP LL: failed to start UART buffer thread");
                return espERR;
            }
        }
    }

    esp_reconfigure_uart(ll->uart.baudrate);
    esp_set_operating_mode(ESP_RUNNING_MODE);
    initialized = 1;
    return espOK;
}

/**
 * \brief           Callback function to de-init low-level communication part
 */
espr_t
esp_ll_deinit(esp_ll_t *ll) {
    if (uartBufferMbox_id != NULL) {
        osMessageQId tmp = uartBufferMbox_id;
        uartBufferMbox_id = NULL;
        osMessageDelete(tmp);
    }
    if (UartBufferThread_id != NULL) {
        osThreadId tmp = UartBufferThread_id;
        UartBufferThread_id = NULL;
        osThreadTerminate(tmp);
    }
    initialized = 0;
    ESP_UNUSED(ll);
    return espOK;
}

espr_t esp_flash_initialize() {
    espr_t err = esp_ll_deinit(NULL);
    if (err != espOK) {
        return err;
    }
    esp_set_operating_mode(ESP_FLASHING_MODE);
    esp_reconfigure_uart(115200);
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

static void uart_nic_input(uint8_t *data, size_t size);

/**
 * \brief           USART data processing
 */
static void UartNICBufferThreadBody(void const *arg) {
    size_t old_pos = 0;
    size_t pos = 0;

    ESP_UNUSED(arg);

    _dbg("UART NIC RX THREAD RUNNING");

    while (1) {
        /* Wait for the event message from DMA or USART */
        /* There is 100ms max wait time to ensure some small standalone message
           does not get stuck in DMA buffer for too long. */
        osMessageGet(uartBufferMbox_id, 100);

        /* Read data */
        uint32_t dma_bytes_left = __HAL_DMA_GET_COUNTER(huart6.hdmarx); // no. of bytes left for buffer full
        pos = sizeof(dma_buffer_rx) - dma_bytes_left;
        if (pos != old_pos && esp_get_operating_mode() == ESP_RUNNING_MODE) {
            if (pos > old_pos) {
                uart_nic_input(&dma_buffer_rx[old_pos], pos - old_pos);
            } else {
                uart_nic_input(&dma_buffer_rx[old_pos], sizeof(dma_buffer_rx) - old_pos);
                if (pos > 0) {
                    uart_nic_input(&dma_buffer_rx[0], pos);
                }
            }
            old_pos = pos;
            if (old_pos == sizeof(dma_buffer_rx)) {
                old_pos = 0;
            }
        }
    }
}

static void low_level_input(uint8_t *data, size_t dlen);

static const char INTRON[] = { 'U', 'N', 'U' };

static int state = 0;
// 0 - waiting for intron
// 1 - reading message type
// 2 - reading packet
// 3 - reading link
// 4 - reading devinfo

#define MSG_DEVINFO      0
#define MSG_LINK         1
#define MSG_CLIENTCONFIG 2
#define MSG_PACKET       3

static uint intronPos = 0;
static uint macLen = 0;
static uint macRead = 0;
static uint8_t macData[16];

static uint32_t rxPacketLen = 0;
static uint rxPacketLenRead = 0;

static uint8_t *rxPacketData = NULL;
static uint32_t rxPacketRead = 0;

static void uart_nic_input(uint8_t *data, size_t size) {
    for (uint pos = 0; pos < size; pos++) {
        char c = data[pos];

        if (state == 0) {
            if (c == INTRON[intronPos]) {
                intronPos++;
                if (intronPos > sizeof(INTRON)) {
                    state = 1;
                }
            } else {
                intronPos = 0;
            }

            continue;
        }

        if (state == 1) {
            const uint8_t messageType = c;
            if (messageType == MSG_DEVINFO) {
                state = 4;
            } else if (messageType == MSG_LINK) {
                state = 3;
            } else if (messageType == MSG_PACKET) {
                state = 2;
            } else {
                _dbg("Unknown message type %d", messageType);
            }
            continue;
        }

        if (state == 3) {
            bool linkUp = c;
            _dbg("Link status changed: %d", linkUp);
            state = 0;
            continue;
        }

        if (state == 4) {
            if (macLen == 0) {
                macLen = c;
                macRead = 0;
                continue;
            }

            if (macRead < macLen) {
                macData[macRead++] = c;
                continue;
            } else {
                _dbg("Devinfo: mac");
                for (uint i = 0; i < 6; ++i) {
                    _dbg("Mac byte %d: %x", i, macData[i]);
                }
                macLen = 0;
                macRead = 0;
                state = 0;
                continue;
            }
        }

        if (state == 2) {
            if (rxPacketLenRead < sizeof(rxPacketLen)) {
                ((uint8_t *)&rxPacketLen)[rxPacketLenRead++] = c;
                continue;
            }
            if (rxPacketLenRead == sizeof(rxPacketLen)) {
                _dbg("Reading packet size: %d", rxPacketLen);
                rxPacketData = malloc(rxPacketLen);
            }

            if (rxPacketData) {
                rxPacketData[rxPacketRead++] = c;
                if (rxPacketRead == rxPacketLen) {
                    _dbg("Read packet size: %d", rxPacketLen);
                    low_level_input(rxPacketData, rxPacketLen);
                    free(rxPacketData);
                    rxPacketLenRead = 0;
                    rxPacketLen = 0;
                    rxPacketData = NULL;
                    rxPacketRead = 0;
                }
                continue;
            } else {
                _dbg("Skipping input packet byte as data == NULL");
                continue;
            }
        }
    }
}

static struct netif *wlif = NULL;

static void low_level_input(uint8_t *data, size_t dlen) {
    if (wlif == NULL) {
        _dbg("wlif not set, droping input data");
    }

    uint16_t len = dlen;

#if ETH_PAD_SIZE
    len += ETH_PAD_SIZE; /* allow room for Ethernet padding */
#endif

    /* We allocate a pbuf chain of pbufs from the pool. */
    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);

    if (p == NULL) {
        //        LINK_STATS_INC(link.memerr);
        //        LINK_STATS_INC(link.drop);
        //        MIB2_STATS_NETIF_INC(netif, ifindiscards);
        return;
    }

#if ETH_PAD_SIZE
    pbuf_header(p, -ETH_PAD_SIZE); /* drop the padding word */
#endif

    /* We iterate over the pbuf chain until we have read the entire
     * packet into the pbuf. */
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        /* Read enough bytes to fill this pbuf in the chain. The
       * available data in the pbuf is given by the q->len
       * variable.
       * This does not necessarily have to be a memcpy, you can also preallocate
       * pbufs for a DMA-enabled MAC and after receiving truncate it to the
       * actually received size. In this case, ensure the tot_len member of the
       * pbuf is the sum of the chained pbuf len members.
       */

        memcpy(q->payload, data, q->len);
        data += q->len;
    }

    //MIB2_STATS_NETIF_ADD(netif, ifinoctets, p->tot_len);
    //if (((u8_t*)p->payload)[0] & 1) {
    //  /* broadcast or multicast packet*/
    //  MIB2_STATS_NETIF_INC(netif, ifinnucastpkts);
    //} else {
    //  /* unicast packet*/
    //  MIB2_STATS_NETIF_INC(netif, ifinucastpkts);
    //}
    //#if ETH_PAD_SIZE
    //    pbuf_header(p, ETH_PAD_SIZE); /* reclaim the padding word */
    //#endif

    //LINK_STATS_INC(link.recv);

    if (wlif->input == NULL) {
        _dbg("wlif input not set, packet drop");
        return;
    }

    if (wlif->input(p, wlif) != ERR_OK) {
        _dbg("ethernetif_input: IP input error");
        pbuf_free(p);
        p = NULL;
    }
    _dbg("Input packet processed ok");
}

struct ethernetif {
    struct eth_addr *ethaddr;
    /* Add whatever per-interface state that is needed here. */
};

static err_t low_level_output(struct netif *netif, struct pbuf *p) {
    _dbg("Low level output");

    size_t len = p->tot_len;

    //  _dbg("\nAT+OUTPUT:%d,", len);

    static char buff[20];
    int hlen = sprintf(buff, "\nAT+OUTPUT:%d,", len);
    esp_transmit_data(buff, hlen);

    while (p != NULL) {
        esp_transmit_data(p->payload, p->len);
        /*    for(uint i = 0; i < p->len; i++) {
            _dbg("%02x", ((uint8_t*)p->payload)[i]);
        }*/
        p = p->next;
    }

    return 0;
}

uint8_t esp_lwip_init(struct netif *netif) {
    _dbg("ESP LWIP INIT");
    if (!initialized) {
        /* if (HAL_UART_Receive_DMA(&huart6, (uint8_t *)dma_buffer_rx, RX_BUFFER_LEN) != HAL_OK) {
            Error_Handler();
        }*/
        esp_reconfigure_uart(500000);

        /*    // Create mbox and start thread
        if (uartBufferMbox_id == NULL) {
            uartBufferMbox_id = osMessageCreate(osMessageQ(uartBufferMbox), NULL);
            if (uartBufferMbox_id == NULL) {
                printf("error!");
            }
        }*/
        if (UartBufferThread_id == NULL) {
            osThreadDef(UartBufferThread, UartNICBufferThreadBody, osPriorityNormal, 0, 512);
            UartBufferThread_id = osThreadCreate(osThread(UartBufferThread), NULL);
            if (UartBufferThread_id == NULL) {
                _dbg("error!");
            }
        }
    } else {
        _dbg("ESP LWIP INIT - already initialized");
    }

    esp_set_operating_mode(ESP_RUNNING_MODE);
    initialized = 1;

    // Initialize lwip netif
    struct ethernetif *ethernetif;
    ethernetif = malloc(sizeof(struct ethernetif));
    memset(ethernetif, 0, sizeof(struct ethernetif));
    //memset(netif, 0, sizeof(struct netif));

    netif->state = ethernetif;
    netif->name[0] = 'w';
    netif->name[1] = 'l';
    netif->output = etharp_output;
#if LWIP_IPV6
    netif->output_ip6 = ethip6_output;
#endif
    netif->linkoutput = low_level_output;
    ethernetif->ethaddr = (struct eth_addr *)&(netif->hwaddr[0]);

    // LL init
    netif->hwaddr_len = ETHARP_HWADDR_LEN;
    netif->hwaddr[0] = 0xbc;
    netif->hwaddr[1] = 0xdd;
    netif->hwaddr[2] = 0xc2;
    netif->hwaddr[3] = 0x6b;
    netif->hwaddr[4] = 0x61;
    netif->hwaddr[5] = 0x82;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    wlif = netif;

    _dbg("ESP LWIP INIT completed");
    return 0;
}
