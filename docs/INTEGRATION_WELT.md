# Integration: Welt-Details Kestrel Bay (AMTAWorldDetail)

Stand: 24.11.2026. Betrifft `Source/MTA6/MTAWorldDetail.*`, `MTACityGenerator.*`, `MTADistrict.*`,
`tools/ue/build_materials.py`, `tools/ue/build_level.py`. `MTA6GameMode.*` wurde **nicht** geändert, die
nötigen Änderungen stehen unten (Abschnitt 4).

## 1. Ablauf

1. **Bauen** (UBT, Modul MTA6). Neue Klasse `AMTAWorldDetail`, neue Structs `FMTACityLot`,
   `FMTAAegisCameraPoint`, `FMTALandmarkInfo`, `FMTABillboardSpot`, `FMTAEnterableSite`, `FMTAFurnitureStats`,
   neues Enum `EMTARoofStyle`, `EMTADetailLayer`. Keine neuen Modulabhängigkeiten (TextRender liegt in Engine).
2. **Materialien**: `build_materials.py` ausführen (Commandlet wie bisher). Neu: `M_MTA_Roof`, `M_MTA_Water`,
   `M_MTA_Concrete`, `M_MTA_PaintedMetal`, `M_MTA_Neon`, `M_MTA_Signage`, `M_MTA_Mountain`, `M_MTA_Road`;
   `M_MTA_Building` hat neue Eingänge (Custom-Data 8–13 + Parameter `NeonLuminance`).
3. **Level**: `build_level.py` ausführen. Es spawnt nach dem Stadtgenerator den Aktor `WorldDetail`
   (Tag `MTA6_Generated`), setzt `City`, alle Materialien, `Seed = 6` und ruft `Generate()`.
   Wichtig: Das Skript legt den Generator neu an – nur so bekommen die Bezirksprofile die neuen Werte
   (`LotSplitWeights`/`HeightBias`-Anpassungen). Die Look-Felder (Version 2) werden auch in alten Leveln
   automatisch ergänzt (`LookVersion`, siehe `MTADistrict::CopyLookFields`).
4. **GameMode** anpassen (Abschnitt 4) und Selbsttest starten (`-MTA6SelfTest`).

## 2. Was erzeugt wird

| Bereich | Inhalt |
|---|---|
| Küste/Meer | Generator schneidet den Boden an der Küstenlinie ab (`bGenerateCoast`, `CoastOffset` 80 m westlich des Rasters, Landzungen ab \|X\| > 520 m → Bucht). Wasser: feine 20-m-Kacheln bis 1,2 km vor der Küste (WPO-Wellen), danach 200-m-Kacheln bis 6 km. Meeresgrund (Kollision) bei −9 m, Wasserspiegel −1,5 m. Kaimauern im Stadtbereich, Felsbrocken an den Landzungen, gepflasterte Promenade, Strand in Sunstrip. Ferne Landplatten außerhalb der Bodenfläche. |
| Straßen | Coast Road nach Nord/Süd entlang der Bucht (mit Leitplanke zur Meerseite), Route 9 nach Osten bis zum gesperrten Portal der Ashridge Tube (Schranke, Schild, Aegis-Checkpoint). |
| Berge | Hauptgipfel (420 m) mit Tunnelportal, Ostkamm (Ashridge, 220–520 m, Schnee ab 380 m), Nordkamm, niedrigere Südhügel, Rückenlinien, Landzungen-Hügel, ferner Dunst-Ring (3–4 km). Nebel-Verlauf im Material (Entfernung + Talhöhe) zusätzlich zum ExponentialHeightFog. |
| Core | Aurelia Tower (Generator), 5 höchste Türme mit Aegis-Augen-Logo (2 Fassaden) + Flugwarnlicht; Generator: gestufte Türme mit Lichtkrone (RoofStyle Setback). |
| Saltmarket | Containerterminal (Mole 80 × 240 m), Nordmole mit Molenfeuer, Hafenbecken dazwischen, **Kranreihe** (3 rote Containerbrücken, blinkende Warnlichter), ~300 Container, Hafentor mit Kamera. |
| Lantern Hill | Platzblock mit **Glockenturm St. Merrin** (Terrasse, beleuchtete Treppe, glühende Glockenstube, Uhren), **Antennenwald** auf allen Altstadtdächern, **Wäscheleinen** mit Wäsche zwischen Nachbarhäusern. |
| Greenmoor | Platzblock mit **Country-Club-Wasserturm** (grünes Leuchtband), bis zu 6 **Tore** (Pfeiler, Schranken, Wachhaus, Schild, Kameramast) an Einfahrten in den Bezirk. |
| Rustline | **Raffinerie** südlich der Stadt (Fackel mit flackernder Flamme, auch am Tag sichtbar; Schornsteine, Kolonnen, Tanks, Rohrbrücken, Arbeitslichter), Schrottberg + Schrotthaufen auf Brachen, Salzebene. |
| Sunstrip | **Pier** (360 m, Pfähle, Geländer, Lampen), **Kestrel Wheel** (60 m, dreht sich zur Laufzeit), **Neon-Promenade** (Bögen, Leuchtband, Palmen), große Strandwerbung. |
| Möblierung | Ampeln bzw. Stoppschilder (Anteil je Bezirk), Straßennamensschilder, Zebrastreifen-Schilder, Hydranten, Mülleimer (Bezirksfarbe), Bänke, Poller, Leitplanken, Aegis-Kameramasten (rote Linse; Dichte Core ≫ Rustline; Lantern Hill teils gehackt), Neon-Ausleger, Plakate/Graffiti, Dach-Werbetafeln mit bezirkstypischen Sprüchen (Text nur in 60 m Nähe). |
| Bezirks-Look | Neue Profilfelder (`RoofStyle`, `PitchedRoofChance`, `RoofColor`, `AccentColor`, `NeonShare`, `FacadeDetail`, `RooftopClutter`, `AegisCameraDensity`, `StreetFurnitureDensity`, `TrafficLightShare`, `GraffitiShare`, `BillboardShare`). Generator: Satteldächer (Lantern Hill Terrakotta, Greenmoor Schiefer, Saltmarket), Sheddächer (Rustline), Lichtkronen (Core), Antennen je Bezirk. Material per Custom-Data: Ladenfront mit Neonband, Ziegelfugen (Saltmarket), Balkone (Lantern Hill), Fensterläden (Greenmoor), Wellblech + Rost (Rustline), Art-Deco-Bänder (Sunstrip), Lisenen (Core). |
| Begehbare Gebäude | `Generate()` plant `PlannedEnterableSites` (84) Standorte aus den Generator-Parzellen, reihum über alle 6 Bezirke, Raumtyp passend zu Bezirk/Industrie (Kiosk, Bar, Imbiss, Werkstatt, Klinik, Lager, Büro, Wache, Wohnungen …; Safehouses bewusst nicht markiert). Breite Fassaden ergeben zwei Ladeneinheiten. `SpawnEnterableBuildings(MaxCount)` erzeugt die `AMTABuildingEntry` (Fassadenseite = Straßenseite der Parzelle) plus Türfläche, beleuchtetes Schild in Typfarbe und Text. |

