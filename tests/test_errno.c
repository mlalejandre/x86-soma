/* PARCHE 124: tabla unica de errores y traduccion de BIGFS. */
#include <stdio.h>
#include "../src/fs/bigfs_errno.h"
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)
int main(void)
{
    /* todos los codigos conocidos de BIGFS -> un valor valido de la tabla */
    for (int e = -1; e >= -60; --e) { int t = bigfs_errno(e); CHECK(t < 0 && soma_errno_valid(t), "bigfs_errno(%d) = %d no esta en la tabla", e, t); }
    CHECK(bigfs_errno(0) == 0 && bigfs_errno(7) == 7, "los valores no negativos pasan igual");
    /* significados concretos */
    CHECK(bigfs_errno(BIGFS_E_NOENT) == SOMA_ENOENT, "NOENT"); CHECK(bigfs_errno(BIGFS_E_ISDIR) == SOMA_EISDIR, "ISDIR"); CHECK(bigfs_errno(BIGFS_E_IO) == SOMA_EIO, "IO");
    CHECK(bigfs_errno(BIGFS_E_NOTMNT) == SOMA_ENODEV, "NOTMNT"); CHECK(bigfs_errno(BIGFS_E_CORRUPT) == SOMA_EUCLEAN, "CORRUPT");
    /* la colision de antes: un error de disco ya NO se confunde con "descriptor invalido" ni con "puntero invalido" ni con "sin permiso" */
    CHECK(bigfs_errno(BIGFS_E_IO) != SOMA_EBADF, "IO colisiona con EBADF"); CHECK(bigfs_errno(BIGFS_E_NOTMNT) != SOMA_EFAULT, "NOTMNT colisiona con EFAULT"); CHECK(bigfs_errno(BIGFS_E_FBIG) != SOMA_EACCES, "FBIG colisiona con EACCES");
    /* la tabla: valores distintos, todos negativos, validos */
    static const int tab[] = { SOMA_ENOENT, SOMA_EIO, SOMA_EBADF, SOMA_EAGAIN, SOMA_ENOMEM, SOMA_EACCES, SOMA_EFAULT, SOMA_EBUSY, SOMA_EEXIST, SOMA_ENODEV, SOMA_ENOTDIR, SOMA_EISDIR,
                               SOMA_EINVAL, SOMA_EMFILE, SOMA_EFBIG, SOMA_ENOSPC, SOMA_ENAMETOOLONG, SOMA_ENOSYS, SOMA_ENOTEMPTY, SOMA_EUCLEAN };
    for (unsigned i = 0; i < sizeof tab / sizeof *tab; ++i) { CHECK(tab[i] < 0 && soma_errno_valid(tab[i]), "tabla[%u]", i); for (unsigned j = i + 1; j < sizeof tab / sizeof *tab; ++j) CHECK(tab[i] != tab[j], "valores repetidos %d", tab[i]); }
    /* valores que NO son errores del sistema */
    CHECK(!soma_errno_valid(-1) && !soma_errno_valid(0) && !soma_errno_valid(1) && !soma_errno_valid(-3) && !soma_errno_valid(-3000) && !soma_errno_valid(-134), "valores ajenos aceptados");
    /* compatibilidad con los valores que ya documentaban los programas de usuario (usermode.h / ulib.h) */
    CHECK(SOMA_EFAULT == -14 && SOMA_EBADF == -9 && SOMA_EINVAL == -22 && SOMA_EMFILE == -24 && SOMA_EBUSY == -16 && SOMA_ENODEV == -19 && SOMA_ENOSYS == -38 && SOMA_ENOENT == -2, "valores documentados cambiados");
    printf("%s: %d comprobaciones, %d fallos\n", fails ? "FALLO" : "OK", checks, fails);
    return fails != 0;
}
