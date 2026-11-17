# Moonlight Music — Pesquisa de Fontes e Arquitetura

**Data:** 2026-11-16  
**Objetivo:** Transformar o Moonlight Music em catálogo pesquisável online (estilo YouTube Music),  
sem biblioteca pessoal, sem servidor pessoal, sem mensalidades obrigatórias.

---

## Resumo executivo

Nenhuma fonte disponível no mercado oferece catálogo completo de gravadoras com streaming gratuito
e irrestrito para apps de terceiros. As opções reais se dividem em:

1. **Catálogo independente completo** (Audius, Jamendo): músicas de artistas independentes,
   tocam inteiras, gratuitas — mas não têm Kanye, Coldplay, etc.
2. **Catálogo mainstream com prévia** (Deezer): acha qualquer artista famoso, mostra capa e
   metadados oficiais, mas toca só 30 segundos.
3. **Catálogo de arquivo** (Internet Archive): público-domínio e CC, qualidade variável,
   sem artistas mainstream.

A estratégia recomendada é combinar os três: usar Deezer como catálogo de metadados/capas,
e tentar reproduzir a faixa inteira pelo Audius ou Archive quando houver correspondência.

---

## Fontes investigadas

### 1. Audius ✅ JÁ INTEGRADO

**O que é:** Plataforma descentralizada de música independente. Artistas publicam diretamente.

**API:**
- Base: `https://api.audius.co/v1`
- Autenticação: apenas `app_name=MoonlightHB` (sem chave)
- Endpoints:
  - `GET /v1/tracks/search?query=&limit=30&app_name=MoonlightHB`
  - `GET /v1/tracks/trending?time=week&limit=40&app_name=MoonlightHB`
  - `GET /v1/tracks/{id}/stream?app_name=MoonlightHB` → redirect 302 para CDN
  - `GET /v1/tracks/trending/underground`
  - `GET /v1/playlists/trending`
  - `GET /v1/users/{id}/tracks?sort=plays`
  - `GET /v1/tracks/recommended?genre=&time=month&exclusion_list=`
  - `GET /v1/search/full?query=` → retorna tracks + users + playlists + albums em um request

**Campos de resposta de faixa:** `id`, `title`, `user.name`, `user.handle`, `genre`, `mood`,
`tags`, `duration`, `artwork` (150x150, 480x480, 1000x1000), `is_streamable`, `is_stream_gated`,
`is_delete`, `access.stream`, `play_count`, `release_date`, `remix_of`, `track_cid`

**Stream:** redirect 302 para nó de conteúdo Audius. Aceita `Range` header.
Com `no_redirect=true`: retorna JSON com campo `data` contendo a URL direta.

**Capas:** campo `artwork` com três tamanhos. Usar `480x480` para cards, `1000x1000` para player.

**Catálogo:** somente música independente. Artistas mainstream NÃO estão disponíveis.

**Limite:** sem limite documentado para leitura pública.

**PS4:** stream MP3 via redirect funciona com o `RangeReader` existente.
Testado no console: busca retorna resultados (confirmado em 01.30). Stream não verificado no PS4.

**Implementação atual:** `src/music/audius.cpp/hpp` — funcional.

---

### 2. Internet Archive ✅ JÁ INTEGRADO

**O que é:** Arquivo digital público. Música pública-domínio, Creative Commons, gravações ao vivo.

**API:**
- Base: `https://archive.org`
- Autenticação: nenhuma
- Busca: `GET /advancedsearch.php?q=(title:(q) OR creator:(q)) AND mediatype:(audio OR etree) AND format:("VBR MP3")&fl[]=identifier&fl[]=title&fl[]=creator&sort[]=downloads desc&rows=30&output=json`
- Metadados: `GET /metadata/{id}`
- Download: `GET /download/{id}/{filename}` → redirect para servidor de dados
- Capa: `GET /services/img/{id}`

**Catálogo:** sem artistas mainstream. Forte em jazz antigo, blues, gravações históricas,
shows ao vivo (Grateful Dead, etc.), audiobooks, música clássica.

**Stream:** MP3 direto com `Range` support. Testado e funcionando no PS4 (01.28).

**Implementação atual:** `src/music/archive.cpp/hpp` — funcional.

---

### 3. Jamendo 🆕 RECOMENDADO PARA PRÓXIMA FASE

