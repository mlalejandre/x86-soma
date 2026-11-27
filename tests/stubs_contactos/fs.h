/* SOMA_357_CONTACTOS: imitacion minima de fs.h para probar contactos.c y agenda.c en el host */
#include <stdint.h>
#include <stddef.h>
int   vfs_read(const char *name, char *buf_out, uint32_t max_len);
int   vfs_write(const char *name, const char *data, uint32_t len);
char *vfs_read_alloc(const char *name, uint32_t *len_out);
int   vfs_size(const char *name);
int   vfs_format_list_ext(int show_all, const char *filter_dir, char *out_buf, uint32_t max);
