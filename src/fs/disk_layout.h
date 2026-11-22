#ifndef SOMA_DISK_LAYOUT_H
#define SOMA_DISK_LAYOUT_H

/* ====================================================================
 * MAPA FÍSICO DE DISCO SOMA V2 (Sectores LBA de 512 bytes en virtio-blk)
 * Capacidad total estándar: 16 MiB (32.768 sectores)
 * ==================================================================== */

/* Zona 1: Buzón y Puente Host-Bridge (Singularity Loop) */
#define SOMA_LBA_MB_START       1      /* LBA 1..16: Buzón de parches (8 KiB) */
#define SOMA_LBA_MB_SECTORS     16
#define SOMA_LBA_RESULT_START   20     /* LBA 20..23: Resultado del host (2 KiB) */
#define SOMA_LBA_RESULT_SECTORS 4
#define SOMA_LBA_BOOT_GATE      24     /* LBA 24: Flag de prueba canaria */
#define SOMA_LBA_RESUME_FLAG    25     /* LBA 25: Flag de reanudación atómica */
#define SOMA_LBA_CKPT_INTENT    31     /* LBA 31: marca "restauracion de checkpoint en curso" (src/fs/ckptio.h); el arranque la completa */
#define SOMA_LBA_KILLTEST       30     /* LBA 30: marca "SOMA_KILLTEST" -> el arranque ejecuta la prueba de cortes reales (tools/killtest.py) */

/* Zona 2: Sistema de Archivos Persistente V2 (RamFS SOMAFS02) */
#define SOMA_LBA_FS_SUPER       1024   /* LBA 1024: Superbloque del VFS */
#define SOMA_LBA_FS_DATA        1025   /* LBA 1025..3137: 64 inodos x 33 sectores (16 KiB/archivo) */
#define SOMA_LBA_FS_LIMIT       3138
#define SOMA_LBA_FS_JOURNAL     3200   /* LBA 3200..3234: diario del VFS (1 sector de compromiso + cabecera + 32 de datos) */
#define SOMA_LBA_FS_JOURNAL_END 3234

/* Zona 3: Scratch del Sistema y Zona de Usuario */
#define SOMA_LBA_SCRATCH_MIN    3500   /* LBA 3500..3508: Scratch para canary tests */
#define SOMA_LBA_USER_MIN       3600   /* LBA >= 3600: Libre para usuario / sector_write */

/* Zona 4: Checkpoints de Hardware */
#define SOMA_LBA_CHECKPOINT     4096   /* LBA 4096..6210: Checkpoint de respaldo RamFS */
#define SOMA_LBA_CHECKPOINT_END 6210
/* PARCHE 130: segunda ranura del checkpoint (A/B), partida en dos tramos para aprovechar huecos libres: marcador 6211, datos 6212..8191 (1980 sectores)
 * y los 133 restantes en 3962..4094. Total de la ranura: 1 + 2113 sectores (igual que la A). */
#define SOMA_LBA_CKPT_B_MARKER  6211
#define SOMA_LBA_CKPT_B_DATA    6212
#define SOMA_LBA_CKPT_B_SPLIT   1980   /* sectores de datos en el primer tramo */
#define SOMA_LBA_CKPT_B_DATA2   3962   /* LBA 3962..4094 */

/* Zona 5: Fuentes del Sistema (SRCFS) */
#define SOMA_LBA_SRCFS_INDEX    8192   /* LBA 8192..8319: Índice de fuentes (128 sectores = 1023 entradas; SOMA_267_SRCFS) */
#define SOMA_LBA_SRCFS_DATA     8320   /* LBA 8320..16383: Fuentes empaquetadas (~4 MiB) */
#define SOMA_LBA_SRCFS_LIMIT    16384
/* SOMA_PARCHE_018_BIGFS: almacen de archivos grandes (formato SOMABIG1, ver src/fs/bigfs.h): desde aqui hasta
 * el final del disco. ejecutar.py agranda hdd.img (por defecto a 64 MiB) para que haya sitio. */
#define SOMA_LBA_BIGFS_START    16384


/* PARCHE 122 (Fase 1, permisos): zonas del disco que un USUARIO (o un agente, para leer) puede tocar con sector_read/sector_write.
 * Antes la lista de zonas protegidas se escribio sin BIGFS (LBA >= 16384, el almacen de archivos grandes) y sin el checkpoint entero (solo se
 * protegian 4095..4400 de 4095..6210). Ahora SOLO es escribible lo que esta listado aqui como libre; todo lo demas (arranque, buzon, VFS, diario,
 * checkpoint, fuentes, BIGFS) esta protegido. */
static inline int soma_lba_user_zone(unsigned long long sec)
{
    if (sec >= SOMA_LBA_USER_MIN && sec < SOMA_LBA_CKPT_B_DATA2) return 1;                   /* 3600..3961 (PARCHE 130: el resto es la ranura B del checkpoint) */
    return 0;
}
#endif
