# Análise técnica — Cortex Command Community Project

> Fork: `samuelsmoreira02/Cortex-Command-Community-Project` (branch `development`, commit `1a033c4fa`, out/2026)
> Método: leitura estática do código-fonte. **Nada foi compilado nem executado** (a máquina não tem Visual Studio Build Tools).
> Classificação: **Confirmado** = verificado lendo o código e as assinaturas das bibliotecas; **Plausível** = depende de timing/uso e precisa ser reproduzido.

---

## 1. Engenharia reversa — como o motor funciona

### 1.1 Tamanho e stack

| Item | Valor |
|---|---|
| Código próprio (sem ImGui/raylib/glad) | ~126 mil linhas em 392 arquivos `.cpp/.h` |
| Linguagem | C++20 (MSVC 2019/2022 no Windows; **só GCC ≥ 13** no Linux/macOS — clang não suportado) |
| Build | `RTEA.sln` (MSBuild) + `meson.build` (Linux/macOS) |
| API Lua exposta para mods | ~1.550 funções/propriedades (`Source/Lua/LuaBindings*.cpp`) |
| Conteúdo | 10 módulos `.rte` em `Data/` (Base, Coalition, Browncoats, Ronin, Techion, Imperatus, Uzira, Mu-Ilaak, Missions, Dummy) |

**Dependências (`external/sources`)**

| Lib | Uso | Situação |
|---|---|---|
| Allegro 4.4.3.1 (fork customizado) | Bitmaps 8-bit em software, blit, primitivas | Biblioteca de ~2007, sem manutenção; 695 usos de `BITMAP*` em 191 arquivos |
| SDL3 3.2 + SDL3_image | Janela, input, gamepad, PNG | Moderno (migração feita em 2025) |
| OpenGL via `glad` + partes do raylib (`rlgl`) | Envio do frame para a GPU e pós-processamento | Moderno |
| LuaJIT 2.1 + **luabind 0.7.1** | Scripts dos mods | luabind está abandonado desde ~2009 e precisa de macros para compilar com libc++ |
| **RakNet** | Multiplayer | Abandonado (~2014) |
| FMOD | Áudio | Proprietário (binário em `external/lib`) |
| BS::thread_pool, `std::execution::par_unseq`, TBB | Paralelismo | Moderno |
| Tracy | Profiler | Moderno |
| Dear ImGui | Ferramentas de debug | Moderno |

### 1.2 Arquitetura: "Managers" singletons

`Source/Main.cpp` constrói e inicializa ~25 singletons globais (`g_XxxMan`) em ordem fixa (`InitializeManagers`, [Main.cpp:74](Source/Main.cpp:74)). Os principais:

- **TimerMan**: relógio e passo fixo da simulação
- **MovableMan**: todos os objetos móveis (atores, itens, partículas)
- **SceneMan**: terreno pixel a pixel e camadas de material/MOID
- **ActivityMan**: modo de jogo/missão atual
- **LuaMan**: estados Lua, incluindo um estado por thread
- **FrameMan, WindowMan e PostProcessMan**: renderização
- **NetworkServer e NetworkClient**: multiplayer
- **PresetMan**: carrega os módulos `.rte` (INI próprio + Lua)

### 1.3 Loop principal ([Main.cpp:293](Source/Main.cpp:293))

Usa **passo fixo com acumulador**: `TimerMan::Update` soma tempo real, e `while (TimeForSimUpdate())` roda N passos de simulação; depois desenha um frame. Existe um teto contra a "espiral da morte" ([TimerMan.cpp:96](Source/Managers/TimerMan.cpp:96)).

O menu (`RunMenuLoop`) é um *loop aninhado* chamado de dentro do loop de simulação ([Main.cpp:374](Source/Main.cpp:374)). Isso funciona, mas é frágil, porque o estado global fica "pausado no meio" de um passo de simulação.

### 1.4 Um passo de simulação (`MovableMan::Update`, [MovableMan.cpp:1290-1700](Source/Managers/MovableMan.cpp:1290))

