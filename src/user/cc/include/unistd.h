#ifndef _SOMA_UNISTD_H
#define _SOMA_UNISTD_H
#include <stddef.h>
#include <sys/types.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
ssize_t read(int fd, void *b, size_t n); ssize_t write(int fd, const void *b, size_t n); int close(int fd);
off_t lseek(int fd, off_t o, int w); int unlink(const char *p); char *getcwd(char *b, size_t n); int isatty(int fd);
unsigned sleep(unsigned s); int usleep(unsigned us); int access(const char *p, int m);
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
#endif
