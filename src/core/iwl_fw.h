#ifndef SOMA_IWL_FW_H
#define SOMA_IWL_FW_H
/* SOMA_175A_T480S: lector del firmware de las tarjetas WiFi Intel (formato TLV de iwlwifi, "IWL\n").
 * Logica pura, sin hardware (tests/test_iwl_fw_175.c, con el archivo real iwlwifi-8265-36.ucode si esta en firmware/).
 * Referencia: Linux drivers/net/wireless/intel/iwlwifi/iwl-drv.c y fw/file.h. */
#include <stdint.h>

#define IWL_FW_MAX_SEC   16
#define IWL_SEP_CPU1_CPU2 0xFFFFCCCCu      /* separa las secciones de la CPU1 (LMAC) de las de la CPU2 (UMAC) */
#define IWL_SEP_PAGING    0xAAAABBBBu      /* despues van las de paginacion (se sirven desde la RAM, no se cargan) */
enum { IWL_IMG_RT = 0, IWL_IMG_INIT = 1, IWL_IMG_WOWLAN = 2, IWL_IMG_N = 3 };

struct iwl_fw_sec { uint32_t offset, len; const uint8_t *data; };
struct iwl_fw_img { struct iwl_fw_sec sec[IWL_FW_MAX_SEC]; int n; };
struct iwl_fw_info {
    char human[65];                    /* p. ej. "release/core33::9f0a2d68" */
    uint32_t ver, build;
    struct iwl_fw_img img[IWL_IMG_N];
    uint32_t phy_sku;                  /* PHY_SKU: tipo, paso y "dash" de la radio y antenas */
    uint32_t paging_size;              /* bytes de paginacion de la CPU2 (0 = sin paginacion) */
    uint32_t n_scan_channels, num_cpu;
    uint32_t api[4], capa[4];          /* indicadores de API y capacidades */
    uint32_t calib_flow[3], calib_event[3];   /* SOMA_175B: DEF_CALIB por tipo de imagen (0 RUNTIME, 1 INIT, 2 WOWLAN) */
    uint32_t tlvs;
};
/* 0 = bien; si no, *why explica el problema */
int iwl_fw_parse(const uint8_t *d, uint32_t len, struct iwl_fw_info *o, const char **why);
/* Secciones de una imagen que se cargan en la CPU 1 o 2: indices [*first, *end) */
int iwl_fw_cpu_range(const struct iwl_fw_img *im, int cpu, int *first, int *end);
#endif