**O que é:** Plataforma de música Creative Commons. +700.000 faixas de artistas independentes.
Foco em pop, rock, eletrônica, ambient.

**API:**
- Base: `https://api.jamendo.com/v3.0`
- Autenticação: `client_id` obrigatório (obtido grátis em devportal.jamendo.com)
- Busca de faixas: `GET /tracks/?client_id=ID&search=QUERY&format=json&limit=50&imagesize=600&audioformat=mp32`
  - `search`: busca em todos os campos
  - `namesearch`: busca só por título (mais preciso para busca exata)
  - `artist_name`: filtro por nome do artista
  - `order=popularity_total_desc`: ordena por popularidade
- Stream de faixa: `GET /tracks/file/?client_id=ID&id=TRACK_ID&action=stream&audioformat=mp32`
  → redirect HTTP para URL do MP3 (VBR)
  - Se `audiodownload_allowed=false`, retorna 404 (tratar esse caso)
- Formato `mp32` = VBR MP3 (melhor qualidade, funciona com minimp3)

**Campos de resposta de faixa:** `id`, `name` (título), `artist_name`, `album_name`, `album_id`,
`duration`, `audio` (URL direta do MP3), `audiodownload` (URL para download), `audiodownload_allowed`,
`image` (capa), `album_image`, `shareurl`, `license_ccurl`, `tags`, `position`

**Capas:** campo `image`, com tamanho definido por `imagesize=600` (25–600 px).
Para álbum: `album_image`. Para singles: `image` está disponível, `album_*` fica vazio.

**Limite:** 500.000 hits/mês antes de precisar contato. Muito acima do uso esperado.

**Licença do conteúdo:** Creative Commons (varia por faixa: CC-BY, CC-BY-SA, CC-BY-NC etc.)
O app pode reproduzir qualquer faixa; atribuição não é necessária na interface mas é boa prática.

**Vantagem sobre Audius:** catálogo Jamendo tem mais música "ouvível" mainstream-adjacent
(pop, rock, eletrônica de artistas sérios), melhor para "descobrir músicas novas".
Busca por nome/artista é mais precisa com `namesearch`.

**Implementação necessária:**
1. Criar conta em devportal.jamendo.com e obter `client_id` gratuito
2. Implementar `src/music/jamendo.cpp/hpp` seguindo o padrão archive/audius
3. `client_id` configurável em Settings (nunca hardcoded — regra do CLAUDE.md)
4. Ou: usar `client_id` público de teste `709fa152` para desenvolvimento/demo

**Integração PS4:** MP3 VBR com redirect — idêntico ao Archive, que já funciona.

---

### 4. Deezer 🆕 RECOMENDADO PARA CATÁLOGO/METADADOS

**O que é:** Serviço de streaming comercial francês. +120 milhões de faixas incluindo
todo catálogo mainstream (Universal, Sony, Warner).

**API pública (sem chave):**
- Base: `https://api.deezer.com`
- Busca: `GET /search?q=QUERY&limit=25`
- Busca de álbum: `GET /search/album?q=QUERY`
- Busca de artista: `GET /search/artist?q=QUERY`
- Autenticação: **nenhuma para busca e metadados** (confirmado)

**Campos de resposta de faixa:** `id`, `title`, `title_short`, `rank`, `explicit_lyrics`,
`duration` (segundos), `preview` (URL MP3 30s), `artist.name`, `artist.picture_medium`,
`album.title`, `album.cover_medium` (600x630), `album.cover_xl` (1000x1000)

**Preview:** campo `preview` = URL MP3 direta, 30 segundos, sem autenticação, sem redirect.
Formato: `https://cdns-preview-X.dzcdn.net/stream/c-HASH-1.mp3`

**Capa:** `album.cover_xl` para cards grandes; `album.cover_medium` para listas.
URLs diretas sem autenticação.

**Stream completo:** ❌ Não disponível para apps de terceiros. Exige OAuth + conta paga.
A faixa inteira só é acessível via app oficial Deezer.

**Rate limit:** não documentado para a API pública. Na prática, sem autenticação o limite
é mais restritivo — recomendado não exceder ~5 req/s.

**Estratégia recomendada para o Moonlight:**
- Usar Deezer como **fonte de catálogo e metadados**: busca, capas, títulos corretos
- Tocar o `preview` (30s) com indicador visual "prévia"
- Quando o usuário quiser a faixa inteira: procurar automaticamente por correspondência
  no Audius, Jamendo ou Archive e tocar de lá se encontrar

