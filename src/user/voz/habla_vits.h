// SOMA_247_HABLA: sintesis de voz (texto a voz) con las voces VITS de Piper, sin onnxruntime.
//
// Implementa a mano la inferencia de piper_train/vits (MIT) a partir de los pesos del .onnx:
//   1. codificador de texto: embedding + transformer con atencion relativa (ventana 4) -> media y log-varianza por fonema
//   2. predictor estocastico de duraciones (flujos con splines racionales cuadraticos) -> frames por fonema
//   3. flujo de acoplamiento residual inverso (WaveNet) -> variable latente z
//   4. generador HiFi-GAN: conv_pre + subidas de frecuencia (ConvTranspose) + resblocks + conv_post + tanh -> audio
// Todo en float32. Las convoluciones son im2col + producto de matrices con AVX2/FMA (si el compilador lo permite) repartidas
// entre hilos: el 95 % del tiempo es el generador. Los hiperparametros (canales, nucleos, dilataciones, pasos) se leen de los
// pesos y de los atributos de los nodos Conv del ONNX, asi que valen las calidades x_low, low, medium y high.
#pragma once
#include "habla_onnx.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#if defined(__AVX2__) && defined(__FMA__)
#include <immintrin.h>
#define HABLA_AVX2 1
#endif

extern "C" int pthread_create(unsigned long * th, const void * attr, void * (*fn)(void *), void * arg);
extern "C" int pthread_join(unsigned long th, void ** ret);

