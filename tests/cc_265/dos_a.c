#include <stdio.h>
int doble(int x);
extern const char *nombre;
int main(void) { printf("%s: %d\n", nombre, doble(21)); return 0; }
