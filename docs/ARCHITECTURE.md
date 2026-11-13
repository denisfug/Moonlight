# Arquitetura — Moonlight HB

## Camadas

```
┌──────────────────────────────────────────────────────────────┐
│ app/      App (shell, navegação, overlays) + telas            │  interface
│ ui/       Canvas (renderizador por software), Font, widgets,   │
│           InputMap (repetição, analógico → direcional)         │
├──────────────────────────────────────────────────────────────┤
│ addons/   Protocolo Stremio (parse/rotas) + SourceManager      │  lógica
│ library/  favoritos, histórico, progresso                     │
│ music/    biblioteca local, ID3, fila (shuffle/repeat)         │
│ live/     parser M3U                                          │
│ player/   MediaPlayer: AudioPlayer (MP3) | VideoPlayer (pend.) │
│ storage/  JsonStore (escrita atômica)    core/ JSON, URL, HTTP │
├──────────────────────────────────────────────────────────────┤
│ platform/platform.hpp  ← única fronteira com o hardware        │  plataforma
│   host/  PC: framebuffer em memória→PNG, controle por script,  │
│          arquivos em sandbox, HTTP simples (testes)            │
│   ps4/   VideoOut, Pad, AudioOut, arquivos (libkernel),        │
│          threads (scePthread), GoldHEN (jailbreak p/ USB)      │
└──────────────────────────────────────────────────────────────┘
```

Regra: nada fora de `src/platform/ps4` inclui `orbis/*`. Tudo acima de `platform/` compila e roda no PC,
e é isso que os testes exercitam.

## Decisões

| Decisão | Motivo |
|---|---|
| Renderizador por software próprio (sem SDL2) | O toolchain OpenOrbis disponível foi construído do código-fonte e não tem SDL2/libc++ pré-compilados (os releases prontos não puderam ser baixados neste ambiente). VideoOut + framebuffer linear é o caminho dos exemplos oficiais e das nossas builds anteriores. SDL2 pode entrar depois atrás da mesma interface `Display`. |
| libc++ compilada por nós (`toolchain/build-libcxx.sh`) | Necessária para C++17. Sem exceções e sem localização (sem iostream). |
| stb_truetype em vez de FreeType | Uma única fonte em C, domínio público, sem módulo do sistema. FreeType do sistema (`libSceFreeType`) não foi validado. |
| Arte de capa gerada | Sem rede no PS4 ainda e sem uso de pôsteres de terceiros; a arte procedural é original e cacheada. |
| JSON próprio | Sem exceções, com limite de tamanho/profundidade, testado. |
| Música separada do vídeo | Modelos e contratos diferentes; o protocolo Stremio é só para vídeo. |
| Sem objetos estáticos com construtor no binário PS4 | Evita depender de `.init_array` no startup (verificado: 0 inicializadores). |

## Fluxo de um quadro

`App::frame()` → lê o controle → `InputMap` gera eventos (com repetição) → overlay ativo (teclado/modal)
ou barra lateral ou tela atual → `update(dt)` (animações) → **só redesenha se algo mudou** → `present()`.
No PS4, `present()` espera o vsync; sem mudanças o loop dorme 16 ms.

## Telas

| Seção | Arquivo | Dados |
|---|---|---|
| Início | `screens_home.cpp` | continuar assistindo, demo, catálogos das fontes, playlists, canais |
| Filmes / Séries | `screens_media.cpp` | grade + gêneros (L1/R1) + busca; detalhe com temporadas/episódios |
| Música | `screens_music.cpp` | playlists do HD/USB + demo, fila, busca |
| Canais | `screens_misc.cpp` | `/data/moonlight/channels.m3u` ou demo |
| Biblioteca | `screens_misc.cpp` | favoritos, em andamento, histórico |
| Fontes | `screens_misc.cpp` | SourceManager (adicionar/ativar/atualizar/remover) |
| Configurações | `screens_misc.cpp` | animações, demo, reescanear música, sobre |

## Persistência (`/data/moonlight`)

`settings.json`, `library.json`, `sources.json` — escritos em `arquivo.tmp` e renomeados.
Arquivo corrompido é guardado como `*.corrupt` e o app recomeça vazio.
