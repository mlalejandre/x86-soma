/* gui_abrir.c: abrir aplicaciones (desde el escritorio, la consola o el agente).
 * SOMA_234_GUI_PARTIDO: NO se compila por separado. Lo incluye src/ui/gui.c en su sitio (una sola unidad de
 * compilacion: comparte el estado del escritorio con gui.c, y el nucleo compilado sale identico). */
/* SOMA_PARCHE_V2_GUI: las herramientas abren ventanas reales del sistema multiventana */
static int gui_win_open(enum app_mode app)
{
    if (!fb_is_active()) return 0;
    her_open_app(app, fb_get_width(), fb_get_height());
    gui_live_paint();
    return 1;
}

static void gui_open_browser__impl(const char *url)
{
    if (!url || !*url) return;
    char local_url[160];
    if (url[0] != '/' && memcmp(url, "file://", 7) != 0 && memcmp(url, "http://", 7) != 0 && memcmp(url, "https://", 8) != 0) {
        local_url[0] = '/';
        uint32_t p = 0;
        while (url[p] && p < sizeof(local_url) - 2) {
            local_url[p + 1] = url[p]; p++;
        }
        local_url[p + 1] = '\0'; url = local_url;
    }
    if (gui_win_open(APP_WEB_BROWSER)) {
        /* browser_init() lanza la demo en segundo plano: esperar antes de cargar la URL */
        for (int i = 0; i < 300 && browser_is_loading(); ++i) thread_sleep(10);
        browser_load_url(url);
        for (int i = 0; i < 30 && browser_is_loading(); ++i) thread_sleep(10);
        gui_live_paint();
        return;
    }
    ensure_window_geom();
    current_app = APP_WEB_BROWSER;
    agent_global_focus = 0;
    browser_init();
    browser_load_url(url);
    for (int i = 0; i < 30 && browser_is_loading(); ++i) thread_sleep(10);
    g_pending_app = APP_WEB_BROWSER;                    /* PARCHE 086: antes se abria el escritorio sin la ventana */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_browser(const char *url)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_browser__impl(url);
    gui_leave(tok);
}

static void gui_open_file_manager__impl(const char *folder)
{
    if (fb_is_active()) {                                    /* SOMA_214_ARCHIVOS: la ventana de esa carpeta, o una nueva */
        her_open_app_path(APP_FILE_MANAGER, folder, fb_get_width(), fb_get_height());
        gui_live_paint();
        return;
    }
    ensure_window_geom();
    current_app = APP_FILE_MANAGER;
    agent_global_focus = 0;
    if (folder && *folder) fm_open_folder(folder); else fm_init();
    g_pending_app = APP_FILE_MANAGER;                   /* PARCHE 086 */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_file_manager(const char *folder)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_file_manager__impl(folder);
    gui_leave(tok);
}

static void gui_open_memview__impl(void)
{
    if (gui_win_open(APP_AGENT_MEMORY)) return;
    ensure_window_geom();
    current_app = APP_AGENT_MEMORY;
    agent_global_focus = 0;
    memview_init();
    g_pending_app = APP_AGENT_MEMORY;                   /* PARCHE 120: sin esto la orden abria el escritorio sin la ventana */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_memview(void)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_memview__impl();
    gui_leave(tok);
}

/* PARCHE 085: Ajustes */
static void gui_open_settings__impl(void)
{
    if (gui_win_open(APP_SETTINGS)) return;
    ensure_window_geom();
    settings_load();
    g_pending_app = APP_SETTINGS;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_settings(void)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_settings__impl();
    gui_leave(tok);
}

/* PARCHE 085: "navegador" desde la consola (con la GUI apagada abre el escritorio con el navegador) */
static void gui_open_web__impl(void)
{
    if (gui_win_open(APP_WEB_BROWSER)) return;
    ensure_window_geom();
    g_pending_app = APP_WEB_BROWSER;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_web(void)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_web__impl();
    gui_leave(tok);
}

/* PARCHE 088: lector de PDF (con la GUI apagada abre el escritorio con la ventana) */
static void gui_open_pdf__impl(const char *path)
{
    if (fb_is_active()) { her_open_app_path(APP_PDF, path, fb_get_width(), fb_get_height()); gui_live_paint(); return; }   /* SOMA_213_PDF */
    ensure_window_geom();
    pdfview_init();
    if (path && *path) pdfview_open_file(path);
    g_pending_app = APP_PDF;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_pdf(const char *path)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_pdf__impl(path);
    gui_leave(tok);
}

