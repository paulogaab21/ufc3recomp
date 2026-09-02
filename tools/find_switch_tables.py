
import os as _os

# Raiz do projeto. Por padrao e a pasta que contem este script (tools/..),
# entao o script funciona em qualquer clone. UFC3_ROOT sobrescreve.
PROJ = _os.environ.get("UFC3_ROOT") or _os.path.dirname(
    _os.path.dirname(_os.path.abspath(__file__)))

"""
Varre o binario procurando jump tables (switch do C) e extrai os destinos.

Padrao classico gerado pelo compilador do Xbox 360:

    cmplwi cr6,rN,<limite>      <- quantos casos (limite = ultimo indice)
    bgt    cr6,<default>        <- fora da faixa vai pro default
    ...
    lis    rT,<hi>              \
    rlwinm rI,rN,2,0,29          |  endereco = base + indice*4
    addi   rT,rT,<lo>            |
    lwzx   rD,rT,rI             /   le o destino DA TABELA (que esta em dados)
    mtctr  rD
    bctr                        <- salta

O rexglue nao consegue seguir isso: os destinos estao em dados, nao em codigo.
Sem [[switch_tables]] ele encerra a funcao no bctr e o switch vira trap.

Aqui a extracao e' exata, nao estimada:
  - a base da tabela vem de lis+addi (dois imediatos, aritmetica fechada)
  - a contagem vem do cmplwi anterior
  - os destinos sao lidos do proprio binario, 4 bytes big-endian cada
  - cada destino e' conferido: precisa cair dentro da faixa de codigo

Escreve work/switch_tables.toml. NAO aplica sozinho -- confira antes.
"""
import io, re, struct, sys

# PROJ definido no cabecalho
DIS = PROJ + r"\work\code.dis"
BASE = PROJ + r"\work\base.bin"
OUT = PROJ + r"\work\switch_tables.toml"

IMG_BASE = 0x82000000
CODE_INI = 0x82220000
CODE_FIM = 0x8325CDB0

LINE = re.compile(r"^([0-9a-f]{8}):\t([0-9a-f ]+)\t(\S+)\s*(.*)$")

print("lendo desmontagem...")
ordem = []
mn = {}
with io.open(DIS, encoding="utf-8", errors="replace") as f:
    for l in f:
        m = LINE.match(l)
        if m:
            a = int(m.group(1), 16)
            ordem.append(a)
            mn[a] = (m.group(3), m.group(4))
print("  %d instrucoes" % len(ordem))

img = open(BASE, "rb").read()


def le_u32(addr):
    off = addr - IMG_BASE
    if off < 0 or off + 4 > len(img):
        return None
    return struct.unpack(">I", img[off:off + 4])[0]


def imm(txt):
    """ultimo operando como inteiro com sinal"""
    p = txt.split(",")[-1].strip()
    try:
        return int(p, 0)
    except ValueError:
        return None


achados = []
ordem.sort()
for i, a in enumerate(ordem):
    if mn[a][0] != "bctr":
        continue
    # olha para tras uma janela curta procurando o calculo do endereco
    lis_val = addi_val = None
    reg_idx = None
    limite = None
    j = i - 1
    passos = 0
    while j >= 0 and passos < 14:
        m2, o2 = mn[ordem[j]]
        if m2 == "lwzx" and lis_val is None:
            pass
        elif m2 == "addi" and addi_val is None:
            addi_val = imm(o2)
        elif m2 == "lis" and lis_val is None:
            lis_val = imm(o2)
        elif m2 == "rlwinm" and reg_idx is None and ",2,0,29" in o2.replace(" ", ""):
            # 'rlwinm rDest,rIndice,2,0,29' = indice*4. O SDK quer o numero do
            # registrador de INDICE (o segundo operando), nao o de destino.
            src = o2.split(",")[1].strip()
            mreg = re.match(r"^r(\d+)$", src)
            if mreg:
                reg_idx = int(mreg.group(1))
        elif m2 in ("cmplwi", "cmpwi") and limite is None:
            limite = imm(o2)
        j -= 1
        passos += 1

    if lis_val is None or addi_val is None or limite is None or reg_idx is None:
        continue
    tabela = ((lis_val & 0xFFFF) << 16) + addi_val
    tabela &= 0xFFFFFFFF
    if not (CODE_INI <= tabela <= CODE_FIM + 0x100000):
        continue
    n = limite + 1
    if n < 2 or n > 512:
        continue

    destinos = []
    ok = True
    for k in range(n):
        d = le_u32(tabela + k * 4)
        if d is None or not (CODE_INI <= d < CODE_FIM):
            ok = False
            break
        destinos.append(d)
    if not ok or not destinos:
        continue
    achados.append((a, tabela, destinos, reg_idx))

print("JUMP TABLES com destinos validos: %d" % len(achados))
with io.open(OUT, "w", encoding="utf-8") as f:
    f.write("# Jump tables extraidas por work/find_switch_tables.py.\n")
    f.write("# Base e contagem vem do proprio codigo; destinos lidos do binario\n")
    f.write("# e conferidos contra a faixa de codigo. Sem estimativa.\n")
    for bctr, tab, dest, reg in achados:
        f.write("\n[[switch_tables]]\n")
        f.write("# bctr em 0x%08X, tabela em 0x%08X, %d casos\n" % (bctr, tab, len(dest)))
        f.write("address = 0x%08X\n" % bctr)
        f.write("register = %d\n" % reg)
        f.write("labels = [\n")
        for d in dest:
            f.write("    0x%08X,\n" % d)
        f.write("]\n")
print("escrito: %s" % OUT)
for bctr, tab, dest, reg in achados[:8]:
    print("  bctr 0x%08X  tabela 0x%08X  r%-2d  %d casos"
          % (bctr, tab, reg, len(dest)))
if len(achados) > 8:
    print("  ... e mais %d" % (len(achados) - 8))
