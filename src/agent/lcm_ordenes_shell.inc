
/* SOMA_258_LCM: "cerrar" cierra aplicaciones (antes solo se podia con el raton o desde el Monitor).
 *   cerrar            la ventana de delante
 *   cerrar todo       todas las ventanas y las minimizadas
 *   cerrar <app>      p. ej. cerrar navegador | cerrar paint | cerrar bloc de notas | cerrar youtube (sinonimos: internet, consola,
 *                     explorador, fotos, notas, dibujo, editor de codigo, reproductor, configuracion, lector, webcam...)
 * Usa la API publica de la GUI (gui_app_list, gui_app_title, gui_close_app) con turno de pantalla (gui_enter/gui_leave). */
static void cerrar_pliega(const char *in, char *o, uint32_t cap)
{
    uint32_t p = 0;
    for (const unsigned char *s = (const unsigned char *)in; *s && p + 1 < cap; ++s) {
        unsigned char c = *s;
        if (c == 0xC3 && s[1]) {
            unsigned char d = *++s; char r = 0;
            if (d == 0xA1 || d == 0x81) r = 'a'; else if (d == 0xA9 || d == 0x89) r = 'e'; else if (d == 0xAD || d == 0x8D) r = 'i';
            else if (d == 0xB3 || d == 0x93) r = 'o'; else if (d == 0xBA || d == 0x9A || d == 0xBC || d == 0x9C) r = 'u'; else if (d == 0xB1 || d == 0x91) r = 'n';
            if (r) o[p++] = r;
            continue;
        }
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
        o[p++] = (char)c;
    }
    o[p] = 0;
}
static int cerrar_contiene(const char *s, const char *t)
{
    for (; *s; ++s) { const char *a = s, *b = t; while (*a && *b && *a == *b) { ++a; ++b; } if (!*b) return 1; }
    return 0;
}
static const struct { const char *clave, *titulo; } CERRAR_ALIAS[] = {
    { "editor de texto", "Bloc de notas" }, { "bloc", "Bloc de notas" }, { "notas", "Bloc de notas" }, { "notepad", "Bloc de notas" },
    { "editor de codigo", "Coder" }, { "studio", "Coder" }, { "codigo", "Coder" }, { "coder", "Coder" }, { "programador", "Coder" },
    { "memoria", "Memoria de agentes" }, { "agentes", "Agentes" },
    { "navegador", "Navegador" }, { "internet", "Navegador" }, { "browser", "Navegador" }, { "web", "Navegador" }, { "chrome", "Navegador" }, { "firefox", "Navegador" },
    { "terminal", "Terminal" }, { "consola", "Terminal" }, { "shell", "Terminal" },
    { "archivos", "Archivos" }, { "explorador", "Archivos" }, { "carpetas", "Archivos" }, { "ficheros", "Archivos" },
    { "imagen", "Imagenes" }, { "visor", "Imagenes" }, { "foto", "Imagenes" },
    { "maquetador", "Maquetador" }, { "latex", "Maquetador" },
    { "paint", "Paint" }, { "dibujo", "Paint" }, { "pintar", "Paint" },
    { "media", "Media" }, { "reproductor", "Media" }, { "video", "Media" }, { "musica", "Media" }, { "pelicula", "Media" },
    { "ajustes", "Ajustes" }, { "configuracion", "Ajustes" }, { "preferencias", "Ajustes" },
    { "pdf", "Lector PDF" }, { "lector", "Lector PDF" },
    { "monitor", "Monitor del sistema" }, { "rendimiento", "Monitor del sistema" },
    { "grabadora", "Grabadora" }, { "microfono", "Grabadora" },
    { "camara", "Camara" }, { "webcam", "Camara" }, { "captura", "Captura" },
    { "youtube", "YouTube" }, { "chat", "SOMA (agente)" }, { "agente", "SOMA (agente)" }, { "programa", "Programa SOMA" }, { "app", "Programa SOMA" },
};
static int cmd_cerrar(const char *args, char *out_buf, uint32_t max_out)
{
    char a[96]; uint32_t p = 0;
    while (args && *args == ' ') args++;
    cerrar_pliega(args ? args : "", a, sizeof a);
    int tok = gui_enter();
    int apps[32]; int n = gui_app_list(apps, 32);
    int cerradas = 0; const char *ultima = 0;
    const char *titulo = 0;
    for (uint32_t i = 0; i < sizeof CERRAR_ALIAS / sizeof CERRAR_ALIAS[0] && !titulo; ++i) if (cerrar_contiene(a, CERRAR_ALIAS[i].clave)) titulo = CERRAR_ALIAS[i].titulo;
    int todo = !titulo && (cerrar_contiene(a, "todo") || cerrar_contiene(a, "todas"));
    int delante = !titulo && !todo && (!a[0] || cerrar_contiene(a, "ventana") || cerrar_contiene(a, "eso") || cerrar_contiene(a, "esto") || cerrar_contiene(a, "esta"));
    if (todo) {
        for (int i = n - 1; i >= 0; --i) if (gui_close_app(apps[i])) { ++cerradas; ultima = gui_app_title(apps[i]); }
        for (int k = 0; k < 32; ++k) { int m = 0; for (int app = 1; app < 32; ++app) if (gui_close_app(app)) { ++m; ++cerradas; } if (!m) break; }   /* las minimizadas */
    } else if (delante) {
        if (n > 0 && gui_close_app(apps[n - 1])) { cerradas = 1; ultima = gui_app_title(apps[n - 1]); }
    } else {
        if (titulo) {
            for (int app = 1; app < 32; ++app) {
                const char *t = gui_app_title(app);
                int igual = 1; for (int k = 0; t[k] || titulo[k]; ++k) if (t[k] != titulo[k]) { igual = 0; break; }
                if (igual) { if (gui_close_app(app)) { cerradas = 1; ultima = t; } break; }
            }
        }
        if (!titulo) {
            gui_leave(tok);
            soma_str_put(out_buf, max_out, &p, "cerrar: no se que aplicacion es '"); soma_str_put(out_buf, max_out, &p, args);
            soma_str_put(out_buf, max_out, &p, "'. Uso: cerrar | cerrar todo | cerrar <aplicacion> (navegador, terminal, paint, notas, youtube...)");
            kprint(out_buf); kprint("\n");
            return 1;
        }
    }
    gui_leave(tok);
    if (!cerradas) soma_str_put(out_buf, max_out, &p, todo || delante ? "No hay ninguna ventana abierta." : "No hay ninguna ventana de esa aplicacion abierta.");
    else if (todo) { soma_str_put(out_buf, max_out, &p, "Cerradas: "); soma_str_put_dec(out_buf, max_out, &p, (uint64_t)cerradas); }
    else { soma_str_put(out_buf, max_out, &p, "Cerrada: "); soma_str_put(out_buf, max_out, &p, ultima ? ultima : "ventana"); }
    kprint(out_buf); kprint("\n");
    return 1;
}
/* SOMA_258_LCM fin */

