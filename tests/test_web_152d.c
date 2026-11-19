/* SOMA 152D: lista de bloqueo del usuario (/etc/bloqueo_web.txt): troceo y veredicto. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "web_fluido_152.h"
int main(void)
{
    char buf[] = "# comentario\r\n  sensic.net  \nmrf.io\n\n!googletagmanager.com\n!\n\t hb.20m.es\t\r\n#otro";
    const char *e[16];
    int n = soma152_blocklist_parse(buf, e, 16);
    assert(n == 4);
    assert(!strcmp(e[0], "sensic.net") && !strcmp(e[1], "mrf.io") && !strcmp(e[2], "!googletagmanager.com") && !strcmp(e[3], "hb.20m.es"));
    assert(soma152_blocklist_verdict("https://es-config.sensic.net/s2s-web.js", e, n) == 1);
    assert(soma152_blocklist_verdict("https://hb.20m.es/stable/x.js", e, n) == 1);
    assert(soma152_blocklist_verdict("https://www.googletagmanager.com/gtm.js", e, n) == 0);   /* permitido por el usuario */
    assert(soma152_blocklist_verdict("https://www.20minutos.es/assets/global.bundle.js", e, n) == -1);
    const char *one[1]; char b2[] = "a.com"; assert(soma152_blocklist_parse(b2, one, 1) == 1);
    char b3[] = "x\ny\nz"; const char *two[2]; assert(soma152_blocklist_parse(b3, two, 2) == 2);   /* respeta el maximo */
    char b4[] = ""; assert(soma152_blocklist_parse(b4, e, 16) == 0);
    assert(soma152_blocklist_verdict("https://a.com/", e, 0) == -1);
    puts("SOMA 152D: lista de bloqueo del usuario OK");
    return 0;
}
