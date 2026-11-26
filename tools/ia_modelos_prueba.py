#!/usr/bin/env python3
"""SOMA_329_IA_LOCAL: modelos GGUF diminutos (pesos al azar con semilla fija) para probar el motor de IA local sin descargar nada.

    python3 tools/ia_modelos_prueba.py DIR          escribe DIR/emb-prueba.gguf (BERT, embeddings) y DIR/llm-prueba.gguf (llama)

Sin numpy ni gguf-py: un escritor GGUF v3 minimo. Los numeros salen de random.Random(semilla), que da lo mismo en cualquier equipo,
asi que la salida del motor es reproducible (tools/sbx_check.py la compara con la de llama.cpp en el anfitrion).
Los tokenizadores son de verdad (WordPiece para el BERT, SentencePiece con bytes para el llama), con un vocabulario pequeno.
"""
import random
import struct
import sys

T_U32, T_I32, T_F32, T_BOOL, T_STR, T_ARR = 4, 5, 6, 7, 8, 9


def s(b, x):
    x = x.encode("utf-8")
    b += struct.pack("<Q", len(x)) + x


def kv(b, k, t, v, et=None):
    s(b, k)
    b += struct.pack("<I", t)
    if t == T_U32:
        b += struct.pack("<I", v)
    elif t == T_I32:
        b += struct.pack("<i", v)
    elif t == T_F32:
        b += struct.pack("<f", v)
    elif t == T_BOOL:
        b += struct.pack("<B", 1 if v else 0)
    elif t == T_STR:
        s(b, v)
    elif t == T_ARR:
        b += struct.pack("<IQ", et, len(v))
        for e in v:
            if et == T_STR:
                s(b, e)
            elif et == T_I32:
                b += struct.pack("<i", e)
            elif et == T_F32:
                b += struct.pack("<f", e)


def escribe(ruta, kvs, tensores):
    """kvs: [(clave, tipo, valor[, tipo_elem])]; tensores: [(nombre, [ne0, ne1...], lista de floats)]"""
    b = bytearray(b"GGUF") + struct.pack("<IQQ", 3, len(tensores), len(kvs))
    for k in kvs:
        kv(b, *k)
    off = 0
    datos = []
    for nombre, dims, vals in tensores:
        s(b, nombre)
        b += struct.pack("<I", len(dims)) + b"".join(struct.pack("<Q", d) for d in dims)
        b += struct.pack("<IQ", 0, off)                       # F32
        raw = struct.pack("<%df" % len(vals), *vals)
        datos.append(raw)
        off += (len(raw) + 31) // 32 * 32
    b += b"\0" * ((32 - len(b) % 32) % 32)
    for raw in datos:
        b += raw + b"\0" * ((32 - len(raw) % 32) % 32)
    with open(ruta, "wb") as f:
        f.write(b)


def azar(rng, n, esc):
    return [rng.gauss(0.0, 1.0) * esc for _ in range(n)]


PALABRAS = ["hola", "mundo", "que", "tal", "memoria", "soma", "casa", "perro", "gato", "agua", "sol", "luna", "libro", "rojo",
            "azul", "verde", "uno", "dos", "tres", "el", "la", "de", "y", "en", "un", "una", "por", "para", "con", "es"]


def emb(ruta):
    rng = random.Random(329)
    # WordPiece como lo guarda llama.cpp: principio de palabra con "\u2581" y continuaciones sin "##"
    toks = ["[PAD]", "[UNK]", "[CLS]", "[SEP]", "[MASK]"] + ["\u2581" + c for c in ".,!?abcdefghijklmnopqrstuvwxyz0123456789"]
    toks += ["\u2581" + w for w in PALABRAS] + list("abcdefghijklmnopqrstuvwxyz") + ["s", "es", "ando", "ado"]
    toks = list(dict.fromkeys(toks))                          # sin repetidos ("y" es letra y palabra)
    V, E, F, L, H, C = len(toks), 32, 64, 2, 4, 128
    tipos = [3 if t.startswith("[") else 1 for t in toks]
    kvs = [("general.architecture", T_STR, "bert"), ("general.name", T_STR, "emb-prueba"),
           ("bert.context_length", T_U32, C), ("bert.embedding_length", T_U32, E), ("bert.feed_forward_length", T_U32, F),
           ("bert.block_count", T_U32, L), ("bert.attention.head_count", T_U32, H), ("bert.attention.layer_norm_epsilon", T_F32, 1e-12),
           ("bert.attention.causal", T_BOOL, False), ("bert.pooling_type", T_U32, 2),
           ("tokenizer.ggml.model", T_STR, "bert"), ("tokenizer.ggml.pre", T_STR, "bert-bge"),
           ("tokenizer.ggml.tokens", T_ARR, toks, T_STR), ("tokenizer.ggml.token_type", T_ARR, tipos, T_I32),
           ("tokenizer.ggml.token_type_count", T_U32, 2), ("tokenizer.ggml.unknown_token_id", T_U32, 1),
           ("tokenizer.ggml.seperator_token_id", T_U32, 3), ("tokenizer.ggml.padding_token_id", T_U32, 0),
           ("tokenizer.ggml.cls_token_id", T_U32, 2), ("tokenizer.ggml.mask_token_id", T_U32, 4)]
    t = [("token_embd.weight", [E, V], azar(rng, E * V, 0.1)), ("token_types.weight", [E, 2], azar(rng, E * 2, 0.1)),
         ("position_embd.weight", [E, C], azar(rng, E * C, 0.1)),
         ("token_embd_norm.weight", [E], [1.0] * E), ("token_embd_norm.bias", [E], [0.0] * E)]
    for i in range(L):
        for q in "qkv":
            t += [("blk.%d.attn_%s.weight" % (i, q), [E, E], azar(rng, E * E, 0.1)), ("blk.%d.attn_%s.bias" % (i, q), [E], azar(rng, E, 0.01))]
        t += [("blk.%d.attn_output.weight" % i, [E, E], azar(rng, E * E, 0.1)), ("blk.%d.attn_output.bias" % i, [E], azar(rng, E, 0.01)),
              ("blk.%d.attn_output_norm.weight" % i, [E], [1.0] * E), ("blk.%d.attn_output_norm.bias" % i, [E], [0.0] * E),
              ("blk.%d.ffn_up.weight" % i, [E, F], azar(rng, E * F, 0.1)), ("blk.%d.ffn_up.bias" % i, [F], azar(rng, F, 0.01)),
              ("blk.%d.ffn_down.weight" % i, [F, E], azar(rng, F * E, 0.1)), ("blk.%d.ffn_down.bias" % i, [E], azar(rng, E, 0.01)),
              ("blk.%d.layer_output_norm.weight" % i, [E], [1.0] * E), ("blk.%d.layer_output_norm.bias" % i, [E], [0.0] * E)]
    escribe(ruta, kvs, t)


