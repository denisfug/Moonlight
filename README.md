# Moonlight HB

Centro multimídia homebrew para **PlayStation 4 desbloqueado (GoldHEN)**: música, filmes, séries,
rádios e canais ao vivo numa interface própria para TV, navegável pelo DualShock 4.

> **Versão atual: 01.35**
> Desenvolvido em C++17 com toolchain [OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain).

---

## O que funciona

### No PS4 (validado no console)
- Interface completa 1920×1080 (tema escuro, violeta/azul), navegação pelo DualShock 4
- Música local: MP3 do pendrive ou `/data/moonlight/music/`, tags ID3, fila, aleatório, volume
- HTTP e TLS próprios (Mbed TLS 3.6.4) — o TLS do sistema falhou com 0x8095F00C
- Atualização OTA: Configurações → Atualizar o Moonlight

### No PC (validado, PS4 pendente de teste)
- Fontes Stremio (catálogo, busca, detalhes, streams)
- Rádios online (Radio Browser, só MP3, sem anúncio)
- Busca de música: Internet Archive, Audius, Jamendo, Openverse, Deezer (30s), JioSaavn, YouTube Data API
- Servidor de música próprio (Navidrome / Subsonic)
- Canais ao vivo por lista M3U
- Favoritos, histórico e progresso em JSON
- Controle pelo celular (página local na rede, com PIN)
- Light Tester interativo (Configurações → Light Tester)

### Pendente
- Vídeo pela internet tocar no console (player do sistema só abre MP4 H.264 local)
- YouTube Music com autenticação
- Rádio dentro do plugin GoldHEN

---

## Estrutura do projeto

```
src/
  core/        JSON, URL, contratos HTTP e Sockets
  net/         HTTP/1.1 + TLS próprios (Mbed TLS), RangeReader
  storage/     JsonStore (escrita atômica)
  addons/      Protocolo Stremio + SourceManager
  library/     Favoritos, histórico, progresso
  music/       Biblioteca MP3, ID3, fila, Subsonic, Archive, Audius, Jamendo...
  live/        Parser M3U
  radio/       Radio Browser, estações, favoritas
  player/      MediaPlayer, AudioPlayer (MP3), mp3_core (C)
  ui/          Canvas (software), Font, widgets, teclado, tema
  app/         App (shell/navegação) + telas + updater + remote
  platform/    platform.hpp | host/ (PC) | ps4/ (PS4)
tests/         Testes unitários (framework próprio)
docs/          ROADMAP.md, COMPATIBILITY.md, BUILD_GUIDE.md
tools/         make_pkg.sh, make_icon.py, publish_update.sh
third_party/   mbedtls, minimp3, stb, qrcodegen
```

---

## Como buildar

### PC (testes)
```bash
make test          # build + todos os testes
make shots         # capturas de tela em out/shots
```

### PS4 (PKG)
```bash
OO_PS4_TOOLCHAIN=/caminho/openorbis GOLDHEN_SDK=/caminho/goldhen make pkg
# PKG gerado em out/Moonlight-HB_01.35.pkg
```

### Publicar atualização OTA
```bash
tools/publish_update.sh "descrição da versão"
```

---

## Dependências principais

| Biblioteca | Versão | Licença |
|---|---|---|
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) | 3.6.4 | Apache 2.0 |
| [minimp3](https://github.com/lieff/minimp3) | — | CC0 |
| [stb_truetype](https://github.com/nothings/stb) | — | Domínio público |
| [qrcodegen](https://github.com/nayuki/QR-Code-generator) | — | MIT |
| Outfit (fonte) | — | OFL |

---

## Regras do projeto

- Sem DRM bypass, sem P2P/torrent para conteúdo de terceiros
- Sem credenciais no código, sem tokens em logs
- Fontes de conteúdo devem ser serviços que você está autorizado a usar
- Sem exceções C++ (`-fno-exceptions`); erros por valor de retorno
- Nada de rede na thread da interface

Detalhes completos em [`CLAUDE.md`](CLAUDE.md) e [`docs/ROADMAP.md`](docs/ROADMAP.md).

---

## Licença

Código próprio: MIT. Dependências: ver tabela acima.
