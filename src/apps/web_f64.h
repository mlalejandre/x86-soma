#ifndef SOMA_WEB_F64_H
#define SOMA_WEB_F64_H
/* Aritmetica IEEE-754 de doble precision en software (el nucleo se compila sin FPU/SSE). */
#include <stdint.h>
typedef uint64_t f64;

#define F64_ZERO 0ULL
#define F64_ONE  0x3FF0000000000000ULL
#define F64_NAN  0x7FF8000000000000ULL
#define F64_INF  0x7FF0000000000000ULL
#define F64_NINF 0xFFF0000000000000ULL
#define F64_PI   0x400921FB54442D18ULL
#define F64_E    0x4005BF0A8B145769ULL

f64 f64_from_i64(int64_t v);
f64 f64_from_u64(uint64_t v);
static inline f64 f64_from_i32(int32_t v) { return f64_from_i64(v); }
int32_t f64_to_i32(f64 a);       /* ToInt32 de JS (modulo 2^32) */
uint32_t f64_to_u32(f64 a);
int64_t f64_to_i64(f64 a);       /* trunca y satura; NaN -> 0 */
f64 f64_add(f64 a, f64 b);
f64 f64_sub(f64 a, f64 b);
f64 f64_mul(f64 a, f64 b);
f64 f64_div(f64 a, f64 b);
f64 f64_mod(f64 a, f64 b);       /* % de JS */
static inline f64 f64_neg(f64 a) { return a ^ 0x8000000000000000ULL; }
static inline f64 f64_abs(f64 a) { return a & 0x7FFFFFFFFFFFFFFFULL; }
int f64_is_nan(f64 a);
int f64_is_inf(f64 a);
int f64_is_finite(f64 a);
int f64_is_int(f64 a);           /* entero finito */
int f64_eq(f64 a, f64 b);        /* == IEEE (NaN != NaN, 0 == -0) */
int f64_lt(f64 a, f64 b);
int f64_le(f64 a, f64 b);
f64 f64_floor(f64 a);
f64 f64_ceil(f64 a);
f64 f64_trunc(f64 a);
f64 f64_round(f64 a);            /* Math.round */
f64 f64_sqrt(f64 a);
f64 f64_exp(f64 a);
f64 f64_log(f64 a);
f64 f64_pow(f64 a, f64 b);
f64 f64_sin(f64 a);
f64 f64_cos(f64 a);
f64 f64_tan(f64 a);
f64 f64_atan(f64 a);
f64 f64_atan2(f64 y, f64 x);
f64 f64_asin(f64 a);
f64 f64_acos(f64 a);
f64 f64_parse(const char *s, const char **end);        /* decimal con signo y exponente; end = tras lo leido (NULL si nada) */
/* SOMA_200: tamanos de `out`. Estas funciones no reciben el tamano del bufer: quien llama debe reservar F64_STR_MAX bytes. Antes
 * number_proto usaba 400 y (1e308).toString(2) escribe 1025; toExponential y toLocaleString aceptaban cualquier numero de decimales
 * pedido por la pagina: tres desbordamientos de pila del nucleo alcanzables desde JavaScript.
 *   f64_to_str base 10: <= 25 bytes; otra base: <= 1080 (1024 cifras + punto + 52 + signo)
 *   f64_to_fixed / f64_to_precision: <= 130 con los decimales acotados a F64_DIGITS_MAX
 *   f64_to_exponential: <= 780 (sin decimales pedidos escribe todas las cifras exactas de un denormal) */
#define F64_STR_MAX    1200
#define F64_DIGITS_MAX 100
int f64_to_str(f64 a, char *out, int radix);            /* formato JS Number#toString; devuelve longitud */
int f64_to_fixed(f64 a, int digits, char *out);         /* toFixed */
int f64_to_precision(f64 a, int prec, char *out);       /* toPrecision */
int f64_to_exponential(f64 a, int digits, char *out);   /* toExponential (digits<0: los necesarios) */
#endif
