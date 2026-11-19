/* SOMA_PARCHE_020_AUDIO: marcador de posicion para AArch64 */
#include "hda.h"
int  hda_init(void) { return -1; }
const struct audio_hw *hda_hw(void) { return 0; }
int  hda_ready(void) { return 0; }
int  hda_set_volume(int percent) { (void)percent; return -1; }
int  hda_format_for_rate(uint32_t rate, uint16_t *fmt) { (void)rate; (void)fmt; return -1; }
void hda_info(char *out, uint32_t max) { const char *s = "audio: no disponible en AArch64"; uint32_t i = 0; while (s[i] && i + 1 < max) { out[i] = s[i]; i++; } if (max) out[i] = 0; }
/* SOMA_177_T480S: sin microfono en AArch64 */
int  hda_mic_ready(void) { return 0; }
int  hda_mic_count(void) { return 0; }
int  hda_mic_selected(void) { return -1; }
int  hda_mic_select(int idx) { (void)idx; return -1; }
int  hda_mic_set_gain(int pct) { (void)pct; return -1; }
int  hda_mic_get_gain(void) { return 0; }
void hda_mic_describe(int idx, char *out, uint32_t max) { (void)idx; if (max) out[0] = 0; }
int  hda_cap_start(uint32_t rate) { (void)rate; return -1; }
void hda_cap_stop(void) {}
uint32_t hda_cap_pos(void) { return 0; }
const uint8_t *hda_cap_ring(uint32_t *bytes) { if (bytes) *bytes = 0; return 0; }
int  hda_cap_running(void) { return 0; }
int  hda_raw(uint8_t nid, uint32_t verb, uint32_t payload, uint32_t *resp) { (void)nid; (void)verb; (void)payload; (void)resp; return -1; }
void hda_dump(void) {}
