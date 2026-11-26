/* SOMA_336_INVESTIGA_TEMAS: el tema de una pregunta y que fuentes valen mas para el. Ver inv_tema.h. Codigo puro. */
#include "inv_tema.h"

/* ---------------------------------------------------------------- texto */
/* la pregunta en minusculas, sin tildes, con la puntuacion como espacio y un espacio delante y detras (" ... ") */
static uint32_t normaliza(const char *s, char *o, uint32_t cap)
{
    uint32_t n = 0;
    if (cap < 3) { if (cap) o[0] = 0; return 0; }
    o[n++] = ' ';
    while (*s && n + 2 < cap) {
        unsigned char a = (unsigned char)s[0], b = (unsigned char)s[1];
        char c;
        if (a == 0xC3 && b) {
            unsigned char d = b | 0x20;
            c = d == 0xA1 ? 'a' : d == 0xA9 ? 'e' : d == 0xAD ? 'i' : d == 0xB3 ? 'o' : (d == 0xBA || d == 0xBC) ? 'u' : d == 0xB1 ? 'n' : ' ';
            s += 2;
        } else if (a == 0xC2 && b) { c = ' '; s += 2; }               /* signos de apertura */
        else if (a >= 0x80) { c = ' '; s++; }
        else {
            c = (char)a; s++;
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) c = ' ';
        }
        if (c == ' ' && o[n - 1] == ' ') continue;
        o[n++] = c;
    }
    if (o[n - 1] != ' ') o[n++] = ' ';
    o[n] = 0;
    return n;
}
static int contiene(const char *h, const char *w)
{
    for (; *h; h++) { const char *a = h, *b = w; while (*b && *a == *b) { a++; b++; } if (!*b) return 1; }
    return 0;
}
/* cuantas palabras (o grupos de palabras) de la lista aparecen; cada una se escribe con espacios alrededor (" gol ") */
static int cuenta(const char *t, const char *const *l) { int k = 0; for (int i = 0; l[i]; i++) if (contiene(t, l[i])) k++; return k; }

static const char *const DEPORTES[] = { " futbol", " liga ", " laliga ", " champions ", " partido", " gol ", " goles ", " marcador", " clasificacion ",
    " jornada", " fichaje", " entrenador", " baloncesto ", " nba ", " acb ", " euroliga ", " tenis ", " atp ", " wta ", " roland garros ", " wimbledon ",
    " formula 1 ", " f1 ", " gran premio ", " motogp ", " ciclismo ", " tour de francia ", " mundial de ", " eurocopa ", " olimpiada", " juegos olimpicos ",
    " real madrid ", " barca ", " fc barcelona ", " atletico de madrid ", " seleccion espanola ", " golf ", " rugby ", " boxeo ", " ufc ", " atletismo ",
    " maraton ", " nfl ", " super bowl ", " premier league ", " serie a ", " bundesliga ", " copa del rey ", " supercopa ", " alcaraz ", " nadal ",
    " fernando alonso ", " messi ", " mbappe ", " cristiano ronaldo ", " gano el ", " quien gano ", " deporte", " futbolista", " tenista", " piloto ",
    " carrera ", " clasico ", " derbi ", " balon de oro ", " temporada ", 0 };
static const char *const SALUD[] = { " sintoma", " enfermedad", " tratamiento", " medicamento", " farmaco", " dosis ", " efectos secundarios ",
    " diagnostico", " dolor ", " fiebre ", " virus ", " bacteria", " infeccion", " cancer ", " tumor", " diabetes ", " hipertension ", " tension arterial ",
    " colesterol ", " vacuna", " embarazo", " alergia", " medico", " salud ", " nutricion ", " vitamina", " ibuprofeno ", " paracetamol ", " antibiotico",
    " covid", " gripe ", " migrana", " ansiedad ", " depresion ", " insomnio ", " cirugia ", " hospital", " pediatra", " es grave ", " contagio",
    " anemia ", " asma ", " artritis ", " ictus ", " infarto ", " alzheimer ", " parkinson ", " dermatitis ", " tiroides ", " hepatitis ", " sarampion ", 0 };
