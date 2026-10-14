# Novidades para o Cortex Command, inspiradas em jogos atuais

> Base: código do branch `development` (out/2026), CHANGELOG, PRs e issues abertos no projeto da comunidade.
> Cada ideia diz **o que o motor já tem** para apoiá-la, para separar o que é viável do que é sonho.

---

## 0. Antes de tudo: o jogo não parou de evoluir

A versão original da Data Realms parou em 2012, mas o **Community Project continua ativo**. Só nesta semana foram abertos PRs como estes:

- **#291 Simulação determinística** (parte 1). Prepara replays e um multiplayer novo.
- **#271 Facção Mu-Ilaak** (nova facção baseada nas artes conceituais antigas).
- **#268 e #269**: refação do gerenciador de mods e da tela de cenários.
- **#270**: carregamento de módulos numa thread separada.

E a versão ainda não lançada (seção *Unreleased* do CHANGELOG) já traz:

- pathfinding refeito, com IA que sabe pular, usar jetpack e cavar;
- correr e agachar;
- renderizador novo em GPU (OpenGL);
- migração para SDL3;
- saves compactados;
- névoa de guerra 5 vezes mais fina.

Então as ideias abaixo são **lacunas em relação aos jogos atuais**, não uma ressurreição.

### O que o jogo tem hoje (resumo)

