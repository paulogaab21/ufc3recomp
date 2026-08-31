# Ferramentas de análise

O trabalho difícil desta recompilação não é gráfico nem de áudio: é descobrir
onde cada função começa e termina dentro do executável do console. Estas são as
ferramentas que usei para isso.

Nenhuma delas é mágica. Todas partem do executável desmontado e do que o
recompilador já sabe, e devolvem **candidatos** — a decisão final sempre foi
minha, uma por uma. Foi justamente tentar automatizar essa decisão que deu
errado: uma vez gerei 4.751 entradas automáticas no manifesto e colhi 2.919
falhas latentes, enquanto 8 entradas conferidas à mão davam zero avisos.

## Configuração

Nada aqui tem caminho fixo. Três variáveis de ambiente controlam tudo:

| Variável | O que é | Padrão |
|---|---|---|
| `UFC3_ROOT` | raiz do projeto | a pasta que contém `tools/` |
| `UFC3_GAME` | pasta do disco extraído | — obrigatória para rodar o jogo |
| `REXSDK_SRC` | fonte do ReXGlue SDK | — necessária para o objdump de PowerPC |

```powershell
$env:UFC3_GAME  = "D:\UFC3_extraido"
$env:REXSDK_SRC = "C:\dev\rexglue-sdk"
```

Alguns scripts esperam encontrar em `work/` o desmontado (`code.dis`) e o
basefile (`base.bin`), que saem do seu próprio disco e por isso não estão neste
repositório.

## Os três tipos de erro, e por que importam

Antes das ferramentas, o mapa. O runtime falha de três jeitos, e **dois deles
pedem correções opostas**:

| Mensagem | O que significa | O que fazer |
|---|---|---|
| `Call to invalid or unregistered function at 0xA` | a função em `0xA` não existe | **acrescentar** ao manifesto |
| `Unresolved branch from 0xA to 0xB` | a função que contém `0xA` foi cortada cedo demais | **remover** entradas na região |
| `Unresolved call from 0xA to 0xB` | idem | idem |

Distinguir os dois é olhar se existe entrada no manifesto naquela região.
Confundir os dois é gastar horas indo na direção errada — foi o que me aconteceu.

## Encontrar funções que faltam

**`add_missing.py`** — dado o endereço de um crash, calcula o fim da função
percorrendo o grafo de fluxo e acrescenta ao manifesto. Reconhece adjustor thunk
(`addi rX,rX,N` seguido de `b alvo`, 8 bytes) e recusa corpos esparsos, em que o
caminho percorrido cobre menos de 70% do intervalo — sinal de que o fim foi
estimado errado.

**`varre_regiao.py`** — varre um intervalo aplicando os quatro testes que uma
função candidata precisa passar: não ser conhecida, não ser alvo de nenhum
branch direto, vir depois de um terminador, e ter corpo contíguo que fecha.
Com `--aplicar`, grava no manifesto.

**`find_orphans.py`** e **`find_orphans2.py`** — detectores em massa. Desmontam
os 17 MB de código, cruzam com o que o recompilador conhece e procuram código
válido que ninguém chama diretamente. O primeiro para no primeiro terminador,
o que gera falso positivo em bloco de `switch`; o segundo segue o fluxo de
controle. **Trate a saída como lista de candidatos, nunca como verdade.**

**`acha_thunks.py`** — procura adjustor thunks de herança múltipla. Exige
`addi r3,r3,N` (só o ponteiro `this`) e que o endereço não seja alvo de branch.
Esse segundo filtro é essencial: sem ele eu peguei 16 blocos internos de funções
existentes, e o link quebrou com `use of undeclared label`.

**`acha_despachantes.py`** — procura despachantes de vtable frameless: o padrão
`lwz r,0(r3)` / `lwz r,N(r)` / `mtctr` / `bctr` em 16 bytes, com o mesmo filtro
de alvo de branch. De 41 candidatos, só 1 sobreviveu ao filtro.

**`acha_switch.py`** — extrai jump tables. Acha o registrador de índice pelo
`rlwinm rD,rIndex,2,0,29` e lê os rótulos em big-endian do `base.bin`,
validando que todos caem na faixa de código. Extraiu 1.067 tabelas, **ainda não
aplicadas ao manifesto**.

**`filtra_manifesto.py`** — poda o manifesto, removendo entradas que não se
sustentam.

## Ciclos automáticos

**`crash-loop.ps1`** — roda o jogo, classifica a falha nos tipos acima,
desmonta a região com origem e destino marcados. Com `-Corrigir`, remove
sozinho no caso de função cortada.

**`loop-faltantes.ps1`** — ciclo fechado para o caso *função faltando*: roda,
pega o endereço, calcula o fim, acrescenta, regera, recompila, repete. Só age
nesse caso; qualquer outro erro ele para e devolve para análise humana.
Acrescentar função faltante é a direção segura — com o tamanho certo, não
trunca nada. Remover ou redimensionar mexe em código que já funciona, e isso
não vai para o automático.

**`auto-loop.ps1`** — o espelho: só age no caso *função cortada*, que tem
correção mecânica.

## Medição

**`medir.ps1`** — mede CPU, GPU, threads e taxa de resolve ao mesmo tempo, que é
o que separa as hipóteses:

```
GPU alta + CPU baixa      -> gargalo na GPU
GPU baixa + 1 thread 100% -> gargalo no código recompilado
Textura/s alto            -> cache pequeno, recarga constante
Resolve/s < 60            -> a simulação não acompanha os vblanks
```

A última linha é a mais importante e a menos óbvia: neste motor o jogo avança
um passo de simulação por vblank, então saturar a GPU **não derruba os fps —
derruba a velocidade do jogo**.
