# Integration: Fahrzeug- und Figurenmodelle

Verkehr, Polizei und Passanten der Welt-KI (`AMTAWorldAIManager`) sowie die Figuren der lebendigen Welt (`AMTALivingNPC`) nutzen jetzt echte Modelle bzw. Variation statt Würfel/Zylinder.

## Assets (Epic-Vorlagen, nicht in Git)

Kopieren mit:

```
powershell -ExecutionPolicy Bypass -File tools/setup_template_assets.ps1
```

Quelle: `F:\UnrealEngine\UE_5.8\Templates\TemplateResources\Standard`. Das Skript prüft am Ende, ob jeder `/Game/...`-Verweis in den kopierten Paketen eine Datei im Projekt hat (Ausgabe: `Alle /Game/-Verweise aufgeloest`).

| Ziel im Projekt | Quelle | Interner Pfad | Verwendung |
|---|---|---|---|
| `Content/Vehicles/SportsCar` | `Vehicles/Content/SportsCar` | `/Game/Vehicles/SportsCar/...` | Spielerauto (unverändert), Verkehr, Polizei: `SM_SportsCar`, `SM_SportsCar_Glass`, `SM_SportsCar_Wheel` |
| `Content/Vehicles/OffroadCar` (komplett) | `Vehicles/Content/OffroadCar` | `/Game/Vehicles/OffroadCar/...` | Verkehr: `SM_Offroad_Body`, `SM_Offroad_Tire` (Materialien sind Instanzen von `M_SportsCarBase`) |
| `Content/ConceptCar/Car` | `ConceptCar/Content/Car` | `/Game/ConceptCar/Car/...` | Verkehr (Limousine/Coupé): `SM_AutomotiveTP_Car` (Räder im Mesh) |
| `Content/Mannequin` | `Mannequin/Content` | `/Game/Mannequin/...` | Spieler, `AMTALivingNPC` (unverändert) |

`.gitignore` enthält `Content/Vehicles/`, `Content/ConceptCar/`, `Content/Mannequin/`.

Material-Parameter (per Textsuche in den .uasset gefunden):

- `M_SportsCarBase` (Sportwagen- und Offroad-Lack): Vektor `Paint Tint`
- `M_CarPaint` (Concept Car): Vektor `PaintColor`
- `M_UE4Man_Body` (Mannequin): `BodyColor`, `PlasticColor`, `LogoColor`

Der Manager sucht den Lack-Slot und den Parameter zur Laufzeit (`FindPaintSlot`), die Namen sind also nicht fest verdrahtet.

## Welt-KI (`MTAWorldAIManager.*`)

- Drei Karosserie-Sorten: Sportwagen, Geländewagen und Concept Car. Gewichte stehen in `ModelWeights`, jede Sorte kommt mindestens einmal vor.
- Einpassung aus `GetBoundingBox()`:
  - Länge × Breite auf `*TargetSize` (Standard 440 × 190 cm, Gelände 450 × 195).
  - Pivot mittig, Unterkante auf Bodenfreiheit (`BodyClearance`).
  - Die Vorlagen schauen in +X.
- Räder:
  - Sportwagen und Gelände bekommen je 4 Rad-Instanzen. Sie drehen sich mit dem Tempo.
  - Lage über `SportsAxleFactor`/`OffroadAxleFactor` und `WheelDiameter`.
  - Steht die Felge innen, `bFlipWheelSides` setzen.
- Lack: eine `UMaterialInstanceDynamic` je (Sorte, Farbe) aus `TrafficColors`, also eine Instanzkomponente je Gruppe. Das sind höchstens 24 Karosserie-Komponenten (3 Sorten × 8 Farben), dazu 2 Rad-Komponenten und 1 Glas-Komponente.
  - Palette hell und satt (Weiß, Silber, Rot, Blau, Gelb, Orange, Grün, Türkis), keine fast schwarzen Töne.
  - `PaintBrightness` (Standard 1.5, 0.1–4) multipliziert die Farbe vor dem Setzen. `Paint Tint` multipliziert die Lacktextur, Werte über 1 hellen sie also auf. Beim Concept Car (`PaintColor`) klemmt die Grundfarbe bei 1.