1. Espera os raios de visão do passo anterior (`m_ActorsSeeFuture.wait()`).
2. `PreControllerUpdate` → `UpdateControllers` (IA). A IA em Lua roda em **paralelo, um estado Lua por thread** (`ThreadedUpdateAI`).
3. Scripts `ThreadedUpdate` em paralelo, depois `SyncedUpdate` em série.
4. `Travel` (física: forças, colisão por `AtomGroup`, movimento) e `Update` de atores → itens → partículas. **Tudo em série, numa única thread.**
5. Transferência dos objetos adicionados no frame, mortos viram partículas, exclusões.
6. **Settle**: partículas paradas são "pintadas" no terreno e destruídas. É isso que faz o terreno acumular sangue/sucata.
7. Dispara tarefas assíncronas: raios de visão (paralelo), GC do Lua e desenho da camada MOID para o próximo frame.

A física é baseada em **átomos**: cada objeto é um conjunto de pontos (`AtomGroup`) que testa colisão pixel a pixel contra a camada de material do terreno e contra a camada MOID (ID de objeto por pixel).

### 1.5 Renderização

É **híbrida**. A versão em desenvolvimento ganhou um renderizador **OpenGL + raylib** (`Source/Renderer`, usado em ~42 arquivos), e o CHANGELOG diz que o tempo de desenho ficou desprezível. Os bitmaps **Allegro de 8 bits** continuam sendo a base da *simulação*: terreno, camada de material e camada MOID (que a física consulta pixel a pixel), além de parte do desenho de sprites (`draw_sprite`/`rotate_scaled_sprite` em `MOSprite.cpp`).

### 1.6 Multiplayer — removido

O CHANGELOG (*Unreleased → Removed*) diz que o multiplayer foi **removido**. `NetworkServer.cpp` e `NetworkClient.cpp` continuam no repositório, mas **não são compilados**: estão comentados no `meson.build` e fora do `RTEA.vcxproj`. O modelo antigo era "thin client": o servidor rodava o jogo e transmitia os frames prontos para os clientes.

---

## 2. Bugs encontrados

> **Status (branch `fix/regressoes-2025` no fork):** B1–B4, B7, B8, B9 e B10–B14 (segunda rodada, abaixo) **corrigidos**, sem compilar/testar (ver seção 5). B5 e B6 estão em **código de rede que não é mais compilado** (multiplayer removido), então ficaram sem correção; só importam se o multiplayer voltar.

### Segunda rodada (todos corrigidos na branch)

| # | Bug | Onde | Origem |
|---|---|---|---|
| B10 | A prévia de cena (*scene preview dump*) saía corrompida. A versão SDL3 de `SaveIndexedPNG` deixou de converter a imagem de 32 bits para a paleta de 8 bits, usava pitch 4 e o chamador comparava o `bool` de sucesso com `0` | [FrameMan.cpp](Source/Managers/FrameMan.cpp) `SaveIndexedPNG` | Migração SDL3 (jan/2025) |
| B11 | Copiar texto informava falha quando dava certo (`SDL_SetClipboardText` retorna `bool` no SDL3), e colar vazava a string (`SDL_GetClipboardText` precisa de `SDL_free`) | [GUIUtil.cpp](Source/GUI/GUIUtil.cpp) | Migração SDL3 |
| B12 | `FlagAltState` (exposto ao Lua) era **sempre verdadeiro**: `a & b \| c` é avaliado como `(a & b) \| c` | [UInputMan.h](Source/Managers/UInputMan.h) | jun/2025 |
| B13 | **Travamento ao abrir o jogo** com mais controles conectados do que jogadores configurados para controle (ex.: 3 controles com a configuração padrão, ou o jogador 3 trocado para teclado): `find_if` devolvia `end()` e o código usava assim mesmo | [UInputMan.cpp](Source/Managers/UInputMan.cpp) `Initialize` | — |
| B8 | Condição de corrida ao remover ator (ver acima). Agora `RemoveActor` e `GetAllActors` esperam os raios de visão terminarem, e o lock de `RemoveActor` cobre a busca inteira | [MovableMan.cpp](Source/Managers/MovableMan.cpp) | — |
| B14 | Vazamento pequeno: a lista de monitores de `SDL_GetDisplays` nunca era liberada em `MapDisplays` | [WindowMan.cpp](Source/Managers/WindowMan.cpp) | Migração SDL3 |

