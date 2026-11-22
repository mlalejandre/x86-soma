#include <stdio.h>
#include <soma.h>
int main(void) {
    if (!soma_win_create("Prueba", 320, 200)) { printf("sin escritorio: la ventana no se abre\n"); return 0; }
    for (int f = 0; f < 3 && soma_win_active(); ++f) {
        soma_win_clear(0x202040); soma_win_rect(10, 10, 100, 50, 0xff0000); soma_win_line(0, 0, 319, 199, 0x00ff00);
        soma_win_circle(160, 100, 30, 0xffff00); soma_win_text(20, 150, "hola", 0xffffff); soma_win_present(); soma_sleep(16);
    }
    soma_win_close();
    printf("ventana dibujada\n");
    return 0;
}