namespace habla {

// ------------------------------------------------------------------ hilos: un grupo fijo que espera trabajo girando
// (en SOMA los hilos de usuario corren en nucleos secundarios y no pueden hacer llamadas al sistema: solo calculan)
struct Pool {
    typedef void (*fn_t)(void * ctx, int64_t a, int64_t b);
    int n = 1;                                        // hilos en total (incluido el principal)
    unsigned long th[16];
    volatile int quit = 0;
    volatile uint32_t gen = 0;
    volatile int64_t next = 0, total = 0, chunk = 1;
    volatile int busy = 0;
    fn_t fn = nullptr; void * ctx = nullptr;
    static void run_chunks(Pool * p) {
        for (;;) {
            int64_t a = __atomic_fetch_add(&p->next, p->chunk, __ATOMIC_ACQ_REL);
            if (a >= p->total) break;
            int64_t b = a + p->chunk; if (b > p->total) b = p->total;
            p->fn(p->ctx, a, b);
        }
    }
    static void * worker(void * arg) {
        Pool * p = (Pool *)arg; uint32_t seen = 0;
        for (;;) {
            uint32_t g;
            while ((g = __atomic_load_n(&p->gen, __ATOMIC_ACQUIRE)) == seen && !p->quit) __builtin_ia32_pause();
            if (p->quit) break;
            seen = g;
            run_chunks(p);
            __atomic_fetch_sub(&p->busy, 1, __ATOMIC_ACQ_REL);
        }
        return nullptr;
    }
    void start(int want) {
        n = 1;
        for (int i = 1; i < want && i < 16; i++) { if (pthread_create(&th[n - 1], nullptr, worker, this) != 0) break; n++; }
    }
    void stop() { quit = 1; __atomic_add_fetch(&gen, 1, __ATOMIC_RELEASE); for (int i = 0; i < n - 1; i++) pthread_join(th[i], nullptr); n = 1; }
    // reparte [0, total) en trozos de "ch" entre todos los hilos y espera a que terminen
    void run(int64_t tot, int64_t ch, fn_t f, void * c) {
        if (tot <= 0) return;
        if (n == 1 || tot <= ch) { f(c, 0, tot); return; }
        fn = f; ctx = c; total = tot; chunk = ch; next = 0;
        busy = n - 1;
        __atomic_add_fetch(&gen, 1, __ATOMIC_RELEASE);
        run_chunks(this);
        while (__atomic_load_n(&busy, __ATOMIC_ACQUIRE) > 0) __builtin_ia32_pause();
    }
};

// ------------------------------------------------------------------ tensores [canales][tiempo] en filas contiguas
struct Mat {
    int c = 0, t = 0; std::vector<float> d;
    Mat() {}
    Mat(int c_, int t_) : c(c_), t(t_), d((size_t)c_ * t_, 0.0f) {}
    float * row(int i) { return d.data() + (size_t)i * t; }
    const float * row(int i) const { return d.data() + (size_t)i * t; }
};

struct Conv {                                          // Conv1d / ConvTranspose1d con sus atributos del ONNX
    int co = 0, ci = 0, k = 1, dil = 1, padl = 0, padr = 0, stride = 1, group = 1; bool tr = false, ok = false;
    std::vector<float> w, b;                           // w: [co][ci/group][k] (Conv) o, ya reordenada, por fases (ConvTranspose)
    std::vector<std::vector<float>> phase_w;           // ConvTranspose: una convolucion normal por fase de salida
    int phase_k = 0;
};

// ------------------------------------------------------------------ producto de matrices para la convolucion
static const int TILE = 64;                            // columnas de salida por trozo de im2col

struct ConvJob { const Conv * cv; const float * w; int ck, kk, dil, padl; const Mat * x; Mat * y; int tout; const float * bias; std::vector<float> * scratch; int nthr; };

static inline void gemm_tile(const float * W, int co, int ck, const float * X, int n, float * Y, int ystride, const float * bias)
{
    // Y[r][0..n) = bias[r] + sum_k W[r][k] * X[k][0..n)   (X con TILE columnas por fila)
    int r = 0;
#ifdef HABLA_AVX2
    if (n == TILE || n >= 16) {
        int n16 = n & ~15;
        for (; r + 4 <= co; r += 4) {
            const float * w0 = W + (size_t)r * ck, * w1 = w0 + ck, * w2 = w1 + ck, * w3 = w2 + ck;
            for (int j = 0; j < n16; j += 16) {
                __m256 a00 = _mm256_set1_ps(bias ? bias[r] : 0), a01 = a00, a10 = _mm256_set1_ps(bias ? bias[r + 1] : 0), a11 = a10;
                __m256 a20 = _mm256_set1_ps(bias ? bias[r + 2] : 0), a21 = a20, a30 = _mm256_set1_ps(bias ? bias[r + 3] : 0), a31 = a30;
                const float * xp = X + j;
                for (int k = 0; k < ck; k++, xp += TILE) {
                    __m256 x0 = _mm256_loadu_ps(xp), x1 = _mm256_loadu_ps(xp + 8);
                    __m256 b0 = _mm256_broadcast_ss(w0 + k); a00 = _mm256_fmadd_ps(b0, x0, a00); a01 = _mm256_fmadd_ps(b0, x1, a01);
                    __m256 b1 = _mm256_broadcast_ss(w1 + k); a10 = _mm256_fmadd_ps(b1, x0, a10); a11 = _mm256_fmadd_ps(b1, x1, a11);
                    __m256 b2 = _mm256_broadcast_ss(w2 + k); a20 = _mm256_fmadd_ps(b2, x0, a20); a21 = _mm256_fmadd_ps(b2, x1, a21);
                    __m256 b3 = _mm256_broadcast_ss(w3 + k); a30 = _mm256_fmadd_ps(b3, x0, a30); a31 = _mm256_fmadd_ps(b3, x1, a31);
                }
                float * y = Y + (size_t)r * ystride + j;
                _mm256_storeu_ps(y, a00); _mm256_storeu_ps(y + 8, a01);
                _mm256_storeu_ps(y + ystride, a10); _mm256_storeu_ps(y + ystride + 8, a11);
                _mm256_storeu_ps(y + 2 * ystride, a20); _mm256_storeu_ps(y + 2 * ystride + 8, a21);
                _mm256_storeu_ps(y + 3 * ystride, a30); _mm256_storeu_ps(y + 3 * ystride + 8, a31);
            }
            for (int j = n16; j < n; j++) {
                for (int q = 0; q < 4; q++) {
                    const float * wr = W + (size_t)(r + q) * ck; float s = bias ? bias[r + q] : 0;
                    for (int k = 0; k < ck; k++) s += wr[k] * X[(size_t)k * TILE + j];
                    Y[(size_t)(r + q) * ystride + j] = s;
                }
            }
        }
    }
#endif
    for (; r < co; r++) {
        const float * wr = W + (size_t)r * ck; float * y = Y + (size_t)r * ystride;
        float acc[TILE];
        for (int j = 0; j < n; j++) acc[j] = bias ? bias[r] : 0;
        for (int k = 0; k < ck; k++) { float wv = wr[k]; const float * xr = X + (size_t)k * TILE; for (int j = 0; j < n; j++) acc[j] += wv * xr[j]; }
        for (int j = 0; j < n; j++) y[j] = acc[j];
    }
}

static void conv_chunk(void * c, int64_t a, int64_t b)
{
    ConvJob * J = (ConvJob *)c;
    const Mat & x = *J->x; int ci = x.c, T = x.t;
    // cada trozo usa su propio bufer im2col: se toma uno libre (como mucho uno por hilo a la vez)
    static volatile int lockv;
    std::vector<float> * buf = nullptr;
    while (__atomic_exchange_n(&lockv, 1, __ATOMIC_ACQUIRE)) __builtin_ia32_pause();
    for (int i = 0; i < J->nthr; i++) if (!J->scratch[i].empty() && J->scratch[i][0] == 0.0f) { buf = &J->scratch[i]; (*buf)[0] = 1.0f; break; }
    __atomic_store_n(&lockv, 0, __ATOMIC_RELEASE);
    if (!buf) return;                                                    // no pasa: hay un bufer por hilo
    float * X = buf->data() + 16;
    for (int64_t tile = a; tile < b; tile++) {
        int t0 = (int)tile * TILE, n = J->tout - t0 < TILE ? J->tout - t0 : TILE;
        for (int i = 0; i < ci; i++) {
            const float * xr = x.row(i);
            for (int kk = 0; kk < J->kk; kk++) {
                float * dst = X + (size_t)(i * J->kk + kk) * TILE;
                int off = kk * J->dil - J->padl;
                for (int j = 0; j < n; j++) { int s = t0 + j + off; dst[j] = (s >= 0 && s < T) ? xr[s] : 0.0f; }
            }
        }
        gemm_tile(J->w, J->y->c, J->ck, X, n, J->y->row(0) + t0, J->y->t, J->bias);
    }
    while (__atomic_exchange_n(&lockv, 1, __ATOMIC_ACQUIRE)) __builtin_ia32_pause();
    (*buf)[0] = 0.0f;
    __atomic_store_n(&lockv, 0, __ATOMIC_RELEASE);
}

struct Net;
static Pool * g_pool;
static std::vector<float> g_scratch[16];

// y = conv(x) con paso 1: Tout = T + padl + padr - dil*(k-1)
static Mat conv_plain(const float * w, int co, int ci, int k, int dil, int padl, int padr, const float * bias, const Mat & x)
{
    int tout = x.t + padl + padr - dil * (k - 1); if (tout < 0) tout = 0;
    Mat y(co, tout);
    if (!tout) return y;
    int ck = ci * k, nthr = g_pool ? g_pool->n : 1;
    for (int i = 0; i < nthr; i++) if (g_scratch[i].size() < (size_t)ck * TILE + 16) { g_scratch[i].assign((size_t)ck * TILE + 16, 0.0f); }
    ConvJob J{ nullptr, w, ck, k, dil, padl, &x, &y, tout, bias, g_scratch, nthr };
    int64_t tiles = (tout + TILE - 1) / TILE;
    if (g_pool) g_pool->run(tiles, 1, conv_chunk, &J); else conv_chunk(&J, 0, tiles);
    return y;
}

static Mat conv_apply(const Conv & cv, const Mat & x)
{
    if (!cv.tr && cv.group == 1) return conv_plain(cv.w.data(), cv.co, cv.ci, cv.k, cv.dil, cv.padl, cv.padr, cv.b.empty() ? nullptr : cv.b.data(), x);
    if (!cv.tr) {                                                         // convolucion por canales (DDSConv): pocas cuentas
        int tout = x.t + cv.padl + cv.padr - cv.dil * (cv.k - 1);
        Mat y(cv.co, tout);
        for (int c = 0; c < cv.co; c++) {
            const float * wr = cv.w.data() + (size_t)c * cv.k; const float * xr = x.row(c); float * yr = y.row(c);
            float bb = cv.b.empty() ? 0 : cv.b[c];
            for (int t = 0; t < tout; t++) {
                float s = bb;
                for (int kk = 0; kk < cv.k; kk++) { int q = t + kk * cv.dil - cv.padl; if (q >= 0 && q < x.t) s += wr[kk] * xr[q]; }
                yr[t] = s;
            }
        }
        return y;
    }
    // ConvTranspose1d por fases: la salida n = m*s + r - p sale de una convolucion normal de phase_k tomas sobre x
    int s = cv.stride, tout = (x.t - 1) * s - cv.padl - cv.padr + cv.k;
    Mat y(cv.co, tout);
    for (int r = 0; r < s; r++) {
        // y_r[m] = sum_j sum_ci W[ci][co][r + j*s] * x[ci][m - j]  ->  salida n = m*s + r - p   (m = 0 .. T + pk - 2)
        int pk = cv.phase_k;
        Mat src = conv_plain(cv.phase_w[r].data(), cv.co, cv.ci, pk, 1, pk - 1, pk - 1, nullptr, x);
        for (int c = 0; c < cv.co; c++) {
            const float * a = src.row(c); float * o = y.row(c); float bb = cv.b.empty() ? 0 : cv.b[c];
            for (int m = 0; m < src.t; m++) { int nidx = m * s + r - cv.padl; if (nidx >= 0 && nidx < tout) o[nidx] = a[m] + bb; }
        }
    }
    return y;
}

// ------------------------------------------------------------------ funciones elementales
static inline float erf_approx(float x)
{
    // Abramowitz y Stegun 7.1.26 (error < 1.5e-7)
    float s = x < 0 ? -1.0f : 1.0f; x = std::fabs(x);
    float t = 1.0f / (1.0f + 0.3275911f * x);
    float y = 1.0f - (((((1.061405429f * t - 1.453152027f) * t) + 1.421413741f) * t - 0.284496736f) * t + 0.254829592f) * t * std::exp(-x * x);
    return s * y;
}
static inline float gelu(float x) { return 0.5f * x * (1.0f + erf_approx(x * 0.70710678f)); }
static inline float sigmoidf(float x) { return 1.0f / (1.0f + std::exp(-x)); }
static void layer_norm(Mat & x, const std::vector<float> & g, const std::vector<float> & b)
{
    for (int t = 0; t < x.t; t++) {
        double m = 0, v = 0;
        for (int c = 0; c < x.c; c++) m += x.row(c)[t];
        m /= x.c;
        for (int c = 0; c < x.c; c++) { double d = x.row(c)[t] - m; v += d * d; }
        v /= x.c;
        float inv = (float)(1.0 / std::sqrt(v + 1e-5));
        for (int c = 0; c < x.c; c++) { float & e = x.row(c)[t]; e = (float)((e - m) * inv) * g[c] + b[c]; }
    }
}
static void add_inplace(Mat & a, const Mat & b) { for (size_t i = 0; i < a.d.size(); i++) a.d[i] += b.d[i]; }
static void leaky(Mat & a, float s) { for (float & v : a.d) if (v < 0) v *= s; }
static Mat leaky_copy(const Mat & a, float s) { Mat r = a; leaky(r, s); return r; }

struct Rng {
    uint64_t s = 0x9E3779B97F4A7C15ULL; bool have = false; float spare = 0;
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    float uni() { return (float)((next() >> 11) * (1.0 / 9007199254740992.0)); }
    float normal() {
        if (have) { have = false; return spare; }
        float u1 = uni(), u2 = uni(); if (u1 < 1e-12f) u1 = 1e-12f;
        float r = std::sqrt(-2.0f * std::log(u1)), th = 6.2831853f * u2;
        spare = r * std::sin(th); have = true; return r * std::cos(th);
    }
};

// ------------------------------------------------------------------ la red
struct LN { std::vector<float> g, b; };
struct DDS { std::vector<Conv> sep, one; std::vector<LN> n1, n2; };
struct CFlow { Conv pre, proj; DDS convs; };
struct WNL { Conv in, rs; };
struct RCL { Conv pre, post, cond; std::vector<WNL> wn; };
struct Attn { Conv q, k, v, o; std::vector<float> rk, rv; int win = 4; };
struct FFNL { Conv c1, c2; };

struct Net {
    std::string err;
    int H = 192, nheads = 2, nvocab = 0, inter = 192;
    std::vector<float> emb;
    std::vector<Attn> attn; std::vector<FFNL> ffn; std::vector<LN> ln1, ln2;
    Conv enc_proj;
    // duraciones: estocastico (sdp) o determinista
    bool sdp = true;
    Conv dp_pre, dp_proj; DDS dp_convs; std::vector<CFlow> dp_flows; std::vector<float> ea_m, ea_expneg;   // sdp
    Conv dd_c1, dd_c2, dd_proj; LN dd_n1, dd_n2;                                                         // determinista
    std::vector<RCL> flows;                     // en orden de inferencia (inverso)
    Conv conv_pre, conv_post; std::vector<Conv> ups; std::vector<std::vector<std::vector<Conv>>> res;   // res[i][j] = convs del resblock j de la subida i
    int res_type = 1, nk = 3;                   // ResBlock1 (convs1/convs2) o ResBlock2 (convs)
    int n_spk = 0, gin = 0; std::vector<float> emb_g; Conv dp_cond, dec_cond;   // voces con varios hablantes
    std::vector<std::vector<int>> res_pairs;    // ResBlock1: cada paso son 2 convs

