# Compatibilidade — o que foi verificado e onde

Legenda: **PC** = testado no computador (Linux, testes automáticos). **PS4** = testado no console.
Resultados do console estão na tabela "Testes no console".

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
| Rede HTTP/HTTPS própria (sockets libSceNet + Mbed TLS 3.6.4) | sim | sim (servidores locais HTTP e TLS 1.2/1.3, certificados de teste) | não (01.09) | Substitui libSceHttp/libSceSsl, que no console recusaram todo HTTPS com `0x8095F00C`. Verificação de cadeia, datas e nome sempre ligada; 25 raízes Mozilla embutidas. Números de opções de socket e `sceRandomGetRandomNumber` tirados do shadPS4 (fatos de interface). |
| Controle pelo celular (servidor na rede local, PIN) | sim | sim (teste de ponta a ponta no PC) | não (01.09) | Porta 8080 (ou seguinte), desligado por padrão; 5 PINs errados bloqueiam por 60 s. |
| Atualização automática (`update.json` no branch `claude/updates`, ou GitHub Releases → `/data/pkg`; instalação pelo GoldHEN desde 01.11) | sim | busca, download e conferência sha256 testados no PC contra servidor local; feed publicado conferido com curl | não (01.10) | Instalação por `sceBgftServiceIntDownloadRegisterTaskByStorageEx`, fluxo do homebrew PS4-4PT (só fatos). Por cima da versão instalada exige remover antes (opção explícita); o PKG fica em `/data/pkg` para o GoldHEN Package Installer. |
| Vídeo pela internet via cliente próprio (callbacks de arquivo do AvPlayer + Range) | sim | leitura por Range testada no PC | não (01.09) | HLS (`.m3u8`) ainda vai pelo player do sistema. Formato continua limitado ao que o AvPlayer decodifica (MP4 H.264/AAC); MKV/HEVC provavelmente não. |
| Rádios (Radio Browser + MP3 ao vivo) | sim | sim | não | Só estações MP3 (minimp3); AAC/HLS ficam de fora. Streams de rádio aceitam `http://` (só áudio é decodificado); a API do diretório exige HTTPS. Servidores SHOUTcast antigos respondem `ICY 200`; no PC aceitamos, no PS4 depende do sceHttp. |
| Vídeo (libSceAvPlayer + NV12→ARGB por CPU) | sim | conversão NV12 testada (7,6 ms/quadro 1080p no PC) | não | Experimental. Declarações da ABI escritas a partir da documentação do emulador shadPS4 (só fatos de interface), tamanhos conferidos com `static_assert`. Memória de quadro em memória direta. O player mostra em que passo parou (△). |
| SDL2 | não usado | — | — | Não disponível no toolchain construído do código-fonte. |
| FreeType do sistema | não usado | — | — | stb_truetype no lugar. |

## Testes no console

| Versão | Resultado relatado |
|---|---|
| 01.02 | PKG instala e abre. A tela ficava presa na imagem de abertura (`pic1.png`); a interface só aparecia por um instante ao fechar o app. Causa provável: faltava `sceSystemServiceHideSplashScreen()`. Controle, áudio e rede: não deu para avaliar. |
| 01.03 | Correção acima + redesenho forçado nos primeiros quadros. **Testado no PS4: interface aparece e navega ("funciona tudo").** Filmes/séries mostram só a demonstração, como esperado. |
| 01.04 | Player de vídeo experimental (libSceAvPlayer). **Aguardando teste.** |
| 01.05 | Pôsteres reais das fontes, importar fontes por arquivo no pendrive. Relato: importar "não abriu" (arquivo provavelmente com outro nome/extensão dupla). |
| 01.06 | **Testado no PS4:** teclado do sistema (IME) abre normalmente; músicas de `/data/devhub/Music` aparecem. **Falha:** app indica "sem internet" com o console conectado; fontes não importam. |
| 01.07 | **Testado no PS4:** a internet inicia ("Internet do PS4 iniciada"), mas todo HTTPS falha no envio com `0x8095F00C` (example.com e as 4 fontes do usuário). Causa provável: lista de certificados raiz desatualizada no firmware (mesmo erro relatado por outro homebrew). |
| 01.08 | 25 raízes atuais adicionadas com `sceHttpsLoadCert`. **Testado no PS4:** "25 certificados" carregados, data do console correta, HTTP comum responde (~700 bytes), mas **todo HTTPS continua falhando com `0x8095F00C`** (example.com, fontes, rádios, filmes). Conclusão: o TLS do sistema nesse firmware não serve. |
| 01.09 | HTTP/TLS próprios (Mbed TLS sobre sockets do sistema), controle pelo celular, vídeo pela internet lido pelo cliente próprio. **Aguardando teste** (o usuário ainda estava no 01.08 ao relatar que o vídeo não abre). |
| 01.10 | Filmes/Séries em fileiras (uma por catálogo da fonte — muitas fontes nomeiam por serviço, ex. "Netflix" — e uma por gênero quando o catálogo oferece filtro), carregadas em segundo plano. Atualização pelo GitHub Releases (procura ao abrir, baixa para `/data/pkg`, instala via serviço BGFT — experimental). Endereços `http://` escolhidos pelo usuário são aceitos. **Testado no PS4: fechou logo ao abrir (CE-34878-0).** Causa mais provável: `sceRandomGetRandomNumber` (semente do Mbed TLS) chamado sem carregar o módulo `libSceRandom`, que o sistema não carrega sozinho — o mesmo valeria para o 01.09, nunca instalado. |
| 01.11 | Carrega `ORBIS_SYSMODULE_RANDOM` antes do primeiro uso; instalador automático (BGFT) retirado do binário; NetCtl só quando o controle pelo celular precisa; busca de atualização 6 s depois de abrir; **diagnóstico de abertura**: o app grava o passo arriscado em `startup.txt` e, se fechar, mostra na abertura seguinte onde parou e abre sem os recursos automáticos. **Aguardando teste.** |

## Desempenho

Quadro completo 1920x1080 por software: **7,2 ms no PC** (Xeon 2,3 GHz, 1 thread) depois do cache de arte
e de fundo (antes: 33,5 ms). O Jaguar do PS4 (1,6 GHz) deve ser algumas vezes mais lento: estimativa de
20–35 ms por quadro, ou seja, ~30 fps durante animações — **estimativa, não medição**. O app só redesenha
quando algo muda.

## Firmware / instalação

Firmware do console não informado. O PKG é um "fake PKG" homebrew (PAID 0x3800000000000011) instalado pelo
**GoldHEN → Package Installer** (pendrive ou `/data/pkg`). Nenhuma alteração de firmware é feita.
