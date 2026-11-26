# Integration Wildnis „Verdanta“ (AMTAWildernessBuilder)

Stand: 25.11.2026 · Code: `Source/MTA6/MTAWildernessBuilder.h/.cpp` · Daten: `Content/Data/wilderness.json` · Design: `docs/WILDNIS.md`

## Klassen

| Klasse | Aufgabe |
|---|---|
| `AMTAWildernessBuilder` | Erzeugt das Gebiet zur Laufzeit rein aus C++: Gelände (`UProceduralMeshComponent`, 4×4 Kacheln, Kollision Complex-as-Simple, synchron gekocht), Vegetation/Felsen/Bauten (16 HISM-Ebenen aus `/Engine/BasicShapes`), Wasserfall (animierte Bänder + Gischt als ISM, Tick), Becken/Bach/Teich, Höhle, Überraschungen, Wasserfallton. |
| `AMTAWildernessInteractable` | `IMTAInteractable`-Punkt (Kiste, Notiz, Fernglas, Stahltür). `Interact` meldet die Entdeckung über `UMTADiscoverySubsystem::ForceDiscover`. |
| `AMTAWildernessMarker` | Unsichtbarer Anker, Tag = Entdeckungs-Id (überschreibt `localM` aus der JSON). |

Neu in `UMTADiscoverySubsystem`: `RefreshAnchors()` (Tag-Anker nach dem Spawnen neu suchen).

## Lage

`FindOrSpawn` prüft Kandidaten gegen Stadtfläche, Route 9, Küste und die HISM-Ebenen `Roads/Water/Quay/Seabed/Piles` (hart) bzw. `Mountains/Hills` (weich) von `AMTAWorldDetail`:

1. **Nord (hinter Greenmoor)**: lokal X 329…929 m, Y −160…480 m (Stadtmitte = 0). Bei Standardstadt frei, das ist der erwartete Platz.
2. Nord-Ost (Y 0…640 m), 3. Ost (Ashridge-Vorland, um 90° gedreht).

Regionssystem: U = weg von der Stadt, V = entlang der Kante (Meter), Ursprung Südwest-Ecke, Größe 600 × 640 m. Die Zufahrt fluchtet mit einer Stadtstraße (Standard: Straße Y = +80 m, V ≈ 240 m), Asphaltband bis hinter den Parkplatz, danach Schotter-Forstweg bis zur Aussicht am Wasserfall (Steigung auf 9 % begrenzt). An der Kante ist das Gelände 5 cm über dem Stadtboden (gleiche Höhe, kein Z-Fighting).

Wichtige Orte (lokal U/V in m): Parkplatz 80/V-Zufahrt · Wrack 250/150 · Lagerfeuer 185/420 · Tierspuren 362/451 · Hängebrücke 315/476 · Wachturm 330/582 · Aussichtsfels 530/300 (Plateau) · Wasserfall 470/420 (22 m, Becken 454/420) · Höhle dahinter (Felsband von V 399 aus, Grotte 9 × 9 m mit Kiste, Notiz, Stahltür).

## Was im GameMode zu tun ist

```cpp
#include "MTAWildernessBuilder.h"
// BeginPlay, nach AMTAWorldDetail::FindOrSpawnWorldDetail(this) (dann sind Straßen/Wasser für die Platzprüfung da):
AMTAWildernessBuilder* Wild = AMTAWildernessBuilder::FindOrSpawn(this);

// RunSelfTest (optional):
if (AMTAWildernessBuilder* W = AMTAWildernessBuilder::FindWildernessBuilder(this)) { UE_LOG(LogTemp, Display, TEXT("%s"), *W->RunWildernessSelfTest()); }
```

Marker/Missionen: `GetWildernessCenter()`, `GetEntranceLocation()` (Beginn der Forststraße an der Stadtkante), `GetParkingLocation()`, `GetSecretEntranceLocation()` (Höhlenmund), `GetWaterfallLocation()`.
Statuszeile/Karte: `AMTAWildernessBuilder::GetRegionNameAt(this, PawnLocation)` liefert „Verdanta“ im Gebiet, sonst leer; `IsInsideWilderness`, `GetTerrainHeightAt`.
HUD-Meldung: `OnWildernessDiscovery(Id, Text)` (Vorhang, Kiste, Notiz, Fernglas). Funde laufen ohnehin über `UMTADiscoverySubsystem::OnSecretFound`.

## Entdeckungen (wilderness.json, district „verdanta“)

