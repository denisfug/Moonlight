# CLAUDE.md — Moonlight HB

Centro multimídia homebrew para PS4 (GoldHEN), C++17, toolchain OpenOrbis. Leia também `docs/ROADMAP.md`
(estado real de cada item) e `docs/COMPATIBILITY.md` (o que foi testado onde).

## Regras do projeto (obrigatórias)

- Funcionalidade só está concluída com: implementação + compilação + teste + resultado no ROADMAP.
- Separe sempre resultados **PC** de resultados **PS4**. Nunca declare algo funcionando no console sem teste real.
- Não invente APIs, dependências ou resultados. Verifique headers em `$OO_PS4_TOOLCHAIN/include/orbis`.
- Nada fora de `src/platform/ps4` inclui `orbis/*`. Toda chamada de hardware passa por `src/platform/platform.hpp`.
- Sem DRM bypass, sem P2P/torrent para conteúdo de terceiros, sem credenciais no código, sem tokens em logs
  (use `redactUrl`).
- Respeite licenças: pPlay é GPL-3 (só referência); não copiar código de Stremio/Convx/SimpMusic.
- Não use objetos estáticos com construtor nem `static` local com construtor no código do app
  (o binário PS4 hoje não tem `.init_array` nem `__cxa_guard`; verifique com `nm build-ps4/moonlight`).
- Sem exceções (`-fno-exceptions`); erros por valor de retorno/string.
- Mudanças pequenas e testadas; não reescreva arquivos que funcionam sem justificar.

## Estrutura

```
src/core        JSON, URL/política de rede, contrato HttpClient
src/storage     JsonStore (escrita atômica)
src/addons      protocolo Stremio (protocol.*) + SourceManager
src/library     favoritos / histórico / progresso
src/music       biblioteca MP3, ID3, fila
src/live        parser M3U
src/radio       diretório Radio Browser, estações, favoritas
src/player      MediaPlayer, AudioPlayer (MP3), VideoPlayer (indisponível), mp3_core (C)
src/ui          Canvas (software), Font (stb_truetype), widgets, teclado, InputMap, tema
src/app         App (shell/navegação/overlays) + telas (screens_*.cpp) + demo_data
src/platform    platform.hpp | host/ (PC) | ps4/{video,controller,audio,filesystem,network} + ps4_platform
tests/          testes (framework próprio em test.hpp) + fixtures (addon de demonstração, M3U, MP3 de teste)
toolchain/      build-libcxx.sh, overlay/math.h
tools/          make_pkg.sh, make_icon.py, run_live_test.sh
```

## Comandos

```bash
make test                         # PC: build + testes (deve terminar com "0 failed checks")
make live                         # testes de fontes contra servidor HTTP local
make shots                        # capturas de tela em out/shots (confira visualmente)
OO_PS4_TOOLCHAIN=... GOLDHEN_SDK=... make pkg     # PS4: out/Moonlight-HB_<versão>.pkg
```

Para adicionar um teste: `TEST(nome) { CHECK(...); }` em `tests/test_*.cpp`.
Versão do app: `ML_VERSION` em `CMakeLists.txt` (formato `NN.NN`).

## Dependências

Ver tabela em README.md. libc++ é gerada em `toolchain/libcxx` (não versionada).

## Implementado / pendente / limitações

Fonte da verdade: `docs/ROADMAP.md`. Resumo: UI completa com dados de demonstração, música MP3 local,
fontes Stremio (PC), biblioteca e M3U funcionam no PC. Pendentes: primeiro teste no PS4 (inclusive a internet via
`sceHttp`/`sceSsl`, já implementada), rádio dentro do plugin, motor de vídeo (`docs/MEDIA_ENGINE.md`), imagens de pôster, requisições em thread.
Limitação conhecida: o PS4 fecha o app quando um jogo abre, então a música do app não toca durante jogos.
