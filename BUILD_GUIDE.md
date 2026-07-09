# 🔨 Guia de Build - Precision Mouse Filter

Versão: 1.0.0 (Corrigida)
Última atualização: 2026-07-09

---

## ✅ Verificação de Pré-requisitos

### Windows (Recomendado)

```bash
# Verificar Visual Studio
vs_where version

# Verificar CMake
cmake --version  # Requer 3.16+

# Verificar Git
git --version
```

**Versões mínimas necessárias:**
- Windows 10 Build 19041+
- Visual Studio 2022 (Community, Professional ou Enterprise)
- CMake 3.16+
- Git 2.30+

---

## 🚀 Compilação Rápida

### Opção 1: Linha de Comando (Recomendado)

```bash
# 1. Clone o repositório
git clone https://github.com/seu-usuario/precision-mouse-filter.git
cd precision-mouse-filter

# 2. Configure com o preset x64-release
cmake --preset x64-release

# 3. Compile
cmake --build --preset x64-release --config Release

# 4. Verifique o resultado
dir out\build\x64-release\Release\PrecisionMouseFilter.exe
```

**Tempo esperado:** 2-5 minutos (primeira compilação)

---

### Opção 2: Visual Studio IDE

```bash
# 1. Clone e configure
git clone https://github.com/seu-usuario/precision-mouse-filter.git
cd precision-mouse-filter
cmake --preset x64-release

# 2. Abra a solution
start out\build\x64-release\PrecisionMouseFilter.sln

# 3. No Visual Studio:
#    - Selecione "Release" no dropdown
#    - Build → Build Solution (Ctrl+Shift+B)
```

**Vantagens:**
- Interface gráfica
- Debugging integrado
- IntelliSense/code completion

---

### Opção 3: CMake GUI

```bash
# 1. Abra CMake GUI
cmake-gui .

# 2. Configure:
#    - Selecione "Visual Studio 17 2022" como gerador
#    - Clique "Configure"
#    - Clique "Generate"

# 3. Abra solution gerada:
start out\build\x64-release\PrecisionMouseFilter.sln
```

---

## 📦 Artefatos de Build

### Localização dos Binários

| Tipo | Caminho |
|------|---------|
| **Release (Otimizado)** | `out/build/x64-release/Release/PrecisionMouseFilter.exe` |
| **Debug (Com símbolos)** | `out/build/x64-debug/Debug/PrecisionMouseFilter.exe` |

### Tamanho Esperado

- Release: ~3-5 MB
- Debug: ~15-20 MB

---

## 🧹 Limpeza de Build

```bash
# Remover todos os arquivos de build
rm -r out\

# Limpar cache do CMake
rm -r CMakeFiles\
rm CMakeCache.txt
```

---

## 🐛 Troubleshooting

### Erro: "CMake error: generator not found"

```bash
# ❌ Errado (antigo)
cmake --preset x64-release  # Estava usando Ninja

# ✅ Correto (novo)
cmake --preset x64-release  # Agora usa Visual Studio 17 2022
```

**Solução:**
- Use o CMakePresets.json corrigido (incluído nesta versão)

### Erro: "Visual Studio not found"

```bash
# Verifique a instalação
"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe" /?

# Se não encontrar, instale:
# https://visualstudio.microsoft.com/vs/
```

**Requisitos de instalação (VS Community 2022):**
- ✅ Desktop development with C++
- ✅ CMake tools for Windows
- ✅ Windows SDK

### Erro: "CMake version is too old"

```bash
# Verifique versão
cmake --version  # Precisa de 3.16+

# Atualize CMake
# https://cmake.org/download/

# Ou via package manager
choco install cmake
```

### Arquivo .exe não gerado

```bash
# 1. Verifique se a compilação completou
cmake --build --preset x64-release --config Release --verbose

# 2. Procure por erros de compilação (procura por "error:")
# 3. Verifique permissões de pasta

# 4. Tente limpeza completa
rm -r out\
cmake --preset x64-release
cmake --build --preset x64-release --config Release
```

---

## 🔄 GitHub Actions (CI/CD)

### Configuração Automática

O repositório contém workflow automático que:

1. ✅ Faz checkout do código
2. ✅ Configura o CMake
3. ✅ Compila em Release
4. ✅ Faz upload do .exe como artifact

### Como Usar

```bash
# 1. Faça push para main ou master
git push origin main

# 2. Verifique em GitHub:
# - Actions → último run
# - Artifacts → PrecisionMouseFilter

# 3. Download do .exe compilado:
# - Clique em "PrecisionMouseFilter"
# - Descompacte o ZIP
```

### Validar Workflow Localmente

```bash
# Instale act (simula GitHub Actions)
choco install act-cli

# Execute o workflow
act -j build
```

---

## 📊 Verificação de Build

### Checklist Pós-Compilação

- [ ] Arquivo `.exe` criado em `out/build/x64-release/Release/`
- [ ] Tamanho do arquivo está entre 3-5 MB
- [ ] Sem erros de compilação (erro: 0)
- [ ] Sem warnings graves (W3+)
- [ ] Executável roda sem erros de runtime

### Teste do Executável

```bash
# Navegue até o diretório
cd out\build\x64-release\Release\

# Execute
PrecisionMouseFilter.exe

# Esperado:
# - Janela da UI abre
# - Sem erros de DLL faltante
# - Filtro de mouse funcional
```

---

## 🔐 Assinatura de Código (Opcional)

Para compilações de produção, considere assinar o .exe:

```bash
# Com certificado instalado
signtool sign /f cert.pfx /p senha /t http://timestamp.server /fd SHA256 ^
    PrecisionMouseFilter.exe
```

---

## 📈 Otimizações de Build

### Compilação Paralela (Mais Rápida)

```bash
# Use todos os núcleos da CPU
cmake --build --preset x64-release --config Release -j 8

# Ou no CMake:
cmake --build --preset x64-release -- /MP
```

**Speedup esperado:** 30-50% em máquinas multi-core

### Compilação Incremental

Após a primeira compilação:

```bash
# Modifique só um arquivo
# Compile apenas as mudanças
cmake --build --preset x64-release --config Release
```

**Tempo:** 5-15 segundos (vs 2-5 minutos primeira vez)

---

## 🎯 Próximos Passos

1. **Teste o Binário**
   ```bash
   PrecisionMouseFilter.exe
   ```

2. **Crie uma Release no GitHub**
   - Adicione tags: `git tag v1.0.0`
   - Push tags: `git push origin v1.0.0`

3. **Distribua o .exe**
   - Upload em GitHub Releases
   - Distribua via website
   - Considere criador de instalador (NSIS/MSI)

---

## 📞 Suporte

Se encontrar problemas:

1. Verifique este guia (seção Troubleshooting)
2. Abra uma issue no GitHub com:
   - Saída de `cmake --version`
   - Saída de `cmake --build ... --verbose`
   - Sistema operacional (Windows 10/11 + build number)
   - Visual Studio versão

---

**Status: ✅ PRONTO PARA COMPILAÇÃO**
