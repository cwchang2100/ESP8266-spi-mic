/******************************************************************************
 * Copyright 2013-2015 Espressif Systems
 *
 * FileName: user_main.c
 *
 * Description: Driver for a 23LC1024 or similar chip connected to the SPI port.
 * The chip is connected to the same pins as the main flash chip except for the
 * /CS pin: that needs to be connected to IO0. The chip is driven in 1-bit SPI
 * mode: theoretically, we can move data faster by using double- or quad-SPI
 * mode but that is not implemented here. The chip also is used like a generic
 * SPI device, nothing memory-mapped like the main flash. Also: these routines
 * are not thread-safe; use mutexes around them if you access the SPI RAM from
 * different threads.
 *
 * Modification history:
 *     2015/06/01, v1.0 File created.
 *     2015/06/12, modifications for SPI in QSPI mode
*******************************************************************************/
#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "spimic.h"

#define SPI 			0
#define HSPI			1

//Macro to quickly access the W-registers of the SPI peripherial
#define SPI_W(i)                   (REG_SPI_BASE(i) + 0x40)

//#define SPIRAM_QIO 1

//Initialize the SPI port to talk to the chip.
void ICACHE_FLASH_ATTR spiMicInit() {
	char dummy[64];
	 //hspi overlap to spi, two spi masters on cspi
	//#define HOST_INF_SEL 0x3ff00028 
	SET_PERI_REG_MASK(0x3ff00028, BIT(7));
	//SET_PERI_REG_MASK(HOST_INF_SEL, PERI_IO_CSPI_OVERLAP);

	//set higher priority for spi than hspi
	SET_PERI_REG_MASK(SPI_EXT3(SPI), 0x1);
	SET_PERI_REG_MASK(SPI_EXT3(HSPI), 0x3);
	SET_PERI_REG_MASK(SPI_USER(HSPI), BIT(5));

	//select HSPI CS2 ,disable HSPI CS0 and CS1 // GPIO0
	CLEAR_PERI_REG_MASK(SPI_PIN(HSPI), SPI_CS2_DIS);
	SET_PERI_REG_MASK(SPI_PIN(HSPI), SPI_CS0_DIS |SPI_CS1_DIS);

	//SET IO MUX FOR GPIO0 , SELECT PIN FUNC AS SPI CS2
	//IT WORK AS HSPI CS2 AFTER OVERLAP(THERE IS NO PIN OUT FOR NATIVE HSPI CS1/2)
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_SPICS2);

	WRITE_PERI_REG(SPI_CLOCK(HSPI), 
					(((0)&SPI_CLKDIV_PRE)<<SPI_CLKDIV_PRE_S)|
					(((3)&SPI_CLKCNT_N)<<SPI_CLKCNT_N_S)|
					(((1)&SPI_CLKCNT_H)<<SPI_CLKCNT_H_S)|
					(((3)&SPI_CLKCNT_L)<<SPI_CLKCNT_L_S));

#ifdef SPIRAM_QIO
	SET_PERI_REG_MASK(SPI_USER(HSPI), SPI_CS_SETUP|SPI_CS_HOLD|SPI_USR_COMMAND);
	CLEAR_PERI_REG_MASK(SPI_USER(HSPI), SPI_FLASH_MODE);

	SET_PERI_REG_MASK(SPI_CTRL(HSPI), SPI_QIO_MODE|SPI_FASTRD_MODE);
	SET_PERI_REG_MASK(SPI_USER(HSPI),SPI_FWRITE_QIO);
#endif
}

//Read bytes from a memory location. The max amount of bytes that can be read is 64.
void spiMicRead(char *buff, int len) {
	int *p=(int*)buff;
	int d1, d2;
	int i=0;
	while(READ_PERI_REG(SPI_CMD(HSPI))&SPI_USR) ;
#ifndef SPIRAM_QIO
	SET_PERI_REG_MASK(SPI_USER(HSPI), SPI_CS_SETUP|SPI_CS_HOLD|SPI_USR_COMMAND|SPI_USR_ADDR|SPI_USR_MISO);
	CLEAR_PERI_REG_MASK(SPI_USER(HSPI), SPI_FLASH_MODE|SPI_USR_MOSI);
	WRITE_PERI_REG(SPI_USER1(HSPI), ((0&SPI_USR_MOSI_BITLEN)<<SPI_USR_MOSI_BITLEN_S)| //no data out
			(((8-1)&SPI_USR_MISO_BITLEN)<<SPI_USR_MISO_BITLEN_S)| //len bits of data in
			((0&SPI_USR_ADDR_BITLEN)<<SPI_USR_ADDR_BITLEN_S)); //address is 16 bits A0-A15
	//WRITE_PERI_REG(SPI_ADDR(HSPI), addr<<16); //write address
	//WRITE_PERI_REG(SPI_USER2(HSPI), (((7&SPI_USR_COMMAND_BITLEN)<<SPI_USR_COMMAND_BITLEN_S) | 0x03));
#else
	SET_PERI_REG_MASK(SPI_USER(HSPI), SPI_USR_ADDR|SPI_USR_MISO|SPI_USR_DUMMY);
	CLEAR_PERI_REG_MASK(SPI_USER(HSPI), SPI_FLASH_MODE|SPI_USR_MOSI|SPI_USR_COMMAND);
	WRITE_PERI_REG(SPI_USER1(HSPI), ((0&SPI_USR_MOSI_BITLEN)<<SPI_USR_MOSI_BITLEN_S)| //no data out
			(((8-1)&SPI_USR_MISO_BITLEN)<<SPI_USR_MISO_BITLEN_S)| //len bits of data in
			((0&SPI_USR_ADDR_BITLEN)<<SPI_USR_ADDR_BITLEN_S)|				//8bits command+address is 24 bits A0-A23
			((0&SPI_USR_DUMMY_CYCLELEN)<<SPI_USR_DUMMY_CYCLELEN_S)); 		//8bits dummy cycle
			
	//WRITE_PERI_REG(SPI_ADDR(HSPI), addr<<16); //write address
#endif

	while (len>0) {
		
	    SET_PERI_REG_MASK(SPI_CMD(HSPI), SPI_USR);
	    while(READ_PERI_REG(SPI_CMD(HSPI))&SPI_USR);
	    //Unaligned dest address. Copy 8bit at a time
		
		d1=READ_PERI_REG(SPI_W(HSPI));
		d2=READ_PERI_REG(SPI_W(HSPI));
		
		buff[i*2+0]=(d1>>0)&0xff;
		buff[i*2+1]=(d2>>8)&0xff;
		i++;
		len-=2;
	}
}


//Simple routine to see if the SPI actually stores bytes. This is not a full memory test, but will tell
//you if the RAM chip is connected well.
int ICACHE_FLASH_ATTR spiMicTest() {
	int x;
	int err=0;
	char a[32];
	char b[32];

	spiMicRead(a, 32);
	spiMicRead(b, 32);
	return !err;
}
