# tools/build

## Ambiente (RISK-009 — toolchain antigo, documentado)

1. Instalar **Visual Studio 2019 ou mais novo**.
2. Instalar o **compilador Visual C++ 2010 x64** (toolset `v100`) — cópias
   do VS2010 estão arquivadas no Wayback Machine (ver README do KenshiLib).
3. Clonar `https://github.com/BFrizzleFoShizzle/KenshiLib_Examples_deps`
   (via `git clone` — o repo usa LFS; não baixar o .zip do GitHub).
4. Rodar `Setup.bat` do deps (eleva para admin): extrai o Boost 1.60 e
   define as variáveis de ambiente de usuário que o vcxproj consome:
   - `KENSHILIB_DIR`        → `<deps>/KenshiLib`
   - `KENSHILIB_DEPS_DIR`   → `<deps>`
   - `BOOST_INCLUDE_PATH`   → `<deps>/boost_1_60_0`
   - `BOOST_ROOT`           → `<deps>/boost_1_60_0`
5. Abrir `plugin/LivingSettlements.sln` e compilar **Release | x64**.
   (Debug está quebrado no KenshiLib — aviso do README oficial.)

Libs linkadas: `KenshiLib.lib` + `OgreMain_x64.lib`
(`<deps>/KenshiLib/Libraries/`). Boost usa auto-link (`-vc100-` casa com
o toolset v100).

## Smoke build

Enquanto não há CI: compilar Release|x64 e conferir que
`plugin/x64/Release/LivingSettlements.dll` exporta `?startPlugin@@YAXXZ`
(`dumpbin /exports LivingSettlements.dll`).
