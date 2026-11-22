/* SOMA_260_LLM_TAM: orden "llm-limites" (la incluye src/agent/shell.c).
 *   llm-limites                    muestra los tamanos con los que SOMA habla con el modelo
 *   llm-limites contexto <tokens>  ventana de contexto del modelo (p. ej. 250000); 0 = como antes
 *   llm-limites salida <tokens>    cuanto puede escribir el modelo de una vez (max_tokens); 0 = 8192, como antes
 *   llm-limites como-antes         vuelve a los tamanos de siempre
 * Ojo: 'salida' no puede pasar del maximo del modelo (algunos proveedores rechazan la peticion si se pasa). */
static void lim_kb(char *o, uint32_t cap, uint32_t *p, const char *nombre, uint32_t bytes)
{
    soma_str_put(o, cap, p, "  "); soma_str_put(o, cap, p, nombre); soma_str_put(o, cap, p, ": ");
    if (bytes >= (1u << 20)) { soma_str_put_dec(o, cap, p, bytes >> 20); soma_str_put(o, cap, p, "."); soma_str_put_dec(o, cap, p, ((bytes & 0xFFFFFu) * 10u) >> 20); soma_str_put(o, cap, p, " MB\n"); }
    else { soma_str_put_dec(o, cap, p, (bytes + 1023u) >> 10); soma_str_put(o, cap, p, " KB\n"); }
}
static int lim_es(const char *s, const char *t) { while (*t) if (*s++ != *t++) return 0; return 1; }
static int lim_num(const char *s, uint32_t *v)
{
    while (*s == ' ') ++s;
    uint32_t n = 0; int d = 0;
    while (*s >= '0' && *s <= '9') { if (n > 400000000u) return 0; n = n * 10u + (uint32_t)(*s++ - '0'); d = 1; }
    if (d && (*s == 'k' || *s == 'K')) { n *= 1000u; ++s; }
    while (*s == ' ') ++s;
    if (!d || *s) return 0;
    *v = n; return 1;
}
static int cmd_llm_limites(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t p = 0; if (max_out) out_buf[0] = 0;
    while (*args == ' ') ++args;
    const struct llm_tam *t = llm_tam();
    uint32_t ctx = t->contexto, sal = t->salida_fijada ? t->salida : 0, v;
    int cambia = 0;
    if (!*args) { }
    else if (lim_es(args, "contexto") && lim_num(args + 8, &v)) { ctx = v; cambia = 1; }
    else if (lim_es(args, "salida") && lim_num(args + 6, &v)) { sal = v; cambia = 1; }
    else if (lim_es(args, "como-antes") || lim_es(args, "como antes")) { ctx = 0; sal = 0; cambia = 1; }
    else {
        soma_str_put(out_buf, max_out, &p, "Uso: llm-limites | llm-limites contexto <tokens> | llm-limites salida <tokens> | llm-limites como-antes\n"
                                             "  (p. ej. llm-limites contexto 250000; 0 = como antes)");
        kprint(out_buf); kprint("\n"); return 1;
    }
    if (cambia) {
        if (ctx && ctx < LLM_TAM_CONTEXTO_MIN) ctx = LLM_TAM_CONTEXTO_MIN;
        if (llm_tam_fija(ctx, sal) != 0) soma_str_put(out_buf, max_out, &p, "No se pudo guardar /etc/llm_limites.conf (sigue valiendo hasta reiniciar).\n");
        t = llm_tam();
    }
    soma_str_put(out_buf, max_out, &p, "Tamanos con los que SOMA habla con el modelo");
    soma_str_put(out_buf, max_out, &p, t->contexto ? ":\n" : " (los de siempre; para aprovechar un contexto grande: llm-limites contexto <tokens>):\n");
    soma_str_put(out_buf, max_out, &p, "  contexto del modelo: ");
    if (t->contexto) { soma_str_put_dec(out_buf, max_out, &p, t->contexto); soma_str_put(out_buf, max_out, &p, " tokens\n"); }
    else soma_str_put(out_buf, max_out, &p, "sin indicar\n");
    soma_str_put(out_buf, max_out, &p, "  salida maxima: "); soma_str_put_dec(out_buf, max_out, &p, t->salida);
    soma_str_put(out_buf, max_out, &p, t->salida_fijada ? " tokens\n" : " tokens (la de siempre)\n");
    lim_kb(out_buf, max_out, &p, "prompt del agente", t->prompt);
    lim_kb(out_buf, max_out, &p, "historial de pasos", t->historial);
    lim_kb(out_buf, max_out, &p, "salida de cada orden", t->herramienta);
    soma_str_put(out_buf, max_out, &p, "  de cada orden el modelo ve: "); soma_str_put_dec(out_buf, max_out, &p, t->paso); soma_str_put(out_buf, max_out, &p, " caracteres\n");
    lim_kb(out_buf, max_out, &p, "texto de una respuesta", t->texto);
    lim_kb(out_buf, max_out, &p, "respuesta HTTP", t->http);
    lim_kb(out_buf, max_out, &p, "peticion HTTP", t->peticion);
    lim_kb(out_buf, max_out, &p, "respuesta final (pantalla y voz)", (uint32_t)sizeof agent_last_response);
    kprint(out_buf); kprint("\n");
    return 1;
}
/* SOMA_260_LLM_TAM fin */
