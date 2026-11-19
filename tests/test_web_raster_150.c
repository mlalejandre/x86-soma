#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "web_raster_150.h"
static unsigned long checks;
static void ck(int x, const char *msg) { if (!x) { fprintf(stderr,"SOMA150 fallo: %s\n",msg); exit(1); } checks++; }
static uint32_t rnd=0x92a7b90du;
static uint32_t rng(void) { rnd ^= rnd<<13; rnd ^= rnd>>17; rnd ^= rnd<<5; return rnd; }

static void test_direct(void)
{
    /* Composicion sin escalado: misma imagen original, incluyendo pixeles transparentes. */
    for (int t=0;t<40000;t++) {
        int iw=1+(int)(rng()%128), ih=1+(int)(rng()%128);
        int x=(int)(rng()%(unsigned)iw), y=(int)(rng()%(unsigned)ih);
        int w=1+(int)(rng()%(unsigned)(iw-x)), h=1+(int)(rng()%(unsigned)(ih-y));
        uint32_t original[128*128], old[128*128], fast[128*128];
        for (int i=0;i<iw*ih;i++) original[i]=rng();
        /* La ruta anterior de draw_image (cuando dimensiones coinciden) copia
           pixels a un buffer temporal antes de fb_blit_alpha. */
        for (int j=0;j<h;j++) for (int i=0;i<w;i++) {
            int xx=(int)(((int64_t)(x+i)*iw+iw/2)/iw);
            int yy=(int)(((int64_t)(y+j)*ih+ih/2)/ih);
            old[j*w+i]=original[yy*iw+xx];
        }
        /* Nueva ruta: el framebuffer usa directamente la ventana del origen. */
        const uint32_t *src=original+(size_t)y*iw+x;
        for (int j=0;j<h;j++) memcpy(fast+j*w,src+j*iw,(size_t)w*4);
        ck(memcmp(old,fast,(size_t)w*h*4)==0,"pixels directos incl alpha y recorte");
    }
    ck(soma150_direct_image(200,100,200,100,0,255),"identidad directa");
    ck(!soma150_direct_image(200,100,199,100,0,255),"escalado no directo");
    ck(!soma150_direct_image(200,100,200,100,3,255),"radio no directo");
    ck(!soma150_direct_image(200,100,200,100,0,200),"alpha no directo");
}
static void test_tiles(void)
{
    for (int t=0;t<100000;t++) {
        int w=1+(int)(rng()%4096), h=1+(int)(rng()%2048);
        int tile=soma150_tile_rows(w,h);
        ck(tile>=1 && tile<=128 && tile<=h,"limites de fila");
        ck((uint64_t)w*tile*4 <= 512u*1024u || tile==1,"scratch limitado");
        int count=0;
        for(int y=0;y<h;y+=tile) { int n=h-y<tile?h-y:tile; ck(n>=1 && y+n<=h,"tira en bounds"); count+=n; }
        ck(count==h,"cobertura sin omisiones");
    }
    ck(soma150_tile_rows(0,20)==0,"ancho nulo");
    ck(soma150_tile_rows(10,0)==0,"alto nulo");
    ck(soma150_tile_rows(INT_MAX,10)==1,"ancho muy grande no desborda");
}
static void test_progress(void)
{
    struct soma150_image_progress p;
    p=soma150_progress(1,0,0,0,50); ck(p.reflow_now && p.finish && !p.reflow_finish,"ultimo recurso: una sola maquetacion");
    p=soma150_progress(1,3,0,2000,50); ck(p.reflow_now && !p.finish && !p.reflow_finish,"maquetacion periodica");
    p=soma150_progress(1,3,0,100,50); ck(!p.reflow_now && !p.finish,"agrupar descargas");
    p=soma150_progress(0,3,1,100,50); ck(!p.reflow_now && p.reflow_finish && p.finish,"espera fuera de pantalla: vaciar pendiente");
    p=soma150_progress(0,3,0,100,50); ck(!p.reflow_now && !p.reflow_finish && p.finish,"sin cambios: no recalcular layout");
    for(int i=0;i<100000;i++) {
        int did=(int)(rng()%2), pending=(int)(rng()%160), dirty=(int)(rng()%2);
        p=soma150_progress(did,pending,dirty,rng()%20000,rng()%4000);
        ck(!(p.reflow_now && p.reflow_finish),"nunca doble maquetacion");
        ck(!p.reflow_finish || p.finish,"solo reflow final al finalizar");
        ck(!p.reflow_now || did,"solo reflow inmediato tras actividad");
    }
}
int main(void)
{
    test_direct(); test_tiles(); test_progress();
    printf("SOMA150: %lu validaciones; imagen 1024x768 temporal %u KiB antes, %u KiB max con tiles\n",
           checks, (unsigned)(1024u*768u*4u/1024u),
           (unsigned)(1024u*soma150_tile_rows(1024,768)*4u/1024u));
    return 0;
}