Neu: `verd_veil_passage` (Vorhang durchschritten, Script), `verd_grotto_chest` (Kiste, 1500 $, Item `survey_compass`), `verd_grotto_note`, `verd_wreck_clearing`, `verd_lookout` (Fernglas), `verd_campfire`, `verd_animal_tracks`, `verd_watchtower` (Skillpunkt Stealth).
Verankert, aber unverändert: `verd_gorge_bridge`, `verd_waterfall_view`, `verd_waterfall_cave` und `verd_seed_vault` (Kette Wurzelwerk 4/5, bleiben bis zur Feldstation unsichtbar; Stahltür ist Kulisse, gesperrt).
Nicht gebaut (bleiben ruhend, weil der Builder keine Region registriert): Gewächshäuser, Halsbandbaum, Feldstation, warme Quellen, Glühwürmchen, Otter, Kronenplattform, Werksbus.

## Assets (nur `/Engine/BasicShapes/*` und Projekt-Assets)

| Zweck | Pfad | Rückfall |
|---|---|---|
| Gelände | `/Game/MTA6/Materials/M_MTA_Terrain` (Vertex-Farben) | `M_MTA_Mountain` (Weg dann nur an der Rodung erkennbar) |
| Wasserfall | `/Game/MTA6/Materials/M_MTA_Waterfall` (transluzent, Panner, Fresnel) | `M_MTA_Water` (opak, ohne Gischt) |
| Ton | `/Game/MTA6/Audio/Wilderness/amb_waterfall` | `/Game/Audio/Generated/amb_rain` |
| Rest | `M_MTA_TreeTrunk, Foliage, Mountain, Concrete, PaintedMetal, Signage, Neon, Water, Road` | – |

Einmalig erzeugen (Editor geschlossen):
```
python tools/audio/make_waterfall_sound.py
"F:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "E:\Projekte\GTA 6\MTA6.uproject" -run=pythonscript -script="E:/Projekte/GTA 6/tools/ue/build_materials.py" -unattended -nosplash -stdout
"F:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "E:\Projekte\GTA 6\MTA6.uproject" -run=pythonscript -script="E:/Projekte/GTA 6/tools/ue_audio/import_wilderness_sounds.py" -unattended -nosplash -stdout
```
Plugin `ProceduralMeshComponent` ist in `MTA6.uproject` und `MTA6.Build.cs` eingetragen.

## Selbsttest (`RunWildernessSelfTest`, läuft unter `-nullrhi`)

Gelände + Kollision (Linientest), Instanzen 1500…6000, Sichtweiten, keine Überlappung mit der Stadt, Platzprüfung, Zufahrt fluchtet, Fahrweg-Steigung ≤ 12 % (Linientests im 1-m-Raster), Wasserfall, Höhle, Erreichbarkeit Straße → Felsband → Höhle → Grotte (Boden- und Sichtlinientests), Höhlenboden plausibel, 12 Entdeckungen registriert und verankert, Grundkörper/Materialien/Ton geladen. Letzte Zeile: `WILDNISTEST ERGEBNIS: n Fehler`.

## Tuning

- `MaxInstances` (5800, Obergrenze 6000), `FoliageDensity` (0,1…2), `CullDistanceScale` (Sichtweiten: Kronen 600 m, Stämme 300 m, Unterholz 90 m ohne Schatten; Felswand-Bauteile nie ausgeblendet).
- `GridSpacingM` 2,5 m (≈ 124 000 Dreiecke, synchrones Kollisions-Cooking beim Start, ca. 0,3–1 s). 2,0 m = feiner, aber langsamer.
- `AnimationRadiusM` (350 m): Wasserfall/Gischt/Lagerfeuer werden nur in der Nähe animiert.
- Layout-Konstanten (Felswand U 470, Fall V 420, Höhe 22 m, Pfadpunkte) stehen oben in der `.cpp` (anonymer Namespace bzw. `BuildHeightfield`).

## Risiken

- Nicht kompiliert/gestartet (keine UBT-/Editor-Läufe erlaubt). Erste Kandidaten für Nacharbeit: Material-Optik (Vertex-Farben sind lineare 8-Bit-Werte), Lage des Höhleneingangs relativ zu den Wandquads, Wasserfall-Bänder (Cube-Lokalachsen im Material).
- Fehlt `M_MTA_Waterfall`, sind die Bänder opak (Rückfall `M_MTA_Water`); der Höhleneingang ist dann nur zwischen den Bändern sichtbar, Gischt entfällt.
- Kein NavMesh-Bounds-Volumen in der Wildnis: KI-Fußgänger betreten das Gebiet nicht (gewollt).
- Der Spieler kann im Becken stehen (keine Schwimmlogik); Wasserflächen haben keine Kollision.
