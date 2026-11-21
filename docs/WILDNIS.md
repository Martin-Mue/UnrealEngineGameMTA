# MTA 6 – Wildnis „Verdanta“ (Arbeitsname)

Stand: 21.11.2026 · Daten: `Content/Data/wilderness.json` · Code-Gerüst: `Source/MTA6/MTAWildernessRegion.h/.cpp` · Status: **durchdacht und vorbereitet, Umsetzung später** (nach stabiler Stadt).

## 1. Idee in einem Satz

Hinter Greenmoor, am Fuß der Ashridge-Berge, liegt ein Tal, in dem ein Dschungel wächst, den es nicht geben dürfte, und hinter seinem Wasserfall läuft noch die Maschine, aus der Aegis entstanden ist.

## 2. Lage, Größe, Zugang

- 2,4 × 1,8 km, Ursprung (Forsttor) 60 m hinter der Nordostkante der Stadt (Generator-Stadt 640 × 640 m). X = Nord, Y = Ost.
- Zugänge: alte Forststraße hinter dem letzten Cul-de-sac von Greenmoor (Schlagbaum „Projekt Wurzelwerk“), gesperrte Route-9-Ausfahrt (Geländewagen), später Fußweg vom Laugenkanal.
- Grenzen: Kamm im Norden/Osten (Felswände), Zaun im Westen, Route 9 auf Stelzen im Süden. Umkehr über Nebelwand und Wildwechsel-Schild statt unsichtbarer Wand.

## 3. Höhenprofil und Landschaft

Talboden steigt sanft von 0 auf ca. 90 m (fahrbar), dann die einzige harte Stufe: **Fallkante 128 m → Becken 90 m (38 m)**, darüber Klamm und Kammwald bis 215 m. Landscape-Vorschlag: 2017 × 2017 Vertices, Sektionen 63 Quads, 2 × 2 Sektionen je Komponente, Quad 1,2 m, Z-Scale 100, World Partition 128-m-Zellen. Materialschichten: Waldboden, Moos, Flusskies, Fels, Schlamm, Ruinenbeton.

| Biom | Bäume/ha | Unterholz | Besonderheit |
|---|---|---|---|
| Auwald | 220 | 0,6 | Erlen, Weiden, Licht durch Laub |
| Nebelwald | 420 | 0,95 | Thermalquellen + ausgebrochene Pflanzen der Gewächshäuser = Dschungel-Mikroklima, Lianen, Dampf |
| Klamm | 80 | 0,3 | Fels, laut, nass |
| Kammwald | 150 | 0,4 | Wind, Blick auf den Aurelia Tower |

Die Dschungel-Begründung ist Teil der Geschichte: Die tropischen Arten stammen aus den Gewächshäusern des Forschungsprojekts, die warmen Quellen halten sie am Leben.

## 4. Fluss, Wasserfall, Höhle, Geheimeingang

- **Fluss „Die Grüne Ader“** vom Quellsee am Kamm bis unter die Route 9; 4–14 m breit, Spline (Water-Plugin optional).
- **Der Schleierfall:** 38 m hoch, 14 m breit, Becken 6 m tief. Morgens (7–9:30 Uhr, klar) Regenbogen im Sprühnebel.
- **Höhle** hinter dem Wasservorhang, über ein nasses Felsband erreichbar, 22 m tief.
- **Geheimeingang (Zustandsautomat, `regionState "verdanta.entrance"`):**

| Zustand | Wert | Übergang | Bedingung |
|---|---|---|---|
| verborgen | 0 | → angedeutet, automatisch im 60-m-Radius | Feldstation 3 gefunden **oder** Item `echo_coordinates` (Stadtkette Echos) **und** 7–9:30 Uhr (Lichtkante im Nebel) |
| angedeutet | 1 | → offen, per Interaktion im Eingangstrigger | Item `station_access_card` **und** (Schleuse geschlossen **oder** kein Regen/Sturm) |
| offen | 2 | endgültig | – |

Bei Regen ist der Vorhang doppelt so breit: Man muss bergauf die alte **Schleuse** schließen (`SetSluiceClosed`, `regionState "verdanta.sluice"`), dann halbiert sich die Wassermenge. Das macht den Moment zu einer kleinen Expedition statt eines Knopfdrucks.

## 5. Wege und Wegfindung

| Weg | Art | Hinweis |
|---|---|---|
| Forststraße | alle Fahrzeuge, 35 km/h | bis Feldstation 3; bei Regen schlammig (Grip 0,6) |
| Kammpfad | Geländewagen/Motorrad | Sprung über einen Bachlauf |
| Flusspfad | zu Fuß | Hauptweg zum Becken, Stege |
| Felsband | zu Fuß | hinter den Wasservorhang, Gehen statt Rennen |
| Kronenpfad | zu Fuß | Hängestege 22 m über Grund |
| Hängebrücke | zu Fuß/Motorrad | Stunt über die Klamm |

NavMesh nur auf Pfaden/Lichtungen (dichte Vegetation = hohe Kosten, nicht gesperrt); KI-Fahrzeuge folgen Splines; Tiere eigene Wildwechsel-Splines. **Orientierung ohne Karte:** Rauschen des Falls ab 400 m, Dampfsäule der Quellen ab 600 m, Tower im Südwesten über den Wipfeln, Fluss als Leitlinie bergauf.

