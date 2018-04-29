#include "lwip/debug.h"
#include "lwip/stats.h"
#include "socket_server.h"
#include "lwip/tcp.h"

#include "../spiraw/spiraw.h"
// #include "../player/player.h"
#include "key.h"

#include <string.h>
#include <stdlib.h>

#ifndef SOCKET_DEBUG
#define SOCKET_DEBUG LWIP_DBG_OFF
#endif

#ifndef SOCKET_TCP_PRIO
#define SOCKET_TCP_PRIO TCP_PRIO_MIN
#endif

/** Maximum retries before the connection is aborted/closed.
 * - number of times pcb->poll is called -> default is 4*500ms = 2s;
 * - reset when pcb->sent is called
 */
#ifndef SOCKET_MAX_RETRIES
#define SOCKET_MAX_RETRIES 4
#endif

/** The poll delay is X*500ms */
#ifndef SOCKET_POLL_INTERVAL
#define SOCKET_POLL_INTERVAL 4
#endif


int mp3_volume = 10;
static char  buffer[MAX_WRITE_SIZE];
static u32_t current_address;
static int   current_count;
static u32_t data_address;
static int   data_size;
static int   remain_size;

struct socket_state {
    u8_t            retries;
    char           *buf;          /* File read buffer. */
    int             buf_len;      /* Size of file read buffer, buf. */
	u16_t           pos;
    u32_t           time_started;
    u32_t           unrecved_bytes;
    struct tcp_pcb *pcb;
};

static err_t socket_poll(void *arg, struct tcp_pcb *pcb);
	
/** Like strstr but does not need 'buffer' to be NULL-terminated */
static char* ICACHE_FLASH_ATTR
strnstr(const char* buffer, const char* token, size_t n)
{
    const char* p;
    int tokenlen = (int)strlen(token);
    if (tokenlen == 0) {
        return (char *)buffer;
    }
    for (p = buffer; *p && (p + tokenlen <= buffer + n); p++) {
        if ((*p == *token) && (strncmp(p, token, tokenlen) == 0)) {
            return (char *)p;
        }
    }
    return NULL;
} 

/** Allocate a struct socket_state. */
	
static struct socket_state* ICACHE_FLASH_ATTR
socket_state_alloc(void)
{
    struct socket_state *ret;
    ret = (struct socket_state *)mem_malloc(sizeof(struct socket_state));
    if (ret != NULL) {
        /* Initialize the structure. */
        memset(ret, 0, sizeof(struct socket_state));
    }
    return ret;
}

/** Free a struct socket_state.
 * Also frees the file data if dynamic.
 */
static void ICACHE_FLASH_ATTR
socket_state_free(struct socket_state *ss)
{
    if (ss != NULL) {
        if (ss->buf != NULL) {
            mem_free(ss->buf);
            ss->buf = NULL;
        }
        mem_free(ss);
    }
}

/**
 * The connection shall be actively closed.
 * Reset the sent- and recv-callbacks.
 *
 * @param pcb the tcp pcb to reset callbacks
 * @param hs connection state to free
 */
static err_t ICACHE_FLASH_ATTR
socket_close_conn(struct tcp_pcb *pcb, struct socket_state *ss)
{
    err_t err;
    //LWIP_DEBUGF(SOCKET_DEBUG, ("Closing connection %p\n", (void*)pcb));

    tcp_arg(pcb,  NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb,  NULL);
    tcp_poll(pcb, NULL, 0);
    tcp_sent(pcb, NULL);
	
    if (ss != NULL) {
        socket_state_free(ss);
    }

    err = tcp_close(pcb);
    if (err != ERR_OK) {
        //LWIP_DEBUGF(SOCKET_DEBUG, ("Error %d closing %p\n", err, (void*)pcb));
        /* error closing, try again later in poll */
        tcp_poll(pcb, socket_poll, SOCKET_POLL_INTERVAL);
    }
    return err;
}



/** Call tcp_write() in a loop trying smaller and smaller length
 *
 * @param pcb tcp_pcb to send
 * @param ptr Data to send
 * @param length Length of data to send (in/out: on return, contains the
 *        amount of data sent)
 * @param apiflags directly passed to tcp_write
 * @return the return value of tcp_write
 */
