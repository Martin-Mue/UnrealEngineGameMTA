@echo off
rem Startet MTA 6 (Unreal Engine 5.8, Level Kestrel Bay). Doppelklick genuegt.
set ENGINE=F:\UnrealEngine\UE_5.8
start "" "%ENGINE%\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0MTA6.uproject" /Game/MTA6/Maps/Kestrel_Bay -game -windowed -ResX=1600 -ResY=900
