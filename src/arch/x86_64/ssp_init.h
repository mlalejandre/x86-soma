#ifndef SOMA_SSP_INIT_H
#define SOMA_SSP_INIT_H
void ssp_randomize(void);       /* lo llama boot.s antes de kernel_main */
extern int ssp_source;          /* 0 constante, 1 RDRAND, 2 contador de ciclos */
#endif
