/**
 * @file
 * Application layered TCP connection API (to be used from TCPIP thread)\n
 * This interface mimics the tcp callback API to the application while preventing
 * direct linking (much like virtual functions).
 * This way, an application can make use of other application layer protocols
 * on top of TCP without knowing the details (e.g. TLS, proxy connection).
 *
 * This file contains the base implementation calling into tcp.
 */

/*
 * Copyright (c) 2021 Marek Mosna
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the ESPIP TCP/IP stack.
 *
 * Author: Marek Mosna <marek.mosna@prusa3d.cz>
 * Author: Vladimir Matena <vladimir.matena@prusa3d.cz>
 *
 */

#include "esp/esp_config.h"

#include "dbg.h"

#if ESP_ALTCP /* don't build if not configured for use in espopts.h */

    #include "esp/esp.h"
    #include <string.h>
    #include "lwip/altcp.h"
    #include "lwip/priv/altcp_priv.h"
    #include "esp/esp_netconn.h"

    #include "sockets/lwesp_sockets_priv.h"

    #include "lwip/tcp.h"
// #include "lwip/mem.h"
//#include "lwip/altcp_tcp.h"

    #include "esp_tcp.h"

/* Variable prototype, the actual declaration is at the end of this file
   since it contains pointers to static functions declared here */
extern const struct altcp_functions altcp_esp_functions;

static void altcp_esp_setup(struct altcp_pcb *conn, esp_netconn_p tpcb);

static err_t espr_t2err_t(const espr_t err) {
    switch(err) {
        case espOK: return ERR_OK;                  /*!< Function succeeded */
        case espOKIGNOREMORE:                       /*!< Function succedded, should continue as espOK but ignore sending more data. This result is possible on connection data receive callback */
            _dbg("espOKIGNOREMORE - pretending all ok");
            return ERR_OK;
        case espERR: 
            _dbg("Generic ESP err");
            return ERR_IF;
        case espERRCONNTIMEOUT:                          /*!< Timeout received when connection to access point */
            _dbg("espERRCONNTIMEOUT");
            return ERR_IF;
        case espERRPASS:                                 /*!< Invalid password for access point */
            _dbg("espERRPASS");
            return ERR_IF;
        case espERRNOAP:                                 /*!< No access point found with specific SSID and MAC address */
            _dbg("espERRNOAP");
            return ERR_IF;
        case espERRCONNFAIL:                             /*!< Connection failed to access point */
            _dbg("espERRCONNFAIL");
            return ERR_IF;
        case espERRWIFINOTCONNECTED:                     /*!< Wifi not connected to access point */
            _dbg("espERRWIFINOTCONNECTED");
            return ERR_IF;
        case espERRNODEVICE:                             /*!< Device is not present */
            _dbg("espERRNODEVICE");
            return ERR_IF;
        case espCONT:
            _dbg("espCONT");                             /*!< There is still some command to be processed in current command */
            return ERR_IF;
        case espPARERR: return ERR_VAL;                  /*!< Wrong parameters on function call */
        case espERRNOFREECONN:                           /*!< There is no free connection available to start */
        case espERRMEM: return ERR_MEM;                  /*!< Memory error occurred */
        case espTIMEOUT: return ERR_TIMEOUT;             /*!< Timeout occurred on command */
        case espCLOSED: return ERR_CLSD;                 /*!< Connection just closed */
        case espINPROG: return ERR_INPROGRESS;           /*!< Operation is in progress */
        case espERRNOIP: return ERR_ISCONN;              /*!< Station does not have IP address */
        case espERRBLOCKING: return ERR_WOULDBLOCK;
        default:
            _dbg("Unknown ESP err");
            return -1;
    }
}

struct esp_con_reg_rec *esp_con_registry = NULL;


/* callback functions for TCP */
static err_t
altcp_esp_accept(void *arg, esp_netconn_p new_tpcb, err_t err) {
    _dbg("altcp_esp_accept");
    struct altcp_pcb *listen_conn = (struct altcp_pcb *)arg;
    if (listen_conn && listen_conn->accept) {
        /* create a new altcp_conn to pass to the next 'accept' callback */
        struct altcp_pcb *new_conn = altcp_alloc();
        if (new_conn == NULL) {
            return ERR_MEM;
        }
        altcp_esp_setup(new_conn, new_tpcb);
        return listen_conn->accept(listen_conn->arg, new_conn, err);
    }
    return ERR_ARG;
}

