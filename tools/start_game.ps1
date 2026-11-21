# Startet MTA 6 als Spielfenster (Unreal Engine 5.8, Level Kestrel Bay).
# Aufruf:  powershell -File tools/start_game.ps1 [-Engine "F:\UnrealEngine\UE_5.8"]
param([string]$Engine = "F:\UnrealEngine\UE_5.8")
$project = Join-Path $PSScriptRoot "..\MTA6.uproject" | Resolve-Path
Start-Process -FilePath "$Engine\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList @("`"$project`"", "/Game/MTA6/Maps/Kestrel_Bay", "-game", "-windowed", "-ResX=1600", "-ResY=900")