/* PARCHE 089: para el Monitor del sistema */
const int gui_app_sysmon = APP_SYSMON;
const char *gui_app_title(int app)
{
    switch ((enum app_mode)app) {
    case APP_AGENT_CHAT: return "SOMA (agente)"; case APP_TERMINAL_SHELL: return "Terminal"; case APP_WEB_BROWSER: return "Navegador";
    case APP_FILE_MANAGER: return "Archivos"; case APP_IMAGE_VIEWER: return "Imagenes"; case APP_NOTEPAD: return "Bloc de notas";
    case APP_AGENT_MEMORY: return "Memoria de agentes"; case APP_LAYOUT: return "Maquetador"; case APP_AGENTS: return "Agentes";
    case APP_PAINT: return "Paint"; case APP_CODER_APP: return "Coder"; case APP_MEDIA: return "Media"; case APP_SETTINGS: return "Ajustes";
    case APP_PDF: return "Lector PDF"; case APP_SYSMON: return "Monitor del sistema"; case APP_RECORDER: return "Grabadora"; case APP_CAMERA: return "Camara"; case APP_CAPTURE: return "Captura"; case APP_SOMA_APP: return "Programa SOMA"; case APP_DESKTOP: return "Escritorio";
    default: return "Aplicacion";
    }
}
int gui_app_list(int *apps, int max) { int n = 0; for (int i = 0; i < her_num_windows && n < max; i++) apps[n++] = (int)her_windows[i].app; return n; }
int gui_close_app(int app)
{
    for (int i = 0; i < her_num_windows; i++) if ((int)her_windows[i].app == app) { her_close_target_window_vortex(i); return 1; }   /* el bucle principal resincroniza y repinta */
    int mi = her_find_minimized((enum app_mode)app);
    if (mi >= 0) { her_kill_minimized(mi); return 1; }
    return 0;
}
static void gui_open_sysmon__impl(void)
{
    if (gui_win_open(APP_SYSMON)) return;
    ensure_window_geom();
    g_pending_app = APP_SYSMON;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_sysmon(void)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_sysmon__impl();
    gui_leave(tok);
}

