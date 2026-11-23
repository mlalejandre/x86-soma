#ifndef SOMA_SHELL_H
#define SOMA_SHELL_H

#include <stdint.h>

typedef int (*cmd_handler_t)(const char *args, char *out_buf, uint32_t max_out);

#define CMD_F_NORMAL    0
#define CMD_F_ALIAS     (1 << 0)
#define CMD_F_SAFE      (1 << 1)

struct command_entry {
    const char   *name;
    cmd_handler_t handler;
    uint32_t      flags;
    const char   *help;
} __attribute__((aligned(8)));

int  cmd_lang_lint(const char *args, char *out_buf, uint32_t max_out);
/* SOMA_200_MENU_ENERGIA: controles de energia invocables desde la GUI. */
void shell_system_reboot(void);
void shell_system_poweroff(void);
void shell_run(void);
int  shell_python_stop(void);              /* SOMA_237_PYTHON: para la orden python en marcha */
int  shell_python_source(int check, const char *src, uint32_t len, const char *input, char *out, uint32_t cap, uint32_t max_ms);   /* SOMA_239 */
int  shell_js_source(int check, const char *src, uint32_t len, const char *input, char *out, uint32_t cap, uint32_t max_ms);        /* SOMA_274_JS */
int  shell_html_source(int check, const char *src, uint32_t len, const char *input, char *out, uint32_t cap, uint32_t max_ms);     /* SOMA_275_HTML */
int  shell_c_source(int check, const char *src, uint32_t len, const char *input, char *out, uint32_t cap, uint32_t max_ms);        /* SOMA_265_CC */
int  dispatch_command(const char *cmd_line, char *out_buf, uint32_t max_out);
int  dispatch_command_usuario(const char *cmd_line, char *out_buf, uint32_t max_out);   /* SOMA_268_ACCESO */
int  dispatch_agent_command(const char *cmd_line, char *out_buf, uint32_t max_out);
int  shell_autocomplete(const char *prefix, uint32_t plen, const char *matches[], int max_matches);
void show_somafetch(char *out_buf, uint32_t max_out);
void pci_scan(void);

/* PARCHE_TERMINAL_V2: catalogo vivo de comandos para los agentes.
 * Todo lo que se anada a cmd_table queda visible aqui sin tocar el agente. */
int  shell_command_info(const char *name, uint32_t *flags_out, const char **help_out);
int  shell_format_names(char *out, uint32_t max, int only_agent_safe);
int  shell_format_catalog(char *out, uint32_t max, int only_agent_safe, int with_help, int tag_agent);

#endif
