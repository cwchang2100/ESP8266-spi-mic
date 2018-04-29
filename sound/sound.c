#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "sound.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "../mad/mad.h"
#include "../mad/stream.h"
#include "../mad/frame.h"
#include "../mad/synth.h"

#include "../spiraw/spiraw.h"

#include "spimic.h"

#include "fcntl.h"
#include "unistd.h"

int                fd  = -1;
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
int          sound_run = 0;

xTaskHandle      xTaskSoundNotify;
xSemaphoreHandle sound_semphr;

static void ICACHE_FLASH_ATTR
play()
{
	int i;
    int low;
    int high;
    int samp;
	
    spiMicRead(buffer, 128);
	
	low  = buffer[0];
	high = buffer[1];
	samp = (high << 8) | low;
	printf("%x\n", samp);
	
	for (i = 0; i < 128; i += 2) {
		low  = buffer[i];
		high = buffer[i+1];
		samp = (high << 8) | low;
		//printf("%x\n", samp);
 	    i2sPushSample(samp);
	}
}

void ICACHE_FLASH_ATTR
sound_task(void *pv)
{
	vSemaphoreCreateBinary(sound_semphr);

    printf("Begin sound task\n");

    while (sound_run) {
		xSemaphoreTake(sound_semphr, portMAX_DELAY);
        while (true) {
            play();
        }
    }
    i2sStop();
    vTaskDelete(NULL);
}

int ICACHE_FLASH_ATTR
sound_init()
{
    i2sInit();
	
    sound_run = 1;
    if (xTaskCreate(&sound_task, "sound_task", 3200, NULL, 11, &xTaskSoundNotify) == pdFALSE) {
        printf("Error init sound_task\n");
    }
	vTaskDelay(500/portTICK_RATE_MS);
    return 0;
}

void ICACHE_FLASH_ATTR
sound_play()
{
	xSemaphoreGive(sound_semphr);
}

void ICACHE_FLASH_ATTR
sound_stop()
{
	if (sound_run == 1) {
        sound_run = 0;
		
        vTaskDelete(xTaskSoundNotify);
	
	    i2sSilent();
        i2sStop();
	}
}

void ICACHE_FLASH_ATTR
sound_suspend()
{
	if (sound_run == 1) {
	    vTaskSuspend(xTaskSoundNotify);
	    i2sSilent();
	}
}

void ICACHE_FLASH_ATTR
sound_resume()
{
	if (sound_run == 1) {
	    vTaskResume(xTaskSoundNotify);
	}
}
