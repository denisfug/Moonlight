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

1. Instalar `Moonlight-HB_01.04.pkg` (interface já validada no 01.03) no PS4 e anotar: a tela aparece? o controle navega? MP3 do pendrive toca?
   A aba Rádios lista estações (= internet + HTTPS funcionando)? Uma rádio toca?
2. Com isso validado: internet dentro do plugin GoldHEN (rádio durante o jogo) e prova de vídeo com `sceAvPlayer`.
