#ifndef _SOMA_STDIO_H
#define _SOMA_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct _soma_file FILE;
extern FILE *stdin, *stdout, *stderr;
#define EOF (-1)
#define BUFSIZ 1024
#define FILENAME_MAX 256
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
typedef long fpos_t;
FILE *fopen(const char *p, const char *m); FILE *fdopen(int fd, const char *m); FILE *freopen(const char *p, const char *m, FILE *f);
int fclose(FILE *f); int fflush(FILE *f); size_t fread(void *b, size_t s, size_t n, FILE *f); size_t fwrite(const void *b, size_t s, size_t n, FILE *f);
int fgetc(FILE *f); int getc(FILE *f); int getchar(void); int ungetc(int c, FILE *f); char *fgets(char *s, int n, FILE *f);
int fputc(int c, FILE *f); int putc(int c, FILE *f); int putchar(int c); int fputs(const char *s, FILE *f); int puts(const char *s);
int fseek(FILE *f, long o, int w); long ftell(FILE *f); void rewind(FILE *f); int feof(FILE *f); int ferror(FILE *f); void clearerr(FILE *f);
int fileno(FILE *f); int setvbuf(FILE *f, char *b, int m, size_t s); void setbuf(FILE *f, char *b);
int printf(const char *f, ...); int fprintf(FILE *o, const char *f, ...); int sprintf(char *s, const char *f, ...);
int snprintf(char *s, size_t n, const char *f, ...); int vprintf(const char *f, va_list a); int vfprintf(FILE *o, const char *f, va_list a);
int vsprintf(char *s, const char *f, va_list a); int vsnprintf(char *s, size_t n, const char *f, va_list a);
int scanf(const char *f, ...); int fscanf(FILE *i, const char *f, ...); int sscanf(const char *s, const char *f, ...);
int vsscanf(const char *s, const char *f, va_list a); int vfscanf(FILE *i, const char *f, va_list a);
void perror(const char *s); int remove(const char *p); int rename(const char *a, const char *b);
#endif
