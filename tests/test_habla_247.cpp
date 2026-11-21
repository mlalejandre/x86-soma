// SOMA_247_HABLA: pruebas del paso de texto a fonemas en espanol (src/user/voz/habla_g2p_es.h) y del lector ONNX.
// Las frases de referencia son la salida de espeak-ng (voz "es") con la que se entrenan las voces de Piper.
#include "../src/user/voz/habla_g2p_es.h"
#include "../src/user/voz/habla_onnx.h"
#include <cstdio>
#include <cstring>
#include <string>

static int fails, checks;
static void eq(const std::string & got, const char * want, const char * what)
{
    checks++;
    if (got != want) { fails++; std::printf("FALLO %s\n  esperado: %s\n  obtenido: %s\n", what, want, got.c_str()); }
}
static std::string ph(const char * text, bool seseo = false)
{
    g2p_es::Opts op; op.seseo = op.yeismo = seseo;
    std::string o;
    for (auto & s : g2p_es::sentences(text)) { if (!o.empty()) o += " "; o += g2p_es::phonemize_sentence(s, op); }
    return o;
}

static const char * const PAIRS[][2] = {
    { "Hola, ¿cómo estás?", "ˈola, kˈomo estˈas?" },
    { "Llueve en Guadalajara; el agua, el guiso y la cigüeña.", "ʎuˈeβe eŋ ɡwˌaðalaxˈaɾa; el ˈaɣwa, el ɣˈiso i la θˌiɣuˈeɲa." },
    { "Tengo 25 años.", "tˈɛŋɡo βˌeɪntiθˈinko ˈaɲos." },
    { "Los niños juegan en el parque todos los días después del colegio.", "los nˈiɲos xwˈeɣan en el pˈaɾke tˈoðos los ðˈiʲas ðespwˈes ðel kolˈexjo." },
    { "Mañana lloverá en el norte y hará sol en el sur del país.", "maɲˈana ʎˌoβeɾˈa en el nˈoɾte iʲ aɾˈa sˈol en el sˈuɾ ðel paˈis." },
    { "El gobierno anunció nuevas medidas económicas para el próximo año.", "el ɣoβjˈeɾno ˌanunθjˈo nwˈeβas meðˈiðas ˌekonˈomikas pˌaɾa el pɾˈoksimo ˈaɲo." },
    { "Me gustaría reservar una mesa para cuatro personas a las nueve.", "me ɣˌustaɾˈiʲa rˌeseɾβˈaɾ ˈuna mˈesa pˌaɾa kwˈatɾo peɾsˈonas a las nwˈeβe." },
    { "La biblioteca municipal abre de lunes a viernes por la mañana.", "la βˌiβliʲotˈeka mˌuniθipˈal ˈaβɾe ðe lˈunes a βjˈeɾnes poɾ la maɲˈana." },
    { "¿Has visto mis llaves? Las dejé encima de la mesa de la cocina.", "ˈas βˈisto mis ʎˈaβes? las ðexˈe enθˈima ðe la mˈesa ðe la koθˈina." },
    { "Hoy es el cumpleaños de mi abuela y vamos a celebrarlo en familia.", "ˈoɪ ˈes el kˌumpleˈaɲos ðe miʲ aβwˈela i βˈamos a θˌeleβɾˈaɾlo en famˈilja." },
    { "Necesito comprar leche, huevos, pan y un poco de queso.", "nˌeθesˈito kompɾˈaɾ lˈetʃe, wˈeβos, pˈan iʲ ˈum pˈoko ðe kˈeso." },
    { "El tren con destino a Valencia saldrá con veinte minutos de retraso.", "el tɾˈen kon destˈino a βalˈɛnθja salðɾˈa kom bˈeɪnte minˈutos ðe retɾˈaso." },
    { "Querido amigo, te escribo para contarte las novedades del trabajo.", "keɾˈiðo amˈiɣo, te eskɾˈiβo pˌaɾa kontˈaɾte las nˌoβeðˈaðes ðel tɾaβˈaxo." },
    { "La película fue muy emocionante, sobre todo el final.", "la pelˈikula fwˈe mˈuj ˌemoθjonˈante, sˌoβɾe tˈoðo el finˈal." },
    { "Guárdalo en el cajón y no se lo digas a nadie.", "ɡwˈaɾðalo en el kaxˈon i nˈo se lo ðˈiɣas a nˈaðje." },
    { "El agua del río estaba helada aquel invierno.", "el ˈaɣwa ðel rˈiʲo estˈaβa elˈaða akˈel imbjˈeɾno." },
    { "Ayer conocí a una chica muy simpática que trabaja en un hospital.", "aʝˈeɾ kˌonoθˈiʲ a ˈuna tʃˈika mˈuj simpˈatika ke tɾaβˈaxa en ˈun ˌospitˈal." },
    { "También se puede probar sin placa en una Pi 5 emulada (QEMU ).", "tambjˈen se pwˈeðe pɾoβˈaɾ sim plˈaka en ˈuna pˈi θˈinko ˌemulˈaða kˈemu." },
    { "experimental: guarda copia de tus datos y no lo instales como único sistema.", "ˌekspeɾˌimentˈal: ɡwˈaɾða kˈopja ðe tus ðˈatos i nˈo lo instˈales kˌomo ˈuniko sistˈema." },
    { "Los cambios de esta fase están en", "los kˈambjos ðe ˈesta fˈase estˈan ˈen" },
    { "Para informar de un fallo, abre una  issue  con lo que hiciste, el equipo y, si puedes, la", "pˌaɾa ˌinfoɾmˈaɾ ðe ˈun fˈaʎo, ˈaβɾe ˈuna ˈisswe kon lo ke iθˈiste, el ekˈipo i, si pwˈeðes, lˈa" },
    { "salida de la consola serie (las líneas ,  o el informe de pánico).", "salˈiða ðe la konsˈola sˈeɾje las lˈineas, o el infˈoɾme ðe pˈaniko." },
    { "Partición   Formato   Etiqueta   Tamaño   Para qué", "pˌaɾtiθjˈon foɾmˈato ˌetikˈeta tamˈaɲo pˌaɾa kˈe" }
};

