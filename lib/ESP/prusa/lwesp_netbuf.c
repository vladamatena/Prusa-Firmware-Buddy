/**
 * @file
 * Network buffer management
 *
 * @defgroup netbuf Network buffers
 * @ingroup netconn
 * Network buffer descriptor for @ref netconn. Based on @ref pbuf internally
 * to avoid copying data around.\n
 * Buffers must not be shared accross multiple threads, all functions except
 * netbuf_new() and netbuf_delete() are not thread-safe.
 */

/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
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
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

#include "lwip/opt.h"

#if LWIP_NETCONN /* don't build if not configured for use in lwipopts.h */

#include "lwesp_netbuf.h"
#include "lwip/memp.h"

#include <string.h>

/**
 * @ingroup netbuf
 * Create (allocate) and initialize a new netbuf.
 * The netbuf doesn't yet contain a packet buffer!
 *
 * @return a pointer to a new netbuf
 *         NULL on lack of memory
 */
struct
lwesp_netbuf *lwesp_netbuf_new(void)
{
  struct lwesp_netbuf *buf;

  buf = (struct lwesp_netbuf *)memp_malloc(MEMP_NETBUF);
  if (buf != NULL) {
    memset(buf, 0, sizeof(struct lwesp_netbuf));
  }
  return buf;
}

/**
 * @ingroup netbuf
 * Deallocate a netbuf allocated by netbuf_new().
 *
 * @param buf pointer to a netbuf allocated by netbuf_new()
 */
void
lwesp_netbuf_delete(struct lwesp_netbuf *buf)
{
  if (buf != NULL) {
    if (buf->p != NULL) {
      lwesp_pbuf_free(buf->p);
      buf->p = buf->ptr = NULL;
    }
    memp_free(MEMP_NETBUF, buf);
  }
}

/**
 * @ingroup netbuf
 * Allocate memory for a packet buffer for a given netbuf.
 *
 * @param buf the netbuf for which to allocate a packet buffer
 * @param size the size of the packet buffer to allocate
 * @return pointer to the allocated memory
 *         NULL if no memory could be allocated
 */
void *
lwesp_netbuf_alloc(struct lwesp_netbuf *buf, u16_t size)
{
  LWIP_ERROR("netbuf_alloc: invalid buf", (buf != NULL), return NULL;);

  /* Deallocate any previously allocated memory. */
  if (buf->p != NULL) {
    lwesp_pbuf_free(buf->p);
  }

//   buf->p = lwesp_pbuf_alloc(PBUF_TRANSPORT, size, PBUF_RAM);
//   TODO: THis is not a direct equivalent
  buf->p = lwesp_pbuf_new(size);
  
  if (buf->p == NULL) {
    return NULL;
  }
  LWIP_ASSERT("check that first pbuf can hold size",
              (lwesp_pbuf_length(buf->p, 0) >= size));
  buf->ptr = buf->p;
  return lwesp_pbuf_data(buf->p);
}

/**
 * @ingroup netbuf
 * Free the packet buffer included in a netbuf
 *
 * @param buf pointer to the netbuf which contains the packet buffer to free
 */
void
lwesp_netbuf_free(struct lwesp_netbuf *buf)
{
  LWIP_ERROR("netbuf_free: invalid buf", (buf != NULL), return;);
  if (buf->p != NULL) {
    lwesp_pbuf_free(buf->p);
  }
  buf->p = buf->ptr = NULL;
#if LWIP_CHECKSUM_ON_COPY
  buf->flags = 0;
  buf->toport_chksum = 0;
#endif /* LWIP_CHECKSUM_ON_COPY */
}

/**
 * @ingroup netbuf
 * Let a netbuf reference existing (non-volatile) data.
 *
 * @param buf netbuf which should reference the data
 * @param dataptr pointer to the data to reference
 * @param size size of the data
 * @return ERR_OK if data is referenced
 *         ERR_MEM if data couldn't be referenced due to lack of memory
 */