    bool fail(const std::string & e) { if (err.empty()) err = e; return false; }

    static std::string node_path(const std::string & mod) {             // "dec.ups.0" -> "/dec/ups.0"
        std::string s = "/";
        // separar en "." pero manteniendo "ups.0" junto: los modulos de ModuleList llevan indice
        for (size_t i = 0; i < mod.size(); i++) {
            char c = mod[i];
            if (c == '.' && i + 1 < mod.size() && !(mod[i + 1] >= '0' && mod[i + 1] <= '9')) s += '/';
            else s += c;
        }
        return s;
    }
    bool load_conv(const onnxmin::Model & m, const std::string & mod, Conv & cv, bool required = true) {
        std::string np = node_path(mod);
        const onnxmin::Node * n = m.node(np + "/Conv");
        if (!n) { n = m.node(np + "/ConvTranspose"); if (n) cv.tr = true; }
        const onnxmin::Tensor * w = nullptr, * b = nullptr;
        if (n) { if (n->in.size() > 1) w = m.get(n->in[1]); if (n->in.size() > 2) b = m.get(n->in[2]); }
        if (!w) w = m.get(mod + ".weight");
        if (!b) b = m.get(mod + ".bias");
        if (!w || w->dims.size() != 3) { if (required) fail("falta la capa " + mod); return false; }
        if (cv.tr) { cv.ci = (int)w->dims[0]; cv.co = (int)w->dims[1]; }
        else { cv.co = (int)w->dims[0]; cv.ci = (int)w->dims[1]; }
        cv.k = (int)w->dims[2];
        if (n) {
            cv.dil = (int)n->ai("dilations", 1); cv.group = (int)n->ai("group", 1); cv.stride = (int)n->ai("strides", 1);
            cv.padl = (int)n->aint("pads", 0, 0); cv.padr = (int)n->aint("pads", 1, cv.padl);
        } else {                                                           // sin nombres en el grafo: relleno "same"
            cv.padl = cv.padr = (cv.k - 1) * cv.dil / 2;
            if (cv.tr) { cv.stride = cv.k / 2; cv.padl = cv.padr = (cv.k - cv.stride) / 2; }
        }
        if (!cv.tr && cv.group > 1) cv.ci = cv.ci * cv.group;            // por canales: ci = co
        cv.w = w->f; if (b) cv.b = b->f;
        if (cv.tr) {                                                       // reordenar por fases
            int s = cv.stride, pk = (cv.k + s - 1) / s;
            cv.phase_k = pk; cv.phase_w.assign((size_t)s, std::vector<float>((size_t)cv.co * cv.ci * pk, 0.0f));
            for (int r = 0; r < s; r++)
                for (int o = 0; o < cv.co; o++)
                    for (int i = 0; i < cv.ci; i++)
                        for (int kk = 0; kk < pk; kk++) {
                            int j = pk - 1 - kk, tap = r + j * s;
                            if (tap < cv.k) cv.phase_w[(size_t)r][((size_t)o * cv.ci + i) * pk + kk] = cv.w[((size_t)i * cv.co + o) * cv.k + tap];
                        }
            cv.w.clear(); cv.w.shrink_to_fit();
        }
        cv.ok = true;
        return true;
    }
    bool vec(const onnxmin::Model & m, const std::string & name, std::vector<float> & out, bool required = true) {
        const onnxmin::Tensor * t = m.get(name);
        if (!t) { if (required) fail("falta " + name); return false; }
        out = t->f; return true;
    }
    bool load_dds(const onnxmin::Model & m, const std::string & mod, DDS & d) {
        for (int i = 0;; i++) {
            Conv s, o; LN a, b; std::string p = mod + ".convs_sep." + std::to_string(i);
            if (!load_conv(m, p, s, false)) break;
            if (!load_conv(m, mod + ".convs_1x1." + std::to_string(i), o)) return false;
            if (!vec(m, mod + ".norms_1." + std::to_string(i) + ".gamma", a.g) || !vec(m, mod + ".norms_1." + std::to_string(i) + ".beta", a.b)) return false;
            if (!vec(m, mod + ".norms_2." + std::to_string(i) + ".gamma", b.g) || !vec(m, mod + ".norms_2." + std::to_string(i) + ".beta", b.b)) return false;
            d.sep.push_back(s); d.one.push_back(o); d.n1.push_back(a); d.n2.push_back(b);
        }
        return !d.sep.empty() || fail("falta " + mod);
    }

