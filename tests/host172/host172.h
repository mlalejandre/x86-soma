/* SOMA_172_T480S: cabeceras de sustitucion para compilar hda.c en el anfitrion (tests/test_hda_172.c) */
#ifndef HOST172_H
#define HOST172_H
#include <stdint.h>
#include <stddef.h>
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value);
uint16_t pci_vendor_id(uint8_t bus, uint8_t slot, uint8_t function);
uintptr_t pmm_alloc_frame(void);
uintptr_t pmm_alloc_contig(size_t nframes);
#define VMM_FLAG_PRESENT 1ULL
#define VMM_FLAG_WRITABLE 2ULL
#define VMM_FLAG_NX (1ULL << 63)
int vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
void kprint(const char *s);
void kprint_dec(uint32_t v);
void kprint_hex16(uint16_t v);
void kprint_hex32(uint32_t v);
uint64_t timer_get_uptime_ms(void);
static inline void arch_pause(void) { __asm__ volatile ("pause"); }
struct tcb;
typedef void (*thread_func_t)(void *);
struct tcb *thread_create(const char *name, thread_func_t entry, void *arg);
void thread_sleep(uint32_t ms);
int bootinfo_disabled(const char *feature);
extern uint64_t tsc_ticks_per_ms;
#endif