def llm(ruta):
    rng = random.Random(330)
    toks = ["<unk>", "<s>", "</s>"] + ["<0x%02X>" % i for i in range(256)]
    piezas = ["▁" + p for p in PALABRAS] + list("abcdefghijklmnopqrstuvwxyz") + ["▁"]
    piezas = [p for p in dict.fromkeys(piezas) if p not in toks]
    toks += piezas
    V, E, F, L, H = len(toks), 32, 64, 2, 4
    tipos = [2, 3, 3] + [6] * 256 + [1] * len(piezas)
    puntos = [0.0, 0.0, 0.0] + [0.0] * 256 + [-float(i) for i in range(len(piezas))]
    kvs = [("general.architecture", T_STR, "llama"), ("general.name", T_STR, "llm-prueba"),
           ("llama.context_length", T_U32, 256), ("llama.embedding_length", T_U32, E), ("llama.feed_forward_length", T_U32, F),
           ("llama.block_count", T_U32, L), ("llama.attention.head_count", T_U32, H), ("llama.attention.head_count_kv", T_U32, H),
           ("llama.attention.layer_norm_rms_epsilon", T_F32, 1e-5), ("llama.rope.dimension_count", T_U32, E // H),
           ("llama.vocab_size", T_U32, V),
           ("tokenizer.ggml.model", T_STR, "llama"), ("tokenizer.ggml.pre", T_STR, "default"),
           ("tokenizer.ggml.tokens", T_ARR, toks, T_STR), ("tokenizer.ggml.scores", T_ARR, puntos, T_F32),
           ("tokenizer.ggml.token_type", T_ARR, tipos, T_I32),
           ("tokenizer.ggml.bos_token_id", T_U32, 1), ("tokenizer.ggml.eos_token_id", T_U32, 2), ("tokenizer.ggml.unknown_token_id", T_U32, 0),
           ("tokenizer.ggml.add_bos_token", T_BOOL, True), ("tokenizer.ggml.add_eos_token", T_BOOL, False),
           ("tokenizer.chat_template", T_STR, "{% for m in messages %}<|im_start|>{{ m['role'] }}\n{{ m['content'] }}<|im_end|>\n{% endfor %}")]
    t = [("token_embd.weight", [E, V], azar(rng, E * V, 0.5)), ("output_norm.weight", [E], [1.0] * E),
         ("output.weight", [E, V], azar(rng, E * V, 0.5))]
    for i in range(L):
        t += [("blk.%d.attn_norm.weight" % i, [E], [1.0] * E)]
        for q in "qkv":
            t += [("blk.%d.attn_%s.weight" % (i, q), [E, E], azar(rng, E * E, 0.2))]
        t += [("blk.%d.attn_output.weight" % i, [E, E], azar(rng, E * E, 0.2)), ("blk.%d.ffn_norm.weight" % i, [E], [1.0] * E),
              ("blk.%d.ffn_gate.weight" % i, [E, F], azar(rng, E * F, 0.2)), ("blk.%d.ffn_up.weight" % i, [E, F], azar(rng, E * F, 0.2)),
              ("blk.%d.ffn_down.weight" % i, [F, E], azar(rng, F * E, 0.2))]
    escribe(ruta, kvs, t)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    d = sys.argv[1]
    emb(d + "/emb-prueba.gguf")
    llm(d + "/llm-prueba.gguf")
    print("modelos de prueba en", d)
    return 0


if __name__ == "__main__":
    sys.exit(main())