static err_t
altcp_esp_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    _dbg("altcp_esp_connected");
    return ERR_OK;
}

static err_t
altcp_esp_recv(void *arg, struct altcp_pcb *tpcb, struct pbuf *p, err_t err) {
    _dbg("altcp_esp_recv");
    struct altcp_pcb *conn = (struct altcp_pcb *)arg;
    
    if(conn) {
        //ALTCP_TCP_ASSERT_CONN_PCB(conn, tpcb);
        if(conn->recv) {
            return conn->recv(conn->arg, conn, p, err);
        }
    }
    if(p != NULL) {
        /* prevent memory leaks */
        pbuf_free(p);
    }
    return ERR_OK;
}

static err_t
altcp_esp_sent(void *arg, struct altcp_pcb *tpcb, u16_t len) {
    _dbg("altcp_esp_sent");
    struct altcp_pcb *conn = (struct altcp_pcb *)arg;
    if (conn) {
        //   ALTCP_TCP_ASSERT_CONN_PCB(conn, tpcb);
        if (conn->sent) {
            return conn->sent(conn->arg, conn, len);
        }
    }
    return ERR_OK;
}

static err_t
altcp_esp_poll(void *arg, struct altcp_pcb *tpcb) {
    _dbg("altcp_esp_poll");
    // struct altcp_pcb *conn = (struct altcp_pcb *)arg;
    // if (conn) {
    //   ALTCP_TCP_ASSERT_CONN_PCB(conn, tpcb);
    //   if (conn->poll) {
    //     return conn->poll(conn->arg, conn);
    //   }
    // }
    return ERR_OK;
}

static void
altcp_esp_err(void *arg, err_t err) {
    _dbg("altcp_esp_err");
    // struct altcp_pcb *conn = (struct altcp_pcb *)arg;
    // if (conn) {
    //   conn->state = NULL; /* already freed */
    //   if (conn->err) {
    //     conn->err(conn->arg, err);
    //   }
    //   altcp_free(conn);
    // }
}

/* setup functions */

static void
altcp_esp_remove_callbacks(struct altcp_pcb *pcb) {
    _dbg("altcp_esp_remove_callbacks");
    LWIP_ASSERT_CORE_LOCKED();
    if (pcb != NULL) {
        pcb->recv = NULL;
        pcb->sent = NULL;
        pcb->err = NULL;
        pcb->poll = NULL;
    }
    // tcp_arg(tpcb, NULL);
    // tcp_recv(tpcb, NULL);
    // tcp_sent(tpcb, NULL);
    // tcp_err(tpcb, NULL);
    // tcp_poll(tpcb, NULL, tpcb->pollinterval);
}

static void
altcp_esp_setup_callbacks(struct altcp_pcb *pcb, esp_netconn_p tpcb) {
    _dbg("altcp_esp_setup_callbacks");
    LWIP_ASSERT_CORE_LOCKED();
    if (pcb != NULL) {
        pcb->recv = altcp_esp_recv;
        pcb->sent = altcp_esp_sent;
        pcb->err = altcp_esp_err;
        // TODO: THIS SHOULD BE THE OTHER WAY ROUND, OR NOT ???
    }

    // tcp_arg(tpcb, conn);
    /* tcp_poll is set when interval is set by application */
    /* listen is set totally different :-) */
}

static void
altcp_esp_setup(struct altcp_pcb *conn, esp_netconn_p tpcb) {
    _dbg("altcp_esp_setup");
    altcp_esp_setup_callbacks(conn, tpcb);
    conn->state = tpcb;
    conn->fns = &altcp_esp_functions;
    tpcb->mbox_accept = conn; // TODO: Not nice, store conn pointer in mbox accept
}

static esp_netconn_t* listen_api;