/*err_t
lwesp_netbuf_ref(struct lwesp_netbuf *buf, const void *dataptr, u16_t size)
{
  LWIP_ERROR("netbuf_ref: invalid buf", (buf != NULL), return ERR_ARG;);
  if (buf->p != NULL) {
    lwesp_pbuf_free(buf->p);
  }

//   buf->p = lwesp_pbuf_alloc(PBUF_TRANSPORT, 0, PBUF_REF);
//   TODO: THis is not a direct equivalent
  buf->p = lwesp_pbuf_new(0);

  if (buf->p == NULL) {
    buf->ptr = NULL;
    return ERR_MEM;
  }
  ((struct pbuf_rom *)buf->p)->payload = dataptr;
  
  // TODO: is this ok
  //   buf->p->len = buf->p->tot_len = size;
  lwesp_pbuf_set_length(buf->p, size);
  // lwesp does not support setting total length?
  
  
  buf->ptr = buf->p;
  return ERR_OK;
}*/

/**
 * @ingroup netbuf
 * Chain one netbuf to another (@see pbuf_chain)
 *
 * @param head the first netbuf
 * @param tail netbuf to chain after head, freed by this function, may not be reference after returning
 */
void
lwesp_netbuf_chain(struct lwesp_netbuf *head, struct lwesp_netbuf *tail)
{
  LWIP_ERROR("netbuf_chain: invalid head", (head != NULL), return;);
  LWIP_ERROR("netbuf_chain: invalid tail", (tail != NULL), return;);
  lwesp_pbuf_cat(head->p, tail->p);
  head->ptr = head->p;
  memp_free(MEMP_NETBUF, tail);
}

/**
 * @ingroup netbuf
 * Get the data pointer and length of the data inside a netbuf.
 *
 * @param buf netbuf to get the data from
 * @param dataptr pointer to a void pointer where to store the data pointer
 * @param len pointer to an u16_t where the length of the data is stored
 * @return ERR_OK if the information was retrieved,
 *         ERR_BUF on error.
 */
err_t
lwesp_netbuf_data(struct lwesp_netbuf *buf, void **dataptr, u16_t *len)
{
  LWIP_ERROR("netbuf_data: invalid buf", (buf != NULL), return ERR_ARG;);
  LWIP_ERROR("netbuf_data: invalid dataptr", (dataptr != NULL), return ERR_ARG;);
  LWIP_ERROR("netbuf_data: invalid len", (len != NULL), return ERR_ARG;);

  if (buf->ptr == NULL) {
    return ERR_BUF;
  }
  *dataptr = lwesp_pbuf_data(buf->ptr);
  *len = lwesp_pbuf_length(buf->ptr, 0);
  return ERR_OK;
}

/**
 * @ingroup netbuf
 * Move the current data pointer of a packet buffer contained in a netbuf
 * to the next part.
 * The packet buffer itself is not modified.
 *
 * @param buf the netbuf to modify
 * @return -1 if there is no next part
 *         1  if moved to the next part but now there is no next part
 *         0  if moved to the next part and there are still more parts
 */
// s8_t
// lwesp_netbuf_next(struct lwesp_netbuf *buf)
// {
//   LWIP_ERROR("netbuf_next: invalid buf", (buf != NULL), return -1;);
//   if (buf->ptr->next == NULL) {
//     return -1;
//   }
//   buf->ptr = buf->ptr->next;
//   if (buf->ptr->next == NULL) {
//     return 1;
//   }
//   return 0;
// }

/**
 * @ingroup netbuf
 * Move the current data pointer of a packet buffer contained in a netbuf
 * to the beginning of the packet.
 * The packet buffer itself is not modified.
 *
 * @param buf the netbuf to modify
 */
void
lwesp_netbuf_first(struct lwesp_netbuf *buf)
{
  LWIP_ERROR("netbuf_first: invalid buf", (buf != NULL), return;);
  buf->ptr = buf->p;
}

#endif /* LWIP_NETCONN */