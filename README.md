# UFC 3 Recomp

Recompilação  de **UFC Undisputed 3** (Xbox 360) para PC, usando o
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

Não é emulação. O executável do jogo é traduzido de PowerPC para C++ e depois
compilado como um programa nativo de PC — o resultado é um `.exe` que roda
direto no processador, sem interpretar nada em tempo real.

---

> ## ⚠️ Você precisa do seu próprio disco de UFC Undisputed 3
> **Este projeto não distribui o jogo.**

---

## 📱 A versão Android está chegando

**UFC Undisputed 3 já está rodando em celular — nativo, sem emulação.**

[![UFC Undisputed 3 rodando no Galaxy S22](https://i.ytimg.com/vi/NKbL0ufgN_A/maxresdefault.jpg)](https://www.youtube.com/shorts/NKbL0ufgN_A)

*Gravado num Galaxy S22. Clique para assistir.*

[▶ Assista no YouTube](https://www.youtube.com/shorts/NKbL0ufgN_A)

O mesmo caminho do PC, do começo ao fim: o `default.xex` é traduzido de PowerPC
para C++ e compilado para **ARM64 nativo**. Não há emulador, não há camada de
tradução de instruções, não há Box64 nem nada parecido no meio — o código do jogo
vira instruções ARM de verdade e roda direto no Snapdragon.

### Rodando no Galaxy S22

| | |
|---|---|
| Aparelho | Galaxy S22 (Snapdragon 8 Gen 1, Adreno 730) |
| Sistema | Android 16, arm64-v8a |
| Resolução | 2340×1080, tela cheia |
| Gráficos | Vulkan 1.4 |
| Menus | **60 fps** |
| Luta | **30 fps** — a mesma taxa do Xbox 360 original |

O desempenho é ótimo: o jogo entrega a taxa de quadros do console, em tela cheia
e na resolução nativa do aparelho.

---

## Gameplay em 2K

**UFC Undisputed 3 está jogável no PC, em 2560x1440, por meio desta recompilação.**

[![UFC Undisputed 3 — jogável em 2K a 144 FPS](https://i.ytimg.com/vi/hUpWKWqRkJ8/maxresdefault.jpg)](https://www.youtube.com/watch?v=5cQYiwqAFsc)

*Imagem de UFC Undisputed 3 — clique para assistir ao vídeo do projeto.*

[▶ Assista ao gameplay no YouTube — 2K a 144 FPS](https://www.youtube.com/watch?v=5cQYiwqAFsc)

### Uma palavra sobre o número de quadros

Esse vídeo foi gravado com o modo de vídeo em 144 Hz, e depois descobrimos o que
esse número significa **neste motor**, o que vale registrar.

O runtime deriva o vblank do guest de `video_mode_refresh_rate`, e o jogo avança
**um passo de simulação por vblank**. A 144 Hz a simulação anda 2,4 vezes mais
rápido que no console: a imagem fica fluida, mas o jogo corre acelerado. Subir
para 244 não seria uma melhoria — seria o jogo em câmera rápida.

Então o número que importa aqui não é o de quadros por segundo, é o do vblank:
**60 Hz é a velocidade correta**, e é o padrão. O ganho de PC vem da resolução e
da filtragem, não da taxa de atualização.

---

## Em que pé está

**O jogo está jogável do começo ao fim, e bonito.**

Roda a 2560x1440 com filtragem anisotrópica 16x, cache de textura ampliado e
profundidade arredondada — o resultado é bem mais nítido do que o console
entregava, sem o cintilar de sombra que o original tinha. Menus, seleção de
lutador, entrada no octógono, luta completa com HUD, criação de lutador e saves
de carreira: tudo funciona.

E não é preciso compilar nada para jogar. A [release](../../releases/latest) traz
o executável pronto; você aponta o launcher para a ISO do seu disco e joga.

O que está montado por baixo:

- O `default.xex` é traduzido inteiro para C++ — 569 arquivos, 295 MB de código
- Isso compila e linka num executável nativo de 93 MB
- **66.157 funções recompiladas** registradas na tabela de funções
- Imports do kernel do Xbox 360 resolvidos: 101 do `xam`, 184 do `xboxkrnl`
- Runtime completo no ar: memória do guest, SDL, entrada, áudio com as threads
  do decodificador XMA, sistema de arquivos virtual montando `game:` e `d:`
- Codegen sem nenhum aviso — o manifesto cobre todas as funções que o scanner
  não alcança sozinho

O trabalho agora é estabilidade e os cantos menos percorridos do jogo.

---

## Como isso evoluiu

Vale registrar o caminho, porque quase nada dele foi o que eu esperava no
começo.

**O gargalo nunca foi gráfico.** É achar onde cada função começa e termina
dentro do executável do console. Essa é a seção seguinte, e é o coração do
projeto.

**O manifesto encolheu para funcionar.** Uma tentativa de resolver na força
bruta gerou 4.751 entradas automáticas e produziu 2.919 falhas latentes. A
medição mostrou o oposto do esperado: 8 entradas conferidas à mão davam zero
avisos. Hoje são **66 entradas, todas verificadas uma a uma** — e o codegen sai
limpo. Nesse tipo de trabalho, quantidade não é progresso.

**Descobrimos que o motor amarra simulação a vblank.** Foi o que explicou o
"jogo rápido demais" e, junto, por que aumentar o supersampling derruba a
*velocidade* em vez dos fps: a GPU satura, os vblanks atrasam, e a simulação
anda menos. Por isso o padrão é 1x e 60 Hz.

**Escala não-quadrada quebra as texturas dos lutadores.** Testamos 2x1 para
ganhar nitidez sem o custo de 2x2: as texturas dos personagens corrompem. O
caminho está fechado, e os dois eixos precisam andar juntos.

**Cinco defeitos do SDK apareceram** — três no build, um no codegen, um no
caminho de saves. Estão descritos mais abaixo.

**O launcher virou parte do projeto**, não um acessório: é ele que faz a ponte
da ISO até o jogo aberto, sem exigir ferramenta nenhuma de quem só quer jogar.

---

## O launcher

Um único executável, com a interface, as imagens e as fontes embutidas — não há
arquivo solto para alguém alterar.

- Aceita a ISO do seu disco ou uma pasta já extraída
- Lê o cabeçalho do XEX e confere o title ID (`5451087D`) antes de seguir; se
  você apontar outro jogo, ele diz qual encontrou
- Ajusta resolução, supersampling, presets de qualidade, idioma e controles,
  gravando tudo no mesmo `ufc3.toml` que a tela de opções do jogo usa
- Funciona de dois jeitos, decididos por um único teste — existe um `ufc3.exe`
  ao lado dele? Se sim, só extrai e joga. Se não, traduz e compila.

O código está em [`launcher/`](launcher).

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
[`tools/find_orphans.py`](tools/find_orphans.py) que desmonta os 17 MB de código, cruza com as funções
que o recompilador já conhece e procura código válido que não é alvo de nenhum
branch direto. Ele acha o caso confirmado com o tamanho exato — mas a detecção
de **fim** de função ainda é ingênua (para no primeiro terminador, e função
real tem vários blocos internos), o que gera falso positivo em bloco de
`switch`. Melhorar isso para seguir o fluxo de controle de verdade é
provavelmente a contribuição mais valiosa que alguém pode fazer aqui.

---

## Compilando por conta própria

Isto é para quem quer fazer a tradução na própria máquina. Para só jogar, use a
[release](../../releases/latest) — ela não exige nenhuma ferramenta.

Primeiro, extraia do seu disco:

```
assets/game/default.xex
```

Sem esse arquivo não há o que recompilar.

Só o preset **`win-amd64-relwithdebinfo`** foi testado. O `CMakePresets.json`
vem do `rexglue init` e lista também Linux, macOS e ARM64 — nenhum desses foi
exercitado aqui, então trate-os como ponto de partida, não como suporte.

Depois: Clang 18+, CMake 3.25+, Ninja e o ReXGlue SDK. No Windows tudo isso, com
exceção do SDK, vem junto com os **Build Tools da MSVC** — o Clang usa os headers
e libs deles, e o link é feito no ABI da Microsoft. Não é preciso instalar cmake
nem ninja à parte.

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

Montar isso do zero no Windows revelou **seis** defeitos no ReXGlue SDK. Os
três primeiros são da mesma família — **consumir o SDK pelo código-fonte, do
jeito que o próprio `rexglue init` documenta, estava quebrado**:

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

Os outros dois apareceram depois, já com o jogo rodando:

4. **Aviso enganoso no codegen.** `emitBranchWithBoundsCheck` avisava sobre
   qualquer branch que saísse da função, mas thunks de despacho fazem isso o
   tempo todo, por construção. O aviso escondia os casos que de fato importam.
   A correção está escrita, mas **ainda não validada** — precisa de um build
   separado para não arriscar a versão jogável.

5. **`user_data_root` do TOML não tem efeito.** Em `ui/rex_app.cpp` os caminhos
   são resolvidos a partir dos cvars **antes** de `LoadConfig()` ler o arquivo,
   então o valor posto no TOML chega tarde demais e a pasta padrão prevalece —
   em silêncio, criando um segundo conjunto de saves em `Documentos`. O
   contorno é passar o caminho na linha de comando, que é o que o launcher faz.

6. **A versão do SDK é lida do repositório errado.** `rex_resolve_version`
   usa `CMAKE_SOURCE_DIR` como padrão, que aponta para a raiz do projeto
   **consumidor** quando o SDK entra por `add_subdirectory`. O `git describe`
   roda então no repositório de quem usa o SDK, e uma tag de release lá em cima
   é comparada com o piso de versão do SDK. Criar a tag `v1.0.0` neste projeto
   foi o bastante para o configure abortar com *"floor version (0.10) is behind
   tag version (1.0)"*. A correção é uma linha: passar
   `SOURCE_DIR ${REXGLUE_ROOT}` na chamada.

As correções dos três primeiros, mais esta última, estão prontas para virar PR.

---

## Roteiro

- [x] Traduzir o XEX para C++
- [x] Compilar um executável nativo
- [x] Fazer o runtime subir e carregar o jogo
- [x] Passar dos inicializadores estáticos iniciais
- [x] Chegar na primeira imagem renderizada
- [x] Chegar ao gameplay jogável
- [x] Manifesto sem avisos no codegen (66 entradas verificadas)
- [x] Saves de carreira funcionando e num lugar só
- [x] Launcher: da ISO até o jogo aberto, sem exigir ferramentas
- [x] Release pronta para jogar
- [ ] Texturas do tronco na criação de lutador
- [ ] Melhorar o detector de funções órfãs (seguir fluxo de controle)
- [ ] Resolver as jump tables (`[[switch_tables]]`) — 1.067 já extraídas
- [ ] Validar a correção nº 4 do SDK num build separado
- [ ] Abrir os PRs no ReXGlue SDK

**Android**

- [x] Traduzir e compilar o jogo inteiro para ARM64
- [x] Runtime, Vulkan e memória do guest de pé no aparelho
- [x] Montar a ISO direto, sem extrair
- [x] Tela cheia e orientação travada em paisagem
- [x] Controles do Xbox 360 na tela
- [x] Menu com seletor de arquivos para a ISO
- [ ] Aquecer o cache de shader antes da primeira partida
- [ ] Release do APK

---

## Sobre

Sou brasileiro, 23 anos, estudante de engenharia de software.
Isso aqui é projeto de tempo livre — nasceu da curiosidade de entender como uma
recompilação estática funciona de verdade, e virou isso.

Não tenho pressa nem promessa de entrega. Vou publicando o que for saindo.

Se você entende de PowerPC, de engenharia reversa de Xbox 360, ou só quer
acompanhar, issues e discussões são bem-vindas.

Agora que o código está aberto, dá para contribuir de verdade. O
[CONTRIBUTING](CONTRIBUTING.md) diz onde a ajuda faz mais diferença — e,
principalmente, **o que eu já tentei e não deu certo**, para ninguém repetir.
As ferramentas de análise estão em [`tools/`](tools/), cada uma explicada.

---

## Legal

Projeto de pesquisa e engenharia reversa para interoperabilidade.

O que a release distribui é o executável do console **traduzido** para C++ e
compilado como programa nativo — obra derivada, produzida por este projeto.
Nenhum dado do jogo vai junto: arte, áudio, vídeo, modelos e atributos dos
lutadores continuam vindo do disco que você comprou, e sem ele o programa nem
abre.

UFC Undisputed 3 é marca e propriedade da THQ / Yuke's / seus respectivos
detentores, sem nenhuma relação com este projeto.