static espr_t altcp_esp_evt(esp_evt_t* evt) {
    // _dbg("altcp_esp_evt");
    esp_conn_p conn;
    esp_netconn_t* nc = NULL;
    uint8_t close = 0;

    conn = esp_conn_get_from_evt(evt);          /* Get connection from event */
    switch (esp_evt_get_type(evt)) {
        /*
         * A new connection has been active
         * and should be handled by netconn API
         */
        case ESP_EVT_CONN_ACTIVE: {             /* A new connection active is active */
            _dbg("ESP_EVT_CONN_ACTIVE");
            if (esp_conn_is_client(conn)) {     /* Was connection started by us? */
                _dbg("ESP_EVT_CONN_ACTIVE - CLIENT");
                nc = esp_conn_get_arg(conn);    /* Argument should be already set */
                if (nc != NULL) {
                    nc->conn = conn;            /* Save actual connection */
                } else {
                    close = 1;                  /* Close this connection, invalid netconn */
                }
            } else if (esp_conn_is_server(conn) && listen_api != NULL) {    /* Is the connection server type and we have known listening API? */
                _dbg("ESP_EVT_CONN_ACTIVE - SERVER");
                /*
                 * Create a new netconn structure
                 * and set it as connection argument.
                 */
                nc = esp_netconn_new(ESP_NETCONN_TYPE_TCP); /* Create new API */
                ESP_DEBUGW(ESP_CFG_DBG_NETCONN | ESP_DBG_TYPE_TRACE | ESP_DBG_LVL_WARNING,
                    nc == NULL, "[NETCONN] Cannot create new structure for incoming server connection!\r\n");

                if (nc != NULL) {
                    nc->conn = conn;            /* Set connection handle */
                    esp_conn_set_arg(conn, nc); /* Set argument for connection */

                    /*
                     * In case there is no listening connection,
                     * simply close the connection
                     */
                    // if (!esp_sys_mbox_isvalid(&listen_api->mbox_accept) ||
                    //     !esp_sys_mbox_putnow(&listen_api->mbox_accept, nc)) {
                    //     close = 1;
                    // }
                    altcp_esp_accept(listen_api->mbox_accept, nc, 0); // mboxaccept actually a pointer to altcp conn

                } else {
                    close = 1;
                }
            } else {
                _dbg("ESP_EVT_CONN_ACTIVE - OTHER");
                ESP_DEBUGW(ESP_CFG_DBG_NETCONN | ESP_DBG_TYPE_TRACE | ESP_DBG_LVL_WARNING, listen_api == NULL,
                    "[NETCONN] Closing connection as there is no listening API in netconn!\r\n");
                close = 1;                      /* Close the connection at this point */
            }

            /* Decide if some events want to close the connection */
            if (close) {
                if (nc != NULL) {
                    esp_conn_set_arg(conn, NULL);   /* Reset argument */
                    esp_netconn_delete(nc);     /* Free memory for API */
                }
                esp_conn_close(conn, 0);        /* Close the connection */
                close = 0;
            }
            break;
        }

        /*
         * We have a new data received which
         * should have netconn structure as argument
         */
        case ESP_EVT_CONN_RECV: {
            _dbg("ESP_EVT_CONN_RECV");
            esp_pbuf_p pbuf;

            nc = esp_conn_get_arg(conn);        /* Get API from connection */
            pbuf = esp_evt_conn_recv_get_buff(evt); /* Get received buff */

            esp_conn_recved(conn, pbuf);        /* Notify stack about received data */
            nc->rcv_packets++;                  /* Increase number of received packets */

            /*
             * First increase reference number to prevent
             * other thread to process the incoming packet
             * and free it while we still need it here
             *
             * In case of problems writing packet to queue,
             * simply force free to decrease reference counter back to previous value
             */
            esp_pbuf_ref(pbuf);                 /* Increase reference counter */
            // if (!nc || !esp_sys_mbox_isvalid(&nc->mbox_receive)
            //     || !esp_sys_mbox_putnow(&nc->mbox_receive, pbuf)) {
            //     ESP_DEBUGF(ESP_CFG_DBG_NETCONN,
            //         "[NETCONN] Ignoring more data for receive!\r\n");
            //     esp_pbuf_free(pbuf);            /* Free pbuf */
            //     return espOKIGNOREMORE;         /* Return OK to free the memory and ignore further data */
            // }
            if(!nc) {
                esp_pbuf_free(pbuf);
            }
            struct altcp_pcb *pcb = nc->mbox_accept;

            // Copy pbuf data pbuf to pbuf
            struct pbuf *lwip_pbuf = pbuf_alloc(PBUF_TRANSPORT, esp_pbuf_length(pbuf, 0), PBUF_RAM);
            esp_pbuf_copy(pbuf, lwip_pbuf->payload, esp_pbuf_length(pbuf, 0), 0);
            esp_pbuf_free(pbuf);

            altcp_esp_recv(pcb, pcb, lwip_pbuf, 0);

            ESP_DEBUGF(ESP_CFG_DBG_NETCONN | ESP_DBG_TYPE_TRACE,
                "[NETCONN] Written %d bytes to receive mbox\r\n",
                (int)esp_pbuf_length(pbuf, 0));
            break;
        }

        /* Connection was just closed */
        case ESP_EVT_CONN_CLOSED: {
            _dbg("ESP_EVT_CONN_CLOSED");
            // nc = esp_conn_get_arg(conn);        /* Get API from connection */

            // /*
            //  * In case we have a netconn available,
            //  * simply write pointer to received variable to indicate closed state
            //  */
            // if (nc != NULL && esp_sys_mbox_isvalid(&nc->mbox_receive)) {
            //     esp_sys_mbox_putnow(&nc->mbox_receive, (void *)&recv_closed);
            // }

            break;
        }
        case ESP_EVT_CONN_SEND:
            _dbg("ESP_EVT_CONN_SEND");

            nc = esp_conn_get_arg(conn);
            struct altcp_pcb *pcb = nc->mbox_accept;
            const size_t sent = esp_evt_conn_send_get_length(evt);

            altcp_esp_sent(pcb, pcb, sent);

        case ESP_EVT_CONN_POLL:
            // _dbg("Unhandled pol event");
            return espERR;
        default:
            _dbg("Unknown event type: %d", esp_evt_get_type(evt));
            return espERR;
    }
    return espOK;
}

