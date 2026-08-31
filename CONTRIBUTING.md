# Contribuindo

Se você chegou aqui querendo ajudar: obrigado. Este documento diz onde a ajuda
faz mais diferença, e o que eu já tentei e não deu certo — para você não gastar
tempo repetindo.

## Antes de tudo

Você precisa do seu próprio disco de UFC Undisputed 3. Nada do jogo está neste
repositório, e **pull request que traga qualquer arquivo do disco será
recusado** — `.xex`, `.iso`, o C++ gerado, texturas, áudio, save. O
`.gitignore` já barra os casos óbvios, mas confira antes de abrir.

Para montar o ambiente, veja *Compilando por conta própria* no
[README](README.md). As ferramentas de análise estão em
[`tools/`](tools/README.md), com um documento explicando cada uma.

## O problema central

Não é gráfico, nem áudio, nem desempenho. É **descobrir onde cada função começa
e termina** dentro do executável do console.

O recompilador acha funções seguindo chamadas (`bl`) e reconhecendo prólogos.
Existe uma classe que escapa dos dois: funções pequenas demais para terem
prólogo, alcançadas só indiretamente — por vtable, por adjustor thunk, por
tabela de construtores estáticos. E, como não mexem na pilha, elas também não
aparecem no `.pdata`. Das 47.145 funções que o `.pdata` lista, nenhuma cobre
esses casos.

Cada uma dessas foi encontrada e conferida à mão. São as 66 entradas do
`ufc3_manifest.toml`.

## Onde ajudar, em ordem de valor

### 1. Detecção de fim de função por fluxo de controle

`tools/find_orphans.py` para no primeiro terminador, e função real tem vários
blocos internos — o que gera falso positivo em bloco de `switch`.
`find_orphans2.py` já segue o fluxo, mas ainda erra. Acertar isso é
provavelmente a contribuição mais valiosa possível aqui: destrava tudo o mais.

Um candidato só é aceitável se passar nos quatro testes: não ser função
conhecida, não ser alvo de nenhum branch direto, vir depois de um terminador, e
ter corpo contíguo que fecha.

### 2. Aplicar as jump tables

`tools/acha_switch.py` já extraiu **1.067 tabelas**, com endereço, registrador
de índice e rótulos validados na faixa de código. Elas nunca foram aplicadas ao
manifesto. Aplicar e medir o efeito é trabalho bem delimitado.

### 3. Texturas do tronco na criação de lutador

Bug aberto e reproduzível. O que já foi descartado: não é formato exótico — o
log não mostra nenhum `k_DXN`, `k_DXT3A` ou `k_CTX1`, só formatos bem
suportados. Suspeita atual é o caminho de composição por render-to-texture que
o jogo usa para tatuagens e logos.

### 4. Validar a correção nº 4 do SDK

Está escrita e compila, mas **nunca foi validada**. Precisa de um diretório de
build separado — não vale arriscar a versão jogável.

## O que já tentei e não funcionou

Vale ler antes de propor.

**Manifesto na força bruta.** Gerei 4.751 entradas automáticas: 2.919 falhas
latentes. Reduzi para 1.874. Enquanto isso, 8 entradas conferidas à mão davam
zero avisos. **Quantidade não é progresso aqui** — cada entrada errada trunca
uma função que funcionava.

**Despachantes de vtable em lote.** 41 candidatos, link quebrado com `use of
undeclared label`: eram alvos de branch, isto é, blocos internos de funções
existentes. Com o filtro de alvo de branch, só 1 sobreviveu. O mesmo erro me
custou 16 thunks falsos antes de eu aplicar o filtro aos dois detectores.

**Escala não-quadrada de renderização.** 2x1 para ganhar nitidez com metade do
custo: corrompe as texturas dos personagens. Testado também com
`draw_resolution_scaled_texture_offsets = false`; continua corrompendo. Os dois
eixos precisam andar juntos.

**Aumentar a taxa de atualização.** Não deixa o jogo mais fluido: o vblank do
guest sai de `video_mode_refresh_rate` e o jogo avança um passo de simulação
por vblank. A 144 Hz ele roda 2,4× mais rápido. Pelo mesmo motivo, saturar a
GPU derruba a **velocidade**, não os fps.

## Como propor uma mudança no manifesto

Diga, para cada entrada:

1. o endereço e o tamanho;
2. como você chegou nesse fim — o caminho pelo grafo de fluxo;
3. por que não é alvo de branch direto;
4. o que muda no jogo, medido: avisos do codegen antes e depois, e até onde ele
   roda.

Entrada sem justificativa não entra, mesmo que pareça certa. Já perdi tempo
demais com palpite bem-intencionado — inclusive o meu.
