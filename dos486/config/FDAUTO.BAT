@echo off
SET DOSDIR=C:\FREEDOS
SET PATH=%DOSDIR%\BIN;C:\
SET DIRCMD=/P /OGN /Y
SET TEMP=C:\TEMP
SET TMP=C:\TEMP
SET BLASTER=A220 I5 D1 H5 P330 T6
if not exist C:\TEMP\NUL mkdir C:\TEMP
SET OS_NAME=FreeDOS
SET OS_VERSION=1.3
PROMPT $P$G
alias reboot=fdapm warmboot
alias halt=fdapm poweroff
alias juegos=call C:\MENU.BAT

if exist %DOSDIR%\BIN\CTMOUSE.EXE LH CTMOUSE.EXE

echo.
echo  DOS 486 - FreeDOS 1.3 - CPU 486, Sound Blaster 16, VGA
echo  Escribe JUEGOS para abrir el menu, o DIR para explorar.
echo.
vdelay 1500
call C:\MENU.BAT
