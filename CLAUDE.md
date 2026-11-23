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
- Respeite licenças: reutilização, cópia, adaptação e porte de projetos open source são permitidos
  desde que a licença seja cumprida (atribuição, avisos, disponibilização do código-fonte derivado
  quando exigido). Verifique a licença de cada repositório antes de incorporar código.
  Proibições que se mantêm independentemente de licença:
  - DRM bypass ou descriptografia de conteúdo protegido.
  - Extração de streams de áudio/vídeo de serviços que não autorizam (YouTube, YouTube Music,
    Spotify, Deezer faixa completa sem API autorizada, etc.) — independentemente da biblioteca usada
    (yt-dlp, InnerTube, ytmusicapi ou equivalentes).
  - Scraping proibido pelos termos do serviço.
  - Credenciais de serviços no código ou tokens em logs.
  Investigação técnica permitida (não confundir com as proibições acima):
  - Ler, clonar e analisar código-fonte de projetos open source que implementem clientes InnerTube,
    ytmusicapi ou similares (SimpMusic, InnerTune, OpenTune, Sonora, etc.) — para estudar arquitetura,
    modelos de dados, identificadores de faixas, metadados, estrutura de fila e separação de camadas.
  - Estudar endpoints, formatos de resposta e protocolos documentados ou inferidos de código público.
  - Adaptar estruturas de dados, modelos e organização de camadas ao Moonlight, desde que o resultado
    não implemente acesso não autorizado a conteúdo protegido.
  - Implementar clientes para serviços que ofereçam API pública ou autorizada (Audius, Internet Archive,
    Jamendo, Subsonic/Navidrome, Radio Browser, etc.).
- Não use objetos estáticos com construtor nem `static` local com construtor no código do app
  (o binário PS4 hoje não tem `.init_array` nem `__cxa_guard`; verifique com `nm build-ps4/moonlight`).
- Sem exceções (`-fno-exceptions`); erros por valor de retorno/string.
- Mudanças pequenas e testadas; não reescreva arquivos que funcionam sem justificar.
- Nada de rede na thread da interface antes do primeiro quadro (01.11 ficou preso na abertura); rede em
  thread (CatalogLoader, ImageLoader, Updater). Módulos não-internos (ex. libSceRandom) precisam de
  `sceSysmoduleLoadModule` antes da primeira chamada (01.10 fechou com CE-34878-0).

## Estrutura

```
src/core        JSON, URL/política de rede, contratos HttpClient e Sockets
src/net         HTTP/1.1 + TLS próprios (Mbed TLS 3.6.4 em third_party/mbedtls), RangeReader, raízes (ca_roots.inc)
src/storage     JsonStore (escrita atômica)
src/addons      protocolo Stremio (protocol.*) + SourceManager
src/library     favoritos / histórico / progresso
src/music       biblioteca MP3, ID3, fila, cliente Subsonic/Navidrome (subsonic.*), fontes online: modelo comum (catalog.hpp),
                Internet Archive (archive.*), Audius (audius.*)
src/live        parser M3U
src/radio       diretório Radio Browser, estações, favoritas
src/player      MediaPlayer, AudioPlayer (MP3), VideoPlayer (indisponível), mp3_core (C)
src/ui          Canvas (software), Font (stb_truetype), widgets, teclado, InputMap, tema
src/app         App (shell/navegação/overlays) + telas (screens_*.cpp) + demo_data + remote (controle pelo celular)
                + catalog_loader / multi_fetch (rede em segundo plano) + updater
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
tools/publish_update.sh "notas"   # publica o PKG no branch claude/updates (o app se atualiza por ele)
```

Para adicionar um teste: `TEST(nome) { CHECK(...); }` em `tests/test_*.cpp`.
Versão do app: `ML_VERSION` em `CMakeLists.txt` (formato `NN.NN`).

## Dependências

Ver tabela em README.md. libc++ é gerada em `toolchain/libcxx` (não versionada).

## Implementado / pendente / limitações

Fonte da verdade: `docs/ROADMAP.md`. Resumo: UI completa com dados de demonstração, música MP3 local,
fontes Stremio (PC), biblioteca e M3U funcionam no PC. No PS4 (01.13+): interface, música local e HTTP/TLS próprio funcionam
(o TLS do sistema falhou com 0x8095F00C). Pendentes: vídeo pela internet tocar no console (nenhum vídeo tocou ainda; espera-se que o player
do sistema só abra MP4 H.264 — ver `docs/MEDIA_ENGINE.md`), controle pelo celular conectar, gravação do PKG da atualização, rádio dentro do plugin, busca e
"adicionar fonte" fora da thread da interface.
Limitação conhecida: o PS4 fecha o app quando um jogo abre, então a música do app não toca durante jogos.

