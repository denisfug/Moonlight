# Roadmap

Regra: uma funcionalidade só é "concluída" com implementação + compilação + teste + resultado registrado.
Colunas: **PC** = teste automático no computador; **PS4** = teste no console.

## Fase 0 — Ambiente ✅

| Item | Resultado |
|---|---|
| Inspeção: clang/lld 18.1.3, CMake 3.28, OpenOrbis (construído do código-fonte) | feito |
| Faltava libc++ e SDL2 no toolchain | libc++ construída (`toolchain/build-libcxx.sh`); SDL2 adiado |
| Estudo do pPlay | `docs/MEDIA_ENGINE.md` |

## Fase 1 — Primeiro executável

| Item | Compila PS4 | PC | PS4 |
|---|---|---|---|
| Projeto CMake (PC + PS4), Makefile | sim | sim | — |
| Loop principal, redesenho sob demanda | sim | sim | **pendente** |
| Renderizador, fontes, identidade visual, logo/ícone | sim | sim (capturas) | **pendente** |
| DualShock: D-pad, analógico, X/O/□/△, L1/R1, L2/R2, Options, R3, repetição | sim | sim (`input_repeat_and_stick`, `app_navigation_headless`) | **pendente** |
| PKG gerado e validado estruturalmente | sim | — | **pendente instalação** |

## Fase 2 — Estrutura multimídia (dados de demonstração)

| Item | PC | PS4 |
|---|---|---|
| Início: hero + fileiras (continuar, filmes, séries, música, canais) | sim | pendente |
| Filmes/Séries: grade, gêneros, busca, detalhe, temporadas/episódios, favoritos | sim | pendente |
| Música: playlists, faixas, fila, aleatório, repetir, volume, busca, barra de progresso | sim | pendente |
| Canais: M3U, categorias, aviso de DRM | sim | pendente |
| Biblioteca: favoritos, em andamento, histórico (persistência) | sim | pendente |
| Configurações | sim | pendente |
| Teclado na tela (busca, URL de fonte) | sim | pendente |

## Fase 3 — Addon Manager

| Item | PC | PS4 |
|---|---|---|
| Parse manifest/catalog/meta/stream/subtitles | sim | — |
| SourceManager + persistência + segurança | sim | — |
| Servidor HTTP local de teste | sim | — |
| Cliente HTTP/HTTPS do PS4 (sceHttp/sceSsl) | — | ver Fase 3b |
| Requisições em segundo plano (thread) | catálogos, pôsteres, atualização, detalhes e links (01.17): sim, testado com servidores locais; busca, adicionar fonte e servidor de música ainda na thread da interface | catálogos/pôsteres/atualização em uso desde 01.12; detalhes e links: aguardando teste (01.17) |
| Imagens de pôster (stb_image + rede, thread própria, cache limitado) | sim (servidor local) | pendente (01.05) |
| Importar fontes de `moonlight-fontes.txt` no pendrive (R1 em Fontes) + atalhos de URL no teclado | sim | relatado: não achou o arquivo (01.05) |
| Importação tolerante a nome (`*.txt` com "fonte"/"moonlight", extensão dupla do Windows) | sim | pendente (01.06) |
| Teclado do sistema PS4 (libSceImeDialog), com o teclado próprio como reserva | — (PC usa o próprio) | pendente (01.06) |
| Demonstração de filmes/séries some quando há fonte ativa; catálogos em cache até mudar as fontes | sim | pendente (01.06) |

## Fase 3b — Internet no PS4 e Rádios (01.01)