struct altcp_pcb *
altcp_esp_new_ip_type(u8_t ip_type) {
    _dbg("altcp_esp_new_ip_type");
    /* Allocate the tcp pcb first to invoke the priority handling code
     if we're out of pcbs */
    // TODO: Handle IP typoe somehow, this is not netconn type
    esp_netconn_p tpcb = esp_netconn_new(ESP_NETCONN_TYPE_TCP);
    if (tpcb != NULL) {
        struct altcp_pcb *ret = altcp_alloc();
        if (ret != NULL) {
            altcp_esp_setup(ret, tpcb);
            return ret;
        } else {
            /* altcp_pcb allocation failed -> free the tcp_pcb too */
            esp_netconn_delete(tpcb);
        }
    }
    return NULL;
}

/** altcp_esp allocator function fitting to @ref altcp_allocator_t / @ref altcp_new.
*
* arg pointer is not used for TCP.
*/
struct altcp_pcb *
altcp_esp_alloc(void *arg, u8_t ip_type) {
    _dbg("altcp_esp_alloc");
    LWIP_UNUSED_ARG(arg);
    return altcp_esp_new_ip_type(ip_type);
}

struct altcp_pcb *
altcp_esp_wrap(struct tcp_pcb *tpcb) {
    _dbg("altcp_esp_wrap");
    // if (tpcb != NULL) {
    //   struct altcp_pcb *ret = altcp_alloc();
    //   if (ret != NULL) {
    //     altcp_esp_setup(ret, tpcb);
    //     return ret;
    //   }
    // }
    return NULL;
}

/* "virtual" functions calling into tcp */
static void
altcp_esp_set_poll(struct altcp_pcb *conn, u8_t interval) {
    _dbg("altcp_esp_set_poll");
    // if (conn != NULL) {
    //   struct tcp_pcb *pcb = (struct tcp_pcb *)conn->state;
    //   ALTCP_TCP_ASSERT_CONN(conn);
    //   tcp_poll(pcb, altcp_esp_poll, interval);
    // }
}