## 3. Abfragen für andere Systeme

```cpp
#include "MTAWorldDetail.h"
AMTAWorldDetail* Detail = AMTAWorldDetail::FindWorldDetail(this);
const TArray<FMTAAegisCameraPoint>& Cams = Detail->GetAegisCameraPoints();      // Lage, Blickrichtung, Bezirk, Kreuzung, bHacked, Range, Kind
TArray<FMTAAegisCameraPoint> Near = Detail->GetAegisCamerasNear(PlayerLoc, 6000.f); // ohne gehackte
Detail->GetLandmarks();              // Id, Name, Bezirk (EMTADistrict::Count = Umland), Fusspunkt, Höhe
Detail->GetEnterableSites();         // geplante Standorte
Detail->GetWaterLevel();             // Welt-Z des Meeres (z. B. für Ertrinken/Fahrzeug-Wasserschaden)
City->GetCityLots();                 // Parzellen des Generators (Welt), inkl. Straßenrichtung
City->GetCoastWorldY(WorldX);        // Küstenlinie
City->GetDistrictPlaza(District, Center, Size);
```

Kamera-Arten (`Kind`): `crossing`, `gate`, `harbor`, `checkpoint`, `landmark`. Die spätere Kamera-Logik
(`AMTAChaosAegisCamera` o. Ä.) kann aus der Liste Akteure/Sichtkegel erzeugen; die Masten selbst sind
HISM-Instanzen.

## 4. Änderungen im GameMode (bitte selbst einfügen)

`MTA6GameMode.cpp`, Include ergänzen:

```cpp
#include "MTAWorldDetail.h"
```

In `BeginPlay` den Aufruf `SpawnEnterableBuildings();` ersetzen durch:

```cpp
if (AMTAWorldDetail* Detail = AMTAWorldDetail::FindOrSpawnWorldDetail(this))
{
	Detail->SpawnEnterableBuildings(72);
}
else
{
	SpawnEnterableBuildings(); // Rueckfall: alte kleine Version (4 Gebaeude am Start)
}
```

Nicht beide Versionen aufrufen: gleiche Positionen ergäben doppelte `BuildingKey`s.

In `RunSelfTest` (z. B. nach Block 2b):

```cpp
if (const AMTAWorldDetail* Detail = AMTAWorldDetail::FindWorldDetail(this))
{
	const FString WorldReport = Detail->RunWorldSelfTest();
	Check(TEXT("Welt-Details (Kueste, Berge, Landmarken, Moeblierung, Gebaeude, Budget)"), WorldReport.StartsWith(TEXT("WORLD SELFTEST OK")));
}
else
{
	Check(TEXT("Welt-Details vorhanden"), false);
}
```

