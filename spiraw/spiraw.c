/******************************************************************************
 * Copyright 2017 Sparkring Computer
 *
 * FileName: spifs.c
 *
 * Description: 
 *
 * Modification history:
 *     2017/10/01, v1.0 File created.
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_common.h"

#include "spiraw.h"
#include "spiffs_params.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static spiraw_config spi_config;
static int erases[FS_FLASH_SIZE/SECTOR_SIZE];

void ICACHE_FLASH_ATTR
spiraw_init(void)
{
    spi_config.phys_size        = FS_FLASH_SIZE;
    spi_config.phys_addr        = FS_FLASH_ADDR;
    spi_config.phys_erase_block = SECTOR_SIZE;
    spi_config.log_block_size   = LOG_BLOCK;
    spi_config.log_page_size    = LOG_PAGE;
}

#define FLASH_UNIT_SIZE 256

static s32_t ICACHE_FLASH_ATTR
esp_raw_readwrite(u32_t addr, u32_t size, u8_t *p, int write)
{
    /*
     * With proper configurarion spiffs never reads or writes more than
     * LOG_PAGE_SIZE
     */

    if (size > spi_config.log_page_size) {
        printf("Invalid size provided to read/write (%d)\n\r", (int) size);
        return SPIFFS_ERR_NOT_CONFIGURED;
    }

    char tmp_buf[spi_config.log_page_size + FLASH_UNIT_SIZE * 2];
    u32_t aligned_addr = addr & (-FLASH_UNIT_SIZE);
    u32_t aligned_size =
        ((size + (FLASH_UNIT_SIZE - 1)) & -FLASH_UNIT_SIZE) + FLASH_UNIT_SIZE;

    int res = spi_flash_read(aligned_addr, (u32_t *) tmp_buf, aligned_size);

    if (res != 0) {
        printf("spi_flash_read failed: %d (%d, %d)\n\r", res, (int) aligned_addr,
               (int) aligned_size);
        return res;
    }

    if (!write) {
        memcpy(p, tmp_buf + (addr - aligned_addr), size);
        return SPIFFS_OK;
    }

    memcpy(tmp_buf + (addr - aligned_addr), p, size);

    res = spi_flash_write(aligned_addr, (u32_t *) tmp_buf, aligned_size);

    if (res != 0) {
//	    printf("spi_flash_write failed: %d (%d, %d)\n\r", res,
//	              (int) aligned_addr, (int) aligned_size);
        return res;
    }

    return SPIFFS_OK;
}

static s32_t ICACHE_FLASH_ATTR
esp_raw_read(u32_t addr, u32_t size, u8_t *dst)
{
    return esp_raw_readwrite(addr, size, dst, 0);
}

static s32_t ICACHE_FLASH_ATTR
esp_raw_write(u32_t addr, u32_t size, u8_t *src)
{
    return esp_raw_readwrite(addr, size, src, 1);
}

static s32_t ICACHE_FLASH_ATTR
esp_raw_erase(u32_t addr, u32_t size)
{
    /*
     * With proper configurarion spiffs always
     * provides here sector address & sector size
     */
    if (size != spi_config.phys_erase_block || addr % spi_config.phys_erase_block != 0) {
        //printf("Invalid size provided to esp_spiffs_erase (%d, %d)\n\r",
        //       (int) addr, (int) size);
        return SPIFFS_ERR_NOT_CONFIGURED;
    }

    return spi_flash_erase_sector(addr / spi_config.phys_erase_block);
}

s32_t ICACHE_FLASH_ATTR
spiraw_read(u32_t addr, u32_t size, u8_t *dst) {
   if (addr < spi_config.phys_addr) {
        //printf("FATAL read addr too low %08x < %08x\n", addr, FS_FLASH_ADDR);
        exit(0);
    }
    if (addr + size > spi_config.phys_addr + spi_config.phys_size) {
        //printf("FATAL read addr too high %08x + %08x > %08x\n", addr, size, FS_FLASH_ADDR + FS_FLASH_SIZE);
        exit(0);
    }

    return esp_raw_read(addr, size, dst);
}

s32_t ICACHE_FLASH_ATTR
spiraw_write(u32_t addr, u32_t size, u8_t *src) {
    int i;

    if (addr < spi_config.phys_addr) {
        //printf("FATAL write addr too low %08x < %08x\n", addr, FS_FLASH_ADDR);
        exit(0);
    }
    if (addr + size > spi_config.phys_addr + spi_config.phys_size) {
        //printf("FATAL write addr too high %08x + %08x > %08x\n", addr, size, FS_FLASH_ADDR + FS_FLASH_SIZE);
        exit(0);
    }

    return esp_raw_write(addr, size, src);
}

s32_t ICACHE_FLASH_ATTR
spiraw_erase(u32_t addr, u32_t size) {
    if (addr & (spi_config.phys_erase_block-1)) {
        //printf("trying to erase at addr %08x, out of boundary\n", addr);
        return -1;
    }
    if (size & (spi_config.phys_erase_block-1)) {
        //printf("trying to erase at with size %08x, out of boundary\n", size);
        return -1;
    }
    erases[(addr - spi_config.phys_addr) / spi_config.phys_erase_block]++; // ???
    return esp_raw_erase(addr, size);
}


void spiraw_info(void)
{
}