static err_t ICACHE_FLASH_ATTR
socket_write(struct tcp_pcb *pcb, const void* ptr, u16_t *length, u8_t apiflags)
{
    u16_t len;
    err_t err;
    //LWIP_ASSERT("length != NULL", length != NULL);
    len = *length;
    do {
        //LWIP_DEBUGF(SOCKET_DEBUG | LWIP_DBG_TRACE, ("Trying to send %d bytes\n", len));
        //printf("WRITE %d bytes, content: %s", len, ptr);
        err = tcp_write(pcb, ptr, len, apiflags);
        if (err == ERR_MEM) {
            if ((tcp_sndbuf(pcb) == 0) ||
                (tcp_sndqueuelen(pcb) >= TCP_SND_QUEUELEN)) {
                /* no need to try smaller sizes */
                len = 1;
            } else {
                len /= 2;
            }
            //LWIP_DEBUGF(SOCKET_DEBUG | LWIP_DBG_TRACE, 
            //       ("Send failed, trying less (%d bytes)\n", len));
        }
    } while ((err == ERR_MEM) && (len > 1));
	
#if 0
    if (err == ERR_OK) {
        LWIP_DEBUGF(SOCKET_DEBUG | LWIP_DBG_TRACE, ("Sent %d bytes\n", len));
    } else {
        LWIP_DEBUGF(SOCKET_DEBUG | LWIP_DBG_TRACE, ("Send failed with err %d (\"%s\")\n", err, lwip_strerr(err)));
    }
#endif

    *length = len;
    return err;
}

/**
 * Try to send more data on this pcb.
 *
 * @param pcb the pcb to send data
 * @param hs connection state
 */
static u8_t ICACHE_FLASH_ATTR
socket_send_data(struct tcp_pcb *pcb, struct socket_state *ss, char *data, int count)
{
    err_t err;
    u16_t len;
    u8_t  data_to_send = false;
    u16_t sendlen;
	const void *ptr;
    u16_t old_sendlen;

    //LWIP_DEBUGF(SOCKET_DEBUG | LWIP_DBG_TRACE, ("socket_send_data: pcb=%p ss=%p\n", (void*)pcb, (void*)ss));

    /* Send GREETING*/
    //printf("[*] LWIP_GREETING on\n");
    /* If we were passed a NULL state structure pointer, ignore the call. */
    if (ss == NULL) {
        return 0;
    }

    /* Assume no error until we find otherwise */
    err = ERR_OK;
    /* Do we have any more data to send for this file? */
    /* How much data can we send? */
    len = tcp_sndbuf(pcb);
    sendlen = len;
    //printf("[*] socket_send_data after tcp_sndbuf, len=%d, sendlen=%d\n", len, sendlen);
	
	ss->buf = (char*)mem_malloc((mem_size_t)count);
	if (ss->buf != NULL) {
         MEMCPY(ss->buf, data, count);
         ss->buf_len = count;
    } else {
        ss->buf_len = 0;
	}
	ss->pos = 0;
	len = ss->buf_len;
	
	while (len && sendlen) {
        ptr = (const void *)(ss->buf + ss->pos);
        old_sendlen = sendlen;
        err = socket_write(pcb, ptr, &sendlen, 0);
        if ((err == ERR_OK) && (old_sendlen != sendlen)) {
            /* Remember that we added some more data to be transmitted. */
            data_to_send = true;
        } else if (err != ERR_OK) {
             /* special case: socket_write does not try to send 1 byte */
            sendlen = 0;
        }
        ss->pos += sendlen;
        len -= sendlen;
	}

    if (ss->buf != NULL) {
      mem_free(ss->buf);
      ss->buf = NULL;
    }
	
    /* end of sending header*/
    //printf("[*] socket_send_data nothing to send, closed\n");
    socket_close_conn(pcb, ss);
    return 0;
}


#define LWIP_PACKET_MAX_PAYLOAD_LEN 1500
static char packet_payload[LWIP_PACKET_MAX_PAYLOAD_LEN];
static u16_t packet_payload_len = 0;
static int   ishead = 1;
static u32_t filesize = 0;
static u32_t sum      = 0;
static unsigned char b1, b2, b3, b4;

/**
 * Data has been received on this pcb.
 */
