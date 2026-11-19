/* PARCHE 121 (Fase 1, permisos): rutas que un programa de ring 3 puede abrir con SYS_OPEN. Funcion pura (probada en el host).
 * Antes sys_open aceptaba cualquier ruta del almacen BIGFS: un ELF podia leer /.ramfs/NNN (el desbordamiento de RamFS: memoria del agente,
 * /etc, estado) o cualquier archivo privado. Ahora la ruta se normaliza (//, /./, /../) con la misma funcion que usan los agentes y se
 * rechaza si cae en una zona privada o si el primer componente es oculto (empieza por '.', p. ej. /.ramfs, /.ckpt). Una ruta que no se
 * puede normalizar (demasiado larga) tambien se rechaza. El resto del almacen (musica, video, /bin, datos del usuario) sigue accesible. */
#ifndef SOMA_USER_PATH_H
#define SOMA_USER_PATH_H
#include "agent_policy.h"

static inline int user_path_allowed(const char *path, unsigned len)
{
    char tmp[AGENT_POLICY_PATH_MAX + 1], c[AGENT_POLICY_PATH_MAX + 1];
    static const char *const private_dirs[] = { "/etc", "/sys", "/boot", "/mem", "/agent", "/tmp", "/proc", "/dev", 0 };
    if (!path || len == 0 || len > AGENT_POLICY_PATH_MAX) return 0;
    for (unsigned i = 0; i < len; ++i) { if (path[i] == 0) return 0; tmp[i] = path[i]; }      /* un NUL dentro de la ruta: truco clasico */
    tmp[len] = 0;
    if (!agent_path_canon(tmp, c, sizeof c)) return 0;
    if (c[1] == '.' ) return 0;                                                              /* /.ramfs, /.ckpt, ... */
    if (c[1] == '\0') return 0;                                                              /* la raiz no es un archivo */
    for (int i = 0; private_dirs[i]; ++i)
        if (agent_path_has_prefix(c, private_dirs[i])) return 0;
    return 1;
}
#endif
