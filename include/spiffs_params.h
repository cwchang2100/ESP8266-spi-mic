/*
 * spiffs_params.h
 *
 */

#ifndef __SPIFFS_PARAMS_H__
#define __SPIFFS_PARAMS_H__

#define FS_FLASH_SIZE      (1*1024*1024)
#define FS_FLASH_ADDR      (1*1024*1024)

#define SECTOR_SIZE         (4*1024) 
#define LOG_BLOCK           (SECTOR_SIZE)
#define LOG_PAGE            (256)

#define FD_BUF_SIZE         32*4
#define CACHE_BUF_SIZE      (LOG_PAGE + 32)*8

#endif /* __SPIFFS_PARAMS_H__ */
