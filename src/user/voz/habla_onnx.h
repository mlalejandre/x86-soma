// SOMA_247_HABLA: lector minimo de modelos ONNX (protobuf) para las voces de Piper.
//
// Solo lo que hace falta para reconstruir una red VITS: los tensores constantes (initializers) y, de cada nodo, su nombre, su tipo,
// sus entradas y sus atributos enteros (dilations, pads, strides, group...). No interpreta el grafo: el modelo lo implementa
// habla_vits.h. Los pesos se copian a memoria alineada como float32 (los modelos de Piper son float32; float16 tambien se acepta).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>

namespace onnxmin {

struct Tensor {
    std::vector<int64_t> dims;
    std::vector<float> f;          // datos en float32 (convertidos si venian en otro tipo)
    std::vector<int64_t> i64;      // datos enteros (constantes de forma; casi nunca hacen falta)
    int64_t numel() const { int64_t n = 1; for (int64_t d : dims) n *= d; return n; }
};
struct Attr { std::string name; int64_t i = 0; float f = 0; std::vector<int64_t> ints; bool has_i = false; };
struct Node {
    std::string name, op;
    std::vector<std::string> in, out;
    std::vector<Attr> attrs;
    const Attr * attr(const char * n) const { for (const Attr & a : attrs) if (a.name == n) return &a; return nullptr; }
    int64_t ai(const char * n, int64_t def) const { const Attr * a = attr(n); if (!a) return def; if (!a->ints.empty()) return a->ints[0]; return a->has_i ? a->i : def; }
    int64_t aint(const char * n, int idx, int64_t def) const { const Attr * a = attr(n); if (!a || (int)a->ints.size() <= idx) return def; return a->ints[idx]; }
};
struct Model {
    std::map<std::string, Tensor> init;
    std::vector<Node> nodes;
    std::map<std::string, int> node_by_name;
    std::string err;
    const Tensor * get(const std::string & n) const { auto it = init.find(n); return it == init.end() ? nullptr : &it->second; }
    const Node * node(const std::string & n) const { auto it = node_by_name.find(n); return it == node_by_name.end() ? nullptr : &nodes[it->second]; }
};

struct Rd {
    const uint8_t * p, * e; bool bad = false;
    uint64_t varint() { uint64_t v = 0; int s = 0; while (p < e && s < 64) { uint8_t b = *p++; v |= (uint64_t)(b & 0x7F) << s; if (!(b & 0x80)) return v; s += 7; } bad = true; return 0; }
    bool key(uint32_t & field, uint32_t & wt) { if (p >= e) return false; uint64_t k = varint(); field = (uint32_t)(k >> 3); wt = (uint32_t)(k & 7); return !bad; }
    Rd sub() { uint64_t n = varint(); Rd r; if (bad || n > (uint64_t)(e - p)) { bad = true; r.p = r.e = e; return r; } r.p = p; r.e = p + n; p += n; return r; }
    void skip(uint32_t wt) {
        if (wt == 0) varint();
        else if (wt == 1) { if (e - p < 8) bad = true; else p += 8; }
        else if (wt == 2) sub();
        else if (wt == 5) { if (e - p < 4) bad = true; else p += 4; }
        else bad = true;
    }
    std::string str() { Rd s = sub(); return std::string((const char *)s.p, (size_t)(s.e - s.p)); }
    uint32_t fixed32() { uint32_t v = 0; if (e - p < 4) { bad = true; return 0; } std::memcpy(&v, p, 4); p += 4; return v; }
};

static inline float half_to_float(uint16_t h)
{
    uint32_t s = (uint32_t)(h & 0x8000) << 16, ex = (h >> 10) & 0x1F, m = h & 0x3FF, u;
    if (ex == 0) { if (!m) u = s; else { ex = 127 - 15 + 1; while (!(m & 0x400)) { m <<= 1; ex--; } m &= 0x3FF; u = s | (ex << 23) | (m << 13); } }
    else if (ex == 31) u = s | 0x7F800000 | (m << 13);
    else u = s | ((ex + 127 - 15) << 23) | (m << 13);
    float f; std::memcpy(&f, &u, 4); return f;
}

static inline bool parse_tensor(Rd r, std::string & name, Tensor & t)
{
    int dtype = 1; const uint8_t * raw = nullptr; size_t raw_n = 0;
    std::vector<float> fdata; std::vector<int64_t> idata;
    uint32_t f, wt;
    while (r.key(f, wt)) {
        if (f == 1) {                                              // dims (empaquetados o no)
            if (wt == 2) { Rd s = r.sub(); while (s.p < s.e && !s.bad) t.dims.push_back((int64_t)s.varint()); }
            else t.dims.push_back((int64_t)r.varint());
        } else if (f == 2 && wt == 0) dtype = (int)r.varint();
        else if (f == 4) {                                         // float_data
            if (wt == 2) { Rd s = r.sub(); while (s.e - s.p >= 4) { uint32_t u = s.fixed32(); float x; std::memcpy(&x, &u, 4); fdata.push_back(x); } }
            else { uint32_t u = r.fixed32(); float x; std::memcpy(&x, &u, 4); fdata.push_back(x); }
        } else if (f == 7 || f == 5) {                             // int64_data / int32_data
            if (wt == 2) { Rd s = r.sub(); while (s.p < s.e && !s.bad) idata.push_back((int64_t)s.varint()); }
            else idata.push_back((int64_t)r.varint());
        } else if (f == 8 && wt == 2) name = r.str();
        else if (f == 9 && wt == 2) { Rd s = r.sub(); raw = s.p; raw_n = (size_t)(s.e - s.p); }
        else r.skip(wt);
        if (r.bad) return false;
    }
    int64_t n = t.numel();
    if (dtype == 1) {                                              // FLOAT
        if (raw) { if (raw_n < (size_t)n * 4) return false; t.f.resize((size_t)n); std::memcpy(t.f.data(), raw, (size_t)n * 4); }
        else t.f.swap(fdata);
    } else if (dtype == 10) {                                      // FLOAT16
        t.f.resize((size_t)n);
        if (raw) { if (raw_n < (size_t)n * 2) return false; for (int64_t k = 0; k < n; k++) { uint16_t h; std::memcpy(&h, raw + 2 * k, 2); t.f[(size_t)k] = half_to_float(h); } }
        else for (int64_t k = 0; k < n && k < (int64_t)idata.size(); k++) t.f[(size_t)k] = half_to_float((uint16_t)idata[(size_t)k]);
    } else if (dtype == 7 || dtype == 6) {                         // INT64 / INT32
        if (raw) { int w = dtype == 7 ? 8 : 4; for (int64_t k = 0; k < n && (size_t)(k + 1) * w <= raw_n; k++) { int64_t v = 0; if (w == 8) std::memcpy(&v, raw + 8 * k, 8); else { int32_t v32; std::memcpy(&v32, raw + 4 * k, 4); v = v32; } t.i64.push_back(v); } }
        else t.i64.swap(idata);
        for (int64_t v : t.i64) t.f.push_back((float)v);
    }
    return true;
}

static inline bool parse_attr(Rd r, Attr & a)
{
    uint32_t f, wt;
    while (r.key(f, wt)) {
        if (f == 1 && wt == 2) a.name = r.str();
        else if (f == 2 && wt == 5) { uint32_t u = r.fixed32(); std::memcpy(&a.f, &u, 4); }
        else if (f == 3 && wt == 0) { a.i = (int64_t)r.varint(); a.has_i = true; }
        else if (f == 8) {
            if (wt == 2) { Rd s = r.sub(); while (s.p < s.e && !s.bad) a.ints.push_back((int64_t)s.varint()); }
            else a.ints.push_back((int64_t)r.varint());
        } else r.skip(wt);
        if (r.bad) return false;
    }
    return true;
}

static inline bool parse_node(Rd r, Node & n)
{
    uint32_t f, wt;
    while (r.key(f, wt)) {
        if (f == 1 && wt == 2) n.in.push_back(r.str());
        else if (f == 2 && wt == 2) n.out.push_back(r.str());
        else if (f == 3 && wt == 2) n.name = r.str();
        else if (f == 4 && wt == 2) n.op = r.str();
        else if (f == 5 && wt == 2) { Attr a; if (!parse_attr(r.sub(), a)) return false; n.attrs.push_back(a); }
        else r.skip(wt);
        if (r.bad) return false;
    }
    return true;
}

// Lee un ModelProto completo de memoria. false = archivo no valido (m.err dice por que).
static inline bool load(const uint8_t * data, size_t size, Model & m)
{
    Rd r{ data, data + size };
    uint32_t f, wt; bool graph = false;
    while (r.key(f, wt)) {
        if (f == 7 && wt == 2) {                                   // graph
            graph = true;
            Rd g = r.sub();
            uint32_t gf, gwt;
            while (g.key(gf, gwt)) {
                if (gf == 1 && gwt == 2) { Node n; if (!parse_node(g.sub(), n)) { m.err = "nodo danado"; return false; } m.node_by_name[n.name] = (int)m.nodes.size(); m.nodes.push_back(std::move(n)); }
                else if (gf == 5 && gwt == 2) { std::string name; Tensor t; if (!parse_tensor(g.sub(), name, t)) { m.err = "tensor danado"; return false; } m.init[name] = std::move(t); }
                else g.skip(gwt);
                if (g.bad) { m.err = "grafo danado"; return false; }
            }
        } else r.skip(wt);
        if (r.bad) { m.err = "no es un archivo ONNX"; return false; }
    }
    if (!graph || m.init.empty()) { m.err = "el archivo no tiene un grafo ONNX con pesos"; return false; }
    return true;
}

} // namespace onnxmin
