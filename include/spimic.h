#ifndef _SPIMIC_H_
#define _SPIMIC_H_

#define SPIMICSIZE (32*1024) //for a 23LC1024 chip


//Define this to use the SPI RAM in QSPI mode. This mode theoretically improves
//the bandwith to the chip four-fold, but it needs all 4 SDIO pins connected. It's
//disabled here because not everyone using the MP3 example will have those pins 
//connected and the overall speed increase on the MP3 example is negligable.
//#define SPIRAM_QIO


void spiMicInit();
void spiMicRead(char *buff, int len);
int spiMicTest();

#endif