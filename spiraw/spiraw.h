/*
 * spiraw.h
 *
 */

#ifndef __SPIRAW_H__
#define __SPIRAW_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_common.h"
#include "spiffs_params.h"

#define MAX_WRITE_SIZE 256
#define MAX_READ_SIZE  128
#define INFO_ADDRESS FS_FLASH_ADDR
#define DATA_ADDRESS (FS_FLASH_ADDR+SECTOR_SIZE)

typedef struct {
  // physical size of the spi flash
  u32_t phys_size;
  // physical offset in spi flash used for spiffs,
  // must be on block boundary
  u32_t phys_addr;
  // physical size when erasing a block
  u32_t phys_erase_block;

  // logical size of a block, must be on physical
  // block size boundary and must never be less than
  // a physical block
  u32_t log_block_size;
  // logical size of a page, must be at least
  // log_block_size / 8
  u32_t log_page_size;
} spiraw_config;

void  spiraw_init(void);
s32_t spiraw_read(u32_t addr, u32_t size, u8_t *dst);
s32_t spiraw_write(u32_t addr, u32_t size, u8_t *src);
s32_t spiraw_erase(u32_t addr, u32_t size);
void  spiraw_info(void);

#endif /* __SPIRAW_H__ */
