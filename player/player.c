#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "player.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "../mad/mad.h"
#include "../mad/stream.h"
#include "../mad/frame.h"
#include "../mad/synth.h"

#include "../spiraw/spiraw.h"

#include "fcntl.h"
#include "unistd.h"

int                fd = -1;
static u32_t filesize  = 0;
static u32_t readcount = 0;

static char  buffer[MAX_READ_SIZE];
static u32_t current_address;
static int   current_count;
static u32_t data_address;
static int   data_size;
static int   remain_size;

#define READBUFSZ (2106)
static char  readBuf[READBUFSZ];
//static char *readBuf;
track_t     *playlist;
int          player_run = 0;

xTaskHandle      xTaskPlayerNotify;
xSemaphoreHandle player_semphr;

OUT_CB out_callback = NULL;
//Reformat the 16-bit mono sample to a format we can send to I2S.
static int ICACHE_FLASH_ATTR
to_i2s(short s) {
    //We can send a 32-bit sample to the I2S subsystem and the DAC will neatly split it up in 2
    //16-bit analog values, one for left and one for right.

    //Duplicate 16-bit sample to both the L and R channel
    int samp = s;
    samp = (samp) & 0xffff;
    samp = (samp << 16) | samp;
    return samp;
}
//Routine to print out an error
static enum mad_flow ICACHE_FLASH_ATTR
error(void *data, struct mad_stream *stream, struct mad_frame *frame)
{
    printf("dec err 0x%04x (%s)\n", stream->error, mad_stream_errorstr(stream));
    return MAD_FLOW_CONTINUE;
}


static track_t * ICACHE_FLASH_ATTR
track_last(void)
{
    track_t *found = playlist;
    while(found->next != NULL) {
        found = found->next;
    }
    return found;
}

static track_t * ICACHE_FLASH_ATTR
track_first(void)
{
    return playlist->next;
}


static track_t *
ICACHE_FLASH_ATTR track_add(const char *song)
{
    track_t *found = track_last();
    if(found == NULL) {
        //printf("Not found last track\n");
        return NULL;
    }
    track_t *new_track = malloc(sizeof(track_t));
    if (new_track == NULL) {
        //printf("Not enough memory\n");
        return NULL;
    }
    int song_len = strlen(song);
    memset(new_track, 0, sizeof(track_t));
    new_track->filename = malloc(song_len + 1);
    if (new_track->filename == NULL) {
        free(new_track);
        return NULL;
    }
    strcpy(new_track->filename, song);
    new_track->filename[song_len + 1] = 0;

    found->next = new_track;
    new_track->prev = found;
    //printf("added: %s, found: %x, new_track: %x\n", song, (int)found, (int)new_track);
    return new_track;
}

static void ICACHE_FLASH_ATTR
track_remove(track_t *song)
{
    if(song == NULL)
        return;
    track_t *prev = song->prev;
    track_t *next = song->next;
    if(prev != NULL) {
        prev->next = next;
    }
    if(next != NULL) {
        next->prev = prev;
    }
    free(song->filename);
    free(song);
}

static void ICACHE_FLASH_ATTR
track_clear(void)
{
    track_t *found;
    while((found = track_last()) != NULL) {
        if(found->prev == NULL) {
            break;
        }
        track_remove(found);
    }

}

static enum mad_flow ICACHE_FLASH_ATTR
input(struct mad_stream *stream) {
    int n;
	int res;
    int rem, read_bytes;
    //Shift remaining contents of buf to the front
    rem = stream->bufend - stream->next_frame;
    memmove(readBuf, stream->next_frame, rem);

    n = (sizeof(readBuf) - rem);
	
    //printf("read %d\n", n);
    data_address = (u32_t)(readBuf + rem);
    data_size    = n;
	read_bytes   = 0;
	
    if (remain_size > 0) {
		if (remain_size > data_size) {
            MEMCPY((char *)data_address, buffer + (MAX_READ_SIZE - remain_size), data_size);
            //printf("copy A %d\n", data_size);
		    data_address += data_size;
			remain_size  -= data_size; // left unplayed data
 		    readcount    += data_size;
			read_bytes   += data_size;
		    data_size    -= data_size;
		} else {
            MEMCPY((char *)data_address, buffer + (MAX_READ_SIZE - remain_size), remain_size);
            //printf("copy B %d\n", remain_size);
		    data_address += remain_size;
		    data_size    -= remain_size; // no left unplayed data
 		    readcount    += remain_size;
			read_bytes   += remain_size;
		    remain_size   = 0;
		}
    }
	
	while (data_size >= MAX_READ_SIZE) {
        //spiraw_read(current_address, MAX_READ_SIZE, buffer);
        res = spi_flash_read(current_address, (u32_t *)buffer, MAX_READ_SIZE);
        if (res != 0) {
            printf("spi_flash_read failed! %d\n", res);
        }
        //printf("read A %x\n", current_address);
		current_address += MAX_READ_SIZE;
		
        MEMCPY((char *)data_address, buffer, MAX_READ_SIZE);
        //printf("copy C %d\n", MAX_READ_SIZE);
		data_address += MAX_READ_SIZE;
		data_size    -= MAX_READ_SIZE;
		remain_size   = 0;
		readcount    += MAX_READ_SIZE;
        read_bytes   += MAX_READ_SIZE;
	}
	if (data_size > 0) {
        //spiraw_read(current_address, MAX_READ_SIZE, buffer);
        res = spi_flash_read(current_address, (u32_t *)buffer, MAX_READ_SIZE);
        if (res != 0) {
            printf("spi_flash_read failed! %d\n", res);
        }
        //printf("read B %x\n", current_address);
        current_address += MAX_READ_SIZE;
	
        MEMCPY((char *)data_address, buffer, data_size);
        //printf("copy D %d\n", data_size);
        data_address += data_size;
 	    remain_size   = MAX_READ_SIZE - data_size; // left unplayed data in the end
        readcount    += data_size;
        read_bytes   += data_size;
        data_size    -= data_size;
	}

    if (readcount >= filesize) {
        return MAD_FLOW_STOP;
    }
    //Okay, let MAD decode the buffer.
    mad_stream_buffer(stream, (unsigned char*)readBuf, read_bytes + rem);
    printf(".");
    return MAD_FLOW_CONTINUE;
}