static const char *const CIENCIA[] = { " fisica ", " quimica ", " biologia ", " astronomia ", " planeta", " galaxia", " estrella", " agujero negro",
    " universo ", " cuantica ", " relatividad ", " evolucion ", " genetica ", " adn ", " celula", " atomo", " molecula", " teorema", " matematica",
    " ecuacion", " nasa ", " telescopio", " cambio climatico ", " dinosaurio", " fosil", " volcan", " terremoto", " especie", " ecologia ",
    " experimento", " cientific", " big bang ", " foton", " electron", " gravedad ", " fotosintesis ", " ecosistema", " neurona", " cometa ",
    " asteroide", " marte ", " jupiter ", " saturno ", " luna ", " sistema solar ", " velocidad de la luz ", " particula", " premio nobel ", 0 };
static const char *const TECNO[] = { " programacion ", " programar ", " python ", " javascript ", " java ", " linux ", " windows ", " android ", " iphone ",
    " ios ", " macos ", " ordenador", " procesador", " cpu ", " gpu ", " tarjeta grafica ", " inteligencia artificial ", " ia ", " llm ", " chatgpt ",
    " software ", " hardware ", " app ", " wifi ", " router ", " smartphone", " movil ", " portatil", " ram ", " ssd ", " kernel ", " codigo ", " api ",
    " ciberseguridad ", " servidor", " base de datos ", " compilador", " navegador ", " github ", " docker ", " rust ", " c++ ", " sql ", " bios ", 0 };
static const char *const ACTUALIDAD[] = { " noticia", " actualidad ", " ultima hora ", " elecciones ", " gobierno ", " presidente", " ministro",
    " ministra", " parlamento ", " congreso ", " politica ", " economia ", " bolsa ", " ibex ", " inflacion ", " ipc ", " guerra ", " conflicto ",
    " ataque ", " huelga ", " ley ", " tribunal ", " sentencia ", " ucrania ", " gaza ", " israel ", " rusia ", " trump ", " sanchez ", " feijoo ",
    " union europea ", " otan ", " onu ", " referendum ", " dimision", " paro ", " pensiones ", " impuesto", " aranceles ", 0 };

int it_tema(const char *pregunta)
{
    char t[600];
    if (!pregunta) return IT_GENERAL;
    normaliza(pregunta, t, sizeof t);
    int k[IT_N] = { 0 };
    k[IT_DEPORTES] = cuenta(t, DEPORTES); k[IT_SALUD] = cuenta(t, SALUD); k[IT_CIENCIA] = cuenta(t, CIENCIA);
    k[IT_TECNO] = cuenta(t, TECNO); k[IT_ACTUALIDAD] = cuenta(t, ACTUALIDAD);
    int mejor = IT_GENERAL, max = 0;
    for (int i = 1; i < IT_N; i++) if (k[i] > max) { max = k[i]; mejor = i; }
    return mejor;
}
int it_reciente(const char *pregunta, int tema)
{
    char t[600];
    static const char *const DIA[] = { " hoy ", " ayer ", " anoche ", " esta manana ", " esta tarde ", " ahora mismo ", " en directo ", " ultima hora ", 0 };
    static const char *const SEMANA[] = { " esta semana ", " ultimo ", " ultima ", " ultimos ", " ultimas ", " reciente", " actual ", " actualmente ",
        " este fin de semana ", " resultado", " marcador", " clasificacion ", " noticias ", " quien gano ", " gano ", " ha ganado ", " va primero ", 0 };
    if (!pregunta) return IT_RECIENTE_NO;
    normaliza(pregunta, t, sizeof t);
    if (cuenta(t, DIA)) return IT_RECIENTE_DIA;
    if (cuenta(t, SEMANA)) return IT_RECIENTE_SEMANA;
    return tema == IT_ACTUALIDAD ? IT_RECIENTE_SEMANA : IT_RECIENTE_NO;
}
const char *it_nombre(int tema)
{
    static const char *const N[IT_N] = { "general", "deportes", "salud", "ciencia", "tecnologia", "actualidad" };
    return tema >= 0 && tema < IT_N ? N[tema] : N[0];
}
int it_wiki_en(int tema) { return tema == IT_CIENCIA || tema == IT_SALUD || tema == IT_TECNO; }