| Área | Situação |
|---|---|
| Modos | 15 atividades com script (Wave Defense, Bunker Breach, Survival, Massacre, Harvester, One-Man Army…), tutorial, campanha de conquista (`MetaMan`) e 6 editores (cena, base, ator, montagem, área, gib) |
| Física | Terreno pixel a pixel com materiais (densidade, atrito, integridade, "piling"). Partículas que param viram terreno. Gibs e ferimentos por pixel |
| Terreno | **Estático**: não tem líquidos, gás, fogo nem temperatura (`Material.h` só tem propriedades mecânicas). Pedaços soltos no ar não caem |
| Visual | Renderizador OpenGL com shaders (`Data/Base.rte/Shaders`) e *glows* aditivos. **Não tem iluminação dinâmica nem ciclo dia/noite** (pedido na issue #195) |
| Multijogador | Só local (até 4 jogadores na mesma tela). O online foi **removido** |
| Mods | `.rte` com INI + Lua (~1.550 funções expostas). Mods vêm do site mod.io, mas não dá para navegar e baixar mods dentro do jogo |
| Conquistas | `AchievementMan` com Steam já existe |
| Mapas | Feitos à mão. Já existem as peças para montar bunkers (`BunkerAssembly`, `BunkerAssemblyScheme`), mas **não há geração procedural** |

---

## 1. Ideias por impacto

Legenda de esforço: 🟢 semanas · 🟡 1 a 3 meses · 🔴 vários meses

### 1.1 Terreno vivo: líquidos, gases e fogo · 🟡 **fogo implementado** (branch `feature/terreno-vivo`), líquidos e gases pendentes · referência: **Noita**, **Teardown**, **Sandustry**

**O que é:** cada pixel do terreno passa a ter um *estado*: sólido, pó, líquido ou gás. Líquidos escorrem e fazem poças, gases sobem, fogo se espalha por materiais inflamáveis, ácido corrói, óleo pega fogo, água apaga fogo.

**Por que combina com CC:** o jogo **já é** um simulador de terreno por pixel. O Noita mostrou que esse é justamente o diferencial que vende um jogo "de pixels" hoje. No CC, cavar e explodir já é a graça; com fluidos, inundar um bunker ou incendiar uma base de madeira vira tática.

**Como fazer no motor:**
- Acrescentar em `Material` os campos `State` (Solid/Powder/Liquid/Gas), `Flammability`, `BurnsInto`, `Corrosiveness` e `Viscosity`, todos configuráveis por INI. Os mods ganham isso de graça.
- Um passo de *autômato celular* sobre a camada de material, que já é um bitmap de 8 bits por pixel, rodando só em **chunks "sujos"** (64×64).
- Paralelizar no padrão "tabuleiro de xadrez" que o Noita apresentou na GDC 2019: chunks alternados em 4 passadas, sem locks.
- A IA precisa saber evitar fogo e ácido. O pathfinder novo já tem custo por material.

**Primeiro passo pequeno:** só **fogo em materiais inflamáveis** (madeira, grama, combustível), sem líquidos. Ele cabe na camada atual e já gera bastante jogabilidade.

### 1.2 Colapso estrutural · ✅ **implementado** (branch `feature/colapso-estrutural`) · referência: **Teardown**, **Noita**, **Worms Armageddon**

**O que é:** um pedaço de terreno que perde a ligação com o resto (ou com o chão) **cai**, como bloco rígido ou como pó.

**Hoje:** dá para cavar por baixo de uma montanha inteira e ela fica flutuando. Isso é a coisa que mais "data" o CC visualmente.

**Como fazer:** depois de explosões, rodar um *flood fill* na região afetada para achar "ilhas" desconectadas. Ilhas pequenas viram partículas (o motor já sabe soltar pixels). Ilhas médias viram um `MOSRotating` temporário com o sprite recortado, que cai, colide e assenta de novo como terreno. O motor já tem todos esses tipos.

### 1.3 Iluminação dinâmica, noite e clima · ✅ **implementado** (branch `feature/iluminacao`) · referência: **Barotrauma**, **Terraria**, **Dead Cells**

> **Feito:** passada de luz na GPU (ambiente + até 64 fontes por tela; todo brilho vira luz da própria cor), HUD e brilhos preservados, custo zero de dia; scripts globais "Day and Night Cycle" (com lanterna no soldado controlado) e "Dynamic Weather" (chuva/neve existentes, céu escurecido); IA não enxerga alvos no escuro à distância. **Não feito:** sombras projetadas pelo terreno, sinalizadores e óculos de visão noturna como itens.

**O que é:** luz por fonte (lanternas nas armas, clarão de tiro, explosões, lava, luzes de base), sombras projetadas pelo terreno, ciclo dia/noite, chuva, neblina e tempestade de areia.

**Já existe:** renderizador OpenGL com shaders, sistema de *glows* (`PostProcessMan::RegisterGlowDotEffect` etc.) e camada de material (que serve para saber o que bloqueia luz). Também há um pedido aberto da comunidade (issue #195).

**Como fazer:**
- Um *light map* em baixa resolução, calculado na GPU com *ray-marching* 2D na textura de material e multiplicado no `PostProcess.frag`.
- Expor `LightSource` no INI e no Lua (cor, raio, cone).

**Jogabilidade nova:** missões noturnas, furtividade (inimigo só vê o que está iluminado, ligando a visão da IA ao *light map*), sinalizadores e óculos de visão noturna como item de compra.

### 1.4 Replays, "kill cam" e clipes · 🟢 depois do PR #291 · referência: **StarCraft II**, **Worms W.M.D**, **Rocket League**

**O que é:** gravar a partida inteira em poucos KB (só as entradas dos jogadores mais a semente aleatória) e assistir de novo, com câmera livre, câmera lenta e botão "salvar os últimos 30 s como GIF/MP4".

**Por que agora:** o PR #291 (simulação determinística) é exatamente o pré-requisito, e o próprio autor cita replays como objetivo. Falta a camada de gravação de input (que o PR diz ser "parte dois") e a interface.

**Bônus:** replays viram ferramenta de depuração. Um crash vira um arquivo que reproduz o bug.

### 1.5 Multiplayer online de verdade · 🔴 · referência: **Broforce**, **Worms**, **Brotato co-op**

**O que é:** cooperativo e versus pela internet.

**Situação:** o online antigo transmitia vídeo do servidor (o que o CHANGELOG chama de *"terrible and broken"*) e foi removido. Hoje o CHANGELOG recomenda o Steam Remote Play Together.

**Caminho moderno:** *lockstep determinístico* (todos simulam, só as entradas viajam pela rede), que é o mesmo pré-requisito dos replays. Usar Steam Networking Sockets ou GameNetworkingSockets, que resolvem NAT. A ordem natural é: replays (1.4), depois "replay ao vivo" entre duas máquinas, depois multiplayer.

### 1.6 Modo roguelite com mapas procedurais · ✅ **implementado** (branch `feature/roguelite`) · referência: **Noita**, **Spelunky 2**, **Deep Rock Galactic**, **Brotato**

> **Feito:** gerador de terreno em C++ (`TerrainGenerator`, configurado no INI da cena, determinístico por semente), cenas Expedition Highlands/Desert/Tundra, e a atividade **Expedition**: 5 setores, objetivos Purge/Prospect/Last Stand, comandante com morte permanente, sobreviventes que seguem, 6 melhorias escolhidas no menu radial, ranks de veterania permanentes, saves no meio da expedição. Testado por roteiros automáticos e partida real.

**O que é:** uma "expedição" de missões curtas em mapas gerados na hora. Entre elas você escolhe melhorias (implantes, armas raras, mais um soldado). A morte do cérebro encerra a run, mas desbloqueia coisas para a próxima.

**Já existe:**
- `BunkerAssembly`/`BunkerAssemblyScheme`, que montam bunkers a partir de peças;
- materiais e texturas de terreno;
- atividades com script (`GAScripted`): o modo em si pode ser **100% Lua**.

**Falta:** um gerador de terreno (ruído para relevo e cavernas, veios de ouro, posição de bunkers) exposto ao Lua, por exemplo `SceneMan:GenerateTerrain(seed, params)`. Mapas com semente também ajudam replays e desafios diários ("seed do dia", como em Spelunky e Noita).

### 1.7 Comando de esquadrão tático · ✅ **implementado** (branch `feature/esquadrao-tatico`) · referência: **Door Kickers 2**, **Company of Heroes 3**, **Into the Breach**

> **Feito:** scripts globais "Tactical Squads" (submenu "Squad Orders" no líder: segurar posição, seguir, cessar fogo/fogo à vontade, formação cerrada/coluna/dispersa; HUD com linhas e status) e "Tactical Time" (câmera lenta a 10% enquanto dá ordens). Testado no jogo por um roteiro automático. Fila de waypoints e seleção por círculo já existiam no jogo.

**O que é:**
- **Pausa tática** para dar ordens com o tempo parado;
- **fila de waypoints**, formações e posturas (segurar posição, avançar cobrindo, fogo à vontade);
- seleção de grupo por arraste;
- indicador de linha de tiro.

**Situação:** a issue #208 ("Improve squad logic") pede isso, e o pathfinding novo (pulo, jetpack, cavar) dá a base. A IA está em Lua (`ThreadedUpdateAI`), então dá para fazer quase tudo sem tocar no C++. A pausa tática já é possível com `TimerMan` (escala de tempo).

### 1.8 Campanha com progressão persistente · ✅ **veteranos persistentes implementados** (branch `feature/campanha-persistente`); o mapa estratégico com pesquisa ficou de fora · referência: **XCOM 2**, **Battle Brothers**, **Kenshi**

**O que é:** soldados com **nome, experiência, ferimentos permanentes e especializações**, um mapa estratégico com pesquisa de tecnologia, e o *memorial* dos mortos.

**Situação:** a campanha de conquista (`MetaMan`/`MetagameGUI`, 302 KB num arquivo só) é a parte mais antiga do jogo, e o PR #269 já está refazendo a tela de cenários. Com a progressão, cada clone perdido passa a doer, o que hoje não acontece.

### 1.9 Navegador de mods dentro do jogo · 🟢/🟡 · referência: **RimWorld**, **Noita**, **Baldur's Gate 3** (mod.io)

**O que é:** buscar, avaliar, baixar e atualizar mods do **mod.io** sem sair do jogo, com resolução automática de dependências e "coleções".

**Situação:** os mods já vêm do mod.io (site), e o PR #268 refaz o gerenciador. O mod.io tem API REST e SDK oficial em C++.

⚠️ **Pré-requisito:** a correção do sandbox Lua (B7 na `ANALISE.md`, já feita na branch `fix/regressoes-2025`). Baixar mods com um clique sem sandbox seria um vetor de malware.

### 1.10 Qualidade de vida e acessibilidade · 🟡 **parcial**: marcadores para daltônicos, modo foto (F8) e botão "Continue" feitos; suporte a controle e escala da interface pendentes · referência: padrão de qualquer lançamento atual

- **Interface pensada para controle**, com **selo Steam Deck Verified**. A issue #267 diz que a UI não funciona bem com gamepad.
- **Escala de interface**, fonte maior e **modos para daltônicos**. Os times hoje se distinguem por cor.
- **Remapeamento total** com ícones do controle certo (o `UInputMan` já carrega ícones por dispositivo).
- **Modo foto**: pausa, câmera livre, esconder HUD (a issue #5 pedia o HUD oculto).
- **Dicas contextuais** no lugar do tutorial isolado (a issue #188 pede o tutorial em `GAScripted`).
- **Salvamento automático** e "continuar a última partida" no menu.

### 1.11 Arsenal e recompensas · ✅ **implementado** (branch `feature/arsenal`) · referência: **Vampire Survivors**, **Risk of Rain 2**, **Enter the Gungeon**, **Doom Eternal**

Ideia que surgiu depois da lista original: deixar o combate mais recompensador.

- **13 armas novas** numa aba própria do menu de compra ("Arsenal"): Arc Caster, Railgun, MIRV Launcher, Cryo Projector, Dragon's Breath, Singularity Grenade, Airstrike Beacon, Gravity Gun, Tunneler, Chakram Launcher, Plasma Cutter, Sentry Deployer e Tesla Trap.
- **Combat Feedback**: hitmarkers, números de dano, anúncios de abates múltiplos e de sequências, pausa de impacto e ouro por combo.
- **Loot Drops**: inimigos derrubam ouro, kits médicos, power-ups e armas lendárias, com cores de raridade.
- **Sprites locais**: imagens em `Userdata/SpriteOverrides/` substituem as do jogo sem entrar no Git, para usar arte de mods só no próprio PC.

---

## 2. Mapa de prioridade

| # | Ideia | Impacto | Esforço | Depende de | Pode ser só Lua/INI? |
|---|---|---|---|---|---|
| 1.10 | Qualidade de vida e acessibilidade | Médio | 🟢 | — | Parcialmente |
| 1.7 | Esquadrão tático | Alto | 🟡 | — | **Sim, quase tudo** |
| 1.6 | Roguelite procedural | **Muito alto** | 🟡 | Gerador de terreno em C++ | O modo sim, o gerador não |
| 1.3 | Iluminação, noite e clima | **Muito alto** | 🟡 | — | Não (shader + C++) |
| 1.4 | Replays e clipes | Alto | 🟢* | PR #291 + gravação de input | Não |
| 1.9 | Navegador de mods | Alto | 🟢/🟡 | Sandbox Lua (B7) | Não |
| 1.8 | Campanha com progressão | Alto | 🟡 | PR #269 | Em boa parte |
| 1.1 | Fogo, líquidos e gases | **Altíssimo** (o "Noita do CC") | 🔴 | — | Não |
| 1.2 | Colapso estrutural | Alto | 🔴 | Idealmente 1.1 | Não |
| 1.5 | Online | Muito alto | 🔴 | 1.4 | Não |

\* depois que a simulação determinística estiver pronta.

### Sugestão de ordem

1. **Começar por 1.7 (esquadrão) ou 1.6 (roguelite).** Dá para prototipar em Lua como um mod, sem esperar revisão do motor, e testar se é divertido antes de investir em C++.
2. **Em paralelo, um protótipo de 1.1 só com fogo.** É a mudança que mais "moderniza" a sensação do jogo e usa o que o motor tem de único.
3. **Conversar com a comunidade no Discord** antes de mexer no motor. Várias dessas ideias encostam em trabalho em andamento (#291, #268, #269), e alinhar evita PRs rejeitados.