static void
altcp_esp_recved(struct altcp_pcb *conn, u16_t len) {
    _dbg("altcp_esp_recved");
    // if (conn != NULL) {
    //   struct tcp_pcb *pcb = (struct tcp_pcb *)conn->state;
    //   ALTCP_TCP_ASSERT_CONN(conn);
    //tcp_recved(pcb, len);
    // }
}

static err_t
altcp_esp_bind(struct altcp_pcb *conn, const ip_addr_t *ipaddr, u16_t port) {
    _dbg("altcp_esp_bind");
    if (conn == NULL) {
        return ERR_VAL;
    }
    esp_netconn_p pcb = (esp_netconn_p)conn->state;
    // TODO: ESP does not support listening on IP ???
    return espr_t2err_t(esp_netconn_bind(pcb, port));
}

static err_t
altcp_esp_connect(struct altcp_pcb *conn, const ip_addr_t *ipaddr, u16_t port, altcp_connected_fn connected) {
    _dbg("altcp_esp_connect");
    // struct tcp_pcb *pcb;
    // if (conn == NULL) {
    return ERR_VAL;
    // }
    // ALTCP_TCP_ASSERT_CONN(conn);
    // conn->connected = connected;
    // pcb = (struct tcp_pcb *)conn->state;
    // return tcp_connect(pcb, ipaddr, port, altcp_esp_connected);
}

static struct altcp_pcb *
altcp_esp_listen(struct altcp_pcb *conn, u8_t backlog, err_t *err) {
    _dbg("altcp_esp_listen");
    if (conn == NULL) {
        return NULL;
    }

    esp_netconn_p nc = (esp_netconn_p)conn->state;
    nc->mbox_accept = conn; // TODO: THis is not nice, we do not use accept mbox so we use it to hold altcp conn pointer

    /*
    if (esp_netconn_listen(nc) != espOK) {
        _dbg("listen failed");
        return NULL;
    }

   /* esp_netconn_p client;
    _dbg("Accepting connection");
    if(esp_netconn_accept(nc, &client) != espOK) {
        _dbg("accept failed");
        return NULL;
    }
    _dbg("Connection accepted");*/


    /* Enable server on port and set default netconn callback */
    if(esp_set_server(1, nc->listen_port, ESP_U16(ESP_MIN(backlog, ESP_CFG_MAX_CONNS)), nc->conn_timeout, altcp_esp_evt, NULL, NULL, 1) != espOK) {
        _dbg("Failed to set connection to server mode");
    }
    listen_api = nc;



    //tcp_accept(nc, altcp_esp_accept);
    //nc->accept = altcp_esp_accept;

    // Register connection (we need to provide callback from esp service thread for accepted connections)
    /*struct esp_con_reg_rec *next = (struct esp_con_reg_rec*)mem_malloc(sizeof(struct esp_con_reg_rec));
    next->pcb = conn;
    next->next = NULL;
    if(esp_con_registry) {
        esp_con_registry->next = next;
    } else {
        struct esp_con_reg_rec *prev = esp_con_registry;
        while(prev->next) {
            prev = prev->next;
        }
        prev->next = next;
    }*/
    
    return conn; // Return the same connection as we do not realocate listening pcb to save space
}

static void
altcp_esp_abort(struct altcp_pcb *conn) {
    _dbg("altcp_esp_abort");
    // if (conn != NULL) {
    //   struct tcp_pcb *pcb = (struct tcp_pcb *)conn->state;
    //   ALTCP_TCP_ASSERT_CONN(conn);
    //   if (pcb) {
    //     tcp_abort(pcb);
    //   }
    // }
}

static err_t
altcp_esp_close(struct altcp_pcb *conn) {
    _dbg("altcp_esp_close");
    // struct tcp_pcb *pcb;
    // if (conn == NULL) {
    //   return ERR_VAL;
    // }
    // ALTCP_TCP_ASSERT_CONN(conn);
    // pcb = (struct tcp_pcb *)conn->state;
    // if (pcb) {
    //   err_t err;
    //   tcp_poll_fn oldpoll = pcb->poll;
    //   altcp_esp_remove_callbacks(pcb);
    //   err = tcp_close(pcb);
    //   if (err != ERR_OK) {
    //     /* not closed, set up all callbacks again */
    //     altcp_esp_setup_callbacks(conn, pcb);
    //     /* poll callback is not included in the above */
    //     tcp_poll(pcb, oldpoll, pcb->pollinterval);
    //     return err;
    //   }
    //   conn->state = NULL; /* unsafe to reference pcb after tcp_close(). */
    // }
    // altcp_free(conn);
    return ERR_OK;
}