| Item | Compila PS4 | PC | PS4 |
|---|---|---|---|
| Cliente HTTP/HTTPS do PS4 | sim | sim (TLS local) | **PS4 01.13: funciona (HTTP/TLS próprios, Mbed TLS, 163 raízes)** |
| Controle pelo celular (página web na rede local, PIN) | sim | sim | **01.09, aguardando teste** |
| Vídeo HTTPS via cliente próprio (Range) | sim | Range testado | **01.09, aguardando teste** |
| Catálogo em fileiras (catálogo/serviço e gênero), carga em segundo plano | sim | sim (servidor local) | **01.10, aguardando teste** |
| Atualização (feed no branch claude/updates) | sim | busca/download/sha256 sim | **01.10 fechou ao abrir (CE-34878-0); 01.11 corrige o módulo Random, aguardando teste.** |
| Instalação automática do PKG | sim (contrato; sem motor no PC) | — | **01.19: religada via libSceAppInstUtil (sceKernelLoadStartModule+Dlsym). 01.20 (bug): PrepareOverwritePkg desinstalava o app antes de instalar e, se a instalação não completava, o app sumia ("apagou o app"). 01.21: NUNCA desinstala — instala por cima; se falhar, cai no manual com o app intacto. AGUARDANDO TESTE.** |
| Nova interface (pílula de ícones, vidro, brilho) | sim (conferido em make shots) | — | **01.21: barra lateral vira pílula flutuante + vidro + brilho. 01.22: tela inicial RECONSTRUÍDA conforme o design (Main.dc.html) — top bar (busca+abas+perfil), painel "Continuar assistindo" à esquerda, card de destaque com lua à direita, fileira embaixo; 01.21 só havia repintado o layout velho. AGUARDANDO TESTE no console (desempenho de desenho).** |
| **Redesign identidade visual: preto/branco/vermelho** (`theme.hpp/theme.cpp`): tokens Dark (preto puro, cinzas, branco, vermelho #DC2626) e Light (branco, cinzas claros, preto, mesmo vermelho); todas as telas atualizadas; alternância Dark/Light/Auto persistida em Configurações; sem objeto estático com construtor (PS4-safe) | sim | sim (`theme_dark_light_persisted`, make shots) | pendente |
| Streaming HTTP incremental (`HttpStream`) | sim | sim (servidor local real) | pendente |
| Rádios: diretório Radio Browser (só MP3), abas por gênero/país, busca, favoritas | sim | sim (fixtures + servidor local) | pendente |
| AudioPlayer ao vivo (rádio): conecta, toca, detecta queda e dados que não são MP3 | sim | sim (`radio_stream_playback`, `radio_live_local_server`) | pendente |
| Rádio durante jogos (plugin com internet) | — | — | **não iniciado** |

## Fase 3c — Servidor de música próprio (01.02)

Navidrome/Gonic/Airsonic (protocolo Subsonic, documentação OpenSubsonic Apache-2.0). Ideia vinda do
PixelPlayer (licença proprietária: nenhum código dele foi usado).

| Item | Compila PS4 | PC | PS4 |
|---|---|---|---|
| MD5 próprio (vetores RFC 1321 + exemplo da doc Subsonic) | sim | sim | — |
| Login por token (senha nunca salva), ping, playlists, mix aleatório, busca | sim | sim (fixtures + servidor local) | pendente |
| Streaming com transcodificação para MP3 (`format=mp3`), fim de faixa avança a fila | sim | sim | pendente |
| Configuração pelo teclado na tela (Configurações) | sim | sim | pendente |
| Seek em faixas do servidor | não | não | — |

## Fase 3d — Música livre: Internet Archive (01.27)

Áudio de domínio público / Creative Commons de archive.org: sem conta, sem chave, sem DRM. Só MP3
(decodificador minimp3); itens que só existem em OGG/FLAC não mostram faixas. O Archive busca **álbuns**
(itens), não faixas soltas. Liga/desliga em Configurações > "Música do Internet Archive".

| Item | Compila PS4 | PC | PS4 |
|---|---|---|---|
| URLs e leitura das respostas (`advancedsearch.php`, `/metadata/<id>`, `/download/<id>/<arquivo>`) | sim | sim (fixtures) | pendente |
| Busca de música mostra álbuns do Archive; X abre o álbum; faixa toca por streaming | sim | sim (`archive_live_local_server`: servidor local, não o archive.org de verdade) | pendente |
| Rede fora da thread da interface (MultiFetch) para a busca e para abrir o álbum | sim | sim | pendente |
| Teste contra o archive.org real (sintaxe da busca, redirecionamento para o servidor de dados, certificado) | — | **não feito** (o ambiente de build não alcança archive.org) | pendente |
| Capas dos álbuns (`/services/img/<id>`) na lista, no cabeçalho do álbum e no "tocando agora"; lista de resultados acompanha a seleção (01.29) | sim | sim (servidor local) | pendente |
| 01.28 no console: busca no archive.org real, abrir álbum e tocar | — | — | **sim** (relato do dono, 16/11: tocou um álbum) |
| Busca do Archive só em título e artista (`title:(..) OR creator:(..)`) em vez de qualquer campo (01.30) | sim | sim (URL) | pendente — **a sintaxe nunca falou com o archive.org real** |
| **Audius** (`src/music/audius.*`, 01.30): busca por faixa, "Em alta no Audius" (semana), stream MP3, capa por faixa, selo da fonte; fora as faixas restritas/não tocáveis; repetidas não aparecem duas vezes | sim | sim (fixtures + `archive_live_local_server` com servidor local) | pendente — **nunca falou com api.audius.co** (endereço base e redirecionamento do stream a confirmar no console) |
| Modelo comum das fontes (`src/music/catalog.hpp`: `Song`, `SongSource`); liga/desliga por fonte em Configurações | sim | sim | pendente |
| **Jamendo** (`src/music/jamendo.*`): busca por faixa Creative Commons full-length MP3, capa 500px, duração, gênero, selo da fonte; kPublicClientId como fallback documentado (devportal.jamendo.com); primeiro na busca; liga/desliga em Configurações | sim | sim (fixtures + test_jamendo: 4 testes) | pendente — nunca falou com api.jamendo.com real |
| **Openverse** (`src/music/openverse.*`, 01.30): busca por faixa Creative Commons full-length MP3 via api.openverse.org/v1/audio/ (Wikimedia Foundation), capa, duração, gênero, selo da fonte; deduplicação por título+artista; liga/desliga em Configurações > "Música do Openverse" | sim | sim (fixtures, 4 testes; `openverse_search_url`, `openverse_featured_url`, `openverse_parse_search`, `openverse_parse_empty`) | pendente — **nunca falou com api.openverse.org real** |

A busca no servidor Subsonic continua na thread da interface (pendente, como antes).

## Desempenho do desenho (01.27)

Medido no PC com `render_cost_report` e callgrind (tela inicial, 20 quadros). No PS4: **não medido**.

| Mudança | PC |
|---|---|
| `strokeRoundRect` só percorre a borda (antes: o retângulo inteiro, 2 testes de distância por pixel) | imagens idênticas pixel a pixel (`make shots`, 5 telas) |
| `shape`: recorte uma vez, cantos separados do miolo; gradiente horizontal/vertical com tabela; diagonal sem refazer a separação dos canais; `fillCircle` com miolo direto | idem |
| Total | 8,8 bi -> 2,0 bi instruções; 32 -> 10 ms por quadro no PC |
| Tela só "carregando" (sem movimento) e capas chegando: no máximo ~5 redesenhos por segundo (`Screen::busy()`), em vez de um por quadro disputando CPU com as threads de rede | testes passam; efeito no console não medido |

## Builds 01.24 a 01.27: fecham na abertura (CE-34878-0) — causas encontradas, 01.28 aguarda teste

O ambiente de build foi reiniciado e remontado com o pacote **v0.5.2** do OpenOrbis. Cinco builds fecharam
no console ao abrir (01.24, 01.25, 01.26, 01.27 e 01.27 "b"). Comparando o `eboot.bin` da 01.23 (abre) com os
que fecham, e recompilando o código da 01.23 com cada toolchain:

| Causa | Evidência | Correção |
|---|---|---|
| `create-fself` do v0.5.2 corta os dados iniciados do executável depois de `.data` (0x340 bytes no ELF, 0xB8 no eboot): `__libc`, `stderr` etc. chegam zerados | reproduzido: mesmo ELF, as duas ferramentas; a 01.23 que abre tem 0x340 e TLS alinhado em 0x20, as que fecham 0xB8 e 0x8 | toolchain **v0.5.4** (`releases/download/v0.5.4/toolchain-llvm-18.tar.gz`); `tools/check_eboot.py` recusa o pacote se o corte acontecer |
| link pegava a `libc++.a` de 2021 que vem em `$OO/lib` em vez da do projeto (01.24–01.27) | +464 KB, 11 importações `pthread_*` a mais, `__cxa_guard` presente | `-L toolchain/libcxx/lib` antes de `-L $OO/lib` |

01.28 = 01.27 com as duas correções: sem `__cxa_guard`/`.init_array`, mesmas importações da 01.23 (menos as
duas de memória de vídeo retiradas de propósito), mesmos tamanhos de segmento de dados e TLS da 01.23, e o PKG só
difere dela no `eboot.bin`. **PS4: a 01.28 abriu no console (relato do dono, 16/11).** Teste de vídeo: falhou de novo (detalhes da tela ainda não recebidos); música do Internet Archive e leveza: ainda não testados. O feed de atualização continua na 01.23.
O conserto do vídeo (av_engine: memória de textura comum, prioridade 160, 4 buffers) está no código desde a
01.24 e **nunca rodou no console**.

**01.29**: memória de textura restaurada para GPU-visível (sceKernelAllocateDirectMemory type=3 + Map prot=0x33,
align=0x200000 — idêntico ao video_out.cpp que funciona). A hipótese revisada é que nas 01.13-01.23 o problema
era a *combinação* priority=700 + 2 buffers + memória direta. A 01.24+ corrigiu prioridade e buffers mas reverteu
a memória; nenhuma versão testou os três fatores corretos juntos até agora. `TexTable` sem destrutor (PS4-safe);
`releaseAll()` chamado em `stop()`. Tipos corrigidos em `texAlloc`: `size_t` para `len`/`alignment` em vez de
`off_t` (como exige `libkernel.h`). **PC: 99 testes, 0 falhas. PS4: aguardando teste.**

**01.30**: Openverse integrado (fonte CC Wikimedia, full-length MP3, sem DRM, sem conta). No av_engine, fallback
`posix_memalign` disponível via `ML_AV_TEX_MALLOC` para o caso da GPU memory não produzir quadros: se definido
em tempo de compilação, `texAlloc` usa `posix_memalign` como o PS4 IPTV Player que funciona no console.
Nenhum teste de vídeo novo rodou no console nesta versão.

**Referência externa:** o PS4 IPTV Player (OpenOrbis, código aberto) usa `posix_memalign` para a textura e
funciona no console. Se a 01.29 (GPU memory) também não produzir quadros, a próxima hipótese é: reverte para
`posix_memalign` como o IPTV Player — ou seja, o problema pode nunca ter sido o tipo de memória, mas sim
priority=700 e 2 buffers. As versões 01.24-01.28 que usavam `posix_memalign` **nunca chegaram ao console**
(fechavam ao abrir por bugs do toolchain v0.5.2 e libc++ antiga, corrigidos em 01.28). A 01.28 abriu no console
mas o teste de vídeo falhou sem detalhes suficientes para diagnosticar.

## Fase 4 — Reprodução

| Item | PC | PS4 |
|---|---|---|
| AudioPlayer MP3 | sim | pendente |
| Prova de vídeo com sceAvPlayer (MP4 local), tela Vídeos, player em tela cheia | sim (UI; sem motor no PC) | **01.04 aguardando teste** |
| VideoPlayer real, legendas, streaming | não | não |
| Música em segundo plano durante jogos | fora do app (o PS4 fecha o app); ver plugin GoldHEN do projeto DevHub | — |

## Fases 5–6 — Integração e testes

Pendentes. Precisam do resultado do primeiro teste no console.

## Próxima tarefa recomendada

**Para o dono (teste no console):**
1. Instalar `Moonlight-HB_01.30.pkg` e rodar o Teste de Vídeo (Configurações > Problemas ao reproduzir).
   - Registrar o diagnóstico completo (passos do player, contagem de blocos de memória, quadros produzidos).
   - Se `texOk > 0` mas `quadros = 0` → próximo passo: build com `-DML_AV_TEX_MALLOC=1` (posix_memalign).
   - Se `texFail > 0` → GPU memory não alocou; build com `-DML_AV_TEX_MALLOC=1` diretamente.
2. Música do Openverse: após conectar à internet no console, buscar qualquer artista e confirmar se faixas aparecem.

**Para o agente (próxima build):**
- Gerar PKG 01.30 assim que a pasta do toolchain for compartilhada pelo dispositivo.
- Próximos recursos: busca/adicionar-fonte fora da thread UI; controle pelo celular; rádio durante jogos (plugin GoldHEN).