    bool load(const onnxmin::Model & m) {
        // embedding de fonemas: la entrada 0 del Gather de /enc_p/emb (el exportador a veces lo llama "sid")
        const onnxmin::Node * g = m.node("/enc_p/emb/Gather");
        const onnxmin::Tensor * e = g && !g->in.empty() ? m.get(g->in[0]) : nullptr;
        if (!e) e = m.get("enc_p.emb.weight");
        if (!e) e = m.get("sid");
        if (!e || e->dims.size() != 2) return fail("no se encuentra el embedding de fonemas");
        nvocab = (int)e->dims[0]; H = (int)e->dims[1]; emb = e->f;
        for (int i = 0;; i++) {
            std::string p = "enc_p.encoder.attn_layers." + std::to_string(i);
            Attn a;
            if (!load_conv(m, p + ".conv_q", a.q, false)) break;
            if (!load_conv(m, p + ".conv_k", a.k) || !load_conv(m, p + ".conv_v", a.v) || !load_conv(m, p + ".conv_o", a.o)) return false;
            const onnxmin::Tensor * rk = m.get(p + ".emb_rel_k"), * rv = m.get(p + ".emb_rel_v");
            if (!rk || !rv || rk->dims.size() != 3) return fail("falta la atencion relativa");
            if (rk->dims[0] != 1) return fail("atencion relativa por cabeza no soportada");
            a.rk = rk->f; a.rv = rv->f; a.win = (int)(rk->dims[1] - 1) / 2; nheads = H / (int)rk->dims[2];
            FFNL f; std::string q = "enc_p.encoder.ffn_layers." + std::to_string(i);
            if (!load_conv(m, q + ".conv_1", f.c1) || !load_conv(m, q + ".conv_2", f.c2)) return false;
            f.c1.padl = (f.c1.k - 1) / 2; f.c1.padr = f.c1.k / 2; f.c2.padl = (f.c2.k - 1) / 2; f.c2.padr = f.c2.k / 2;   // _same_padding
            LN n1, n2; std::string r = "enc_p.encoder.norm_layers_";
            if (!vec(m, r + "1." + std::to_string(i) + ".gamma", n1.g) || !vec(m, r + "1." + std::to_string(i) + ".beta", n1.b)) return false;
            if (!vec(m, r + "2." + std::to_string(i) + ".gamma", n2.g) || !vec(m, r + "2." + std::to_string(i) + ".beta", n2.b)) return false;
            attn.push_back(a); ffn.push_back(f); ln1.push_back(n1); ln2.push_back(n2);
        }
        if (attn.empty()) return fail("no hay capas en el codificador de texto");
        if (!load_conv(m, "enc_p.proj", enc_proj)) return false;
        inter = enc_proj.co / 2;
        // duraciones
        Conv probe;
        sdp = load_conv(m, "dp.flows.7.pre", probe, false) || m.get("dp.flows.0.m");
        if (sdp) {
            if (!load_conv(m, "dp.pre", dp_pre) || !load_conv(m, "dp.proj", dp_proj) || !load_dds(m, "dp.convs", dp_convs)) return false;
            for (int idx = 7; idx >= 3; idx -= 2) {                        // inferencia: 7, 5, 3 (la 1 no se usa)
                CFlow f; std::string p = "dp.flows." + std::to_string(idx);
                if (!load_conv(m, p + ".pre", f.pre) || !load_conv(m, p + ".proj", f.proj) || !load_dds(m, p + ".convs", f.convs)) return false;
                dp_flows.push_back(f);
            }
            if (!vec(m, "dp.flows.0.m", ea_m)) return false;
            std::vector<float> logs;
            if (vec(m, "dp.flows.0.logs", logs, false)) { ea_expneg.resize(logs.size()); for (size_t i = 0; i < logs.size(); i++) ea_expneg[i] = std::exp(-logs[i]); }
            else {
                const onnxmin::Node * mul = m.node("/dp/flows.0/Mul");
                const onnxmin::Tensor * c = mul && mul->in.size() > 1 ? m.get(mul->in[1]) : nullptr;
                if (!c) return fail("falta dp.flows.0.logs");
                ea_expneg = c->f;
            }
        } else {
            if (!load_conv(m, "dp.conv_1", dd_c1) || !load_conv(m, "dp.conv_2", dd_c2) || !load_conv(m, "dp.proj", dd_proj)) return false;
            if (!vec(m, "dp.norm_1.gamma", dd_n1.g) || !vec(m, "dp.norm_1.beta", dd_n1.b) || !vec(m, "dp.norm_2.gamma", dd_n2.g) || !vec(m, "dp.norm_2.beta", dd_n2.b)) return false;
        }
        // flujo: bloques 0, 2, 4, 6 (en inferencia al reves)
        std::vector<RCL> fw;
        for (int idx = 0;; idx += 2) {
            RCL r; std::string p = "flow.flows." + std::to_string(idx);
            if (!load_conv(m, p + ".pre", r.pre, false)) break;
            if (!load_conv(m, p + ".post", r.post)) return false;
            for (int l = 0;; l++) {
                WNL w; std::string q = p + ".enc.in_layers." + std::to_string(l);
                if (!load_conv(m, q, w.in, false)) break;
                if (!load_conv(m, p + ".enc.res_skip_layers." + std::to_string(l), w.rs)) return false;
                r.wn.push_back(w);
            }
            if (r.wn.empty()) return fail("el flujo no tiene capas WaveNet (pesos sin nombre)");
            fw.push_back(r);
        }
        if (fw.empty()) return fail("falta el flujo");
        for (int i = (int)fw.size() - 1; i >= 0; i--) flows.push_back(fw[i]);
        // generador
        if (!load_conv(m, "dec.conv_pre", conv_pre) || !load_conv(m, "dec.conv_post", conv_post)) return false;
        for (int i = 0;; i++) { Conv u; if (!load_conv(m, "dec.ups." + std::to_string(i), u, false)) break; if (!u.tr) return fail("dec.ups no es ConvTranspose"); ups.push_back(u); }
        if (ups.empty()) return fail("faltan las capas de subida del generador");
        int nres = 0; while (m.get("dec.resblocks." + std::to_string(nres) + ".convs.0.bias") || m.get("dec.resblocks." + std::to_string(nres) + ".convs1.0.bias") ||
                             m.node("/dec/resblocks." + std::to_string(nres) + "/convs.0/Conv") || m.node("/dec/resblocks." + std::to_string(nres) + "/convs1.0/Conv")) nres++;
        if (!nres || nres % (int)ups.size()) return fail("numero de resblocks inesperado");
        nk = nres / (int)ups.size();
        res_type = (m.get("dec.resblocks.0.convs1.0.bias") || m.node("/dec/resblocks.0/convs1.0/Conv")) ? 1 : 2;
        res.assign(ups.size(), std::vector<std::vector<Conv>>((size_t)nk));
        for (int j = 0; j < nres; j++) {
            std::vector<Conv> & v = res[(size_t)(j / nk)][(size_t)(j % nk)];
            std::string p = "dec.resblocks." + std::to_string(j);
            for (int c = 0;; c++) {
                Conv a, b;
                if (res_type == 1) {
                    if (!load_conv(m, p + ".convs1." + std::to_string(c), a, false)) break;
                    if (!load_conv(m, p + ".convs2." + std::to_string(c), b)) return false;
                    v.push_back(a); v.push_back(b);
                } else {
                    if (!load_conv(m, p + ".convs." + std::to_string(c), a, false)) break;
                    v.push_back(a);
                }
            }
            if (v.empty()) return fail("resblock vacio");
        }
        // varios hablantes: embedding de hablante y condicionamiento (dp, flujo, generador)
        const onnxmin::Node * gg = m.node("/emb_g/Gather");
        const onnxmin::Tensor * eg = gg && !gg->in.empty() ? m.get(gg->in[0]) : nullptr;
        if (!eg) eg = m.get("emb_g.weight");
        if (eg && eg->dims.size() == 2) {
            n_spk = (int)eg->dims[0]; gin = (int)eg->dims[1]; emb_g = eg->f;
            if (sdp || true) load_conv(m, "dp.cond", dp_cond, false);
            load_conv(m, "dec.cond", dec_cond, false);
            for (size_t f = 0; f < flows.size(); f++) {
                int idx = 2 * (int)(flows.size() - 1 - f);
                load_conv(m, "flow.flows." + std::to_string(idx) + ".enc.cond_layer", flows[f].cond, false);
            }
        }
        return true;
    }
    Mat gvec(int sid) const {                                           // vector del hablante como Mat [gin][1]
        Mat g(gin, 1);
        if (sid < 0 || sid >= n_spk) sid = 0;
        for (int c = 0; c < gin; c++) g.row(c)[0] = emb_g[(size_t)sid * gin + c];
        return g;
    }
    static void add_bcast(Mat & x, const Mat & v) { for (int c = 0; c < x.c && c < v.c; c++) { float a = v.row(c)[0]; float * r = x.row(c); for (int t = 0; t < x.t; t++) r[t] += a; } }

