# MOONLIGHT HB

Centro multimídia homebrew para **PlayStation 4 desbloqueado (GoldHEN)**: música, filmes, séries e canais
numa interface própria para TV, navegável só pelo DualShock 4. Fontes de conteúdo seguem o
[protocolo de add-ons do Stremio](https://stremio.github.io/stremio-addon-sdk/protocol.html) e devem ser
**serviços e servidores que você está autorizado a usar**. Não há contorno de DRM nem motor P2P.

> Estado: **versão 01.16 de desenvolvimento.** A interface foi validada no PS4 (01.03); player de vídeo experimental. Compila e gera PKG; testado no PC; **ainda não testado no PS4.**
> Vídeo ainda não reproduz. A internet no console (rádios, fontes) foi implementada mas não testada. Detalhes em [`docs/ROADMAP.md`](docs/ROADMAP.md)
> e [`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md).

## O que já funciona (no PC)

- Interface 1920x1080 escura (violeta/azul) com barra lateral: Início, Música, Filmes, Séries, Canais,
  Biblioteca, Fontes, Configurações. Foco visível, rolagem suave, teclado na tela.
- Catálogo de demonstração (títulos inventados, capas geradas).
- Rádios: diretório comunitário Radio Browser (grátis, sem anúncio do app), só estações MP3, por país/gênero, busca e favoritas.
- Servidor de música próprio (Navidrome e outros compatíveis com Subsonic): suas músicas do PC pelo Wi-Fi,
  playlists, mix aleatório e busca. Configure em Configurações.
- Música: MP3 do HD (`/data/moonlight/music/<playlist>/`) ou pendrive (`Music/<playlist>/`), tags ID3,
  fila, aleatório, repetir, volume, busca.
- Fontes: adicionar por URL de `manifest.json`, ativar/desativar, atualizar, remover, catálogos, busca,
  detalhes, streams e legendas — testado contra um servidor HTTP local.
- Filmes e Séries em fileiras: uma por catálogo de cada fonte e uma por gênero (Ação, Comédia...).
- Atualização: Configurações > Atualizar o Moonlight (versões publicadas em GitHub Releases).
- Controle pelo celular: em Configurações, liga uma página na rede de casa (PIN na TV) com setas, botões,
  teclado e "Adicionar fonte".
- Canais por lista M3U (`/data/moonlight/channels.m3u`).
- Favoritos, histórico e progresso salvos em JSON.

## Controles

| Botão | Ação |
|---|---|
| D-pad / analógico esquerdo | navegar |
| X | selecionar |
| O | voltar (na raiz: vai para a barra lateral) |
| L1 / R1 | gênero, temporada, categoria, faixa anterior/próxima (Música) |
| L2 / R2 | trocar de seção (volume na Música) |
| △ | buscar (Filmes/Séries), aleatório (Música), atualizar fonte |
| □ | pausar (Música), remover fonte |
| Options | buscar música, adicionar fonte, confirmar no teclado |

## Compilar

```bash
make test                     # PC: compila e roda os testes
make shots                    # PC: gera capturas em out/shots
export OO_PS4_TOOLCHAIN=... GOLDHEN_SDK=...
make libcxx                   # uma vez
make pkg                      # PS4: out/Moonlight-HB_01.00.pkg
```

Guia completo: [`docs/BUILD_GUIDE.md`](docs/BUILD_GUIDE.md). Arquitetura: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Terceiros

| Componente | Licença | Uso |
|---|---|---|
| stb_truetype, stb_image, stb_image_write | domínio público | texto, (imagens), PNG no PC |
| minimp3 | CC0 | decodificação MP3 |
| Fonte Outfit | SIL OFL 1.1 (`assets/fonts/OFL.txt`) | tipografia |
| LLVM libc++/libc++abi 18 | Apache-2.0 com exceção LLVM | biblioteca C++ (linkada no binário PS4) |
| OpenOrbis PS4 Toolchain | GPL-3.0 (ferramentas) | compilação/empacotamento |
| Mbed TLS 3.6.4 | Apache-2.0 | HTTPS próprio (`third_party/mbedtls`) |
| QR Code generator (Nayuki) | MIT | QR code do controle pelo celular (`third_party/qrcodegen`) |
| Certificados raiz (Mozilla, via ca-certificates e certifi 2024.8.30) | MPL-2.0 | `src/net/ca_roots.inc`, raízes confiáveis do HTTPS |
| GoldHEN Plugins SDK | MIT | `sys_sdk_jailbreak` (acesso a USB/`/data`) |

Nenhum código do Stremio, pPlay, Convx ou SimpMusic foi copiado. A licença do próprio Moonlight HB ainda
precisa ser escolhida pelo dono do repositório.