**Observado, mas não alterado:**
- `UInputMan::Initialize` usa `SDL_GetGamepads`, que só lista *gamepads*. Joysticks genéricos conectados antes de abrir o jogo são ignorados (o ramo `else` nunca roda), enquanto os conectados depois funcionam via hotplug. Corrigir exige `SDL_GetJoysticks` e muda o comportamento, então vale testar com hardware.
- Em `MovableMan::Update`, `Travel()` (que move os atores) roda **antes** de esperar os raios de visão do frame anterior. As threads de visão leem posições enquanto elas mudam. Provavelmente é intencional (desempenho) e só afeta a precisão da visão, mas é uma *data race* formal que o ThreadSanitizer vai apontar.

### B1 — Travamento (abort) ao voltar a resolução anterior · **Confirmado** · Alta
[WindowMan.cpp:483](Source/Managers/WindowMan.cpp:483)
```cpp
bool result = SDL_SetWindowFullscreen(m_PrimaryWindow.get(), fullscreen) == 0;
```
No SDL3, `SDL_SetWindowFullscreen` retorna `bool` (**true = sucesso**). No SDL2 retornava `0` para sucesso. Com `== 0`, um sucesso vira "falha". Na primeira tentativa o jogo mostra "Failed to revert…", tenta os padrões e, na segunda "falha", chama `RTEAbort`, **fechando o jogo mesmo quando deu certo**.
- **Quando acontece:** sempre que uma troca de resolução ou de tela cheia falha e o jogo tenta reverter ([WindowMan.cpp:513-515](Source/Managers/WindowMan.cpp:513)). A linha 513 usa a forma correta (`!SDL_SetWindowFullscreen`), o que confirma a inconsistência.
- **Origem:** regressão da migração para SDL3 (commit `7b99e34a73`, mar/2025).
- **Correção:** remover o `== 0`.

### B2 — "World dump" gera imagem corrompida e reporta errado · **Confirmado** · Média
[FrameMan.cpp:589-596](Source/Managers/FrameMan.cpp:589)
- `SDL_CreateSurfaceFrom(..., depthConvertBitmap->dat, 3)`: o último parâmetro é o **pitch** (bytes por linha) e deveria ser `w * 3`. Com `3`, cada linha começa 3 bytes depois da anterior, então o PNG sai como um borrão diagonal.
- `IMG_SavePNG(...) == 0`: no SDL3_image também retorna `bool`. Um sucesso não marca `saveSuccess`, e uma falha imprime "World was dumped".
- **Plausível (verificar):** o Allegro 4 em 24 bpp costuma guardar em ordem BGR, e a surface foi declarada `RGB24`. Vermelho e azul podem sair trocados.

### B3 — Screenshot reporta o contrário e vaza memória · **Confirmado** · Baixa
[FrameMan.cpp:551-562](Source/Managers/FrameMan.cpp:551)
- O mesmo `IMG_SavePNG(...) == 0` invertido: um sucesso imprime "ERROR: Unable to save bitmap".
- A cópia da surface nunca é liberada (`// SDL_FreeSurface` está comentado). Vaza um frame inteiro (≈8 MB em 1080p) por screenshot. Correção: chamar `SDL_DestroySurface(bitmapToSaveCopy)` no fim da lambda.

### B4 — Ordenação dos times 2 a 4 nunca acontece · **Confirmado** · Baixa
[MovableMan.cpp:1696-1700](Source/Managers/MovableMan.cpp:1696)
```cpp
for (int team = Activity::TeamOne; team < Activity::MaxTeamCount; ++team) {
    if (m_SortTeamRoster[Activity::TeamOne]) {   // deveria ser [team]
```
`SortTeamRoster(2)` é ignorado, e se o time 1 pedir, ordena todos. O impacto é pequeno porque `GetNextTeamActor` reordena por conta própria, mas é um bug de cópia e cola (commit `abc41e59b2`, ago/2025).

### B5 — Servidor multiplayer: pacotes sem validação · **Confirmado, mas em código não compilado** · relevante só se o multiplayer voltar
[NetworkServer.cpp:415-476](Source/Managers/NetworkServer.cpp:415)
- `(MsgRegister*)packet->data` e `(MsgInput*)packet->data` são convertidos **sem checar `packet->length`**. Um pacote curto ou malicioso causa leitura fora do buffer (crash do host).
- `msgReg->Name` (char[64]) é copiado para `std::string` sem garantia de `\0`, o que causa leitura além do fim.
- `ResolutionX/Y` vindos do cliente não são validados e vão direto para `CreateNewNetworkPlayerBackBuffer`. Um cliente pode pedir 100000×100000 e derrubar o servidor por falta de memória.
- `new std::thread(...)` a cada registro ([:436](Source/Managers/NetworkServer.cpp:436)). Na desconexão o ponteiro é só zerado (`// delete ... SendThread` comentado em [:405](Source/Managers/NetworkServer.cpp:405)): a thread nunca recebe `join` e vaza. Se o mesmo cliente reconectar, **duas threads de envio** passam a disputar o mesmo slot.

