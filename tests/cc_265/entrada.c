#include <stdio.h>
#include <string.h>
int main(void) {
    char nombre[64]; int a, b; long suma = 0; int x;
    printf("nombre? "); if (!fgets(nombre, sizeof nombre, stdin)) return 1;
    nombre[strcspn(nombre, "\n")] = 0;
    printf("hola, %s\n", nombre);
    if (scanf("%d %d", &a, &b) == 2) printf("a+b=%d\n", a + b);
    while (scanf("%d", &x) == 1) suma += x;
    printf("suma=%ld\n", suma);
    printf("fin de la entrada: %d\n", feof(stdin) != 0);
    return 0;
}
