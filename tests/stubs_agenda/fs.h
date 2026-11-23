/* SOMA_272_AGENDA: imitacion minima de fs.h/mem.h/rtc.h/idt.h para probar agenda.c en el host */
#include <stdint.h>
#include <stddef.h>
int   vfs_read(const char *name, char *buf_out, uint32_t max_len);
int   vfs_write(const char *name, const char *data, uint32_t len);
char *vfs_read_alloc(const char *name, uint32_t *len_out);