int main()
{
    for (auto & p : PAIRS) eq(ph(p[0]), p[1], p[0]);
    // numeros
    eq(g2p_es::number_words(0), "cero", "0");
    eq(g2p_es::number_words(21), "veintiuno", "21");
    eq(g2p_es::number_words(101), "ciento uno", "101");
    eq(g2p_es::number_words(1000), "mil", "1000");
    eq(g2p_es::number_words(2021), "dos mil veintiuno", "2021");
    eq(g2p_es::number_words(31000), "treinta y un mil", "31000");
    eq(g2p_es::number_words(1000000), "un millón", "1e6");
    eq(g2p_es::number_words(3500000), "tres millones quinientos mil", "3.5e6");
    eq(ph("3.500 euros"), "tɾˈes mˈil kinjˈɛntos ˈeʊɾos", "3.500 euros");
    eq(ph("1,5 kilos"), "ˈuno kˈoma θˈinko kˈilos", "1,5");
    // seseo (voces de America)
    eq(ph("La ciudad de Zaragoza", true), "la sjuðˈad ðe sˌaɾaɣˈosa", "seseo");
    eq(ph("doscientos quinientos"), "dosθjˈentos kinjˈɛntos", "cientos");
    // frases largas: se parten
    {
        std::string t; for (int i = 0; i < 90; i++) t += "palabra ";
        auto ss = g2p_es::sentences(t);
        checks++; if (ss.size() < 2) { fails++; std::printf("FALLO: una frase de 90 palabras no se parte\n"); }
    }
    // lector ONNX: un ModelProto minimo con un tensor float y un nodo Conv con atributos
    {
        std::string g;
        auto varint = [](std::string & o, unsigned long long v) { while (v >= 0x80) { o += (char)(v | 0x80); v >>= 7; } o += (char)v; };
        auto field = [&](std::string & o, int f, const std::string & data) { varint(o, (unsigned)(f << 3 | 2)); varint(o, data.size()); o += data; };
        std::string t; varint(t, 1 << 3); varint(t, 2); varint(t, 1 << 3); varint(t, 3);   // dims 2, 3
        varint(t, 2 << 3); varint(t, 1);                                                    // FLOAT
        field(t, 8, "w"); { std::string raw; for (int i = 0; i < 6; i++) { float f = (float)i * 0.5f; raw.append((const char *)&f, 4); } field(t, 9, raw); }
        std::string a; field(a, 1, "dilations"); { std::string ints; varint(ints, 3); field(a, 8, ints); }
        std::string n; field(n, 1, "x"); field(n, 1, "w"); field(n, 3, "/dec/conv/Conv"); field(n, 4, "Conv"); field(n, 5, a);
        field(g, 1, n); field(g, 5, t);
        std::string m; field(m, 7, g);
        onnxmin::Model om;
        bool ok = onnxmin::load((const uint8_t *)m.data(), m.size(), om);
        checks++; if (!ok) { fails++; std::printf("FALLO: ONNX minimo: %s\n", om.err.c_str()); }
        else {
            const onnxmin::Tensor * w = om.get("w"); const onnxmin::Node * c = om.node("/dec/conv/Conv");
            checks++; if (!w || w->dims.size() != 2 || w->f.size() != 6 || w->f[5] != 2.5f) { fails++; std::printf("FALLO: tensor ONNX\n"); }
            checks++; if (!c || c->op != "Conv" || c->ai("dilations", 1) != 3 || c->in.size() != 2) { fails++; std::printf("FALLO: nodo ONNX\n"); }
        }
        onnxmin::Model bad; checks++; if (onnxmin::load((const uint8_t *)"\xff\xff\xff", 3, bad)) { fails++; std::printf("FALLO: acepta basura\n"); }
    }
    std::printf("[habla 247: texto a fonemas y ONNX] %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
