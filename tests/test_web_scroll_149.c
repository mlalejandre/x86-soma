/* SOMA 149: desplazamiento de raster sobre un viewport sintetico.
 * La prueba reconstruye exactamente el resultado de repintar todo el viewport. */
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "web_scroll_149.h"
static int checks;
static void ck(int cond, const char *msg) {
    if (!cond) { fprintf(stderr,"FALLO SOMA149: %s\n",msg); _Exit(1); } checks++;
}
static void scroll_case(int old, int now, int h) {
    struct soma149_scroll_plan p={0}; int got=soma149_make_scroll_plan(h,old,now,&p);
    if (h<2 || old==now || (int64_t)now-old >= h || (int64_t)now-old <= -((int64_t)h)) {
        ck(!got,"desplazamiento grande o nulo tiene fallback"); return;
    }
    ck(got,"plan valido");
    ck(p.copy_rows>0 && p.damage_rows>0 && p.copy_rows+p.damage_rows==h,"cobertura completa");
    ck(p.src_row>=0 && p.dst_row>=0 && p.copy_rows+p.src_row<=h && p.copy_rows+p.dst_row<=h,"copia dentro del viewport");
    ck(p.damage_row>=0 && p.damage_row+p.damage_rows<=h,"franja dentro del viewport");
    int oldpic[64], out[64], full[64]; ck(h<=64,"viewport acotado en test");
    for (int y=0;y<h;y++) { oldpic[y]=old+y; out[y]=-99999; full[y]=now+y; }
    for (int y=0;y<p.copy_rows;y++) out[p.dst_row+y]=oldpic[p.src_row+y];
    for (int y=0;y<p.damage_rows;y++) out[p.damage_row+y]=now+p.damage_row+y;
    for (int y=0;y<h;y++) ck(out[y]==full[y],"raster incremental == raster completo");
}
int main(void) {
    scroll_case(100,105,32); scroll_case(100,95,32);
    scroll_case(100,131,32);scroll_case(100,69,32);
    scroll_case(0,0,32);scroll_case(100,132,32);scroll_case(100,68,32);
    scroll_case(0,1,2);scroll_case(1,0,2);
    scroll_case(INT_MIN,INT_MAX,40);scroll_case(INT_MAX,INT_MIN,40);
    for(int old=0;old<64;old++) for(int d=-47;d<=47;d++) scroll_case(old,old+d,48);
    ck(!soma149_make_scroll_plan(32,1,2,0),"puntero nulo rechazado");
    fprintf(stderr,"SOMA 149: %d comprobaciones de raster incremental OK\n",checks);
    return 0;
}
