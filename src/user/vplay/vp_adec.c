/* SOMA_252_VIDEO_NATIVO: decodificador de audio AAC del reproductor nativo (libxaac, third_party/libxaac, Apache 2.0).
 * Va en su propio archivo porque los tipos basicos de libxaac y de libavc (WORD64...) chocan entre si. Ver vp_vdec.c. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "vp_abi.h"

static void *vp_aalloc(void *ctx, int align, int size)
{
    (void)ctx;
    if (align < 16) align = 16;
    if (size <= 0) return 0;
    uint8_t *raw = (uint8_t *)malloc((size_t)size + (size_t)align + sizeof(void *));
    if (!raw) return 0;
    uintptr_t a = ((uintptr_t)raw + sizeof(void *) + (uintptr_t)align - 1) & ~((uintptr_t)align - 1);
    ((void **)a)[-1] = raw;
    return (void *)a;
}
static void vp_afree(void *ctx, void *p) { (void)ctx; if (p) free(((void **)p)[-1]); }

#include "ixheaac_type_def.h"
#include "ixheaac_error_standards.h"
#include "ixheaacd_apicmd_standards.h"
#include "ixheaacd_memory_standards.h"
#include "ixheaacd_aac_config.h"

IA_ERRORCODE ixheaacd_dec_api(pVOID p_ia_module_obj, WORD32 i_cmd, WORD32 i_idx, pVOID pv_value);

static void *ad, *ad_mem[8]; static int ad_nmem;
static uint8_t *ad_in, *ad_outb; static uint32_t ad_in_cap;
static int ad_ready; static uint32_t ad_rate, ad_ch;

void ad_close(void)
{
    for (int i = 0; i < ad_nmem; ++i) vp_afree(0, ad_mem[i]);
    ad_nmem = 0; ad = 0; ad_in = ad_outb = 0; ad_ready = 0;
}
static void *ad_take(uint32_t size, uint32_t align)
{
    if (ad_nmem >= 8) return 0;
    void *p = vp_aalloc(0, (int)(align < 16 ? 16 : align), (int)(size ? size : 16));
    if (p) { memset(p, 0, size); ad_mem[ad_nmem++] = p; }
    return p;
}

static int ad_init_done(void)
{
    UWORD32 done = 0;
    if (ixheaacd_dec_api(ad, IA_API_CMD_INIT, IA_CMD_TYPE_INIT_DONE_QUERY, &done) != IA_NO_ERROR || !done) return 0;
    WORD32 sf = 0, ch = 0, wd = 0;
    (void)ixheaacd_dec_api(ad, IA_API_CMD_GET_CONFIG_PARAM, IA_ENHAACPLUS_DEC_CONFIG_PARAM_SAMP_FREQ, &sf);
    (void)ixheaacd_dec_api(ad, IA_API_CMD_GET_CONFIG_PARAM, IA_ENHAACPLUS_DEC_CONFIG_PARAM_NUM_CHANNELS, &ch);
    (void)ixheaacd_dec_api(ad, IA_API_CMD_GET_CONFIG_PARAM, IA_ENHAACPLUS_DEC_CONFIG_PARAM_PCM_WDSZ, &wd);
    if (sf < 8000 || sf > 96000 || ch < 1 || ch > 8 || wd != 16) return 0;
    ad_rate = (uint32_t)sf; ad_ch = (uint32_t)ch; ad_ready = 1;
    return 1;
}
/* Formato de la SALIDA; 0 hasta que el decodificador ha visto lo que necesita (la configuracion o el primer cuadro). */
int ad_format(uint32_t *rate, uint32_t *channels) { *rate = ad_rate; *channels = ad_ch; return ad_ready; }

