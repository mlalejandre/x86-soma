#ifndef _SOMA_SYS_STAT_H
#define _SOMA_SYS_STAT_H
#include <sys/types.h>
struct stat { long st_size; mode_t st_mode; time_t st_mtime; };
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_ISREG(m) (((m) & 0170000) == S_IFREG)
#define S_ISDIR(m) (((m) & 0170000) == S_IFDIR)
int stat(const char *p, struct stat *s);
#endif
