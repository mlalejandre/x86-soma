#ifndef SOMA_IWL_H
#define SOMA_IWL_H
/* SOMA_175A_T480S: WiFi Intel 8265. Ver iwl.c. */
#include <stdint.h>
struct iwl_info {
    int ok, alive, rfkill, fail_section;
    const char *stage, *why;
    uint16_t dev_id; uint32_t subsys, hw_rev; uint64_t bar;
    uint32_t fw_size, bytes_loaded, sections_loaded, load_ms, total_ms;
    uint32_t alive_status, alive_len, lmac_major, lmac_minor, umac_major, umac_minor, err_table, scd_base, err_valid;
    int calibrated; uint8_t mac[6]; uint32_t phy_db, cmds_sent, cmd_timeouts, last_timeout_cmd, scd_sram;   /* SOMA_175B: fase B1 */
    uint32_t fw_err_valid, fw_err_id, fw_err_pc, fw_err_d1;
    uint32_t rerr_type, rerr_n; uint8_t rerr_cmd; uint16_t rerr_seq;   /* SOMA_175B1B: REPLY_ERROR */
    int up, init_alive, rt_alive, scan_result; uint32_t phy_db_sent, sta_status, mcc_status, mcc_chans; char mcc[3];   /* SOMA_175B2 */
    uint32_t sb1, sb2, wfpm, rx_cfg, rx_base, rx_wptr, rx_status, stts0, stts1, csr_reset, int_seen;   /* SOMA_175B: diagnostico */
    uint32_t csr_int, fh_int, gp_cntrl, load_status, drv_gp1, rb_closed, rx_packets, last_cmd, err_access;
};
/* SOMA_175B2: red encontrada. SOMA_175C: sec es SEC_* de wpa.h (wpa_sec_name); datos para asociarse. */
#define IWL_MAX_BSS 64
struct iwl_bss { uint8_t bssid[6]; char ssid[33]; uint8_t chan; int8_t rssi; uint8_t sec, group, nrates, nxrates; uint16_t capab, bi; uint8_t rates[8], xrates[8]; };
/* SOMA_175C: estado de la conexion para Ajustes y el terminal */
struct iwl_wifi_status {
    int present, ready, enabled, state, scanning, connecting, nbss, rssi, rate_mbps;
    const char *state_name, *why;
    char ssid[33]; uint8_t bssid[6], chan; uint32_t tx, rx;
};
enum { IWL_W_IDLE = 0, IWL_W_JOIN, IWL_W_AUTH, IWL_W_ASSOC, IWL_W_KEYS, IWL_W_UP, IWL_W_FAIL };
void iwl_wifi_status(struct iwl_wifi_status *o);
int  iwl_wifi_request_scan(void);                                          /* en segundo plano; 0 = pedida */
int  iwl_wifi_request_connect(const char *ssid, const char *pass, int save);   /* pass puede ser NULL (red guardada o abierta) */
void iwl_wifi_request_disconnect(void);
int  iwl_wifi_is_saved(const char *ssid);
int  iwl_wifi_forget(const char *ssid);
int  iwl_wifi_saved_count(void);
const char *iwl_wifi_saved_name(int i);
void iwl_wifi_enable(int on);
struct netif_ops;
extern const struct netif_ops netif_iwl;                                   /* wlan0 (registrada en virtio_net.c) */
int iwl_scan(void);                          /* busqueda pasiva (bloquea unos segundos); 0 = bien */
const struct iwl_bss *iwl_bss_list(int *n);
void iwl_start(void);                       /* arranque en segundo plano (hilo "wifi") */
void iwl_command(const char *args);         /* orden wifi */
#endif
