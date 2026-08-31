
import os as _os

# Raiz do projeto. Por padrao e a pasta que contem este script (tools/..),
# entao o script funciona em qualquer clone. UFC3_ROOT sobrescreve.
PROJ = _os.environ.get("UFC3_ROOT") or _os.path.dirname(
    _os.path.dirname(_os.path.abspath(__file__)))

"""
Detector de funcoes orfas, versao 2 -- com fim de funcao por fluxo de controle.

A versao 1 media o tamanho parando no primeiro terminador. Isso esta errado:
funcao real tem varios blocos, e um 'bctr'/'b' no meio nao e' o fim. Aplicar
aqueles tamanhos truncava a funcao que continha o orfao e gerava 2919 erros
de validacao.

Aqui o fim vem de percorrer o grafo:
  - branch condicional  -> visita alvo E queda
  - 'b' para dentro     -> visita alvo (salto interno)
  - 'b' para funcao conhecida ou para fora da regiao -> tail call, fecha o caminho
  - blr / bctr / rfi    -> fecha o caminho
  - bl / bctrl (chamada)-> segue na queda
O tamanho e' (maior endereco visitado + 4) - inicio.

Candidato e' descartado quando:
  - o grafo invade uma funcao conhecida (sobreposicao)
  - o grafo nao fecha dentro de um limite razoavel
  - o corpo nao e' contiguo o bastante (buraco grande = provavel dado)
"""
import json, re, io, bisect

DIS = PROJ + r"\work\code.dis"
PART = PROJ + r"\generated\default\codegen.partition.json"
OUT = PROJ + r"\work\orphans2.toml"

LINE = re.compile(r"^([0-9a-f]{8}):\t([0-9a-f ]+)\t(\S+)\s*(.*)$")
TARGET = re.compile(r"0x([0-9a-f]{8})")

STOP = {"blr", "bctr", "rfi", "blrl", "bctrl"}   # bctrl e' chamada, tratado a parte
CALL = {"bl", "bla", "bctrl", "blrl"}
MAX_SPAN = 0x2000


def load():
    by_addr = {}
    order = []
    data = set()
    with io.open(DIS, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LINE.match(line)
            if not m:
                continue
            a = int(m.group(1), 16)
            mnem, ops = m.group(3), m.group(4)
            order.append(a)
            if mnem.startswith("."):
                data.add(a)
                by_addr[a] = (".data", "")
            else:
                by_addr[a] = (mnem, ops)
    return by_addr, order, data


def classify(mnem, ops):
    """-> (kind, target). kind: stop | call | cond | jump | plain"""
    t = TARGET.search(ops)
    tgt = int(t.group(1), 16) if t else None
    if mnem in ("blr", "bctr", "rfi"):
        return "stop", None
    if mnem in ("bl", "bla", "bctrl", "blrl"):
        return "call", None
    if mnem in ("b", "ba"):
        return "jump", tgt
    if mnem.startswith("b"):
        # bnelr/beqlr... = retorno condicional: fecha um caminho, segue na queda
        if mnem.endswith("lr"):
            return "call", None
        if tgt is not None:
            return "cond", tgt
        return "call", None      # bcctr condicional etc.
    return "plain", None


def main():
    by_addr, order, data = load()
    order.sort()
    print("instrucoes: %d" % len(order))

    branch_targets = set()
    for a in order:
        mnem, ops = by_addr[a]
        if mnem != ".data" and mnem.startswith("b") and mnem not in ("blr", "bctr", "rfi"):
            t = TARGET.search(ops)
            if t:
                branch_targets.add(int(t.group(1), 16))
    print("alvos de branch direto: %d" % len(branch_targets))

    part = json.load(io.open(PART, encoding="utf-8"))
    starts = set(int(k, 16) for k in part["assignments"].keys())
    sorted_starts = sorted(starts)
    print("funcoes conhecidas: %d" % len(starts))

    pos = {a: i for i, a in enumerate(order)}

    def next_start(a):
        i = bisect.bisect_right(sorted_starts, a)
        return sorted_starts[i] if i < len(sorted_starts) else None

    def extent(A, limit):
        """Percorre o CFG a partir de A. -> (fim, ok)"""
        seen = set()
        work = [A]
        while work:
            a = work.pop()
            if a in seen:
                continue
            if a not in by_addr:
                return None, False
            if limit is not None and a >= limit:
                return None, False           # invadiu funcao conhecida
            if a - A > MAX_SPAN:
                return None, False
            seen.add(a)
            mnem, ops = by_addr[a]
            if mnem == ".data":
                seen.discard(a)
                continue
            kind, tgt = classify(mnem, ops)
            if kind == "stop":
                continue
            if kind == "jump":
                if tgt is None:
                    continue
                if tgt in starts:
                    continue                  # tail call
                if A <= tgt and (limit is None or tgt < limit):
                    work.append(tgt)          # salto interno
                continue                      # fora da regiao = tail call
            if kind == "cond":
                if tgt is not None and A <= tgt and (limit is None or tgt < limit):
                    work.append(tgt)
                work.append(a + 4)
                continue
            work.append(a + 4)                # call / plain
        if not seen:
            return None, False
        end = max(seen) + 4
        # exige corpo razoavelmente contiguo: no maximo 25% de buraco
        span = end - A
        if span <= 0 or len(seen) * 4 < span * 0.75:
            return None, False
        return end, True

    orphans = []
    rejected = 0
    for i, a in enumerate(order):
        if a in starts or a in data or a in branch_targets:
            continue
        mnem, ops = by_addr[a]
        j = i - 1
        while j >= 0 and order[j] in data:
            j -= 1
        if j < 0:
            continue
        pm = by_addr[order[j]][0]
        if not (pm in ("blr", "bctr", "rfi") or pm in ("b", "ba")):
            continue
        limit = next_start(a)
        end, ok = extent(a, limit)
        if not ok:
            rejected += 1
            continue
        orphans.append((a, end - a))

    print("ORFAOS ACEITOS: %d   (descartados pelo CFG: %d)" % (len(orphans), rejected))
    with io.open(OUT, "w", encoding="utf-8") as f:
        f.write("# Funcoes alcancadas so indiretamente. Fim de funcao calculado\n")
        f.write("# percorrendo o grafo de fluxo de controle. Gerado por find_orphans2.py\n")
        for a, size in orphans:
            f.write("0x%08X = { size = 0x%X }\n" % (a, size))
    print("escrito: %s" % OUT)

    for probe in (0x831820A8, 0x8300EC08, 0x82B4E120):
        hit = [(a, s) for a, s in orphans if a == probe]
        print("  sonda 0x%08X: %s" % (probe, ("size=0x%X" % hit[0][1]) if hit else "NAO detectado"))


main()
