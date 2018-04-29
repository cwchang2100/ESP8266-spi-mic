/*
 * spifs.h
 *
 */

#ifndef __SPIFS_H__
#define __SPIFS_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spiffs.h"
#include "spiffs_nucleus.h"

#define FS &__fs

extern spiffs __fs;

void spiffs_fs_init(void);
void spiffs_format(void);
void spifs_mount(void);
void spifs_umount(void);
void spiffs_fs_info(void);

#endif /* __SPIFS_H__ */