/* ---------------------------------------------------------------- fuentes */
struct sitio { const char *d; int8_t p[IT_N]; };   /* dominio (o "dominio/ruta") y puntos por tema: general, deportes, salud, ciencia, tecno, actualidad */
static const struct sitio SITIOS[] = {
    { "wikipedia.org",            {  4,  1,  2,  5,  3,  1 } },
    { "britannica.com",           {  4,  0,  2,  5,  2,  1 } },
    { "rae.es",                   {  4,  0,  0,  0,  0,  0 } },
    /* salud: organismos y manuales profesionales */
    { "medlineplus.gov",          {  2,  0, 10,  4,  0,  0 } },
    { "who.int",                  {  2,  0, 10,  4,  0,  3 } },
    { "cdc.gov",                  {  1,  0,  9,  3,  0,  1 } },
    { "nih.gov",                  {  1,  0,  9,  5,  0,  0 } },
    { "mayoclinic.org",           {  1,  0,  9,  2,  0,  0 } },
    { "nhs.uk",                   {  1,  0,  9,  2,  0,  0 } },
    { "msdmanuals.com",           {  1,  0,  9,  3,  0,  0 } },
    { "sanidad.gob.es",           {  2,  0, 10,  1,  0,  3 } },
    { "aemps.gob.es",             {  1,  0, 10,  1,  0,  0 } },
    { "cima.aemps.es",            {  1,  0, 10,  1,  0,  0 } },
    { "cun.es",                   {  1,  0,  8,  1,  0,  0 } },
    { "clinicbarcelona.org",      {  1,  0,  8,  1,  0,  0 } },
    { "aecc.es",                  {  1,  0,  8,  1,  0,  0 } },
    { "fisterra.com",             {  1,  0,  8,  1,  0,  0 } },
    { "aeped.es",                 {  1,  0,  8,  0,  0,  0 } },
    { "ecdc.europa.eu",           {  1,  0,  9,  2,  0,  1 } },
    { "cochrane.org",             {  1,  0,  9,  4,  0,  0 } },
    /* deportes */
    { "marca.com",                {  1,  8,  0,  0,  0,  2 } },
    { "as.com",                   {  1,  8,  0,  0,  0,  2 } },
    { "mundodeportivo.com",       {  1,  8,  0,  0,  0,  2 } },
    { "sport.es",                 {  1,  7,  0,  0,  0,  1 } },
    { "relevo.com",               {  1,  7,  0,  0,  0,  1 } },
    { "espn.com",                 {  1,  8,  0,  0,  0,  1 } },
    { "espn.com.ar",              {  1,  8,  0,  0,  0,  1 } },
    { "bbc.com/sport",            {  1,  7,  0,  0,  0,  1 } },
    { "eurosport.es",             {  1,  7,  0,  0,  0,  1 } },
    { "uefa.com",                 {  1,  9,  0,  0,  0,  1 } },
    { "laliga.com",               {  1,  9,  0,  0,  0,  1 } },
    { "fifa.com",                 {  1,  9,  0,  0,  0,  1 } },
    { "rfef.es",                  {  1,  8,  0,  0,  0,  0 } },
    { "nba.com",                  {  1,  9,  0,  0,  0,  0 } },
    { "acb.com",                  {  1,  9,  0,  0,  0,  0 } },
    { "euroleaguebasketball.net", {  1,  9,  0,  0,  0,  0 } },
    { "atptour.com",              {  1,  9,  0,  0,  0,  0 } },
    { "wtatennis.com",            {  1,  9,  0,  0,  0,  0 } },
    { "formula1.com",             {  1,  9,  0,  0,  0,  0 } },
    { "motogp.com",               {  1,  9,  0,  0,  0,  0 } },
    { "olympics.com",             {  1,  9,  0,  0,  0,  0 } },
    { "letour.fr",                {  1,  9,  0,  0,  0,  0 } },
    { "transfermarkt.es",         {  1,  6,  0,  0,  0,  0 } },
    { "transfermarkt.com",        {  1,  6,  0,  0,  0,  0 } },
    { "flashscore.es",            {  0,  6,  0,  0,  0,  0 } },
    { "sofascore.com",            {  0,  6,  0,  0,  0,  0 } },
    { "besoccer.com",             {  0,  6,  0,  0,  0,  0 } },
    /* ciencia */
    { "nasa.gov",                 {  2,  0,  0,  9,  1,  1 } },
    { "esa.int",                  {  2,  0,  0,  9,  1,  1 } },
    { "nature.com",               {  1,  0,  5,  9,  1,  0 } },
    { "science.org",              {  1,  0,  5,  9,  1,  0 } },
    { "scientificamerican.com",   {  1,  0,  3,  7,  1,  0 } },
    { "newscientist.com",         {  1,  0,  2,  7,  1,  0 } },
    { "sciencedaily.com",         {  1,  0,  3,  6,  1,  0 } },
    { "arxiv.org",                {  0,  0,  1,  6,  2,  0 } },
    { "csic.es",                  {  2,  0,  3,  8,  1,  1 } },
    { "agenciasinc.es",           {  1,  0,  5,  8,  2,  1 } },
    { "investigacionyciencia.es", {  1,  0,  2,  7,  1,  0 } },
    { "nationalgeographic.com",   {  2,  0,  0,  6,  0,  0 } },
    { "nationalgeographic.com.es",{  2,  0,  0,  6,  0,  0 } },
    { "iac.es",                   {  1,  0,  0,  8,  0,  0 } },
    { "ign.es",                   {  1,  0,  0,  8,  0,  2 } },
    { "aemet.es",                 {  2,  1,  0,  8,  0,  3 } },
    { "noaa.gov",                 {  1,  0,  0,  8,  0,  1 } },
    { "nobelprize.org",           {  1,  0,  2,  8,  1,  0 } },
    { "khanacademy.org",          {  2,  0,  0,  5,  2,  0 } },
    /* tecnologia */
    { "developer.mozilla.org",    {  1,  0,  0,  0,  9,  0 } },
    { "docs.python.org",          {  1,  0,  0,  0,  9,  0 } },
    { "learn.microsoft.com",      {  1,  0,  0,  0,  8,  0 } },
    { "wiki.archlinux.org",       {  1,  0,  0,  0,  8,  0 } },
    { "kernel.org",               {  1,  0,  0,  0,  8,  0 } },
    { "stackoverflow.com",        {  1,  0,  0,  0,  6,  0 } },
    { "github.com",               {  0,  0,  0,  1,  5,  0 } },
    { "arstechnica.com",          {  1,  0,  0,  2,  6,  1 } },
    { "anandtech.com",            {  0,  0,  0,  0,  6,  0 } },
    { "tomshardware.com",         {  0,  0,  0,  0,  6,  0 } },
    { "xataka.com",               {  1,  0,  0,  1,  5,  1 } },
    { "genbeta.com",              {  1,  0,  0,  0,  5,  0 } },
    { "theverge.com",             {  1,  0,  0,  0,  5,  1 } },
    /* actualidad */
    { "rtve.es",                  {  3,  7,  3,  3,  2,  8 } },
    { "efe.com",                  {  2,  5,  2,  2,  1,  8 } },
    { "europapress.es",           {  2,  5,  2,  2,  1,  7 } },
    { "elpais.com",               {  2,  4,  2,  2,  2,  7 } },
    { "elmundo.es",               {  2,  4,  1,  1,  1,  6 } },
    { "abc.es",                   {  2,  4,  1,  1,  1,  6 } },
    { "lavanguardia.com",         {  2,  4,  1,  1,  1,  6 } },
    { "elconfidencial.com",       {  2,  3,  1,  1,  1,  6 } },
    { "eldiario.es",              {  2,  3,  1,  1,  1,  6 } },
    { "20minutos.es",             {  1,  4,  1,  1,  1,  5 } },
    { "reuters.com",              {  2,  4,  2,  2,  2,  9 } },
    { "apnews.com",               {  2,  4,  2,  2,  2,  9 } },
    { "bbc.com",                  {  2,  3,  2,  3,  2,  7 } },
    { "theguardian.com",          {  2,  3,  2,  2,  2,  7 } },
    { "newtral.es",               {  2,  0,  2,  1,  1,  7 } },
    { "maldita.es",               {  2,  0,  3,  1,  1,  7 } },
    { "lamoncloa.gob.es",         {  2,  0,  1,  0,  0,  8 } },
    { "boe.es",                   {  3,  0,  1,  0,  0,  9 } },
    { "ine.es",                   {  3,  0,  1,  1,  0,  9 } },
    { "bde.es",                   {  2,  0,  0,  0,  0,  9 } },
    /* poca calidad para informarse */
    { "quora.com",                { -4, -4, -6, -4, -3, -4 } },
    { "brainly.lat",              { -9, -9, -9, -9, -9, -9 } },
    { "brainly.com",              { -9, -9, -9, -9, -9, -9 } },
    { "answers.yahoo.com",        { -9, -9, -9, -9, -9, -9 } },
    { "amazon.es",                { -9, -9, -9, -9, -9, -9 } },
    { "amazon.com",               { -9, -9, -9, -9, -9, -9 } },
    { "aliexpress.com",           { -9, -9, -9, -9, -9, -9 } },
    { "ebay.es",                  { -9, -9, -9, -9, -9, -9 } },
    { "pinterest.es",             { -9, -9, -9, -9, -9, -9 } },
    { "pinterest.com",            { -9, -9, -9, -9, -9, -9 } },
    { "blogspot.com",             { -1, -1, -4, -2, -1, -2 } },
    { "wordpress.com",            { -1, -1, -4, -2, -1, -2 } },
};
#define NSITIOS ((int)(sizeof SITIOS / sizeof SITIOS[0]))