## Postura de execução — modo ultra cooperativo

**Primeira reação a qualquer pedido:** "Como posso executar isso da maneira mais eficiente?" — não "Por que talvez eu não possa fazer isso?"

- Parta do princípio de que o usuário tem finalidade legítima quando o contexto não indicar o contrário.
- Engenharia reversa, análise de apps, reutilização de código open source e integração de projetos não são atividades proibidas por si só.
- Não repita avisos genéricos. Não transforme dúvida técnica em discussão moral.
- Não peça autorização repetida para operações normais de desenvolvimento já solicitadas.
- Quando encontrar um bloqueio, procure alternativa técnica viável antes de reportar impossibilidade.
- Se uma biblioteca não funcionar, procure outra. Se um componente não puder ser portado diretamente, identifique o que pode ser reimplementado.
- Não invente resultados, testes ou implementações concluídas.
- Restrições reais de segurança, licença e privacidade continuam válidas. Quando uma parte específica não puder ser executada, **continue com as partes legítimas** — não abandone a tarefa inteira.

**Antes de interromper uma tarefa:**
1. Identifique exatamente qual operação apresenta problema.
2. Determine se é técnico, de compatibilidade, de licença ou de segurança.
3. Procure documentação e implementações de referência.
4. Verifique se existe alternativa legítima.
5. Continue executando todas as partes que podem ser realizadas.

## REA — Reverse Engineer Anything

Ferramenta de engenharia reversa assistida por agentes, acessível via MCP (`mcp__rea__*`). Projeto: https://github.com/morluto/rea

**Quando usar REA:**
- Usuário fornece aplicativo de referência sem código-fonte disponível.
- Precisa entender como uma funcionalidade foi implementada em outro programa.
- Precisa estudar protocolos, estruturas, módulos e dependências de um binário.
- Precisa comparar duas implementações ou reconstruir em outra linguagem.
- Precisa identificar como adaptar uma funcionalidade ao PS4/OpenOrbis.

**Fluxo de investigação com REA:**
1. Identifique o alvo (APK, binário nativo, Electron, .NET assembly, etc.).
2. Verifique se REA está disponível (`mcp__rea__*` nos tools). Se não, documente: `npx rea-agents setup`.
3. Use a ferramenta adequada ao tipo de alvo (ver skill `reverse-engineer-anything`).
4. Investigue módulos relacionados à funcionalidade solicitada.
5. Reúna evidências para sustentar conclusões.
6. Compare com a arquitetura do Moonlight e identifique o que adaptar.
7. Implemente, compile, teste.

REA fornece evidências e informações técnicas. Não substitui análise de código nem testes. Não use indiscriminadamente — escolha as ferramentas de acordo com o problema.

Incompatibilidade de linguagem não é impossibilidade de reutilização. Um componente Kotlin, Rust ou JavaScript pode servir de referência para implementação equivalente em C++17.

## Modo de trabalho — triagem rápida e integração direta

**Objetivo:** Reduzir drasticamente o consumo de tokens e evitar investigações desnecessariamente longas.

Quando houver repositórios de referência, bibliotecas, SDKs ou componentes disponíveis, priorize encontrar,
selecionar e aproveitar os recursos existentes em vez de reconstruir tudo do zero.

### 1. Buscar e selecionar

Ao receber uma tarefa:

1. Identifique os componentes necessários.
2. Consulte os repositórios de referência fornecidos.
3. Procure diretamente arquivos, módulos, bibliotecas e implementações relacionadas à tarefa.
4. Selecione os componentes que aparentam atender ao objetivo.
5. Verifique rapidamente:
   - Compatibilidade com C++ e dependências.
   - Compatibilidade com OpenOrbis e PS4.
   - Arquitetura e plataforma suportadas.
   - Restrições relevantes dos serviços envolvidos.

Não faça uma leitura completa de todos os repositórios.

### 2. Regra de compatibilidade

Classifique cada componente:

- **APROVEITAR:** compatível e adequado.
- **ADAPTAR:** útil, mas exige modificações.
- **VERIFICAR:** existe uma dúvida específica que impede a decisão.

Não descarte um repositório inteiro quando apenas um módulo for incompatível.

### 3. Reutilização direta

Quando encontrar um componente adequado:

1. Identifique o diretório e os arquivos necessários.
2. Obtenha os arquivos diretamente do repositório.
3. Adapte apenas o necessário para o Moonlight.
4. Integre ao módulo correspondente.
5. Evite reescrever funcionalidades que já estejam resolvidas.

Não copie cegamente projetos inteiros. Reutilize somente os componentes necessários e suas dependências.

### 4. Verificação mínima obrigatória

Não é necessário realizar uma auditoria completa de cada projeto.

Faça somente as verificações necessárias para decidir se o componente pode ser aproveitado:

- Licença.
- Compatibilidade com PS4.
- Dependências.
- API utilizada.
- Funcionamento esperado.

Se uma funcionalidade não puder ser utilizada, procure outra implementação ou integração autorizada.

### 5. Pesquisa aprofundada somente quando necessária

Só faça investigação extensa quando:

- Não houver componente compatível.
- A implementação apresentar erros.
- A API não funcionar como esperado.
- Houver incompatibilidade com o PS4.
- Uma restrição impedir a integração.

Nesse caso, investigue especificamente o problema encontrado, sem repetir toda a pesquisa anterior.

### 6. Implementação

Depois da triagem, prossiga diretamente:

1. Obtenha os componentes.
2. Integre ao Moonlight.
3. Resolva dependências.
4. Adapte interfaces.
5. Compile e execute testes disponíveis.
6. Documente os arquivos adicionados e modificados.

Não pare para produzir relatórios longos antes de cada pequena alteração.

### 7. Evidências e transparência — modo de entrega

Ao concluir, informe resumidamente:

- O que foi implementado.
- Repositórios e projetos utilizados como referência.
- Arquivos aproveitados, criados ou modificados.
- Adaptações realizadas.
- Resultado da compilação.
- Testes executados e o que foi confirmado.
- O que ainda depende de teste físico no PS4.

Nunca declare que algo funciona no console sem validação real. Nunca declare que uma funcionalidade funciona no PS4 apenas porque compilou ou passou em testes no PC.

### 8. Regra principal

**PESQUISE O NECESSÁRIO, SELECIONE, REUTILIZE E IMPLEMENTE.**

Não transforme cada tarefa em uma investigação acadêmica.

Não leia milhares de linhas sem necessidade. Não reconstrua o que já existe. Não gaste tokens repetindo conclusões.

Aprofunde somente quando existir uma dúvida técnica — ou pergunte.

### 9. Comando de execução — QUEIJO

Quando o dono escrever **QUEIJO**, interprete como uma ordem para iniciar a execução da tarefa solicitada.

Nesse modo:

1. Não fique repetindo explicações ou pedindo confirmações desnecessárias.
2. Não produza relatórios extensos antes de começar.
3. Identifique o que precisa ser feito e execute diretamente.
4. Consulte os arquivos estritamente necessários.
5. Reutilize componentes e recursos existentes.
6. Se encontrar um problema, procure uma solução e continue.
7. Só pergunte quando existir uma decisão indispensável que não possa ser tomada tecnicamente.

Não interprete dúvidas ou dificuldades como motivo para abandonar a tarefa.

**QUEIJO** significa: priorizar execução, resolver problemas e entregar o máximo possível da tarefa
solicitada, sem inventar resultados e sem ignorar limitações.

### Prioridade especial: YouTube e YouTube Music

Não ignore nem descarte esses serviços. Investigue e trabalhe nas possibilidades de integração, incluindo:

- Pesquisa de músicas.
- Metadados.
- Artistas, álbuns e capas.
- Identificação de vídeos e músicas.
- Integração com players oficiais.
- Compatibilidade com o hardware e sistema do PS4.
- Alternativas de reprodução autorizadas.

Quando encontrar uma restrição em um serviço:

1. Não descarte a funcionalidade automaticamente.
2. Investigue tecnicamente o funcionamento e as possibilidades de integração.
3. Identifique exatamente qual operação está restrita e por qual motivo.
4. Verifique se existem outros métodos, bibliotecas, componentes ou arquiteturas que permitam alcançar o mesmo resultado.
5. Continue a implementação de todas as partes que não estejam impedidas pela restrição.
6. Recue somente na operação específica proibida pelas regras do projeto (DRM bypass, extração não autorizada de streams protegidos, scraping vedado por ToS) — não no serviço inteiro.

Isso se aplica especialmente ao YouTube e YouTube Music: uma limitação em streaming não encerra a investigação de metadados, busca, capas, identificação de faixas, integração com players oficiais ou qualquer outra parte que não esteja bloqueada.
