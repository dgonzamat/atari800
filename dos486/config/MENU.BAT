@echo off
C:
cd \
:menu
cls
echo ==============================================================
echo    DOS 486  -  Juegos y programas de los anos 80 y 90
echo ==============================================================
echo.
echo   JUEGOS (shareware / freeware)
echo    1. DOOM 1.9 (1993)             6. Commander Keen 4 (1991)
echo    2. Wolfenstein 3D (1992)       7. Dangerous Dave (1990)
echo    3. Duke Nukem 3D (1996)        8. Scorched Earth (1991)
echo    4. Duke Nukem II (1993)        9. Epic Pinball (1993)
echo    5. Commander Keen 1 (1990)     A. SkyRoads (1993)
echo                                   B. ZZT (1991)
echo    F. Lemmings demo (1991)        C. Heretic (1994)
echo    G. Prince of Persia demo       H. Stunts demo (1990)
echo    I. Carmageddon demo (1997, pide Pentium)
echo    J. Another World demo (1991)
echo   PROGRAMAS
echo    D. Turbo Pascal 5.5 (1989)     E. Editor de texto (EDIT)
echo    K. Telix 3.21 (terminal para BBS por modem)
echo.
echo    Q. Salir a la linea de comandos DOS
echo.
choice /c:123456789ABCDEFGHIJKQ /n "  Elige una opcion: "
if errorlevel 21 goto salir
if errorlevel 20 goto telix
if errorlevel 19 goto another
if errorlevel 18 goto carma
if errorlevel 17 goto stunts
if errorlevel 16 goto prince
if errorlevel 15 goto lemmings
if errorlevel 14 goto edit
if errorlevel 13 goto tp
if errorlevel 12 goto heretic
if errorlevel 11 goto zzt
if errorlevel 10 goto sky
if errorlevel 9 goto pinball
if errorlevel 8 goto scorch
if errorlevel 7 goto dave
if errorlevel 6 goto keen4
if errorlevel 5 goto keen1
if errorlevel 4 goto duke2
if errorlevel 3 goto duke3d
if errorlevel 2 goto wolf
if errorlevel 1 goto doom
goto menu

:doom
cd \GAMES\DOOM
DOOM.EXE
goto volver
:wolf
cd \GAMES\WOLF3D
WOLF3D.EXE
goto volver
:duke3d
%G2%
cd \GAMES\DUKE3D
DUKE3D.EXE
goto volver
:duke2
cd \GAMES\DUKE2
NUKEM2.EXE
goto volver
:keen1
cd \GAMES\KEEN1
KEEN1.EXE
goto volver
:keen4
cd \GAMES\KEEN4
KEEN4E.EXE
goto volver
:dave
cd \GAMES\DAVE
DAVE.EXE
goto volver
:scorch
cd \GAMES\SCORCH
SCORCH.EXE
goto volver
:pinball
cd \GAMES\EPICPIN
PINBALL.EXE
goto volver
:sky
cd \GAMES\SKYROADS
SKYROADS.EXE
goto volver
:zzt
cd \GAMES\ZZT
ZZT.EXE
goto volver
:prince
cd \GAMES\PRINCE
PRINCE.EXE
goto volver
:another
cd \GAMES\ANOTHER
AWDEMO.EXE
goto volver
:telix
cd \PROGRAMS\TELIX
TELIX.EXE
goto volver
:carma
%G2%
cd \GAMES\CARMA
DATA.EXE
goto volver
:stunts
cd \GAMES\STUNTS
LOAD.EXE /u MCGA /ssb
goto volver
:lemmings
cd \GAMES\LEMMINGS
call VGALEMMI.BAT /V
goto volver
:heretic
%G2%
cd \GAMES\HERETIC
HERETIC.EXE
goto volver
:tp
cd \PROGRAMS\TP55
TURBO.EXE
goto volver
:edit
cd \
EDIT.EXE
goto volver

:volver
C:
cd \
goto menu

:salir
cls
echo Escribe JUEGOS para volver al menu.
echo.
