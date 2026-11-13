# Guia de compilação

## 1. PC (testes e capturas de tela)

Requisitos: CMake ≥ 3.20, g++ ou clang++ com C++17, Python 3 (servidor de teste).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ML_FIXTURES=$PWD/tests/fixtures ./build/moonlight_tests        # todos os testes
tools/run_live_test.sh                                          # inclui o teste com servidor HTTP local
```

Capturas de tela com o controle simulado:

```bash
mkdir -p /tmp/ml/sb /tmp/ml/shots
./build/moonlight_host /tmp/ml/sb assets /tmp/ml/shots "shot:home left down down right shot:filmes x shot:detalhe"
```

Passos do script: `up down left right x o square triangle l1 r1 l2 r2 options r3`, `wait:MS`, `shot:NOME`.

## 2. PS4

Requisitos:
- OpenOrbis PS4 Toolchain (`OO_PS4_TOOLCHAIN`), com `bin/linux/{create-fself,create-gp4,PkgTool.Core}`
  e `src/modules/{libc.prx,libSceFios2.prx,right.sprx}`.
- clang/lld 18 (testado com Ubuntu clang 18.1.3).
- GoldHEN Plugins SDK compilado (`GOLDHEN_SDK`, com `libGoldHEN_Hook.a`) — opcional; sem ele o app não lê pendrive.
- Git + acesso ao GitHub (para baixar o LLVM uma vez).

```bash
export OO_PS4_TOOLCHAIN=/caminho/oosdk
export GOLDHEN_SDK=/caminho/GoldHEN_Plugins_SDK
toolchain/build-libcxx.sh                       # uma vez: gera toolchain/libcxx
cmake -B build-ps4 -DCMAKE_TOOLCHAIN_FILE=cmake/ps4-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-ps4 -j
tools/make_pkg.sh build-ps4/moonlight out        # -> out/Moonlight-HB_01.00.pkg
```

Ou, com as variáveis definidas: `make ps4 pkg`.

## 3. Instalar no console

1. Copie `Moonlight-HB_01.00.pkg` para a raiz de um pendrive exFAT/FAT32 (ou para `/data/pkg` por FTP).
2. No PS4: GoldHEN → Package Installer → escolha o PKG.
3. Músicas: `/data/moonlight/music/<playlist>/*.mp3` ou pendrive `Music/<playlist>/*.mp3`.
4. Canais: `/data/moonlight/channels.m3u` (lista M3U autorizada).
