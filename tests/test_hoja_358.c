/* SOMA_358_HOJA: pruebas del nucleo de la hoja de calculo (hoja_core.c) en el anfitrion. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hoja_core.h"

void *hj_alloc(uint32_t n) { return malloc(n ? n : 1); }
void hj_free(void *p) { free(p); }
static int fallos = 0, n = 0;
#define CK(c, m) do { ++n; if (!(c)) { ++fallos; printf("FALLO %d: %s\n", __LINE__, m); } } while (0)
static struct hj_libro L;
static char b[4096];
static const char *ver(int h, const char *dir) { uint32_t f, c; hj_lee_dir(dir, &f, &c); hj_texto_ver(&L, h, f, c, b, sizeof b); return b; }
static const char *edi(int h, const char *dir) { uint32_t f, c; hj_lee_dir(dir, &f, &c); hj_texto_edicion(&L, h, f, c, b, sizeof b); return b; }
static int esc(int h, const char *dir, const char *t) { uint32_t f, c; hj_lee_dir(dir, &f, &c); int e; int r = hj_escribe(&L, h, f, c, t, &e); return r; }
#define VER(d, esp) do { const char *_v = ver(0, d); ++n; if (strcmp(_v, esp)) { ++fallos; printf("FALLO %d: %s = '%s' (esperaba '%s')\n", __LINE__, d, _v, esp); } } while (0)
#define VERH(h, d, esp) do { const char *_v = ver(h, d); ++n; if (strcmp(_v, esp)) { ++fallos; printf("FALLO %d: %s = '%s' (esperaba '%s')\n", __LINE__, d, _v, esp); } } while (0)
#define F(t, esp) do { esc(0, "Z1", t); hj_recalcula(&L); VER("Z1", esp); } while (0)
static int64_t ahora(void) { return (int64_t)20784 * 1440 + 600; }   /* 2026-11-27 10:00 */

