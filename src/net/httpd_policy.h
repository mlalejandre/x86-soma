/* PARCHE 123 (Fase 1, red): que rutas del VFS sirve el servidor HTTP. Funcion pura (probada en el host).
 * Antes era una lista NEGRA (todo salvo /etc /mem /tmp /sys /agent...): cualquier carpeta nueva (informes, /share, datos del usuario) quedaba
 * expuesta a toda la red por defecto. Ahora es una lista BLANCA: solo `/www/...` y `/public/...` (la carpeta que el usuario decide publicar)
 * y tres rutas de informacion que enlaza el panel (/proc/version, /proc/meminfo, /dev/rtc). La ruta no puede llevar `..`, `//`, `/.`, `%`,
 * `\`, caracteres de control ni pasar de 120 bytes. */
#ifndef SOMA_HTTPD_POLICY_H
#define SOMA_HTTPD_POLICY_H

#define HTTPD_PATH_MAX 120

static inline int httpd_pol_streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static inline int httpd_pol_prefix(const char *s, const char *p) { while (*p) { if (*s != *p) return 0; s++; p++; } return 1; }

static inline int httpd_path_allowed(const char *path)
{
    unsigned n = 0;
    if (!path || path[0] != '/') return 0;
    for (const char *q = path; *q; q++, n++) {
        if (n >= HTTPD_PATH_MAX) return 0;
        if ((unsigned char)*q < 32 || (unsigned char)*q == 127) return 0;
        if (q[0] == '.' && q[1] == '.') return 0;
        if (q[0] == '/' && q[1] == '/') return 0;
        if (q[0] == '/' && q[1] == '.') return 0;                    /* /. cubre /./ , /.oculto y /.. */
        if (q[0] == '%' || q[0] == '\\') return 0;
    }
    if (httpd_pol_streq(path, "/proc/version") || httpd_pol_streq(path, "/proc/meminfo") || httpd_pol_streq(path, "/dev/rtc")) return 1;
    if (httpd_pol_prefix(path, "/www/") && path[5]) return 1;
    if (httpd_pol_prefix(path, "/public/") && path[8]) return 1;
    return 0;
}

/* SOMA_197: el servidor solo atiende a equipos de la misma red local (misma subred que la interfaz). Sin mascara conocida no atiende
 * a nadie de fuera. Un cliente que llega a traves de un encaminador (Internet, otra VLAN) recibe un cierre sin respuesta. */
static inline int httpd_peer_allowed(const unsigned char peer[4], const unsigned char ip[4], const unsigned char mask[4])
{
    if (peer[0] == 127) return 1;
    if (mask[0] != 255) return 0;                                 /* SOMA_340_RED_HOSTIL: sin mascara o mas ancha que /8: no es una red local */
    if (!(ip[0] | ip[1] | ip[2] | ip[3])) return 0;
    for (int i = 0; i < 4; i++) if ((peer[i] & mask[i]) != (ip[i] & mask[i])) return 0;
    return 1;
}
/* SOMA_197: /etc/httpd.conf ("servidor=activo" | "servidor=apagado"). Devuelve 1, 0 o `def` si el archivo no lo dice. */
static inline int httpd_conf_mode(const char *text, int def)
{
    for (const char *p = text; p && *p; ) {
        if (httpd_pol_prefix(p, "servidor=")) { p += 9; if (httpd_pol_prefix(p, "activo")) return 1; if (httpd_pol_prefix(p, "apagado")) return 0; }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return def;
}
#endif