    // ---------------------------------------------------------------- codificador de texto
    void encode(const std::vector<int64_t> & ids, Mat & x, Mat & mp, Mat & logsp) {
        int T = (int)ids.size();
        x = Mat(H, T);
        float sc = std::sqrt((float)H);
        for (int t = 0; t < T; t++) { int64_t id = ids[(size_t)t]; if (id < 0 || id >= nvocab) id = 0; for (int c = 0; c < H; c++) x.row(c)[t] = emb[(size_t)id * H + c] * sc; }
        int dk = H / nheads;
        std::vector<float> sc_row((size_t)T);
        for (size_t l = 0; l < attn.size(); l++) {
            const Attn & A = attn[l];
            Mat q = conv_apply(A.q, x), k = conv_apply(A.k, x), v = conv_apply(A.v, x), out(H, T);
            float inv = 1.0f / std::sqrt((float)dk);
            for (int h = 0; h < nheads; h++) {
                for (int i = 0; i < T; i++) {
                    float mx = -1e30f;
                    for (int j = 0; j < T; j++) {
                        float s = 0; for (int d = 0; d < dk; d++) s += q.row(h * dk + d)[i] * k.row(h * dk + d)[j];
                        int rel = j - i;
                        if (rel >= -A.win && rel <= A.win) { const float * e = A.rk.data() + (size_t)(rel + A.win) * dk; for (int d = 0; d < dk; d++) s += q.row(h * dk + d)[i] * e[d]; }
                        s *= inv; sc_row[(size_t)j] = s; if (s > mx) mx = s;
                    }
                    double sum = 0; for (int j = 0; j < T; j++) { sc_row[(size_t)j] = std::exp(sc_row[(size_t)j] - mx); sum += sc_row[(size_t)j]; }
                    float is = (float)(1.0 / sum);
                    for (int d = 0; d < dk; d++) {
                        float o = 0; const float * vr = v.row(h * dk + d);
                        for (int j = 0; j < T; j++) o += sc_row[(size_t)j] * vr[j];
                        for (int rel = -A.win; rel <= A.win; rel++) { int j = i + rel; if (j >= 0 && j < T) o += sc_row[(size_t)j] * A.rv[(size_t)(rel + A.win) * dk + d]; }
                        out.row(h * dk + d)[i] = o * is;
                    }
                }
            }
            Mat y = conv_apply(A.o, out);
            add_inplace(x, y); layer_norm(x, ln1[l].g, ln1[l].b);
            Mat f = conv_apply(ffn[l].c1, x);
            for (float & z : f.d) if (z < 0) z = 0;
            Mat f2 = conv_apply(ffn[l].c2, f);
            add_inplace(x, f2); layer_norm(x, ln2[l].g, ln2[l].b);
        }
        Mat st = conv_apply(enc_proj, x);
        mp = Mat(inter, T); logsp = Mat(inter, T);
        for (int c = 0; c < inter; c++) { std::memcpy(mp.row(c), st.row(c), sizeof(float) * T); std::memcpy(logsp.row(c), st.row(c + inter), sizeof(float) * T); }
    }