static err_t ICACHE_FLASH_ATTR
socket_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	int i;
	u32_t res;
	u32_t total;
	u32_t used;
    struct socket_state *ss = (struct socket_state *)arg;
    
	//printf("[*] socket_recv invoked\n");
    LWIP_DEBUGF(SOCKET_DEBUG | LWIP_DBG_TRACE, ("socket_recv: pcb=%p pbuf=%p err=%s\n", (void*)pcb,
                (void*)p, lwip_strerr(err)));

    if ((err != ERR_OK) || (p == NULL) || (ss == NULL)) {
        /* error or closed by other side? */
        if (p != NULL) {
            /* Inform TCP that we have taken the data. */
         	//printf("p->tot_len %d\n", p->tot_len);
            tcp_recved(pcb, p->tot_len);
            pbuf_free(p);
        }
        if (ss == NULL) {
            /* this should not happen, only to be robust */
            //LWIP_DEBUGF(SOCKET_DEBUG, ("Error, socket_recv: ss is NULL, close\n"));
        }
        socket_close_conn(pcb, ss);
        return ERR_OK;
    }

    struct pbuf *q = p;
    int    count;
	packet_payload_len = 0;
	
	//printf("p->tot_len %d\n", p->tot_len);
    while (q != NULL)
    {
 	    //printf("q->len %d\n", q->len);
        if (packet_payload_len + q->len <= LWIP_PACKET_MAX_PAYLOAD_LEN) {
            MEMCPY(packet_payload + packet_payload_len, q->payload, q->len);
            packet_payload_len += q->len;
        } else {
			// data missed!!! it does not happen, because packet length is 1440.
           tcp_recved(pcb, packet_payload_len);
           //pbuf_free(p);
           break;
        }
        q = q->next;
    }

    /* Inform TCP that we have taken the data. */
	if (q == NULL) { // all buffers are received.
        tcp_recved(pcb, p->tot_len);
        pbuf_free(p);
	}
	
	if (ishead == 1) {
		
		b1 = packet_payload[0];
		b2 = packet_payload[1];
		b3 = packet_payload[2];
		b4 = packet_payload[3];
		
		if (b1 == 0xff && b2 == 0xff) {
			if (b3 == 1) {
				mp3_volume = b4;
                printf("volume %d", b4);
			}
			socket_close_conn(pcb, ss);
            return ERR_OK;
		}
        led_14(1);
		//player_stop();
		
		filesize = (b1 << 24) | (b2 << 16) | (b3 << 8) | b4;
		ishead = 0;
		sum    = 0;
	    printf("size %d\n", filesize);
		
		spiraw_init();
		//spiraw_erase(INFO_ADDRESS, SECTOR_SIZE);
		res = spi_flash_erase_sector(INFO_ADDRESS/SECTOR_SIZE);
		if (res != 0) {
			printf("spi_flash_erase_sector failed! %d\n", res);
		}

		MEMCPY(buffer, packet_payload, 4);
		buffer[4] = 0xFF;
		buffer[5] = 0x00;
		buffer[6] = 0xFF;
		buffer[7] = 0x00;
 		//spiraw_write(INFO_ADDRESS, MAX_WRITE_SIZE, buffer);
 		res = spi_flash_write(INFO_ADDRESS, (u32_t *)buffer, MAX_WRITE_SIZE);
		if (res != 0) {
			printf("spi_flash_write failed! %d\n", res);
		}
		
		current_address = DATA_ADDRESS;
        current_count   = 0;
		data_address    = (u32_t)packet_payload+4;
		data_size       = packet_payload_len-4;
		remain_size     = 0;
		
		//spiraw_erase(current_address, SECTOR_SIZE);
		res = spi_flash_erase_sector(current_address/SECTOR_SIZE);
		if (res != 0) {
			printf("spi_flash_erase_sector failed! %d\n", res);
		}
        //printf("erase A %x\n", current_address);
		
		while (data_size >= MAX_WRITE_SIZE) {
			MEMCPY(buffer, (char *)data_address, MAX_WRITE_SIZE);
            //printf("move A %d\n", MAX_WRITE_SIZE);
			data_address += MAX_WRITE_SIZE;
			data_size    -= MAX_WRITE_SIZE;
			
		    //spiraw_write(current_address, MAX_WRITE_SIZE, buffer);
 		    res = spi_flash_write(current_address, (u32_t *)buffer, MAX_WRITE_SIZE);
		    if (res != 0) {
			    printf("spi_flash_write failed! %d\n", res);
		    }
            //printf("write A %d\n", MAX_WRITE_SIZE);
			current_address += MAX_WRITE_SIZE;
		}
		MEMCPY(buffer, (char *)data_address, data_size);
        //printf("move B %d\n", data_size);
		remain_size = data_size; // left unwritten data in the benginning of buffer
		
	} else {
		data_address = (u32_t)packet_payload;
		data_size    = packet_payload_len;
		
		while (data_size >= (MAX_WRITE_SIZE - remain_size)) {
	    	if ((current_address & 0x00000fff) == 0) {
				//spiraw_erase(current_address, SECTOR_SIZE);
		        res = spi_flash_erase_sector(current_address/SECTOR_SIZE);
		        if (res != 0) {
		        	printf("spi_flash_erase_sector failed! %d\n", res);
		        }
                //printf("erase B %x\n", current_address);
			}
			if (remain_size > 0) {
			    MEMCPY(buffer+remain_size, (char *)data_address, MAX_WRITE_SIZE-remain_size);
                //printf("move C %d\n", MAX_WRITE_SIZE-remain_size);
			    data_address += MAX_WRITE_SIZE - remain_size;
			    data_size    -= MAX_WRITE_SIZE - remain_size;
			} else {
			    MEMCPY(buffer, (char *)data_address, MAX_WRITE_SIZE);
                //printf("move D %d\n", MAX_WRITE_SIZE);
			    data_address += MAX_WRITE_SIZE;
			    data_size    -= MAX_WRITE_SIZE;
			}
		    //spiraw_write(current_address, MAX_WRITE_SIZE, buffer);
 		    res = spi_flash_write(current_address, (u32_t *)buffer, MAX_WRITE_SIZE);
		    if (res != 0) {
			    printf("spi_flash_write failed! %d\n", res);
		    }
            //printf("write B %x\n", current_address);
			current_address += MAX_WRITE_SIZE;
            remain_size = 0;
		}
		MEMCPY(buffer + remain_size, (char *)data_address, data_size);
        //printf("move E %d\n", data_size);
		remain_size += data_size; // left unwritten data in the benginning of buffer
	}
	
	sum += packet_payload_len;
    //printf("sum %d\n", sum - 4);
    printf("*");
	
	if ((filesize + 4) <= sum) {
		ishead = 1;
        printf("\nfinal sum %d\n", sum - 4);
        if ((current_address & 0x00000fff) == 0) {
            //spiraw_erase(current_address, SECTOR_SIZE);
		    res = spi_flash_erase_sector(current_address/SECTOR_SIZE);
		    if (res != 0) {
			    printf("spi_flash_erase_sector failed! %d\n", res);
		    }
            //printf("erase C %x\n", current_address);
        }
        //spiraw_write(current_address, MAX_WRITE_SIZE, buffer); // write the remaining data
        res = spi_flash_write(current_address, (u32_t *)buffer, MAX_WRITE_SIZE);
		if (res != 0) {
			printf("spi_flash_write failed! %d\n", res);
		}
        //printf("write C %x\n", current_address);
		res = spi_flash_erase_sector(INFO_ADDRESS/SECTOR_SIZE);
		if (res != 0) {
			printf("spi_flash_erase_sector failed! %d\n", res);
		}
		buffer[0] = b1;
		buffer[1] = b2;
		buffer[2] = b3;
		buffer[3] = b4;
 		buffer[4] = 0xF1;
		buffer[5] = 0x1F;
		buffer[6] = 0x55;
		buffer[7] = 0xAA;
  		res = spi_flash_write(INFO_ADDRESS, (u32_t *)buffer, MAX_WRITE_SIZE);
		if (res != 0) {
			printf("spi_flash_write failed! %d\n", res);
		}

        socket_close_conn(pcb, ss);
        led_14(0);
		system_restart(); // reboot system
 	}
	
    return ERR_OK;
}


