/* SOMA_251_RUTAS: ver agent_ruta.h */
#include "agent_ruta.h"

#define AR_MAX 1024

/* minusculas, sin tildes, todo lo que no es letra o numero pasa a espacio; con espacios al principio y al final */
static uint32_t norm_k(const char *in, char *out, uint32_t cap, int keep_star)
{
    uint32_t n = 0;
    if (cap < 3) return 0;
    out[n++] = ' ';
    for (const unsigned char *p = (const unsigned char *)in; *p && n + 2 < cap; p++) {
        unsigned char c = *p; char r;
        if (c == 0xC3 && p[1]) {
            unsigned char d = *++p;
            switch (d) { case 0xA1: case 0x81: case 0xA0: case 0xA4: r = 'a'; break; case 0xA9: case 0x89: case 0xA8: case 0xAB: r = 'e'; break;
                         case 0xAD: case 0x8D: case 0xAC: case 0xAF: r = 'i'; break; case 0xB3: case 0x93: case 0xB2: case 0xB6: r = 'o'; break;
                         case 0xBA: case 0x9A: case 0xBC: case 0x9C: case 0xB9: r = 'u'; break; case 0xB1: case 0x91: r = 'n'; break; case 0xA7: case 0x87: r = 'c'; break;
                         default: r = ' '; }
        } else if (c >= 0x80) { r = ' '; while ((p[1] & 0xC0) == 0x80) p++; }
        else if (c >= 'A' && c <= 'Z') r = (char)(c + 32);
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (keep_star && c == '*')) r = (char)c;
        else r = ' ';
        if (r == ' ' && out[n - 1] == ' ') continue;
        out[n++] = r;
    }
    if (out[n - 1] != ' ') out[n++] = ' ';
    out[n] = 0;
    return n;
}
static uint32_t norm(const char *in, char *out, uint32_t cap) { return norm_k(in, out, cap, 0); }
static int match_at(const char *t, const char *ph)
{
    /* t apunta a un inicio de palabra; ph ya normalizada, sin espacios a los lados, palabras con '*' opcional */
    while (*ph) {
        if (*ph == '*') { while (*t && *t != ' ') t++; ph++; continue; }
        if (*ph == ' ') { if (*t != ' ') return 0; while (*ph == ' ') ph++; t++; continue; }
        if (*t != *ph) return 0;
        t++; ph++;
    }
    return *t == ' ';
}
int ar_word(const char *text, const char *phrase)
{
    if (!text || !phrase || !*phrase) return 0;
    char t[AR_MAX], ph[128];
    norm(text, t, sizeof t);
    norm_k(phrase, ph, sizeof ph, 1);                    /* la frase igual, conservando los '*' */
    uint32_t k = 0; while (ph[k]) k++;
    while (k && ph[k - 1] == ' ') k--;
    ph[k] = 0;
    const char *phs = ph; while (*phs == ' ') phs++;
    if (!*phs) return 0;
    for (uint32_t i = 0; t[i]; i++) if (t[i] == ' ' && t[i + 1] && match_at(t + i + 1, phs)) return 1;
    return 0;
}
int ar_any(const char *text, const char *const *ph) { for (int i = 0; ph[i]; i++) if (ar_word(text, ph[i])) return 1; return 0; }

static int is_ext(const char *s, uint32_t n)
{
    static const char *const ext[] = { ".txt", ".md", ".html", ".htm", ".soma", ".py", ".csv", ".json", ".tex", ".log", ".sh", ".c", ".h", 0 };
    for (int i = 0; ext[i]; i++) {
        uint32_t el = 0; while (ext[i][el]) el++;
        if (n > el) { uint32_t k = 0; while (k < el && (s[n - el + k] | 32) == ext[i][k]) k++; if (k == el) return 1; }
    }
    return 0;
}
int ar_wants_save(const char *m, char *path, uint32_t cap)
{
    if (cap) path[0] = 0;
    if (!m) return 0;
    /* una ruta o un nombre de archivo en la mision */
    for (const char *p = m; *p; ) {
        while (*p == ' ' || *p == '\'' || *p == '"' || *p == '(' || *p == ',') p++;
        const char *a = p; while (*p && *p != ' ' && *p != '\'' && *p != '"' && *p != ',' && *p != ')') p++;
        uint32_t n = (uint32_t)(p - a);
        while (n && (a[n - 1] == '.' || a[n - 1] == ':' || a[n - 1] == ';')) n--;
        int slash = 0; for (uint32_t i = 0; i < n; i++) if (a[i] == '/') slash = 1;
        if (n > 1 && ((a[0] == '/' && slash && n > 2) || is_ext(a, n))) {
            if (n > 4 && a[0] == 'h' && a[1] == 't' && a[2] == 't' && a[3] == 'p') continue;    /* un enlace no es un archivo */
            uint32_t o = 0;
            if (a[0] != '/') { const char *w = "/workspace/"; while (*w && o + 1 < cap) path[o++] = *w++; }
            for (uint32_t i = 0; i < n && o + 1 < cap; i++) path[o++] = a[i];
            if (cap) path[o] = 0;
            return 1;
        }
    }
    static const char *const save[] = { "guarda", "guardalo", "guardala", "guardalos", "guardar", "guardarlo", "guardarla", "guardame", "salva", "salvalo",
                                        "en un archivo", "en el archivo", "a un archivo", "en un fichero", "en el fichero", "en un documento de texto",
                                        "crea un archivo", "crea el archivo", "escribe un archivo", "escribelo en", "graba", "grabalo", 0 };
    return ar_any(m, save);
}

