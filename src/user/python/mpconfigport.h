/* SOMA: configuracion de MicroPython para los programas de ring 3 de SOMA (src/user/python.c). */
#include <port/mpconfigport_common.h>

#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)
#define MICROPY_ENABLE_COMPILER                 (1)
#define MICROPY_ENABLE_GC                       (1)
#define MICROPY_PY_GC                           (1)
#define MICROPY_FLOAT_IMPL                      (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_LONGINT_IMPL                    (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_PY_SYS                          (1)
#define MICROPY_PY_SYS_EXIT                     (1)
#define MICROPY_PY_SYS_STDFILES                 (0)
#define MICROPY_PY_IO                           (1)   // io.StringIO / BytesIO (json.dumps las usa)
#define MICROPY_PY_OS                           (0)
#define MICROPY_PY_TIME                         (0)
#define MICROPY_PY_SELECT                       (0)
#define MICROPY_PY_THREAD                       (0)
#define MICROPY_PY_MICROPYTHON_STACK_USE        (0)
#define MICROPY_ENABLE_EXTERNAL_IMPORT          (1)
#define MICROPY_HELPER_REPL                     (1)
#define MICROPY_REPL_AUTO_INDENT                (0)
#define MICROPY_KBD_EXCEPTION                   (0)

#define MICROPY_PY_BUILTINS_HELP                (0)
#define MICROPY_USE_INTERNAL_PRINTF             (0)
#define MICROPY_READER_VFS                      (0)
#define MICROPY_ERROR_REPORTING                 (MICROPY_ERROR_REPORTING_DETAILED)
#define MICROPY_ENABLE_SOURCE_LINE              (1)
#define MICROPY_STACK_CHECK                     (1)
#define MICROPY_ALLOC_PATH_MAX                  (128)
#define MICROPY_NLR_SETJMP                      (0)
#define MICROPY_PY_SYS_PLATFORM                 "soma"
#define MICROPY_PY_UCTYPES                      (0)
#define MICROPY_PY_BUILTINS_INPUT               (0)
#define MICROPY_FLOAT_USE_NATIVE_FLT16          (0)
#define MICROPY_HW_BOARD_NAME                   "SOMA"
#define MICROPY_HW_MCU_NAME                     "x86_64 (ring 3)"
// input() propio (lee una linea de la consola de SOMA por una llamada al sistema); exit() y quit() como en CPython
extern const struct _mp_obj_fun_builtin_var_t soma_input_obj, soma_exit_obj;
#define MICROPY_PORT_BUILTINS { MP_ROM_QSTR(MP_QSTR_input), MP_ROM_PTR(&soma_input_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_exit), MP_ROM_PTR(&soma_exit_obj) }, { MP_ROM_QSTR(MP_QSTR_quit), MP_ROM_PTR(&soma_exit_obj) },
#define MICROPY_USE_INTERNAL_ERRNO              (1)
// Modulos de extmod que se compilan (third_party/micropython/extmod): random, json, re, heapq, binascii (sin crc32)
extern unsigned int soma_random_seed(void);
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC        (soma_random_seed())
#define MICROPY_MODULE_BUILTIN_INIT             (1)
#define MICROPY_PY_BINASCII_CRC32               (0)
#define MICROPY_PY_HASHLIB                      (0)
#define MICROPY_PY_DEFLATE                      (0)
#define MICROPY_PY_PLATFORM                     (0)
#define MICROPY_PY_ERRNO                        (1)