static void ICACHE_FLASH_ATTR
play(char *song)
{
    int r;
	int res;
	u32_t total;
	unsigned char b1, b2, b3, b4;
    struct mad_stream *stream;
    struct mad_frame  *frame;
    struct mad_synth  *synth;

    //Allocate structs needed for mp3 decoding
    stream = malloc(sizeof(struct mad_stream));
    frame  = malloc(sizeof(struct mad_frame));
    synth  = malloc(sizeof(struct mad_synth));
    //readBuf = malloc(READBUFSZ);

    if (stream == NULL) { printf("MAD: malloc(stream) failed, %d\n", xPortGetFreeHeapSize()); return; }
    if (synth  == NULL) { printf("MAD: malloc(synth) failed, %d\n",  xPortGetFreeHeapSize()); return; }
    if (frame  == NULL) { printf("MAD: malloc(frame) failed, %d\n",  xPortGetFreeHeapSize()); return; }
    //if (readBuf  == NULL) { printf("MAD: malloc(readBuf) failed, %d\n",  xPortGetFreeHeapSize()); return; }

    printf("MAD: Decoder start., %d\n", xPortGetFreeHeapSize());
    //Initialize mp3 parts
    mad_stream_init(stream);
    mad_frame_init(frame);
    mad_synth_init(synth);
    //openfile
    //spiraw_read(INFO_ADDRESS, MAX_READ_SIZE, buffer);
    res = spi_flash_read(INFO_ADDRESS, (u32_t *)buffer, MAX_READ_SIZE);
    if (res != 0) {
        printf("spi_flash_read failed! %d\n", res);
    }
    b1 = buffer[0];
    b2 = buffer[1];
    b3 = buffer[2];
    b4 = buffer[3];
    filesize = (b1 << 24) | (b2 << 16) | (b3 << 8) | b4;
	printf("filesize: %d\n", filesize);
    current_address = DATA_ADDRESS;
    current_count   = 0;
    remain_size     = 0;
	readcount       = 0;

    enum mad_flow m = MAD_FLOW_CONTINUE;
    while(m == MAD_FLOW_CONTINUE) {
        m = input(stream); //calls mad_stream_buffer internally
        while(1) {
            r = mad_frame_decode(frame, stream);
            if(r == -1) {
                if(!MAD_RECOVERABLE(stream->error)) {
                    //We're most likely out of buffer and need to call input() again
                    break;
                }
                error(NULL, stream, frame);
                continue;
            }
            mad_synth_frame(synth, frame);
        }
    }
    //free(readBuf);
    free(stream);
    free(frame);
    free(synth);
	filesize = 0;
    fd = -1;

}

void ICACHE_FLASH_ATTR
player_task(void *pv)
{
    track_t *song;
    //printf("esp_spiffs_mount,%d\n", xPortGetFreeHeapSize());
	
	vSemaphoreCreateBinary(player_semphr);

    printf("Begin player task\n");

    while (player_run) {
        //printf("wait decode...%d\n", xPortGetFreeHeapSize());
		xSemaphoreTake(player_semphr, portMAX_DELAY);
        while ((song = track_first()) != NULL) {
            //printf("playing: %s\n", song->filename);
            play(song->filename);
            //track_remove(song);
        }
    }
    track_clear();
    i2sStop();
    vTaskDelete(NULL);
}

int ICACHE_FLASH_ATTR
player_init(OUT_CB cb)
{
    out_callback = cb;
    playlist = malloc(sizeof(track_t));
    if (playlist == NULL)
        return -1;
    memset(playlist, 0, sizeof(track_t));
    i2sInit();
	
    player_run = 1;
    if (xTaskCreate(&player_task, "player_task", 3200, NULL, 11, &xTaskPlayerNotify) == pdFALSE) {
        printf("Error init player_task\n");
    }
	vTaskDelay(500/portTICK_RATE_MS);
    return 0;
}

void ICACHE_FLASH_ATTR
player_play(const char *file)
{
    track_add(file);
	xSemaphoreGive(player_semphr);
}

void ICACHE_FLASH_ATTR
player_stop()
{
	if (player_run == 1) {
        player_run = 0;
	
        track_clear();
	
        vTaskDelete(xTaskPlayerNotify);
	
	    i2sSilent();
        i2sStop();
	}
}

void ICACHE_FLASH_ATTR
player_suspend()
{
	if (player_run == 1) {
	    vTaskSuspend(xTaskPlayerNotify);
	    i2sSilent();
	}
}

void ICACHE_FLASH_ATTR
player_resume()
{
	if (player_run == 1) {
	    vTaskResume(xTaskPlayerNotify);
	}
}
