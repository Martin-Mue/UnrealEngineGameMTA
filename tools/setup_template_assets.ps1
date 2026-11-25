# Kopiert Fahrzeuge (Sportwagen, Gelaendewagen, Concept Car) und Spielfigur (Mannequin) aus den Vorlagen
# der installierten Unreal Engine in das Projekt. Diese Epic-Inhalte werden bewusst nicht in Git eingecheckt.
# Aufruf:  powershell -ExecutionPolicy Bypass -File tools/setup_template_assets.ps1 [-Engine "F:\UnrealEngine\UE_5.8"]
#
# Interne Paketpfade (aus den .uasset geprueft):
#   Vehicles\Content\...        -> /Game/Vehicles/...        -> Content\Vehicles\...
#   ConceptCar\Content\Car\...  -> /Game/ConceptCar/Car/...  -> Content\ConceptCar\Car\...
#   Mannequin\Content\...       -> /Game/Mannequin/...       -> Content\Mannequin\...
param([string]$Engine = "F:\UnrealEngine\UE_5.8")
$ErrorActionPreference = "Stop"
$src = Join-Path $Engine "Templates\TemplateResources\Standard"
$dst = Join-Path $PSScriptRoot "..\Content"
New-Item -ItemType Directory -Force "$dst\Vehicles\SportsCar", "$dst\Vehicles\PhysicsMaterials", "$dst\Mannequin" | Out-Null

# --- Sportwagen (Spielerfahrzeug, Verkehr, Polizei) ---------------------------------------------
Copy-Item "$src\Vehicles\Content\SportsCar\Materials" "$dst\Vehicles\SportsCar\" -Recurse -Force
foreach ($f in 'ABP_SportsCar','CtrlRig_SportsCar','PA_SportsCar','SKM_SportsCar','SK_SportsCar','SM_SportsCar','SM_SportsCar_Glass','SM_SportsCar_Wheel') {
    Copy-Item "$src\Vehicles\Content\SportsCar\$f.uasset" "$dst\Vehicles\SportsCar\" -Force
}
Copy-Item "$src\Vehicles\Content\PhysicsMaterials\*.uasset" "$dst\Vehicles\PhysicsMaterials\" -Force

# --- Gelaendewagen (Verkehr): komplett inkl. Materialien und Texturen ----------------------------
# Die Offroad-Materialien sind Instanzen von /Game/Vehicles/SportsCar/Materials/M_SportsCarBase (oben kopiert).
Copy-Item "$src\Vehicles\Content\OffroadCar" "$dst\Vehicles\" -Recurse -Force

# --- Concept Car (Verkehr, Limousine/Coupe) -----------------------------------------------------
# Ohne SampleScene (Beispiellevel wird nicht gebraucht).
New-Item -ItemType Directory -Force "$dst\ConceptCar" | Out-Null
Copy-Item "$src\ConceptCar\Content\Car" "$dst\ConceptCar\" -Recurse -Force

# --- Mannequin -----------------------------------------------------------------------------------
Copy-Item "$src\Mannequin\Content\Animations", "$src\Mannequin\Content\Character" "$dst\Mannequin\" -Recurse -Force

# Quelldateien (FBX) werden nicht gebraucht
Get-ChildItem "$dst\Mannequin", "$dst\Vehicles", "$dst\ConceptCar" -Recurse -Filter *.FBX | Remove-Item -Force

# --- Pruefung: jeder /Game/-Verweis in den kopierten Paketen muss eine Datei im Projekt haben -------
# Ausnahmen (fehlen auch in der Epic-Vorlage selbst, werden zur Laufzeit nie geladen):
#   - Importdaten-Verweis auf das Skelett beim FBX-Import,
#   - Editor-Soft-Verweise in MI_SportsCarBody (alte Textur, Texture-Graph),
#   - alter eigener Paketname eines Assets (z. B. /Game/Character/... im Mannequin): Eintrag endet auf den Dateinamen.
$ignore = @('/Game/Vehicles/SportsCar/SportsCar_Skeleton',
            '/Game/Vehicles/OldTextures/T_SportsCar_Body_BC',
            '/Game/Vehicles/SportsCar/Materials/Textures/TG_SportsCar_Body_MRA')
$missing = @{}
$files = Get-ChildItem "$dst\Vehicles", "$dst\ConceptCar", "$dst\Mannequin" -Recurse -Include *.uasset, *.umap
foreach ($file in $files) {
    $text = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($file.FullName))
    foreach ($m in [regex]::Matches($text, '/Game/[A-Za-z0-9_/]+')) {
        $pkg = $m.Value
        if ($ignore -contains $pkg) { continue }
        if ($pkg.EndsWith('/' + $file.BaseName)) { continue }
        $rel = $pkg.Substring(6).Replace('/', '\')
        if ((Test-Path "$dst\$rel.uasset") -or (Test-Path "$dst\$rel.umap") -or (Test-Path "$dst\$rel" -PathType Container)) { continue }
        $missing[$pkg] = $file.Name
    }
}
if ($missing.Count -gt 0) {
    Write-Warning "Fehlende Abhaengigkeiten:"
    $missing.GetEnumerator() | Sort-Object Name | ForEach-Object { Write-Warning ("  {0}  (verwendet in {1})" -f $_.Name, $_.Value) }
} else {
    Write-Host "Alle /Game/-Verweise aufgeloest ($($files.Count) Pakete geprueft)."
}
Write-Host "Fertig: Vehicles (SportsCar, OffroadCar), ConceptCar und Mannequin kopiert."