Optional die bestehende Prüfung `Entries.Num() >= 1` auf `>= 60` anheben. Der Bericht wird zusätzlich
zeilenweise geloggt (`LogMTAWorldDetail`, Präfix `WORLDTEST`).

## 5. Selbsttest `RunWorldSelfTest()`

Prüft ohne Eingabe: Wasserkacheln (≥ 100), Meeresgrund, Kaimauer-/Molenteile (≥ 20), Straßensegmente
(≥ 20), Wasserspiegel < 0 und Meeresgrund darunter, Gipfel (≥ 30), Hügel (≥ 10), Landmarken je Bezirk (≥ 2),
Pflicht-Landmarken (Kranreihe, Kestrel Wheel, St. Merrin, Wasserturm, Raffinerie-Fackel), Ampeln+Stopp (≥ 60),
Straßenschilder (≥ 30), Zebrastreifen-Schilder (≥ 20), Hydranten (≥ 100), Mülleimer (≥ 100), Bänke (≥ 50),
Poller (≥ 50), Leitplanken (≥ 40), Werbetafeln (≥ 6), Kameras (≥ 30, Liste = Zähler), Plakate/Graffiti (≥ 30),
Neon-Schilder (≥ 30), Container (≥ 150), Aegis-Dichte Core > Rustline, geplante Standorte (≥ 60), in der
Spielwelt: lebende begehbare Gebäude (≥ 60), alle 6 Bezirke mit ≥ 3, Türmarkierungen; Budget: Instanzen ohne
Wasser ≤ `InstanceBudget` (30 000), Wasser ≤ `WaterTileBudget` (20 000), Komponenten ≤ `ComponentBudget` (260),
Sichtweiten aller Kleinteil-Ebenen gesetzt, Daten aktuell zur Stadt (Fingerabdruck).
Erste Zeile: `WORLD SELFTEST OK|FEHLER: n Fehler, …`.

## 6. Budget und Performance (RX 5700 XT)

- 30 HISM-Komponenten, 1 bewegliche Riesenrad-Gruppe (3 HISM am `WheelPivot`), Texte nur zur Laufzeit
  (≈ 40 Werbe-/Hinweistexte + 1 je Gebäude, Sichtweite 60 m bzw. 30 m).
- Erwartete Instanzen (Seed 6): Wasser ≈ 10 400 Kacheln (je 2 Dreiecke), Rest ≈ 12 000–20 000 (exakt im
  Selbsttest-Bericht und im Log von `Generate()`).
- Sichtweiten: Möblierung 90–150 m, Poles 150 m, Neon-Kleinteile 250 m, Container 600 m, Pfähle 400 m;
  Landmarken, Berge, Wasser, Neon-Akzente ohne Grenze (Silhouette/Orientierung). `CullDistanceScale` für
  Qualitätsstufen.
- Schatten: Kleinteile standardmäßig ohne (`bSmallPropsCastShadow`), Wasser/Neon/Schilder ohne. Distance
  Fields nur für große Bauteile (Kai, Beton, Tanks, Container), nicht für Berge/Wasser.
- Wasser ist **opak** (Lumen-Reflexionen/SSR statt Transluzenz-Overdraw), Wellen per WPO nur nahe der Kamera.
- Alles statisch außer Riesenrad (Tick nur, wenn `bAnimateWheel`).
- `Config/DefaultEngine.ini` wurde nicht geändert (vorhandene Render-Einstellungen reichen).

## 7. Bekannte Grenzen / Risiken

- Nicht kompiliert/ausgeführt (Vorgabe). Mögliche Kleinigkeiten bei UHT/MSVC oder HLSL (Custom-Nodes).
- Annahmen zu Engine-Grundkörpern: alle 100 cm, mittig gepivotet, Kegel-Spitze +Z, Ebene mit Normale +Z.
- Wasser hat keine Kollision: Spieler/Fahrzeuge sinken bis zum Meeresgrund (−9 m); Schwimmen fehlt noch.
- Kleine Möblierung (Hydranten, Mülleimer, Poller, Bänke, Ampelarme) nur Abfrage-Kollision: Fußgänger stoßen
  an, Fahrzeuge fahren durch (bewusst, statische HISM können nicht umfallen). Masten, Leitplanken, Landmarken
  haben volle Kollision.
- Wird die Stadt im Editor neu generiert, sind die Welt-Details veraltet, bis `Generate` am WorldDetail
  geklickt wird; in BeginPlay wird das über den Fingerabdruck erkannt und neu erzeugt.
- Pfützen-Decals sind nicht umgesetzt (optional).
- Die Ampelphasen sind statisch (eine Lampe je Kreuzung hell).