/**
 * The pcb had an error and is already deallocated.
 * The argument might still be valid (if != NULL).
 */
static void ICACHE_FLASH_ATTR
socket_err(void *arg, err_t err)
{
    //printf("[*] socket_err invoked\n");
    struct socket_state *ss = (struct socket_state *)arg;
    LWIP_UNUSED_ARG(err);

    //LWIP_DEBUGF(SOCKET_DEBUG, ("http_err: %s", lwip_strerr(err)));

    if (ss != NULL) {
        socket_state_free(ss);
    }
}


/**
 * The poll function is called every 2nd second.
 * If there has been no data sent (which resets the retries) in 8 seconds, close.
 * If the last portion of a file has not been sent in 2 seconds, close.
 *
 * This could be increased, but we don't want to waste resources for bad connections.
 */
static err_t ICACHE_FLASH_ATTR
socket_poll(void *arg, struct tcp_pcb *pcb)
{
    //printf("[*] socket_poll invoked\n");
    struct socket_state *ss = (struct socket_state *)arg;
	
	return ERR_OK;
	
    //LWIP_DEBUGF(SOCKET_DEBUG | LWIP_DBG_TRACE, ("socket_poll: pcb=%p hs=%p pcb_state=%s\n",
    //            (void*)pcb, (void*)hs, tcp_debug_state_str(pcb->state)));

    if (ss == NULL) {
        err_t closed;
        /* arg is null, close. */
        //LWIP_DEBUGF(SOCKET_DEBUG, ("socket_poll: arg is NULL, close\n"));
        closed = socket_close_conn(pcb, ss);
        LWIP_UNUSED_ARG(closed);
        return ERR_OK;
    } else {
        ss->retries++;
        if (ss->retries == SOCKET_MAX_RETRIES) {
            //LWIP_DEBUGF(SOCKET_DEBUG, ("socket_poll: too many retries, close\n"));
            socket_close_conn(pcb, ss);
            return ERR_OK;
        }

        /* If this connection has a file open, try to send some more data. If
         * it has not yet received a GET request, don't do this since it will
         * cause the connection to close immediately. */
        //LWIP_DEBUGF(SOCKET_DEBUG | LWIP_DBG_TRACE, ("socket_poll: try to send more data\n"));
        if (socket_send_data(pcb, ss, SOCKET_SERVER_GREETING, strlen(SOCKET_SERVER_GREETING))) {
            /* If we wrote anything to be sent, go ahead and send it now. */
            //LWIP_DEBUGF(SOCKET_DEBUG | LWIP_DBG_TRACE, ("tcp_output\n"));
            tcp_output(pcb);
        }
    }

    return ERR_OK;
}

