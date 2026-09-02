
import os as _os

# Raiz do projeto. Por padrao e a pasta que contem este script (tools/..),
# entao o script funciona em qualquer clone. UFC3_ROOT sobrescreve.
PROJ = _os.environ.get("UFC3_ROOT") or _os.path.dirname(
    _os.path.dirname(_os.path.abspath(__file__)))

"""
Varre o binario procurando DESPACHANTES DE VTABLE e escreve os que o rexglue
nao conhece.

    lwz   rA,0(r3)      <- carrega o ponteiro da vtable do objeto (this = r3)
    lwz   rB,N(rA)      <- pega o metodo no deslocamento N
    mtctr rB
    bctr                <- salta

Exatos 16 bytes, quatro instrucoes, forma fechada. Sao gerados um por metodo
virtual e alcancados so por ponteiro -- nenhum 'bl' aponta para eles, nao tem
prologo, e nao estao no .pdata. Escapam do scanner do rexglue pelos mesmos
motivos dos adjustor thunks.

Foram a causa de varios crashes de hoje: 0x8300EC08 (menu), 0x822AE5C8 (inicio
da luta), entre outros. Cada tela nova instancia classes novas, cada classe traz
sua vtable, e cada metodo virtual chamado passa por um destes.

Por que a varredura em massa e' segura aqui:
  - tamanho FIXO (16 bytes), nada a estimar
  - termina em bctr, entao nao trunca funcao nenhuma
  - a forma de 4 instrucoes nao ocorre por acaso no meio de um corpo
  - exige que o anterior feche (terminador ou padding)

Escreve work/despachantes.toml.
"""
import io, re, json

# PROJ definido no cabecalho
DIS = PROJ + r"\work\code.dis"
PART = PROJ + r"\generated\default\codegen.partition.json"
MAN = PROJ + r"\ufc3_manifest.toml"
OUT = PROJ + r"\work\despachantes.toml"

LINE = re.compile(r"^([0-9a-f]{8}):\t([0-9a-f ]+)\t(\S+)\s*(.*)$")

print("lendo desmontagem...")
mn = {}
with io.open(DIS, encoding="utf-8", errors="replace") as f:
    for l in f:
        m = LINE.match(l)
        if m:
            mn[int(m.group(1), 16)] = (m.group(3), m.group(4))
print("  %d instrucoes" % len(mn))

starts = set(int(k, 16) for k in json.load(io.open(PART, encoding="utf-8"))["assignments"].keys())
man = io.open(MAN, encoding="utf-8").read()
no_man = set(int(a, 16) for a in re.findall(r"^0x([0-9A-Fa-f]{8})\s*=", man, re.M))
print("  %d funcoes conhecidas | %d no manifesto" % (len(starts), len(no_man)))

# Alvos de branch direto: se alguem salta para o endereco, ele e' um BLOCO de
# outra funcao, nao uma funcao propria. Sem esta checagem o padrao de 4
# instrucoes tambem casa com blocos internos, e declara-los produz
# "use of undeclared label" na compilacao -- a funcao original perde o rotulo.
ALVO = re.compile(r"0x([0-9a-f]{8})")
alvos = set()
for _a, (_m, _o) in mn.items():
    if _m.startswith("b") and _m not in ("blr", "bctr", "rfi"):
        _t = ALVO.search(_o)
        if _t:
            alvos.add(int(_t.group(1), 16))
print("  %d alvos de branch direto" % len(alvos))

FECHA = {"blr", "bctr", "rfi", "b", "ba"}
# lwz rA,0(r3)  -- o 'this' e' sempre r3 no ABI
LWZ_VT = re.compile(r"^r(\d+),0\(r3\)$")
# lwz rB,N(rA)  -- deslocamento do metodo na vtable
LWZ_MET = re.compile(r"^r(\d+),(\d+)\(r(\d+)\)$")

achados = []
for a in sorted(mn):
    if a in starts or a in no_man or a in alvos:
        continue
    i0, i1, i2, i3 = (mn.get(a), mn.get(a + 4), mn.get(a + 8), mn.get(a + 12))
    if not all((i0, i1, i2, i3)):
        continue
    if i0[0] != "lwz" or i1[0] != "lwz" or i2[0] != "mtctr" or i3[0] != "bctr":
        continue
    m0 = LWZ_VT.match(i0[1].replace(" ", ""))
    if not m0:
        continue
    rA = m0.group(1)
    m1 = LWZ_MET.match(i1[1].replace(" ", ""))
    if not m1 or m1.group(3) != rA:
        continue                      # o segundo lwz precisa usar o registrador do primeiro
    rB = m1.group(1)
    if mn[a + 8][1].replace(" ", "") != "r" + rB:
        continue                      # mtctr precisa usar o que foi carregado
    ant = mn.get(a - 4)
    if not ant or not (ant[0] in FECHA or ant[0].startswith(".")):
        continue                      # precisa vir depois de algo que fecha
    achados.append((a, int(m1.group(2))))

print("DESPACHANTES DE VTABLE nao registrados: %d" % len(achados))
with io.open(OUT, "w", encoding="utf-8") as f:
    f.write("# Despachantes de vtable (lwz/lwz/mtctr/bctr), 16 bytes fixos.\n")
    f.write("# Gerado por work/find_dispatchers.py -- tamanho fixo, sem estimativa.\n")
    for a, off in achados:
        f.write("0x%08X = { size = 0x10 }\n" % a)
print("escrito: %s" % OUT)
for a, off in achados[:10]:
    print("  0x%08X  metodo no deslocamento %d" % (a, off))
if len(achados) > 10:
    print("  ... e mais %d" % (len(achados) - 10))
