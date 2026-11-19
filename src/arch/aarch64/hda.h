#ifndef MYOS_HDA_H
#define MYOS_HDA_H
#include <stdint.h>
#include "audio.h"
/* SOMA_PARCHE_020_AUDIO: sin audio en AArch64 todavia */
int  hda_init(void);
const struct audio_hw *hda_hw(void);
int  hda_ready(void);
int  hda_set_volume(int percent);
int  hda_format_for_rate(uint32_t rate, uint16_t *fmt);
void hda_info(char *out, uint32_t max);
/* SOMA_177_T480S: sin microfono en AArch64 */
int  hda_mic_ready(void);
int  hda_mic_count(void);
int  hda_mic_selected(void);
int  hda_mic_select(int idx);
int  hda_mic_set_gain(int pct);
int  hda_mic_get_gain(void);
void hda_mic_describe(int idx, char *out, uint32_t max);
int  hda_cap_start(uint32_t rate);
void hda_cap_stop(void);
uint32_t hda_cap_pos(void);
const uint8_t *hda_cap_ring(uint32_t *bytes);
int  hda_cap_running(void);
int  hda_raw(uint8_t nid, uint32_t verb, uint32_t payload, uint32_t *resp);
void hda_dump(void);
#endif
