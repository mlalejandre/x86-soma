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
static int strcmp_(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
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

/* SOMA_288_INVESTIGA: ver agent_ruta.h */
int ar_investiga(const char *m)
{
    if (!m || !*m) return 0;
    for (const char *p = m; *p; p++) if (p[0] == '/' && p[1] != '/' && (p == m || p[-1] == ' ')) return 0;    /* una ruta del sistema */
    static const char *const no[] = { "archivo", "archivos", "fichero", "ficheros", "carpeta", "carpetas", "directorio", "disco", "pendrive", "usb",
                                      "youtube", "video", "videos", "cancion", "canciones", "musica", "memoria de soma", "en tu memoria", "recuerdas",
                                      "mis notas", "mi correo", "codigo fuente", "el codigo", "este codigo", "programa", "app", "aplicacion",
                                      "ip", "dns", "ping", "mac", "red local", "wifi", "ram", "memoria libre", "cpu", "bateria", "procesos", 0 };
    if (ar_any(m, no)) return 0;
    static const char *const si[] = { "investiga", "investigar", "investigame", "investigalo", "investigues", "haz una investigacion", "una investigacion sobre",
                                      "busca informacion", "buscame informacion", "busca info", "buscar informacion", "recopila informacion", "informate",
                                      "averigua", "averiguame", "averiguar", "ultimas noticias", "noticias de", "noticias sobre", "ultima hora de",
                                      "que se sabe de", "que se sabe sobre", "documentate", "research", "look up", 0 };
    if (ar_any(m, si)) return 2;
    static const char *const ver[] = { "busca", "buscar", "buscame", "buscalo", "buscala", "mira", "miralo", "consulta", "consultalo", "encuentra", "encuentrame", 0 };
    static const char *const red[] = { "en internet", "por internet", "en la web", "en la red", "en google", "online", "en linea", 0 };
    return ar_any(m, ver) && ar_any(m, red);
}

void ar_consulta(const char *m, char *out, uint32_t cap)
{
    /* por palabras: se comparan normalizadas pero se copian las originales (con sus tildes y enes: "ano" no es "año") */
    static const char *const fuera[] = { "puedes", "podrias", "por favor", "porfa", "investiga", "investigar", "investigame", "investigalo", "haz una investigacion",
        "una investigacion", "busca", "buscar", "buscame", "buscalo", "buscala", "mira", "miralo", "consulta", "consultalo", "encuentra", "encuentrame", "averigua",
        "averiguame", "informate", "documentate", "recopila", "en internet", "por internet", "en la web", "en la red", "en google", "online", "en linea",
        "informacion", "info", "sobre", "acerca de", "que se sabe", 0 };
    enum { NW = 64 };
    const char *wa[NW]; uint32_t wl[NW]; char wn[NW][32]; int nw = 0, quita[NW];
    for (const char *p = m; *p && nw < NW; ) {
        while (*p && (*p == ' ' || *p == ',' || *p == '.' || *p == '?' || *p == '!' || *p == ':' || *p == ';' || *p == '"' || *p == '\'' ||
                      ((unsigned char)p[0] == 0xC2 && ((unsigned char)p[1] == 0xBF || (unsigned char)p[1] == 0xA1)))) p += ((unsigned char)*p == 0xC2) ? 2 : 1;
        if (!*p) break;
        const char *a = p;
        while (*p && *p != ' ' && *p != ',' && *p != '.' && *p != '?' && *p != '!' && *p != ':' && *p != ';' && *p != '"' && !((unsigned char)p[0] == 0xC2 && ((unsigned char)p[1] == 0xBF || (unsigned char)p[1] == 0xA1))) p++;
        char w[64]; uint32_t k = 0; for (const char *c = a; c < p && k + 1 < sizeof w; c++) w[k++] = *c;
        w[k] = 0;
        char n[40]; norm(w, n, sizeof n);
        uint32_t z = 0; const char *nn = n; while (*nn == ' ') nn++;
        while (nn[z] && nn[z] != ' ' && z + 1 < sizeof wn[0]) { wn[nw][z] = nn[z]; z++; }
        wn[nw][z] = 0;
        wa[nw] = a; wl[nw] = (uint32_t)(p - a); quita[nw] = 0; nw++;
    }
    for (int i = 0; fuera[i]; i++) {
        char ph[64]; norm(fuera[i], ph, sizeof ph);
        const char *f[6]; int nf = 0; static char pw[6][32];
        for (const char *c = ph; *c && nf < 6; ) { while (*c == ' ') c++; if (!*c) break; uint32_t z = 0; while (*c && *c != ' ' && z + 1 < sizeof pw[0]) pw[nf][z++] = *c++; pw[nf][z] = 0; f[nf] = pw[nf]; nf++; while (*c && *c != ' ') c++; }
        for (int k = 0; k + nf <= nw; k++) {
            int ok = 1; for (int j = 0; j < nf && ok; j++) { const char *x = wn[k + j], *y = f[j]; while (*x && *x == *y) { x++; y++; } ok = !*x && !*y; }
            if (ok) for (int j = 0; j < nf; j++) quita[k + j] = 1;
        }
    }
    int k0 = 0;
    for (;;) {                                                       /* sin articulos ni "de" sueltos al principio */
        while (k0 < nw && quita[k0]) k0++;
        if (k0 < nw) { const char *x = wn[k0]; if (!strcmp_(x, "de") || !strcmp_(x, "del") || !strcmp_(x, "el") || !strcmp_(x, "la") || !strcmp_(x, "los") || !strcmp_(x, "las") || !strcmp_(x, "que") || !strcmp_(x, "me")) { quita[k0] = 1; continue; } }
        break;
    }
    uint32_t q = 0;
    for (int k = 0; k < nw; k++) {
        if (quita[k]) continue;
        if (q && q + 1 < cap) out[q++] = ' ';
        for (uint32_t z = 0; z < wl[k] && q + 1 < cap; z++) out[q++] = wa[k][z];
    }
    while (q && (out[q - 1] == ' ' || out[q - 1] == '?')) q--;
    if (cap) out[q] = 0;
    if (!q) { while (m[q] && q + 1 < cap) { out[q] = m[q]; q++; } if (cap) out[q] = 0; }
}

/* SOMA_292_PAINT_IA: ver agent_ruta.h */
int ar_dibujo(const char *m)
{
    if (!m || !*m) return 0;
    static const char *const no[] = { "grafico", "graficos", "grafica", "graficas", "diagrama", "diagramas", "esquema", "tabla", "funcion", "curva", "plano",
        "flujo", "ascii", "svg", "codigo", "programa", "html", "latex", "documento", "informe", "presentacion", "archivo", "archivos", "fichero",
        "ficheros", "carpeta", "carpetas", "directorio", "directorios", "arbol de", "memoria", "disco",
        "como se dibuja", "como dibujar", "como se pinta", "como pintar", "como dibujo", "explica", "explicame", "ensename a", "con letras", "con texto", 0 };
    if (ar_any(m, no)) return 0;
    static const char *const verbo[] = { "dibuja", "dibujame", "dibujar", "dibujes", "pinta", "pintame", "pintar", "haz un dibujo", "hazme un dibujo",
        "crea un dibujo", "ilustra", "ilustrame", 0 };
    if (ar_word(m, "paint") && ar_any(m, verbo)) return 1;
    static const char *const solo[] = { "dibujame", "dibuja un", "dibuja una", "dibuja unos", "dibuja unas", "dibuja el", "dibuja la", "dibuja los",
        "dibuja las", "dibuja mi", "haz un dibujo", "hazme un dibujo", "crea un dibujo", "pintame un", "pintame una", "pinta un", "pinta una",
        "ilustrame", "ilustra un", "ilustra una", 0 };
    return ar_any(m, solo);
}

/* SOMA_295_CORREO: ver agent_ruta.h */
int ar_correo(const char *m)
{
    if (!m || !*m) return 0;
    /* "envialo" y parecidos: solo si es una frase corta (confirma el borrador del orbe) */
    { uint32_t pal = 0; int en = 0; for (const char *p = m; *p; p++) { int l = *p != ' ' && *p != ',' && *p != '.' && *p != '!'; if (l && !en) pal++; en = l; }
      static const char *const conf[] = { "envialo", "enviala", "mandalo", "mandala", "envia el correo", "envia el email", "envia el mail", "manda el correo",
                                          "manda el email", "envia el borrador", "manda el borrador", "enviar el correo", "si envialo", "si mandalo", "envialo ya", "ya puedes enviarlo", 0 };
      if (pal <= 6 && ar_any(m, conf)) return AR_CO_ENVIA; }
    static const char *const obj[] = { "correo", "correos", "email", "emails", "e-mail", "e-mails", "mail", "mails", "emilio", "emilios", "bandeja de entrada", 0 };
    if (!ar_any(m, obj)) return 0;
    static const char *const no[] = { "en internet", "por internet", "en la web", "en google", "programa", "programame", "script", "codigo", "html", "funcion",
        "que es un correo", "que es el correo", "como funciona el correo", "como funciona un correo", "explica", "explicame", "protocolo", "smtp", "imap", "pop3",
        "servidor de correo", "correo basura en", "spam en internet", 0 };
    if (ar_any(m, no)) return 0;
    static const char *const abre[] = { "abre el correo", "abre correo", "abre mi correo", "abre la app de correo", "abre la aplicacion de correo", "abre el gestor de correo",
        "abre el cliente de correo", "abre el email", "abre mi email", "abre el mail", "abreme el correo", 0 };
    if (ar_any(m, abre)) return AR_CO_ABRE;
    static const char *const busca[] = { "busca", "buscame", "busques", "encuentra", "encuentrame", "localiza", "localizame", 0 };
    if (ar_any(m, busca)) return AR_CO_BUSCA;
    static const char *const escribe[] = { "escribe", "escribeme", "escribele", "escribirle", "redacta", "redactame", "redactale", "manda", "mandale", "mandar",
        "envia", "enviale", "enviar", "prepara", "preparame", "preparale", "responde", "respondele", "responder", "contesta", "contestale", "contestar", "redactar", 0 };
    if (ar_any(m, escribe)) return AR_CO_REDACTA;
    static const char *const lee[] = { "tengo correo*", "tengo email*", "tengo mail*", "correos nuevos", "correo nuevo", "emails nuevos", "mails nuevos", "mis correos", "mi correo",
        "mis emails", "mis mails", "mi email", "bandeja de entrada", "lee", "leeme", "resume", "resumeme", "resumen", "que correos", "hay correos", "algun correo",
        "revisa", "revisame", "mira", "comprueba", "ha llegado", "han llegado", "he recibido", "me ha escrito", "sin leer", 0 };
    if (ar_any(m, lee)) return AR_CO_LEE;
    return 0;
}