    void dds(const DDS & d, Mat & x, const Mat * g) {
        if (g) add_inplace(x, *g);
        for (size_t i = 0; i < d.sep.size(); i++) {
            Mat y = conv_apply(d.sep[i], x);
            layer_norm(y, d.n1[i].g, d.n1[i].b); for (float & v : y.d) v = gelu(v);
            Mat z = conv_apply(d.one[i], y);
            layer_norm(z, d.n2[i].g, d.n2[i].b); for (float & v : z.d) v = gelu(v);
            add_inplace(x, z);
        }
    }

    static float spline_inv(float in, const float * uw, const float * uh, const float * ud, int nb, float bound) {
        if (in < -bound || in > bound) return in;
        const float minw = 1e-3f, minh = 1e-3f, mind = 1e-3f;
        float w[32], h[32], cw[33], ch[33], der[33];
        float mx = -1e30f; for (int i = 0; i < nb; i++) if (uw[i] > mx) mx = uw[i];
        double s = 0; for (int i = 0; i < nb; i++) { w[i] = std::exp(uw[i] - mx); s += w[i]; }
        for (int i = 0; i < nb; i++) w[i] = minw + (1 - minw * nb) * (float)(w[i] / s);
        mx = -1e30f; for (int i = 0; i < nb; i++) if (uh[i] > mx) mx = uh[i];
        s = 0; for (int i = 0; i < nb; i++) { h[i] = std::exp(uh[i] - mx); s += h[i]; }
        for (int i = 0; i < nb; i++) h[i] = minh + (1 - minh * nb) * (float)(h[i] / s);
        cw[0] = -bound; ch[0] = -bound; float aw = 0, ah = 0;
        for (int i = 0; i < nb; i++) { aw += w[i]; ah += h[i]; cw[i + 1] = 2 * bound * aw - bound; ch[i + 1] = 2 * bound * ah - bound; }
        cw[nb] = bound; ch[nb] = bound;
        der[0] = 1.0f; der[nb] = 1.0f;                                    // colas lineales: mind + softplus(log(e^(1-mind)-1)) = 1
        for (int i = 1; i < nb; i++) { float u = ud[i - 1]; der[i] = mind + (u > 20 ? u : std::log1p(std::exp(u))); }
        int bin = -1; for (int i = 0; i <= nb; i++) { float edge = ch[i] + (i == nb ? 1e-6f : 0); if (in >= edge) bin++; }
        if (bin < 0) bin = 0;
        if (bin > nb - 1) bin = nb - 1;
        float icw = cw[bin], ibw = cw[bin + 1] - cw[bin], ich = ch[bin], ih = ch[bin + 1] - ch[bin];
        float delta = ih / ibw, d0 = der[bin], d1 = der[bin + 1];
        float a = (in - ich) * (d0 + d1 - 2 * delta) + ih * (delta - d0);
        float b = ih * d0 - (in - ich) * (d0 + d1 - 2 * delta);
        float c = -delta * (in - ich);
        float disc = b * b - 4 * a * c; if (disc < 0) disc = 0;
        float root = (2 * c) / (-b - std::sqrt(disc));
        return root * ibw + icw;
    }