static void anfitrion(const char *url, char *h, uint32_t cap, const char **ruta)   /* "www.marca.com", ruta = "/futbol/..." */
{
    const char *p = url;
    for (const char *c = url; *c && c[0] != '/'; c++) if (c[0] == ':' && c[1] == '/' && c[2] == '/') { p = c + 3; break; }
    uint32_t n = 0;
    while (*p && *p != '/' && *p != '?' && *p != '#' && *p != ':' && n + 1 < cap) { char c = *p++; h[n++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
    h[n] = 0;
    while (*p && *p != '/' && *p != '?' && *p != '#') p++;
    if (ruta) *ruta = p;
}
static int es_de(const char *h, const char *d)               /* h es d o un subdominio de d */
{
    uint32_t nh = 0, nd = 0; while (h[nh]) nh++; while (d[nd]) nd++;
    if (nd > nh) return 0;
    for (uint32_t i = 0; i < nd; i++) if (h[nh - nd + i] != d[i]) return 0;
    return nh == nd || h[nh - nd - 1] == '.';
}
int it_puntua(int tema, const char *url)
{
    if (tema < 0 || tema >= IT_N || !url) tema = IT_GENERAL;
    char h[128]; const char *ruta = "";
    if (!url) return 0;
    anfitrion(url, h, sizeof h, &ruta);
    if (!h[0]) return -9;
    int p = 0, hecho = 0;
    for (int i = 0; i < NSITIOS && !hecho; i++) {
        const char *d = SITIOS[i].d; char dd[64]; uint32_t k = 0; const char *r = 0;
        while (d[k] && d[k] != '/' && k + 1 < sizeof dd) { dd[k] = d[k]; k++; }
        dd[k] = 0; if (d[k] == '/') r = d + k;
        if (!es_de(h, dd)) continue;
        if (r) { const char *a = ruta, *b = r; while (*b && *a == *b) { a++; b++; } if (*b) continue; }
        p = SITIOS[i].p[tema]; hecho = 1;
    }
    if (!hecho && (es_de(h, "gob.es") || es_de(h, "gov") || es_de(h, "europa.eu"))) p = tema == IT_SALUD || tema == IT_ACTUALIDAD ? 6 : 3;   /* organismos publicos */
    if (!hecho && (es_de(h, "edu") || contiene(h, "universi") || es_de(h, "ac.uk"))) p = tema == IT_DEPORTES ? 0 : 3;                                                  /* universidades */
    /* en salud, lo que vende remedios no es una fuente */
    if (tema == IT_SALUD) {
        char lr[200]; uint32_t k = 0; for (const char *c = ruta; *c && k + 1 < sizeof lr; c++) lr[k++] = (*c >= 'A' && *c <= 'Z') ? (char)(*c + 32) : *c;
        lr[k] = 0;
        if (contiene(lr, "remedio") || contiene(lr, "homeopat") || contiene(lr, "naturist") || contiene(h, "remedio") || contiene(h, "naturist")) p -= 6;
    }
    if (contiene(h, "foro") || contiene(h, "forum")) p -= 3;
    return p;
}
int it_ordena(int tema, char urls[][300], int n)
{
    int pt[64]; if (n > 64) n = 64;
    for (int i = 0; i < n; i++) pt[i] = it_puntua(tema, urls[i]);
    for (int i = 1; i < n; i++) {                                 /* insercion: estable */
        int j = i;
        while (j > 0 && pt[j - 1] < pt[j]) {
            char t[300]; for (int k = 0; k < 300; k++) { t[k] = urls[j][k]; urls[j][k] = urls[j - 1][k]; urls[j - 1][k] = t[k]; }
            int x = pt[j]; pt[j] = pt[j - 1]; pt[j - 1] = x; j--;
        }
    }
    while (n > 0 && pt[n - 1] < 0) n--;
    return n;
}

/* ---------------------------------------------------------------- informe */
const char *it_instrucciones(int tema, int reciente)
{
    static char b[900];
    static const char *const T[IT_N] = {
        "",
        " Es una pregunta de deportes: da la fecha concreta (dia, mes y ano) de cada partido o resultado, el marcador exacto y la competicion. "
        "Compara con la fecha de hoy para separar lo que ya se ha jugado de lo que esta por jugar. Si las fuentes no tienen el ultimo partido o "
        "la ultima jornada, dilo: no completes con datos antiguos.",
        " Es una pregunta de salud: basate primero en las fuentes sanitarias oficiales o profesionales (organismos de salud, hospitales, manuales "
        "medicos). No des dosis ni pautas que no esten en ellas; distingue lo comprobado de lo que no; si hay sintomas de alarma, dilo claramente. "
        "Termina con una frase que recuerde que esta informacion no sustituye la consulta con un profesional sanitario.",
        " Es una pregunta de ciencia: explica con claridad y rigor, distingue el consenso cientifico de las hipotesis o los estudios aislados, da "
        "las cifras con sus unidades y, si ayuda, una formula en LaTeX.",
        " Es una pregunta tecnica: se preciso con las versiones, las ordenes y los nombres; pon el codigo en bloques con su lenguaje y avisa si "
        "la respuesta depende de la version.",
        " Es una pregunta de actualidad: da la fecha de cada hecho, empieza por lo mas reciente, separa los hechos de las opiniones y, si las "
        "fuentes no coinciden, dilo con la fuente de cada version.",
    };
    uint32_t n = 0;
    const char *t = (tema >= 0 && tema < IT_N) ? T[tema] : "";
    while (*t && n + 1 < sizeof b) b[n++] = *t++;
    if (reciente != IT_RECIENTE_NO) {
        const char *r = " Lo que se pregunta es reciente: mira la fecha de hoy y la de cada fuente, y no presentes como actual algo que ya ha cambiado.";
        while (*r && n + 1 < sizeof b) b[n++] = *r++;
    }
    b[n] = 0;
    return b;
}

/* ---------------------------------------------------------------- razonamiento */
uint32_t it_sin_razonamiento(char *r)
{
    uint32_t n = 0; while (r[n]) n++;
    static const char RZ[] = "[Razonamiento]:", RS[] = "[Respuesta]:";
    for (uint32_t i = 0; i + sizeof RS - 1 <= n; ++i) {
        uint32_t k = 0; while (RS[k] && r[i + k] == RS[k]) k++;
        if (!RS[k]) { uint32_t a = i + k, o = 0; while (a < n) r[o++] = r[a++]; r[o] = 0; n = o; break; }
    }
    { uint32_t k = 0; while (RZ[k] && r[k] == RZ[k]) k++; if (!RZ[k]) { r[0] = 0; return 0; } }
    for (;;) {
        uint32_t i = 0, a = n;
        for (; i + 7 <= n; ++i) if (r[i] == '<' && r[i + 1] == 't' && r[i + 2] == 'h' && r[i + 3] == 'i' && r[i + 4] == 'n' && r[i + 5] == 'k' && r[i + 6] == '>') { a = i; break; }
        if (a == n) break;
        uint32_t e = n;
        for (i = a + 7; i + 8 <= n; ++i) if (r[i] == '<' && r[i + 1] == '/' && r[i + 2] == 't' && r[i + 3] == 'h' && r[i + 4] == 'i' && r[i + 5] == 'n' && r[i + 6] == 'k' && r[i + 7] == '>') { e = i + 8; break; }
        uint32_t o = a; for (i = e; i < n; ++i) r[o++] = r[i];
        r[o] = 0; n = o;
    }
    return n;
}
