#ifndef _SOMA_ASSERT_H
#define _SOMA_ASSERT_H
void __assert_fail(const char *expr, const char *file, unsigned int line, const char *func);
#endif
#undef assert
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#define assert(e) ((e) ? (void)0 : __assert_fail(#e, __FILE__, __LINE__, __func__))
#endif
