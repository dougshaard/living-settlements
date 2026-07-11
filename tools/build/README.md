# tools/build

## Ambiente

O toolchain é antigo de propósito: o Kenshi foi compilado com o Visual Studio 2010,
então o mod precisa da mesma ABI.

1. Instale o **Visual Studio 2019 ou mais novo**.
2. Instale o **compilador Visual C++ 2010 x64** (toolset `v100`). Cópias arquivadas do
   VS2010 estão no Wayback Machine — o README do KenshiLib aponta o caminho.
3. Clone o [KenshiLib_Examples_deps](https://github.com/BFrizzleFoShizzle/KenshiLib_Examples_deps)
   com `git clone` (o repo usa LFS; não baixe o .zip do GitHub).
4. Rode o `Setup.bat` dele (pede admin). Ele extrai o Boost 1.60 e define as variáveis
   de ambiente que o projeto consome:
   - `KENSHILIB_DIR`      → `<deps>/KenshiLib`
   - `KENSHILIB_DEPS_DIR` → `<deps>`
   - `BOOST_INCLUDE_PATH` → `<deps>/boost_1_60_0`
   - `BOOST_ROOT`         → `<deps>/boost_1_60_0`
5. Abra a solution e compile em **Release | x64** (o Debug está quebrado no KenshiLib).

As libs linkadas são `KenshiLib.lib` e `OgreMain_x64.lib` (em `<deps>/KenshiLib/Libraries/`).
O Boost usa auto-link — o sufixo `-vc100-` casa com o toolset.

## Sanity check

Enquanto não há CI: compile em Release|x64 e confirme que a
`plugin/x64/Release/LivingSettlements.dll` exporta `?startPlugin@@YAXXZ`:

```
dumpbin /exports LivingSettlements.dll
```
