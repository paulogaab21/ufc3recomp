
import os as _os

# Raiz do projeto. Por padrao e a pasta que contem este script (tools/..),
# entao o script funciona em qualquer clone. UFC3_ROOT sobrescreve.
PROJ = _os.environ.get("UFC3_ROOT") or _os.path.dirname(
    _os.path.dirname(_os.path.abspath(__file__)))

"""
Acha funcoes que o scanner do rexglue nao descobriu.

Criterio: um endereco A e' um inicio de funcao orfao quando
  1. decodifica como instrucao valida (nao e' .long/.byte/dado)
  2. vem logo depois de um terminador (blr / b / bctr / bclr condicional)
  3. NAO e' inicio de funcao conhecido (codegen.partition.json)
  4. NAO e' alvo de nenhum branch direto em todo o binario
  5. termina com terminador proprio antes do proximo inicio conhecido

(4) e' o filtro decisivo: separa "bloco basico de uma funcao existente"
(sempre e' alvo de algum branch) de "funcao alcancada so indiretamente"
- tabela de construtores estaticos, vtable, ponteiro de funcao. Esta
ultima e' a classe que estoura em runtime como
"Call to invalid or unregistered function".
"""
import json, re, sys, io

DIS = PROJ + r"\work\code.dis"
PART = PROJ + r"\generated\default\codegen.partition.json"
OUT = PROJ + r"\work\orphans.toml"

LINE = re.compile(r"^([0-9a-f]{8}):\t([0-9a-f ]+)\t(\S+)\s*(.*)$")
TARGET = re.compile(r"0x([0-9a-f]{8})")

# Terminadores: encerram o fluxo linear.
RET = {"blr", "bctr", "rfi", "blrl"}
# Condicionais de retorno (bnelr, beqlr, bltlr, ...) tambem encerram, mas
# so condicionalmente -- o fluxo continua na proxima instrucao.
COND_RET = re.compile(r"^b(eq|ne|lt|gt|ge|le|so|ns|dnz|dz)?lr$")

def is_uncond_branch(mnem):
    return mnem == "b" or mnem == "ba"

def main():
    addrs = []          # lista ordenada de (addr, mnem, ops)
    by_addr = {}
    branch_targets = set()
    data_addrs = set()

    with io.open(DIS, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LINE.match(line)
            if not m:
                continue
            a = int(m.group(1), 16)
            mnem = m.group(3)
            ops = m.group(4)
            if mnem.startswith("."):
                data_addrs.add(a)
                by_addr[a] = (".data", "")
                addrs.append(a)
                continue
            by_addr[a] = (mnem, ops)
            addrs.append(a)
            # coleta alvo de qualquer branch direto (condicional ou nao)
            if mnem[0] == "b" and mnem not in RET:
                t = TARGET.search(ops)
                if t:
                    branch_targets.add(int(t.group(1), 16))

    print("instrucoes lidas: %d" % len(addrs))
    print("alvos de branch diretos: %d" % len(branch_targets))

    part = json.load(io.open(PART, encoding="utf-8"))
    starts = set(int(k, 16) for k in part["assignments"].keys())
    print("funcoes conhecidas (partition.json): %d" % len(starts))

    addrs.sort()
    pos = {a: i for i, a in enumerate(addrs)}
    sorted_starts = sorted(starts)

    import bisect
    def next_known_start(a):
        i = bisect.bisect_right(sorted_starts, a)
        return sorted_starts[i] if i < len(sorted_starts) else None

    orphans = []
    for i, a in enumerate(addrs):
        if a in starts or a in data_addrs:
            continue
        if a in branch_targets:
            continue
        mnem, ops = by_addr[a]
        # (2) instrucao anterior precisa ser terminador (pulando padding)
        j = i - 1
        while j >= 0 and addrs[j] in data_addrs:
            j -= 1
        if j < 0:
            continue
        pm = by_addr[addrs[j]][0]
        if not (pm in RET or is_uncond_branch(pm)):
            continue
        # (5) precisa fechar com terminador antes do proximo inicio conhecido
        nxt = next_known_start(a)
        end = None
        k = i
        while k < len(addrs):
            cur = addrs[k]
            if nxt is not None and cur >= nxt:
                break
            cm = by_addr[cur][0]
            if cm == ".data":
                break
            if cm in RET or is_uncond_branch(cm):
                end = cur + 4
                break
            k += 1
        if end is None:
            continue
        size = end - a
        if size <= 0 or size > 0x400:
            continue
        orphans.append((a, size, by_addr[a][0]))

    print("ORFAOS ENCONTRADOS: %d" % len(orphans))
    with io.open(OUT, "w", encoding="utf-8") as f:
        f.write("# Funcoes alcancadas apenas indiretamente (tabela de construtores,\n")
        f.write("# vtable, ponteiro de funcao). Nao tem prologo, nao estao no .pdata,\n")
        f.write("# e nenhum branch direto aponta para elas -- por isso o scanner do\n")
        f.write("# rexglue nao as descobre. Gerado por work/find_orphans.py\n")
        f.write("[entrypoint.functions]\n")
        for a, size, mnem in orphans:
            f.write("0x%08X = { size = 0x%X }\n" % (a, size))
    print("escrito: %s" % OUT)

    for a, size, mnem in orphans[:15]:
        print("  0x%08X  size=0x%-5X  primeira instr: %s" % (a, size, mnem))
    if len(orphans) > 15:
        print("  ... e mais %d" % (len(orphans) - 15))

main()
