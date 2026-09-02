
import os as _os

# Raiz do projeto. Por padrao e a pasta que contem este script (tools/..),
# entao o script funciona em qualquer clone. UFC3_ROOT sobrescreve.
PROJ = _os.environ.get("UFC3_ROOT") or _os.path.dirname(
    _os.path.dirname(_os.path.abspath(__file__)))

"""
Varre uma FAIXA de enderecos procurando funcoes que o rexglue nao conhece,
aplicando as mesmas verificacoes que faco uma a uma:

  1. nao e' funcao ja conhecida nem esta no manifesto
  2. NAO e' alvo de branch direto  (se alguem salta pra la, e' bloco interno)
  3. o anterior fecha um corpo (terminador ou padding)
  4. o grafo de fluxo fecha, com corpo contiguo

Serve para tabelas de despachantes/encaminhadores, onde os enderecos ficam
lado a lado e um crash so revela um deles por vez. Em vez de esperar N
execucoes, cobre a fileira inteira -- mas com a mesma barra de evidencia,
nao por atacado.

Uso:  python work\\sweep_region.py 0x8258A200 0x8258A400
      python work\\sweep_region.py 0x8258A200 0x8258A400 --apply
"""
import io, re, sys, json, subprocess, os

# PROJ definido no cabecalho
DIS = PROJ + r"\work\code.dis"
PART = PROJ + r"\generated\default\codegen.partition.json"
MAN = PROJ + r"\ufc3_manifest.toml"

LINE = re.compile(r"^([0-9a-f]{8}):\t([0-9a-f ]+)\t(\S+)\s*(.*)$")
ALVO = re.compile(r"0x([0-9a-f]{8})")
FECHA = {"blr", "bctr", "rfi", "b", "ba"}


def main():
    if len(sys.argv) < 3:
        raise SystemExit("uso: python work\\sweep_region.py 0xINI 0xFIM [--apply]")
    ini, fim = int(sys.argv[1], 16), int(sys.argv[2], 16)
    apply_it = "--apply" in sys.argv

    mn = {}
    alvos = set()
    with io.open(DIS, encoding="utf-8", errors="replace") as f:
        for l in f:
            m = LINE.match(l)
            if not m:
                continue
            a = int(m.group(1), 16)
            mn[a] = (m.group(3), m.group(4))
            if m.group(3).startswith("b") and m.group(3) not in ("blr", "bctr", "rfi"):
                t = ALVO.search(m.group(4))
                if t:
                    alvos.add(int(t.group(1), 16))

    starts = set(int(k, 16) for k in json.load(io.open(PART, encoding="utf-8"))["assignments"].keys())
    man = io.open(MAN, encoding="utf-8").read()
    no_man = set(int(a, 16) for a in re.findall(r"^0x([0-9A-Fa-f]{8})\s*=", man, re.M))

    achados = []
    a = ini
    while a < fim:
        if a not in mn:
            a += 4
            continue
        if a in starts or a in no_man or a in alvos:
            a += 4
            continue
        ant = mn.get(a - 4)
        if not ant or not (ant[0] in FECHA or ant[0].startswith(".")):
            a += 4
            continue
        r = subprocess.run([sys.executable, PROJ + r"\work\add_missing.py",
                            "0x%08X" % a, "--so-mostrar"],
                           capture_output=True, text=True)
        saida = r.stdout.strip()
        if r.returncode == 0 and saida.startswith("0x"):
            m = re.search(r"size = (0x[0-9A-Fa-f]+)", saida)
            if m:
                achados.append((a, int(m.group(1), 16), saida))
        a += 4

    print("CANDIDATOS na faixa 0x%08X-0x%08X: %d\n" % (ini, fim, len(achados)))
    for addr, size, s in achados:
        print("  " + s)

    if not apply_it:
        print("\n(rode com --apply para acrescentar ao manifesto)")
        return

    with io.open(MAN, "a", encoding="utf-8") as f:
        f.write("\n# --- Fileira de encaminhadores/despachantes em 0x%08X-0x%08X ---\n"
                "# Cada um repassa para um slot diferente da vtable. Verificados:\n"
                "# nao sao alvo de branch, vem depois de terminador, grafo fecha contiguo.\n"
                % (ini, fim))
        for addr, size, s in achados:
            f.write("0x%08X = { size = 0x%X }\n" % (addr, size))
    total = len(re.findall(r"^0x[0-9A-Fa-f]{8}\s*=",
                           io.open(MAN, encoding="utf-8").read(), re.M))
    print("\nacrescentados: %d | manifesto: %d entradas" % (len(achados), total))


main()