**Implementação necessária:**
1. `src/music/deezer.cpp/hpp` com `searchUrl`, `parseSearch`, `coverUrl`, `previewUrl`
2. Sem credencial necessária (campo `source="Deezer"`)
3. Integração PS4: MP3 direto sem redirect — mais simples que Archive

---

### 5. SoundCloud ⚠️ POSSÍVEL MAS COM BARREIRAS

**O que é:** Maior plataforma de upload independente. Mix de artistas independentes
e releases oficiais de gravadoras.

**API oficial:**
- Requer `client_id` (OAuth 2.1 client credentials)
- Obtenção: soundcloud.com/you/apps (pode exigir conta Artist Pro para aprovação automática)
- Endpoints: `GET /tracks?q=&access=playable&limit=50`
- Stream: HLS AAC (`.m3u8`), não MP3 — **incompatível com minimp3**
- Preview: campo `preview_mp3_128_url` (30s, MP3 128kbps)
- Faixas de gravadora: `access=preview` (trecho), não `playable`

**Problema técnico crítico:** o stream completo do SoundCloud é HLS (HTTP Live Streaming)
com segmentos AAC. O Moonlight só decodifica MP3 (minimp3). Adicionar HLS + AAC exigiria:
- Parser M3U8 de HLS
- Decodificador AAC (libFDK-AAC ou similar)
- Gestão de segmentos (cada segment = ~10s)
- **Estimativa:** 2–4 semanas de trabalho só para o player

**Recomendação:** adiar até que HLS/AAC seja necessário por outra razão.
O preview MP3 de 30s funcionaria se a credencial for obtida, mas sem vantagem
sobre o Deezer que não exige credencial.

---

### 6. Spotify / Apple Music / Tidal / YouTube Music ❌ INVIÁVEL

**Motivo:** DRM obrigatório (Widevine / FairPlay). Stream só funciona através dos
players e SDKs oficiais de cada plataforma. Nenhuma alternativa técnica legítima existe.
Implementar bypass de DRM viola o CLAUDE.md e os termos de cada serviço.

---

## Arquitetura recomendada

### Modelo unificado (proposta `src/music/provider.hpp`)

```cpp
// Capacidades declaradas por cada provider (sem construtores estáticos)
struct Capabilities {
    bool search, albums, artists, trending, recommend;
};

// Provider como POD de ponteiros de função — sem estado, sem construtor
struct Provider {
    const char *id, *name;
    Capabilities caps;
    bool needsKey;  // precisa de credencial em Settings?
    
    // Constrói URLs; parsing; sem chamadas de rede
    std::string (*searchUrl)(const std::string &q, const std::string &base);
    std::string (*trendingUrl)(const std::string &genre, const std::string &base);
    std::string (*albumUrl)(const std::string &id, const std::string &base);
    
    // Parse: retorna Songs; base permite substituição em testes
    std::vector<catalog::Song> (*parseSearch)(const Json &, const std::string &base);
    std::vector<catalog::Song> (*parseAlbum)(const Json &, const std::string &base);
};

// Tabela estática, sem inicialização dinâmica
extern const Provider *const kProviders[];
extern const size_t kProviderCount;
```

### Tabela de providers

| ID | Nome | Busca | Stream completo | Capa | Precisa chave |
|---|---|---|---|---|---|
| `audius` | Audius | ✅ faixas independentes | ✅ MP3 redirect | ✅ 480x480 | ❌ |
| `archive` | Internet Archive | ✅ arquivo histórico | ✅ MP3 redirect | ✅ por item | ❌ |
| `deezer` | Deezer | ✅ catálogo mainstream | ❌ só 30s | ✅ cover_xl | ❌ |
| `jamendo` | Jamendo | ✅ indie/CC | ✅ MP3 VBR | ✅ 600px | ✅ client_id |

### Sistema de ranking (`src/music/rank.*`)

Inspirado no Rheoson (Apache-2.0, não copiado — reimplementação independente):

1. **Normalizar** query e campos: lowercase, remover acentos básicos, strip `(feat.)`, `[live]`, etc.
2. **Score por faixa:**
   - Título exato: 1.0
   - Título substring bidirecional: 0.9
   - Título overlap (token set): `0.72 * title_score + 0.28 * artist_score`
   - Artista sem match: penalidade 0.3