    // ---------------------------------------------------------------- duraciones (frames por fonema)
    std::vector<int> durations(const Mat & xenc, float length_scale, float noise_w, Rng & rng, const Mat * g = nullptr) {
        int T = xenc.t; std::vector<float> logw((size_t)T, 0.0f);
        if (sdp) {
            Mat x = conv_apply(dp_pre, xenc);
            if (g && dp_cond.ok) add_bcast(x, conv_apply(dp_cond, *g));
            dds(dp_convs, x, nullptr);
            x = conv_apply(dp_proj, x);
            Mat z(2, T);
            for (float & v : z.d) v = rng.normal() * noise_w;
            auto flip = [&]() { for (int t = 0; t < T; t++) { float a = z.row(0)[t]; z.row(0)[t] = z.row(1)[t]; z.row(1)[t] = a; } };
            for (size_t f = 0; f < dp_flows.size(); f++) {
                flip();
                const CFlow & F = dp_flows[f];
                Mat x0(1, T); std::memcpy(x0.row(0), z.row(0), sizeof(float) * T);
                Mat h = conv_apply(F.pre, x0);
                dds(F.convs, h, &x);
                Mat p = conv_apply(F.proj, h);                              // [3*nb-1][T]
                int nb = (p.c + 1) / 3; float isq = 1.0f / std::sqrt((float)F.pre.co);
                float uw[32], uh[32], ud[32];
                for (int t = 0; t < T; t++) {
                    for (int i = 0; i < nb; i++) { uw[i] = p.row(i)[t] * isq; uh[i] = p.row(nb + i)[t] * isq; }
                    for (int i = 0; i < nb - 1; i++) ud[i] = p.row(2 * nb + i)[t];
                    z.row(1)[t] = spline_inv(z.row(1)[t], uw, uh, ud, nb, 5.0f);
                }
            }
            flip();
            for (int t = 0; t < T; t++) logw[(size_t)t] = (z.row(0)[t] - ea_m[0]) * ea_expneg[0];
        } else {
            Mat xi = xenc; if (g && dp_cond.ok) add_bcast(xi, conv_apply(dp_cond, *g));
            Mat a = conv_apply(dd_c1, xi); for (float & v : a.d) if (v < 0) v = 0; layer_norm(a, dd_n1.g, dd_n1.b);
            Mat b = conv_apply(dd_c2, a); for (float & v : b.d) if (v < 0) v = 0; layer_norm(b, dd_n2.g, dd_n2.b);
            Mat p = conv_apply(dd_proj, b);
            for (int t = 0; t < T; t++) logw[(size_t)t] = p.row(0)[t];
        }
        std::vector<int> d((size_t)T);
        for (int t = 0; t < T; t++) { float w = std::exp(logw[(size_t)t]) * length_scale; int c = (int)std::ceil(w); d[(size_t)t] = c < 0 ? 0 : c; }
        return d;
    }