- Lichter (keine echten Lichtquellen):
  - Je Fahrzeug 2 Scheinwerfer (`HeadLightISM`) und 2 Rücklichter (`TailLightISM`) als abgeflachte Engine-Kugeln (`VehicleLightMesh`). Instanzindex = Fahrzeugindex × 2 + Seite.
  - Material: `M_MTA_LampEmissive` (`VehicleLightMaterialAsset`) als MID mit `LampLuminance` = `VehicleLightLuminance` (Standard 150). Farbe per Custom-Data 0–2 (`LampColor`); leuchtet nur nachts (MPC `Night`).
  - Lage aus den Bounds: `HeadLightPlacement`/`TailLightPlacement` (Einzug ab Fahrzeugende in cm, Anteil der halben Breite, Anteil der Karosseriehöhe), Größe `HeadLightSize`/`TailLightSize`. Die Lichter werden wie Räder und Sirenen jedes Frame mitbewegt.
  - Bremslicht (`BrakeLightColor`, heller) beim Verzögern (0,4 s Haltezeit), im Stand und beim Warten.
  - Polizei: kaltweiße Frontlichter (`PoliceHeadLightColor`), bei Sirene wechselseitig blinkend.
  - Fehlt das Lampenmaterial, gibt es eine Warnung und die Lichter nutzen das Grundform-Material (nicht emissiv).
- Polizei:
  - Sportwagen mit `PoliceColor` (dunkelblau, Luminanz < 0,25).
  - Weißer Seitenstreifen als flacher Quader mit eigener Materialinstanz (`PoliceStripeColor`, Lage über `PoliceStripePlacement`).
  - Sirenenbalken (rot/blau, blinkend) auf dem Dach (`PoliceRoofXFactor`).
- Leistung:
  - `MaxTotalVehicles` = 60 (Verkehr + Polizei); der Verkehr wird bei Bedarf gekürzt.
  - Instanz-Cull-Distanz: `VehicleCullDistance` und `PedestrianCullDistance`.
  - Fahrzeugschatten nur nah: Eine Komponente wirft nur Schatten, wenn eines ihrer Fahrzeuge näher als `VehicleShadowDistance` ist. Ferne Kaskaden sind aus.
  - Räder, Glas, Streifen, Sirenen und Köpfe werfen keine Schatten.
- Rückfall: Fehlt ein Asset, fällt die Sorte weg. Fehlen alle Sorten (oder ist `bUseVehicleModels` = false), gibt es Würfel wie bisher.
- Passanten:
  - Schlanker Zylinder als Körper (je Kleidungsfarbe) und eine Kugel als Kopf (je Hautton, `PedestrianSkinColors`).
  - Größe (0.93–1.07) und Statur werden deterministisch je Index variiert.
- Selbsttest: `FString RunModelSelfTest()` (BlueprintCallable, am Manager). Prüft auch: keine zu dunkle Lackfarbe, Scheinwerfer + Rücklichter = 4 × Fahrzeuge, emissives Lichtmaterial gesetzt, Scheinwerfer vorn und Rücklichter hinten. Letzte Zeile: `MODELLTEST ERGEBNIS: n Fehler`.

## Figuren (`MTALivingNPC.*`)

`BindAgent` ruft `ApplyLookFromId(Id, Faction, Scale)` auf:

- Farben deterministisch aus der CRC der Kennung (Palette).
- Fraktion als Akzent (Logo) mit leichter Körpertönung: aurelia dunkelblau/silber, kcg schwarz, union orange, collective türkis, wrenchers rot.
- Größe über die Actor-Skalierung (0.93–1.07, vom Manager).
- Statur (Breite 0.94–1.06) nur am Mesh; die Kapsel bleibt unverändert.

## Was im GameMode zu tun ist

Nichts Pflichtiges. `SpawnWorldAI()` erzeugt den Manager wie bisher, und die Modelle werden in `BeginPlay` geladen. Für den Selbsttest:

```cpp
if (WorldAI) { const FString R = WorldAI->RunModelSelfTest(); /* R enthaelt "MODELLTEST ERGEBNIS: n Fehler" */ }
```

Erst nach dem `BeginPlay` des Managers aufrufen, sonst schlägt die Instanzprüfung fehl.

## Bekannte Grenzen

- Lage der Räder, Bodenfreiheit, Dachhöhe und Streifen sind aus den Bounds geschätzt. Falls etwas schwebt oder einsinkt, die Faktoren am Manager anpassen (Log-Kategorie `LogMTAWorldAIModels` gibt die Maße aus).
- `Paint Tint` multipliziert die Lacktextur. Sehr dunkle Basistexturen ergeben gedämpftere Farben.
- Ob `BodyColor`/`PlasticColor`/`LogoColor` am Mannequin sichtbar wirken, hängt vom Material ab. Fehlende Parameter werden ignoriert.
