@echo off
:: Moonlight HB - Build PKG para PS4
:: Duplo clique para gerar out\Moonlight-HB_*.pkg
::
:: Na primeira vez, edite os paths abaixo se necessario.
:: Depois disso, so dar duplo clique.

set OO_PS4_TOOLCHAIN=C:\Users\denis\Videos\Marcos\OpenOrbis-PS4-Toolchain-0.5.4\OpenOrbis-PS4-Toolchain-0.5.4
set GOLDHEN_SDK=C:\Users\denis\Videos\Marcos\GoldHEN_Plugins_SDK

:: Converte paths para formato WSL
set OO_WSL=/mnt/c/Users/denis/Videos/Marcos/OpenOrbis-PS4-Toolchain-0.5.4/OpenOrbis-PS4-Toolchain-0.5.4
set GH_WSL=/mnt/c/Users/denis/Videos/Marcos/GoldHEN_Plugins_SDK

echo.
echo === Moonlight HB - Build PS4 PKG ===
echo.

:: Verifica se WSL esta disponivel
wsl echo ok >nul 2>&1
if errorlevel 1 (
    echo ERRO: WSL nao encontrado. Instale o Ubuntu pelo Microsoft Store.
    pause
    exit /b 1
)

:: Roda o build no WSL
wsl bash -c "cd ~/moonlight && OO_PS4_TOOLCHAIN='%OO_WSL%' GOLDHEN_SDK='%GH_WSL%' make pkg 2>&1"

if errorlevel 1 (
    echo.
    echo === BUILD FALHOU ===
    echo Veja os erros acima.
) else (
    echo.
    echo === PKG GERADO ===
    echo Arquivo: moonlight\out\Moonlight-HB_*.pkg
    echo Copie para um pendrive e instale na PS4.
)

echo.
pause