3. **Filtrar:** descartar resultados com score < 0.4 (evita "resultados aleatórios")
4. **Deduplicar:** mesmo título+artista normalizado → manter o de melhor source
5. **Diversidade em recomendações:** máx 3 faixas por artista por shelf

### Seções de busca (tela)

1. **"Melhor resultado"** — 1 card grande, o de maior score entre todos os providers
2. **"Músicas"** — lista com capa, título, artista, fonte, duração, ícone de preview se Deezer
3. **"Álbuns"** — grade (via Archive ou Deezer album search)
4. **"Artistas"** — via Audius `/v1/users/search`
5. Se nenhum resultado com score > 0.4: "Nenhum resultado encontrado para X"

---

## Plano de implementação por etapas

### E1 — Ranking + Deezer como catálogo (risco baixo, impacto imediato)
- `src/music/deezer.cpp/hpp`: searchUrl, parseSearch (campo `preview`), coverUrl
- `src/music/rank.cpp/hpp`: normalização + score (testável com fixtures)
- Tela: seção "Melhor resultado" + ícone "prévia" em faixas Deezer
- Resultado: busca "God Is" acha a faixa correta com capa oficial
- **Teste PC:** fixtures + make test + make shots
- **Teste PS4:** buscar artista mainstream e verificar capa

### E2 — Jamendo (após obter client_id)
- `src/music/jamendo.cpp/hpp`
- Settings: campo para `client_id` Jamendo (digitado pelo usuário)
- Resultado: catálogo indie com boa qualidade e streams completos
- **Teste PC:** fixtures + make test

### E3 — Provider table (refactor)
- `src/music/provider.hpp` com tabela `kProviders[]`
- Mover archive/audius/deezer/jamendo para usar a interface comum
- screens_music.cpp passa a iterar a tabela em vez de chamar módulos diretamente
- Sem mudança de comportamento — refactor puro

### E4 — /search/full Audius + recomendações
- Substituir 3 requests (tracks/artists/playlists) por 1 request `/search/full`
- Adicionar `/v1/tracks/recommended?genre=&exclusion_list=` para shelf "Para você"
- **Teste PC:** fixtures do /search/full (objeto, não array — validar parser)

### E5 — Favoritos e fila
- Liked songs persistidos em JsonStore
- "Tocar próxima" / "Adicionar à fila"

---

## Questões abertas (exigem teste real no PS4)

1. **Deezer preview MP3:** a URL do campo `preview` é HTTP ou HTTPS? Tem redirect?
   O `RangeReader` existente suporta redirect? → Testar antes de lançar E1 no console.
2. **Audius stream redirect:** o PS4 segue o 302 automaticamente?
   O `web_client` trata redirects? → Verificar em `src/net/web_client.cpp`.
3. **Jamendo redirect:** idêntico ao Archive (que já funciona) → baixo risco.
4. **Cover HTTPS no PS4:** capas Deezer vêm de `dzcdn.net` via HTTPS.
   Verificar se o ca_roots.inc do Moonlight tem a raiz do CDN Deezer.

---

## Limitação fundamental (sem solução legítima)

Músicas de gravadoras (Universal, Sony, Warner) em stream completo gratuito
não estão disponíveis para apps independentes. Isso inclui a maioria dos hits.
O Moonlight pode encontrá-las (via Deezer) e mostrar uma prévia de 30s,
mas tocar a faixa inteira exige que o usuário use o app oficial do serviço.

Isso é uma limitação de mercado, não técnica. Nenhuma engenharia resolve sem violar termos.

---

## Referências

- [Audius API Docs (Postman)](https://glama.ai/mcp/servers/qmiveuezs0/blob/main/audius-postman-copypaste.txt)
- [Jamendo API v3.0 — /tracks](https://developer.jamendo.com/v3.0/tracks)
- [Jamendo API v3.0 — /tracks/file](https://developer.jamendo.com/v3.0/tracks/file)
- [Deezer API (resumo)](https://oanor.com/api/deezer-api/llms.txt)
- [REA — github.com/morluto/rea](https://github.com/morluto/rea)
- [Rheoson — referência de ranking (Apache-2.0)](https://github.com/Rheoson/Rheoson)
- [Cadence — referência de providers (MIT)](https://github.com/Cadence-refs/cadence)
