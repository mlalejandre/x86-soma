# Lenovo ThinkPad T480s — hardware real de esta unidad

Datos leídos en el propio portátil con el parche 156 (`hw smbios`, `hw cpu`, `hw mem`, `hw pci`, `hw acpi`, `dmesg`) el
15/11/2026. Es la referencia para todos los parches de `docs/PLAN_T480S.md`.

## Equipo
| Campo | Valor |
|---|---|
| Modelo | LENOVO ThinkPad T480s, tipo de máquina **20L8S2PY00** (placa 20L8S2PY00) |
| BIOS | N22ET53W (1.30), 02/19/2019 — SMBIOS 3.0, 63 estructuras, encontrado por la tabla de configuración EFI |
| Arranque | UEFI (GOP), Secure Boot desactivado, CSM activo |

## CPU
| Campo | Valor |
|---|---|
| Procesador | Intel Core i5-8350U @ 1,70 GHz (Kaby Lake-R; familia 6, modelo 0x8E, revisión 10) |
| Núcleos / hilos | **4 / 8** (SOMA usa 1) |
| Frecuencia | base 1900 MHz, turbo 3600 MHz; TSC 1896 MHz, **invariante** |
| Extensiones | SSE2, SSE4.2, AVX, **AVX2**, FMA, AES-NI, RDRAND, RDSEED, XSAVE, **ERMS**, **x2APIC**, TSC-deadline, **HWP**, Turbo, páginas de 1 GiB, VMX, SMAP, SMEP. Sin AVX-512, sin FSRM, sin SHA |
| Cachés | L1d 32 KiB, L1i 32 KiB, L2 256 KiB por núcleo, L3 6 MiB compartida |

## Memoria
| Campo | Valor |
|---|---|
| Instalada | **16 GiB DDR4-2400**: ChannelA-DIMM0 8 GiB [M471A1K43BB1-CRC] + ChannelB-DIMM0 8 GiB [RMSA3260ME78HAF-2666, funciona a 2400] |
| Máximo de la placa | 32 GiB |
| Según el firmware | 15,7 GiB utilizables en 24 regiones; RAM hasta **0x4_6180_0000 (17,5 GiB de direcciones)** |
| Reservada / ACPI | 284 MiB reservados (incluye dispositivos), ACPI recuperable 404 KiB, ACPI NVS 396 KiB |
| Mayor bloque libre | **0x1_0000_0000 + 13,5 GiB** (por encima de 4 GiB) |
| Usada por SOMA | toda (15,7 GiB) desde el parche 157: reserva baja < 512 MiB (identidad, DMA) + reserva alta (montón, hasta 7,8 GiB) |

## ACPI
| Campo | Valor |
|---|---|
| RSDP | revisión 2, OEM LENOVO |
| Tablas | FACP, APIC (MADT), HPET, MCFG, **ECDT**, DMAR, LPIT, WSMT, BOOT, BATB, SLIC, MSDM, ASF!, DBGP, DBG2, FPDT, 2×UEFI y 14 SSDT |
| MADT | 8 procesadores, LAPIC 0xFEE00000, IOAPIC 0xFEC00000 |
| Redirecciones | IRQ 0 → GSI 2, IRQ 9 → GSI 9 (SCI) |
| Otros | HPET presente; PCIe ECAM en 0xF8000000 (buses 0–63); DMAR = VT-d (IOMMU) disponible |

## PCI
| Dirección | Id | Dispositivo |
|---|---|---|
| 0:0.0 | 8086:5914 | puente de la CPU (Kaby Lake-R) |
| 0:2.0 | 8086:5917 | gráfica **UHD 620** |
| 0:4.0 | 8086:1903 | control térmico de la CPU |
| 0:8.0 | 8086:1911 | GNA |
| 0:20.0 | 8086:9D2F | **xHCI** USB 3.0 |
| 0:20.2 | 8086:9D31 | control térmico del chipset |
| 0:21.0 | 8086:9D60 | I2C (Serial IO) |
| 0:22.0 | 8086:9D3A | Management Engine |
| 0:28.0/.4/.6, 0:29.0 | 8086:9D10/9D14/9D16/9D18 | puertos raíz PCIe |
| 0:31.0 | 8086:9D4E | LPC (EC, teclado, TPM) |
| 0:31.2 | 8086:9D21 | PMC |
| 0:31.3 | 8086:9D71 | **audio HDA** |
| 0:31.4 | 8086:9D23 | SMBus (trackpad RMI4) |
| 0:31.6 | 8086:15D7 | **Ethernet I219-LM** |
| 4:0.0, 5:0–2.0 | 8086:15C0 | Thunderbolt 3 (Alpine Ridge, puentes) |
| 6:0.0 | 8086:15BF | Thunderbolt 3 (NHI) |
| 60:0.0 | 8086:15C1 | Thunderbolt 3 (USB) |
| 61:0.0 | 8086:24FD | **WiFi Wireless 8265** |
| 62:0.0 | 144D:A808 | **SSD NVMe Samsung PM981** |

## USB (dispositivos internos y el pendrive)
| Puerto | Id | Dispositivo |
|---|---|---|
| 8 | 04F2:B604 | cámara (Chicony, clase vídeo) |
| 9 | 06CB:009A | lector de huellas Synaptics (específico del fabricante) |
| 10 | 04F3:2398 | dispositivo HID de Elan |
| 13 | 21C4:0809 | pendrive Lexar 118 850 MiB (almacenamiento masivo, **ya detectado por SOMA**) |
| 15 | 0BDA:0316 | lector de tarjetas Realtek (almacenamiento masivo) |

## Entrada
Teclado PS/2 por i8042 (AB 41, juego 2 traducido) y trackpad Synaptics + TrackPoint en modo relativo (parche 155).

## Consecuencias para el plan
- **157 (RAM):** el PMM debe cubrir hasta 0x4_6180_0000: unos 4,6 millones de marcos de 4 KiB, con un mapa de bits de
  ~575 KiB. El mapa de identidad puede usar páginas de 1 GiB (18 entradas). El mayor bloque libre está por encima de
  4 GiB, así que se necesita `pmm_alloc_frame_low()` para DMA de 32 bits.
- **158/159 (APIC, núcleos):** MADT con 8 hilos, x2APIC y TSC-deadline disponibles; IRQ 0 → GSI 2.
- **161 (SIMD):** AVX2 + ERMS (sin FSRM): `rep movsb` para copias grandes y AVX2 para mezclas de píxeles.
- **162 (USB):** el almacenamiento masivo USB ya enumera el pendrive Lexar: falta montarlo como disco persistente.
- **163 (NVMe):** Samsung PM981 (144D:A808).
- **164–166 (ACPI, energía):** hay ECDT (EC accesible antes del espacio de nombres) y HWP para la frecuencia.
- **BIOS 1.30 (2019):** Lenovo publicó versiones posteriores con correcciones de seguridad. No es necesario para SOMA, pero es recomendable actualizarla desde Windows.
