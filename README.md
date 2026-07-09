# Precision Mouse Filter

Filtro de entrada de mouse para Windows: reduz jitter do sensor e microtremores,
suaviza sem atraso perceptível, preserva flicks rápidos e movimentos retos —
sem qualquer forma de auxílio de mira.

## O que este software NÃO faz (por arquitetura, não apenas por promessa)

O pipeline inteiro (`MouseFilterPipeline`) só recebe três números por evento:
`dx`, `dy` (deslocamento bruto do sensor) e `dt` (tempo desde o evento
anterior). Não existe, em nenhum lugar do código:

- Leitura de tela, captura de framebuffer ou OCR.
- Leitura de memória de outro processo ou de janelas de jogos.
- Qualquer conceito de "alvo", "inimigo" ou "recuo".
- Previsão de movimento futuro (o anti-spike só limita magnitude de um
  evento já ocorrido; nunca extrapola ou "adivinha" a posição correta).
- Qualquer dependência de qual aplicativo está em foco.

Por isso o filtro não pode, estruturalmente, auxiliar mira: ele não tem
acesso a nenhuma informação sobre o que está na tela.

## Arquitetura

```
Sensor USB/BT do mouse
        │
        ├──▶ WM_INPUT (Raw Input) ──▶ MouseFilterPipeline ──▶ SetCursorPos
        │        (dado bruto,             (filtro adaptativo)   (posição final,
        │         sem aceleração                                 sem aceleração
        │         do Windows)                                    do Windows)
        │
        └──▶ WH_MOUSE_LL (hook) ──▶ suprime o movimento nativo
                                     não filtrado do cursor
```

Duas coisas rodam na mesma thread dedicada (prioridade `TIME_CRITICAL`):

1. **Raw Input** (`WM_INPUT`): fonte do delta bruto real do sensor, sem a
   curva de aceleração do Windows.
2. **Hook de baixo nível** (`WH_MOUSE_LL`): serve *apenas* para suprimir a
   atualização nativa (não filtrada) do cursor. A versão filtrada é aplicada
   separadamente via `SetCursorPos` a partir do Raw Input. Eventos
   sintéticos (marcados pelo Windows com `LLMHF_INJECTED`) nunca são
   suprimidos nem re-filtrados — isso evita loop de realimentação.
3. **`SetCursorPos`** (posição absoluta) é usado em vez de `SendInput`
   relativo porque não passa pela curva de aceleração do Windows,
   garantindo a resposta 1:1 exigida.

Cliques, scroll e todo o resto passam direto pelo hook, sem qualquer
interferência.

### Por que não um driver de kernel?

A abordagem "definitiva" para isso (usada por ferramentas como o Raw Accel)
é um driver de filtro no kernel, que intercepta o dado antes de qualquer
outra coisa. É mais robusto, mas exige WDK, assinatura de driver e testes
extensivos em hardware real — um bug ali pode travar o sistema (BSOD). Este
projeto usa a abordagem em modo usuário (hook + Raw Input) porque é
funcional, inspecionável, não requer driver assinado, e é a opção
responsável para entregar sem testes extensivos em hardware físico. As
limitações dessa escolha estão na seção abaixo.

## Limitações conhecidas (leia antes de usar)

- **Não testado em hardware físico por mim.** O código foi compilado com
  MinGW-w64 (`x86_64-w64-mingw32-g++`) para validar que compila e linka
  corretamente contra os cabeçalhos reais do Win32, mas eu não tenho uma
  máquina Windows com mouse físico para testar o comportamento real do
  hook/injeção. Teste com calma e ajuste os sliders à sua sensação.
- **Requer Administrador.** O hook global é mais confiável entre janelas
  elevadas quando o processo também está elevado (UIPI do Windows). O
  manifesto já pede elevação (`requireAdministrator`); mude para
  `asInvoker` em `resources/app.manifest` se preferir não elevar, aceitando
  que a supressão pode ser menos consistente perto de janelas elevadas.
- **Enhance Pointer Precision**: o filtro usa `SetCursorPos` absoluto, que
  não passa pela aceleração do Windows independentemente dessa opção — mas,
  por precaução, desative "Aumentar precisão do ponteiro" em
  Configurações > Mouse para garantir 100% de consistência.
- **Antivírus/EDR**: hooks globais de mouse + injeção de cursor são o
  mesmo mecanismo usado por ferramentas de automação, então alguns
  antivírus podem sinalizar heuristicamente o executável (falso positivo
  comum nessa categoria de software). O código-fonte completo está aqui
  para auditoria.
