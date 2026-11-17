# REA Investigation Report

**Data:** 2026-11-16  
**Versão do REA:** 6.9.0 (rea-agents@6.9.0)  
**Node.js:** v22.23.4

## Preparação e instalação

### Verificação do ambiente
- Node.js 22.23.4 ✅ (mínimo exigido: 22.23.4)
- npm 10.9.9 ✅
- Java 21.0.12.1 ✅ (pré-requisito para Ghidra)

### Instalação
```bash
npx rea-agents@6.9.0 setup --client claude_code --yes
```

**Status:** `healthy: true`  
Arquivos criados:
- Registro MCP em `/root/.claude.json` (backup em `/root/.claude.json.rea.backup`)
- Skill instalada em `/root/.claude/skills/reverse-engineer-anything`

### Providers disponíveis

| Provider | Status | Motivo |
|---|---|---|
| Ghidra | ❌ não configurado | `GHIDRA_INSTALL_DIR` não definido; Ghidra não instalado |
| IDA | ❌ não configurado | `REA_IDA_MCP_CONFIG` não definido |
| JADX | ❌ não configurado | `REA_JADX_MCP_JAR` não definido |
| Kuna | ❌ não configurado | `REA_KUNA_COMMAND` não definido |

**Consequência:** análise de binários nativos (ELF PS4, executáveis) não está disponível nesta sessão. As capacidades de análise web (captura de tráfego, JavaScript, AgentEnv) estão disponíveis.

### Capacidades disponíveis sem providers nativos
- `analyze_javascript_application` — análise de código JavaScript/Electron extraído
- `capture_browser_scenario` — captura de tráfego de SPA via browser conectado
- `export_environment_bundle` — exportação de AgentEnv
- `inspect_web_network_capture` — análise de capturas HAR/SAZ
- `inspect_wasm_artifact` — requer WABT
- `inspect_sqlite_database` — análise de banco SQLite
- `inspect_binary_hardening` / `inspect_elf_notes` / `inspect_go_binary` — análise estática sem disassembler

## Investigações realizadas

### 1. Auditoria do Moonlight (Fase A)

Arquivos principais mapeados:

| Arquivo | Linhas | Função |
|---|---|---|
| `src/music/catalog.hpp` | 27 | Struct `Song` (url, title, artist, album, genre, art, source, durationSec, track) + `SongSource` (definido, não usado) |
| `src/music/archive.cpp/hpp` | 218 | Internet Archive: searchUrl, parseSearch, parseItem, coverUrl, metadataUrl |
| `src/music/audius.cpp/hpp` | 91 | Audius: searchUrl, trendingUrl, streamUrl, parseTracks |
| `src/music/subsonic.cpp/hpp` | 230 | Subsonic/Navidrome: busca na UI thread (problema conhecido) |
| `src/music/taste.cpp/hpp` | 167 | Histórico local: últimas 30 músicas, contagens por gênero/artista |
| `src/app/multi_fetch.cpp/hpp` | 182 | Requisições JSON paralelas em threads, sem bloquear UI |
| `src/app/catalog_loader.cpp/hpp` | 135 | Carregamento de catálogos de addons em background |
| `src/net/web_client.cpp/hpp` | 316 | HTTP/1.1 + TLS (Mbed TLS); `RangeReader` para MP3 progressivo |
| `src/net/range_reader.cpp/hpp` | 155 | Range requests para streaming de áudio |
| `src/player/player.cpp/hpp` | 321 | AudioPlayer (minimp3), VideoPlayer (stub) |

**Arquitetura atual de música:**
1. `screens_music.cpp` chama `audius::searchUrl()` / `archive::searchUrl()` diretamente
2. `MultiFetch::start()` lança threads para buscar o JSON
3. `parseTracks()` / `parseSearch()` → vetor de `catalog::Song`
4. `AudioPlayer::openStream(url)` → `RangeReader` → minimp3
5. `taste.notePlay()` → persiste em `music_taste.json`

**Problemas conhecidos:**
- `SongSource` está definido em `catalog.hpp` mas não é usado — a tela chama os módulos diretamente
- `subsonic::search` ainda bloqueia a UI thread
- Sem ranking de relevância: resultados na ordem que a API retorna

### 2. Análise de APIs de fontes musicais (Fase D)

Ver `docs/MOONLIGHT_MUSIC_RESEARCH.md` para detalhes completos.

## Próximos passos com REA

Para análise binária do eboot do PS4 (CE-34878-0, ELF sections, imports):
```bash
export GHIDRA_INSTALL_DIR=/opt/ghidra_12.x  # após instalar Ghidra
npx rea-agents@6.9.0 setup --client claude_code  # re-executar setup
```

Para captura de tráfego do Audius web app:
- Requer browser conectado (`list_browser_targets`)
- Capturar `/v1/tracks/search` e `/v1/tracks/{id}/stream` com `capture_browser_scenario`
- Verificar se stream faz redirect 302 e para qual CDN

Para análise do eboot.bin atual do Moonlight:
```bash
npx -y rea-agents@6.9.0 analyze-javascript-application /caminho/para/app --json
# (não aplicável; usar open_binary com Ghidra para ELF nativo)
```