int ar_mayordomo(const char *m)
{
    static const char *const action[] = { "muestra", "mostrar", "muestrame", "lista", "listar", "listame", "busca", "buscar", "encuentra", "localiza", "crea", "crear",
                                          "lee", "leer", "elimina", "borra", "borrar", "copia", "copiar", "mueve", "renombra", "consulta", "dime", "indica",
                                          "diagnostica", "comprueba", "revisa", "cuanta", "cuanto", "cuantos", "cuantas", "show", "list", "find", "create", "read", "inspect", "how much", 0 };
    static const char *const object[] = { "ram", "memoria ram", "memoria libre", "memoria del sistema", "uso de memoria", "heap", "disco", "discos", "archivo", "archivos",
                                          "fichero", "ficheros", "carpeta", "carpetas", "directorio", "directorios", "almacenamiento", "espacio libre", "espacio en disco",
                                          "inodo", "inodos", "vfs", "ramfs", "hardware", "estado del sistema", "uptime", "disk", "file", "files", "folder", "directory", "memory", 0 };
    static const char *const writing[] = { "redacta", "redactar", "redaccion", "informe", "escribe sobre", "texto", "contenido", "sobre", "write about", "nota sobre",
                                           "poema", "cuento", "historia", "resumen", "explica", "explicame", 0 };
    int has_path = 0; for (const char *p = m; *p; p++) if (p[0] == '/' && ((p[1] | 32) == 'w' || (p[1] | 32) == 'm')) { has_path = 1; break; }
    int a = ar_any(m, action), o = ar_any(m, object) || has_path;
    if (ar_any(m, writing) && !ar_word(m, "ram") && !ar_word(m, "disco")) return 0;
    return a && o;
}

int ar_direct_ok(const char *m)
{
    if (!m || !*m) return 0;
    for (const char *p = m; *p; p++) if (*p == '/' || (p[0] == '.' && (p[1] == 's' || p[1] == 't') && (p[2] == 'o' || p[2] == 'x'))) return 0;   /* rutas, .soma, .txt */
    static const char *const tools[] = {
        /* tiempo real e internet */
        "busca", "buscar", "buscame", "investiga", "investigar", "noticias", "noticia", "ahora mismo", "actual", "actuales", "actualmente", "ultima hora",
        "ultimas", "ultimos", "precio", "precios", "cotizacion", "bolsa", "clima", "que tiempo hace", "tiempo que hace", "el tiempo en", "web", "internet",
        "pagina", "url", "enlace", "descarga", "descargar", "navegador", "resultado del partido", "quien gano",
        /* sistema y archivos */
        "archivo", "archivos", "fichero", "ficheros", "carpeta", "carpetas", "directorio", "disco", "ram", "memoria del sistema", "cpu", "procesador", "bateria",
        "wifi", "direccion ip", "ping", "ejecuta", "ejecutar", "abre", "abrir", "lanza", "instala", "comando", "terminal", "procesos", "estado del sistema",
        "informacion del sistema", "ajustes", "volumen", "brillo", "captura de pantalla", "graba", "grabar", "camara", "microfono",
        /* agentes y programas */
        "delega", "agente", "agentes", "mayordomo", "buscador", "compila", "codigo soma", "memoria de soma", "recuerda", "recuerdas", 0 };
    return !ar_any(m, tools);
}
