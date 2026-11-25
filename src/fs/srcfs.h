#ifndef MYOS_SRCFS_H
#define MYOS_SRCFS_H

#include <stdint.h>
#include "disk_layout.h"

#define SECTOR_USER_MIN SOMA_LBA_USER_MIN

int srcfs_ls_filter(const char *prefix, char *out, uint32_t max);   /* prefix NULL o "" = todo */
int srcfs_cat(const char *name, uint32_t offset, char *out, uint32_t max);
int srcfs_grep(const char *name, const char *pat, char *out, uint32_t max);
int  srcfs_a_ram(void);                       /* SOMA_269_RAM: copia las fuentes a memoria; 0 = bien */
int src_tool(const char *cmd, char *out, uint32_t max);

#endif
