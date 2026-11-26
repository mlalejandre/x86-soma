/* SOMA_322_TEX_HOSTIL: el motor LaTeX (src/apps/tex.c) con documentos GENERADOS con estructura y cada cosa de su tamano exacto: el
 * texto sin NUL detras (tex_typeset recibe la longitud), y las operaciones y el pool reservados con la capacidad que se le dice
 * (como hacen el visor, el maquetador y el chat). Lo usa el visor de documentos con archivos .tex de cualquier web y el chat con las
 * formulas que escribe el modelo. Columnas estrechas y capacidades pequenas para forzar los caminos de "no cabe". ASan y UBSan
 * vigilan; ademas ninguna operacion apunta fuera de lo escrito en el pool. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "soma_test.h"
#include "../src/apps/tex.h"

static uint64_t semilla = 322;
static uint32_t rnd(void) { semilla = semilla * 6364136223846793005ULL + 1442695040888963407ULL; return (uint32_t)(semilla >> 33); }

static int fw(const char *s, int n, int px, int st, void *u) { (void)u; int c = 0; for (int i = 0; i < n; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) c++; return c * px * (st & 1 ? 62 : 55) / 100; }
static int fa(int px, void *u) { (void)u; return px * 9 / 10; }
static int fd(int px, void *u) { (void)u; return px / 4; }
static int fi(const char *p, int *w, int *h, void *u) { (void)u; if (strncmp(p, "foto", 4)) return -1; *w = 40000; *h = 30000; return 0; }
static const struct tex_metrics M = { fw, fa, fd, fi, 0 };

static const char *const TROZOS[] = {
    "\\documentclass{article}", "\\usepackage{amsmath}", "\\begin{document}", "\\end{document}", "\\title{", "\\author{", "\\and ", "\\maketitle",
    "\\section{", "\\subsection*{", "\\paragraph{", "\\label{", "\\ref{", "\\eqref{", "\\cite{", "\\footnote{", "}", "{", "\n", "\n\n", "\\\\", "\\newline",
    "\\begin{itemize}", "\\end{itemize}", "\\begin{enumerate}", "\\end{enumerate}", "\\item ", "\\item[", "]", "[",
    "\\begin{tabular}{|l|c|r|}", "\\begin{tabular}{", "\\end{tabular}", "\\hline", "&", "\\begin{verbatim}", "\\end{verbatim}", "\\verb|x|",
    "\\begin{figure}", "\\includegraphics[width=0.5\\textwidth]{foto.png}", "\\includegraphics{", "\\caption{", "\\end{figure}",
    "\\textbf{", "\\textit{", "\\emph{", "\\texttt{", "\\textcolor{red}{", "\\href{https://a.b}{", "\\url{", "\\small", "\\huge",
    "$", "$$", "\\[", "\\]", "\\(", "\\)", "\\begin{equation}", "\\end{equation}", "\\begin{align}", "\\end{align}", "\\nonumber",
    "\\frac{", "\\dfrac{", "\\binom{", "\\sqrt[", "\\sqrt{", "^", "_", "^{", "_{", "\\sum_{i=1}^{n}", "\\int_0^1", "\\lim_{x\\to 0}",
    "\\left(", "\\right)", "\\left[", "\\right.", "\\begin{pmatrix}", "\\end{pmatrix}", "\\begin{cases}", "\\end{cases}",
    "\\hat{", "\\vec{", "\\overline{", "\\mathbb{R}", "\\mathcal{", "\\text{", "\\alpha", "\\Omega", "\\,", "\\quad", "\\unknowncmd",
    "\\'a", "\\~n", "\\\"u", "--", "---", "``", "''", "%", "\\%", "\\&", "\\$", "\\#", "\\_", "\\", "x", "palabra ", "\303\241", "\377", "\342\210\253",
};

int main(int argc, char **argv)
{
    int iter = argc > 1 ? atoi(argv[1]) : 60000;
    char *t = (char *)malloc(16384);
    int malos = 0;
    for (int it = 0; it < iter; ++it) {
        uint32_t k = 0, partes = 1 + rnd() % 200;
        for (uint32_t q = 0; q < partes; ++q) {
            const char *p = TROZOS[rnd() % (sizeof TROZOS / sizeof TROZOS[0])];
            uint32_t rep = (rnd() % 16 == 0) ? 1 + rnd() % 40 : 1;
            for (uint32_t r = 0; r < rep; ++r) for (const char *c = p; *c && k < 16000; ++c) t[k++] = *c;
        }
        if (k && rnd() % 6 == 0) t[rnd() % k] = (char)rnd();
        char *e = (char *)malloc(k ? k : 1); memcpy(e, t, k);
        int cap = 1 + (int)(rnd() % 3000); uint32_t pcap = 1 + rnd() % 40000;
        struct tex_op *ops = (struct tex_op *)malloc((size_t)cap * sizeof *ops);
        char *pool = (char *)malloc(pcap);
        struct tex_out o; memset(&o, 0, sizeof o);
        o.ops = ops; o.cap = cap; o.pool = pool; o.poolcap = pcap;
        int w = (rnd() % 4 == 0) ? 1 + (int)(rnd() % 60) : 60 + (int)(rnd() % 900);
        (void)tex_typeset(e, k, w, 6 + (int)(rnd() % 30), &M, &o);
        if (o.nops < 0 || o.nops > cap || o.npool > pcap || !memchr(o.title, 0, sizeof o.title)) malos++;
        for (int i = 0; i < o.nops && i < cap; i++) {
            if (ops[i].type > TOP_IMAGE) { malos++; break; }
            if ((ops[i].type == TOP_TEXT || ops[i].type == TOP_IMAGE) && (ops[i].s > o.npool || ops[i].n > o.npool - ops[i].s)) {
                malos++; if (getenv("VER")) printf("op %d tipo %d s=%u n=%u npool=%u pcap=%u\n", i, ops[i].type, ops[i].s, ops[i].n, o.npool, pcap); break;
            }
        }
        (void)tex_is_latex(e, k);
        free(ops); free(pool); free(e);
    }
    free(t);
    CHECK_EQ_INT(malos, 0);
    printf("[tex_hostil_322] %d documentos generados\n", iter);
    TEST_MAIN_END("tex_hostil_322");
}
