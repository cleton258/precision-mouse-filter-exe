# Como compilar o Precision Mouse Filter (gerar o .exe)

Duas opções abaixo. Se você nunca compilou um projeto C++ antes, use a
**Opção 1 (Visual Studio)** — é só cliques, sem terminal.

---

## Opção 1 — Visual Studio (recomendado)

### Passo 1: Instalar o Visual Studio Community (gratuito)

1. Baixe em: https://visualstudio.microsoft.com/pt-br/vs/community/
2. Rode o instalador.
3. Na tela **"Cargas de Trabalho"**, marque **"Desenvolvimento para desktop com C++"**
   (*"Desktop development with C++"*).
4. No painel à direita, confira se **"Ferramentas C++ CMake para Windows"**
   (*"C++ CMake tools for Windows"*) está marcado — normalmente já vem
   marcado por padrão junto com essa carga de trabalho.
5. Clique em **Instalar**. É um download grande (alguns GB); pode levar de
   20 a 40 minutos dependendo da sua internet.

### Passo 2: Extrair o projeto

Extraia o `PrecisionMouseFilter.zip` em qualquer pasta, por exemplo
`C:\Projetos\PrecisionMouseFilter`.

### Passo 3: Abrir no Visual Studio

1. Abra o Visual Studio.
2. Na tela inicial, clique em **"Abrir uma pasta local"** (*"Open a local
   folder"*) — não é "Abrir projeto", é a opção de pasta.
3. Selecione a pasta extraída (a que tem o arquivo `CMakeLists.txt` dentro).
4. Espere a barra de status embaixo terminar de aparecer
   "Gerando cache do CMake..." / configurando (1-2 minutos na primeira vez).
   Se aparecer algum erro em vermelho na aba "Saída", copie e me mande.

### Passo 4: Escolher a configuração

No topo da janela, perto do botão verde de "Executar", tem um menu
suspenso (geralmente mostrando `x64-Debug`). Troque para **`x64-Release`**.

### Passo 5: Compilar

Menu **Compilar > Compilar Tudo** (*Build > Build All*), ou aperte
**Ctrl+Shift+B**. Acompanhe a aba "Saída" embaixo — no final deve aparecer:

```
1>PrecisionMouseFilter.exe -> ...\PrecisionMouseFilter.exe
========== Build: 1 succeeded ==========
```

### Passo 6: Achar o .exe

O arquivo fica em:

```
<pasta do projeto>\out\build\x64-release\PrecisionMouseFilter.exe
```

(O projeto já inclui um `CMakePresets.json` que define esse caminho; se por
algum motivo o Visual Studio usar outro nome de pasta, procure dentro de
`out\build\`.) Esse `.exe` já pode ser copiado para qualquer lugar — ele não
depende dos arquivos-fonte para rodar.

### Passo 7: Rodar

Clique com o botão direito no `.exe` → **Executar como administrador**. O
manifesto embutido já solicita elevação automaticamente mesmo com duplo
clique simples, então normalmente o Windows já vai mostrar o prompt do UAC
sozinho.

---

## Opção 2 — MinGW-w64 / linha de comando

Para quem já usa MSYS2/MinGW-w64 ou prefere terminal. Estes são exatamente
os comandos usados para validar que o projeto compila sem erros antes de
te enviar (compilação cruzada em Linux, com o mesmo compilador
`x86_64-w64-mingw32-g++` que o MSYS2 fornece no Windows):

```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

O `.exe` fica em `build\PrecisionMouseFilter.exe`.

---

## Problemas comuns

| Sintoma | Causa provável / solução |
|---|---|
| `'cmake' não é reconhecido como um comando` | Abra o **"Developer PowerShell for VS"** (vem instalado junto com o Visual Studio) em vez do PowerShell comum — ele já configura o PATH certo. |
| Erro mencionando `manifest` duplicado | Não deve ocorrer neste projeto; indica que algo em `resources/app.rc` ou `resources/app.manifest` foi alterado. |
| Antivírus sinaliza o `.exe` recém-compilado | Comportamento comum para qualquer programa com hook global de mouse (não é malware) — veja "Limitações conhecidas" no `README.md`. |
| A janela abre mas o mouse não muda de comportamento | Confirme que rodou como Administrador e que o botão "Filtro" na interface mostra "ATIVADO". |

Qualquer erro de compilação diferente destes: copie a mensagem completa da
aba "Saída" e me envie que eu ajudo a resolver.
