# Kopiert Fahrzeug (Sportwagen) und Spielfigur (Mannequin) aus den Vorlagen der installierten Unreal Engine
# in das Projekt. Diese Epic-Inhalte werden bewusst nicht in Git eingecheckt.
# Aufruf:  powershell -File tools/setup_template_assets.ps1 [-Engine "F:\UnrealEngine\UE_5.8"]
param([string]$Engine = "F:\UnrealEngine\UE_5.8")
$src = Join-Path $Engine "Templates\TemplateResources\Standard"
$dst = Join-Path $PSScriptRoot "..\Content"
New-Item -ItemType Directory -Force "$dst\Vehicles\SportsCar", "$dst\Vehicles\PhysicsMaterials", "$dst\Mannequin" | Out-Null
Copy-Item "$src\Vehicles\Content\SportsCar\Materials" "$dst\Vehicles\SportsCar\" -Recurse -Force
foreach ($f in 'ABP_SportsCar','CtrlRig_SportsCar','PA_SportsCar','SKM_SportsCar','SK_SportsCar','SM_SportsCar','SM_SportsCar_Glass','SM_SportsCar_Wheel') {
    Copy-Item "$src\Vehicles\Content\SportsCar\$f.uasset" "$dst\Vehicles\SportsCar\" -Force
}
Copy-Item "$src\Vehicles\Content\PhysicsMaterials\*.uasset" "$dst\Vehicles\PhysicsMaterials\" -Force
Copy-Item "$src\Mannequin\Content\Animations", "$src\Mannequin\Content\Character" "$dst\Mannequin\" -Recurse -Force
Get-ChildItem "$dst\Mannequin" -Recurse -Filter *.FBX | Remove-Item -Force
Write-Host "Fertig: Vehicles und Mannequin kopiert."