static err_t
altcp_esp_shutdown(struct altcp_pcb *conn, int shut_rx, int shut_tx) {
    _dbg("altcp_esp_shutdown");
    // struct tcp_pcb *pcb;
    // if (conn == NULL) {
    return ERR_VAL;
    // }
    // ALTCP_TCP_ASSERT_CONN(conn);
    // pcb = (struct tcp_pcb *)conn->state;
    // return tcp_shutdown(pcb, shut_rx, shut_tx);
}

extern espr_t conn_send(esp_conn_p conn, const esp_ip_t* const ip, esp_port_t port, const void* data,
            size_t btw, size_t* const bw, uint8_t fau, const uint32_t blocking);

static err_t
altcp_esp_write(struct altcp_pcb *conn, const void *dataptr, u16_t len, u8_t apiflags) {
    _dbg("altcp_esp_write");
    // struct tcp_pcb *pcb;
    // if (conn == NULL) {
    //return ERR_VAL;
    // }
    // ALTCP_TCP_ASSERT_CONN(conn);
    // pcb = (struct tcp_pcb *)conn->state;
    // return tcp_write(pcb, dataptr, len, apiflags);

    esp_netconn_p nc = conn->state;

    //espr_t err = esp_netconn_write(nc, dataptr, len);
    size_t written = 0;
    //espr_t err = esp_conn_send(nc->conn, dataptr, len, &written, 0); // TODO: Flags ignored, we could only set blocking
    espr_t err = conn_send(nc->conn, NULL, 0, dataptr, len, &written, 0, 0);  // TODO: Flags ignored, we could only set blocking

    _dbg("written: %d out of %d", written, len);
    _dbg("error code: %d", err);

    return espr_t2err_t(err);
}

static err_t
altcp_esp_output(struct altcp_pcb *conn) {
    _dbg("altcp_esp_output");
    // struct tcp_pcb *pcb;
    // if (conn == NULL) {
    return ERR_VAL;
    // }
    // ALTCP_TCP_ASSERT_CONN(conn);
    // pcb = (struct tcp_pcb *)conn->state;
    // return tcp_output(pcb);
}

static u16_t
altcp_esp_mss(struct altcp_pcb *conn) {
    _dbg("altcp_esp_mss");
    // struct tcp_pcb *pcb;
    // if (conn == NULL) {
    return 536;
    // }
    // ALTCP_TCP_ASSERT_CONN(conn);
    // pcb = (struct tcp_pcb *)conn->state;
    // return tcp_mss(pcb);
}

static u16_t
altcp_esp_sndbuf(struct altcp_pcb *conn) {
    _dbg("altcp_esp_sndbuf");
    // struct tcp_pcb *pcb;
    // if (conn == NULL) {
    return 256;
    // }
    // ALTCP_TCP_ASSERT_CONN(conn);
    // pcb = (struct tcp_pcb *)conn->state;
    // return tcp_sndbuf(pcb);
}

static u16_t
altcp_esp_sndqueuelen(struct altcp_pcb *conn) {
    _dbg("altcp_esp_sndqueuelen");
    // struct tcp_pcb *pcb;
    // if (conn == NULL) {
    return 0;
    // }
    // ALTCP_TCP_ASSERT_CONN(conn);
    // pcb = (struct tcp_pcb *)conn->state;
    // return tcp_sndqueuelen(pcb);
}

static void
altcp_esp_nagle_disable(struct altcp_pcb *conn) {
    _dbg("altcp_esp_nagle_disable");
    // if (conn && conn->state) {
    //   struct tcp_pcb *pcb = (struct tcp_pcb *)conn->state;
    //   ALTCP_TCP_ASSERT_CONN(conn);
    //   tcp_nagle_disable(pcb);
    // }
}