### B6 — Vazamento de memória da pilha para o servidor NAT · **Confirmado, mas em código não compilado**
[NetworkServer.cpp:461-472](Source/Managers/NetworkServer.cpp:461)
```cpp
MsgRegisterServer msg = {};            // 193 bytes
int payloadSize = sizeof(MsgSceneSetup);   // struct errada, bem maior
m_Server->Send((const char*)&msg, payloadSize, ...);
```
`MsgSceneSetup` tem 5 × `LightweightSceneLayer` e é maior que `MsgRegisterServer`. O envio lê **além da variável na pilha** e manda esses bytes pela rede. Correção: `sizeof(MsgRegisterServer)`.
Extra: `LightweightSceneLayer` usa `size_t` dentro de um struct de rede `#pragma pack(1)`. Ele tem 4 bytes em 32-bit e 8 em 64-bit, então clientes x86 e x64 se desentendem.

### B7 — Mods Lua têm acesso irrestrito ao sistema · **Confirmado** · Crítica (segurança)
[LuaMan.cpp:39-55](Source/Managers/LuaMan.cpp:39)
Todo estado Lua abre `io`, `os`, `ffi` e `package` (inclusive `package.cpath` para carregar `.dll`/`.so`). O comentário diz que é "para o debugger", mas **o carregamento não depende** de `EnableLuaDebugging`.
- **Consequência:** qualquer mod baixado (ex.: do mod.io) pode executar `os.execute(...)`, ler e escrever qualquer arquivo com `io.open`, ou chamar a API do Windows via `ffi`. Isso torna inútil o "sandbox" que o jogo já tem (`LuaMan::FileOpen` + `IsValidModulePath`, [LuaMan.cpp:1059](Source/Managers/LuaMan.cpp:1059)), que só permite caminhos dentro de `.rte`.
- **Origem:** commit `f2ef5c4b7c` (jul/2025). Antes, essas libs estavam removidas justamente "por segurança".
- **Correção:** carregar `io`, `os`, `ffi` e `package.cpath` **só** quando `EnableLuaDebugging` estiver ligado. No modo normal, expor no máximo `os.clock`, `os.time` e `os.date`.
- O `IsValidModulePath` também é fraco: aceita qualquer caminho que *contenha* `.rte` e não tenha `..`. O correto é normalizar com `std::filesystem::weakly_canonical` e checar o prefixo `Data/` ou `Mods/`.

### B8 — Possível condição de corrida ao remover ator · **Plausível** · Média
[MovableMan.cpp:761-773](Source/Managers/MovableMan.cpp:761) e [:1672](Source/Managers/MovableMan.cpp:1672)
No fim de `MovableMan::Update`, `CastSeeRays` é disparado em paralelo indexando `m_Actors[i]`. O `wait()` só acontece no **início do próximo** `MovableMan::Update`. No intervalo rodam `ActivityMan::Update` e os scripts globais, que podem chamar `MovableMan:RemoveActor` (exposto ao Lua). Essa função faz `m_Actors.erase(...)` num `std::deque`, que desloca elementos enquanto as threads de visão leem por índice. O `m_ActorsMutex` não ajuda, porque as threads de visão não o usam, e o laço `for` que procura o ator roda antes do lock.
- **Para confirmar:** rodar com ThreadSanitizer (Linux/meson) uma atividade cujo script global remove atores.
- **Correção simples:** chamar `m_ActorsSeeFuture.wait()` no início de `RemoveActor`, `AddActor` e `ChangeActorTeam`.

### B9 — Assert fatal de timer · **Confirmado (risco baixo)**
[TimerMan.cpp:86](Source/Managers/TimerMan.cpp:86): `RTEAssert(timeIncrease > 0, ...)`. Com resolução de microssegundos, duas chamadas no mesmo µs fecham o jogo. Basta tratar `0` como "nada a fazer".

