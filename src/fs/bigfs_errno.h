/* PARCHE 124: traduccion de los errores internos de BIGFS (src/fs/bigfs.h) a la tabla unica de src/core/errno.h. Funcion pura (probada en el host).
 * Todo codigo negativo desconocido se convierte en EIO: ring 3 nunca ve un valor que no este en la tabla. */
#ifndef SOMA_BIGFS_ERRNO_H
#define SOMA_BIGFS_ERRNO_H
#include "bigfs.h"
#include "errno.h"

static inline int bigfs_errno(int e)
{
    if (e >= 0) return e;
    switch (e) {
    case BIGFS_E_NOENT:    return SOMA_ENOENT;
    case BIGFS_E_EXIST:    return SOMA_EEXIST;
    case BIGFS_E_NOTDIR:   return SOMA_ENOTDIR;
    case BIGFS_E_ISDIR:    return SOMA_EISDIR;
    case BIGFS_E_NOTEMPTY: return SOMA_ENOTEMPTY;
    case BIGFS_E_NAME:     return SOMA_ENAMETOOLONG;
    case BIGFS_E_NOSPC:    return SOMA_ENOSPC;
    case BIGFS_E_IO:       return SOMA_EIO;
    case BIGFS_E_CORRUPT:  return SOMA_EUCLEAN;
    case BIGFS_E_INVAL:    return SOMA_EINVAL;
    case BIGFS_E_NOINODE:  return SOMA_ENOSPC;
    case BIGFS_E_FBIG:     return SOMA_EFBIG;
    case BIGFS_E_NOTMNT:   return SOMA_ENODEV;
    case BIGFS_E_NOTFMT:   return SOMA_ENODEV;
    default:               return SOMA_EIO;
    }
}
#endif
