#include <stdio.h>
int main(void) { int *p = (int *)0; printf("antes\n"); fflush(stdout); *p = 1; printf("despues\n"); return 0; }