> **Observação:** B1, B2, B3, B4 e B7 são **regressões de 2025**, introduzidas durante a modernização (migração para SDL3 e refatorações). O projeto **não tem testes automatizados**: o CI ([.github/workflows/ci.yml](.github/workflows/ci.yml)) só compila. É por isso que regressões assim passam.

---

## 3. Pontos de melhoria (por impacto)

### 3.1 Desempenho
1. **Física em uma única thread.** `Travel`/`Update` de atores, itens e partículas são laços sequenciais ([MovableMan.cpp:1405-1470](Source/Managers/MovableMan.cpp:1405) e [:1703-1754](Source/Managers/MovableMan.cpp:1703)). É o principal teto de FPS em batalhas grandes. Caminho sugerido:
   - Separar partículas simples (`MOPixel`, sem script) num **sistema orientado a dados** (structure-of-arrays, sem `new`/`delete` por partícula, sem `virtual`) e paralelizar o `Travel` delas em lotes.
   - Coordenar com o **PR aberto "Deterministic Simulation Part One"** upstream: o paralelismo precisa manter ordem determinística, senão quebra replays e multiplayer.
2. **Allegro 4 nas camadas da simulação.** O desenho na tela já foi para a GPU, mas terreno, material e MOID continuam em bitmaps Allegro (695 usos de `BITMAP*` em 191 arquivos). Trocar por buffers próprios (ex.: `std::vector<uint8_t>` com largura/altura) permitiria remover a dependência de uma biblioteca de 2007 e facilitaria paralelizar consultas de colisão. É trabalho longo, por etapas.
3. `AddActorToTeamRoster` faz `push_back` + `sort` de `std::list` **a cada ator adicionado** ([MovableMan.cpp:864-865](Source/Managers/MovableMan.cpp:864)). Bastaria inserir na posição certa, ou ordenar uma vez por frame (que é o que `m_SortTeamRoster` deveria fazer, ver B4).

### 3.2 Segurança e robustez
4. **B7** (sandbox Lua) já está corrigido na branch. Próximo passo: o `IsValidModulePath` (normalizar caminho e checar prefixo) e avaliar a biblioteca `debug`, que também permite escapar de sandboxes Lua.
5. Se o multiplayer voltar: **não reaproveitar** `NetworkServer`/`NetworkClient` como estão. Validar o tamanho de todo pacote (`ReadMessage<T>(packet)`), usar `std::jthread` e trocar o RakNet (ver 3.3).

### 3.3 Dívida técnica
7. **luabind 0.7.1 → sol2.** O luabind está morto e só compila com hacks de macro (ver commit `c26a70e5d`). O sol2 é header-only, mais rápido e mantido. Dá para migrar arquivo por arquivo (`LuaBindings*.cpp`).
8. **Multiplayer novo, se for refeito:** usar ENet, GameNetworkingSockets ou yojimbo no lugar do RakNet (abandonado), replicando estado em vez de transmitir frames. Isso depende da simulação determinística citada acima. Enquanto isso, `NetworkServer/Client`, `MultiplayerGame` e o RakNet em `external/` são código morto e poderiam sair do repositório.
9. **Funções gigantes:** `AtomGroup::Travel` e `PushTravel` têm ~450 linhas cada, com comentários do próprio time como *"trainwreck"* e *"dumpsterfire"* ([AtomGroup.cpp:323](Source/Entities/AtomGroup.cpp:323), [:762](Source/Entities/AtomGroup.cpp:762)). `MetagameGUI.cpp` tem 302 KB. São candidatas a quebrar em funções menores, **com testes antes**.
10. **Memória manual:** 213 `new` contra 383 `delete` no código próprio. Objetos vivem em `std::deque<T*>` + `unordered_set` de "válidos". Usar um pool com handles (índice + geração) eliminaria use-after-free e as buscas no `unordered_set` a cada `ValidMO`.
11. O loop de menu aninhado dentro do loop de simulação ([Main.cpp:369-376](Source/Main.cpp:369)) deveria virar uma máquina de estados simples (Menu ↔ Jogo).
12. ~200 TODO/FIXME no código próprio; os maiores focos são `AtomGroup.cpp` (19), `MOSRotating.cpp` (14) e `AHuman.cpp` (13).

