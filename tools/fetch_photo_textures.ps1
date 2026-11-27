# Laedt die CC0-Fototexturen (ambientCG, 1K JPG) nach tools/photo_textures/<Id>/ und entpackt sie.
# Danach im Editor-Commandlet importieren:
#   UnrealEditor-Cmd.exe "<Projekt>\MTA6.uproject" -run=pythonscript -script="<Projekt>/tools/ue/import_photo_textures.py" -unattended -nosplash -stdout
# und die Materialien neu bauen (tools/ue/build_materials.py). Siehe docs/TEXTUREN.md.
param(
    [switch]$Force   # vorhandene Pakete erneut laden
)
$ErrorActionPreference = 'Stop'
$ids = @('Asphalt031', 'Concrete034', 'Rock064', 'Grass004', 'Ground037', 'Bricks097', 'Metal064')
$root = Join-Path $PSScriptRoot 'photo_textures'
$dl = Join-Path $root 'download'
New-Item -ItemType Directory -Force -Path $dl | Out-Null
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

foreach ($id in $ids) {
    $target = Join-Path $root $id
    if ((Test-Path (Join-Path $target "${id}_1K-JPG_Color.jpg")) -and -not $Force) {
        Write-Host "$id ist bereits vorhanden"
        continue
    }
    $zip = Join-Path $dl "$id.zip"
    $url = "https://ambientcg.com/get?file=${id}_1K-JPG.zip"
    Write-Host "Lade $url"
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing -UserAgent 'MTA6-texture-fetch'
    if (Test-Path $target) { Remove-Item -Recurse -Force $target }
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Expand-Archive -Path $zip -DestinationPath $target -Force
    if (-not (Test-Path (Join-Path $target "${id}_1K-JPG_Color.jpg"))) {
        throw "${id}: Farbtextur fehlt nach dem Entpacken ($target)"
    }
    Write-Host "$id entpackt nach $target"
}
Write-Host "Fertig. Jetzt tools/ue/import_photo_textures.py im Editor-Commandlet ausfuehren."