/* SOMA_258_LCM: "contiene <texto> [ruta] [-a]": que archivos tienen ese texto DENTRO (find busca por nombre; grep, en un solo archivo).
 * Sin distinguir mayusculas. Recorre la misma lista que find y lee cada archivo (hasta 4 MiB). */
static int contiene_en(const char *b, uint32_t n, const char *t)
{
    uint32_t tl = 0; while (t[tl]) ++tl;
    if (!tl || n < tl) return 0;
    for (uint32_t i = 0; i + tl <= n; ++i) {
        uint32_t k = 0;
        while (k < tl) { char a = b[i + k], c = t[k]; if (a >= 'A' && a <= 'Z') a = (char)(a + 32); if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if (a != c) break; ++k; }
        if (k == tl) return 1;
    }
    return 0;
}
static int cmd_contiene(const char *args, char *out_buf, uint32_t max_out)
{
    char term[64], root[48], tok[64], item[FS_NAME_MAX + 4];
    int show_all = 0; uint32_t hits = 0, vistos = 0, p = 0;
    term[0] = root[0] = '\0';
    if (!args) args = "";
    while (*args) {
        args = shell_next_arg(args, tok, sizeof(tok));
        if (!tok[0]) { if (!*args) break; continue; }
        if (tok[0] == '-' && tok[1] == 'a' && tok[2] == '\0') show_all = 1;
        else if (!term[0]) shell_copy(term, tok, sizeof(term));
        else if (!root[0]) shell_copy(root, tok, sizeof(root));
    }
    if (!term[0]) { shell_emit("Uso: contiene <texto> [ruta] [-a]   (que archivos tienen ese texto dentro)\n", out_buf, max_out); return 1; }
    char *list = (char *)kmalloc(4096), *rep = (char *)kmalloc(4096);
    if (!list || !rep) { if (list) kfree(list); if (rep) kfree(rep); kprint("contiene: sin memoria.\n"); return 1; }
    rep[0] = '\0';
    vfs_format_list_ext(show_all, root[0] ? root : 0, list, 4096);
    const char *q = shell_list_start(list);
    while (*q) {
        q = shell_list_next(q, item, sizeof(item));
        uint32_t il = 0; while (item[il]) ++il;
        if (!il || item[il - 1] == '/') continue;
        uint32_t len = 0; char *b = vfs_read_alloc(item, &len);
        if (!b) continue;
        ++vistos;
        if (len <= (4u << 20) && contiene_en(b, len, term)) { soma_str_put(rep, 4096, &p, item); soma_str_putc(rep, 4096, &p, '\n'); hits++; }
        kfree(b);
    }
    if (!hits) { soma_str_put(rep, 4096, &p, "contiene: ningun archivo tiene '"); soma_str_put(rep, 4096, &p, term); soma_str_put(rep, 4096, &p, "' ("); soma_str_put_dec(rep, 4096, &p, vistos); soma_str_put(rep, 4096, &p, " mirados).\n"); }
    else { soma_str_put_dec(rep, 4096, &p, hits); soma_str_put(rep, 4096, &p, " archivo(s) con '"); soma_str_put(rep, 4096, &p, term); soma_str_put(rep, 4096, &p, "'\n"); }
    shell_emit(rep, out_buf, max_out);
    kfree(list); kfree(rep);
    return 1;
}
/* SOMA_258_LCM fin (contiene) */