static void
altcp_esp_nagle_enable(struct altcp_pcb *conn) {
    _dbg("altcp_esp_nagle_ensable");
    // if (conn && conn->state) {
    //   struct tcp_pcb *pcb = (struct tcp_pcb *)conn->state;
    //   ALTCP_TCP_ASSERT_CONN(conn);
    //   tcp_nagle_enable(pcb);
    // }
}

static int
altcp_esp_nagle_disabled(struct altcp_pcb *conn) {
    _dbg("altcp_esp_nagle_disabled");
    // if (conn && conn->state) {
    //   struct tcp_pcb *pcb = (struct tcp_pcb *)conn->state;
    //   ALTCP_TCP_ASSERT_CONN(conn);
    //   return tcp_nagle_disabled(pcb);
    // }
    return 0;
}

static void
altcp_esp_setprio(struct altcp_pcb *conn, u8_t prio) {
    _dbg("altcp_esp_setprio");
    // if (conn != NULL) {
    //   struct tcp_pcb *pcb = (struct tcp_pcb *)conn->state;
    //   ALTCP_TCP_ASSERT_CONN(conn);
    //   tcp_setprio(pcb, prio);
    // }
}

static void
altcp_esp_dealloc(struct altcp_pcb *conn) {
    _dbg("altcp_esp_dealloc");
    // ESP_UNUSED_ARG(conn);
    // ALTCP_TCP_ASSERT_CONN(conn);
    /* no private state to clean up */
}

static err_t
altcp_esp_get_tcp_addrinfo(struct altcp_pcb *conn, int local, ip_addr_t *addr, u16_t *port) {
    _dbg("altcp_esp_get_tcp_addrinfo");
    // if (conn) {
    //   struct tcp_pcb *pcb = (struct tcp_pcb *)conn->state;
    //   ALTCP_TCP_ASSERT_CONN(conn);
    //   return tcp_tcp_get_tcp_addrinfo(pcb, local, addr, port);
    // }
    return ERR_VAL;
}

static ip_addr_t *
altcp_esp_get_ip(struct altcp_pcb *conn, int local) {
    _dbg("altcp_esp_get_ip");
    // if (conn) {
    //   struct tcp_pcb *pcb = (struct tcp_pcb *)conn->state;
    //   ALTCP_TCP_ASSERT_CONN(conn);
    //   if (pcb) {
    //     if (local) {
    //       return &pcb->local_ip;
    //     } else {
    //       return &pcb->remote_ip;
    //     }
    //   }
    // }
    return NULL;
}

static u16_t
altcp_esp_get_port(struct altcp_pcb *conn, int local) {
    _dbg("altcp_esp_get_port");
    // if (conn) {
    //   struct tcp_pcb *pcb = (struct tcp_pcb *)conn->state;
    //   ALTCP_TCP_ASSERT_CONN(conn);
    //   if (pcb) {
    //     if (local) {
    //       return pcb->local_port;
    //     } else {
    //       return pcb->remote_port;
    //     }
    //   }
    // }
    return 0;
}

    #ifdef ESP_DEBUG
static enum tcp_state
altcp_esp_dbg_get_tcp_state(struct altcp_pcb *conn) {
    if (conn) {
        struct tcp_pcb *pcb = (struct tcp_pcb *)conn->state;
        ALTCP_TCP_ASSERT_CONN(conn);
        if (pcb) {
            return pcb->state;
        }
    }
    return CLOSED;
}
    #endif
const struct altcp_functions altcp_esp_functions = {
    altcp_esp_set_poll,
    altcp_esp_recved,
    altcp_esp_bind,
    altcp_esp_connect,
    altcp_esp_listen,
    altcp_esp_abort,
    altcp_esp_close,
    altcp_esp_shutdown,
    altcp_esp_write,
    altcp_esp_output,
    altcp_esp_mss,
    altcp_esp_sndbuf,
    altcp_esp_sndqueuelen,
    altcp_esp_nagle_disable,
    altcp_esp_nagle_enable,
    altcp_esp_nagle_disabled,
    altcp_esp_setprio,
    altcp_esp_dealloc,
    altcp_esp_get_tcp_addrinfo,
    altcp_esp_get_ip,
    altcp_esp_get_port
    #ifdef ESP_DEBUG
    ,
    altcp_esp_dbg_get_tcp_state
    #endif
};

#endif /* ESP_ALTCP */