/* asc = AudioSpecificConfig del MP4. 1 = abierto (frecuencia y canales de la SALIDA, o 0 si aun no se saben: ad_format). */
int ad_open(const uint8_t *asc, uint32_t asc_n, uint32_t *rate, uint32_t *channels)
{
    ad_close();
    UWORD32 sz = 0;
    if (ixheaacd_dec_api(0, IA_API_CMD_GET_API_SIZE, 0, &sz) != IA_NO_ERROR || !sz) return 0;
    ad = ad_take(sz, 16);
    if (!ad) return 0;
    if (ixheaacd_dec_api(ad, IA_API_CMD_INIT, IA_CMD_TYPE_INIT_API_PRE_CONFIG_PARAMS, 0) != IA_NO_ERROR) goto bad;
    { UWORD32 mp4 = 1; if (ixheaacd_dec_api(ad, IA_API_CMD_SET_CONFIG_PARAM, IA_ENHAACPLUS_DEC_CONFIG_PARAM_ISMP4, &mp4) != IA_NO_ERROR) goto bad; }
    { UWORD32 st = 1; (void)ixheaacd_dec_api(ad, IA_API_CMD_SET_CONFIG_PARAM, IA_ENHAACPLUS_DEC_CONFIG_PARAM_TOSTEREO, &st); }      /* mono -> dos canales iguales */
    if (ixheaacd_dec_api(ad, IA_API_CMD_GET_MEMTABS_SIZE, 0, &sz) != IA_NO_ERROR) goto bad;
    { void *mt = ad_take(sz, 16); if (!mt || ixheaacd_dec_api(ad, IA_API_CMD_SET_MEMTABS_PTR, 0, mt) != IA_NO_ERROR) goto bad; }
    if (ixheaacd_dec_api(ad, IA_API_CMD_INIT, IA_CMD_TYPE_INIT_API_POST_CONFIG_PARAMS, 0) != IA_NO_ERROR) goto bad;
    for (int i = 0; i < 4; ++i) {
        int size = 0, align = 0, type = 0;
        if (ixheaacd_dec_api(ad, IA_API_CMD_GET_MEM_INFO_SIZE, i, &size) != IA_NO_ERROR) goto bad;
        if (ixheaacd_dec_api(ad, IA_API_CMD_GET_MEM_INFO_ALIGNMENT, i, &align) != IA_NO_ERROR) goto bad;
        if (ixheaacd_dec_api(ad, IA_API_CMD_GET_MEM_INFO_TYPE, i, &type) != IA_NO_ERROR) goto bad;
        void *p = ad_take((uint32_t)size, (uint32_t)align);
        if (!p || ixheaacd_dec_api(ad, IA_API_CMD_SET_MEM_PTR, i, p) != IA_NO_ERROR) goto bad;
        if (type == IA_MEMTYPE_INPUT) { ad_in = (uint8_t *)p; ad_in_cap = (uint32_t)size; }
        if (type == IA_MEMTYPE_OUTPUT) ad_outb = (uint8_t *)p;
    }
    if (!ad_in || !ad_outb || asc_n > ad_in_cap) goto bad;
    memcpy(ad_in, asc, asc_n);
    { WORD32 n = (WORD32)asc_n; if (ixheaacd_dec_api(ad, IA_API_CMD_SET_INPUT_BYTES, 0, &n) != IA_NO_ERROR) goto bad; }
    if (ixheaacd_dec_api(ad, IA_API_CMD_INIT, IA_CMD_TYPE_INIT_PROCESS, 0) != IA_NO_ERROR) goto bad;
    ad_rate = ad_ch = 0;
    (void)ad_init_done();                                        /* con algunos flujos hace falta ademas el primer cuadro: ver ad_decode */
    *rate = ad_rate; *channels = ad_ch;
    return 1;
bad:
    ad_close();
    return 0;
}

/* Un cuadro AAC -> PCM de 16 bits entrelazado (en un bufer interno). Devuelve los BYTES de salida (0 = nada o error). */
uint32_t ad_decode(const uint8_t *frame, uint32_t n, const int16_t **pcm)
{
    if (!ad || !n || n > ad_in_cap) return 0;
    memcpy(ad_in, frame, n);
    WORD32 in = (WORD32)n, out = 0;
    if (ixheaacd_dec_api(ad, IA_API_CMD_SET_INPUT_BYTES, 0, &in) != IA_NO_ERROR) return 0;
    if (!ad_ready) {                                             /* la inicializacion termina con el primer cuadro */
        if (ixheaacd_dec_api(ad, IA_API_CMD_INIT, IA_CMD_TYPE_INIT_PROCESS, 0) & IA_FATAL_ERROR) return 0;
        if (!ad_init_done()) return 0;
        memcpy(ad_in, frame, n);
        if (ixheaacd_dec_api(ad, IA_API_CMD_SET_INPUT_BYTES, 0, &in) != IA_NO_ERROR) return 0;
    }
    IA_ERRORCODE e = ixheaacd_dec_api(ad, IA_API_CMD_EXECUTE, IA_CMD_TYPE_DO_EXECUTE, 0);
    if (e & IA_FATAL_ERROR) return 0;
    if (ixheaacd_dec_api(ad, IA_API_CMD_GET_OUTPUT_BYTES, 0, &out) != IA_NO_ERROR || out <= 0) return 0;
    *pcm = (const int16_t *)ad_outb;
    return (uint32_t)out;
}