/**
 * Data has been sent and acknowledged by the remote host.
 * This means that more data can be sent.
 */
static err_t ICACHE_FLASH_ATTR
socket_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    //printf("[*] socket_sent invoked\n");
    struct socket_state *ss = (struct socket_state *)arg;

    //LWIP_DEBUGF(SOCKET_DEBUG | LWIP_DBG_TRACE, ("socket_sent %p\n", (void*)pcb));

    LWIP_UNUSED_ARG(len);

    if (ss == NULL) {
        return ERR_OK;
    }

    ss->retries = 0;

    // socket_send_data(pcb, ss, SOCKET_SERVER_GREETING, strlen(SOCKET_SERVER_GREETING));

    return ERR_OK;
}


/**
 * A new incoming connection has been accepted.
 */
static err_t ICACHE_FLASH_ATTR
socket_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    struct socket_state   *ss;
    struct tcp_pcb_listen *lpcb = (struct tcp_pcb_listen*)arg;
	
    LWIP_UNUSED_ARG(err);
    //LWIP_DEBUGF(HTTPD_DEBUG, ("http_accept %p / %p\n", (void*)pcb, arg));

    /* Decrease the listen backlog counter */
    tcp_accepted(lpcb);
    /* Set priority */
    tcp_setprio(pcb, SOCKET_TCP_PRIO);

    /* Allocate memory for the structure that holds the state of the
       connection - initialized by that function. */
    ss = socket_state_alloc();
    if (ss == NULL) {
        LWIP_DEBUGF(HTTPD_DEBUG, ("socket_accept: Out of memory, RST\n"));
        return ERR_MEM;
    }
	ss->pcb = pcb;

    /* Tell TCP that this is the structure we wish to be passed for our
       callbacks. */
    tcp_arg(pcb, ss);

    /* Set up the various callback functions */
    tcp_recv(pcb, socket_recv);
    tcp_err(pcb,  socket_err);
    tcp_poll(pcb, socket_poll, SOCKET_POLL_INTERVAL);
    tcp_sent(pcb, socket_sent);

    return ERR_OK;
}


void ICACHE_FLASH_ATTR
start_socket_server(void* arg)
{
    struct tcp_pcb *pcb;
    err_t err;

	//printf("start_socket_server...\n");
    pcb = tcp_new();
    tcp_setprio(pcb, SOCKET_TCP_PRIO);
    err = tcp_bind(pcb, IP_ADDR_ANY, SOCKET_SERVER_PORT);
    //LWIP_ASSERT("init_socket_server: tcp_bind failed", err == ERR_OK);
    pcb = tcp_listen(pcb);
    //LWIP_ASSERT("init_socket_server: tcp_listen failed", pcb != NULL);
    /* initialize callback arg and accept callback */
    tcp_arg(pcb, pcb);
    tcp_accept(pcb, socket_accept);
}
