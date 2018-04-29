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
#include "i2c_master.h"
#include "gsensor.h"


/******************************************************************************
 *  * FunctionName : user_gsensor_read
 *   * Description  : read gsensor internal data
 *    * Parameters   : uint8 addr - gsensor address
 *     *                uint8 subaddr - gsensor register address
 *      *                uint8 *pData - data point to put read data
 *       *                uint16 len - read length
 *        * Returns      : bool - true or false
 * *******************************************************************************/
bool ICACHE_FLASH_ATTR
user_gsensor_read(uint8 addr, uint8 subaddr, uint8 *pData, uint16 len)
{
    uint8  ack;
    uint16 i;
    
    i2c_master_start();
    i2c_master_writeByte(addr&0xFE);
    ack = i2c_master_getAck();
    
    if (ack) {
        os_printf("addr not ack when tx read cmd 1\n");
        i2c_master_stop();
    }
    i2c_master_writeByte(subaddr);
    ack = i2c_master_getAck();
    if (ack) {
        os_printf("addr not ack when tx read cmd 2\n");
        i2c_master_stop();
    }
    i2c_master_wait(100);
    
    i2c_master_start();
    i2c_master_writeByte(addr|0x01);
    ack = i2c_master_getAck();
    if (ack) {
        os_printf("addr not ack when tx read cmd 3\n");
        i2c_master_stop();
    }
    
    for (i = 0; i < len; i++)
    {
        pData[i] = i2c_master_readByte();
        i2c_master_setAck((i == (len - 1)) ? 1 : 0);
    }
    i2c_master_stop();
    
    
    return true;
}
/******************************************************************************
 *  * FunctionName : user_gsensor_write
 *   * Description  : write gsensor internal data
 *    * Parameters   : uint8 addr - gsensor address
 *     *                uint8 subaddr - gsensor register address
 *      *                uint8 pData - data point to set wriet data
 *       * Returns      : bool - true or false
 ********************************************************************************/

bool ICACHE_FLASH_ATTR
user_gsensor_write_byte(uint8 addr, uint8 subaddr, uint8 pData)
{
    uint8  ack;
    uint16 i;
    
    i2c_master_start();
    i2c_master_writeByte(addr);
    ack = i2c_master_getAck();
    
    if (ack) {
        os_printf("addr not ack when tx write byte cmd 1\n");
        i2c_master_stop();
        return false;
    }
    i2c_master_writeByte(subaddr);
    ack = i2c_master_getAck();
    
    i2c_master_writeByte(pData);
    ack = i2c_master_getAck();
    
    if (ack) {
        os_printf("addr not ack when tx write byte cmd 2\n");
        i2c_master_stop();
        return false;
    }
    i2c_master_stop();
    i2c_master_wait(1000);
    
    return true;
}

bool ICACHE_FLASH_ATTR
user_gsensor_read_x(uint16 *x)
{
    unsigned char g_x[2];
    if(user_gsensor_read(0x24, 0x01, g_x, 2))
    {
        *x = (uint16) (g_x[1]<<2 | g_x[0]>>6);
        return true;
    }
    return false;
}

bool ICACHE_FLASH_ATTR
user_gsensor_read_y(uint16 *y)
{
    unsigned char g_y[2];
    if(user_gsensor_read(0x24, 0x03, g_y, 2))
    {
        *y = (uint16) (g_y[1]<<2 | g_y[0]>>6);
        return true;
    }
    return false;
}

bool ICACHE_FLASH_ATTR
user_gsensor_read_z(uint16 *z)
{
    unsigned char g_z[2];
    if(user_gsensor_read(0x24, 0x05, g_z, 2))
    {
        *z = (uint16) (g_z[1]<<2 | g_z[0]>>6);
        return true;
    }
    return false;
}

void ICACHE_FLASH_ATTR
gsensor_init(void)
{
    uint8 id;
    
    i2c_master_gpio_init();
    i2c_master_wait(1000);
    user_gsensor_read(0x24,0x00, &id,1);
    os_printf("Gsensor ID:0x%x\n", id);
    i2c_master_wait(1000);
    user_gsensor_write_byte(0x24,0x36,0xB6);
    i2c_master_wait(4000);
    user_gsensor_write_byte(0x24,0x33,0x04);
    i2c_master_wait(1000);
    user_gsensor_write_byte(0x24,0x11,0x80);
    i2c_master_wait(1000);
}

void ICACHE_FLASH_ATTR
gsensr_cmd_read(void)
{
    uint16      gx, gy, gz;
    
    user_gsensor_read_x(&gx);
    user_gsensor_read_y(&gy);
    user_gsensor_read_z(&gz);
    
    os_printf("Gsensor X/Y/Z :%u, %u, %u\n", gx, gy, gz);
}

