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

#include "spifs.h"
#include "spiffs_params.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

spiffs __fs;
static u8_t _work[LOG_PAGE * 2];
static u8_t _fds[FD_BUF_SIZE * 2];
static u8_t _cache[CACHE_BUF_SIZE];
static char _path[256];
static int erases[FS_FLASH_SIZE/SECTOR_SIZE];

static u32_t fs_check_fixes = 0;

void ICACHE_FLASH_ATTR
spiffs_fs_init(void)
{
    struct esp_spiffs_config config;
    
    config.phys_size        = FS_FLASH_SIZE;
    config.phys_addr        = FS_FLASH_ADDR;
    config.phys_erase_block = SECTOR_SIZE;
    config.log_block_size   = LOG_BLOCK;
    config.log_page_size    = LOG_PAGE;
    config.fd_buf_size      = FD_BUF_SIZE * 2;
    config.cache_buf_size   = CACHE_BUF_SIZE;
    
    esp_spiffs_init(&config);
}

#define FLASH_UNIT_SIZE 4

static s32_t ICACHE_FLASH_ATTR
esp_spiffs_readwrite(u32_t addr, u32_t size, u8_t *p, int write)
{
    /*
     * With proper configurarion spiffs never reads or writes more than
     * LOG_PAGE_SIZE
     */

    if (size > __fs.cfg.log_page_size) {
        printf("Invalid size provided to read/write (%d)\n\r", (int) size);
        return SPIFFS_ERR_NOT_CONFIGURED;
    }

    char tmp_buf[__fs.cfg.log_page_size + FLASH_UNIT_SIZE * 2];
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
esp_spiffs_read(u32_t addr, u32_t size, u8_t *dst)
{
    return esp_spiffs_readwrite(addr, size, dst, 0);
}

static s32_t ICACHE_FLASH_ATTR
esp_spiffs_write(u32_t addr, u32_t size, u8_t *src)
{
    return esp_spiffs_readwrite(addr, size, src, 1);
}

static s32_t ICACHE_FLASH_ATTR
esp_spiffs_erase(u32_t addr, u32_t size)
{
    /*
     * With proper configurarion spiffs always
     * provides here sector address & sector size
     */
    if (size != __fs.cfg.phys_erase_block || addr % __fs.cfg.phys_erase_block != 0) {
        //printf("Invalid size provided to esp_spiffs_erase (%d, %d)\n\r",
        //       (int) addr, (int) size);
        return SPIFFS_ERR_NOT_CONFIGURED;
    }

    return spi_flash_erase_sector(addr / __fs.cfg.phys_erase_block);
}

static s32_t ICACHE_FLASH_ATTR
_read(u32_t addr, u32_t size, u8_t *dst) {
   if (addr < __fs.cfg.phys_addr) {
        //printf("FATAL read addr too low %08x < %08x\n", addr, FS_FLASH_ADDR);
        exit(0);
    }
    if (addr + size > __fs.cfg.phys_addr + __fs.cfg.phys_size) {
        //printf("FATAL read addr too high %08x + %08x > %08x\n", addr, size, FS_FLASH_ADDR + FS_FLASH_SIZE);
        exit(0);
    }

    return esp_spiffs_read(addr, size, dst);
}

static s32_t ICACHE_FLASH_ATTR
_write(u32_t addr, u32_t size, u8_t *src) {
    int i;

    if (addr < __fs.cfg.phys_addr) {
        //printf("FATAL write addr too low %08x < %08x\n", addr, FS_FLASH_ADDR);
        exit(0);
    }
    if (addr + size > __fs.cfg.phys_addr + __fs.cfg.phys_size) {
        //printf("FATAL write addr too high %08x + %08x > %08x\n", addr, size, FS_FLASH_ADDR + FS_FLASH_SIZE);
        exit(0);
    }

    return esp_spiffs_write(addr, size, src);
}

static s32_t ICACHE_FLASH_ATTR
_erase(u32_t addr, u32_t size) {
    if (addr & (__fs.cfg.phys_erase_block-1)) {
        //printf("trying to erase at addr %08x, out of boundary\n", addr);
        return -1;
    }
    if (size & (__fs.cfg.phys_erase_block-1)) {
        //printf("trying to erase at with size %08x, out of boundary\n", size);
        return -1;
    }
    erases[(addr - __fs.cfg.phys_addr) / __fs.cfg.phys_erase_block]++;
    return esp_spiffs_erase(addr, size);
}

static void ICACHE_FLASH_ATTR
spiffs_check_cb_f(spiffs_check_type type, spiffs_check_report report,
                  u32_t arg1, u32_t arg2) {
    if (report != SPIFFS_CHECK_PROGRESS) {
        if (report != SPIFFS_CHECK_ERROR) fs_check_fixes++;
#if 0
        printf("   check: ");
        switch (type) {
            case SPIFFS_CHECK_INDEX:
                printf("INDEX  "); break;
            case SPIFFS_CHECK_LOOKUP:
                printf("LOOKUP "); break;
            case SPIFFS_CHECK_PAGE:
                printf("PAGE   "); break;
            default:
                printf("????   "); break;
        }
        if (report == SPIFFS_CHECK_ERROR) {
            printf("ERROR %i", arg1);
        } else if (report == SPIFFS_CHECK_DELETE_BAD_FILE) {
            printf("DELETE BAD FILE %04x", arg1);
        } else if (report == SPIFFS_CHECK_DELETE_ORPHANED_INDEX) {
            printf("DELETE ORPHANED INDEX %04x", arg1);
        } else if (report == SPIFFS_CHECK_DELETE_PAGE) {
            printf("DELETE PAGE %04x", arg1);
        } else if (report == SPIFFS_CHECK_FIX_INDEX) {
            printf("FIX INDEX %04x:%04x", arg1, arg2);
        } else if (report == SPIFFS_CHECK_FIX_LOOKUP) {
            printf("FIX INDEX %04x:%04x", arg1, arg2);
        } else {
            printf("??");
        }
        printf("\n");
#endif
    }
}

s32_t ICACHE_FLASH_ATTR
spifs_specific_mount(u32_t phys_addr, 
                     u32_t phys_size,
                     u32_t phys_sector_size,
                     u32_t log_block_size, 
                     u32_t log_page_size) {
    spiffs_config c;
    c.hal_erase_f      = _erase;
    c.hal_read_f       = _read;
    c.hal_write_f      = _write;
    c.log_block_size   = log_block_size;
    c.log_page_size    = log_page_size;
    c.phys_addr        = phys_addr;
    c.phys_erase_block = phys_sector_size;
    c.phys_size        = phys_size;

    return SPIFFS_mount(&__fs, &c, _work, _fds, sizeof(_fds), _cache, sizeof(_cache), spiffs_check_cb_f);
}

void ICACHE_FLASH_ATTR
spifs_mount(void)
{
    spiffs_fs_init();
    memset(&__fs, 0, sizeof(__fs));
    s32_t res = spifs_specific_mount(FS_FLASH_ADDR, FS_FLASH_SIZE, SECTOR_SIZE, SECTOR_SIZE, LOG_PAGE);
    if (res == SPIFFS_OK) {
        printf("spifs_mount is OK\n");		
    } else {
        printf("mount failed, %i\n", SPIFFS_errno(FS));
	}
}

void ICACHE_FLASH_ATTR
spifs_umount(void)
{
    SPIFFS_unmount(&__fs);
}

void ICACHE_FLASH_ATTR
spiffs_format(void)
{
	printf("start spiffs_format...\n");
	spiffs_fs_init();
	
    memset(&__fs, 0, sizeof(__fs));
    memset(erases,0,sizeof(erases));
    memset(_cache,0,sizeof(_cache));

    s32_t res = spifs_specific_mount(FS_FLASH_ADDR, FS_FLASH_SIZE, SECTOR_SIZE, SECTOR_SIZE, LOG_PAGE);
	printf("SPIFFS_mount is done\n");

    if (res == SPIFFS_OK) {
        SPIFFS_unmount(&__fs);
	    printf("SPIFFS_unmount is done\n");
    } else {
        printf("mount failed, %i\n", SPIFFS_errno(&__fs));		
	}
	printf("start SPIFFS_format...\n");
    res = SPIFFS_format(&__fs);
    if (res != SPIFFS_OK) {
        printf("format failed, %i\n", SPIFFS_errno(&__fs));
    }

    res = spifs_specific_mount(FS_FLASH_ADDR, FS_FLASH_SIZE, SECTOR_SIZE, SECTOR_SIZE, LOG_PAGE);
    if (res != SPIFFS_OK) {
         printf("mount failed, %i\n", SPIFFS_errno(&__fs));
    }

	printf("spiffs_format is done\n");
}

void spiffs_fs_info(void)
{
	u32_t total;
	u32_t used;
    u32_t res = SPIFFS_info(FS, &total, &used);
	printf("SPIFFS info: total %d used %d\n", total, used);	
}
