/* PARCHE 124 (calidad): codigos de error UNICOS del sistema, con los mismos valores que Linux (negativos al devolverse).
 * Antes cada capa tenia los suyos y se solapaban: BIGFS_E_IO (-9) valia lo mismo que E_BADF (-9), BIGFS_E_NOTMNT (-14) que E_FAULT, BIGFS_E_FBIG (-13) que "sin permiso":
 * un programa de usuario no podia distinguir "descriptor invalido" de "error de disco". Ahora la frontera de las syscalls traduce los errores de BIGFS a esta tabla
 * (src/fs/bigfs_errno.h) y ring 3 solo ve estos valores. Cabecera sin dependencias: la usan el kernel y los programas de usuario. */
#ifndef SOMA_ERRNO_H
#define SOMA_ERRNO_H
#define SOMA_ENOENT        (-2)    /* no existe */
#define SOMA_EIO           (-5)    /* error de E/S del dispositivo */
#define SOMA_EBADF         (-9)    /* descriptor no valido */
#define SOMA_EAGAIN        (-11)   /* ahora no: reintenta mas tarde (cuota agotada) */
#define SOMA_ENOMEM        (-12)
#define SOMA_EACCES        (-13)   /* sin permiso */
#define SOMA_EFAULT        (-14)   /* puntero fuera de la memoria del proceso */
#define SOMA_EBUSY         (-16)   /* recurso en uso (audio, pantalla) */
#define SOMA_EEXIST        (-17)
#define SOMA_ENODEV        (-19)   /* sin dispositivo / sin sistema montado */
#define SOMA_ENOTDIR       (-20)
#define SOMA_EISDIR        (-21)
#define SOMA_EINVAL        (-22)   /* argumento no valido */
#define SOMA_EMFILE        (-24)   /* demasiados archivos abiertos */
#define SOMA_EFBIG         (-27)
#define SOMA_ENOSPC        (-28)
#define SOMA_ENAMETOOLONG  (-36)
#define SOMA_ENOSYS        (-38)   /* syscall inexistente */
#define SOMA_ENOTEMPTY     (-39)
#define SOMA_EUCLEAN       (-117)  /* estructura del disco corrupta */

/* 1 si `e` es uno de los codigos anteriores (lo unico que ring 3 debe ver como error). */
static inline int soma_errno_valid(long long e)
{
    switch (e) {
    case SOMA_ENOENT: case SOMA_EIO: case SOMA_EBADF: case SOMA_EAGAIN: case SOMA_ENOMEM: case SOMA_EACCES: case SOMA_EFAULT: case SOMA_EBUSY:
    case SOMA_EEXIST: case SOMA_ENODEV: case SOMA_ENOTDIR: case SOMA_EISDIR: case SOMA_EINVAL: case SOMA_EMFILE: case SOMA_EFBIG: case SOMA_ENOSPC:
    case SOMA_ENAMETOOLONG: case SOMA_ENOSYS: case SOMA_ENOTEMPTY: case SOMA_EUCLEAN: return 1;
    default: return 0;
    }
}
#endif
