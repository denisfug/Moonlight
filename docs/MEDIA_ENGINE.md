# Motor de reprodução

## Interface (`src/player/player.hpp`)

`MediaPlayer`: `open`, `play`, `pause`, `stop`, `seek`, `setVolume`, `volume`, `getPosition`, `getDuration`,
`state`. Implementações independentes:

| Implementação | Estado |
|---|---|
| `AudioPlayer` — MP3 local (minimp3, CC0) → reamostragem 48 kHz → `AudioOut` da plataforma, em thread própria | **Funciona no PC** (teste `audio_player_mp3`: abre, toca, pausa, seek, fim, volume, áudio não-silencioso). No PS4 compila; o mesmo decodificador e o mesmo formato de AudioOut já tocaram no console dentro do nosso plugin GoldHEN, mas **este app ainda não foi testado no console**. |
| `VideoPlayer` | **Não existe motor ainda.** `open()` retorna erro explicando isso. A UI mostra a mensagem em vez de fingir que toca. |

Limitação do áudio: o seek é proporcional a bytes (exato em CBR, aproximado em VBR). A música para quando
o app fecha (o PS4 encerra o app ao abrir um jogo).

## Estudo do pPlay (github.com/Cpasjuste/pplay)

- Licença **GPL-3.0**. Usa `libcross2d` (UI/plataforma), **libmpv** + **FFmpeg** (decodificação), libass,
  freetype, fribidi, libsmb2.
- O repositório **não** traz a receita para compilar mpv/FFmpeg para PS4 (o `ffmpeg.sh` é para Switch); o
  build PS4 depende dos "portlibs" do ambiente do autor.
- Incorporar código ou linkar libmpv obrigaria o Moonlight a ser distribuído sob GPL-3 (com código-fonte).
  Isso é uma decisão do dono do projeto, não técnica.

## Caminhos candidatos (a validar no console, nesta ordem)

1. **`sceAvPlayer` do sistema** (`orbis/AvPlayer.h` existe no OpenOrbis): decodificação por hardware de
   MP4/H.264/AAC, arquivos locais e HTTP. Sem GPL. Riscos: formatos limitados (sem MKV/HEVC garantido),
   quadros saem em YUV (NV12) e precisam de conversão para o framebuffer ARGB — custo de CPU a medir.
2. **FFmpeg (LGPL, só decodificadores) compilado com nosso toolchain** + saída de vídeo por software.
   Riscos: CPU Jaguar para 1080p em software provavelmente insuficiente; precisa de hardware decode
   (`sceVideodec2`) para ser viável.
3. **libmpv/pPlay** — mais completo, mas GPL-3 e sem receita de build PS4 pública.

Plano: um executável de prova (`tools/` ou `tests/`) que toca um MP4 local com AvPlayer no console,
medindo fps e CPU. Só depois disso o `VideoPlayer` real entra no app.
