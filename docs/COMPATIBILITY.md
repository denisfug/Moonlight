# Compatibilidade — o que foi verificado e onde

Legenda: **PC** = testado no computador (Linux, testes automáticos). **PS4** = testado no console.
Nada deste projeto foi testado no PS4 ainda.

| Componente | Compila PS4 | PC | PS4 | Observação |
|---|---|---|---|---|
| libc++ 18.1.8 (static, sem exceções/localização) | sim | — | não | Construída por `toolchain/build-libcxx.sh`. Um programa C++17 de teste (vector, string, map, function, unique_ptr, dynamic_cast) linka sem símbolos pendentes. |
| Startup C++ | sim | — | não | Binário sem `.init_array` e sem `__cxa_guard` (verificado com `nm`/`readelf`). |
| VideoOut 1920x1080 ARGB linear, 2 buffers | sim | — | não | Mesma sequência dos exemplos OpenOrbis. |
| Controle (scePad, usuário inicial) | sim | via script | não | Leitura por polling: o mesmo método funcionou no nosso plugin no console. |
| AudioOut 48 kHz S16 estéreo, 1024 quadros | sim | sim (sink) | não | Formato já validado no console pelo plugin de música. |
| Arquivos via libkernel + Getdents | sim | sim (POSIX) | não | Mesmo código-base que funcionou no DevHub. |
| GoldHEN `sys_sdk_jailbreak` (USB/`/data`) | sim (se `GOLDHEN_SDK`) | — | não | Chamado só depois de carregar as fontes de `/app0` (depois do jailbreak `/app0` fica inacessível — achado de testes anteriores). |
| Threads (`scePthreadCreate`) | sim | std::thread | não | |
| Rede HTTP/HTTPS (libSceNet/Ssl/Http) | sim | http local | não | Ordem de inicialização do exemplo oficial `net_http`, mas **sem** o callback do exemplo que aceita qualquer certificado. Se a inicialização falhar o app avisa e segue sem internet. Não sabemos ainda se continua funcionando depois do `sys_sdk_jailbreak`. |
| Rádios (Radio Browser + MP3 ao vivo) | sim | sim | não | Só estações MP3 (minimp3); AAC/HLS ficam de fora. Streams de rádio aceitam `http://` (só áudio é decodificado); a API do diretório exige HTTPS. Servidores SHOUTcast antigos respondem `ICY 200`; no PC aceitamos, no PS4 depende do sceHttp. |
| Vídeo | **não implementado** | — | não | Ver `MEDIA_ENGINE.md`. |
| SDL2 | não usado | — | — | Não disponível no toolchain construído do código-fonte. |
| FreeType do sistema | não usado | — | — | stb_truetype no lugar. |

## Desempenho

Quadro completo 1920x1080 por software: **7,2 ms no PC** (Xeon 2,3 GHz, 1 thread) depois do cache de arte
e de fundo (antes: 33,5 ms). O Jaguar do PS4 (1,6 GHz) deve ser algumas vezes mais lento: estimativa de
20–35 ms por quadro, ou seja, ~30 fps durante animações — **estimativa, não medição**. O app só redesenha
quando algo muda.

## Firmware / instalação

Firmware do console não informado. O PKG é um "fake PKG" homebrew (PAID 0x3800000000000011) instalado pelo
**GoldHEN → Package Installer** (pendrive ou `/data/pkg`). Nenhuma alteração de firmware é feita.
