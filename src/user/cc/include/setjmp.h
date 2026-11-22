#ifndef _SOMA_SETJMP_H
#define _SOMA_SETJMP_H
typedef long jmp_buf[8];
int setjmp(jmp_buf env); void longjmp(jmp_buf env, int val) __attribute__((noreturn));
#define _setjmp setjmp
#define _longjmp longjmp
#endif