static void gui_open_recorder__impl(void)
{
    if (gui_win_open(APP_RECORDER)) return;
    ensure_window_geom();
    g_pending_app = APP_RECORDER;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_recorder(void)   /* SOMA_177_T480S: orden "grabadora" */
{
    int tok = gui_enter();
    gui_open_recorder__impl();
    gui_leave(tok);
}
static void gui_open_camera__impl(void)
{
    if (gui_win_open(APP_CAMERA)) return;
    ensure_window_geom();
    g_pending_app = APP_CAMERA;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
static void gui_open_capture__impl(void)
{
    if (gui_win_open(APP_CAPTURE)) return;
    ensure_window_geom();
    g_pending_app = APP_CAPTURE;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_capture(void)   /* SOMA_179_T480S: orden "captura abrir" */
{
    int tok = gui_enter();
    gui_open_capture__impl();
    gui_leave(tok);
}
void gui_open_camera(void)   /* SOMA_178_T480S: orden "camara" */
{
    int tok = gui_enter();
    gui_open_camera__impl();
    gui_leave(tok);
}

static void gui_open_layout__impl(void)
{
    if (gui_win_open(APP_LAYOUT)) return;
    ensure_window_geom();
    layout_init();
    g_pending_app = APP_LAYOUT;                         /* PARCHE 084: abre de verdad la ventana al arrancar la GUI */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_layout(void)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_layout__impl();
    gui_leave(tok);
}

static void gui_open_layout_file__impl(const char *path)
{
    if (gui_win_open(APP_LAYOUT)) { layout_open_file(path); gui_live_paint(); return; }
    ensure_window_geom();
    layout_init();
    layout_open_file(path);
    g_pending_app = APP_LAYOUT;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_layout_file(const char *path)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_layout_file__impl(path);
    gui_leave(tok);
}

void gui_open_coder_app(void);
void coder_app_load_project(const char *path);
/* SOMA_217_STUDIO: la orden 'coder' trae la ventana de Studio mas reciente (o la restaura); si no hay ninguna, abre una */
static int her_raise_app(enum app_mode app)
{
    for (int i = her_num_windows - 1; i >= 0; --i)
        if (her_windows[i].app == app) {
            her_bring_to_front(i); agent_global_focus = 0;
            her_win_x = her_windows[her_num_windows - 1].x; her_win_y = her_windows[her_num_windows - 1].y;
            her_win_w = her_windows[her_num_windows - 1].w; her_win_h = her_windows[her_num_windows - 1].h;
            return 1;
        }
    for (int i = her_min_count - 1; i >= 0; --i) if (her_minimized[i].app == app) { her_restore_minimized(i); return 1; }
    return 0;
}
void gui_open_coder_file(const char *path)   /* SOMA_217_STUDIO: el agente abre un archivo: su ventana o una nueva */
{
    int tok = gui_enter();
    if (fb_is_active()) { her_open_app_path(APP_CODER_APP, path, fb_get_width(), fb_get_height()); gui_live_paint(); }
    else { coder_app_load_project(path); gui_open_coder_app(); }
    gui_leave(tok);
}
static void gui_open_coder_app__impl(void)
{
    if (fb_is_active()) { if (!her_raise_app(APP_CODER_APP)) her_open_app(APP_CODER_APP, fb_get_width(), fb_get_height()); gui_live_paint(); return; }
    ensure_window_geom();
    current_app = APP_CODER_APP;
    agent_global_focus = 0;
    coder_app_init();
    g_pending_app = APP_CODER_APP;                      /* PARCHE 093: abre de verdad la ventana al arrancar la GUI */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_coder_app(void)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_coder_app__impl();
    gui_leave(tok);
}

/* SOMA_211_NOTAS: abre un archivo en el Bloc de notas. Si ya esta abierto en una ventana (aunque este minimizada) se trae esa;
 * si no, ventana nueva. Sin archivo: ventana nueva con un documento vacio. */
static void her_open_app_path(enum app_mode app, const char *path, uint32_t cw, uint32_t ch)
{
    int slot = (path && *path) ? her_inst_find(app, path) : 0;
    if (slot) {
        for (int i = 0; i < her_num_windows; ++i)
            if (her_windows[i].app == app && her_windows[i].term_slot == slot) {
                her_bring_to_front(i); agent_global_focus = 0;
                her_win_x = her_windows[her_num_windows - 1].x; her_win_y = her_windows[her_num_windows - 1].y;
                her_win_w = her_windows[her_num_windows - 1].w; her_win_h = her_windows[her_num_windows - 1].h;
                return;
            }
        for (int i = 0; i < her_min_count; ++i)
            if (her_minimized[i].app == app && her_minimized[i].term_slot == slot) { her_restore_minimized(i); return; }
    }
    int before = her_num_windows;
    her_open_noinit = (path && *path) ? 1 : 0;
    her_open_app(app, cw, ch);
    her_open_noinit = 0;
    if (her_num_windows > before && path && *path) {
        if (app == APP_NOTEPAD) notepad_load_file(path);
        else if (app == APP_IMAGE_VIEWER) imgview_load_file(path);
        else if (app == APP_CODER_APP) coder_app_open_path(path);
        else if (app == APP_PAINT) paint_open_file(path);
        else if (app == APP_FILE_MANAGER) fm_open_folder(path);
        else if (app == APP_PDF) pdfview_open_file(path);
    }
}
static void her_open_notepad_path(const char *path, uint32_t cw, uint32_t ch) { her_open_app_path(APP_NOTEPAD, path, cw, ch); }
static void gui_open_notepad__impl(const char *path)
{
    if (fb_is_active()) {
        her_open_notepad_path(path, fb_get_width(), fb_get_height());
        gui_live_paint();
        return;
    }
    ensure_window_geom();
    current_app = APP_NOTEPAD;
    agent_global_focus = 0;
    notepad_init();
    if (path && *path) notepad_load_file(path);
    g_pending_app = APP_NOTEPAD;                        /* PARCHE 120 */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_notepad(const char *path)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_notepad__impl(path);
    gui_leave(tok);
}

/* SOMA_PARCHE_ARCHIVOS: abre el visor de imagenes con un archivo (lo usa el explorador de archivos) */
static void gui_open_imgview__impl(const char *path)
{
    if (fb_is_active()) {                                    /* SOMA_212_IMAGENES: la ventana de esa imagen, o una nueva */
        her_open_app_path(APP_IMAGE_VIEWER, path, fb_get_width(), fb_get_height());
        gui_live_paint();
        return;
    }
    ensure_window_geom();
    current_app = APP_IMAGE_VIEWER;
    agent_global_focus = 0;
    imgview_init();
    if (path && *path) imgview_load_file(path);
    g_pending_app = APP_IMAGE_VIEWER;                   /* PARCHE 088: abria el escritorio sin la ventana */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_imgview(const char *path)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_imgview__impl(path);
    gui_leave(tok);
}

static void gui_open_paint__impl(const char *path)
{
    if (fb_is_active()) {                                    /* SOMA_215_PAINT: la ventana de ese dibujo, o una nueva */
        her_open_app_path(APP_PAINT, path, fb_get_width(), fb_get_height());
        gui_live_paint();
        return;
    }
    ensure_window_geom();
    paint_init();
    if (path && *path) paint_open_file(path);
    g_pending_app = APP_PAINT;                          /* PARCHE 083: la orden 'paint' abre de verdad la ventana al arrancar la GUI */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_paint(const char *path)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_paint__impl(path);
    gui_leave(tok);
}

static void gui_open_terminal__impl(int debug)
{
    term_set_debug(debug);
    if (gui_win_open(APP_TERMINAL_SHELL)) return;
    ensure_window_geom();
    g_pending_app = APP_TERMINAL_SHELL;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_terminal(int debug)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_terminal__impl(debug);
    gui_leave(tok);
}

static void gui_open_media__impl(const char *path)
{
    if (gui_win_open(APP_MEDIA)) {
        if (path && *path) media_play(path);
        return;
    }
    ensure_window_geom();
    g_pending_app = APP_MEDIA;
    if (path && *path) media_play(path);
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_media(const char *path)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_media__impl(path);
    gui_leave(tok);
}

static void gui_open_agents__impl(void)
{
    if (gui_win_open(APP_AGENTS)) return;
    ensure_window_geom();
    current_app = APP_AGENTS;
    agent_global_focus = 0;
    g_pending_app = APP_AGENTS;                         /* PARCHE 120 */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_agents(void)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_agents__impl();
    gui_leave(tok);
}

/* El estado del raton es compartido con el hilo de fondo */
#define mouse_x g_mouse_x_s
#define mouse_y g_mouse_y_s