int main(void)
{
    hj_ahora_fn = ahora;
    CK(hj_libro_nuevo(&L) == 0, "libro");
    /* ---- lo que se escribe */
    esc(0, "A1", "1.234,5"); VER("A1", "1.234,5");
    esc(0, "A2", "12 %"); VER("A2", "12%");
    esc(0, "A3", "3/5/2026"); VER("A3", "03/05/2026");
    esc(0, "A4", "14:30"); VER("A4", "14:30");
    esc(0, "A5", "12,5 \342\202\254"); VER("A5", "12,50 \342\202\254");
    esc(0, "A6", "verdadero"); VER("A6", "VERDADERO");
    esc(0, "A7", "'0123"); VER("A7", "0123"); CK(!strcmp(edi(0, "A7"), "'0123"), "apostrofo en la edicion");
    esc(0, "A8", "hola"); VER("A8", "hola");
    esc(0, "A9", "0,1"); VER("A9", "0,1");
    esc(0, "A10", "1e3"); VER("A10", "1000");
    esc(0, "A11", "-7,25"); VER("A11", "-7,25");
    esc(0, "A12", "2026-11-27"); VER("A12", "27/11/2026");
    esc(0, "A13", "123456789012345"); VER("A13", "1,23457E+14");
    esc(0, "A14", "0,333333333333"); VER("A14", "0,3333333333");
    CK(!strcmp(edi(0, "A3"), "3/5/2026"), "edicion de una fecha");
    CK(!strcmp(edi(0, "A2"), "12%"), "edicion de un porcentaje");
    /* ---- formulas basicas */
    hj_libro_libera(&L); hj_libro_nuevo(&L);
    esc(0, "A1", "10"); esc(0, "A2", "20"); esc(0, "A3", "30"); esc(0, "A4", "texto"); esc(0, "B1", "2,5");
    F("=SUMA(A1:A4)", "60"); F("=SUM(A1:A3)", "60"); F("=PROMEDIO(A1:A3)", "20"); F("=MAX(A1:A3;100)", "100"); F("=MIN(A1:A3)", "10");
    F("=CONTAR(A1:A4)", "3"); F("=CONTARA(A1:A4)", "4"); F("=CONTAR.BLANCO(A1:A6)", "2");
    F("=A1*B1", "25"); F("=A1/0", "#\302\241DIV/0!"); F("=2^3^2", "64"); F("=-2^2", "4"); F("=10%", "0,1"); F("=1+2*3", "7"); F("=(1+2)*3", "9");
    F("=\"a\"&\"b\"&1", "ab1"); F("=A1&\" \342\202\254\"", "10 \342\202\254"); F("=1,5+1", "2,5"); F("=SI(A1>5;\"s\303\255\";\"no\")", "s\303\255");
    F("=SI(A1>50;1)", "FALSO"); F("=SI(A1>5;;2)", "0"); F("=Y(A1>5;A2>5)", "VERDADERO"); F("=O(A1>50;A2>50)", "FALSO"); F("=NO(1)", "FALSO");
    F("=SI.ERROR(1/0;\"x\")", "x"); F("=SI.ERROR(5;\"x\")", "5"); F("=SI(1/0;1;2)", "#\302\241DIV/0!"); F("=SI.ERROR(SI(1/0;1;2);3)", "3");
    CK(esc(0, "Z2", "=SUMA(") == -1, "formula mal escrita se rechaza");
    F("=NOEXISTE(1)", "#\302\277NOMBRE?"); F("=SUMA(A1;\"5\")", "15"); F("=A4+1", "#\302\241VALOR!"); F("=\"5\"+1", "6");
    F("=REDONDEAR(2,5;0)", "3"); F("=REDONDEAR(-2,5;0)", "-3"); F("=REDONDEAR(1,005;2)", "1,01"); F("=REDONDEAR(1234;-2)", "1200");
    F("=REDONDEAR.MAS(1,21;1)", "1,3"); F("=REDONDEAR.MENOS(1,29;1)", "1,2"); F("=ENTERO(-1,5)", "-2"); F("=TRUNCAR(-1,5)", "-1");
    F("=RESIDUO(-7;3)", "2"); F("=RAIZ(16)", "4"); F("=RAIZ(-1)", "#\302\241NUM!"); F("=POTENCIA(2;10)", "1024"); F("=ABS(-3)", "3");
    F("=LOG10(1000)", "3"); F("=LN(EXP(2))", "2"); F("=PI()", "3,141592654"); F("=GRADOS(PI())", "180"); F("=SIGNO(-4)", "-1");
    F("=MULTIPLO.SUPERIOR(7;5)", "10"); F("=MULTIPLO.INFERIOR(7;5)", "5"); F("=PRODUCTO(A1:A3)", "6000"); F("=MEDIANA(A1:A3;100)", "25");
    F("=MODA(1;2;2;3)", "2"); F("=DESVEST(2;4;4;4;5;5;7;9)", "2,138089935"); F("=DESVESTP(2;4;4;4;5;5;7;9)", "2"); F("=VAR(1;2;3;4)", "1,666666667");
    F("=K.ESIMO.MAYOR(A1:A3;1)", "30"); F("=K.ESIMO.MENOR(A1:A3;2)", "20"); F("=JERARQUIA(20;A1:A3)", "2");
    F("=SUMAPRODUCTO(A1:A3;A1:A3)", "1400");
    /* ---- condicionales */
    esc(0, "C1", "Madrid"); esc(0, "C2", "Bilbao"); esc(0, "C3", "Madrid"); esc(0, "C4", "madrid");
    esc(0, "D1", "5"); esc(0, "D2", "7"); esc(0, "D3", "9"); esc(0, "D4", "11");
    F("=CONTAR.SI(C1:C4;\"Madrid\")", "3"); F("=CONTAR.SI(D1:D4;\">6\")", "3"); F("=CONTAR.SI(C1:C4;\"*ao\")", "1"); F("=CONTAR.SI(D1:D4;\"<>7\")", "3");
    F("=SUMAR.SI(C1:C4;\"Madrid\";D1:D4)", "25"); F("=SUMAR.SI(D1:D4;\">=9\")", "20"); F("=PROMEDIO.SI(C1:C4;\"madrid\";D1:D4)", "8,333333333");
    F("=CONTAR.SI.CONJUNTO(C1:C4;\"madrid\";D1:D4;\">6\")", "2"); F("=SUMAR.SI.CONJUNTO(D1:D4;C1:C4;\"Madrid\";D1:D4;\"<10\")", "14");
    F("=MAX.SI.CONJUNTO(D1:D4;C1:C4;\"madrid\")", "11"); F("=MIN.SI.CONJUNTO(D1:D4;C1:C4;\"madrid\")", "5"); F("=PROMEDIO.SI.CONJUNTO(D1:D4;C1:C4;\"x\")", "#\302\241DIV/0!");
    F("=SI.CONJUNTO(D1>10;\"a\";D1>4;\"b\")", "b"); F("=ELEGIR(2;\"x\";\"y\";\"z\")", "y");
    /* ---- busquedas */
    esc(0, "F1", "1"); esc(0, "G1", "uno"); esc(0, "F2", "5"); esc(0, "G2", "cinco"); esc(0, "F3", "10"); esc(0, "G3", "diez");
    F("=BUSCARV(5;F1:G3;2;FALSO)", "cinco"); F("=BUSCARV(7;F1:G3;2)", "cinco"); F("=BUSCARV(7;F1:G3;2;0)", "#N/A"); F("=BUSCARV(5;F1:G3;3;0)", "#\302\241REF!");
    F("=COINCIDIR(10;F1:F3;0)", "3"); F("=COINCIDIR(6;F1:F3)", "2"); F("=INDICE(G1:G3;3)", "diez"); F("=INDICE(F1:G3;2;2)", "cinco");
    F("=BUSCARX(\"diez\";G1:G3;F1:F3)", "10"); F("=BUSCARX(\"x\";G1:G3;F1:F3;\"no\")", "no"); F("=BUSCARX(6;F1:F3;G1:G3;;1)", "diez");
    F("=BUSCARH(\"cinco\";G1:G3;1;FALSO)", "#N/A"); F("=FILA(F3)", "3"); F("=COLUMNAS(F1:G3)", "2"); F("=FILA()", "1");
    F("=BUSCARV(\"C*\";G1:G3;1;FALSO)", "cinco");
    /* ---- texto */
    F("=CONCATENAR(\"a\";1;VERDADERO)", "a1VERDADERO"); F("=UNIRCADENAS(\", \";VERDADERO;G1:G3)", "uno, cinco, diez"); F("=CONCAT(G1:G2)", "unocinco");
    F("=IZQUIERDA(\"Espa\303\261a\";5)", "Espa\303\261"); F("=DERECHA(\"Espa\303\261a\";2)", "\303\261a"); F("=EXTRAE(\"Espa\303\261a\";4;2)", "a\303\261"); F("=LARGO(\"Espa\303\261a\")", "6");
    F("=MAYUSC(\"a\303\261o\")", "A\303\221O"); F("=MINUSC(\"\303\221U\")", "\303\261u"); F("=NOMPROPIO(\"juan p\303\251rez\")", "Juan P\303\251rez"); F("=ESPACIOS(\"  a   b \")", "a b");
    F("=SUSTITUIR(\"a-b-c\";\"-\";\"+\")", "a+b+c"); F("=SUSTITUIR(\"a-b-c\";\"-\";\"+\";2)", "a-b+c"); F("=REEMPLAZAR(\"abcdef\";2;3;\"X\")", "aXef");
    F("=ENCONTRAR(\"c\";\"abcabc\";4)", "6"); F("=HALLAR(\"B\";\"abc\")", "2"); F("=ENCONTRAR(\"B\";\"abc\")", "#\302\241VALOR!");
    F("=TEXTO(1234,567;\"#.##0,00\")", "1.234,57"); F("=TEXTO(0,25;\"0%\")", "25%"); F("=TEXTO(FECHA(2026;3;5);\"dd/mm/aaaa\")", "05/03/2026");
    F("=VALOR(\"1.234,5\")", "1234,5"); F("=REPETIR(\"ab\";3)", "ababab"); F("=IGUAL(\"a\";\"A\")", "FALSO"); F("=CARACTER(65)", "A"); F("=CODIGO(\"A\")", "65");
    /* ---- fechas */
    F("=A\303\221O(FECHA(2026;14;1))", "2027"); F("=MES(FECHA(2026;14;1))", "2"); F("=DIA(FECHA(2026;3;0))", "28"); F("=HOY()-FECHA(2026;1;1)", "330");
    F("=DIASEM(FECHA(2026;11;27))", "6"); F("=DIASEM(FECHA(2026;11;27);2)", "5"); F("=DIAS(FECHA(2026;12;31);FECHA(2026;1;1))", "364");
    F("=FIN.MES(FECHA(2024;1;15);1)-FECHA(2024;1;1)", "59"); F("=DIA(FECHA.MES(FECHA(2026;1;31);1))", "28"); F("=DIAS.LAB(FECHA(2026;11;23);FECHA(2026;11;29))", "5");
    F("=SIFECHA(FECHA(1990;5;17);FECHA(2026;11;27);\"Y\")", "36"); F("=SIFECHA(FECHA(2026;1;31);FECHA(2026;3;1);\"M\")", "1"); F("=SIFECHA(FECHA(2026;1;31);FECHA(2026;3;1);\"MD\")", "1");
    F("=HORA(NSHORA(14;30;0))", "14"); F("=MINUTO(\"14:30\")", "30"); F("=NUM.DE.SEMANA(FECHA(2026;1;1))", "1");
    /* ---- finanzas */
    F("=REDONDEAR(PAGO(0,05/12;360;200000);2)", "-1073,64"); F("=REDONDEAR(VF(0,06/12;10;-200;-500;1);2)", "2581,4"); F("=REDONDEAR(VA(0,08/12;240;500);2)", "-59777,15");
    F("=REDONDEAR(VNA(0,1;-10000;3000;4200;6800);2)", "1188,44"); esc(0, "H1", "-70000"); esc(0, "H2", "12000"); esc(0, "H3", "15000"); esc(0, "H4", "18000"); esc(0, "H5", "21000"); esc(0, "H6", "26000");
    F("=REDONDEAR(TIR(H1:H6);4)", "0,0866");
    /* ---- informacion */
    F("=ESBLANCO(Q1)", "VERDADERO"); F("=ESNUMERO(A1)", "VERDADERO"); F("=ESTEXTO(A4)", "VERDADERO"); F("=ESERROR(1/0)", "VERDADERO"); F("=SI.ND(NOD();\"x\")", "x"); F("=ESNOD(NOD())", "VERDADERO");
    /* ---- ciclos y cadenas largas (sin recursion en C) */
    esc(0, "K1", "=K2+1"); esc(0, "K2", "=K1+1"); hj_recalcula(&L); VER("K1", "#\302\241CIRC!"); VER("K2", "#\302\241CIRC!");
    esc(0, "K3", "=K3"); hj_recalcula(&L); VER("K3", "#\302\241CIRC!");
    esc(0, "M1", "1"); for (int i = 2; i <= 3000; ++i) { char d[16], t[32]; snprintf(d, sizeof d, "M%d", i); snprintf(t, sizeof t, "=M%d+1", i - 1); esc(0, d, t); }
    for (int i = 1; i < 3000; ++i) { char d[16], t[32]; snprintf(d, sizeof d, "N%d", i); snprintf(t, sizeof t, "=N%d+1", i + 1); esc(0, d, t); }
    esc(0, "N3000", "5");
    hj_recalcula(&L); VER("M3000", "3000"); VER("N1", "3004");
    esc(0, "P1", "=SUMA(Q1:Q2000)"); for (int i = 1; i <= 2000; ++i) { char d[16]; snprintf(d, sizeof d, "Q%d", i); esc(0, d, "=FILA()"); }
    hj_recalcula(&L); VER("P1", "2001000");
    /* ---- traduccion */
    { char o[256]; CK(hj_form_a_canon(&L, 0, "=SUMA(A1:A3;1,5;\"a;b\";VERDADERO)", o, sizeof o, 0) == 0 && !strcmp(o, "SUM(A1:A3,1.5,\"a;b\",TRUE)"), o);
      CK(hj_form_a_ui(&L, 0, "IF(ISERROR(A1),#DIV/0!,ROUND(B2,2))", o, sizeof o) == 0 && !strcmp(o, "SI(ESERROR(A1);#\302\241DIV/0!;REDONDEAR(B2;2))"), o);
      CK(hj_form_a_canon(&L, 0, "a1+$b$2*sum(c:c)", o, sizeof o, 0) == 0 && !strcmp(o, "A1+$B$2*SUM(C:C)"), o);
      CK(hj_form_a_ui(&L, 0, "_xlfn.XLOOKUP(1,A:A,B:B)", o, sizeof o) == 0 && !strcmp(o, "BUSCARX(1;A:A;B:B)"), o);
      CK(hj_form_desplaza(&L, 0, "A1+$A$1+A$1+$A1+SUM(B2:C3)", 2, 1, o, sizeof o) == 0 && !strcmp(o, "B3+$A$1+B$1+$A3+SUM(C4:D5)"), o);
      CK(hj_form_desplaza(&L, 0, "A1", -1, 0, o, sizeof o) == 0 && !strcmp(o, "#REF!"), o); }
    /* ---- varias hojas */
    hj_libro_libera(&L); hj_libro_nuevo(&L);
    int h2 = hj_hoja_nueva(&L, "Gastos de 2026"); CK(h2 == 1, "hoja nueva");
    CK(hj_hoja_nueva(&L, "gastos DE 2026") < 0, "nombre repetido"); CK(hj_hoja_nueva(&L, "a:b") < 0, "nombre no valido");
    esc(1, "A1", "100"); esc(0, "A1", "='Gastos de 2026'!A1*2"); hj_recalcula(&L); VER("A1", "200");
    CK(hj_hoja_renombra(&L, 1, "Gastos") == 0, "renombrar"); CK(!strcmp(edi(0, "A1"), "=Gastos!A1*2"), b); hj_recalcula(&L); VER("A1", "200");
    esc(0, "B1", "=SUMA(Gastos!A1:A5)"); hj_recalcula(&L); VER("B1", "100");
    CK(hj_hoja_borra(&L, 1) == 0, "borrar hoja"); hj_recalcula(&L); VER("A1", "#\302\241REF!"); CK(!strcmp(edi(0, "A1"), "=#\302\241REF!*2") || strstr(edi(0, "A1"), "REF") != 0, b);
    CK(hj_hoja_borra(&L, 0) < 0, "no se borra la ultima");
    /* ---- insertar y borrar filas */
    hj_libro_libera(&L); hj_libro_nuevo(&L);
    esc(0, "A1", "1"); esc(0, "A2", "2"); esc(0, "A3", "3"); esc(0, "B1", "=SUMA(A1:A3)"); esc(0, "B2", "=A3*10");
    hj_inserta_filas(&L, 0, 1, 2); hj_recalcula(&L);
    VER("A4", "2"); VER("A5", "3"); CK(!strcmp(edi(0, "B1"), "=SUMA(A1:A5)"), b); CK(!strcmp(edi(0, "B4"), "=A5*10"), b); VER("B4", "30");
    hj_borra_filas(&L, 0, 4, 1); hj_recalcula(&L);
    CK(!strcmp(edi(0, "B1"), "=SUMA(A1:A4)"), b); CK(strstr(edi(0, "B4"), "REF") != 0, b); VER("B1", "3");
    hj_inserta_cols(&L, 0, 0, 1); hj_recalcula(&L); CK(!strcmp(edi(0, "C1"), "=SUMA(B1:B4)"), b); VER("C1", "3");
    hj_borra_cols(&L, 0, 0, 1); hj_recalcula(&L); VER("B1", "3");
    /* ---- copiar y rellenar */
    hj_libro_libera(&L); hj_libro_nuevo(&L);
    esc(0, "A1", "1"); esc(0, "A2", "2"); esc(0, "B1", "=A1*2");
    hj_rellena(&L, 0, 0, 0, 5, 1, 0); hj_recalcula(&L);
    VER("A6", "6"); VER("B6", "12"); CK(!strcmp(edi(0, "B6"), "=A6*2"), b);
    esc(0, "C1", "lunes"); esc(0, "D1", "Enero"); esc(0, "E1", "Pedido 9"); esc(0, "F1", "1/1/2026");
    hj_rellena(&L, 0, 0, 2, 2, 5, 0); hj_recalcula(&L); VER("C3", "mi\303\251rcoles"); VER("D3", "Marzo"); VER("E3", "Pedido 11"); VER("F3", "03/01/2026");
    hj_copia(&L, 0, 9, 9, 0, 0, 1, 1, 1, 0); hj_recalcula(&L); CK(!strcmp(edi(0, "J10"), "=I10*2"), b);
    hj_copia(&L, 0, 9, 10, 0, 0, 1, 1, 1, 1); hj_recalcula(&L); VER("K10", "2");
    /* ---- ordenar, filtrar, buscar */
    hj_libro_libera(&L); hj_libro_nuevo(&L);
    esc(0, "A1", "Nombre"); esc(0, "B1", "Edad");
    const char *nom[5] = { "Luis", "ana", "Pedro", "\303\201lvaro", "Bea" }; const char *ed[5] = { "30", "25", "", "40", "25" };
    for (int i = 0; i < 5; ++i) { char d[8]; snprintf(d, sizeof d, "A%d", i + 2); esc(0, d, nom[i]); snprintf(d, sizeof d, "B%d", i + 2); if (ed[i][0]) esc(0, d, ed[i]); }
    esc(0, "C2", "=B2*2");
    hj_ordena(&L, 0, 1, 0, 5, 2, 0, 0, -1, 0); hj_recalcula(&L);
    VER("A2", "\303\201lvaro"); VER("A3", "ana"); VER("A6", "Pedro"); { const char *e = edi(0, "C5"); CK(!strcmp(e, "=B5*2"), e); } VER("C5", "60");
    hj_ordena(&L, 0, 1, 0, 5, 2, 1, 1, 0, 0); hj_recalcula(&L); VER("A2", "\303\201lvaro"); VER("A6", "Pedro"); VER("A3", "Luis");
    CK(hj_filtra(&L, 0, 0, 0, 5, 2, 1, ">26") == 2, "filtro >26");
    CK(hj_fila_oculta(&L.h[0], 3) && !hj_fila_oculta(&L.h[0], 1), "filas ocultas");
    esc(0, "E1", "=SUBTOTALES(9;B2:B6)"); esc(0, "E2", "=SUMA(B2:B6)"); hj_recalcula(&L); VER("E1", "70"); VER("E2", "120");
    CK(hj_filtra(&L, 0, 0, 0, 5, 2, 1, "") == 5, "quitar filtro");
    { uint32_t f = 0, c = 0; CK(hj_busca(&L, 0, &f, &c, "ANA", 0, 0) && f == 3 && c == 0, "buscar"); CK(hj_busca(&L, 0, &f, &c, "alvaro", 0, 0) && f == 1, "buscar sin acento y volver arriba"); }
    CK(hj_reemplaza(&L, 0, "e", "E", 0) >= 2, "reemplazar"); VER("A6", "PEdro");
    /* ---- CSV */
    hj_libro_libera(&L); hj_libro_nuevo(&L);
    const char *csv = "Nombre;Importe;Fecha\r\n\"P\303\251rez; Juan\";1.234,50;3/5/2026\r\n\"Dice \"\"hola\"\"\";=1+1;\r\n";
    CK(hj_csv_lee(&L, 0, csv, (uint32_t)strlen(csv), 0) == 3, "CSV ; ");
    VER("A2", "P\303\251rez; Juan"); VER("B2", "1.234,50"); VER("C2", "03/05/2026"); VER("A3", "Dice \"hola\""); VER("B3", "=1+1");
    { static char o[4096]; uint32_t k = hj_csv_escribe(&L, 0, ';', o, sizeof o); CK(k > 0 && strstr(o, "\"P\303\251rez; Juan\"") && strstr(o, "\"Dice \"\"hola\"\"\"") && strstr(o, "\"'=1+1\""), o); }
    hj_libro_libera(&L); hj_libro_nuevo(&L);
    const char *csv2 = "a,b,c\n1.5,\"x,y\",3\n";
    CK(hj_csv_lee(&L, 0, csv2, (uint32_t)strlen(csv2), 0) == 2, "CSV ,"); VER("A2", "1,5"); VER("B2", "x,y");
    /* ---- formato propio: ida y vuelta */
    hj_libro_libera(&L); hj_libro_nuevo(&L);
    esc(0, "A1", "1.234,5"); esc(0, "A2", "texto|con\nraro\\"); esc(0, "A3", "=A1*2"); esc(0, "A4", "25 %"); esc(0, "A5", "3/5/2026"); esc(0, "A6", "falso");
    { struct hj_estilo e = *hj_estilo_de(&L, 0); e.negrita = 1; e.fondo = 0x01FFEE00; e.nf = NF_CODIGO; strcpy(e.codigo, "#,##0.00 \"kg\""); uint32_t id = hj_estilo_id(&L, &e); hj_pon_estilo(&L, 0, 0, 0, (uint16_t)id); }
    hj_pon_ancho(&L.h[0], 0, 150); hj_pon_alto(&L.h[0], 2, 40); L.h[0].inmov_f = 1;
    int h3 = hj_hoja_nueva(&L, "Otra hoja"); esc(h3, "B2", "=Hoja1!A3+1"); L.activa = h3;
    L.h[0].graf[0].tipo = 3; L.h[0].graf[0].r2 = 5; L.h[0].graf[0].c2 = 1; L.h[0].graf[0].ancho = 400; L.h[0].graf[0].alto = 300; strcpy(L.h[0].graf[0].titulo, "Tarta"); L.h[0].ngraf = 1;
    hj_recalcula(&L);
    { uint32_t cap = hj_serializa_tam(&L); char *o = malloc(cap); uint32_t k = hj_serializa(&L, o, cap); CK(k > 0, "serializa");
      struct hj_libro M; CK(hj_carga(&M, o, k) == 0, "carga");
      struct hj_libro t = L; L = M;
      VER("A1", "1.234,50 kg"); VER("A2", "texto|con\nraro\\"); VER("A3", "2469"); VER("A4", "25%"); VER("A5", "03/05/2026"); VER("A6", "FALSO"); VERH(1, "B2", "2470");
      CK(hj_ancho(&L.h[0], 0) == 150 && hj_alto(&L.h[0], 2) == 40 && L.h[0].inmov_f == 1 && L.activa == 1 && L.h[0].ngraf == 1 && !strcmp(L.h[0].graf[0].titulo, "Tarta"), "ida y vuelta: anchos, inmovilizar, hoja activa, grafico");
      CK(hj_estilo_de(&L, hj_celda(&L, 0, 0, 0)->estilo)->negrita && hj_estilo_de(&L, hj_celda(&L, 0, 0, 0)->estilo)->fondo == 0x01FFEE00, "estilo");
      L = t; hj_libro_libera(&M); free(o); }
    { struct hj_libro M; CK(hj_carga(&M, "nada", 4) == -1, "no es una hoja"); }
    /* formula de un archivo con una funcion que SOMA no tiene: se respeta su valor */
    hj_pon_num(&L, 0, 20, 0, F64_ONE); hj_pon_form_cache(&L, 0, 20, 0, "CUBEVALUE(\"x\")"); hj_recalcula(&L); { const char *v = ver(0, "A21"); CK(!strcmp(v, "1"), v); }
    /* ---- fuzz: formulas, CSV y formato propio al azar */
    srand(358);
    static char fz[2048];
    for (int it = 0; it < 20000; ++it) {
        int len = rand() % 200;
        static const char A[] = "=+-*/^&%<>()\";,.:!$'ABCDEFabc0123456789 #SUMAIFOR\303\241\303\261";
        fz[0] = '='; for (int j = 1; j < len; ++j) fz[j] = it % 2 ? A[rand() % (sizeof A - 1)] : (char)(rand() % 256);
        fz[len > 0 ? len : 1] = 0;
        char o[512]; int e; (void)hj_form_a_canon(&L, 0, fz, o, sizeof o, &e); (void)hj_form_a_ui(&L, 0, fz + 1, o, sizeof o);
        (void)hj_escribe(&L, 0, (uint32_t)(rand() % 30), (uint32_t)(rand() % 10), fz, &e);
        if (it % 500 == 0) hj_recalcula(&L);
    }
    hj_recalcula(&L);
    for (int it = 0; it < 3000; ++it) {
        int len = rand() % 1500; memcpy(fz, "SOMA-HOJA 1\n", 12);
        for (int j = 12; j < len; ++j) fz[j] = "HCEWAOGRF|0123456789\nsnfbegy\\-=SUM(A1)"[rand() % 37];
        struct hj_libro M; if (hj_carga(&M, fz, (uint32_t)(len > 12 ? len : 12)) == 0) hj_libro_libera(&M);
        struct hj_libro N; hj_libro_nuevo(&N); (void)hj_csv_lee(&N, 0, fz, (uint32_t)len, 0); hj_recalcula(&N); hj_libro_libera(&N);
    }
    hj_libro_libera(&L);
    printf("test_hoja_358: %d comprobaciones, %d fallos\n", n, fallos);
    return fallos ? 1 : 0;
}
