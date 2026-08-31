# UFC 3 Recomp

Recompilação  de **UFC Undisputed 3** (Xbox 360) para PC, usando o
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

Não é emulação. O executável do jogo é traduzido de PowerPC para C++ e depois
compilado como um programa nativo de PC — o resultado é um `.exe` que roda
direto no processador, sem interpretar nada em tempo real.

---

## Gameplay — 2K / 144 FPS

**UFC Undisputed 3 já está jogável no PC em 2K a 144 FPS por meio desta recompilação.**

[![UFC Undisputed 3 — jogável em 2K a 144 FPS](https://i.ytimg.com/vi/hUpWKWqRkJ8/maxresdefault.jpg)](https://www.youtube.com/watch?v=5cQYiwqAFsc)

*Imagem de UFC Undisputed 3 — clique para assistir ao vídeo do projeto.*

[▶ Assista ao gameplay no YouTube — 2K a 144 FPS](https://www.youtube.com/watch?v=5cQYiwqAFsc)

A próxima meta do projeto é atingir **2K a 244 FPS**, mantendo estabilidade e
jogabilidade.

---

## Em que pé está

**O jogo dá boot, renderiza e já está jogável em 2K a 144 FPS.**

O que já funciona:

- O `default.xex` é traduzido inteiro para C++ — 569 arquivos, 295 MB de código
- Isso compila e linka num executável nativo
- O executável sobe o runtime completo: memória do guest, SDL, entrada, áudio
  (com as threads do decodificador XMA), sistema de arquivos virtual
- Monta o disco do jogo e registra os caminhos `game:` e `d:` como o console faria
- Registra **66.157 funções recompiladas** na tabela de funções
- Carrega a imagem do XEX e resolve os imports do kernel do Xbox 360 —
  101 do `xam`, 184 do `xboxkrnl`
- Chega ao gameplay com imagem, áudio e entrada funcionando
- Roda em 2K a 144 FPS no vídeo publicado

O trabalho agora está concentrado em aumentar a estabilidade, cobrir caminhos
menos comuns e melhorar o desempenho.

---

## Como funciona

```
default.xex  (PowerPC, big-endian, Xbox 360)
     |
     |  rexglue codegen  — desmonta e traduz instrução por instrução
     v
generated/default/*.cpp  (C++, ~295 MB)
     |
     |  clang + o runtime do ReXGlue
     v
ufc3.exe  (x86-64 nativo)
```

O ReXGlue entra com as duas metades: o **compilador**, que faz a tradução, e o
**runtime**, que é um Xbox 360 reimplementado em C++ — kernel, GPU Xenos
traduzida para D3D12, áudio, entrada, sistema de arquivos.

O trabalho deste repositório é o que fica no meio: dizer ao recompilador onde
cada função começa e termina, corrigir o que ele não descobre sozinho, e
implementar o que o jogo espera do console e o runtime ainda não oferece.

---

## O problema principal: achar os limites das funções

O recompilador descobre funções de dois jeitos: seguindo as chamadas (`bl`) a
partir do ponto de entrada, e reconhecendo o prólogo — aquelas instruções que
toda função normal executa ao entrar, salvando o endereço de retorno e
reservando espaço na pilha.

Existe uma classe de função que escapa dos dois:

- **não tem prólogo** — é tão pequena que o compilador de 2011 não gerou um
- **não é alvo de nenhum `bl`** — só é chamada indiretamente, por tabela de
  construtores estáticos, vtable ou ponteiro de função

E, pelo mesmo motivo (não mexe na pilha), ela também **não aparece na seção
`.pdata`** do executável, que é o diretório de exception unwind. Confirmado na
prática: das 47.145 funções que o `.pdata` lista, nenhuma cobre esses casos.

Quatro delas travavam a geração do código e foram corrigidas à mão:

| Endereço | O que é | Tamanho |
|---|---|---|
| `0x82691B80` | função-folha cortada num tail-call | `0x78` |
| `0x82F451D0` | thunk de vtable, alcançado só por `bctr` | `0x48` |
| `0x8232F760` | adjustor thunk de herança múltipla | `0x8` |
| `0x82314918` | getter-folha | `0x2C` |

Uma quinta (`0x831820A8`, um inicializador estático) só apareceu em runtime.

Resolver isso caso a caso não escala. Existe um detector em
`work/find_orphans.py` que desmonta os 17 MB de código, cruza com as funções
que o recompilador já conhece e procura código válido que não é alvo de nenhum
branch direto. Ele acha o caso confirmado com o tamanho exato — mas a detecção
de **fim** de função ainda é ingênua (para no primeiro terminador, e função
real tem vários blocos internos), o que gera falso positivo em bloco de
`switch`. Melhorar isso para seguir o fluxo de controle de verdade é
provavelmente a contribuição mais valiosa que alguém pode fazer aqui.

---

## Você precisa do seu próprio disco

**Nem o repositório nem a release contêm arquivos do disco de UFC Undisputed 3.**

O executável da release é o código do jogo traduzido de PowerPC para C++ e
compilado como programa nativo. Ele não carrega nada de dentro de si: todo o
conteúdo continua no seu disco — texturas, áudio, vídeo, modelos e os dados dos
lutadores. São 6.451 MB de dados contra 93 MB de executável; o código é 1,4% do
jogo.

Sem o disco o jogo nem abre: ele para com `Entrypoint XEX not found`.

O launcher pede a ISO do seu disco, ou uma pasta já extraída, e confere pelo
cabeçalho do XEX que é mesmo UFC Undisputed 3 (title ID `5451087D`) antes de
seguir.

Para compilar por conta própria você precisa extrair, do seu próprio disco:

```
assets/game/default.xex
```

Sem esse arquivo não há o que recompilar.

---

## Compilando

Precisa de: Clang 20+, CMake 3.25+, Ninja, e no Windows os Build Tools da MSVC
(o Clang usa os headers e libs deles). Mais o ReXGlue SDK.

```bash
# 1. traduz o XEX para C++
rexglue codegen ufc3_manifest.toml

# 2. configura
cmake --preset win-amd64-relwithdebinfo -DREXSDK_DIR=<caminho do rexglue-sdk>

# 3. compila
cmake --build out/build/win-amd64-relwithdebinfo

# 4. roda (apontando para a pasta do seu disco extraído)
./out/build/win-amd64-relwithdebinfo/ufc3.exe --game_data_root "<pasta do jogo>"
```

Para investigar um crash, `--log_file <caminho> --log_verbose` mostra o boot
inteiro passo a passo.

---

## Achados que voltaram para o SDK

Montar isso do zero no Windows revelou três bugs no ReXGlue SDK, todos da mesma
família — **consumir o SDK pelo código-fonte, do jeito que o próprio
`rexglue init` documenta, estava quebrado**:

1. **`MSPACK_DIR` apontando para uma árvore de symlinks.** No Windows o git
   materializa symlink como arquivo de texto de 29 bytes por padrão, e o clang
   tenta compilá-lo como código C. Quebra em qualquer clone Windows.

2. **Presets gerados sem `-march`.** As rotinas de byte-swap do SDK usam
   intrínsecos SSSE3 — necessárias porque o Xbox 360 é big-endian e o PC não.
   O SDK declara essa ISA só nos presets dele; os presets que o `rexglue init`
   gera não tinham nada, então as bibliotecas internas compilavam sem SSSE3 e
   falhavam.

3. **`imgui` linkado como PRIVATE, mas exposto em header público.**
   `rex/ui/style.h` faz `#include <imgui.h>` e `rex/rex_app.h` puxa ele em
   cascata — ou seja, todo projeto gerado pelo template precisa desse include,
   mas ele não propagava.

As correções estão prontas para virar PR.

---

## Roteiro

- [x] Traduzir o XEX para C++
- [x] Compilar um executável nativo
- [x] Fazer o runtime subir e carregar o jogo
- [x] Passar dos inicializadores estáticos iniciais
- [ ] Melhorar o detector de funções órfãs (seguir fluxo de controle)
- [ ] Resolver as jump tables (`[[switch_tables]]`)
- [x] Chegar na primeira imagem renderizada
- [x] Chegar ao gameplay jogável
- [x] Rodar em 2K a 144 FPS
- [ ] Atingir 2K a 244 FPS com estabilidade
- [ ] Abrir os PRs no ReXGlue SDK

---

## Sobre

Sou brasileiro, 23 anos, estudante de engenharia de software.
Isso aqui é projeto de tempo livre — nasceu da curiosidade de entender como uma
recompilação estática funciona de verdade, e virou isso.

Não tenho pressa nem promessa de entrega. Vou publicando o que for saindo.

Se você entende de PowerPC, de engenharia reversa de Xbox 360, ou só quer
acompanhar, issues e discussões são bem-vindas.

---

## Legal

Projeto de pesquisa e engenharia reversa para interoperabilidade. Não distribui
nem inclui nenhum código, arte, áudio ou dado do jogo — tudo isso tem que vir
do disco que você comprou. UFC Undisputed 3 é marca e propriedade da THQ /
Yuke's / seus respectivos detentores, sem nenhuma relação com este projeto.
