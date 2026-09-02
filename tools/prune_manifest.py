
import os as _os

# Raiz do projeto. Por padrao e a pasta que contem este script (tools/..),
# entao o script funciona em qualquer clone. UFC3_ROOT sobrescreve.
PROJ = _os.environ.get("UFC3_ROOT") or _os.path.dirname(
    _os.path.dirname(_os.path.abspath(__file__)))

"""
Filtra o manifesto removendo candidatos que provavelmente sao BLOCOS de uma
funcao maior, e nao funcoes independentes.

Regra: uma funcao de verdade termina e o que vem logo depois e' um destes:
  - padding (.long 0)
  - o prologo da proxima funcao (mflr)
  - o inicio de uma funcao que o rexglue ja conhece
  - o fim da regiao analisada

Se logo depois do fim do candidato vem codigo comum (li, lwz, addi...), entao
o candidato e' quase certamente uma entrada no meio de uma funcao maior --
declarar ele como funcao trunca a original e produz
"[FATAL] Unresolved branch from X to Y" em runtime.

Foi exatamente o caso de 0x82748B74 (+0x28): bloco de um seletor cujo corpo
continua ate 0x82748C1C.
"""
import io, re, json

# PROJ definido no cabecalho
MAN = PROJ + r"\ufc3_manifest.toml"
DIS = PROJ + r"\work\code.dis"
PART = PROJ + r"\generated\default\codegen.partition.json"

LINE = re.compile(r"^([0-9a-f]{8}):\t([0-9a-f ]+)\t(\S+)")

print("lendo desmontagem...")
mnem = {}
with io.open(DIS, encoding="utf-8", errors="replace") as f:
    for line in f:
        m = LINE.match(line)
        if m:
            mnem[int(m.group(1), 16)] = m.group(3)
print("  %d instrucoes" % len(mnem))

starts = set(int(k, 16) for k in json.load(io.open(PART, encoding="utf-8"))["assignments"].keys())
print("  %d funcoes conhecidas pelo rexglue" % len(starts))

verificadas = {0x82691B80, 0x82F451D0, 0x8232F760, 0x82314918, 0x831820A8, 0x82B4E120}

linhas = io.open(MAN, encoding="utf-8").read().split("\n")
manter, remover = [], []
for i, l in enumerate(linhas):
    m = re.match(r"^0x([0-9A-Fa-f]{8}) = \{ size = (0x[0-9A-Fa-f]+) \}", l)
    if not m:
        manter.append(i)
        continue
    a = int(m.group(1), 16)
    fim = a + int(m.group(2), 16)
    if a in verificadas:
        manter.append(i)
        continue
    depois = mnem.get(fim)
    ok = (depois is None                 # fim da regiao
          or depois.startswith(".")      # padding / dado
          or depois == "mflr"            # prologo da proxima
          or fim in starts)              # funcao ja conhecida comeca ali
    (manter if ok else remover).append(i)

print("\nmantidos: %d | removidos: %d" % (
    len([i for i in manter if re.match(r"^0x", linhas[i])]), len(remover)))

# amostra do que sai, para conferencia
print("\namostra de removidos:")
for i in remover[:8]:
    m = re.match(r"^0x([0-9A-Fa-f]{8}) = \{ size = (0x[0-9A-Fa-f]+) \}", linhas[i])
    a = int(m.group(1), 16); fim = a + int(m.group(2), 16)
    print("  0x%08X +%s  -> depois vem '%s'" % (a, m.group(2), mnem.get(fim, "?")))

novas = [linhas[i] for i in sorted(manter)]
io.open(MAN, "w", encoding="utf-8").write("\n".join(novas))
total = len(re.findall(r"^0x[0-9A-Fa-f]{8}\s*=", "\n".join(novas), re.M))
print("\ntotal no manifesto agora: %d" % total)
