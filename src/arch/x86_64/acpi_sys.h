#ifndef SOMA_ACPI_SYS_H
#define SOMA_ACPI_SYS_H
/* SOMA_164_T480S: interprete ACPI (uACPI) y lo que SOMA usa de el. Ver acpi_sys.c. */
#include <stdint.h>
struct acpi_sys_info {
    int active, ec_present, ec_from_ecdt;
    const char *why, *status;
    uint16_t ec_cmd, ec_data;
    uint32_t sci_irq, init_ms, warnings, errors;
    int nbat, has_ac, has_lid, pb_fixed;               /* SOMA_165 */
    uint64_t sci_polls, sci_handled, ec_ops, ec_timeouts;
    int ec_node, hkey, video_bcl, dos_ok;               /* SOMA_168: nodo del EC, HKEY del ThinkPad, salida de video con _BCL */
    uint32_t hkey_ver, hkey_mask, ec_queries, lid_actions;
};
int acpi_sys_init(void);                          /* 0 = interprete en marcha */
const struct acpi_sys_info *acpi_sys_info(void);
int acpi_poweroff(void);                          /* solo vuelve si fallo (<0) */
int acpi_reboot(void);
int acpi_for_each_device(void (*cb)(const char *path, const char *hid, uint32_t sta, void *u), void *u);
/* SOMA_165_T480S: energia. Valores 0xFFFFFFFF = desconocido. Capacidades en mWh (o mAh si unit_ma). */
struct acpi_battery {
    int present, have_state, discharging, charging, critical, unit_ma;
    uint32_t design, full, remaining, rate, voltage, design_mv, cycles, percent, minutes, health;
    char model[24], oem[16];
};
int acpi_battery_read(int i, struct acpi_battery *b);   /* 1 = leida, 0 = no colocada, -1 = no existe */
int acpi_ac_online(void);                         /* 1 enchufado, 0 no, -1 no se sabe */
int acpi_lid_open(void);                          /* 1 abierta, 0 cerrada, -1 no se sabe */
void acpi_set_power_button_action(void (*fn)(void));
void acpi_event_counts(uint32_t *power_button, uint32_t *lid, uint32_t *battery);
/* Resumen guardado (lo renueva el hilo del ACPI cada 10 s): para la interfaz, que no debe esperar al EC */
struct acpi_power_state { int valid, ac, lid, nbat, charging, discharging; uint32_t percent, minutes; };
void acpi_power_state(struct acpi_power_state *o);
void acpi_power_refresh_now(void);
/* SOMA_168_T480S: proyecta MMIO (identidad, sin cache) para otros controladores (brillo). NULL si no se puede. */
void *acpi_sys_map_mmio(uint64_t phys, uint64_t len);
#endif