### 3.4 Qualidade e processo
13. **Testes automatizados** (Catch2 ou doctest) começando pelo que é fácil isolar: `TimerMan`, `Reader`/`Writer` (parser INI), `Vector`/`Matrix`, structs de rede e o sandbox Lua.
14. **Builds com sanitizers no CI:** AddressSanitizer + UBSan e um job com ThreadSanitizer (o motor é multithread e já tem dezenas de mutex). Teriam pegado B2, B3 e B8.
15. Ativar `-Wall -Wextra` e tratar `-Wconversion` nos arquivos novos. Um aviso de "comparação de bool com int" teria pegado B1, B2 e B3.
16. **Portabilidade:** só GCC (clang não suportado) e macOS quebrado (issues upstream #278, #175, #170). Suportar clang/AppleClang resolveria boa parte do macOS.

---

## 4. Ordem sugerida de trabalho

| # | O quê | Esforço | Status |
|---|---|---|---|
| 1 | B7 — sandbox Lua | Pequeno | ✅ Feito (branch) |
| 2 | B1, B2, B3, B4, B9 — bugs de uma linha | Pequeno | ✅ Feito (branch) |
| 3 | Compilar e testar a branch | Pequeno | ⏳ Pendente (seção 5) |
| 4 | B8, B10–B14 — segunda rodada | Pequeno | ✅ Feito (branch) |
| 5 | CI com testes + sanitizers | Médio | Aberto |
| 6 | Sistema de partículas orientado a dados | Grande | Aberto |
| 7 | luabind → sol2 | Grande | Aberto |
| 8 | Allegro fora das camadas de simulação | Muito grande | Aberto |

Depois de testados, os itens 1 e 2 dariam um bom **Pull Request upstream** (`cortex-command-community/Cortex-Command-Community-Project`).

---

## 5. Como validar a branch `fix/regressoes-2025`

**Compilação: ✅ verificada no GitHub Actions do fork** (12/out/2026). O build do Windows (MSBuild, [run 38246265379](https://github.com/samuelsmoreira02/Cortex-Command-Community-Project/actions/runs/38246265379)) e o do Linux (meson/GCC, [run 38246271107](https://github.com/samuelsmoreira02/Cortex-Command-Community-Project/actions/runs/38246271107)) passaram, sem nenhum aviso nos arquivos alterados. O build do macOS falhou **antes de compilar o jogo**: a etapa "Install Osxcross" passou a pedir uma escolha interativa ("Select build flavor") e quebra em modo automático. É um problema da infraestrutura de CI, sem relação com as correções (o upstream provavelmente não vê isso porque usa cache).

**Comportamento no jogo: ainda não testado.** Para testar:

1. **Compilar:** instalar o Visual Studio 2022 Community com "Desenvolvimento para desktop com C++", copiar `external\lib\win\fmod.dll` para a raiz, abrir `RTEA.sln`, escolher `Final` x64 e compilar. Alternativa: ativar o **GitHub Actions** no fork (aba *Actions* → habilitar), e os workflows `meson.yml`/`msbuild.yml` compilam a cada push.
2. **B7:** no console Lua do jogo, rodar `print(io)`, `print(os.execute)` e `print(package.loadlib)`. Os três devem imprimir `nil`, e `print(os.time())` deve imprimir um número. Com `EnableLuaDebugging = 1` no `Settings.ini`, `io` volta a existir.
3. **B2/B3:** tirar um screenshot e um world dump (atalhos com Alt direito; ver CHANGELOG). A mensagem no console deve dizer "dumped to", e o PNG do mundo deve abrir normalmente. Conferir também se as cores não saem com vermelho e azul trocados (ponto plausível do B2).
4. **B1:** difícil de forçar. Basta revisar que `SDL_SetWindowFullscreen` retorna `bool` no SDL3.
5. **B10:** gerar uma prévia de cena e abrir o PNG (deve ser uma miniatura normal da cena).
6. **B11:** copiar e colar texto numa caixa de texto do jogo.
7. **B12:** no console Lua, `print(UInputMan.FlagAltState)` deve dar `false` sem segurar Alt.
8. **B13:** abrir o jogo com 3 controles conectados (ou com o jogador 3 configurado para teclado e 1 controle). Antes travava; agora deve abrir.