    // ---------------------------------------------------------------- flujo inverso
    void flow_inverse(Mat & z, const Mat * g = nullptr) {
        int C = z.c, half = C / 2, T = z.t;
        auto flip = [&]() { for (int c = 0; c < half; c++) for (int t = 0; t < T; t++) { float a = z.row(c)[t]; z.row(c)[t] = z.row(C - 1 - c)[t]; z.row(C - 1 - c)[t] = a; } };
        for (size_t f = 0; f < flows.size(); f++) {
            flip();
            const RCL & R = flows[f];
            Mat x0(half, T); for (int c = 0; c < half; c++) std::memcpy(x0.row(c), z.row(c), sizeof(float) * T);
            Mat h = conv_apply(R.pre, x0);
            int hc = h.c; Mat out(hc, T);
            Mat gc; if (g && R.cond.ok) gc = conv_apply(R.cond, *g);       // [2*hc*capas][1]
            for (size_t l = 0; l < R.wn.size(); l++) {
                Mat xin = conv_apply(R.wn[l].in, h);
                if (gc.c) for (int c = 0; c < 2 * hc; c++) { float a = gc.row((int)l * 2 * hc + c)[0]; float * r = xin.row(c); for (int t = 0; t < T; t++) r[t] += a; }
                Mat acts(hc, T);
                for (int c = 0; c < hc; c++) { const float * a = xin.row(c), * b = xin.row(c + hc); float * o = acts.row(c); for (int t = 0; t < T; t++) o[t] = std::tanh(a[t]) * sigmoidf(b[t]); }
                Mat rs = conv_apply(R.wn[l].rs, acts);
                if (l + 1 < R.wn.size()) {
                    for (int c = 0; c < hc; c++) { float * hr = h.row(c), * orow = out.row(c); const float * r0 = rs.row(c), * r1 = rs.row(c + hc); for (int t = 0; t < T; t++) { hr[t] += r0[t]; orow[t] += r1[t]; } }
                } else add_inplace(out, rs);
            }
            Mat mean = conv_apply(R.post, out);
            for (int c = 0; c < half; c++) { float * x1 = z.row(half + c); const float * mr = mean.row(c); for (int t = 0; t < T; t++) x1[t] -= mr[t]; }
        }
    }

    // ---------------------------------------------------------------- generador HiFi-GAN
    std::vector<float> generate(const Mat & z, const Mat * g = nullptr) {
        Mat x = conv_apply(conv_pre, z);
        if (g && dec_cond.ok) add_bcast(x, conv_apply(dec_cond, *g));
        for (size_t i = 0; i < ups.size(); i++) {
            leaky(x, 0.1f);
            x = conv_apply(ups[i], x);
            Mat xs(x.c, x.t);
            for (int j = 0; j < nk; j++) {
                const std::vector<Conv> & cv = res[i][(size_t)j];
                Mat r = x;
                if (res_type == 1) {
                    for (size_t c = 0; c + 1 < cv.size(); c += 2) {
                        Mat t1 = leaky_copy(r, 0.1f);
                        Mat t2 = conv_apply(cv[c], t1); leaky(t2, 0.1f);
                        Mat t3 = conv_apply(cv[c + 1], t2);
                        add_inplace(r, t3);
                    }
                } else {
                    for (size_t c = 0; c < cv.size(); c++) { Mat t1 = leaky_copy(r, 0.1f); Mat t2 = conv_apply(cv[c], t1); add_inplace(r, t2); }
                }
                add_inplace(xs, r);
            }
            float inv = 1.0f / (float)nk; for (float & v : xs.d) v *= inv;
            x.d.swap(xs.d);
        }
        leaky(x, 0.01f);
        Mat y = conv_apply(conv_post, x);
        std::vector<float> a((size_t)y.t);
        for (int t = 0; t < y.t; t++) a[(size_t)t] = std::tanh(y.row(0)[t]);
        return a;
    }

    // ---------------------------------------------------------------- todo junto: ids de fonemas -> audio (float -1..1)
    std::vector<float> synth(const std::vector<int64_t> & ids, float noise, float length_scale, float noise_w, Rng & rng, int * frames_out = nullptr, int sid = 0) {
        Mat x, mp, lp;
        encode(ids, x, mp, lp);
        Mat gv; const Mat * g = nullptr;
        if (n_spk > 0) { gv = gvec(sid); g = &gv; }
        std::vector<int> d = durations(x, length_scale, noise_w, rng, g);
        int Tf = 0; for (int v : d) Tf += v;
        if (Tf < 1) Tf = 1;
        if (frames_out) *frames_out = Tf;
        Mat z(inter, Tf);
        int f = 0;
        for (int t = 0; t < x.t; t++)
            for (int r = 0; r < d[(size_t)t] && f < Tf; r++, f++)
                for (int c = 0; c < inter; c++) z.row(c)[f] = mp.row(c)[t] + rng.normal() * std::exp(lp.row(c)[t]) * noise;
        flow_inverse(z, g);
        return generate(z, g);
    }
};

} // namespace habla
