def _hs(s, n):                                           # hash estable (FNV-1a, el mismo que el C de SOMA)
    v = 2166136261
    for ch in s.encode("utf-8"):
        v = ((v ^ ch) * 16777619) & 0xFFFFFFFF
    return (v % (n - 1)) + 1