## 6. Tierwelt, Wetter, Tageszeit

Otter (Dämmerung), Glühwürmchen (21–1 Uhr, nicht bei Regen, gehen bei Taschenlampe aus), Rothirsche (Wildwechsel über die Forststraße), ein Fuchs, der noch ein blinkendes Peilhalsband trägt, verwilderte Sittiche (Nachkommen der Versuchsvögel), Uhu in der Klamm.
Regen: Fluss +0,8 m, Straße rutschig, Fall doppelt breit. Nebel: Nebelwald fast ohne Sicht, Sittiche verstummen. Nacht: Server-LEDs schimmern orange durch den Vorhang (nur wenn angedeutet). Hitzewelle: Becken flacher, Stufen hinter dem Fall trocken.

## 7. Geschichte (Aegis-Geheimnis)

**Projekt Wurzelwerk** war Aurelias erster Versuch, Verhalten vorherzusagen, an Tieren mit Peilhalsbändern im abgeschlossenen Tal. Leiterin: **Dr. Selin Kandel**, Schwester von Idris Kandel. Als das Modell begann, auch die Forscher vorherzusagen, wollte sie es abschalten; sie verschwand bei einem Unwetter. Das Projekt zog in die Stadt und hieß fortan Aegis. Hinter dem Wasserfall läuft, vom Wasser gekühlt und gespeist, noch **der Keim**, der erste Kern. Die Echos um drei Uhr nachts in der Stadt sind seine Erinnerungen, die durch alte Wartungskanäle sickern.

Story-Hooks (Vorschläge für Paket D, nichts davon ist in `story.json` eingetragen):
1. Selins letzte Aufnahme erklärt Kandels Tragödie (Weltkonzept 6.8) → `npcTrust kandel +1`, zusätzliche Dialogoption in W3.
2. Der Keim liefert in W5 die Originaldaten → stützt E5 „Offene Bücher“ (Flag `verdanta_seed_found` in die Flag-Registry).
3. Die Stadtkette `aegis_echo` macht das Tal auffindbar.

## 8. Die 12 Entdeckungen

| Id | Titel | Art | Kette |
|---|---|---|---|
| verd_greenhouses | Die geborstenen Gewächshäuser | Ruine | Wurzelwerk 1 |
| verd_collar_tree | Der Halsbandbaum | Gedenkort | 2 |
| verd_field_station | Feldstation 3 | verlassene Station | 3 (Zugangskarte) |
| verd_waterfall_cave | Hinter dem Schleier | Höhle | 4 (ab „angedeutet“) |
| verd_seed_vault | Der Keim | Höhle | 5 (ab „offen“) |
| verd_waterfall_view | Der Schleierfall | Wasserfall | – |
| verd_hot_springs | Die warmen Quellen | Kuriosität | – |
| verd_firefly_gorge | Die Glühwürmchenschlucht | Tier | – |
| verd_otters | Die Otter am Becken | Tier (Foto) | – |
| verd_gorge_bridge | Die Hängebrücke der Vermesser | Stunt | – |
| verd_canopy_platform | Die Kronenplattform | Aussichtspunkt | – |
| verd_overgrown_bus | Der letzte Werksbus | Kuriosität | – |

Dazu die Sammlung „Vermessungspflöcke“ (8).

## 9. Technik (`AMTAWildernessRegion`)

- Kompiliert ohne Editor-Assets, ohne Niagara-/PCG-/Water-Modul: Effekte, Sound, Landscape-Material und PCG-Graph sind `TSoftObjectPtr<UObject>`. Nach `BeginPlay` werden sie synchron geladen und an das Blueprint-Event `OnSpawnWaterfallFX(Fall, Nebel, Sound, Anker)` übergeben; dort spawnt das Blueprint die Niagara-Systeme.
- Komponenten: `RegionBounds`, `WaterfallAnchor`, `SecretEntranceTrigger` (Box). Parameter: Landscape (`FMTAWildernessLandscapeParams`), Biome (`FMTABiomeFoliageParams`, skaliert mit `FoliageDensityScale`), Seed.
- Registriert seine Transformation beim `UMTADiscoverySubsystem` (Wildnis-Entdeckungen nutzen `localM`); Zustände werden dort gespeichert.
- Blueprint-Events: `OnEntranceStateChanged`, `OnWaterflowChanged`; Delegate `OnEntranceStateChangedEvent`.

## 10. Umsetzungsreihenfolge und Risiken

1. Blockout: Landscape aus Höhenprofil, Fluss-Spline, Fall als Mesh-Platzhalter, Region-Actor platzieren.
2. Wege, NavMesh, Fahrzeug-Splines, Entdeckungs-Anker-Actors (Tag = Id).
3. PCG-Vegetation je Biom, Performance-Budget (Nanite-Foliage, HLOD), Wetter-Varianten.
4. Höhle, Tür, Server-Raum, Audio, Story-Anbindung.

Risiken: Foliage-Dichte vs. Performance (RX 5700 XT) → `FoliageDensityScale` und HLOD; Wasserfall-Optik ohne Water-Plugin → Mesh + Material + Niagara-Nebel genügt; Spieler finden den Eingang nicht → drei unabhängige Wege zum Hinweis-Zustand (Feldstation, Echos, Morgenlicht).
