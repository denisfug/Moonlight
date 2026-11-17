# Protocolo de fontes (Stremio Addon Protocol) — implementação do cliente

Referências: <https://stremio.github.io/stremio-addon-sdk/protocol.html> e `docs/api/responses/*.md` do
stremio-addon-sdk (MIT). Implementado a partir da documentação; nenhum código do SDK foi copiado.

## O que está implementado (`src/addons/`)

| Item | Estado | Teste |
|---|---|---|
| `manifest.json`: id, version, name, description, types, idPrefixes, resources (forma curta e objeto), catalogs (+extra, isRequired, options, extraSupported/extraRequired legados), behaviorHints (adult, p2p, configurable, configurationRequired) | feito | `protocol_manifest` |
| Regra de roteamento `supports(resource, type, id)` (types + idPrefixes por recurso) | feito | `protocol_manifest` |
| Rotas `/{resource}/{type}/{id}.json` e `/{resource}/{type}/{id}/{extra}.json` com percent-encoding | feito | `protocol_paths` |
| `catalog` → `metas` (MetaPreview) | feito | `protocol_responses` |
| `meta` → MetaDetail + `videos` (temporada/episódio ordenados, especiais por último) | feito | `protocol_responses` |
| `stream` → url / ytId / infoHash+fileIdx / externalUrl, name, description (title legado), subtitles, behaviorHints (notWebReady, bingeGroup, proxyHeaders.request) | feito | `protocol_responses` |
| `subtitles` → id, url, lang, label | feito | `protocol_responses` |
| SourceManager: adicionar (valida manifest), remover, ativar/desativar, atualizar, persistir, catálogos navegáveis, busca, meta (primeira fonte que responde), streams (agregados), legendas | feito | `sources_*` |
| Teste contra servidor HTTP local real | feito (PC) | `sources_live_local_server` |

## Reprodução de streams

Só `url` http(s) é considerado reproduzível (`Stream::playableHere`). `infoHash` (P2P/torrent) é lido mas
**não é suportado**: o Moonlight não tem motor P2P e não vai ter um para conteúdo de terceiros.
`ytId` e `externalUrl` aparecem como opções não suportadas. O player de vídeo ainda não existe
(`docs/MEDIA_ENGINE.md`).

## Segurança

- Só `http://`/`https://`; URLs com `usuario:senha@`, espaços ou caracteres de controle são rejeitadas.
- HTTPS obrigatório para servidores remotos; HTTP só para rede local (127/8, 10/8, 172.16/12, 192.168/16)
  ou com permissão explícita por fonte (`allowPlainHttp`).
- Limites: manifest 512 KiB, demais respostas 4 MiB, JSON com profundidade 64, timeout 15 s.
- URLs são exibidas com `redactUrl` (segmentos longos e query string escondidos: addons costumam colocar
  chaves de configuração no caminho).
- Respostas são apenas dados: nada é executado, nenhum campo controla navegação ou comandos do app.
- Fonte que declara `p2p` recebe aviso na tela de Fontes.

## Pendente

- Cliente HTTP/HTTPS no PS4 (`sceHttp` + `sceSsl`): sem ele, no console as fontes não carregam (o app avisa).
- Paginação (`skip`), filtros de gênero enviados ao addon (hoje o filtro de gênero é local).
- `addon_catalog`, `meta.links`, `trailers`, eventos de player/biblioteca do protocolo.
- Carregar imagens (`poster`, `background`, `logo`) — exige rede + decodificação (stb_image já está no repo).
- Requisições em thread separada (hoje são síncronas; no PC são instantâneas, no PS4 falham na hora).

## Música

O protocolo Stremio **não** é usado para música. O módulo `music/` tem modelo próprio (Track/Playlist/Queue).
Uma extensão musical futura precisa de um contrato próprio documentado aqui antes de ser implementada.
