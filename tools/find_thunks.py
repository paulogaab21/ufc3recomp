
import os as _os

# Raiz do projeto. Por padrao e a pasta que contem este script (tools/..),
# entao o script funciona em qualquer clone. UFC3_ROOT sobrescreve.
PROJ = _os.environ.get("UFC3_ROOT") or _os.path.dirname(
    _os.path.dirname(_os.path.abspath(__file__)))

"""
Varre o binario inteiro procurando adjustor thunks e escreve os que o rexglue
nao conhece.

    addi rX,rX,N
    b    alvo

Sao os thunks de ajuste do ponteiro 'this' que o C++ gera com heranca multipla.
Oito bytes exatos, forma fechada, alcancados so por vtable -- nenhum 'bl' aponta
para eles, e nao tem prologo, entao escapam do scanner do rexglue e do .pdata.

Por que essa varredura em massa e' segura, ao contrario da que fiz antes:
  - o padrao e' de tamanho FIXO (8 bytes), nao ha o que estimar
  - termina em branch incondicional, entao nao trunca funcao nenhuma
  - nao pode ser bloco de switch: bloco de case nao tem essa forma de duas
    instrucoes fechando em 'b'
  - so entra se o endereco anterior tambem fechar (terminador ou outro thunk),
    garantindo que nao estamos no meio de um corpo

Escreve em work/thunks.toml. Confira antes de juntar ao manifesto.
"""
import io, re, json, bisect

# PROJ definido no cabecalho
DIS = PROJ + r"\work\code.dis"
PART = PROJ + r"\generated\default\codegen.partition.json"
MAN = PROJ + r"\ufc3_manifest.toml"
OUT = PROJ + r"\work\thunks.toml"

LINE = re.compile(r"^([0-9a-f]{8}):\t([0-9a-f ]+)\t(\S+)\s*(.*)$")

print("lendo desmontagem...")
mn = {}
with io.open(DIS, encoding="utf-8", errors="replace") as f:
    for line in f:
        m = LINE.match(line)
        if m:
            mn[int(m.group(1), 16)] = (m.group(3), m.group(4))
print("  %d instrucoes" % len(mn))

starts = set(int(k, 16) for k in json.load(io.open(PART, encoding="utf-8"))["assignments"].keys())
print("  %d funcoes ja conhecidas pelo rexglue" % len(starts))

man = io.open(MAN, encoding="utf-8").read()
no_manifesto = set(int(a, 16) for a in re.findall(r"^0x([0-9A-Fa-f]{8})\s*=", man, re.M))

FECHA = {"blr", "bctr", "rfi", "b", "ba"}
achados = []
for a in sorted(mn):
    if a in starts or a in no_manifesto:
        continue
    m0 = mn.get(a)
    m1 = mn.get(a + 4)
    if not m0 or not m1:
        continue
    if m0[0] != "addi" or m1[0] not in ("b", "ba"):
        continue
    # Precisa ser ajuste do 'this' (r3), origem E destino. Sem isso o padrao
    # pega epilogo de funcao ('addi r1,r1,N ; b <helper de restore>') e blocos
    # que calculam endereco a partir de r1/r31 no meio de um corpo.
    if not re.match(r"^r3,r3,-?\d+$", m0[1].replace(" ", "")):
        continue
    # o que vem antes precisa fechar um corpo -- senao estamos no meio de um
    ant = mn.get(a - 4)
    if not ant:
        continue
    if not (ant[0] in FECHA or ant[0].startswith(".")):
        continue
    achados.append(a)

print("ADJUSTOR THUNKS nao registrados: %d" % len(achados))
with io.open(OUT, "w", encoding="utf-8") as f:
    f.write("# Adjustor thunks (addi + b, 8 bytes) que o rexglue nao descobriu.\n")
    f.write("# Gerado por work/find_thunks.py -- tamanho fixo, sem estimativa.\n")
    for a in achados:
        f.write("0x%08X = { size = 0x8 }\n" % a)
print("escrito: %s" % OUT)
for a in achados[:10]:
    print("  0x%08X  %s %s / %s %s" % (a, mn[a][0], mn[a][1], mn[a + 4][0], mn[a + 4][1]))
if len(achados) > 10:
    print("  ... e mais %d" % (len(achados) - 10))