- **Anti-cheat de jogos**: o software não tem nenhum conhecimento de jogos,
  mas alguns anti-cheats de kernel bloqueiam qualquer hook global de mouse
  por precaução. Desative o filtro (atalho) antes de jogos com anti-cheat
  restritivo, e verifique a política do jogo quanto a software de terceiros.
- **Dispositivos em modo absoluto** (algumas mesas digitalizadoras, sessões
  de área de trabalho remota) não são filtrados — o código detecta
  `MOUSE_MOVE_ABSOLUTE` e ignora esses eventos, propositalmente.

## Como sair de qualquer situação estranha

O atalho de ativar/desativar (`Ctrl+Alt+F9` por padrão) é um hotkey global
do Windows (teclado, não depende do mouse) — funciona mesmo que o cursor
pareça travado. Fechar o processo (Gerenciador de Tarefas) também remove o
hook automaticamente; o Windows desinstala hooks de um processo que termina.

## Build

### Visual Studio + CMake (recomendado)

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

O executável final fica em `build\Release\PrecisionMouseFilter.exe`.

### MinGW-w64

```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Este é o mesmo processo usado para validar o projeto durante o
desenvolvimento (compilação cruzada em Linux com
`g++-mingw-w64-x86-64`), então o `CMakeLists.txt` está confirmado
funcional para esse toolchain.

## Uso

1. Execute como Administrador (o manifesto já solicita isso automaticamente).
2. Ajuste os sliders (veja o guia abaixo) e escolha/crie um perfil.
3. `Ctrl+Alt+F9` ativa/desativa o filtro a qualquer momento.
4. `Ctrl+Alt+F10` / `Ctrl+Alt+F11` alternam entre perfis salvos.
5. Clique em "Alterar" ao lado de qualquer atalho para redefini-lo.

## Guia de ajuste dos parâmetros

| Slider | O que faz | Efeito de subir o valor |
|---|---|---|
| Intensidade do filtro | Remoção de jitter em repouso/lento | Mais estável parado, mais "peso" ao iniciar um movimento |
| Intensidade da suavização | Por quanto tempo a suavização persiste ao acelerar | Suavização dura mais tempo antes de "abrir" para resposta total |
| Sensibilidade geral | Ganho fixo (contagens → pixels) | Cursor mais rápido — **não** varia com velocidade (requisito de curva linear) |
| Preservar linha reta | Amortecimento extra no eixo secundário quando o movimento é claramente H/V | Linhas retas mais estáveis; diagonais deliberadas nunca são tocadas |
| Anti-spike | Agressividade ao limitar picos anômalos | Mais picos de sensor são cortados; jamais afeta flicks normais |

Os valores padrão (`FilterSettings` em `MouseFilterPipeline.h`) são pontos
de partida com fundamentação matemática (ver comentários no código), não
valores "perfeitos" — ajuste à sua sensação e ao DPI do seu mouse (o campo
`mouseDpi`, salvo no perfil, normaliza o comportamento entre DPIs
diferentes).

## Estrutura do projeto

```
include/            headers (.h)
  OneEuroFilter.h        filtro adaptativo por eixo (núcleo do algoritmo)
  MouseFilterPipeline.h  pipeline completo: anti-spike, linha reta, ganho
  InputEngine.h          Raw Input + hook de supressão
  ConfigManager.h        perfis (arquivo texto em %APPDATA%)
  HotkeyManager.h        atalhos globais
  MetricsMonitor.h        CPU em background
  MainWindow.h            interface Win32
  SharedState.h           estado lock-free entre threads
src/                 implementações (.cpp) correspondentes + main.cpp
resources/           manifesto (elevação, DPI, common controls) + dialog de atalho
CMakeLists.txt
```

## Requisitos do pedido original × implementação

| Requisito | Onde |
|---|---|
| Filtro adaptativo de jitter | `AdaptiveDeltaFilter` (cutoff sobe com velocidade) |
| Suavização inteligente, preserva flicks | mesma engine — ganho sempre 1.0, só o atraso/blend varia |
| Preservação de linha reta | amortecimento do eixo secundário em `MouseFilterPipeline::Process`, nunca toca diagonais |
| Microestabilidade | cutoff mínimo em repouso |
| Curva de resposta linear | ganho fixo (`sensitivity`) + `SetCursorPos` absoluto |
| Filtro adaptativo por velocidade | núcleo do 1-Euro Filter adaptado |
| Anti-spike | `SpikeGuard`, clamp por magnitude, nunca prediz |
| Configurações/perfis/atalhos | `ConfigManager` + `HotkeyManager` |
| Interface com stats | `MainWindow` (polling rate, latência, CPU ao vivo) |
