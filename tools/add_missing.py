
import os as _os

# Raiz do projeto. Por padrao e a pasta que contem este script (tools/..),
# entao o script funciona em qualquer clone. UFC3_ROOT sobrescreve.
PROJ = _os.environ.get("UFC3_ROOT") or _os.path.dirname(
    _os.path.dirname(_os.path.abspath(__file__)))

"""
Dado o endereco de uma funcao FALTANDO (a que estourou como "Call to invalid or
unregistered function"), calcula o fim dela percorrendo o grafo de fluxo de
controle e acrescenta ao manifesto.

Por que grafo e nao "ate o proximo mflr": nem toda funcao seguinte tem prologo.
No caso 0x83008868, logo depois do corpo vinham TRES adjustor thunks
independentes ('addi r3,r3,-4 ; b alvo', 8 bytes cada) antes do proximo mflr.
A regra ingenua engolia os tres e dava 0x70 em vez de 0x58.

O percurso:
  - branch condicional  -> visita alvo E queda
  - 'b' para dentro do corpo ja visitado ou adiante dele -> salto interno
  - 'b' para longe / 'blr' / 'bctr'  -> encerra o caminho
  - 'bl' (chamada)      -> segue na queda
O fim e' (maior endereco visitado + 4).

Uso:  python work\\add_missing.py 0x83008868
      python work\\add_missing.py 0x83008868 --so-mostrar
"""
import io, re, sys, subprocess, os

# PROJ definido no cabecalho
BASE = PROJ + r"\work\base.bin"
MAN = PROJ + r"\ufc3_manifest.toml"
OBJDUMP = _os.path.join(_os.environ.get("REXSDK_SRC", ""),
              "tools", "binutils", "powerpc-none-elf-objdump.exe")

LINE = re.compile(r"^([0-9a-f]{8}):\t([0-9a-f ]+)\t(\S+)\s*(.*)$")
ALVO = re.compile(r"0x([0-9a-f]{8})")
JANELA = 0x1000


def desmonta(ini, fim):
    env = dict(os.environ, CYGWIN="nodosfilewarning")
    out = subprocess.run(
        [OBJDUMP, "-D", "-b", "binary", "-m", "powerpc:common", "-EB",
         "--adjust-vma=0x82000000",
         "--start-address=0x%X" % ini, "--stop-address=0x%X" % fim, BASE],
        capture_output=True, text=True, env=env).stdout
    d = {}
    for l in out.split("\n"):
        m = LINE.match(l)
        if m:
            d[int(m.group(1), 16)] = (m.group(3), m.group(4))
    return d


def eh_adjustor_thunk(ins, addr):
    """'addi rX,rX,N' seguido de 'b alvo' = thunk de ajuste de this (heranca
    multipla do C++). Exatamente 8 bytes, forma inequivoca. Precisa de caso
    proprio porque o 'b' e' tail call, e o percurso generico o confundia com
    salto interno -- entrava na funcao de destino e o corpo saia esparso."""
    a0 = ins.get(addr)
    a1 = ins.get(addr + 4)
    if not a0 or not a1:
        return False
    return a0[0] == "addi" and a1[0] in ("b", "ba")


def acha_fim(addr):
    ins = desmonta(addr, addr + JANELA)
    if addr not in ins:
        return None, "nao consegui desmontar 0x%08X" % addr

    if eh_adjustor_thunk(ins, addr):
        return addr + 8, "adjustor thunk (addi + b), 8 bytes"

    visto = set()
    fila = [addr]
    while fila:
        a = fila.pop()
        if a in visto or a not in ins:
            continue
        mnem, ops = ins[a]
        if mnem.startswith("."):          # dado / padding encerra o caminho
            continue
        visto.add(a)

        if mnem in ("blr", "bctr", "rfi"):
            continue                      # retorno / salto indireto: fecha
        t = ALVO.search(ops)
        tgt = int(t.group(1), 16) if t else None

        if mnem in ("b", "ba"):
            # salto interno so se cair dentro da janela e adiante do inicio
            if tgt is not None and addr <= tgt < addr + JANELA:
                fila.append(tgt)
            continue                      # senao e' tail call: fecha
        if mnem.startswith("b") and not mnem.endswith("lr") and tgt is not None:
            fila.append(tgt)              # condicional: alvo
            fila.append(a + 4)            # e queda
            continue
        fila.append(a + 4)                # bl / bXlr / instrucao comum

    if not visto:
        return None, "grafo vazio"
    fim = max(visto) + 4
    size = fim - addr
    # corpo precisa ser razoavelmente contiguo; buraco grande = leitura errada
    if len(visto) * 4 < size * 0.7:
        return None, "corpo esparso (%d instr em 0x%X bytes)" % (len(visto), size)
    return fim, "grafo fechou com %d instrucoes" % len(visto)


def main():
    if len(sys.argv) < 2:
        raise SystemExit("uso: python work\\add_missing.py 0x83008868 [--so-mostrar]")
    addr = int(sys.argv[1], 16)
    so_mostrar = "--so-mostrar" in sys.argv

    man = io.open(MAN, encoding="utf-8").read()
    if re.search(r"^0x%08X\s*=" % addr, man, re.M):
        print("0x%08X ja esta no manifesto" % addr)
        return 1

    fim, motivo = acha_fim(addr)
    if fim is None:
        print("FALHOU: %s  -- confira a mao" % motivo)
        return 1
    size = fim - addr
    ins = desmonta(addr, addr + 8)
    prim = ins.get(addr, ("?", ""))[0]

    print("0x%08X = { size = 0x%X }   inicia com '%s'  (%s)" % (addr, size, prim, motivo))
    if so_mostrar:
        return 0

    with io.open(MAN, "a", encoding="utf-8") as f:
        f.write("\n# Funcao alcancada so indiretamente (inicia com '%s', sem prologo).\n"
                "# Fim em 0x%08X -- %s.\n" % (prim, fim, motivo))
        f.write("0x%08X = { size = 0x%X }\n" % (addr, size))
    total = len(re.findall(r"^0x[0-9A-Fa-f]{8}\s*=",
                           io.open(MAN, encoding="utf-8").read(), re.M))
    print("acrescentada. manifesto: %d entradas" % total)
    return 0


sys.exit(main())
