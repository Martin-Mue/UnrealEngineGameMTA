# MTA 6 – Weltkonzept (Godot 4.7)

> Modulares Konzept für eine außergewöhnlich gute Spielwelt von **MTA 6**, einem **eigenständigen**, dystopisch-realistischen Open-World-Fahrspiel (Einzelspieler-Kern, später gemischt mit MMO-Elementen) mit eigener Stadt, eigenen Figuren und eigener Story.
> **Version 3 – Engine: Godot 4.7.2** (GDScript, Forward+/Vulkan, Windows-PC; Zielhardware AMD RX 5700 XT 8 GB, Ryzen 5 3600, 64 GB RAM). Frühere Versionen waren auf Browser/Three.js ausgelegt; der Inhalt (Module, Welt, Story, Meilensteine) ist erhalten und technisch auf Godot übersetzt. Ergänzt wurden Design-Vorbilder (Kapitel 9), Missionstypen und Aktivitäten (10), Speichern/Laden (11), Spielsysteme und Produktion (12) sowie Erkenntnisse und Aufwandsfortschreibung (13).
> Stand: 19.11.2026 · Sprache: Deutsch · Ausgangsbasis: Godot-Prototyp in `godot/` (`main.gd`, `main.tscn`, `project.godot`), Iterationsplanung: [`ITERATIONSPLAN.md`](ITERATIONSPLAN.md) (Zuordnung in Kapitel 14).
>
> **Abgrenzung:** Dieses Dokument verwendet ausschließlich allgemeine Genre-Prinzipien (Open-World-Design, lebendige Städte, Systemik). Karte, Namen, Figuren, Marken und Story-Elemente sind vollständig eigen – nichts davon ist von GTA oder anderen Rockstar-Titeln abgeleitet.
>
> **Hinweis zur Recherche:** Die Godot-Dokumentation wurde in den Versionen 4.0–4.6 geprüft; für **4.7.2** sind Verhalten und Einstellungen am Prototyp zu verifizieren (jeweils markiert). Zahlen zu Frame-Zeiten sind **Planwerte**, keine Messungen – sie sind mit dem Profiler auf der Zielhardware zu bestätigen.

---

## Inhalt

1. [Ist-Zustand des Godot-Prototyps](#1-ist-zustand-des-godot-prototyps)
2. [Vision und Designsäulen](#2-vision-und-designsäulen)
3. [Modulübersicht und Schnittstellen](#3-modulübersicht-und-schnittstellen)
4. [Module im Detail (M1–M12)](#4-module-im-detail)
5. [Realismus und Detailtiefe (M13)](#5-realismus-und-detailtiefe-m13)
6. [Story-Engine, Entscheidungen und Fraktionen (M14)](#6-story-engine-entscheidungen-und-fraktionen-m14)
7. [Open World trifft MMO: Server und Mehrspieler (M15)](#7-open-world-trifft-mmo-server-und-mehrspieler-m15)
8. [Die Welt: Kestrel Bay (dystopisch)](#8-die-welt-kestrel-bay-dystopisch)
9. [Design-Vorbilder: was wir übernehmen und wie wir es eigenständig umsetzen](#9-design-vorbilder-was-wir-übernehmen-und-wie-wir-es-eigenständig-umsetzen)
10. [Missionstypen und Aktivitäten](#10-missionstypen-und-aktivitäten)
11. [Speichern und Laden (SaveManager)](#11-speichern-und-laden-savemanager)
12. [Spielsysteme und Produktion](#12-spielsysteme-und-produktion-alles-was-zu-einem-spiel-dieses-umfangs-gehört)
13. [Neue Erkenntnisse, Pläne und Aufwandsfortschreibung](#13-neue-erkenntnisse-pläne-und-aufwandsfortschreibung)
14. [Roadmap mit Meilensteinen](#14-roadmap-mit-meilensteinen)
15. [Empfohlene Projektstruktur (Godot)](#15-empfohlene-projektstruktur-godot)
16. [Quellen](#16-quellen)
17. [Zusammenfassung und die 5 nächsten Schritte](#17-zusammenfassung-und-die-5-nächsten-schritte)

---

## 1. Ist-Zustand des Godot-Prototyps

Gelesen wurden `godot/main.gd`, `godot/project.godot` und `godot/README.md`. Der Prototyp entwickelt sich parallel weiter; die Tabelle gibt den **Arbeitsstand vom Abend des 19.11.2026** wieder (`main.gd` ≈ 930 Zeilen, dazu `minimap.gd`; der letzte Commit „SDFGI, Volumennebel, Laternen mit Lichtpools, Bäume, Gebäudedetails“ enthält davon nur einen Teil, Polizei/Fußgänger/Mission/Minimap liegen noch als **nicht eingecheckte Änderungen** vor; `README.md` ist veraltet). Der frühere Three.js-Prototyp (`index.html`, `game.js`) bleibt als eingefrorene Referenz erhalten; Audio ist daraus noch **nicht portiert**.

| Bereich | Ist-Zustand (Godot) | Konsequenz |
|---|---|---|
| Projekt | Godot 4.7.2, **Forward+**, MSAA 2×, Direktionalschatten-Atlas 4096; Fenster 1600×900; `main.gd` baut fast alles im Code (`_build_*`) | Gute Basis, aber ein Skript für alles → in Szenen, Autoloads und Ressourcen zerlegen (Kapitel 15) |
| Stadt | Raster 8×8 Blöcke, `P = 80 m`, Straße 16 m → ca. 640 × 640 m. **Boden = eine 4000×4000-PlaneMesh mit Spatial-Shader**, der Straßen, Mittellinien, Randlinien und Zebrastreifen prozedural aus der Weltposition zeichnet (kein Mesh pro Straße) | Sehr effizient; Shader-Konstanten (`P`, `HALF`) sind aber doppelt hartkodiert (GDScript und Shader) → **Global Shader Uniforms** verwenden |
| Gebäude | Je Lot ein `StaticBody3D` + `MeshInstance3D` (BoxMesh) + `CollisionShape3D`, ein gemeinsames `ShaderMaterial` mit `instance uniform tint`; Fenster und Nachtlicht rein im Fragment-Shader aus Weltposition (`night`-Uniform) | Keine Fenstertexturen nötig (gut); Nodes-Zahl wächst mit der Stadt → für größere Welt **MultiMeshInstance3D** und gebündelte Kollision je Chunk |
| Zufall | `RandomNumberGenerator` mit `seed = 6` | Welt ist **reproduzierbar** (Vorteil gegenüber dem alten Prototyp) |
| Fahrzeug | `VehicleBody3D` (1200 kg, Schwerpunkt abgesenkt), 4 × `VehicleWheel3D` (Federhärte 45, Reibung 3,2), Motorkraft 2000 je Antriebsrad bis 42 m/s, geschwindigkeitsabhängige Lenkung, Verfolgerkamera, `SpotLight3D` als Scheinwerfer; Optik aus Boxen | Spielbar; Godot-Doku warnt, dass `VehicleBody3D` nicht für realistische Fahrphysik gedacht ist [Q22] → Tuning im MVP, eigenes Raycast-Fahrzeug im Ausbau |
| Verkehr/Spielsysteme | 28 Verkehrs- und 60 Fußgänger-`Agent`-Objekte (RefCounted) als **Linien-Agenten** (Achse, Linie, Richtung, Position) mit gewichteter Zufallsabbiegung; Abstandhalten (`_blocked`), Stun/Ghost-Timer; **Polizei-Agenten** mit Fahndungswert (`heat`), Festnahme (`_bust`), Missionsmarker mit Zeitbonus, Geld, Toasts (Arbeitskopie) | Portierung des Browser-Prototyps läuft; weiterhin Linien statt Straßengraph, keine Ampeln, keine Sichtlinien; alles in einem Skript → in Autoloads/Szenen überführen |
| Umwelt | `WorldEnvironment` mit `ProceduralSkyMaterial`, ACES, **SSAO, SSR, Glow, Volumennebel (Dichte 0,004, GI-Inject), SDFGI (3 Cascades, Occlusion)**, Adjustment (Kontrast 1,08, Sättigung 1,12), Nebel; 1 `DirectionalLight3D` (Schatten bis 260 m); Tag/Nacht: 1 Stunde = 20 s | SDFGI und Volumennebel sind bereits aktiv (im Konzept erst für „Hoch“ vorgesehen) → Kosten auf der RX 5700 XT messen (Kapitel 5.1); `--fps`-Modus gibt die FPS nach 6 s aus |
| UI/Eingabe | HUD mit Geld, Fahndung, Mission, Toasts; **Minimap** (`minimap.gd`, `Control` mit `_draw`, Weltausschnitt dreht sich mit); `InputMap` im Code; `--test`- und `--fps`-Modus für Headless-/Messläufe | Gute Basis; Vollkarte, Menüs, Optionen fehlen |
| Beleuchtung/Details | **Laternen** als zwei `MultiMeshInstance3D` (Masten, Köpfe) plus **Pool aus 8 `OmniLight3D`**, die alle 0,25 s den nächsten Laternen zugewiesen werden (derzeit Sortierung *aller* Laternenpositionen → Spatial Hash nötig); **Bäume** (Parks, 14 % der Blöcke) als zwei MultiMeshes; Dachaufbauten/Antennen als Einzel-Nodes | Gutes Muster für Props (Kapitel 5.3); Pool-Prinzip beibehalten |
| Nicht vorhanden | Audio, Speichern, Chunk-Streaming, LOD, Straßengraph, Vollkarte, Story, Wetter | Alles Weitere in den Modulen M1–M15 |

**Schwächen, die Erweiterungen blockieren:** alles in einer Datei, Zufall nicht zentralisiert, Gebäude als Einzel-Nodes, NPC-Autos ohne Kollisionskörper, Kamera/Fahrzeug/Welt vermischt, kein Profiler-Overlay.

---

## 2. Vision und Designsäulen

**Pitch:** *Kestrel Bay* ist eine sonnenverbrannte Küstenmetropole, die von einem Konzern „optimiert“ wird: glänzende Türme voller Kameras, rostige Docks, neonbunte Nächte, ein Highway, der in die Berge schneidet. Der Konzern *Aurelia Holdings* betreibt das Überwachungsnetz **Aegis**, das Kennzeichen, Gesichter und Zahlungen verknüpft und Zugang zu Straßen, Krediten und Wohnungen steuert. Man fährt, entkommt, handelt – und entscheidet, wem die Stadt am Ende gehört. Sie funktioniert auch ohne den Spieler.

**Zielplattform:** Windows-PC, Godot 4.7.2 (Forward+/Vulkan), ausgelegt auf **AMD RX 5700 XT (8 GB, ohne Hardware-Raytracing) + Ryzen 5 3600 + 64 GB RAM**. Läuft ohne Konto-Zwang und ohne Launcher; Start in unter 15 s von SSD. Ziel: 60 fps bei 1080p im Preset „Mittel“, 1440p über FSR 2.2 im Preset „Hoch“ nach Messung. Mehrspieler optional über einen eigenen Dedicated Server.

### Die sieben Designsäulen

1. **Fahren fühlt sich großartig an.** Das Auto ist die Hauptfigur. Wenn eine leere Straße schon Spaß macht, trägt die Welt alles andere.
2. **Jeder Bezirk ist auf einen Blick erkennbar.** Silhouette, Farbpalette, Straßenmuster, Licht, Musik und Verkehr unterscheiden sich so stark, dass man ohne Karte weiß, wo man ist (Orientierung über Landmarken, siehe Quellen [Q7]).
3. **Die Stadt lebt ohne dich.** Verkehr, Passanten, Polizei und Wetter folgen eigenen Regeln. Der Spieler stört ein System, das bereits läuft (Systemik statt Skript, [Q5][Q6]).
4. **Systeme erzeugen Geschichten.** Wenige, saubere Regeln (Verfolgung, Ruf, Geld, Tageszeit, Wetter) kombinieren sich zu unvorhersehbaren Momenten; geschriebene Missionen setzen nur Anker.
5. **Hardware-ehrlich gebaut.** Alles wird gegen feste Performance-Budgets der Zielhardware entworfen (MultiMesh, Streaming, Visibility Ranges, Sim-LOD). Eine schöne Welt, die nur auf einer Highend-GPU läuft, ist keine.
6. **Glaubwürdigkeit durch Detail.** Realismus entsteht aus vielen kleinen, konsistenten Dingen (Materialien, Verschleiß, Straßendetails, plausible Stadtstruktur), nicht aus maximaler Polygonzahl. Ziel ist der bestmögliche Look *innerhalb* der Hardwaregrenzen (Kapitel 5).
7. **Entscheidungen haben Folgen, und die Welt hat eine Haltung.** Dystopie ist kein Anstrich, sondern Spielmechanik: Überwachung (Aegis), Konzernmacht und soziale Gegensätze verändern, wie man fährt, flieht und handelt. Spielerentscheidungen verschieben Fraktionen, Bezirke und Enden (Kapitel 6).

### Alleinstellungsmerkmal (Unique Selling Point)

**„Eine Stadt, die man an der Tageszeit liest.“** Jeder Bezirk hat einen eigenen Tagesrhythmus (Büros leeren sich ab 18 Uhr, der Hafen arbeitet in der Frühschicht, die Promenade lebt nachts). Missionen, Verkehr, Polizeipräsenz und Preise hängen am Rhythmus. Dazu ein **Bezirks-Ruf-System** statt eines einzigen globalen Fahndungslevels: Wer in Saltmarket auffällt, ist dort bekannt, nicht automatisch überall. Zusätzlich das **Aegis-Profil**: Kameras lesen Kennzeichen; wer oft auffällt, wird in überwachten Bezirken schneller erkannt – es sei denn, man hackt Kameras, tauscht Kennzeichen oder fährt durch die „blinden“ Armenviertel. Die Dystopie wird so zu einer Fahrregel, nicht zu Kulisse.

---

## 3. Modulübersicht und Schnittstellen

### 3.1 Architekturdiagramm

```mermaid
flowchart TB
  subgraph DATA["Daten (statisch, seedbar)"]
    CFG["config/ Budgets, Presets"]
    DIS["Districts-Definitionen"]
    CITY["CityData: Straßengraph, Parzellen, Zonen"]
  end

  subgraph SIM["Simulation (_physics_process 60 Hz, ohne Render-Abhängigkeit)"]
    GEN["M1 Weltgenerierung"]
    VEH["M4 Fahrzeuge & Physik"]
    AI["M5 Verkehr/Fußgänger-KI"]
    LAW["M6 Polizei/Gesetz"]
    MIS["M7 Missionen/Story"]
    ECO["M8 Wirtschaft/Fortschritt"]
    ENV["M9 Tag/Nacht/Wetter"]
    STO["M14 Story-Engine: Entscheidungen, Fraktionen, Weltzustand"]
  end

  subgraph VIEW["Darstellung (Godot-Renderer, _process)"]
    STR["M2 Streaming/LOD"]
    REND["Rendering: MultiMesh, Shader, Environment"]
    AUD["M10 Audio"]
    UI["M11 UI/Karte"]
    REAL["M13 Realismus-Pipeline: PBR, Decals, Verschleiß"]
  end

  PERF["M12 Performance & Tooling (Profiler, Seed-Tests, Budgets)"]
  DST["M3 Bezirke & Identität"]
  NET["M15 Server & Mehrspieler: autoritativ, Zonen, Persistenz (ab MS9)"]

  GEN --> CITY
  DIS --> GEN
  DST --> DIS
  CITY --> STR
  CITY --> AI
  CITY --> MIS
  STR --> REND
  VEH --> REND
  AI --> REND
  VEH <--> AI
  VEH <--> LAW
  AI <--> LAW
  LAW --> MIS
  MIS --> ECO
  ENV --> REND
  ENV --> AI
  ENV --> AUD
  VEH --> AUD
  AI --> AUD
  ECO --> UI
  LAW --> UI
  MIS --> UI
  CITY --> UI
  PERF -.überwacht.-> SIM
  PERF -.überwacht.-> VIEW
  MIS <--> STO
  STO --> ECO
  STO -.Weltzustand-Tags.-> DIS
  DST --> REAL
  REAL --> REND
  ENV --> REAL
  NET <-.Sim-Zustand.-> SIM
  NET <-.Story-Zustand.-> STO
```

### 3.2 Leitprinzipien der Architektur (Godot)

- **Szenen als Bausteine, Autoloads als Dienste.** Jede wiederverwendbare Einheit ist eine **Szene** (`scenes/vehicles/car.tscn`, `scenes/world/chunk.tscn`, `scenes/ui/hud.tscn`). Systeme, die es genau einmal gibt (Zeit/Wetter, Ereignisse, Spielzustand, Audio-Manager, Chunk-Manager), sind **Autoloads** (Singletons). Alles andere wird instanziert, nicht global gehalten.
- **Daten als Ressourcen.** Statische Inhalte (Bezirke, Fahrzeugdatenblätter, Fraktionen, Materialien) sind eigene `Resource`-Klassen (`class_name DistrictDef extends Resource`, `.tres`) oder JSON; Story-Inhalte in JSON (diffbar, prüfbar). **Gespeichert wird ausschließlich als JSON**, nie als `.tres/.res`, weil Godot-Ressourcen eingebetteten Skriptcode enthalten können (Sicherheitsrisiko bei fremden Dateien, vgl. Mehrspieler-Kapitel).
- **Simulation und Darstellung trennen – soweit sinnvoll.** Simulationslogik (Verkehr, Polizei, Story, Wirtschaft) läuft in `_physics_process` (fester Takt, 60 Hz) und kennt **keine** Meshes/Materialien; die Darstellung liest den Zustand. Das ermöglicht Sim-LOD, Auslagerung in Threads und – wichtig – den **Dedicated Server im Headless-Modus**, der dieselbe Simulation ohne Rendering ausführt.
- **Signale statt direkter Aufrufe.** Ein Autoload `EventBus` bündelt Signale (`crime_committed`, `mission_completed`, `district_entered`); Polizei, Missionen, Audio und UI abonnieren, ohne sich zu kennen.
- **Ein Datenmodell für die Stadt (`CityData`):** Straßengraph (Knoten, Kanten, Spuren), Parzellen, Zonen/Bezirke als Ressourcen/Objekte, geschrieben von der Weltgenerierung, gelesen von Streaming, KI, Karte und Missionen.
- **Alles seedbar:** ein zentraler `Rng`-Dienst (`RandomNumberGenerator` mit Seed je Aufgabe/Chunk: `seed = hash(world_seed, chunk_x, chunk_z)`); gleicher Seed = gleiche Stadt, **unabhängig von der Ladereihenfolge**.
- **Globale Shader-Parameter statt Doppelpflege:** Welt-Konstanten, Tageszeit (`night`), Nässe (`wetness`), Wetter, Verschmutzung als **Global Shader Uniforms** (`RenderingServer.global_shader_parameter_set`), die alle Materialien lesen – ersetzt die doppelt hartkodierten Konstanten des Prototyps.
- **GDScript zuerst, C#/GDExtension nur nach Messung.** Rechenintensive Schleifen (Verkehrs-Update für Hunderte Agenten, Generator-Mathematik, A*) werden zuerst in GDScript (typisiert!) gebaut und mit dem Profiler gemessen; erst bei nachgewiesenem Engpass auf **C#** (erfordert die .NET-Variante von Godot) oder **GDExtension** (C++/Rust) umstellen – dann nur dieses Modul.
- **Server-fähig von Anfang an:** Dieselbe Simulation läuft im Einzelspieler lokal und im Mehrspielerbetrieb auf dem Server. Der Einzelspieler nutzt die Godot-Multiplayer-API im **Offline-Modus** (`OfflineMultiplayerPeer`), sodass RPC-Code in beiden Modi identisch ist (Kapitel 7).

### 3.3 Schnittstellentabelle


| Modul | Liefert (Output) | Braucht (Input) |
|---|---|---|
| M1 Weltgenerierung | `CityData` (Straßengraph, Parzellen, Zonen, Landmarken-Slots) | Seed, Bezirksdefinitionen, Heightmap |
| M2 Streaming/LOD | Geladene Chunk-Szenen (MultiMesh, Kollisionsformen), Sichtbarkeitsbereiche | `CityData`, Kameraposition |
| M3 Bezirke | `DistrictDef` (Palette, Regeln, Dichte, Musik, Verkehrsprofil) | – (Designdaten) |
| M4 Fahrzeuge | `VehicleState` (Pose, Geschwindigkeit, Schaden), Kollisionsevents | Eingabe/KI-Steuerung, Bodenhöhe, Fahrzeugdaten |
| M5 Verkehrs-/Fußgänger-KI | Agentenzustände, Wahrnehmungsevents („Unfall gesehen“) | Straßengraph, Zeit, Wetter, Spielerposition |
| M6 Polizei/Gesetz | Fahndungsstufe pro Bezirk, Polizeiagenten, Festnahme/Flucht-Events | Verbrechensevents, Sichtlinien, Straßengraph |
| M7 Missionen/Story | Aufträge, Marker, Cutscene-/Dialogtrigger | `CityData`, Spielzustand, Events |
| M8 Wirtschaft | Geld, Ruf, Freischaltungen, Speicherstand | Missionsergebnisse, Verbrechensfolgen |
| M9 Tag/Nacht/Wetter | `Environment` (Sonne, Himmel, Nebel, Nässe, Wind) | Spielzeit, Bezirk |
| M10 Audio | Mix aus Motor-, Umgebungs-, Musik-, UI-Bussen | Fahrzeug-, Umwelt-, KI-Daten |
| M11 UI/Karte | HUD, Vollkarte, GPS-Route, Menüs | Alle Zustände (nur lesend) |
| M12 Performance/Tooling | Budgets, Profiler, Debug-Overlays, Seed-Tests | Alle Module |
| M13 Realismus-Pipeline | Material-Bibliothek (PBR), Decal-/Verschleiß-System, Prop-Sets, Qualitätsstufen | Bezirke (M3), Wetter/Zeit (M9), Streaming (M2) |
| M14 Story-Engine | Entscheidungen, Fraktionsstand, Weltzustand-Tags, Enden | Missions-Events (M7), Verbrechen (M6), Spielzustand (M8) |
| M15 Server/Mehrspieler | Autoritativer Weltzustand, Snapshots, Persistenz, Anti-Cheat-Prüfung | Alle Sim-Module (laufen auch im Server), M14 für Story-Phasen |

---

## 4. Module im Detail

Aufwand in **Personentagen (PT)** für eine Entwicklerin/einen Entwickler mit KI-Unterstützung, pro Stufe; grobe Schätzung, ±50 %. Stufen: **MVP** (spielbar, trägt den Meilenstein), **Ausbau** (Qualitätssprung), **Wunschziel** (Politur/Luxus). Godot liefert vieles fertig (Physik, Navigation, Multiplayer-API, Audio-Busse, UI, Profiler); der Aufwand ist deshalb gegenüber der Browser-Version bei einigen Modulen kleiner.

### M1 – Weltgenerierung

**Ziel:** Eine reproduzierbare, vielfältige Stadt statt eines zufälligen Rasters; Straßen und Parzellen entstehen aus Regeln, Bezirke formen das Ergebnis.

| Stufe | Umfang |
|---|---|
| MVP | Zentraler Seed; Raster wird zu **Straßengraph** (Knoten/Kanten) mit Hierarchie (Hauptstraße, Nebenstraße, Gasse); Blöcke werden rekursiv in Parzellen unterteilt; Gebäudehöhe nach Bezirk statt nur nach Abstand zur Mitte (im Prototyp: `lerpf(80, 16, dist)`) |
| Ausbau | **Tensor-Feld-Straßen**: Raster in der Innenstadt, Radialmuster am Hang, Küstenparallelen an der Promenade, zu einem Feld verblendet; Heightmap-Terrain; Straßen passen sich dem Gelände an; Brücken/Tunnel; Wasserflächen |
| Wunschziel | Grammatik-basierte Fassaden mit Bezirksstil; Einwohner-/Landnutzungssimulation; handgesetzte „Hero“-Gebiete (eigene Szenen) werden in die generierte Stadt eingefügt |

**Technische Umsetzung (Godot):**
- Verfahren nach der Literatur: Straßennetz → Blöcke → Parzellen → Gebäude ([Q1] Parish & Müller, L-Systeme; [Q2] Chen et al., Tensor-Felder; Parzellierung durch rekursive Teilung orientierter Rechtecke [Q3]).
- **Hybridansatz:** Hauptachsen, Highway, Brücken und Landmarken **handgesetzt** (als `.tscn`-Szenen im Editor), Füllstraßen und Parzellen **prozedural**. Reine Prozedur erzeugt Beliebigkeit, reine Handarbeit ist nicht zu leisten.
- Ausgabe ist eine reine Datenstruktur `CityData` (`RefCounted`/`Resource`, JSON-serialisierbar) → in einem **Worker-Thread** (`WorkerThreadPool`) erzeugbar, cachebar und per Skript headless testbar.
- Terrain: Heightmap-Gitter je Chunk (`ArrayMesh` bzw. `HeightMapShape3D` für Kollision); Straßen als Mesh-Bänder entlang der Graph-Kanten **oder** – wie im Prototyp – im Boden-Shader (für gerade, ebene Raster am billigsten); gemischt: Shader im Raster-Kern, Mesh-Bänder für Kurven/Hang. Für fertige Terrain-Werkzeuge existieren Community-Add-ons (z. B. Terrain3D); Eignung und Lizenz vor Einsatz prüfen *(nicht recherchiert)*.
- Rechenintensive Generatorteile (Tensor-Feld, Straßenintegration) sind Kandidaten für **C#/GDExtension**, falls GDScript zu langsam ist (messen).

**Ideen aus den Design-Vorbildern (Kapitel 9):** Straßengraph mit **Ebenenattribut** für Hochstraßen/Skybridges (MVP: eine zusätzliche Ebene im Core); Dichte und Vertikalität statt reiner Fläche (9.7).
**Abhängigkeiten:** M3 (Bezirksparameter), M12 (Seed-Tests). **Risiken:** Tensor-Felder erzeugen unschöne Kreuzungswinkel/Sackgassen → nachbearbeiten (Segmente verschmelzen, Mindestwinkel); Generator wird zum Zeitgrab → Zeitbox je Stufe, Handarbeit als Fallback; GDScript-Laufzeit. **Aufwand:** MVP 6 PT · Ausbau 14 PT · Wunsch 15+ PT.

---

### M2 – Streaming und LOD

**Ziel:** Eine Welt, die um ein Vielfaches größer ist als das, was gleichzeitig geladen/gerendert wird, bei stabilen 60 fps auf der RX 5700 XT.

| Stufe | Umfang |
|---|---|
| MVP | Welt in **Chunks** (z. B. 160 × 160 m); nur Chunks im Radius geladen; Gebäude/Bäume/Laternen je Chunk als **`MultiMeshInstance3D`**; geteilte Materialien; **Visibility Ranges** (`visibility_range_begin/end`, Fade) für Detailobjekte; Kollision je Chunk gebündelt; Nebel/Environment kaschiert Weitsicht |
| Ausbau | Chunk-Erzeugung im **Worker-Thread** (`WorkerThreadPool`), Instanziierung auf dem Hauptthread mit Zeitbudget pro Frame; **HLOD** (Visibility-Parent: viele Häuser → ein Fernmesh); automatische **Mesh-LODs** für importierte Modelle; **Occlusion Culling** mit zur Laufzeit erzeugten `OccluderInstance3D`/`BoxOccluder3D` je Gebäude; Chunk-Vorlauf in Fahrtrichtung |
| Wunschziel | Impostor-Billboards für Ferngebäude; Streaming per `ResourceLoader.load_threaded_request` für handgebaute Hero-Szenen; Texture-Streaming je Bezirk; **Floating-Origin** nur falls die Welt die Einzel-Präzisionsgrenze sprengt (siehe unten) |

**Technische Umsetzung (Godot):**
- `MultiMesh` zeichnet tausende Instanzen in einem Draw Call; Nachteil: **einzelne Instanzen werden nicht gecullt** – das gesamte MultiMesh ist sichtbar oder nicht. Deshalb **ein MultiMesh je Chunk (und je Geometrie)**, sodass das Culling auf Chunk-Ebene greift. Per-Instanz-Daten (Farbe, `wear`, Fensterlicht-Variante) über `INSTANCE_CUSTOM`/Instanzfarbe im Shader [Q23].
- **Visibility Ranges (HLOD)** sind der eingebaute LOD-Mechanismus: nahe Detail-Nodes blenden aus, wenn der Fern-Stellvertreter (`visibility_parent`) sichtbar ist. Godot unterstützt für Visibility Ranges nur alphabasiertes Fading; Dithering bleibt Workaround über getrennte Materialien [Q24].
- **Occlusion Culling** (CPU-Software-Rasterizer) braucht eingeschaltetes Projektsetting und `OccluderInstance3D`; für prozedurale Chunks müssen Occluder zur Laufzeit gebaut werden (Box-Occluder je Hochhaus sind billig). Nutzen hängt von der Kameraperspektive ab (hohe Verfolgerkamera = geringerer Gewinn) [Q25].
- **Größe der Welt:** Godot rechnet standardmäßig mit **Einzelpräzision**. Die Dokumentation nennt „Large World Coordinates“ nur für Raumschiff-/Planetensimulationen; moderne Open-World-Spiele kommen mit Einzelpräzision aus, mit einer Orientierungsgröße von etwa 8 × 8 km um den Ursprung [Q26]. **Kestrel Bay (Stadt ≈ 3 × 3 km, Umland ≈ 6 × 6 km)** liegt darunter → **keine Large-World-Coordinates** (sie kosten Performance und Speicher). Falls später doch nötig: Origin-Shifting, das aber Mehrspielerlogik verkompliziert.
- Chunk-Lebenszyklus: `UNLOADED → GENERATING (Thread) → READY (Daten) → INSTANTIATING (Hauptthread, ≤ 2 ms/Frame) → ACTIVE → UNLOADING`; `queue_free` verteilt über Frames (Hitches vermeiden).
- **Kollision:** Gebäude eines Chunks teilen sich eine `StaticBody3D` mit mehreren `CollisionShape3D` (oder einer `ConcavePolygonShape3D` für Terrain); Boden im Raster-Kern bleibt eine Ebene.

**Abhängigkeiten:** M1 (Daten), M12 (Messung). **Risiken:** Pop-in sichtbar → Nebel, Visibility-Fade, Chunk-Vorlauf; Speicherlecks/Hitches beim Entladen; Thread-Sicherheit (Szenenbaum nur auf dem Hauptthread ändern → `call_deferred`); Verhalten von Occlusion Culling und HLOD in 4.7.2 am Prototyp prüfen. **Aufwand:** MVP 4 PT · Ausbau 10 PT · Wunsch 12+ PT.

---

### M3 – Stadtbezirke und Identität

**Ziel:** Sechs Bezirke, die man an Optik, Licht, Verkehr und Klang erkennt (Details in [Kapitel 8](#8-die-welt-kestrel-bay-dystopisch)).

| Stufe | Umfang |
|---|---|
| MVP | `DistrictDef`-Ressourcen: Farbpalette der Fassaden, Gebäudehöhen, Straßenbreiten, Baumdichte, Passanten-/Verkehrsdichte, Himmelstönung, Aegis-Dichte; je Bezirk 1 Landmarke (Szene) |
| Ausbau | Eigene Prop-Sets (Container, Palmen, Reklametafeln, Poller …) als MultiMesh-Gruppen; Boden-/Asphaltvarianten je Bezirk (Shader-Parameter); bezirksspezifische Nachtbeleuchtung (Neon, Natrium, LED); Bezirksname beim Einfahren (UI, `Area3D`-Zonen) |
| Wunschziel | Bezirkstypische NPC-Kleidung/Fahrzeugmix; Zeitrhythmen (Schichtwechsel, Rush Hour); dynamische Details (Baustellen, Märkte, Veranstaltungen) |

**Technische Umsetzung (Godot):** Reine Daten (`data/districts/*.tres`), konsumiert von M1 (Generierung), M2 (Props), M5 (Verkehrsprofil), M9 (Licht), M10 (Musik). Bezirkszugehörigkeit je Position über eine **Bezirkskarte** (niedrig aufgelöste Textur/Voronoi-Gitter, O(1)-Abfrage) statt Geometrietests. Orientierung folgt bewährten Prinzipien: **Landmarken-Hierarchie**, Wiedererkennung über Farbe und Silhouette, Leitlinien [Q7].
**Ideen aus den Design-Vorbildern (Kapitel 9):** Nachtleben-Orte (Clubs, Nachtmärkte), Bezirks-Events (10.4), bezirkstypische Aktivitäten (10.2), Neon-/Reklame-Vokabular je Bezirk (9.7).
**Abhängigkeiten:** M1, M2. **Risiken:** Bezirke wirken bloß „umgefärbt“ → Unterschiede in *Struktur* (Straßenmuster, Höhenprofil, Prop-Rhythmus) statt nur Farbe. **Aufwand:** MVP 3 PT · Ausbau 10 PT · Wunsch 10+ PT.

---

### M4 – Fahrzeuge und Physik

**Ziel:** Fahrverhalten, das sich nach Gewicht, Grip und Tempo anfühlt; Zerstörung, die Konsequenzen hat.

| Stufe | Umfang |
|---|---|
| MVP | Bestehendes `VehicleBody3D` in eine **Fahrzeugszene** mit Datenblatt-Ressource (`VehicleDef`: Masse, Motorkraft, Grip, Lenkung, Federung) auslagern; Drift über Reibungs-/Handbremsen-Tuning; Verfolgerkamera als eigene Szene; Ein-/Aussteigen vorbereiten; Boden-Höhenanpassung durch Terrainkollision |
| Ausbau | **Eigenes Raycast-Fahrzeug** auf `RigidBody3D` (Feder/Dämpfer je Rad, vereinfachtes Reifenmodell – linear/Bürste statt vollem Pacejka [Q12]); 4–6 Fahrzeugklassen (Kompakt, Limousine, Sportler, SUV, Transporter, Motorrad); Schadensmodell (Zonen, Beulen per Vertex-Offset/Shader, Rauch, Reifenschaden); NPC-Autos wechseln bei Kollision von **Kinematik zu Physik** (RigidBody) |
| Wunschziel | Anhänger, Boote, Hubschrauber; Tuning; Gelenkte Hinterachse, Differenzial/Traktionskontrolle; Force-Feedback für Lenkräder |

**Technische Umsetzung (Godot):**
- **`VehicleBody3D`** ist ein Raycast-Fahrzeug, laut Dokumentation aber **nicht** für realistische 3D-Fahrphysik gedacht; für fortgeschrittenes Verhalten empfiehlt die Doku eine eigene Umsetzung auf `RigidBody3D` [Q22]. Der Prototyp zeigt, dass es als Start trägt.
- **Physik-Engine:** Seit Godot **4.6** ist **Jolt** für neue 3D-Projekte der Standard (in 4.4 als Alternative eingeführt, in 4.6 ohne „experimentell“-Label); bestehende Projekte behalten ihre Einstellung, die Simulationsergebnisse unterscheiden sich [Q27]. Das Projekt (`config/features` „4.7“) prüft in `Project Settings → Physics → 3D`, welche Engine aktiv ist; **früh beide testen**, weil Fahrverhalten und Kollisionsstabilität unterschiedlich ausfallen können. Ob `VehicleBody3D` unter Jolt in 4.7.2 gleich robust ist wie unter Godot Physics, ließ sich nicht belegen → am Prototyp messen.
- Physik läuft mit festem Takt (`physics_ticks_per_second`, Standard 60; für schnelle Fahrzeuge 120 erwägen); Kollisionsformen vereinfachen (Box/Konvex), Karosserie getrennt von Kollision.
- Fahrzeuge als **`CollisionLayer`/`Mask`-Schema** (Welt, Fahrzeug, Fußgänger, Trigger) von Beginn an.
- Gefühl vor Realismus: Tuning-Parameter in `VehicleDef`, Live-Tuning über Debug-Panel (Slider via `Control`-Nodes).

**Ideen aus den Design-Vorbildern (Kapitel 9):** Garage und Fahrzeug-Tuning (12.3), Chaos-Spielzeug (zerstörbare Props, Verkehrsreaktionen), Verfolgungsfahrten als Kernspaß (9.7).
**Abhängigkeiten:** M1/M2 (Boden, Kollision), M12. **Risiken:** Tunneling bei hohem Tempo (→ höherer Physiktakt, CCD-Option am RigidBody); Instabilitäten von `VehicleBody3D` beim Überschlag; „realistisch“ ≠ „spaßig“ → Playtests; Jolt/Godot-Physics-Unterschiede. **Aufwand:** MVP 4 PT · Ausbau 12 PT · Wunsch 18+ PT.

---

### M5 – Fußgänger- und Verkehrs-KI

**Ziel:** Eine Stadt, die glaubwürdig wimmelt, ohne die CPU zu fressen – **Simulation-LOD** als Kernidee (Ryzen 5 3600: 6 Kerne/12 Threads, gute Single-Thread-Leistung, aber begrenzt).

| Stufe | Umfang |
|---|---|
| MVP | Agenten auf dem **Straßengraph** (Kanten mit Spuren, Einbahn, Tempolimit) statt Linien-Agenten; Ampel-/Vorfahrtlogik; Abstandhalten per Vorderfahrzeug-Abfrage je Spur (statt Allpaarvergleich); Fußgänger auf Gehweg-Graph, Überqueren an Zebrastreifen; **Spawn/Despawn am Rand des Aktivradius** |
| Ausbau | **Drei Sim-Stufen**: (A) nah: volle Kollision (Physikkörper), Animation, Wahrnehmung, ggf. `NavigationAgent3D`-Avoidance; (B) mittel: nur Spur-Bewegung ohne Kollision, niedrige Updaterate; (C) fern: statistische Dichte je Kante, keine Einzelagenten. Reaktionen: Ausweichen, Hupen, Flucht bei Unfall/Schüssen/Raserei; Tageszeit-/Wetterprofile je Bezirk |
| Wunschziel | Agenten mit Zielen und Tagesplan; Busse/Bahn mit Haltestellen; Gruppen, Warteschlangen; Skelett-Animation per Vertex-Textur/`AnimationTree`-LOD; Crowd-Rendering per `MultiMesh` |

**Technische Umsetzung (Godot):**
- Das Prinzip der **Simulationsblase** (Verkehr und Passanten laufen in einer Blase um den Spieler, am Rand wird gespawnt/entfernt) und das Einhalten von Verkehrsregeln trotz unvorhersehbarer Kollisionen ist aus Open-City-Spielen belegt [Q4]; Detailtiefe gestuft nach Entfernung ist etablierte Praxis [Q14].
- **Straßenwege:** eigener Graph (`AStar3D` ist unabhängig vom NavigationServer und eignet sich für Straßengraphen, GPS und Polizei-Routing). **`NavigationServer3D`/NavMesh** für freie Fußgängerbewegung auf Plätzen; Avoidance und Pfadabfragen sind teuer → nur für nahe Agenten (Stufe A), Pfade nicht pro Frame neu berechnen, unerreichbare Ziele vermeiden (bekannter Performance-Einbruch) [Q28].
- **Daten statt Nodes:** Agenten als Struktur-Arrays (`PackedFloat32Array`/`PackedVector3Array`) in einem `TrafficSystem`-Autoload; Darstellung über **ein `MultiMeshInstance3D` je Fahrzeugtyp** (Transform pro Frame setzen), echte `VehicleBody3D`/`RigidBody3D`-Körper nur für Stufe A und bei Kollisionen. Hunderte Fahrzeuge sind so realistisch, 28 einzelne Node-Autos mit eigener Logik skalieren nicht.
- Rechenschleifen für Hunderte Agenten zuerst typisiertes GDScript, bei Engpass **C#** oder GDExtension; Updates über `WorkerThreadPool` auf mehrere Kerne verteilen (Ergebnisse auf dem Hauptthread übernehmen).
- Systemische Ereignisse (Unfall, Panne, Straßenfest), die **ohne** Spielerbeteiligung stattfinden, erzeugen Lebendigkeit [Q5][Q6].

**Ideen aus den Design-Vorbildern (Kapitel 9):** **Zielprofile mit Tagesplan** für Verträge (9.5), lebendige Nachtmärkte und Bezirksereignisse (9.7, 10.4).
**Abhängigkeiten:** M1 (Straßengraph), M9 (Zeit/Wetter), M6 (Polizei teilt den Graphen). **Risiken:** Staus/Deadlocks (→ Timeout und Ghosting wie im Prototyp, aber kontrolliert); Mengenzahl vs. CPU (Ryzen 5 3600); unnatürliches Pop-in (→ Spawn nur außerhalb der Sicht). **Aufwand:** MVP 7 PT · Ausbau 14 PT · Wunsch 18+ PT.

---

### M6 – Polizei und Gesetz-System

**Ziel:** Verfolgungen, die fair, spannend und lesbar sind; Folgen statt bloßer Strafpunkte.

| Stufe | Umfang |
|---|---|
| MVP | Verbrechens-**Signale** mit Gewichtung (Raserei, Unfall mit Zivilfahrzeug, Fußgänger verletzt, Polizeikontakt); Fahndungsstufe 1–5 mit Abklingen; Polizei fährt über Straßengraph (Ziel: Spieler/letzte bekannte Position); Festnahme/Flucht, aber mit **Sichtlinie** (Raycast) |
| Ausbau | Zustände **Verdacht → Verfolgung → Suche → Verloren** mit Suchradius um die zuletzt gesehene Position; Zeugenmeldung; **Bezirks-Ruf**: Fahndungsdruck primär im Bezirk; Taktiken je Stufe (PIT-Manöver, Straßensperre, Nagelband, Hubschrauber); Unterschlupf (Garage, Tunnel, Wasser) zum Abschütteln; **Aegis-Kennzeichenerfassung** (Kameras) |
| Wunschziel | Polizei als Fraktion mit Revieren, Schichtwechsel, Korruption/Bestechung; Gerichts-/Kautionssystem; nicht-tödliche Optionen |

**Technische Umsetzung (Godot):** Zustandsautomat je Einheit (`Node`-basiert oder Enum-Klasse) + zentraler Dispatcher-Autoload (verteilt Einheiten, hält Ziel-Wissen). Wahrnehmung über Sichtkegel mit `PhysicsRayQueryParameters3D` gegen Gebäude-Kollisionen (Strahlen budgetiert: wenige pro Frame, reihum), dazu Hörradius via `Area3D`. Das Fahndungssystem als „Spielzeug“ mit klaren, ablesbaren Regeln zu gestalten ist wichtiger als Realismus [Q5].
**Ideen aus den Design-Vorbildern (Kapitel 9):** Eskalationsleiter mit Medien-Hubschrauber und Radio-Meldungen (9.6/9.7), Kamera-Slalom durch Aegis-Blindzonen (10.2).
**Abhängigkeiten:** M5 (Graph, Zeugen), M4 (Fahrzeuge), M3 (Bezirke), M1. **Risiken:** Gummiband-KI, die „betrügt“ (Teleport-Spawns) → Spawn nur außerhalb der Sicht; Frust durch unausweichliche Festnahme → immer ein erkennbarer Fluchtweg; Raycast-Kosten. **Aufwand:** MVP 4 PT · Ausbau 12 PT · Wunsch 15+ PT.

---

### M7 – Missionen und Story-Runner

**Ziel:** Aufträge, die die Welt zeigen und systemische Zufälle zulassen. Die Haupthandlung selbst steht in [Kapitel 6 (M14)](#6-story-engine-entscheidungen-und-fraktionen-m14); M7 liefert den Runner, den M14 mit Inhalten füttert.

| Stufe | Umfang |
|---|---|
| MVP | Missions-**Framework** als Daten (Schritte: *Gehe zu / Fahre zu / Hole ab / Liefere / Entkomme / Zerstöre / Warte*); Typen: Kurier mit Zeitlimit, Taxi-Passagier, Verfolgung, Flucht; Marker (der Prototyp hat Marker aus Zylinder-Mesh) und Wegpunkt-UI; Checkpoints, Erfolg/Fehlschlag |
| Ausbau | Kampagne an Bezirke gekoppelt; Nebenaufträge je Bezirk; Dialoge/Funk als UI-Boxen mit Sprecherportrait; mehrere Lösungswege; Speicherpunkte |
| Wunschziel | Verzweigende Entscheidungen, Beziehungen, dynamische Zufallsbegegnungen, Mission-Editor als Godot-Editor-Plugin (`EditorPlugin`) |

**Technische Umsetzung (Godot):** Zustandsautomat/Skript-DSL in JSON (`data/missions/*.json`), ausgeführt vom `MissionRunner` (Autoload). Missionen **abonnieren Signale** des `EventBus` (z. B. `vehicle_entered_zone`) statt die Hauptschleife zu kennen; Zielzonen sind `Area3D`. Gestaltungsregel: je Mission Zielbedingung, KI-Rollen, Checkpoint-Abstände und Fehlschlagtext explizit festlegen; einfache KI mit starker Reaktion wirkt besser als komplexe KI [Q15]; Missionen „öffnen“, damit systemisches Chaos mitspielen darf [Q5][Q6].
**Ideen aus den Design-Vorbildern (Kapitel 9):** Vier Auftragsklassen (Hauptquest, Nebenquest, Gig, Vertrag; 9.1), zehn Missionstypen mit Parametern (Kapitel 10), Gig-Generator (9.4), Dialog-/Quest-/Flag-Graph (6.9).
**Abhängigkeiten:** M1 (Orte), M5/M6 (Welt reagiert), M8 (Belohnung), M11 (Marker). **Risiken:** Content-Menge → Wiederverwendung der Typen; Skript-Brüche bei systemischen Zufällen → Fail-Safes (Neuaufsetzen, Checkpoint). **Aufwand:** MVP 5 PT · Ausbau 18 PT · Wunsch 22+ PT.

---

### M8 – Wirtschaft und Fortschritt

**Ziel:** Ein Fortschrittsgefühl, das das Fahren belohnt und die Welt schrittweise öffnet.

| Stufe | Umfang |
|---|---|
| MVP | Geld mit klaren Einnahmen/Ausgaben; Speichern/Laden; Fahrzeugreparatur/-ersatz; Strafen bei Festnahme |
| Ausbau | Garagen/Safehouses je Bezirk; Fahrzeugkauf und Tuning; **Fraktions-/Bezirks-Ruf**; Immobilien als Einnahmequelle |
| Wunschziel | Dynamische Preise (Tageszeit, Nachfrage), Handel/Schmuggelrouten mit Risiko, Erfolge |

**Technische Umsetzung (Godot):** Ein zentraler, serialisierbarer `GameState`-Autoload (reine Daten als `Dictionary`/Klassen mit `to_dict()`/`from_dict()`); **Speichern als JSON** (`user://saves/…`, mit Schema-Version, Atomic-Write über temporäre Datei); **keine** `.tres/.res`-Speicherstände (Skript-Einbettung, Sicherheit). Preise und Belohnungen als Daten.
**Ideen aus den Design-Vorbildern (Kapitel 9):** Herkunft, Skills und Module (9.3), Inventar/Garage/Tuning (12.3), Wirtschaft (12.4), **SaveManager** (Kapitel 11).
**Abhängigkeiten:** M7, M6. **Risiken:** Inflation/Balancing → Kennzahlen in Tabelle; Speicherstand-Migration (Schema-Version von Anfang an). **Aufwand:** MVP 3 PT · Ausbau 7 PT · Wunsch 10+ PT.

---

### M9 – Tag/Nacht/Wetter

**Ziel:** Licht und Wetter als wichtigstes Stimmungswerkzeug – jeder Bezirk sieht zu jeder Tageszeit anders aus.

| Stufe | Umfang |
|---|---|
| MVP | Tageszyklus aus dem Prototyp (`_process` mit `ProceduralSkyMaterial`, Sonnenwinkel, Dämmerung) in Autoload `Clock`/`WeatherSystem` auslagern; globale Shader-Parameter (`night`); Straßenlaternen als Emissive-Props + wenige echte Lichter; Spielzeit-API |
| Ausbau | **Wetter-Zustandsautomat** (klar, bewölkt, Regen, Nebel, Gewitter) mit weichen Übergängen; nasse Straßen (`wetness` global), Regen via `GPUParticles3D`, Nebel via `FogVolume`/Environment; physikalischer Himmel (`PhysicalSkyMaterial` oder eigener Sky-Shader mit Wolken/Sternen); Bezirks-Tönung; Wetter wirkt auf Grip (M4), Verkehr (M5), Audio (M10) |
| Wunschziel | Volumetrische Wolken, Jahreszeiten, Windsimulation für Vegetation, Blitz-Beleuchtung |

**Technische Umsetzung (Godot):** `WorldEnvironment` + `Environment`-Ressource (alle Parameter animiert per Code/`Tween`); **Cascaded Shadow Maps** der `DirectionalLight3D` (`directional_shadow_mode` PSSM 4 Splits; Schattenweite pro Qualitätsstufe) statt einer festen Box; Nachtbeleuchtung: Forward+ nutzt einen **Cluster-Renderer** für viele Lichter, die Kosten steigen aber mit Schattenwerfern → nur wenige schattenwerfende Lichter (Kapitel 5.1).
**Ideen aus den Design-Vorbildern (Kapitel 9):** Neon-Nacht und nasse Straßen als Stimmungskern, Stromausfall/Blackout als Bezirks-Event (10.4).
**Abhängigkeiten:** M2, M12. **Risiken:** Nachtbeleuchtung (viele Lichter) → Fake-Licht (Emissive, Glow, Decals), Schattenwerfer strikt begrenzen; Regen-Shader. **Aufwand:** MVP 2 PT · Ausbau 8 PT · Wunsch 12+ PT.

---

### M10 – Audio

**Ziel:** Eine Welt, die man hört: Motor, Stadt, Wetter, Musik.

| Stufe | Umfang |
|---|---|
| MVP | `AudioManager`-Autoload mit **Audio-Bussen** (Motor, Umwelt, Musik, UI, Master) im `AudioBusLayout`; Motor aus **geloopten Samples** (Leerlauf, Last, Schub) mit Pitch nach Drehzahl; Kollision, Hupe, Sirene; Doppler/Dämpfung für NPC-Fahrzeuge (`AudioStreamPlayer3D`) |
| Ausbau | Mehrschicht-Motor mit **Equal-Power-Crossfade** (kein Lautstärkeloch) [Q16]; Reifenquietschen, Wind nach Tempo, Regen; Umgebungs-Layer je Bezirk; Hall in Tunneln/Häuserschluchten (Reverb-Bus-Effekt, per Bezirk/`Area3D` umgeschaltet); Radiosender mit prozeduraler Musik |
| Wunschziel | Dynamische Musik (Intensität folgt Verfolgung/Tageszeit), Sprachausgabe, Mix-Snapshots (z. B. „in der Garage“) |

**Technische Umsetzung (Godot):** `AudioStreamPlayer3D` mit Dämpfung/Doppler; Pitch über `pitch_scale`; Equal-Power-Crossfade über zwei Player und `Tween` (Gains in dB ableiten); Effekte (Reverb, Lowpass, Kompressor) auf **Bussen**. Anzahl gleichzeitiger 3D-Player begrenzen (Pool). Die Racer-Fallstudie von Google zeigt, wie Motorklang mit wenigen Dateien überzeugend wird [Q16].
**Ideen aus den Design-Vorbildern (Kapitel 9):** Reaktives Radio mit fünf fiktiven Sendern und Satire-Werbung (9.6, 12.8).
**Abhängigkeiten:** M4, M5, M9. **Risiken:** Asset-Beschaffung/Lizenzen (→ CC0 oder selbst synthetisiert, Lizenzliste); Dateigröße; Loop-Klicks bei Samples (Loop-Punkte sauber setzen). **Aufwand:** MVP 2 PT · Ausbau 7 PT · Wunsch 10+ PT.

---

### M11 – UI und Karte

**Ziel:** Lesbare, schöne Bedienung; Karte und Navigation, die die Stadt erschließen.

| Stufe | Umfang |
|---|---|
| MVP | HUD als Szene (`CanvasLayer` + `Control`-Nodes mit Theme); Minimap (aus `CityData` gezeichnet oder `SubViewport` mit Top-Down-Kamera); Zielpfeil und Entfernung; Pause-/Optionsmenü (Grafik-Preset, Lautstärke, Tastenbelegung via `InputMap`) |
| Ausbau | **Vollkarte** (Zoom/Pan, Bezirksnamen, Landmarken, Wegpunkte); **GPS-Route** per A* auf Straßengraph, im Spiel als Linienmesh/Pfeile; Bezirksname-Einblendung; Gamepad (Godot-Eingabe nativ); Missions-Tagebuch |
| Wunschziel | Fotomodus, Telefon-UI, anpassbares HUD, Barrierefreiheit (Farbenblind-Modi, Untertitelgröße) |

**Technische Umsetzung (Godot):** `Control`-Nodes, `Theme`-Ressource, `CanvasLayer`; Karte als **einmal gebackene Textur** aus `CityData` (SubViewport-Render oder `Image`), darüber nur Marker; Pfadsuche A* auf dem Straßengraph (derselbe wie KI). UI liest nur Zustand (Signale), schreibt nie in die Simulation.
**Ideen aus den Design-Vorbildern (Kapitel 9):** Journal, Charaktermenü, Optionen und Barrierefreiheit (12.5/12.6), Fotomodus (12.9).
**Abhängigkeiten:** M1, M7, M8. **Risiken:** Textupdates pro Frame (→ nur bei Änderung), Eingabegeräte-Matrix, Auflösungs-/Skalierungsverhalten (Stretch-Modus `canvas_items`). **Aufwand:** MVP 3 PT · Ausbau 8 PT · Wunsch 8+ PT.

---

### M12 – Performance und Tooling

**Ziel:** Messen statt vermuten; Qualität bleibt reproduzierbar und regressionsfrei.

| Stufe | Umfang |
|---|---|
| MVP | **Debug-Overlay** (F3): fps, Frame-ms, `Performance`-Monitore (Draw Calls, Objekte, Primitive, Video-Speicher, Physik-Objekte), Agentenzahlen, Chunk-Zahl; zentraler Seed (URL-Ersatz: Kommandozeilenargument `-- --seed=…`); Budgets (siehe Tabelle); Qualitäts-Presets (Niedrig/Mittel/Hoch) |
| Ausbau | Automatischer **Benchmark-Flug** (fester Seed, fester Kameraweg, Ergebnis als JSON); Seed-Tests für Generator (z. B. „alle Blöcke erreichbar“) als Headless-Skript; Frame-Zeit je System; Speicher-Watch |
| Wunschziel | CI-Lauf (Headless-Tests, Import/Export), Performance-Regressionsgrenzen; In-Game-Editor für Bezirksparameter |

**Performance-Budgets (Zielwerte „Mittel“, 1080p nativ bzw. 1440p mit FSR, RX 5700 XT + Ryzen 5 3600; Planwerte):**

| Größe | Budget |
|---|---|
| Frame-Zeit gesamt | ≤ 16,6 ms (60 fps), Niedrig: ≤ 33 ms |
| GPU-Zeit je Frame | ≤ 14 ms (Reserve für Spitzen) |
| Main-Thread CPU (Skripte + Physik) | ≤ 6 ms |
| Draw Calls | ≤ 2500 (CPU-seitig; per Profiler am Ryzen 5 3600 zu bestätigen) |
| Dreiecke sichtbar | ≤ 5 Mio. |
| Aktive Fahrzeuge / Fußgänger | ≤ 150 / ≤ 300 simuliert (Stufe A+B); davon ≤ 30 Fahrzeuge mit echtem Physikkörper |
| Echte Lichter | ≤ 128 sichtbar im Cluster (Mittel), davon **≤ 6 mit Schatten** |
| Videospeicher | ≤ 6 GB von 8 GB (Reserve für Treiber/Fenster); Texturen ≤ 4 GB |
| Ladezeit bis Spielstart | ≤ 15 s (SSD) |

**Technische Umsetzung (Godot):** `Performance.get_monitor(...)` für Overlay, eingebauter Profiler/Monitore und **Visual Profiler** (GPU-Zeit je Render-Pass) im Editor; eigener Benchmark-Modus als Szene + Kommandozeilenflag; Tests mit **GUT/gdUnit4**-Add-ons oder eigenen `--headless --script`-Läufen *(Auswahl zu prüfen)*; Objekt-Pools (`Node` wiederverwenden statt `instantiate`/`queue_free`); typisierte Arrays/`Packed*Array` in heißen Pfaden.
**Abhängigkeiten:** alle. **Risiken:** Messung wird vergessen → Overlay standardmäßig per F3, Benchmark im Plan verankert. **Aufwand:** MVP 1 PT · Ausbau 4 PT · Wunsch 6+ PT.

---

### Gesamtschätzung (M1–M15)

| Modul | MVP | Ausbau | Wunsch |
|---|---|---|---|
| M1 Weltgenerierung | 6 | 14 | 15+ |
| M2 Streaming/LOD | 4 | 10 | 12+ |
| M3 Bezirke | 3 | 10 | 10+ |
| M4 Fahrzeuge/Physik | 4 | 12 | 18+ |
| M5 Verkehrs-/Fußgänger-KI | 7 | 14 | 18+ |
| M6 Polizei/Gesetz | 4 | 12 | 15+ |
| M7 Missionen (Runner) | 5 | 18 | 22+ |
| M8 Wirtschaft | 3 | 7 | 10+ |
| M9 Tag/Nacht/Wetter | 2 | 8 | 12+ |
| M10 Audio | 2 | 7 | 10+ |
| M11 UI/Karte | 3 | 8 | 8+ |
| M12 Performance/Tooling | 1 | 4 | 6+ |
| M13 Realismus/Detailtiefe | 6 | 18 | 22+ |
| M14 Story-Engine | 8 | 20 | 25+ |
| M15 Server/Mehrspieler | 10 | 22 | 35+ |
| **Summe (PT)** | **68** | **184** | **238+** |

Alle MVPs zusammen: grob **13–15 Arbeitswochen** (eine Person mit KI-Unterstützung, ca. 5 PT/Woche); davon ca. 58 PT für den **Einzelspieler-Kern** (M1–M14), 10 PT für den Server. Alle Ausbaustufen kosten weitere ca. 7–9 Monate. Gegenüber der Browser-Version (80/212/261+) sinkt der Aufwand vor allem bei Streaming, Physik, Navigation, Audio, UI, Profiling und Netzwerk, weil Godot diese Bausteine mitbringt. **Nicht enthalten:** Einarbeitung in Godot/GDScript, Content (Missionen, Assets, Audio, Vertonung) nur teilweise. Die Schätzungen sind Planwerte mit ±50 % Unsicherheit, keine Zusagen. **Fortschreibung (Prototypfortschritt, Design-Vorbilder, Zusatzsysteme): Kapitel 13.**

---

## 5. Realismus und Detailtiefe (M13)

**Ziel:** Eine Stadt, die glaubwürdig *benutzt* aussieht – Asphalt mit Flicken, Gullideckel mit Dampf, abgenutzte Fassaden, nasse Straßen, die Neon spiegeln. Maßstab ist der **bestmögliche Look auf der Zielhardware** (RX 5700 XT, ohne Hardware-Raytracing), nicht Konsolen-Fotorealismus.

### 5.1 Die Zielhardware und ihre Grenzen

**AMD RX 5700 XT** (RDNA 1, 8 GB GDDR6, Vulkan-fähig, **kein Hardware-Raytracing**, keine Mesh Shader) + **Ryzen 5 3600** (6 Kerne/12 Threads). Godot-Forward+ läuft laut Nutzer nachweislich per Vulkan darauf. Konsequenz: Alles, was Raytracing braucht, entfällt; Godots Screen-Space- und Voxel/SDF-Verfahren sind die Mittel der Wahl. **Hinweis:** Der Prototyp hat SDFGI und Volumennebel bereits aktiviert; die Budgettabelle ist deshalb gegen eine Messung zu prüfen – fällt „Mittel“ unter 60 fps, werden SDFGI und Volumennebel zuerst abgeschaltet.

**Frame-Budget bei 60 fps (16,6 ms), 1080p – Planwerte (Schätzung, per Visual Profiler zu bestätigen):**

| Posten | Mittel | Hoch | Bemerkung |
|---|---|---|---|
| Opake Geometrie + Depth-Prepass | 3,5 ms | 4,5 ms | MultiMesh, Visibility Ranges, Occlusion Culling halten es klein |
| Direktionalschatten (4 Kaskaden) | 2,0 ms | 3,0 ms | Schattenweite 150/250 m; Atlas 4096 (Prototyp) |
| Weitere Schatten (Spot/Omni) | 0,5 ms (≤ 4) | 1,0 ms (≤ 6) | Jeder Schattenwerfer kostet extra Durchgänge |
| **SDFGI** | **aus** | 3,0 ms (Cascades 3–4, halbe Auflösung) | Teuerstes Einzelfeature; siehe unten |
| SSAO | 1,0 ms | 1,5 ms | Halbe Auflösung in „Mittel“ |
| SSR | 0,8 ms | 1,2 ms | Nur für nasse Flächen sinnvoll (nur bei Regen aktiv) |
| Volumetrischer Nebel | aus | 1,0 ms | Kleines Volumen, grobe Froxel |
| Transparenz/Partikel/Glow/Tonemapping | 1,2 ms | 1,5 ms | Regen, Rauch, Neon-Glow |
| Skripte/Physik (Main-Thread CPU, parallel zur GPU) | ≤ 6 ms | ≤ 6 ms | Muss unter dem GPU-Budget bleiben, sonst CPU-limitiert |
| **Summe GPU (Richtwert)** | **≈ 9–11 ms** | **≈ 14–16 ms** | Reserve für Spitzen |

**Was sich lohnt (Reihenfolge nach Nutzen pro Kosten):**
1. **Tonemapping/Farbpipeline, Himmel, Nebel, Glow** – fast kostenlos, größter Stimmungsgewinn.
2. **MultiMesh + Visibility Ranges + Occlusion Culling** – senkt die Kosten *aller* Passes, ist Voraussetzung für alles andere.
3. **Materialqualität** (PBR, Detail-Normal, ORM, Decals) – kostet Speicher statt Rechenzeit.
4. **SSAO** (Boden-/Fassaden-Kontakt) und **SSR nur bei Nässe** – Realismus für Neon-Nächte im Regen.
5. **Reflection Probes** (je Bezirk/Straßenzug, statisch gebacken) für Metall-/Glasreflexe.
6. **SDFGI** nur im Preset „Hoch“ und nach Messung: Es liefert Raumlicht/Bounce, ist aber das anspruchsvollste GI-Verfahren; die Doku empfiehlt weniger Cascades bei schneller Kamera (≤ 4), Halbauflösungs-GI, höheren „Frames to converge“ bei statischem Licht und deaktivierte „Use Occlusion“-Option, wenn nicht nötig; SDFGI gilt nur für Forward+ [Q29]. **Zu klären am Prototyp:** Wie verhält sich SDFGI bei zur Laufzeit gestreamten Chunk-Meshes (Neuberechnung, Flackern)? Fallback: kein GI, dafür Himmels-Ambient + Reflection Probes + SSAO.
7. **Volumetrischer Nebel** nur für Hero-Momente (Nachtregen, Industrienebel) – kleine Volumen.

**Was sich nicht lohnt / zu vermeiden ist:** Echtzeit-Raytracing (nicht verfügbar), SDFGI in „Mittel“, mehr als ~6 schattenwerfende Lichter, SSR dauerhaft auf der ganzen Stadt, hochauflösende Schatten für alle Props (nur große Objekte werfen Schatten), tausende individuelle `MeshInstance3D` statt MultiMesh, Transparenz-Overdraw (Glas, Partikel) ohne Begrenzung, 4K-Texturen für Kleinteile.

**Auflösung und Skalierung:** Zielvorgabe **1080p nativ** (MSAA 2× wie im Prototyp, oder TAA) bzw. **1440p über Upscaling**: `Viewport.scaling_3d_mode = FSR 2.2` mit `scaling_3d_scale` ≈ 0,67 („Quality“) bis 0,59. FSR 2.2 gibt es seit Godot 4.2, ist die qualitativ beste, aber langsamste Skalierungsoption; bei FSR 2 wird TAA ignoriert; der Skalierungsfaktor wirkt **pro Achse** (0,5 = ein Viertel der Pixel) [Q30]. Dazu eine **dynamische Skalierung**, die `scaling_3d_scale` bei Frame-Einbruch absenkt (Qualität adaptiv).

**Compositor/Post-Processing:** Godot 4.3+ bietet `Compositor`/`CompositorEffect` für eigene Compute-Post-Effekte (z. B. Schärfung, Film-Grain, Aegis-Scan-Overlay). Das ist ein **Wunschziel** und in dieser Recherche nicht verifiziert; für den MVP genügen `Environment`-Effekte (Tonemapping, Glow, Adjustment/Color-Correction).

**Threads/CPU:** Der Ryzen 5 3600 hat 6 Kerne. Godot trennt Render- und Hauptthread (Threading-Modell in den Projekteinstellungen prüfen); Generierung/KI-Rechnung wandert in den **`WorkerThreadPool`**. Der Main-Thread (Skripte + Physik) ist der typische Engpass – deshalb GDScript typisieren, Batch-Verarbeitung, C#/GDExtension nur für gemessene Hotspots.

### 5.2 Materialien (PBR)

- **Standard:** `StandardMaterial3D` (PBR, Metallic/Roughness) für die Masse, eigene **Spatial-Shader** für Boden, Fassade, Nässe; Godots `BaseMaterial3D` kann Albedo, Normal, ORM (Occlusion/Roughness/Metallic gepackt), Clearcoat und Detail-Texturen.
- **Material-Bibliothek** in `data/materials/` (`.tres`): Asphalt (neu/alt/nass), Beton, Backstein, Blech/Rost, Glas/Stahl-Fassade, Putz, Holz, Kopfsteinpflaster, Gummi, Lack. Jedes Material: Albedo, Normal, ORM, UV-Skalierung in Metern, Wear-Parameter. **Texel-Dichte** nah ca. 256–512 px/m für Boden, 128–256 px/m für Fassaden (Richtwerte).
- **Texturformate:** Godot importiert Texturen als **VRAM-komprimiert (BPTC/S3TC auf dem Desktop)** mit Mipmaps (Import-Voreinstellung „VRAM Compressed“); Normal-Maps mit passendem Import-Typ; so sinkt der VRAM-Bedarf deutlich. Texturspeicher-Ziel ≤ 4 GB.
- **Mixed Prozedural/Textur:** Wie im Prototyp zeichnet ein Shader Straßenmarkierungen und Fenster prozedural – ein Vorteil: keine Texturen, unendliche Auflösung. Für Realismus ergänzen: **Detail-Normal-Map** (Asphaltkorn), Triplanar-Mapping für Terrain/Fels, Werte-Rauschen für Verschmutzung.
- **Quellen für Texturen:** CC0-Bibliotheken wie [ambientCG](https://ambientcg.com) und [Poly Haven](https://polyhaven.com); Lizenzliste in `assets/LICENSES.md`.

### 5.3 Straßendetails und Props

Dichte-Budgets je Bezirk legen fest, *wie viel* Detail ein Chunk bekommt (Chunk-Generator streut Props regelbasiert, seed-stabil).

| Detailklasse | Beispiele | Umsetzung in Godot |
|---|---|---|
| Boden-Decals | Gullideckel, Risse, Flickstellen, Ölflecken, Reifenspuren, abgenutzte Markierungen, Laub | Godots **`Decal`-Node** (projiziert Albedo/Normal/ORM, geclusterte Decals begrenzt) für wenige, wichtige Stellen; **MultiMesh-Quads** mit Decal-Atlas und `depth_bias`/Offset für die Masse; Markierungen weiter im Boden-Shader |
| Straßenmöblierung | Hydranten, Poller, Mülltonnen, Ampeln, Schilder, Laternen, Kabelmasten, Parkuhren | MultiMesh je Chunk und Typ; Varianten je Bezirk (Prop-Sets) |
| Fassadendetails | Klimageräte, Feuerleitern, Wasserspeicher, Regenrohre, Sat-Schüsseln, Reklame, Graffiti | Fassaden-Shader (Gittermuster, wie bei den Fenstern im Prototyp) + Dach-/Anbau-MultiMesh |
| Dystopie-Props | Aegis-Kameras (drehen sich, rote LED), Drohnenstationen, Checkpoint-Schranken, „Social-Zone“-Schilder, Konzern-Werbewände, Zäune um Gated Areas | Gleiche MultiMesh-Pipeline; Kameras sind zugleich **Spielobjekte** (M6/M14, eigene Szene mit `Area3D`-Sichtkegel) |
| Lebendigkeit | Dampf aus Gullis, Müll im Wind, flackernde Neonröhren, Tauben, Pfützen-Ringe | Wenige `GPUParticles3D` nahe der Kamera (Visibility Range), Rest per Shader-Animation |
| Beschilderung/Text | Straßennamen, Ladenschilder, Werbetexte (fiktiv, eigene Marken) | `Label3D`/Textur-Atlas; keine Realmarken |

### 5.4 Verschleiß und Wetter

- **Wear-Attribut:** Jede Gebäude-/Straßeninstanz bekommt einen Verschleißwert `wear` (0–1) aus Bezirk (Rustline hoch, Core niedrig), Alter und Zufall (per MultiMesh-`INSTANCE_CUSTOM`). Der Shader mischt damit Schmutz am Sockel, Regenstreifen unter Fensterbänken, Rost an Metall, abplatzenden Putz.
- **Fahrzeuge:** Dreck-Level wächst mit Fahrstrecke/Regen (Shader-Parameter am Material), Waschanlage setzt zurück; Kratzer/Beulen aus dem Schadensmodell (M4).
- **Nasse Oberflächen (Shader):** `wetness` als **Global Shader Uniform**: Roughness sinkt, Albedo dunkler, ein World-Space-Rauschen erzeugt Pfützen-Masken (dort Roughness ≈ 0, glatte Normalen), Regenringe als animierte Normal-Map; Reflexion über **SSR (nur bei Regen aktivieren)** und Reflection Probes. *(Eigenes Design; zu Nässe-Shadern lieferte die Recherche keine belastbare Quelle – früh als Prototyp prüfen.)*
- **Wettereinfluss auf Welt:** Beleuchtung, Sicht (Nebel), Verkehr/Passanten (M5), Grip (M4), Klang (M10).
- **Tageszeit:** Straßenbeleuchtung natriumorange in Rustline, kaltweiß im Core, warm in Lantern Hill – Beleuchtung gehört zur Bezirksidentität (M3).

### 5.5 Glaubwürdige Stadtstruktur

Realismus ist auch *Plan*: Die Stadt muss aussehen, als wäre sie über Jahrzehnte gewachsen.
- **Straßenhierarchie und Maße:** Hauptstraßen 3–4 Spuren, Nebenstraßen 2, Gassen 1; Gehwegbreite, Bordsteinhöhe (≈ 15 cm), Kreuzungsradien und Parkbuchten sind Parameter je Bezirk.
- **Blockgrößen nach Bezirk:** Core 100–140 m, Altstadt 30–60 m, Vorstadt gewunden mit Sackgassen, Industrie große Blöcke (nach Verfahren in [Q1][Q2][Q3]).
- **Landnutzungslogik:** Gewerbe und Läden an Hauptstraßen, Wohnen in Nebenstraßen, Tankstellen/Supermärkte an Einfallstraßen, Lager/Industrie in Hafennähe; Gebäudefront und Eingänge zur Straße gerichtet, Hinterhöfe und Zufahrten.
- **Gelände:** Straßen folgen Höhenlinien (Terrassierung, Serpentinen, Stützmauern, Treppen), Brücken/Tunnel an echten Hindernissen.
- **Infrastruktur sichtbar:** Strommasten und Leitungen, Brunnen, U-Bahn-Zugänge, Bushaltestellen, Baustellen.
- **Soziale Gegensätze lesbar machen:** Gehwegzustand, Beleuchtung, Sauberkeit und Kameradichte unterscheiden reiche und arme Viertel (Kapitel 8).

### 5.6 Stufen, Technik, Risiken

| Stufe | Umfang |
|---|---|
| MVP | Material-Bibliothek (12 Materialien, PBR, VRAM-Kompression), einheitliche Farbpipeline (ACES oder Filmic, Glow), Himmelsanimation, Direktionalschatten mit 4 Kaskaden, Reflection Probes je Bezirk, Decal-Atlas (20 Decals), 30 Props, `wear`-Attribut, Qualitäts-Presets |
| Ausbau | Nässe-Shader mit Pfützen, Gebäude-Verschleiß, Bezirks-Prop-Sets, Text-Atlas/`Label3D`, Aegis-Kameras als Props, SSAO-Feinschliff, dynamische Auflösung mit FSR 2.2, Fahrzeuglack mit Clearcoat, SDFGI-Test für „Hoch“ |
| Wunschziel | `CompositorEffect`-Pipeline (Schärfung, Grain, Scan-Overlay), volumetrischer Nebel an Hero-Orten, Fensterinterieurs per Parallax-Cubemap-Shader, Photogrammetrie-Hero-Assets, Jahreszeiten |

**Technische Umsetzung:** Asset-Pipeline in Godot (Import-Presets; Texturen/Modelle `.glb` über den Importer, automatische Mesh-LODs); Chunk-Generator (M2) erzeugt Props/Decals regelbasiert; alle Qualitätsstufen über **eine** Preset-Ressource (M12), die Environment-, Viewport- und Projekteinstellungen setzt.
**Ideen aus den Design-Vorbildern (Kapitel 9):** Neon-/Reklame-Look der Nacht, Dystopie-Props und satirische Plakate (9.7, 9.8).
**Abhängigkeiten:** M2, M3, M9, M12. **Risiken:** (1) Asset-Menge und Lizenzen → CC0 + Prozeduralmaterialien, Lizenzliste; (2) Videospeicher (8 GB) → Textur-Budget, Kompression, Streaming nach Bezirk; (3) „Look-Drift“ zwischen Bezirken → eine Material- und Farbpipeline, zentrale Look-Dev-Szene; (4) Performance-Einbruch → Presets, dynamische Auflösung; (5) SDFGI/Streaming-Wechselwirkung (siehe oben). **Aufwand:** MVP 6 PT · Ausbau 18 PT · Wunsch 22+ PT (ohne Kunstproduktion).

---

## 6. Story-Engine, Entscheidungen und Fraktionen (M14)

**Ziel:** Eine ausgereifte Haupthandlung, deren Wendungen vom Verhalten des Spielers abhängen – über Entscheidungen, Ruf, Fraktionen und Beziehungen – mit mehreren Enden. Das Missions-Framework (M7) führt Aufträge aus; M14 entscheidet, *welche* Aufträge, Wendungen und Enden es gibt.

### 6.1 Gestaltungsprinzipien (damit der Umfang beherrschbar bleibt)

1. **Perlenketten-Struktur (Bottleneck):** Eine feste Kette von *Hauptknoten* (alle Spieler erreichen sie), dazwischen **verzweigte Abschnitte**, die wieder zusammenlaufen. Das verhindert die kombinatorische Explosion: Entscheidungen ändern *Inhalt, Verbündete, Schauplatz und Ausgang*, nicht den gesamten Handlungsbaum.
2. **Konsequenzen sichtbar in der Welt**, nicht nur im Dialog: Bezirke verändern sich (Weltzustand-Tags, 6.5).
3. **Keine Gut/Böse-Anzeige.** Statt eines Moralbalkens zählen **konkrete Taten** (Flags) und **Beziehungen**; Fraktionen belohnen und bestrafen unterschiedlich.
4. **Wendungen entstehen aus Daten:** Wer verrät, wer überlebt, wer hilft – ergibt sich aus Vertrauenswerten und Flags, nicht aus einem Würfelwurf.
5. **Test per Simulation:** Ein Skript spielt die Entscheidungsgraphen durch und beweist, dass jedes Ende erreichbar ist und es keine Sackgassen gibt (6.6).

### 6.2 Fraktionen

| Fraktion | Rolle in der Dystopie | Zentrum | Will | Gibt dem Spieler |
|---|---|---|---|---|
| **Aurelia Holdings** | Konzern; betreibt das Überwachungsnetz Aegis und die Konzernpolizei | Kestrel Core | Aegis „Phase Zwei“ einführen (Zugang zu Straßen, Kredit, Wohnen an einen Verhaltens-Score koppeln) | Geld, Kennzeichen-Löschung, Aufstieg, Schutz vor Aegis |
| **Kestrel Civil Guard (KCG)** | Private Sicherheitspolizei, vom Konzern finanziert (M6) | Core, Greenmoor | Ordnung und Konzerninteressen | (Gegner/Zwangsmittel; über Bestechung beeinflussbar) |
| **Dockers' Union** | Hafenarbeiter, Streikbewegung | Saltmarket | Hafenautonomie, faire Löhne | Schmuggel-/Fahrerjobs, Verstecke, Muskeln |
| **Lantern Collective** | Hacker, Aktivisten, Anwohner der Altstadt | Lantern Hill | Aegis stoppen, Beweise veröffentlichen | Hacking-Fähigkeiten (Kameras, Ampeln), Informationen |
| **Rustline Wrenchers** | Garagen-/Rennszene, Schrottkönige | Rustline | Eigenständigkeit, Geld, Respekt | Fahrzeuge, Tuning, Straßenrennen, Fluchtwege |

Zusätzlich Greenmoor als „Fraktion ohne Stimme“: Anwohner, die auf Sicherheit pochen; sie lenken die Polizeipräsenz.

### 6.3 Ruf- und Entscheidungssystem

Vier getrennte Werte (jede Größe hat einen eigenen Zweck):

| Wert | Skala | Wirkung |
|---|---|---|
| **Fraktionsstand** (je Fraktion) | −100 … +100, Schwellen: Feind / Misstrauisch / Neutral / Verbündet / Vertraut | Schaltet Aufträge, Preise, Fluchthilfe, Fraktionsfinale frei; Feinde greifen an |
| **Bezirks-Heat** (je Bezirk, M6) | 0 … 5 | Polizeidruck und Wachsamkeit im Bezirk |
| **Aegis-Profil** (global) | 0 … 100 | Wie schnell Kameras einen erkennen; sinkt durch Kennzeichentausch, Hacks, Untertauchen; steigt durch Kennzeichen-Treffer bei Verbrechen |
| **NPC-Vertrauen** (je Schlüsselfigur) | −3 … +3 | Bestimmt Wendungen (Verrat, Hilfe, Opfer) |

Dazu **Tat-Flags** (Boolesche Werte wie `killedCivilians`, `betrayedUnion`, `savedJonas`, `leakedLedger`), die Bedingungen für Missionen, Dialoge und Enden bilden.
Konsequenzen laufen über **Effekte** (Datenmodell 6.5): Standard ändern, Flag setzen, Weltzustand-Tag setzen, Mission freischalten/sperren, Gegenstand geben.

### 6.4 Haupthandlung: „Nullstunde“

**Protagonistin:** *Mira Voss*, ehemalige Rennfahrerin und Fluchtwagenfahrerin. Die Namen sind fiktiv und frei änderbar.
**Setup:** Mira kehrt nach sieben Jahren zurück, weil ihr Bruder *Jonas Voss*, Datenanalyst bei Aurelia, verschwunden ist. Bei der Einreise am Highway-Checkpoint registriert Aegis ihr Kennzeichen – sie ist ab jetzt im Netz. In 14 Spieltagen geht Aegis „Phase Zwei“ live (Countdown „Nullstunde“).

| Akt | Inhalt | Wendepunkt | Spielerabhängigkeit |
|---|---|---|---|
| **Prolog – Rückkehr** | Tutorial-Kurierfahrten, Checkpoint, erste Kontakte in Saltmarket und Rustline | – | Erste Loyalität: Wer hilft Mira zuerst (Union oder Wrenchers)? Startstände +20/+10 |
| **Akt I – Schuldner** | Aufträge für Union und Wrenchers, Spur zu Jonas; Aegis-Kameras prägen das Fahren | **W1 – „Der Fund“:** Jonas lebt und hat einen Datenträger („Ledger-Kern“) mit Beweisen gegen Aurelia versteckt | Wem Mira den Fund zeigt, bestimmt die Hauptallianz (Union, Collective oder Konzern); sie bekommt zwei Verbündete |
| **Akt II – Netz** | Kampf um Informationen; Heat-Bezirke; Kamera-Hacks; Konzern lockt mit Angeboten | **W2 – „Verrat“:** Eine Vertrauensperson verrät Mira an die KCG. **Wer** verrät, hängt vom NPC-Vertrauen ab (höchstes Vertrauen = Verrat wiegt am schwersten; niedriges Vertrauen = Verrat erwartet, früher) | Verhalten in Akt I wirkt unmittelbar; ein Spieler mit konsequent hohem Vertrauen aller wird *nicht* verraten (dafür andere Wendung) |
| | | **W3 – „Der Preis“:** Aurelia bietet Mira Jonas’ Freiheit, Kennzeichenlöschung und eine Position an – gegen Verrat an ihren Verbündeten | Annehmen/Ablehnen/Vortäuschen (Doppelspiel möglich, wenn Vertrauen und Aegis-Profil niedrig) |
| **Akt III – Nullstunde** | Kampf um die Bezirke; Fraktionen stehen auf den Barrikaden; Welt sichtbar verändert | **W4 – „Der Vorabend“:** Jonas’ Schicksal (gerettet / Doppelagent / tot) | Ergibt sich aus Flags (`savedJonas`, `betrayedX`) und Vertrauen |
| **Finale – Uplink** | Mission auf Route 9 und am Ashridge-Sendemast („Aegis Uplink“) | **W5 – „Die Entscheidung“:** Aegis übernehmen, abschalten, verkaufen oder fliehen | Verfügbare Optionen hängen von Allianzen und Flags ab |

**Mehrere Enden** (Bedingungen als Daten in 6.5):

| Ende | Titel | Bedingung (vereinfacht) | Folge für die Welt |
|---|---|---|---|
| E1 | **Goldener Käfig** | Konzernstand ≥ Vertraut, W3 angenommen | Aegis geht live; Mira wird Teil des Systems; Welt behält die Überwachung, nur der Spieler profitiert |
| E2 | **Hafenrepublik** | Union ≥ Vertraut, Konzern ≤ Feind | Docks autonom, Stadt gespalten; Core bleibt unter Aurelia |
| E3 | **Blackout** | Collective ≥ Vertraut, Uplink zerstört | Netz fällt, Stadt dunkel und chaotisch; Hoffnung und Anarchie |
| E4 | **Exil** | Fluchtroute gewählt oder kein Allianz-Schwellwert erreicht | Mira und Jonas verlassen die Stadt; Stadt bleibt unverändert; Kurz-Ende |
| E5 *(geheim)* | **Offene Bücher** | Alle drei Fraktionen ≥ Verbündet, `savedJonas`, kein `killedCivilians` | Beweise werden veröffentlicht; Aegis wird gerichtlich gestoppt; fragile Reform |

**Umfang (Richtwert):** 14 Hauptknoten, je Fraktionsroute 4 Zweigmissionen (Union, Collective, Wrenchers, Konzern), 4–6 Entscheidungsszenen, 5 Wendepunkte, 5 Enden → ca. **28–32 Missionen** plus ca. 10–15 Nebenaufträge je Bezirk-Fraktion.

**Hinweis zur Originalität:** Handlung, Figuren, Fraktionen und Schauplätze sind frei erfunden; es werden nur allgemeine Dystopie-Motive (Überwachung, Konzernmacht, soziale Spaltung) verwendet.

### 6.5 Datenmodell

Alle Story-Inhalte sind **Daten** (JSON bzw. `Resource` in `data/story/`), kein Code. Die Engine (Autoload `story.gd`) wertet Bedingungen aus und wendet Effekte an.

```text
// data/story/ – Schema als Pseudocode (Umsetzung: GDScript-Klassen mit to_dict()/from_dict(), Speicherung als JSON)

/** Dauerhafter Story-Zustand, wird gespeichert (M8) und – online – serverseitig geführt (M15). */
GameStory = {
  schema: 3,                        // Speicherstand-Version
  day: 4, hour: 17.5,               // Spielzeit (Countdown Nullstunde)
  standing: { aurelia: -10, union: 35, collective: 5, wrenchers: 20, kcg: -25 },
  npcTrust: { jonas: 2, brann: 1, nox: -1, ravn: -2 },   // −3..+3
  flags: { savedJonas: false, betrayedUnion: false, killedCivilians: 0 },
  worldTags: { saltmarket: ['strike'], lantern: ['aegis_dense'] }, // Bezirks-Tags (M3/M5/M6/M9)
  completed: ['p1', 'a1_dockjob', 'w1_find'],
  active: ['a2_hack_cams'],
  decisions: [{ id: 'w1_show_ledger', choice: 'union', at: 1730 }], // für Rückblick/Statistik
  aegis: 42                         // Aegis-Profil
}

/** Ein Missionsknoten. Hauptknoten haben main: true. */
Mission = {
  id: 'w2_betrayal', title: 'Verrat', main: true, act: 2,
  requires: [Cond],                 // wann verfügbar
  excludes: [Cond],                 // wann gesperrt
  variants: [                       // inhaltliche Varianten je Zustand
    { when: [{ npcTrust: ['brann', '>=', 2] }], script: 'w2_brann_betrays' },
    { when: [{ npcTrust: ['nox', '>=', 2] }],   script: 'w2_nox_betrays' },
    { when: [],                                  script: 'w2_ravn_ambush' }  // Fallback
  ],
  steps: [ /* Schritte des Runners (M7) */ ],
  choices: [Choice],                // Entscheidungen innerhalb der Mission
  onSuccess: [Effect], onFail: [Effect]
}

Choice = {
  id: 'w3_offer', text: 'Aurelias Angebot…',
  options: [
    { id: 'accept', text: 'Annehmen',   when: [],                         effects: [Effect] },
    { id: 'refuse', text: 'Ablehnen',   when: [],                         effects: [Effect] },
    { id: 'fake',   text: 'Vortäuschen', when: [{ aegis: ['<', 30] }, { npcTrust: ['nox', '>=', 1] }], effects: [Effect] }
  ]
}

Cond   = { standing: ['union', '>=', 50] } | { flag: ['savedJonas', '==', true] }
       | { npcTrust: ['nox', '>=', 2] }    | { done: 'a1_dockjob' }
       | { tag: ['saltmarket', 'has', 'strike'] } | { aegis: ['<', 30] }
       | { any: [Cond] } | { all: [Cond] } | { not: Cond }

Effect = { standing: ['union', +15] } | { npcTrust: ['nox', -1] } | { flag: ['betrayedUnion', true] }
       | { tag: ['saltmarket', 'add', 'strike'] } | { unlock: 'a3_union_blockade' } | { lock: 'a3_collective_hack' }
       | { money: +500 } | { aegis: +10 } | { heat: ['saltmarket', +1] }

Ending = { id: 'E2', title: 'Hafenrepublik', priority: 20,
           when: [{ standing: ['union', '>=', 70] }, { standing: ['aurelia', '<=', -50] }],
           epilogue: 'epilogue_e2', worldTagsAfter: { saltmarket: ['free_port'] } }
```

**Auswertung:** Ende = das Ende mit der höchsten `priority`, dessen `when` erfüllt ist; `E4 Exil` ist Fallback. Bedingungen sind rein deklarativ → serverseitig prüfbar (wichtig für Anti-Cheat, Kapitel 7) und im Story-Simulator testbar.

### 6.6 Weltzustand, Konsequenzen und Test

- **Weltzustand-Tags** (z. B. `strike`, `aegis_dense`, `blackout`, `free_port`, `gentrified`) wirken als Parameter-Überschreibungen auf `DistrictDef` (M3): andere Verkehrsprofile (M5), mehr/weniger Kameras und KCG (M6), geschlossene Docks, Lichter, Graffiti (M13), Ambient-Klang (M10).
- **Story-Simulator** (`tests/story_sim.gd`, läuft headless: `godot --headless --script …`): erzeugt zufällig alle Entscheidungskombinationen (oder eine feste Stichprobe von mehreren Tausend), spielt den Graph durch und meldet: nicht erreichbare Missionen/Enden, Sackgassen (keine aktive/verfügbare Mission vor einem Hauptknoten), widersprüchliche Flags. Wird in CI/Skript ausgeführt.
- **Rückblick:** Entscheidungsprotokoll (`decisions`) speist ein „Was wäre gewesen“-Menü nach dem Ende (Wiederspielwert).

### 6.7 Stufen, Technik, Risiken

| Stufe | Umfang |
|---|---|
| MVP | Datenmodell + Engine (Bedingungen/Effekte), 2 Fraktionsstände, 1 Entscheidungsszene, 1 Wendung (W1), 2 Enden, Speichern; Story-Simulator-Grundversion |
| Ausbau | Alle 5 Fraktionen, 4 NPC-Vertrauenswerte, W1–W5, E1–E4, Weltzustand-Tags mit sichtbaren Bezirksänderungen, Dialog-UI mit Entscheidungsoptionen, Rückblick-Menü |
| Wunschziel | Geheimes Ende E5, Beziehungsszenen, dynamische Nebenaufträge je Weltzustand, Lokalisierung (DE/EN), Editor-Werkzeug für Story-Graphen |

**Technische Umsetzung:** reine Daten + deterministische Engine; kein Zufall in Story-Entscheidungen; Ereignisse über Signale des `EventBus`-Autoloads (`mission.completed`, `crime`, `district.entered`); Texte in `data/story/text.de.json` (später Godots Übersetzungssystem/`tr()`).
**Ideen aus den Design-Vorbildern (Kapitel 9):** Entscheidungsgewicht und verzögerte Folgen (9.2), Hauptfiguren, Dialog-/Flag-Graph und drei Missionsketten (6.8–6.10).
**Abhängigkeiten:** M7 (Runner), M6 (Verbrechen/Heat), M3/M5/M9/M13 (Weltzustand-Wirkung), M8 (Speichern). **Risiken:** (1) Content-Umfang → Perlenkette und Variantentexte statt vollständiger Verzweigung; (2) Soft-Locks → Story-Simulator, Notfall-Reset „Mission neu starten“; (3) Konsistenzfehler in Dialogen → Bedingungen zentral prüfen, Review-Checkliste; (4) Mehrspieler: globale Story-Spoiler → Story bleibt **pro Spieler** (Kapitel 7.8). **Aufwand:** MVP 8 PT · Ausbau 20 PT · Wunsch 25+ PT (ohne Texte/Vertonung).

### 6.8 Hauptfiguren

Alle Figuren sind frei erfunden. `trust` ist der NPC-Vertrauenswert (−3…+3, Kapitel 6.3).

| Figur | Rolle | Motiv | Geheimnis | Funktion in der Handlung | Wandel nach Vertrauen |
|---|---|---|---|---|---|
| **Mira Voss** (Protagonistin) | Ex-Rennfahrerin, je nach Herkunft (9.3) Fahrerin, Hafenkind oder Konzern-Aussteigerin | Bruder finden, Schuld begleichen | Sie hat vor sieben Jahren ein Rennen verkauft, das den Bruder in Schulden stürzte | Spielerfigur; Herkunft färbt Dialoge | – |
| **Jonas Voss** | Bruder, Datenanalyst bei Aurelia, untergetaucht | Beweise zu Aegis sichern | Er hat Daten verkauft, bevor er floh (Teilschuld) | Auslöser (W1), Schicksal in W4 | `savedJonas` bestimmt E5 |
| **Dr. Idris Kandel** | Vorstand Aurelia Holdings, Architekt von Aegis „Phase Zwei“ | „Ordnung durch Sichtbarkeit“ – glaubt, Aegis rettet die Stadt | Aegis-Unfälle (Fehlfestnahmen) werden vertuscht | Gegenspieler; macht in W3 das Angebot | Bei Vertrauen +2 zeigt er echte Beweggründe (Tragödie in der Familie) |
| **Cmdr. Ravn Okoye** | Leiter der Kestrel Civil Guard | Pflichtgefühl, Karriere | Zweifelt an Aegis, folgt Befehlen | Jäger in Akt I–II; kann in Akt III die Seite wechseln | Vertrauen ≥ +2 → warnt Mira vor Razzien, schützt in E5 |
| **Tomasz „Brann“ Brandt** | Anführer der Dockers' Union | Arbeitsplätze, Würde | Hat einen Deal mit dem Konzern erwogen | Verbündeter, mögliche Verrat-Figur (W2) | Hoch: tiefer Verrat; niedrig: bleibt Zweckpartner |
| **„Nox“** | Hackerin/Hacker des Lantern Collective (Pronomen offen halten) | Aegis stoppen | Kennt Jonas besser als angegeben | Quelle für Hacks; Verrat-Variante (W2) | Hoch: gibt den „Ledger-Schlüssel“ |
| **Yara Quill** | Sprecherin des Lantern Collective, Predigerin am Glockenturm | Altstadt retten | Hat Abrissdaten zurückgehalten | Moralische Gegenstimme in W3 | – |
| **Odalys „Dutch“ Halloran** | Chefin der Rustline Wrenchers | Respekt, Unabhängigkeit | Wettet auf alle Seiten | Fluchtwege, Fahrzeuge, Rennen | Hoch: bietet Fluchtroute (E4) |
| **Pia Sorensen** | Moderatorin von *Open Line* (Radio) | Wahrheit, Quote | Quelle ist ein Konzern-Insider | Verbindet Welt und Spieler; kündigt Folgen an (Kap. 9.6) | Zeugin in E5 |

### 6.9 Quest-, Dialog- und Flag-Graph (Datenmodell)

**Drei Graphen, ein Zustand:**
1. **Quest-Graph:** Knoten = Beats/Missionen, Kanten = Bedingungen (`requires`), siehe 6.5/9.1.
2. **Dialog-Graph:** Knoten = Textzeilen und Wahlmöglichkeiten, eingebettet in Beats.
3. **Flag-Registry:** zentrale Liste aller erlaubten Flags/Werte mit Namensraum (verhindert Tippfehler und Geister-Flags).

```text
DialogGraph = { id: 'a2_brann_confront', speaker_map: { brann: 'npc_brann' }, start: 'n1', nodes: {
  n1: { type: 'line',   who: 'brann', text: 'tr:a2.brann.n1', next: 'n2' },
  n2: { type: 'choice', options: [
        { text: 'tr:a2.brann.ask_deal',  when: [{ skill: ['street', '>=', 2] }], effects: [{ npcTrust: ['brann', -1] }], next: 'n3a' },
        { text: 'tr:a2.brann.stay_calm', when: [],                              effects: [],                            next: 'n3b' } ] },
  n3a: { type: 'check', cond: { flag: ['story.a1.cargo_destroyed', '==', true] }, yes: 'n4a', no: 'n4b' },
  n3b: { type: 'line',  who: 'brann', text: 'tr:a2.brann.n3b', next: 'end' },
  n4a: { type: 'effect', effects: [{ flag: ['story.w2.brann_exposed', true] }], next: 'end' },
  n4b: { type: 'line',  who: 'brann', text: 'tr:a2.brann.n4b', next: 'end' },
  end: { type: 'end' } } }

FlagRegistry = {
  'story.w1.shown_to':      { type: 'enum', values: ['none','union','collective','aurelia'], default: 'none' },
  'story.w2.betrayer':      { type: 'enum', values: ['none','brann','nox','ravn'],            default: 'none' },
  'story.savedJonas':       { type: 'bool', default: false },
  'npc.brann.trust':        { type: 'int',  min: -3, max: 3, default: 0 },
  'world.saltmarket.tags':  { type: 'set',  values: ['strike','free_port','blockade'] },
  'stat.civilians_killed':  { type: 'int',  min: 0, default: 0 } }
```

**Regeln:** (1) Jedes Flag muss in der Registry stehen; unbekannte Flags sind Fehler (Lint). (2) Namensräume `story.*`, `npc.*`, `world.*`, `stat.*`, `quest.*`. (3) Texte ausschließlich als Schlüssel (`tr:…`) → Lokalisierung und Untertitel. (4) Der **Graph-Lint** (`tests/graph_lint.gd`) prüft: Erreichbarkeit aller Knoten, keine Sackgassen, jede Auswahloption hat Ziel, jeder Effekt nennt ein registriertes Flag, jede Bedingung ist auswertbar. (5) Debug-Konsole: `flag set story.savedJonas true`, `quest start a2_blockade`, `jump w3`, `dump_state` – für schnelles Testen. (6) Alle Graphen sind rein deklarativ → serverprüfbar (Kapitel 7.7) und speicherbar (Kapitel 11).

### 6.10 Drei vollständig skizzierte Missionsketten

Jede Kette läuft in der Hauptstory parallel; welche Kette dominiert, hängt von W1 und dem Fraktionsstand ab. Typen sind in Kapitel 10 definiert.

#### Kette A – „Der Hafenpakt“ (Dockers' Union, Akt I–II)

| # | Mission | Typ / Ort | Ablauf | Entscheidung | Folge (Flags/Welt) | Belohnung |
|---|---|---|---|---|---|---|
| A1 | **Frühschicht** | Lieferung · Saltmarket → Core | Fracht vor 6:00 Uhr in den Core fahren; Aegis-Checkpoint umgehen oder Pass vorzeigen | Pass (sauber, kostet Standing bei Union) oder Hintertür (riskant) | `a1.route = pass/back`, Aegis-Profil ±5 | $400, Union +10 |
| A2 | **Der Streikbrecher** | Verfolgung · Hafen/Route 9 | Konzern-Truck mit Ersatzarbeitern jagen, abdrängen | Fahrer verschonen oder Ladung zerstören | `a1.cargo_destroyed`; Tag `strike` bei Zerstörung | $600, Union +15 / KCG −10 |
| A3 | **Docks bei Nacht** | Flucht · Saltmarket | Schmuggelware zum Versteck bringen, KCG-Streifen abschütteln (Bezirks-Heat 3) | Ware abgeben oder Teil für Jonas-Spur behalten | `a3.kept_sample` (öffnet Hinweis auf Jonas) | $500, Hinweis |
| A4 | **Der Preis des Lohns** | Dialog/Entscheidung · Brann | Brann gesteht Gespräche mit dem Konzern; Mira stellt ihn zur Rede (Dialog 6.9) | Brann decken / enttarnen / erpressen | `npc.brann.trust` ±2; Variante für W2 | Standing, Info |
| A5 | **Blockade** | Bezirks-Event · Saltmarket | Straßensperren gegen KCG organisieren (Fahrten, Barrikaden), zeitlich begrenzt | Gewalt (Heat ↑) oder Aussitzen (Zeitdruck) | Tag `blockade`, Hafen autonom bei Erfolg | $1000, Union +25, Zugang E2 |

#### Kette B – „Blackout-Protokoll“ (Lantern Collective, Akt I–III)

| # | Mission | Typ / Ort | Ablauf | Entscheidung | Folge | Belohnung |
|---|---|---|---|---|---|---|
| B1 | **Blinde Flecken** | Rennen/Aktivität · Lantern Hill | Kamera-Slalom durch Aegis-Blindzonen lernen (Tutorial für Hack-Modul) | – | Skill Technik +1, Kamera-Hack freigeschaltet | $250, Modul |
| B2 | **Das Archiv** | Einbruch/Verfolgung · Core | Datenträger aus einem Konzernarchiv holen: Hochstraße anfahren, Kameras hacken, mit Beute fliehen | Datenträger kopieren (schnell) oder Original (riskant, mehr Inhalt) | `b2.original` → mehr Beweise für E5 | $800, Collective +20 |
| B3 | **Der Verräter** | Dialog/Verfolgung · Lantern Hill | Nox wird (oder nicht) als Verräter enttarnt; Verfolgung durch Gassen | Nox stellen / entkommen lassen / Konzern melden | `story.w2.betrayer` (nur wenn nox-Vertrauen ≥ 2), Collective ±30 | Info, Standing |
| B4 | **Uplink-Probe** | Lieferung + Hack · Ashridge | Störsender zum Sendemast fahren, kurzen Blackout in Core testen | Test durchführen oder abbrechen | Tag `blackout` temporär; Aegis-Dichte ↓ | $700, Collective +15 |
| B5 | **Nullstunde: Blackout** | Finale/Flucht · Route 9 → Ashridge | Uplink zerstören, Konzern- und KCG-Jagd über Highway | Uplink zerstören oder kapern (nur bei Konzern-Allianz) | E3 „Blackout“ oder Variante E5 (Voraussetzungen siehe 6.4) | Ende |

#### Kette C – „Schrottkönige“ (Rustline Wrenchers, Akt I–III)

| # | Mission | Typ / Ort | Ablauf | Entscheidung | Folge | Belohnung |
|---|---|---|---|---|---|---|
| C1 | **Probefahrt** | Rennen · Rustline | Qualifikationsrennen gegen drei Fahrer; Vorgeschichte der Wrenchers | Fair fahren oder rammen | `c1.dirty` → Dutch Respekt ↓ aber Gegner meiden Mira | $300, Wrenchers +10 |
| C2 | **Ersatzteile** | Lieferung/Verfolgung · Hafen → Rustline | Teile aus dem Hafen vor KCG-Zugriff retten | Teile teilen (Union +) oder behalten (Wrenchers +) | Verbündete-Konflikt Union/Wrenchers | $500, Standing |
| C3 | **Die Revanche** | Rennen · Sunstrip/Küstenstraße | Rennen gegen den Konzern-Sponsorfahrer; Wetter wechselt (Regen) | Gewinnen oder absichtlich verlieren, um Wetten/Infos zu bekommen | `c3.lost_on_purpose` → Info über Kandel | $1200 / Info |
| C4 | **Fluchtroute Route 9** | Flucht · Route 9 | Jonas oder Dokumente aus dem Stadtgebiet schaffen; Straßensperren, Hubschrauber | Mit Jonas fliehen (E4) oder zurückfahren | `savedJonas` ↔ E4-Pfad, Wrenchers ≥ Vertraut | $1500, E4 öffnen |

**Anforderungen an jede Kette (Qualitätskriterien):** (1) mindestens zwei echte Entscheidungen, (2) mindestens eine **verzögerte Folge** (9.2), (3) mindestens ein systemischer Eingriff (Verkehr/Polizei/Wetter) ohne Skript-Bruch, (4) jede Mission hat Fail-Zustand und Checkpoint, (5) jede Mission nennt Fraktions-Standing-Änderung. Die drei Ketten ergeben zusammen 14 der ca. 28–32 Hauptstory-Missionen; die restlichen Missionen (Prolog, Konzernroute, Finale) folgen demselben Schema.

---

## 7. Open World trifft MMO: Server und Mehrspieler (M15)

**Ziel:** Eine geteilte, persistente Stadt für viele Spieler *zusätzlich* zu einem vollwertigen Einzelspielermodus. **Reihenfolge ist verbindlich:** erst Einzelspieler stabil (bis Meilenstein 8), dann Server und Mehrspieler (ab Meilenstein 9).

### 7.1 Ehrliche Einordnung

- „MMO“ heißt hier realistisch: **ein Realm mit ca. 50–100 gleichzeitigen Spielern** (Richtwert, per Lasttest zu bestimmen), nicht Tausende in einer Szene. Mehrere Realms skalieren die Gesamtzahl.
- Ein MMO braucht **Dauerbetrieb**: Server, Datenbank, Backups, Moderation, Datenschutz (DSGVO), Kosten. Das ist ein eigenes Projekt neben dem Spiel. Mehrspieler bleibt **optional**; das Spiel muss ohne Server voll funktionieren.
- Auch ein **nativer Godot-Client ist manipulierbar** (Speicher, Dateien, Netzwerkverkehr). Es gibt keine absolute Cheat-Freiheit; Ziel ist Schadensbegrenzung durch Serverautorität (7.7).
- Godots eingebaute Multiplayer-API ist für kleinere Sessions gedacht; für ein persistentes MMO braucht es zusätzlich Backend-Dienste (Konten, Datenbank). Das Verhalten bei 50–100 Spielern mit Fahrzeugen ist **nicht belegt** → früh per Lasttest mit Bots prüfen.

### 7.2 Architektur (Godot)

```mermaid
flowchart LR
  subgraph CL["Client (Godot, Windows)"]
    C1["Rendering, UI, Audio (M2/M10/M11/M13)"]
    C2["Prädiktion + Interpolation"]
  end
  subgraph BE["Backend-Dienst (HTTP/JSON)"]
    AU["Anmeldung, Token, Realm-Liste"]
    PG[("PostgreSQL: Konten, Charaktere, Weltzustand")]
  end
  subgraph RL["Realm (Weltinstanz, ~50–100 Spieler)"]
    Z1["Zone-Server (Godot --headless): Kestrel Core"]
    Z2["Zone-Server (Godot --headless): Saltmarket"]
    Z3["Zone-Server (Godot --headless): Umland/Highway"]
  end
  C1 --> C2
  C2 -->|"HTTPS: Login, Token"| AU
  AU --> PG
  C2 <-->|"ENet (UDP) bzw. WebSocket: Eingaben / Zustand"| Z1
  Z1 <-->|"Übergabe an Zonengrenze"| Z2
  Z2 <--> Z3
  Z1 -->|"Speichern (HTTPS/intern)"| AU
  Z2 --> AU
  Z3 --> AU
```

**Grundentscheidungen**
- **Godot High-Level-Multiplayer-API** (`MultiplayerAPI`/`SceneMultiplayer`): Transport **`ENetMultiplayerPeer`** (UDP, Standard, geringe Latenz) für Windows-Clients; **`WebSocketMultiplayerPeer`** (TCP) als Alternative, falls UDP blockiert ist oder später eine Web-Version entsteht. Der Server hat immer die Peer-ID 1 [Q31][Q32].
- **Autoritativer Dedicated Server:** Godot-Projekt läuft auf dem Server **headless** (`--headless` bzw. Export mit Feature-Tag `dedicated_server`, Abfrage `OS.has_feature("dedicated_server")`), ohne Fenster/Rendering, aber mit derselben Simulation (`sim/`-Skripte) wie der Client [Q32]. Dazu die Regel aus Kapitel 3.2: Simulationslogik kennt keine Meshes/Materialien.
- **Clients senden nur Eingaben** (Gas, Lenkung, Bremse, Aktion), nie Positionen. `@rpc`-Funktionen mit `any_peer` validieren **immer** `multiplayer.get_remote_sender_id()` und die Parameter; Objekte werden **nie** aus Netzwerkdaten deserialisiert (kein `bytes_to_var_with_objects`/Objekt-Decoding).
- **`MultiplayerSynchronizer`/`MultiplayerSpawner`** eignen sich für einfache Replikation (Spieler-Szenen spawnen, Zustände). Für Fahrzeuge und Massenobjekte besser **eigene Snapshots** (kompakt, quantisiert, gefiltert nach Interessenbereich) – ein reiner Synchronizer-Einsatz kann in großen Szenen die Bandbreite sprengen [Q32].
- **Einzelspieler = derselbe Pfad:** Der Einzelspieler nutzt `OfflineMultiplayerPeer` (Spieler ist „Server“); RPC-/Autoritätscode ist in beiden Modi identisch.
- **Persistenz über einen Backend-Dienst** (HTTP/JSON, beliebige Sprache), weil Godot keinen nativen Datenbanktreiber für PostgreSQL mitbringt; Server und Backend sprechen per `HTTPRequest`. Alternative für lokale Entwicklung: SQLite-Add-on *(Eignung prüfen)*.

### 7.3 Zonen und Sharding

| Begriff | Bedeutung |
|---|---|
| **Realm** | Eine vollständige Weltinstanz mit eigener Spielerliste, Weltzustand und Wirtschaft; maximal ca. 50–100 Spieler (Richtwert). Weitere Spieler → weiterer Realm (Shard). |
| **Zone** | Räumlicher Teil des Realms (6 Bezirke plus Umland ≈ 8 Zonen). **Ein Godot-Prozess pro Zone** (mehrere Welten parallel im selben SceneTree sind nicht vorgesehen); Prozesse sprechen untereinander über ENet/Backend. |
| **Interessenbereich (Area of Interest)** | Ein Spieler bekommt nur Entitäten innerhalb von ca. 300–400 m (Gitter = Chunks); in Godot über eigene Snapshot-Filter (Synchronizer-Sichtbarkeit für wenige Objekte). |
| **Grenzübergang (Handoff)** | Nähert sich eine Entität der Zonengrenze, wird sie in der Nachbarzone als „Geist“ gespiegelt und beim Überschreiten übergeben (Zustand + Sequenznummer); Überlappung ca. 100 m. Client verbindet sich dabei mit dem nächsten Zonenprozess. |
| **Phase** | Privater Story-Ausschnitt eines Spielers (7.8): Story-NPCs und Missionsobjekte existieren nur für ihn/seine Gruppe (Sichtbarkeitsfilter). |

*Hinweis:* Zu zonenbasiertem Sharding mit Grenz-Handoff fand die Recherche **keine belastbare Quelle**; die Konzeption folgt allgemeinen Praktiken (Instanzen, Interessenmanagement). **Anfangs genau eine Zone pro Realm**, geteilt wird erst nach Lasttest.

### 7.4 Was synchronisiert wird

| Objekt | Autorität | Rate | Methode |
|---|---|---|---|
| Spielerfigur / eigenes Fahrzeug | Server validiert, Client sagt voraus | Eingaben 30–60 Hz, Snapshots 15–20 Hz | Client-Prädiktion + Server-Korrektur (Reconciliation) [Q20] |
| Fremde Spieler und Fahrzeuge | Server | 15–20 Hz (nach Entfernung gestaffelt) | Interpolation (ca. 100–150 ms Zeitversatz) [Q21] |
| Kollisionen zwischen Spielern | Server | ereignisbasiert | Server entscheidet, Client zeigt Effekt |
| Ambient-Verkehr/Passanten | **Nahe an Spielern: Server (Sim-Stufe A)**; fern: lokal deterministisch (Seed + Spielzeit) | 5–10 Hz nah | Wichtige Fahrzeuge (Polizei, Missionsziele) immer serverseitig; Hintergrundverkehr darf je Client leicht abweichen |
| Polizei/Fahndung | Server | ereignisbasiert + 10 Hz Positionen | Server-Dispatcher; Sichtlinie serverseitig (Raycast) |
| Geld, Inventar, Fahrzeugbesitz, Standing, Flags | **Nur Server** | bei Änderung | Transaktional im Backend |
| Missionsfortschritt | Server (pro Spieler) | bei Ereignis | Server prüft Bedingungen (Position, Zeit, Zustand) |
| Weltzustand (Bezirks-Tags, Fraktionskontrolle) | Server (pro Realm) | langsam (Minuten) | Aggregation aus Spieleraktionen mit Dämpfung |
| Tageszeit/Wetter | Server (pro Realm) | 1 Hz | Client interpoliert lokal |
| Chat/Ping/Emotes | Server | Ereignis | Rate-Limit, Filter |

**Bandbreitenbudget (Überschlag):** 20 sichtbare Fahrzeuge × ca. 24 Byte × 15 Hz ≈ 7 KB/s je Client abwärts, plus Weltereignisse; mit Quantisierung (Position in cm, Winkel in 1/256) und Deltakompression darunter. Zielwert < 30 KB/s je Client.

### 7.5 Netzcode

- **Kompakte Pakete:** `PackedByteArray` mit eigener Kodierung (`encode_*`/`decode_*`) für Snapshots; `@rpc("unreliable_ordered")` für Positionsdaten, `@rpc("reliable")` für Ereignisse/Transaktionen; versionierte Nachrichten, `tick`/`seq`-Nummern.
- **Client-Prädiktion und Reconciliation:** Client wendet Eingaben sofort an, merkt sich unbestätigte Eingaben mit Sequenznummer; bei Server-Snapshot setzt er zurück und wiederholt ausstehende Eingaben. Für andere Spieler **Interpolation** [Q20][Q21]. Bei physikbasierten Fahrzeugen (`RigidBody3D`) ist exakte Wiederholung schwierig → Fahrzeug-Prädiktion wird am Prototyp erprobt; Fallback: Server-autoritatives Fahrzeug mit Glättung.
- **Tick-Raten:** Physik 60 Hz (`physics_ticks_per_second`), Netzwerk-Snapshots 15–20 Hz [Q33].
- **Lag-Kompensation** nur für Trefferprüfungen (später).
- **Verbindungsabbruch:** Fahrzeug bleibt kurz „Geist“ (5 s), Wiederverbinden mit Sitzungstoken.

### 7.6 Persistenz

| Daten | Speicher | Anmerkung |
|---|---|---|
| Konten, Anmeldung | Backend + PostgreSQL | E-Mail-Login mit Magic Link oder OAuth; keine Klartext-Passwörter, ggf. Passkeys |
| Charaktere (`GameStory`, Geld, Fahrzeuge, Standing, Flags) | PostgreSQL (JSONB + relationale Kerne) | Schema-Versionierung und Migrationen von Anfang an |
| Weltzustand je Realm | PostgreSQL | Periodisch (alle 1–5 min) und bei Ereignissen („write-behind“) |
| Sitzungen/Warteschlangen/Realm-übergreifender Chat | Backend (Redis optional) | Flüchtig |
| Einzelspieler-Speicher | JSON-Dateien in `user://saves/` | **Gleiches Schema wie online** → ein Pfad im Code; **kein** `.tres/.res`-Speichern (Skripteinbettung) |

**Regeln:** Wirtschaftliche Änderungen (Kauf, Belohnung) als Transaktion; tägliche Backups; Löschkonzept für personenbezogene Daten (DSGVO: Auskunft, Löschung; Hosting in der EU bevorzugen).

### 7.7 Anti-Cheat – Grundlagen (realistisch)

Ein nativer Client lässt sich ebenfalls nicht „absichern“. Strategie ist **Serverautorität plus Plausibilitätsprüfung**:

1. **Never trust the client:** Geld, Fahrzeuge, Standing, Flags, Missionsabschluss und Schaden entscheidet nur der Server.
2. **Eingaben statt Zustände:** Client sendet Lenkung/Gas/Bremse, nie Positionen. Der Server simuliert mit denselben `VehicleDef`-Daten; Abweichungen führen zu Korrektur (Rubber-Banding), nicht zum Vertrauen.
3. **Plausibilitätsgrenzen:** maximale Beschleunigung/Geschwindigkeit je Fahrzeug, Teleport-Erkennung, Sprunghöhen, Schussrate; Verstöße zählen auf ein Konto-Risiko-Konto.
4. **Rate-Limits und Größenlimits** je RPC und Verbindung; Validierung aller Parameter (Typ, Bereich); `any_peer`-RPCs nur dort, wo nötig.
5. **Authentifizierung:** kurzlebige, signierte Tokens; Sitzungsbindung; keine Geheimnisse im Client/PCK (PCK-Verschlüsselung erschwert nur das Auslesen, bietet keine echte Sicherheit).
6. **Deterministische Story-Prüfung:** Missions-Bedingungen (6.5) sind deklarativ → Server wertet sie selbst aus.
7. **Protokolle und Replays:** Eingabeprotokolle in Ringpuffer; auffällige Sitzungen nachträglich analysierbar; Melde-Funktion und Moderationswerkzeuge.
8. **Ehrlichkeit nach außen:** Kein Client-seitiger „Anti-Cheat“ als Sicherheitsversprechen; Schutz vor Bots über Verhaltensanalyse und Rate-Limits. Offline-Charaktere sind **nicht** in den Online-Modus übertragbar (Cheat-Risiko), Ausnahme: reine Story-Flags ohne Geld/Fahrzeuge.

### 7.8 Einzelspieler-Story und gemeinsame Welt zusammenbringen

Drei Modi, ein Spiel:

| Modus | Welt | Story | Persistenz |
|---|---|---|---|
| **Offline** | Lokale Simulation (`OfflineMultiplayerPeer`) | Vollständig, einziger Spieler | JSON-Speicher lokal |
| **Online – Story** | Geteilter Realm | **Pro Spieler privat (Phasen):** Story-NPCs, Missionsobjekte, Entscheidungen existieren nur für ihn/seine Gruppe; Mitspieler sehen nur die gemeinsame Welt | Server (Charakter) |
| **Online – Freispiel** | Geteilter Realm | Keine Hauptstory; Nebenaufträge, Fraktionskonflikte, Rennen, Events | Server |

**Verzahnung:**
- **Persönliche Entscheidungen bleiben persönlich.** Ein Spieler, der E3 „Blackout“ erreicht, verändert nicht den Realm für alle (keine globalen Spoiler, keine „verbrannte“ Welt).
- **Gemeinsame Welt reagiert langsam auf Gemeinschaftsverhalten:** Aggregierte Spieleraktionen (Kameras gehackt, Docks beschützt, Straßenrennen) verschieben die **Fraktionskontrolle der Bezirke** im Realm (Zähler mit Zerfall, z. B. wöchentlich bilanziert) – eine dystopische Metaspiel-Ebene.
- **Weltzustand-Tags (6.6)** gibt es zweimal: *persönliche* (Phase) und *Realm-weite* (Gemeinschaft); persönliche überlagern für den Spieler.
- **Gruppen:** Eine Gruppe (2–4) teilt eine Missionsphase; Entscheidungen per Abstimmung oder Gruppenführer.
- **Aegis als Mehrspielermechanik:** Spieler können Kameras hacken (zeitlich begrenzt für alle im Umkreis sichtbar), KCG-Einsätze auslösen oder abfangen; Heat gilt je Bezirk **pro Spieler**.

### 7.9 Stufen, Technik, Risiken

| Stufe | Umfang |
|---|---|
| MVP (MS9) | Lokaler/Online-Pfad über dieselbe API (`OfflineMultiplayerPeer` ↔ `ENetMultiplayerPeer`); Dedicated Server (`--headless`) mit 1 Realm, 1 Zone; einfache Anmeldung; Prädiktion/Interpolation für Fahrzeuge; Charakter-Persistenz über Backend; 2 Clients fahren gleichzeitig sichtbar |
| Ausbau (MS10) | Mehrere Zonen mit Handoff; Interessenverwaltung; serverseitiger Ambient-Verkehr in Spielernähe; Polizei-Dispatcher serverseitig; Anti-Cheat-Plausibilitätsprüfung; Lasttest mit simulierten 50–100 Bots; Chat; Moderationswerkzeug |
| Wunschziel (MS11) | Mehrere Realms, Matchmaking, Fraktionskontrolle der Bezirke, Events/Saisons, Gruppen mit gemeinsamer Story-Phase, Replays, Telemetrie-Dashboard |

**Abhängigkeiten:** Setzt eine saubere Trennung von Sim und Darstellung (M1–M12), ein serialisierbares `GameState` (M8) und die Story-Engine (M14) voraus. **Risiken:** (1) **Betrieb und Kosten** → klein starten, ein Realm, Kosten vorab kalkulieren; (2) **Latenz/Fahrzeugsynchronisation** bei hoher Geschwindigkeit und Physikfahrzeugen → früh prototypen; (3) **Zonenübergabe** komplex → anfangs eine Zone; (4) **Cheater** → Serverautorität von Beginn an; (5) **DSGVO/Jugendschutz/Moderation** → vor Öffnung klären; (6) **Doppelter Content-Pfad** → ein gemeinsamer Daten- und Codepfad; (7) **Deterministik:** Godot-Physik ist nicht garantiert deterministisch zwischen Clients/Server → Server-Autorität statt Lockstep. **Aufwand:** MVP 10 PT · Ausbau 22 PT · Wunsch 35+ PT (ohne Betrieb).

---

## 8. Die Welt: Kestrel Bay (dystopisch)

**Kestrel Bay** ist eine fiktive, dystopische Hafenmetropole an einer halbmondförmigen Bucht: Der Konzern *Aurelia Holdings* hält die Innenstadt und das Überwachungsnetz **Aegis**, an den Rändern leben Hafenarbeiter, Schrotthändler und Aktivisten in „blinden“ Vierteln. Die Stadt ist komplett erfunden; Namen, Fraktionen und Orte stammen aus keinem bestehenden Spiel. Das Meer liegt im Westen, im Osten steigen die **Ashridge-Berge** auf; die Stadt zieht sich vom Strand über Industrie und Zentrum bis in die Hanglagen. Größe: **Stadtgebiet ca. 3 × 3 km**, Umland insgesamt ca. 6 × 6 km (mit Chunk-Streaming in Godot machbar und unterhalb der Einzelpräzisions-Orientierungsgröße von ca. 8 km [Q26]; der Prototyp hat 0,64 × 0,64 km). Fahrzeit durch die Stadt bei Höchsttempo ca. 2 Minuten, Rundfahrt über Umland ca. 8–10 Minuten.

### 8.1 Karte (schematisch)

```
 N ↑
 ┌──────────────────────────────────────────────────────────┐
 │ ~~ MEER ~~     Ashridge-Berge (Serpentinen, Windpark)    │
 │ ~~ ┌─────────┐  ┌────────────┐          ▲▲▲  Aussichtsp. │
 │ ~~ │ SUNSTRIP│  │ LANTERN    │      ▲▲▲▲▲                │
 │ ~~ │ Strand/ │  │ HILL       │  ┌───────────┐            │
 │ ~~ │ Pier    │  │ Altstadt   │  │ GREENMOOR │            │
 │ ~~ └─────────┘  └────────────┘  │ Vorstadt  │            │
 │ ~~ ┌─────────┐  ┌────────────┐  └───────────┘            │
 │ ~~ │SALTMARKT│  │ KESTREL    │      ║ Route 9 (Highway)  │
 │ ~~ │ Hafen   │  │ CORE       │══════╝ ═════════ → Umland │
 │ ~~ └─────────┘  │ Hochhäuser │                           │
 │ ~~ ┌──────────────────────────┐                          │
 │ ~~ │ RUSTLINE: Industrie/Bahn │  Salzebene, Raffinerie   │
 │ ~~ └──────────────────────────┘                          │
 └──────────────────────────────────────────────────────────┘
```

### 8.2 Die sechs Bezirke – dystopisch

Leitgedanke: Kestrel Bay ist **vertikal und sozial geteilt**. Je näher am Konzernturm, desto sauberer, heller und überwachter; je weiter weg, desto verfallener, dunkler und „blind“ – aber freier. Jeder Bezirk hat eine Rolle im Machtgefüge, eine eigene Optik und eine eigene Spielregel.

| # | Bezirk | Rolle in der Dystopie | Optik und Straßenform | Landmarke | Überwachung und Macht (Spielregel) | Verkehr / Passanten | Klang |
|---|---|---|---|---|---|---|---|
| 1 | **Kestrel Core** (Zentrum, Konzernviertel) | Sitz von Aurelia Holdings; Fassade von Ordnung und Wohlstand; Zugang nur mit „sauberem“ Profil | Hochhäuser 60–180 m, Glas/Stahl, Stahlblau/Weiß, makellos; orthogonales Raster, breite Alleen, Drohnen am Himmel | **Aurelia Tower** (190 m, LED-Krone, in der Stadt allgegenwärtig) | **Aegis maximal:** Kamera-Dichte hoch, Kennzeichen-Treffer sofort, KCG-Streifen dicht; Sperrzonen für niedriges Profil | Dichter Tagesverkehr (Limousinen, Taxis), Anzugträger; nachts leer | Gedämpftes Rauschen, Drohnenbrummen, Lautsprecher-Durchsagen |
| 2 | **Saltmarket** (alter Hafen) | Arbeiterviertel, Hafenlogistik; Union gegen Konzern; Streikgefahr | Container, Kräne, Kopfsteinpflaster, rostiges Orange/Grün; unregelmäßige Straßen, Sackgassen, Kais | **Die Kranreihe** (drei rote Hafenkräne mit Warnlichtern) | Mittel: Kameras nur an Toren; Union kontrolliert Schichtwechsel; Schmuggelrouten | LKW, Gabelstapler, Frühschicht-Arbeiter | Schiffshörner, Möwen, Metall auf Metall |
| 3 | **Lantern Hill** (Altstadt am Hang) | Widerstandsnest und Nachtleben; vom Konzern als „Sanierungsgebiet“ vorgesehen | 3–6 Geschosse, Terrakotta/Ocker, Balkone, Treppen; enge, kurvige Gassen am Hang | **Glockenturm St. Merrin** + beleuchtete Treppe | **Aegis lückenhaft:** viele Kameras gehackt/verdeckt; Collective betreibt Untergrundnetz; Abrisskommandos als Gefahr | Fußgängerreich, Roller, Lieferwagen | Bars, Musik, Kirchenglocke |
| 4 | **Greenmoor** (Gated Vorstadt) | Wohlhabende Enklave hinter Zäunen; privat finanzierte KCG-Patrouillen; Angst vor „den anderen“ | Villen, Hecken, Zäune, Schranken, Pastell, Golfplatz; geschwungene Straßen, Cul-de-Sacs | **Country-Club-Wasserturm** | **Aegis privat:** Kennzeichen-Leser an Toren; Fremde werden sofort gemeldet; Polizei reagiert schnell | Wenig Verkehr, Jogger, Gärtner, Lieferdrohnen | Sprinkler, Vögel, Rasenmäher |
| 5 | **Rustline** (Industrie / Schrott) | Vergessener Gürtel; Garagenszene, Straßenrennen, illegale Werkstätten; Umweltverschmutzung | Fabrikhallen, Schornsteine, Gleise, Schrott, Natriumlicht (Orange); große Blöcke, Bahnübergänge | **Raffinerie** mit Flammenfackel | **Aegis fast blind:** wenige Kameras, KCG meidet das Gebiet; Fluchtraum, aber kaum Schutz | LKW, Pendlerbusse, nachts Rennen | Maschinen, Pressluft, Zugpfeifen |
| 6 | **Sunstrip** (Promenade / Strand) | Fassade der Freizeit; Touristen und Influencer, darunter Armut im Hinterland; Werbung allgegenwärtig | Palmen, Neon, Promenade, Pier, Pastell; küstenparallele Boulevards und Fußgängerzonen | **Kestrel Wheel** (Riesenrad am Pier) | **Aegis als Werbung:** Gesichtserkennung für personalisierte Anzeigen, viele Kameras an Hotspots; lockeres Polizeiverhalten, strenge Wegweisung | Cabrios, Skater, dichte Abendmengen | Brandung, Synth-Musik, Jahrmarkt |

**Soziale Gegensätze (sichtbar machen):** Gehwegzustand, Beleuchtung, Sauberkeit, Werbung vs. Graffiti, Müll, Barrikaden und die **Kameradichte** unterscheiden Bezirke (Kapitel 5.5). Wer sich in Core mit hohem Aegis-Profil bewegt, wird verfolgt; in Rustline ist man unsichtbar – aber ohne Hilfe.

**Wiedererkennungsregeln (Designdisziplin):** Jeder Bezirk hat eine **Leitfarbe** (Core: Stahlblau, Saltmarket: Rostorange, Lantern Hill: Ocker/Warmgelb, Greenmoor: Grün/Pastell, Rustline: Orange-Grau/Natrium, Sunstrip: Türkis/Pink), eine **Silhouettenform** am Horizont, ein **eigenes Straßenmuster** und eine **Aegis-Dichte** als spielbare Eigenschaft. Gewünschtes Ergebnis: Ein Screenshot ohne HUD reicht, um den Bezirk zu benennen.

### 8.2a Dystopie-Mechaniken (Kurzüberblick)

| Mechanik | Beschreibung | Module |
|---|---|---|
| **Aegis-Kameras** | Spielobjekte: erfassen Kennzeichen/Gesicht im Sichtkegel, erhöhen Aegis-Profil bei Verbrechen; hackbar (Minispiel), blendbar, zerstörbar | M6, M13, M14 |
| **Kennzeichen-System** | Kennzeichen tauschen, fälschen, löschen (Fraktionsdienst); gestohlene Fahrzeuge „heiß“ | M4, M6 |
| **Sperrzonen und Checkpoints** | Core und Greenmoor kontrollieren niedrige Profile; Zugang über Schranken, Dienstleistungen oder Umgehung | M5, M6 |
| **Aegis „Phase Zwei“** | Story-Countdown; verändert Welt: neue Kameras, Straßensperren, Score-Anzeige für NPCs | M14 |
| **Soziale Zonen** | Bezirke haben Zugangsregeln; Reichtum/Profil bestimmt, wo man nicht auffällt | M3, M6 |
| **Wirtschaft der Ungleichheit** | Preise und Löhne unterscheiden sich je Bezirk; Schwarzmarkt in Rustline | M8 |

### 8.3 Umland

| Bereich | Beschreibung | Spielerischer Zweck |
|---|---|---|
| **Küstenstraße „Coast Road“** | 12 km Serpentinenstraße am Wasser, Klippen, Tunnel, Strandabschnitte | Genussfahrt, Rennen, Flucht |
| **Ashridge-Berge** | Gebirgsstraße mit Haarnadelkurven, Aussichtspunkt mit Stadtpanorama, Windpark, **Aegis-Uplink-Sendemast** (Finale) | Offroad-/Driftgebiet, Schlussszenen, Panorama |
| **Route 9 (Highway)** | 6-spurige Schnellstraße vom Zentrum ostwärts, Brücken, Anschlussstellen, **Aegis-Checkpoint mit Kennzeichenerfassung** (Stadteingang, Prolog) | Hochgeschwindigkeits-Verfolgungen, Verkehrsfluss, Kennzeichen-Mechanik |
| **Salzebene** | Flache, weiße Ebene südlich von Rustline, Salzpfannen | Freies Fahren, Rennstrecken, Verstecke |
| **Bucht & Pier** | Wasserfläche mit Booten, Pier, Leuchtturm | Landmarke, später Boote/Wasserflucht |
| **Tunnel „Ashridge Tube“** | 1,4 km Straßentunnel durch den Berg, Lichtwechsel, Echo | Fluchtweg (Sichtlinie bricht ab), Audio-Showpiece |

**Weltgrenzen:** Statt unsichtbarer Wand → Meer im Westen, Bergkamm im Osten/Norden, Salzebene mit Sperrgebiet im Süden; wer weiterfährt, bekommt eine erzählerische Umkehr-Aufforderung (Sperre, Sandsturm).

### 8.4 Orientierung ohne Karte

Wie in [Q7] beschrieben: Landmarken sind von vielen Punkten sichtbar und geben ein Ziel; eine **Hierarchie** (Aurelia Tower > Kranreihe/Riesenrad/Raffinerie > Bezirksmarken) verhindert Beliebigkeit; Straßen laufen als **Leitlinien** auf Landmarken zu. Nachts leuchten Landmarken stärker (LED-Krone, Fackel, Riesenrad), damit die Orientierung zu jeder Tageszeit trägt.

---

## 9. Design-Vorbilder: was wir übernehmen und wie wir es eigenständig umsetzen

Drei Spielgenres liefern bewährte **Designprinzipien**, an denen sich MTA 6 orientiert:

| Vorbild-Genre | Stärken, die wir als Prinzip übernehmen |
|---|---|
| **Story-getriebenes Rollenspiel (düstere Fantasy)** | Quest-Writing mit moralisch grauen Entscheidungen und echten, verzögerten Konsequenzen; Nebenquests als eigene kleine Geschichten; Auftragsjagd mit Vorbereitung (Informationen sammeln, ausrüsten, stellen) |
| **Dystopisches Cyberpunk-Rollenspiel** | Dichte, vertikale, neonbeleuchtete Stadt; Konzernmacht; Implantate/Fähigkeiten; Herkunft/Lebensphase der Spielfigur; Nachtleben; kurze Aufträge („Gigs“) |
| **Systemisches Open-World-Fahrspiel** | Lebendige Systemik, Fahren und Verfolgungen, Humor/Satire, Radio, Aktivitäten, freies Chaos |

**Abgrenzung (verbindlich):** Übernommen werden **nur allgemeine Gestaltungsprinzipien**. Es werden **keine** Inhalte, Namen, Figuren, Orte, Story-Elemente, Marken, Musik oder Regelwerke aus diesen oder anderen Spielen verwendet. Jede Mechanik unten ist als eigenes Design für Kestrel Bay formuliert; Namen (Sender, Marken, Fraktionen) sind frei erfunden. Vor Release ist jede Mechanik einmal gegen den Satz zu prüfen: *„Ließe sich das ohne Kenntnis des Vorbilds so aus unserer Welt herleiten?“*

### 9.1 Quest-Struktur (Modul M7/M14)

Vier Auftragsklassen mit unterschiedlicher Größe und Qualitätsregel:

| Klasse | Umfang | Eigenschaften | Beispiel (eigen) |
|---|---|---|---|
| **Hauptquest** | 25–32 Missionen (Kapitel 6.4) | Perlenkette, Wendepunkte W1–W5, Fraktions- und Enden-Relevanz | „Nullstunde“ |
| **Nebenquest („Geschichte“)** | 3–5 Beats, 15–25 min, ca. 12 zum Release | Eigene Figuren und Bogen, **unabhängig** von der Hauptquest lesbar, mindestens eine Entscheidung ohne „richtige“ Antwort, eine **verzögerte Folge** in der Welt | Eine Hafenarbeiterin bittet um Fahrten zu einer unbekannten Adresse; am Ziel entscheidet der Spieler, wessen Geheimnis wichtiger ist |
| **Gig** | 3–8 min, systemisch erzeugt, wiederholbar | Template + Parameter (9.4) | Kurierfahrt mit Zeitfenster und Kameraroute |
| **Vertrag („Jagd“)** | 20–40 min, ca. 6 zum Release | Vorbereitung → Jagd → Entscheidung (9.5) | Eine Schmugglerin aufspüren, die ihre Routen täglich wechselt |

**Quest-Anatomie (Writing-Regeln):** *Hook* (eine Frage, kein Marker-Spam) → *Ermittlung* (Spuren in der Welt: Zeugen, Kamera-Archive, Radiomeldungen) → *Entscheidung* (beide Seiten haben Kosten) → *Folge* (sofort kleine, verzögert große Wirkung). Jede Nebenquest enthält Thema, Gegenspiel, Überraschung und eine Folge, die der Spieler **später ungefragt in der Welt sieht** (verändertes Schaufenster, neue Figur im Café, Meldung im Radio).
**Keine Fragezeichen-Karte:** Aufträge werden über Gespräche, Telefon, Aushänge und das Radio gefunden; die Karte zeigt nur angenommene Aufträge und bekannte Kontaktpunkte.

**Datenmodell-Erweiterung (zu 6.5):**
```text
Quest = { id, class: 'main'|'side'|'gig'|'contract', beats: [Beat], tone: ['grim','satire'],
          district: 'saltmarket', giver: 'npc_id', requires: [Cond] }
Beat  = { id, steps: [Step], choices: [Choice], onEnter: [Effect] }
Effect += { delay: { days: 3 }, effect: Effect, announce: 'radio'|'npc'|'none' }   // verzögerte Folge
Choice += { weight: 'light'|'medium'|'heavy', costs: [Effect], witnesses: ['npc_id'] }
```

### 9.2 Entscheidungsgewicht (M14)

- **Gewichtsklassen:** *leicht* (Dialogton, kleine Standing-Änderung ±2–5), *mittel* (Standing ±10–20, Tag im Bezirk, Folgeauftrag gesperrt/geöffnet), *schwer* (Figur stirbt oder geht, Bezirk verändert sich sichtbar, Hauptquest-Variante). **Höchstens ca. 8 schwere Entscheidungen** in der Kampagne, damit jede zählt.
- **Beide Optionen kosten etwas.** Kein „guter“ Ausgang ohne Preis; Entscheidungen werden **nicht** als „gut/böse“ markiert (nur Taten-Flags, Kapitel 6.3).
- **Verzögerte Folgen:** Wirkung erst nach Spielzeit-Tagen (Effekt mit `delay`), angekündigt über Radio, Gespräche oder Veränderungen im Bezirk – nie über einen Pop-up „Entscheidung gespeichert“.
- **Zeugen:** Wer die Tat gesehen hat (`witnesses`), erzählt davon; Ruf verbreitet sich über Figuren und Radio, nicht über einen globalen Zähler.
- **Selten Zeitdruck:** Entscheidungs-Timer nur bei 1–2 Schlüsselszenen.
- **Rückblick:** Entscheidungsprotokoll im Menü („Was wäre gewesen“ erst nach dem Ende, um Spoiler zu vermeiden).
- **Test:** Der Story-Simulator (6.6) prüft zusätzlich: jede schwere Entscheidung führt in eine **spielbare** Folge; verzögerte Effekte feuern auch nach Laden/Speichern.

### 9.3 Charakterfortschritt: Herkunft, Skills, Module (M8/M14)

**Herkunft („Lebensphase“) – drei Startwege, im Prolog gewählt:**

| Herkunft | Vergangenheit | Startvorteil | Eigene Dialogoptionen/Prolog |
|---|---|---|---|
| **Rennfahrerin** | Streckenfahrerin aus dem Umland | Fahrzeugbeherrschung, Garage in Rustline | Kontakte bei den Wrenchers, Ruf als „schnell“ |
| **Hafenkind** | Aufgewachsen in Saltmarket | Union-Standing +20, Zugang zu Schmuggelrouten | Hafen-Slang, Vertrauen der Arbeiter |
| **Konzern-Aussteigerin** | Ex-Angestellte von Aurelia | Aegis-Kenntnis (Kamera-Hack leichter), Konzern-Standing +10, aber Aegis-Profil +20 | Konzern-Jargon, KCG kennt sie |

Die Herkunft setzt Startwerte, Dialogvarianten und eine eigene Prolog-Szene (Variante derselben Perlenketten-Mission, kein Parallel-Content). Sie ist im `GameStory` als `origin` gespeichert.

**Fähigkeiten (nutzungsbasiert, kein XP-Grind):** *Fahren, Technik/Hacking, Straße (Überzeugen/Einschüchtern), Tarnung.* Jede steigt durch Anwenden (Drift-Distanz, gehackte Kameras, gelungene Dialoge). Fähigkeiten öffnen **Lösungswege** in Quests (Skill-Checks mit Alternativen, nie Sackgassen).

**Module (Implantate/Upgrades, eigenes Konzept):** Spieler- und Fahrzeug-Upgrades mit **Preis und Nebenwirkung**:
- *Kennzeichen-Störer* (Fahrzeug): senkt Aegis-Erkennung, kostet Wartung, im Core illegal.
- *Kamera-Hack-Modul* (Körper): erlaubt Hacks aus der Distanz; Konzern-Lizenz → Aegis kann **orten**, wenn Spieler es zu oft nutzt.
- *Fokus* (Körper): kurze Zeitlupe beim Fahren; belastet „Systemrisiko“ (Konsequenzen: Zittern/Blur-Effekt im HUD).
- Einbau nur bei Fraktions-Händlern (Rustline: billig und riskant, Core: teuer und lizenziert) – Fraktionsstand beeinflusst Auswahl und Preis.

**Technik:** Skills/Module als Daten (`data/skills/*.tres`, `data/modules/*.tres`) und Teil von `GameState`; Wirkungen als Modifikatoren auf M4/M6/M5-Parameter (Server prüft, Kapitel 7.7).

### 9.4 Gigs – systemische Kleinaufträge (M7/M8)

- **Vermittler pro Bezirk** (Telefon, Terminals, Stammkunden, Radio-Aushang); jeder Bezirk bietet andere Gig-Typen (Saltmarket: Lieferfahrten; Sunstrip: Taxi/Fotojobs; Rustline: Rennen/Abschleppen; Core: Kurier mit Kameraroute; Lantern Hill: Kamera-Sabotage).
- **Generator aus Templates:** `GigTemplate` = Typ, Orte (aus `CityData`), Zeitfenster (Tageszeit), Risiko, Belohnung, Fraktion. Parameter ziehen Seed + Weltzustand (z. B. nach Streik mehr Hafenlieferungen).
- **Fraktions-Standing schaltet Stufen frei** (Stufe 1 Standard → Stufe 3 gefährlich, hohe Belohnung); **Abkühlzeit** gegen Wiederholung; **Gig-Ketten** („Stammkunde“) können zu einer Nebenquest werden.
- **Ziel:** Es gibt immer etwas Sinnvolles in der Nähe, ohne Content-Fließband: maximal 3 gleichzeitig angebotene Gigs je Bezirk.

### 9.5 Verträge: Jagd mit Vorbereitung (M5/M6/M7)

Eine eigene Auftragsklasse in vier Phasen:
1. **Ermittlung:** Das Ziel (Person/Fahrzeug) hat ein **Profil** (Tagesplan, bevorzugte Route, Schwäche). Spieler sammelt Hinweise: Zeugen befragen, Kamera-Archiv hacken (Technik), Gigs bei Informanten.
2. **Vorbereitung:** Fahrzeug anpassen (schneller/robuster/leiser), Route blockieren lassen (Fraktions-Gefallen), Kameras manipulieren, Ausrüstung/Module wählen. Gute Vorbereitung verändert die Jagd (z. B. weniger Polizei im Zielbezirk).
3. **Jagd:** Verfolgung im systemischen Verkehr; das Ziel nutzt Routinen (M5-Tagesplan) und reagiert auf Eskalation (flieht, ruft Verstärkung).
4. **Entscheidung:** stellen und übergeben / laufen lassen / gegen Bezahlung verkaufen; Konsequenzen in Standing und Bezirk (9.2).

**Technik:** Zielprofil als Daten (`ContractTarget`), Tagesplan wiederverwendet die Agentenlogik aus M5; Jagd-Phase ist ein Zustandsautomat im `MissionRunner`.

### 9.6 Radio und Sound (M10)

- **Sendernetz (fiktiv):** 5 Sender mit eigener Identität: *Clearwave FM* (glatter Konzern-Pop), *Lantern Pirate Radio* (Untergrund, Talk und Elektronik), *Dockline* (Hafen, Arbeiter-Talk und Folk), *Neon Tide FM* (Synth/Nacht, Sunstrip), *Open Line* (Nachrichten und Hörerrufe). Namen frei erfunden und änderbar.
- **Reaktives Radio:** Nachrichten und Moderatoren **reagieren auf Spielerhandlungen und Weltzustand** (Weltzustand-Tags, Heat-Ereignisse, Entscheidungsfolgen: „Streik im Hafen geht in die dritte Woche“). Das ist das wichtigste Werkzeug für verzögerte Folgen (9.2) und Satire (9.8).
- **Werbung und Jingles:** Satirische Werbespots für Konzernprodukte (9.8) zwischen den Songs; Verkehrsfunk mit realen Ereignissen aus M5.
- **Musik:** eigene, prozedurale oder **lizenzfreie/CC0-Musik**; keine fremden Lizenzmusiktitel ohne Rechteklärung; Lizenzliste `assets/LICENSES.md`.
- **Technik in Godot:** `AudioStreamPlayer` mit **Playlist-Ressource** (`RadioStation`: Tracks, Jingles, Nachrichtenpool, Zeitplan); eigener **Radio-Bus** mit Tiefpass außerhalb des Fahrzeugs (Lautsprecher im Café/Club als 3D-Quelle); Nachrichtenpool als Daten mit Bedingungen (`when: [Cond]`, wie 6.5); Streaming-Dateien (OGG), nicht alles im Speicher.

### 9.7 Bezirksleben, Nachtleben, Aktivitäten und vertikale Stadt (M1/M3/M13)

- **Dichte statt Fläche:** Kestrel Bay wirkt durch **Verdichtung und Vertikalität**: Hochstraßen auf zwei Ebenen im Core, Skybridges, Dachwege, Parkhäuser, Hanggassen in Lantern Hill. Technisch: Straßengraph mit **Ebenenattribut** (Boden/Hochstraße), Rampen als Kanten; MVP nur **eine** zusätzliche Ebene im Core.
- **Neonbeleuchtete Nacht:** Reklame als Leuchtkörper (Emissive + Glow, wenige echte Lichter), holografische Werbetafeln als Shader, nasse Straßen (M13). Konzern-Werbung dominiert Core/Sunstrip, Graffiti und Plakate Lantern Hill/Rustline.
- **Nachtleben:** Clubs und Bars (Lantern Hill, Sunstrip) als Orte mit Gigs, Informanten und Radio-Quellen; Nachtmärkte und Straßenküchen mit Ambient-Dialogen; Ausgehzeiten sind Teil des Bezirksrhythmus (M3).
- **Aktivitäten (ohne Pflicht):** Straßenrennen (Rustline), Drift-Zonen, **Kamera-Slalom** (durch Aegis-Blindzonen fahren), Hafen-Stunts, **Foto-Jobs** (Fotomodus), Sammelaufgaben („Kameras überkleben“ als Graffiti-Tags), Lieferketten. Jede Aktivität belohnt Standing/Geld und ist in der Weltzustand-Logik verankert.
- **Systemisches „freies Chaos“:** zerstörbare Straßenprops, Verkehrsreaktionen (Ausweichen, Hupen, Flucht), Eskalationsleiter der Polizei (M6), Medien-Hubschrauber/Live-Berichte im Radio, Verkehrssperren; Chaos wird nicht bestraft, sondern erzeugt **Folgen** (Bezirks-Heat, Radio-Meldungen, Preise).

### 9.8 Satire-Ton (Writing, UI, Radio)

- **Ton-Regel:** Die Welt ist **ernst**, die Medien sind **satirisch**. Humor kommt aus Konzernsprache, Werbung und Überwachungs-Bürokratie – nicht auf Kosten realer Gruppen oder Personen.
- **Humorkanäle:** Radiowerbung, Plakate, Lautsprecherdurchsagen im Core, Zeitungs-/Nachrichtenmeldungen, NPC-Ambient-Dialoge (Pool aus 200+ Sätzen je Bezirk), Gerätenamen und Menütexte.
- **Beispiele (eigene Fiktion):** „Aegis – Wir sehen, was Sie brauchen.“ · „Aurelia Calm™: Ihre Stadt, optimiert.“ · Durchsage im Core: „Ihr Verhalten wurde freundlich bewertet.“
- **Leitplanken:** Keine echten Marken/Personen; Altersfreigabe und Inhaltshinweis festlegen; Satire im Ton einheitlich halten (Textleitfaden `docs/TONFALL.md` anlegen, sobald geschrieben wird).

### 9.9 Zuordnung zu Modulen und Meilensteinen

| Idee | Modul | Meilenstein | Umfang (MVP / Ausbau) |
|---|---|---|---|
| Quest-Klassen, Beat-Modell, verzögerte Folgen, Entscheidungsgewicht | M7, M14 | MS6 / MS8 | Daten + Engine / 12 Nebenquests |
| Herkunft (3 Startwege), Skills (4), Module | M8, M14 | MS6 / MS8 | 3 Herkünfte, 3 Skills / 4 Skills, 6 Module |
| Gig-System (Templates, Vermittler, Stufen) | M7, M8 | MS6 / MS8 | 3 Gig-Typen / 8 Typen, Ketten |
| Verträge (Jagd mit Vorbereitung) | M5, M6, M7 | MS8 | 1 Prototyp-Vertrag (MS6) / 6 Verträge |
| Reaktives Radio, Werbung, Sendernetz | M10, M14 | MS7 / MS8 | 3 Sender / 5 Sender |
| Vertikalität (Hochstraße Core), Neon-Look | M1, M13 | MS3 / MS8 | 1 Ebene im Core / mehrere Ebenen, Skybridges |
| Nachtleben-Orte, Aktivitäten | M3, M7 | MS7 / MS8 | 2 Aktivitäten / 6 Aktivitäten |
| Chaos-Systemik (zerstörbare Props, Eskalation) | M4, M5, M6 | MS5 | Props + 3 Eskalationsstufen |
| Satire-Content (Werbung, Durchsagen, Ambient-Dialoge) | M10, M11, M13 | MS7 / MS8 | 20 Spots, 60 Sätze / 60 Spots, 200 Sätze je Bezirk |

**Zusatzaufwand durch diese Ideen (PT, Planwerte, ohne Texte/Vertonung):**

| Posten | MVP | Ausbau | Wunsch |
|---|---|---|---|
| Quest-Klassen, verzögerte Folgen, Entscheidungsgewicht | 3 | 8 | 10+ |
| Herkunft, Skills, Module | 4 | 10 | 15+ |
| Gig-System | 4 | 8 | 10+ |
| Verträge/Jagd | 3 | 8 | 10+ |
| Radio und Sendernetz | 3 | 8 | 10+ |
| Vertikalität (Hochstraße) | 3 | 8 | 10+ |
| Aktivitäten, Satire-Systeme | 2 | 8 | 10+ |
| **Summe** | **22** | **58** | **75+** |

Die konsolidierte Gesamtschätzung steht in Kapitel 13.4. Die Ideen sind **entkoppelt** planbar: Jede lässt sich streichen, ohne dass Grundmodule brechen; empfohlene Streichreihenfolge bei Zeitnot: Aktivitäten → Verträge → zweite Ebene → Satire-Ausbau → Module.

**Risiken:** (1) Umfangsexplosion → harte Obergrenzen je Klasse (12 Nebenquests, 6 Verträge, 8 Gig-Typen), Content-Wiederverwendung; (2) Ton kippt (Satire untergräbt die Dystopie) → Textleitfaden und Lektorat; (3) Rechte → keine fremden Marken/Musik, Lizenzliste; (4) Vertikalität erschwert Navigation, Kamera und Streaming → klein starten (eine Ebene); (5) Systemisches Chaos kollidiert mit Skript-Missionen → Fail-Safes (6.7).

---

## 10. Missionstypen und Aktivitäten

Alle Aufträge sind **Vorlagen (Templates) + Parameter**, ausgeführt vom `MissionRunner` (M7). Dadurch lassen sich Missionen, Gigs und Aktivitäten aus Daten bauen, statt Code zu schreiben. Belohnungen folgen einer gemeinsamen Formel (10.3), damit Balancing zentral möglich ist.

### 10.1 Missionstypen

| Typ | Kurzbeschreibung | Pflichtparameter | Optionale Parameter | Fehlschlag | Module |
|---|---|---|---|---|---|
| **Lieferung** | Fracht/Person von A nach B | `from`, `to`, `cargo`, `time_limit` | `fragile` (Schaden zählt), `route_hint`, `checkpoints`, `heat_start` | Zeit abgelaufen, Fracht zerstört | M5, M6, M11 |
| **Taxi/Passagier** | Passagier mit Wünschen (schnell/ruhig/leise) | `pickup`, `dropoff`, `mood` | `detours`, `tip_rules` | Passagier verlässt Fahrzeug, Beschwerde | M5, M8 |
| **Verfolgung** | Ziel jagen (Fahrzeug/Person) | `target`, `route_profile`, `escape_skill` | `aggression`, `reinforcements`, `capture_mode` (rammen/blockieren/stoppen) | Ziel entkommt, Zeit | M4, M5 |
| **Flucht** | Verfolger abschütteln | `heat_level`, `pursuers`, `safe_zone` | `time_limit`, `hide_spots`, `weather` | Festnahme, Totalschaden | M6 |
| **Rennen** | Strecke mit Gegnern | `route`, `opponents`, `laps`, `stake` | `weather`, `time_of_day`, `traffic_density`, `rules` (Rammen erlaubt) | Letzter Platz, Disqualifikation | M4, M9 |
| **Auftragsjagd (Vertrag)** | Vier Phasen Ermittlung–Vorbereitung–Jagd–Entscheidung (9.5) | `target_profile`, `investigation_nodes`, `prep_slots` | `heat_cap`, `bonus_objectives` | Ziel entkommt, Zeugen verlieren | M5, M6, M7 |
| **Gig** | Kurzer, systemisch generierter Auftrag (9.4) | `template`, `district`, `faction`, `tier` | `cooldown`, `chain_id` | Abbruch, Zeitablauf | M7, M8 |
| **Nebenquest** | Erzählte Kurzgeschichte mit Entscheidungen (9.1) | `beats`, `giver`, `district` | `requires`, `delayed_effects` | Beat-spezifisch | M14 |
| **Bezirks-Event** | Weltereignis mit zeitlichem Fenster | `district`, `trigger`, `duration` | `participation_rewards`, `world_tags` | Event läuft aus (Folgen trotzdem) | M3, M9, M14 |
| **Hauptmission** | Kapitel 6.4/6.10 | wie Nebenquest + `main: true`, `act` | Cutscene/Dialog-IDs | Checkpoint-Neustart | M14 |

**Beispiel-Parameter (Lieferung):**
```text
{ type: 'delivery', id: 'a1_frueschicht', from: 'saltmarket_dock3', to: 'core_tower_plaza',
  cargo: { kind: 'crate', fragile: false, mass: 400 }, time_limit: 360,
  checkpoints: ['core_checkpoint_n'], heat_start: 0,
  reward: { money: 400, standing: { union: 10 } },
  fail: { standing: { union: -5 }, money: 0 } }
```
**Beispiel-Parameter (Rennen):**
```text
{ type: 'race', id: 'c1_probefahrt', route: 'rustline_loop_a', laps: 2, opponents: 3,
  weather: 'overcast', time_of_day: 'dusk', traffic_density: 0.3, rules: { ramming: true },
  stake: 100, reward: { money: 300, standing: { wrenchers: 10 }, rank_bonus: [1.0, 0.5, 0.25] } }
```

### 10.2 Aktivitäten (optional, ohne Story)

| Aktivität | Bezirk | Ablauf | Belohnung | Besonderheit |
|---|---|---|---|---|
| **Straßenrennen** | Rustline | Nachts, Einsatz mit Gegnern, Streckenwahl | Geld, Standing Wrenchers, Fahrzeugteile | Polizeirisiko je Stufe |
| **Drift-Zonen** | Rustline, Salzebene | Punkte für Driftlänge/Winkel | Geld, Ranglisten (lokal) | Fahrmodell-Prüfstein (M4) |
| **Kamera-Slalom** | Core, Lantern Hill | Strecke durch Aegis-Blindzonen, ohne erfasst zu werden | Aegis-Profil ↓, Modul-Teile | Verbindet Dystopie und Fahren |
| **Foto-Jobs** | Sunstrip, Core | Foto von Motiv/Ereignis machen | Geld, Ruf in Medien | nutzt Fotomodus (12.9) |
| **Hafen-Stunts** | Saltmarket | Rampen, Container-Sprünge | Geld, Respekt | Physik-Spielplatz |
| **Aussichtspunkte** | Umland | Panorama anfahren, Foto | Karten-Reveal, kleiner Bonus | Weltentdeckung ohne Fragezeichen-Spam |
| **Graffiti/Kamera überkleben** | Lantern Hill, Rustline | Sammelaufgaben mit Fahrstrecke | Aegis-Dichte lokal ↓ | Weltzustand-Wirkung |
| **Lieferketten** | alle | Fortlaufende Gigs für einen Kunden | Stammkunde-Bonus | Brücke zu Nebenquests |

### 10.3 Belohnungs- und Schwierigkeitsformel

`Geld = Basis(Klasse, Tier) × Bezirksfaktor × (1 + Zeitbonus + Zielbonus) − Schadenskosten`; `Standing = Basis_Standing(Klasse) × Entscheidungsgewicht`; Fehlschlag: ein Teil der Basis als Strafe, nie Totalverlust (Frustkontrolle).

| Klasse | Dauer | Basis-Geld (Richtwert, $) | Standing | Risiko |
|---|---|---|---|---|
| Gig Stufe 1–3 | 3–8 min | 150 / 350 / 700 | ±3 / ±6 / ±10 | gering → hoch |
| Rennen | 4–10 min | 200–1500 (Einsatz-abhängig) | ±10 | mittel |
| Verfolgung / Flucht | 5–12 min | 400–1000 | ±10–15 | hoch |
| Auftragsjagd | 20–40 min | 1500–4000 | ±15–25 | hoch |
| Nebenquest | 15–25 min | 600–1500 + Folge | ±10–20 | mittel |
| Hauptmission | 15–30 min | 500–2000 | ±20–25 | variabel |
| Bezirks-Event | 10–20 min | 500–1200 + Weltwirkung | ±15–30 | mittel |

*Planwerte; Feintuning im Balancing (12.10). Bezirksfaktor: Core 1,3 · Sunstrip 1,1 · Greenmoor 1,2 · Lantern Hill 0,9 · Saltmarket 0,8 · Rustline 0,7 (riskantere Gebiete zahlen weniger Geld, aber mehr Standing/Material).*

### 10.4 Bezirks-Events

| Event | Bezirk | Auslöser | Dauer | Wirkung | Spieler-Optionen |
|---|---|---|---|---|---|
| **Hafenstreik** | Saltmarket | Weltzustand `strike` oder Zeitplan | 2–4 Spieltage | LKW-Verkehr stockt, Straßensperren, mehr Lieferungs-Gigs | Streikbrecher jagen, Union unterstützen |
| **Razzia** | Lantern Hill | Aegis-Dichte hoch | 1 Tag | KCG-Sperren, Heat-Anstieg | Flucht, Verstecken, Informanten warnen |
| **Aegis-Wartung** | Core | Zeitplan | 1 Nacht | Kameras kurz offline → Kamera-Slalom ohne Risiko | Einbruchsgig, Archivmission |
| **Nachtmarkt/Straßenfest** | Sunstrip, Lantern Hill | Wochenende | 1 Abend | Fußgängerdichte hoch, Gigs, Musik | Fotojobs, Taxi, Chaos |
| **Straßenrennen-Nacht** | Rustline | Zufall (Standing Wrenchers) | 1 Nacht | Strecke gesperrt, Polizei dünn | Teilnahme |
| **Stromausfall** | beliebig | nach Blackout-Quest | variabel | Dunkelheit, Ampeln aus, Chaos | Plündergigs, Schutzjobs |

### 10.5 Datenhaltung und Umsetzung (Godot)

- **Ordner:** `data/missions/` (Templates), `data/gigs/` (Gig-Templates), `data/events/` (Bezirks-Events), `data/activities/`; alles JSON mit Schema-Prüfung.
- **`MissionRunner`:** Zustandsautomat je Mission (`LOCKED → OFFERED → ACTIVE → COMPLETED/FAILED`), Schritte als Plugin-Klassen (`DeliveryStep`, `ChaseStep`, `RaceStep`), jeder Schritt hat `enter()`, `update(dt)`, `exit()`, `serialize()`.
- **Marker und GPS:** Zielmarker (der Prototyp nutzt Zylinder-Mesh) plus GPS-Linie (M11); keine Fragezeichen-Karte (9.1).
- **Checkpoints:** Jede Mission definiert Checkpoints; Laden/Autosave nutzt sie (Kapitel 11).
- **Events:** Ein `WorldEventScheduler`-Autoload plant Events anhand von Weltzustand, Tageszeit und Seed.

**Aufwand:** Der Runner und die Schrittbibliothek sind in M7 enthalten; **zusätzlich** (Kapitel 13): 6 PT MVP (4 Typen: Lieferung, Verfolgung, Flucht, Rennen) · 18 PT Ausbau (alle 10 Typen, Events) · 22+ PT Wunschziel (Editor, Variationen).

---

## 11. Speichern und Laden (SaveManager)

**Ziel:** Ein robustes, versioniertes Speichersystem, das Einzelspieler und (später) Online-Charaktere mit demselben Schema abdeckt; Spielstände überleben Updates und Abstürze.

### 11.1 Format und Entscheidungen

| Entscheidung | Wahl | Begründung |
|---|---|---|
| Format | **JSON** (UTF-8, `JSON.stringify`/`JSON.parse_string`), optional komprimiert (`FileAccess.open_compressed`) | Les-/diffbar, versionierbar, kein Skript-/Objektcode (vgl. Sicherheitsregel in Kapitel 3.2); Binärformat erst bei Größenproblemen |
| Ort | `user://saves/` (plattformabhängiger Benutzerordner, im Editor über „Open User Data Folder“ erreichbar) | Standard in Godot [Q34] |
| Slots | 3 manuelle Slots + 1 Autosave + 1 Quicksave | Übersichtlich; Online: serverseitig |
| Versionierung | `schema` (Ganzzahl) + `game_version` (SemVer) + `seed` | Migrationen und Fehlersuche |
| Integrität | Prüfsumme (SHA-256 über den Nutzdaten-Block via `HashingContext`) | Erkennt Beschädigung, **kein** Manipulationsschutz |
| Nur Daten | Kein `var_to_str`/`str_to_var` mit Objekten, kein `.tres/.res` für Spielstände | Schutz vor Code-Einbettung |
| Welt | **Seed + Deltas** statt voller Weltabbildung | Kleine Dateien; Welt ist reproduzierbar (Kapitel 3.2) |

*Hinweis zur Recherche:* Die Godot-Dokumentation *Saving games* beschreibt das Speichern per `FileAccess`, `JSON` und `ConfigFile` (letzteres für Einstellungen) [Q34]. Zur Frage, ob Ressourcen-Laden fremder Dateien Code ausführen kann, fand die Recherche nur Sekundärquellen (die den Objekt-Ausschluss als Grund für Sicherheit nennen) → Regel „nur JSON für Spielstände“ bleibt.

### 11.2 Was gespeichert wird

```text
Savegame = {
  schema: 3, game_version: '0.4.1', saved_at: '2026-11-19T21:04:00', play_time_s: 18432,
  meta: { slot: 1, label: 'Saltmarket – Frühschicht', district: 'saltmarket', thumbnail: 'slot1.png', origin: 'harbor_kid' },
  world: { seed: 6, day: 4, hour: 17.5, weather: { state: 'rain', t: 12.0 },
           deltas: { destroyed_props: [[chunk, id]…], district_tags: { saltmarket: ['strike'] }, camera_states: { cam_031: 'hacked' } },
           world_flags: { 'world.saltmarket.tags': ['strike'] } },
  player: { pos: [x,y,z], yaw: 1.2, hp: 82, money: 3400, skills: { driving: 3, tech: 2, street: 1, stealth: 0 },
            modules: ['plate_jammer'], inventory: [{ id, count }], vehicle_in_use: 'veh_001' },
  vehicles: [ { id: 'veh_001', def: 'sedan_a', paint: '#d02030', tuning: { engine: 2, tires: 1 }, damage: { body: 0.2 },
                parked: { pos, yaw, district } , plate: 'KB-4471' } ],
  reputation: { standing: { aurelia: -10, union: 35, … }, district_heat: { saltmarket: 1.5 }, aegis: 42, npc_trust: { brann: 1 } },
  story: { flags: { … }, completed: ['p1','a1'], active: [{ id, beat, step_state }], decisions: [ … ], delayed_effects: [{ at_day: 7, effect: … }] },
  economy: { stats: { earned: 5200, spent: 1800 }, safehouses: ['sh_saltmarket'], garage: ['veh_001','veh_002'] },
  settings_ref: null, // Einstellungen liegen separat in user://settings.cfg
  checksum: 'sha256:…'
}
```

**Nicht gespeichert:** Verkehrs-/Passantenpositionen (werden neu gespawnt), Chunk-Geometrie (aus Seed erzeugt), Partikel, Kamerastand. **Gespeichert wird beim Checkpoint-Zustand** laufender Missionen (Schritt + Parameter), damit Laden nach Fehlschlag/Absturz konsistent bleibt.

### 11.3 Autosave, Slots, Sicherheit der Dateien

- **Autosave-Auslöser:** Mission-Checkpoint, Betreten/Verlassen eines Safehouses, Abschluss einer Entscheidung (Beat-Ende), alle 5 Spielminuten außerhalb von Verfolgungen, beim Beenden. **Nie** mitten in einer Verfolgung/Eskalation (Heat ≥ 3) oder im Kampf.
- **Manuelles Speichern:** nur sicher (nicht in Mission/Flucht); Quicksave (F5) mit gleichem Schutz.
- **Atomares Schreiben:** erst `slot1.json.tmp` schreiben und prüfen, alte Datei als `slot1.json.bak` behalten, dann umbenennen (`DirAccess.rename`). Beim Laden: bei Fehler automatisch `.bak`.
- **Mehrere Slots:** Slot-Liste mit Metadaten (Label, Ort, Spielzeit, Datum, Vorschaubild, Herkunft, Fortschritt in %). Löschen mit Bestätigung; Kopieren/Exportieren.
- **Spielzeit-Konsistenz:** Verzögerte Effekte (9.2) speichern absolute Spielzeit (`at_day`), nicht relative Zähler.

### 11.4 Versionierung und Migration

- Jede Strukturänderung erhöht `schema`. Eine **Migrationskette** `migrate_v1_to_v2(d)`, `migrate_v2_to_v3(d)` … läuft beim Laden in Reihenfolge; jede Migration ist rein funktional (Dictionary rein → Dictionary raus) und hat eine **Fixture-Datei** in `tests/fixtures/` (alter Spielstand) + Test.
- **Neuere Version als Spiel:** Laden verweigern mit klarer Meldung („Spielstand stammt aus einer neueren Version“), nie stillschweigend kürzen.
- **Defaults statt Fehler:** Fehlende Felder bekommen Standardwerte (Flag-Registry-Defaults aus 6.9); unbekannte Felder werden erhalten (Vorwärtskompatibilität).
- **Content-Updates:** Wird eine Mission/ein Flag entfernt, mappt eine Migration auf `quest.*`-Alias.

### 11.5 Godot-Umsetzung (Autoload `SaveManager`)

```text
SaveManager (Autoload, scripts/save/save_manager.gd)
  signal saved(slot), loaded(slot), save_failed(reason), migrated(from, to)
  func save(slot: int, reason := 'manual') -> Error
  func load_slot(slot: int) -> Error          # Szene laden → Seed → Chunks → Deltas → Spieler/Quests setzen
  func list_slots() -> Array[SlotInfo]
  func delete(slot: int) -> void
  func autosave(trigger: String) -> void
  func register(node: Node)                   # Knoten in Gruppe 'persist' mit serialize()/deserialize()
```
- **Serialisierer-Muster:** Jedes persistente System (`GameState`, `Story`, `TrafficSystem` (nur Seed), Garage, Economy) implementiert `serialize() -> Dictionary` und `deserialize(d: Dictionary)` und ist in der Gruppe `persist`. Der `SaveManager` sammelt, schreibt, prüft, migriert.
- **Lade-Ablauf:** (1) Datei lesen, Prüfsumme prüfen, migrieren; (2) Ladebildschirm; (3) Szene `World` mit Seed erzeugen; (4) Chunks um Spielerposition laden, Deltas anwenden; (5) Fahrzeuge/Spieler setzen; (6) Quest-/Story-Zustand wiederherstellen (inkl. aktiver Schritt); (7) Uhrzeit/Wetter; (8) `loaded`-Signal.
- **API für Mehrspieler:** Dasselbe Schema (JSON-Teil `player`, `story`, `reputation`) wird auf dem Server gespeichert (Kapitel 7.6); lokaler und serverseitiger Speicher teilen die Migrationskette.
- **Einstellungen getrennt:** `user://settings.cfg` über `ConfigFile` (Auflösung, Audio, Steuerung, Barrierefreiheit), unabhängig von Spielständen.
- **Tests:** Round-Trip-Test (speichern → laden → vergleichen), Migrationstest je Schema-Version, Korruptionstest (abgeschnittene Datei → `.bak`), Soft-Lock-Test (Laden mitten in Mission), Performance (Speichern ≤ 100 ms ohne Frame-Hitch durch Schreiben im Thread).
- **Risiken:** Hitch beim Schreiben (→ Thread), Schemaänderungen vergessen (→ CI-Test „Schema erhöht?“), Spielstand-Manipulation (→ bei Online: Server speichert; offline akzeptiert).

**Aufwand:** MVP 3 PT (Slots, Autosave, JSON, Round-Trip) · Ausbau 7 PT (Migration, Thumbnails, Backup, Checkpoint-Integration) · Wunsch 9+ PT (Cloud-Speicher, Export/Import, Replay-Daten).

---

## 12. Spielsysteme und Produktion: alles, was zu einem Spiel dieses Umfangs gehört

Dieses Kapitel sammelt die Querschnittsthemen, die in Modulen leicht untergehen. Jeder Abschnitt nennt Umfang (MVP/Ausbau/Wunsch), Godot-Umsetzung und Verknüpfung.

### 12.1 Charaktererstellung und Herkunft

- **Umfang MVP:** Name, 3 Herkünfte (9.3), 6 vorgefertigte Aussehen (Auswahl statt freiem Editor), Stimme (2 Varianten). Die Herkunft setzt Startwerte, Prolog-Variante und Dialogoptionen.
- **Ausbau:** Aussehen-Anpassung (Frisur, Kleidung, Farben als Parameter), Hintergrundtext, Startfahrzeug wählbar.
- **Wunsch:** Voller Charakter-Editor, Kleiderschrank, Tattoos/Implantat-Optik.
- **Godot:** `CharacterProfile`-Ressource (JSON-gespeichert), `Control`-Szene `character_creation.tscn`; Spielerfigur zunächst Fahrzeug-zentriert, **Zu-Fuß-Spiel** (Aussteigen, Laufen) wird als eigenes Paket geführt (Kapitel 13). Mesh-Varianten per `MultiMesh`/Blendshapes.

### 12.2 Skills und Fortschritt

Siehe 9.3 (vier Skills, nutzungsbasiert; Module mit Nebenwirkung). Ergänzung: **Fortschrittsanzeige** (Skills, Fraktionsstände, Bezirks-Ruf, Aegis-Profil) im Charakter-Menü; **keine Stufen-Obergrenze-Schleife**, sondern Meilenstein-Belohnungen (neue Gig-Stufen, Händler, Fahrzeuge).

### 12.3 Inventar, Garage und Fahrzeug-Tuning

- **Inventar (klein halten):** Gegenstände ohne Gewichtssystem; Kategorien: *Schlüssel/Zugang*, *Beweise* (z. B. der Ledger-Kern), *Gadgets* (Kamera-Störer, Kennzeichen-Set, Abschleppseil), *Verbrauchsgüter* (Reparatur, Treibstoffzusatz). Begrenzung über Slots (12/20/30 je Fortschritt).
- **Garage:** Safehouses mit 2/4/8/16 Stellplätzen; Fahrzeuge gespeichert mit Lack, Tuning, Schaden, Kennzeichen; **Parken** außerhalb der Garage speichert Position (Bezirk). Fahrzeuge „heiß“ (gesucht) gelten als Risiko in überwachten Bezirken (Aegis).
- **Tuning (Daten, M4):** Motor, Getriebe, Reifen, Federung, Bremsen, Gewicht, Aerodynamik, Lack/Aufkleber, Kennzeichen. Jede Stufe verändert `VehicleDef`-Parameter (Beschleunigung, Grip, Bremsweg, Masse) und kostet Geld + ggf. Fraktionsstand (Rustline-Teile gegen Standing). **Tuning-Slots** pro Klasse; Auswirkungen im Menü als Balken (Vergleich vorher/nachher).
- **Godot:** `ItemDef`/`TuningPart`-Ressourcen (`.tres`); Garage-Szene mit Fahrzeugvorschau im `SubViewport`; Tuning schreibt Modifikatoren (Multiplikatoren) auf den Fahrzeugkontroller; Server prüft (Kapitel 7.7).

### 12.4 Wirtschaft

- **Einnahmen:** Missionen/Gigs/Aktivitäten (Kapitel 10), Verkäufe, Wetten, Schwarzmarkt; **Ausgaben:** Reparatur, Treibstoff (optional), Tuning, Fahrzeuge, Safehouse-Miete, Bestechung, Kennzeichen.
- **Preisgefälle nach Bezirk:** Core teuer und legal, Rustline billig und riskant; Schwarzmarkt-Aufschläge (Aegis-Risiko).
- **Preisrahmen (Planwerte):** Reparatur $50–400, Reifen-/Brems-Upgrade $300–2000, Fahrzeug $6.000–60.000, Safehouse $8.000–40.000. Zielwert: Ein „guter“ Spielstunden-Einkommen ca. $4.000–6.000; ein neues Mittelklassefahrzeug nach 3–4 Spielstunden.
- **Steuerung:** Alle Zahlen in `data/economy.json`; ein **Economy-Simulator** (Skript, Monte-Carlo über typische Spielverläufe) prüft Inflation/Engpässe (12.10).

### 12.5 Menüs und Einstellungen

- **Menüs:** Hauptmenü (Fortsetzen, Neues Spiel, Laden, Optionen, Extras, Beenden), Pause-Menü, Karte, Journal (Aufträge, Entscheidungen, Kontakte), Charakter, Garage, Handy-/Terminal-Menü, Foto, Credits.
- **Optionen:** *Grafik* (Preset Niedrig/Mittel/Hoch/Benutzerdefiniert; Auflösung, Fenstermodus, V-Sync, FPS-Limit, **Skalierung FSR 2.2/Native**, Schatten, SDFGI, Volumennebel, SSAO/SSR, Sichtweite), *Audio* (Master, Musik, Radio, Effekte, Sprache, Umgebung, UI), *Steuerung* (**Tastaturbelegung ändern, Gamepad**, Empfindlichkeit, Invertierung, Totzone), *Spiel* (Sprache, Untertitel, Schwierigkeit, Hilfen), *Barrierefreiheit* (12.6).
- **Godot:** `Control`-Szenen mit einem `Theme`; `InputMap` + Neubelegung (`InputEvent` serialisiert in `ConfigFile`); Einstellungen in `user://settings.cfg`; **Grafik-Presets als Ressource** (Kapitel 5/M12); Menüs sind Szenen, steuerbar per Tastatur/Gamepad (Fokus-Navigation).

### 12.6 Barrierefreiheit

Orientierung an den **Game Accessibility Guidelines** (nach Aufwand in Basis/Fortgeschritten/Erweitert gestuft) [Q35]. Umsetzungsplan:

| Stufe | Maßnahme |
|---|---|
| MVP (Basis) | **Untertitel** für alle gesprochenen Dialoge und wichtige Geräusche, Größe/Hintergrund einstellbar; **freie Tastenbelegung**; getrennte Lautstärkeregler (Musik/Effekte/Sprache); gut lesbarer Text mit Kontrast; mehrere **Schwierigkeitsgrade**; Hilfen wählbar (Lenkhilfe, Bremshilfe) |
| Ausbau | **Farbenblind-Modi** (Filter und Symbole zusätzlich zu Farben: Polizei-/Marker-Icons), Kamera-Schütteln/Blur/Bewegungsunschärfe abschaltbar, Schwierigkeit **während des Spiels** änderbar, Halten statt Drücken (Toggle), Zeitlimit-Entschärfung bei Missionen, UI-Skalierung, Warnton/Visual-Cues für Sirenen (Untertitel „[Sirene links]“) |
| Wunsch | Text-to-Speech für Menüs, einhändige Steuerschemata, visuelle Entsprechungen zu Audiosignalen (Radar), Assistenz-Autopilot für Fahrstrecken (Gigs) |

**Godot:** `Accessibility`-Autoload liest Einstellungen und wirkt auf UI-Theme, Kamera-Effekte, Untertitel-Overlay (`CanvasLayer`), Eingabe-Modi und Missions-Zeitlimits. Farbenblind-Filter als Shader im **Environment/Compositor**-Pfad oder Fullscreen-Material.

### 12.7 Tutorial und Onboarding

- **Prolog als Tutorial:** Die ersten 20 min lehren Gas/Lenken/Handbremse, Minimap/GPS, Fahndung/Aegis, Gig annehmen und abschließen, Speichern – eingebettet in die Handlung, keine Texttafeln-Wand.
- **Kontextuelle Hinweise:** Einmalige Einblendungen bei Erstkontakt (Kamera, Heat, Garage); abschaltbar; im Journal nachlesbar.
- **Fahrschule (Aktivität):** Optionale Fahrübungen (Drift, Notbremsung, Verfolgung) für Wiedereinsteiger; Ranglisten lokal.
- **Test:** Drei Neulinge ohne Anleitung spielen den Prolog; Kennzahl: ≥ 80 % schließen ohne externe Hilfe ab (Telemetrie 12.11).

### 12.8 Radio und Musik

Konzept in 9.6 (fünf fiktive Sender, reaktive Nachrichten, Satire-Werbung). Ergänzend: **Mix-Regeln** (Radio unter Dialogen absenken, Ducking via Bus-Effekt), **Lizenz-Pipeline** (nur eigene/CC0/prozedurale Musik, Lizenzliste, keine Fremdtitel), **Content-Ziel** MVP 3 Sender × 30 min, Ausbau 5 Sender × 60 min, Wunsch dynamische Remixe je Wetter/Heat.

### 12.9 Fotomodus

- **Umfang MVP:** Pause + freie Kamera (Bewegung, Neigung, FOV), Aufnahme als PNG in `user://photos/`, Tiefenschärfe-Regler, Filter (Kontrast/Sättigung/Kornung), HUD ausblenden.
- **Ausbau:** Zeit und Wetter für das Foto einstellen (nur Anzeige), Rahmen/Titel, Fahrzeug-Posen, **Foto-Jobs** (Aufgabenliste, Bewertung nach Motiv/Qualität).
- **Wunsch:** Ranglisten, Bildergalerie im Spiel, Export.
- **Godot:** `Camera3D` getrennt von Spielerkamera; `Viewport.get_texture().get_image()` → `save_png`; Environment-Anpassung per `CameraAttributesPractical` (DoF) bzw. Overrides.

### 12.10 Balancing

- **Daten-getrieben:** Alle Kennzahlen (Belohnungen, Preise, Heat-Parameter, KI-Werte, Fahrzeugdaten) in `data/*.json|tres`, nicht im Code.
- **Werkzeuge:** Economy-Simulator, Heat-Simulator (wie lange bis Verlust), Fahrzeugvergleichstabelle (Benchmark-Tests 0–100, Kurvenradius), **Schwierigkeitsprofile** (Heat-Zerfall, Polizei-Aggressivität, Zeitlimits).
- **Prozess:** Wöchentlicher Playtest-Zyklus, Telemetrie-Auswertung (12.11), Änderungsprotokoll (`docs/BALANCING.md`), keine Änderung ohne Messwert.

### 12.11 Telemetrie

- **Grundsatz:** Zuerst **lokal** (JSON-Lines-Logdatei in `user://telemetry/`), **opt-in** mit klarer Einwilligung; Upload nur nach Zustimmung; keine personenbezogenen Daten, keine Eingabetexte; DSGVO-konform (Zweck, Speicherdauer, Löschung).
- **Ereignisse:** Missionsstart/-ende/-abbruch, Tod/Festnahme (Ort, Ursache), Entscheidungswahl, FPS-Einbrüche (> 50 ms) mit Szene/Bezirk, Ladezeiten, Abstürze (Stack-Info), Menü-Nutzung, Tutorial-Abbrüche.
- **Auswertung:** Skript erzeugt Heatmaps (Todesorte), Abbruchraten je Mission, Entscheidungsverteilungen, Performance-Karte. Wird zur Datengrundlage für Balancing und Optimierung.
- **Godot:** Autoload `Telemetry` mit Puffer, Hintergrund-Thread für Schreiben; `OS`-Informationen (GPU, Treiber) nur für Fehlersuche.

### 12.12 Tests und Qualitätssicherung

| Ebene | Inhalt | Werkzeug |
|---|---|---|
| Unit | Generator, Straßengraph, Story-Bedingungen, Wirtschaft, Spielstand-Migration | Headless-GDScript, **GUT oder gdUnit4** *(Auswahl prüfen)* |
| Graph/Daten | Graph-Lint (Dialog/Quest/Flags), JSON-Schemata, Referenzprüfung | `tests/graph_lint.gd` |
| Simulation | Story-Simulator (alle Enden), Economy-Simulator, Verkehrs-Stresstest | Headless |
| Integration | Speichern/Laden-Round-Trip, Mission Start–Ende per Bot-Fahrer, Dedicated-Server-Verbindung | Szenentests |
| Performance | Benchmark-Flug mit Grenzwerten (fps, Frame-ms, VRAM) | `--fps`/Benchmark-Szene |
| Playtest | Strukturierte Tests (Tutorial, Missionen, Balancing) | Protokollbogen, Telemetrie |
| Regression | Vor jedem Release: Smoke-Test der Kernschleife (Fahren → Gig → Speichern → Laden) | Checkliste + Skript |

Optionale **CI** (z. B. GitHub Actions mit Godot headless) führt Unit-/Lint-/Simulationstests bei jedem Commit aus.

### 12.13 Release-Prozess

1. **Versionierung:** SemVer (`0.x` bis Beta), Changelog (`CHANGELOG.md`), Git-Tags je Build.
2. **Branching:** `main` stabil, Feature-Branches, Release-Branch für Release-Kandidaten.
3. **Build:** Windows-Export mit Export-Templates; Ordner/ZIP, später Installer; Server-Export (Linux, Feature-Tag `dedicated_server`) [Q32].
4. **Qualitätstor:** Tests grün, Benchmark im Budget (M12), Smoke-Test, **Lizenz-Check** aller Assets (`assets/LICENSES.md`), Inhaltsprüfung (keine Fremdmarken).
5. **Absturz-/Fehlerprotokolle:** Logs in `user://logs/`, Meldeformular, optional Upload mit Einwilligung.
6. **Updates/Patches:** Versions-Check beim Start, Patch-Notes im Spiel; Spielstände bleiben durch Migration lesbar (Kapitel 11).
7. **Verteilung:** Direkt-Download zuerst; Plattform-Stores (Konten/Gebühren nötig) erst später entscheiden.
8. **Beta-Phase:** Geschlossener Test mit Fragebogen, Telemetrie-Auswertung, 2 Wochen „Bugfix-only“ vor Release.

**Aufwand (Kapitel 13):** Charaktererstellung 2/5/6 · Inventar/Garage/Tuning 4/10/14 · Menüs/Optionen/Barrierefreiheit 4/8/10 · Tutorial 3/6/8 · Fotomodus 1/3/4 · Balancing/Telemetrie 2/6/8 · Tests/QA 3/8/10 · Release-Prozess 2/5/8 · Wirtschafts-Ausbau 0/3/5 · Dialogsystem 4/10/12 (PT MVP/Ausbau/Wunsch).

---

## 13. Neue Erkenntnisse, Pläne und Aufwandsfortschreibung

### 13.1 Stand des Godot-Prototyps (19.11.2026, Abend)

- **Messwert:** Laut Prototyp-Session läuft der Prototyp mit **ca. 160 FPS auf der RX 5700 XT** (mit SDFGI, Volumennebel, SSAO, SSR, Glow, Laternen mit Licht-Pool, MultiMesh-Bäumen). Die Zahl stammt aus der Rückmeldung der Prototyp-Session und wurde in diesem Dokument **nicht nachgemessen**; Auflösung/Preset sind nicht angegeben (→ MS1 hält die Messung samt Einstellungen fest).
- **Gerade ergänzt (Arbeitskopie):** Fußgänger (60), Polizei-Fahndung (`heat`, Festnahme), Missionsmarker mit Zeitbonus, Minimap (`minimap.gd`).
- **Bereits vorhanden:** Boden-/Fassaden-Shader, Auto (`VehicleBody3D`), 28 Verkehrsautos, Tag/Nacht, Seed, `--test`/`--fps`-Modi, Parks/Bäume, Laternen-MultiMesh mit Lichtpool.

### 13.2 Erkenntnisse aus der bisherigen Arbeit

1. **Shader statt Texturen trägt:** Straßen, Markierungen und Fenster werden im Fragment-Shader aus der Weltposition erzeugt (keine Texturen, keine Mesh-Menge). Das bleibt Standard für Raster und Fassaden; für Hang/Kurven kommen Mesh-Bänder dazu (M1).
2. **MultiMesh ist das richtige Werkzeug:** Bäume und Laternen laufen bereits als MultiMesh. Regel: **Jede wiederholte Prop-Art ist ein MultiMesh je Chunk** (Kapitel 4/M2).
3. **Licht-Pool funktioniert:** 8 `OmniLight3D`, die den nächsten Laternen zugewiesen werden, genügen für die Nachtbeleuchtung. Verbesserungen: **Spatial Hash** statt Sortieren aller Laternenpositionen alle 0,25 s, **Hysterese** (keine springenden Lichter) und weiches Ein-/Ausblenden.
4. **Das Budget ist (in der kleinen Szene) großzügig:** Bei ca. 160 FPS (≈ 6 ms) bleibt viel Reserve. **Aber** die Stadt ist 640 × 640 m; die Zielwelt ist grob 25-mal größer. Folge: SDFGI und Volumennebel zunächst **in „Mittel“ lassen**, Entscheidung erst nach einem Benchmark mit der gewachsenen Welt (Kriterium: ≥ 90 fps im Benchmark-Flug über alle Bezirke, sonst raus aus „Mittel“).
5. **Portierung Three.js → GDScript ist unproblematisch:** Linien-Agenten, Fahndungs- und Missionslogik liefen ohne Umbau (Typisierung beachten). **Konvention festhalten:** `VehicleBody3D` fährt in +Z, Verkehrsfahrzeuge haben ihre Front bei −Z (Prototyp-Kommentar) → Fahrzeugszene mit einheitlicher Vorwärtsrichtung bauen (Visual drehen), bevor weitere Fahrzeugtypen entstehen.
6. **Test-Modi als Muster:** `--test` und `--fps` zeigen den Weg für CI und Benchmark-Flug (Kommandozeilen-Flags, Ausgabe, `quit()`); in MS1 zu einem Test-/Benchmark-Runner verallgemeinern.
7. **Prozess:** Der Prototyp wächst schneller als seine Dokumentation (veraltetes `README.md`, nicht eingecheckter Stand). → Commit-Disziplin je Teilschritt, README mit Stand und Befehlen mitpflegen, Konzept-Dokument bei Meilenstein-Abschluss aktualisieren.
8. **Offen/ungeprüft:** SDFGI mit Streaming, `VehicleBody3D` unter Jolt vs. Godot Physics (Kapitel 4/M4), tatsächliche GPU-Kosten der einzelnen Effekte (Visual Profiler), GDScript-Leistung bei Hunderten Agenten.

### 13.3 Pläne und Entscheidungen

| Thema | Entscheidung / Plan | Status |
|---|---|---|
| Struktur | `main.gd` in Szenen/Autoloads zerlegen (Kapitel 15), danach erst neue Systeme | **Zuerst** (MS1) |
| Messung | Overlay + Benchmark-Flug + Dokumentation der Einstellungen (Auflösung, Preset, Treiber) | MS1 |
| Physik | Jolt vs. Godot Physics am Prototyp-Auto messen, danach festlegen | MS1/MS5 |
| Sprache | **GDScript (typisiert)**; C#/GDExtension nur bei nachgewiesenem Engpass | Entscheidung bei MS2-Messung |
| Welt | Straßengraph (`CityData`) vor Ausbau von KI und Polizei | MS2 |
| Spielsysteme | Speichern/Dialog/Missionstypen in MS6 (Kapitel 10–11) | MS6 |
| Zu-Fuß-Spiel | Eigenes Paket (Aussteigen, Laufen, Animationen, Kamera), 4/8/12 PT | vor MS6 einplanen |
| Test-Framework | GUT oder gdUnit4 vergleichen (1 Tag), dann festlegen | MS1 |
| Terrain | Eigene Heightmap-Chunks vs. Add-on (z. B. Terrain3D) prüfen | MS4 |
| Server | Erst nach Einzelspieler-Release-Kandidat (MS8) | MS9 |

### 13.4 Aufwandsfortschreibung (Personentage, Planwerte ±50 %)

**Anpassung durch den Prototypfortschritt** (bereits gebaut, spart MVP-Aufwand): Fußgänger/Polizei/Mission/Minimap portiert (M5 −2, M6 −1,5, M7 −1, M11 −1), Laternen/Lichtpool (M9 −0,5), Props/MultiMesh (M13 −1, M2 −0,5), `--fps`-Modus (M12 −0,5): **MVP −8 PT, Ausbau −3 PT**.

| Block | MVP | Ausbau | Wunsch |
|---|---|---|---|
| Kapitel 4: M1–M12 (Basis laut Gesamtschätzung, ohne M13–M15) | 44 | 124 | 156+ |
| M13 Realismus/Detailtiefe | 6 | 18 | 22+ |
| M14 Story-Engine | 8 | 20 | 25+ |
| M15 Server/Mehrspieler | 10 | 22 | 35+ |
| Prototypfortschritt (Abzug) | −8 | −3 | 0 |
| **Zwischensumme Basis** | **60** | **181** | **238+** |
| Kapitel 9: Design-Vorbilder | 22 | 58 | 75+ |
| Kapitel 10: Missionstypen und Aktivitäten | 6 | 18 | 22+ |
| Kapitel 11: Speichern/Laden (SaveManager) | 3 | 7 | 9+ |
| Kapitel 12: Charaktererstellung | 2 | 5 | 6+ |
| Kapitel 12: Inventar/Garage/Tuning | 4 | 10 | 14+ |
| Kapitel 12: Menüs/Optionen/Barrierefreiheit | 4 | 8 | 10+ |
| Kapitel 12: Tutorial/Onboarding | 3 | 6 | 8+ |
| Kapitel 12: Fotomodus | 1 | 3 | 4+ |
| Kapitel 12: Balancing/Telemetrie | 2 | 6 | 8+ |
| Kapitel 12: Tests/QA | 3 | 8 | 10+ |
| Kapitel 12: Release-Prozess | 2 | 5 | 8+ |
| Kapitel 12: Wirtschafts-Ausbau | 0 | 3 | 5+ |
| Dialogsystem (Graph-Runner, UI, Untertitel) | 4 | 10 | 12+ |
| Story-Content (Dialoge/Ketten schreiben und skripten, Kapitel 6.8–6.10) | 6 | 25 | 35+ |
| Zu-Fuß-Spiel (Paket) | 4 | 8 | 12+ |
| **Gesamtsumme** | **126** | **361** | **476+** |

(Die Basiszeile ergibt sich aus der Gesamtschätzung in Kapitel 4 abzüglich der Module M13–M15, die separat stehen. Überlappungen zwischen den Missionstypen (Kapitel 10) und M7 sind konservativ **nicht** abgezogen; erwartbar ist ein Korridor von −10 %.)

**Einordnung:** Gesamtsumme MVP ≈ 126 PT ≈ **25 Arbeitswochen** (eine Person mit KI-Unterstützung, 5 PT/Woche), davon ca. 116 PT Einzelspieler und 10 PT Server. Der **Einzelspieler-Release-Kandidat (MS8)** entspricht etwa MVP + Teile des Ausbaus (grob 200–250 PT ≈ 10–12 Monate). Der Umfang ist für eine Einzelperson sehr groß; empfohlene Priorisierung bei Engpass: (1) Fahren, Welt, Verkehr, Polizei (Kern), (2) eine vollständige Kette + Entscheidungen, (3) Speichern/Menüs/Tutorial, (4) Radio/Wetter/Realismus, (5) Rest. Alle Zahlen sind Planwerte, keine Zusagen.

### 13.5 Aktualisierte Meilenstein-Dauern

| Meilenstein | Bisher | Neu | Grund |
|---|---|---|---|
| MS1 Fundament | 1–1,5 Wochen | **1 Woche** | Ports (Fußgänger, Polizei, Mission, Minimap) sind im Prototyp, Struktur-Umbau bleibt |
| MS2 Graph & Verkehr | 2 | 2 | – |
| MS3 Bezirke & Look-Dev | 3 | 3–4 | + Vertikalität Core (1 Ebene), Neon-Look |
| MS4 Streaming | 2–3 | 2–3 | – |
| MS5 Fahren & Polizei | 2–3 | 3 | + Chaos-Systemik, Zu-Fuß-Paket |
| MS6 Missionen & Story | 3–4 | **5–6** | + Dialogsystem, SaveManager, Missionstypen, Herkunft, Menüs-MVP |
| MS7 Atmosphäre | 2–3 | 3–4 | + Radio, Fotomodus, Barrierefreiheit-MVP |
| MS8 Vollversion | 5+ | **10+** | + 3 Ketten, Nebenquests, Verträge, Tuning/Garage, Balancing, Tests, Release-Prozess |
| MS9–MS11 | 3 / 5 / 5+ | 3 / 5 / 5+ | unverändert |

---

## 14. Roadmap mit Meilensteinen

Alle Meilensteine haben ein **überprüfbares Done-Kriterium**. Messgrößen: `Performance`-Monitore im Debug-Overlay (fps, Frame-ms, Draw Calls, Videospeicher), Visual Profiler, Benchmark-Flug (fester Seed). Referenzgerät für alle Messungen: **RX 5700 XT + Ryzen 5 3600**, 1080p, Preset „Mittel“, sofern nicht anders angegeben.
**Reihenfolge-Regel:** Meilensteine 1–8 machen den **Einzelspieler stabil und vollständig**; erst danach folgen Server und Mehrspieler (MS9–MS11). Die Reihenfolge entspricht der bisherigen Planung; geändert haben sich Technik und Aufwände.

### Teil A – Einzelspieler

#### Meilenstein 1 – Fundament auf dem Godot-Prototyp *(≈ 1 Woche Restarbeit)*

**Bereits vorhanden (Stand Godot-Prototyp, nicht neu zu bauen):**
- ✔ Projekt `godot/` (Godot 4.7.2, Forward+), Start per `godot --path …`
- ✔ 8×8-Stadt mit Boden-Shader (Straßen/Markierungen) und Gebäuden mit Fenster-/Nachtlicht-Shader
- ✔ Auto (`VehicleBody3D`, 4 Räder, Verfolgerkamera, Scheinwerfer), Steuerung W/S/A/D/Leertaste/R
- ✔ 28 Verkehrsautos und 60 Fußgänger auf Linien-Agenten; Polizei mit Fahndungswert und Festnahme; Missionsmarker mit Zeitbonus; Minimap (Arbeitskopie, noch nicht committed)
- ✔ Laternen als MultiMesh mit 8-Lichter-Pool, Bäume als MultiMesh, Parks, Gebäude-Details
- ✔ SDFGI (3 Cascades) und Volumennebel bereits aktiviert (noch ohne Messung)
- ✔ Tag/Nacht (20 s/Stunde), SSAO, SSR, Glow, ACES-Tonemapping
- ✔ Reproduzierbarer Seed (`rng.seed = 6`), Headless-Test (`-- --test`)

**Zu tun in MS1:**
- `main.gd` in **Szenen und Autoloads** zerlegen (Kapitel 15): `World`, `Vehicle`, `TrafficSystem`, `Clock`, `HUD`, `EventBus`; Verhalten unverändert.
- Seed als Kommandozeilenargument (`-- --seed=…`); Weltkonstanten als **Global Shader Uniforms** statt doppelter Konstanten.
- Gebäude je Chunk als **`MultiMeshInstance3D`** (Shader-Tint über Instanzdaten), Kollision je Chunk gebündelt.
- **Debug-Overlay (F3)** mit `Performance`-Monitoren; Qualitäts-Presets (Niedrig/Mittel/Hoch) als Ressource.
- Bereits portierte Spielsysteme (Polizei, Fußgänger, Mission, Minimap) in eigene Szenen/Autoloads überführen (`PoliceDispatcher`, `TrafficSystem`, `MissionRunner`, `minimap.tscn`); ihr Ausbau bleibt in MS2/MS5/MS6.
- **Erste Messung:** `--fps`-Lauf auf der RX 5700 XT dokumentieren (mit und ohne SDFGI/Volumennebel); Ergebnis ersetzt die Planwerte aus Kapitel 5.1.
- **Done:** (1) Spiel startet und läuft wie vorher (Fahren, Verkehr, Tag/Nacht, Reset-Taste). (2) Gleicher Seed → identische Stadt in zwei Läufen (Gebäudepositionen im Test verglichen). (3) Draw Calls im Overlay **≤ 600** (Basis: je Chunk ein MultiMesh) bei 64 Blöcken; **Frame-Zeit ≤ 8 ms** im Benchmark-Flug auf dem Referenzgerät. (4) `godot --headless -- --test` meldet Erfolg.

#### Meilenstein 2 – Straßengraph und lebendiger Verkehr *(≈ 2 Wochen)*
- `CityData` mit Straßengraph (Knoten/Kanten/Spuren); Verkehr, Fußgänger und Polizei fahren auf dem Graph (`AStar3D` für Routing).
- Ampeln/Vorfahrt, Abstandhalten, Spawn am Aktivradius; Verkehr als MultiMesh, nahe Autos mit Physikkörper; Minimap aus `CityData`.
- **Done:** 60-s-Messlauf mit 120 Fahrzeugen (Stufe A+B) ohne Stillstands-Deadlock > 10 s; 0 geloggte Rotlichtverstöße; Main-Thread-Zeit der Verkehrs-Simulation ≤ 3 ms/Frame.

#### Meilenstein 3 – Bezirke, dystopische Identität, Look-Dev *(≈ 3–4 Wochen)*
- Generator MVP (Parzellen, Bezirksparameter), **vier** Bezirke spielbar (Core, Saltmarket, Sunstrip, Greenmoor) mit Landmarken, Bezirksname beim Einfahren.
- **Look-Dev-Szene** und Realismus-MVP (M13): 12 PBR-Materialien, Farbpipeline, Reflection Probes, 20 Decals, 30 Props, `wear`-Attribut.
- Aegis-Kameras als erste Props (Optik und Sichtkegel).
- **Ergänzt (Kapitel 9):** eine zusätzliche Straßenebene (Hochstraße) im Core als Prototyp; Neon-/Reklame-Look; erste satirische Plakate/Props.
- **Done:** (1) Blindtest: 5 Screenshots ohne HUD werden von 3 Testpersonen zu ≥ 80 % richtig dem Bezirk zugeordnet. (2) Generator-Test: 100 Seeds ohne Gebäude auf Straße und mit vollständig verbundenem Graph. (3) Preset „Mittel“ hält ≥ 60 fps im Benchmark-Flug durch alle vier Bezirke. (4) Hochstraße im Core befahrbar (Auf-/Abfahrt, KI-Verkehr auf beiden Ebenen) ohne Stau-Deadlock.

#### Meilenstein 4 – Streaming und große Welt *(≈ 2–3 Wochen)*
- Chunk-Streaming mit Worker-Threads, Visibility Ranges/HLOD, Terrain mit Höhen; Welt auf ca. 3 × 3 km Stadt + Umland erweitern.
- **Done:** Fahrt von Küste zu Berggipfel (ca. 6 km) im Benchmark-Flug: durchschnittlich ≥ 60 fps im Preset „Mittel“, 1-%-Low ≥ 40 fps; Videospeicher nach 10 Minuten Fahrt ± 10 % konstant (kein Leck); kein Hitch > 50 ms durch Chunk-Laden; kein sichtbares Pop-in näher als 300 m.

#### Meilenstein 5 – Fahren und Polizei auf Spielniveau *(≈ 3 Wochen)*
- Fahrzeugdatenblätter, Drift, Raycast-Rad-Physik (eigener Controller, Vergleich Jolt/Godot Physics), Schaden (M4); Polizei mit Sichtlinie, Suchzustand, Bezirks-Heat und Aegis-Kennzeichenerfassung (M6).
- **Ergänzt (Kapitel 9/12/13):** Chaos-Systemik (zerstörbare Props, Verkehrsreaktionen, 3 Polizei-Eskalationsstufen); **Zu-Fuß-Paket** (Aussteigen, Laufen, Einsteigen); Garage-Grundlage.
- **Done:** (1) 5 Fahrzeugklassen mit deutlich unterschiedlichem Fahrverhalten (Messtabelle 0–100 km/h, Kurvenradius). (2) Verfolgung kann ohne Cheat beendet werden (Playtest: ≥ 3 von 5 Testpersonen schütteln die Polizei innerhalb von 3 Minuten ab, ≥ 1 wird festgenommen). (3) Überschlag-/Kollisionstest: 100 Zufallskollisionen ohne Physik-Explosion (kein Objekt > 100 m/s). (4) Aussteigen/Einsteigen funktioniert in 100 Versuchen ohne Hänger.

#### Meilenstein 6 – Missionen, Story-Engine, Speichern *(≈ 5–6 Wochen)*
- Missions-Framework (M7), Story-Engine MVP (M14: Datenmodell, Fraktionsstand, Entscheidung, Flags), Prolog + Akt I (ca. 6 Missionen) mit Wendepunkt W1; Garage/Speichern (M8), Vollkarte mit GPS (M11).
- **Ergänzt (Kapitel 6.8–6.10, 9–12):** Dialogsystem (Graph-Runner, Untertitel), Quest-Klassen und verzögerte Folgen, Flag-Registry mit Graph-Lint; **SaveManager** (3 Slots, Autosave, Migration); Missionstypen Lieferung/Verfolgung/Flucht/Rennen; Herkunft (3 Startwege) und 3 Skills; Gig-Generator (3 Typen); Menüs/Optionen-MVP; Prolog als Tutorial; **Kette A „Hafenpakt“** (Prolog–Akt I).
- **Done:** (1) Speichern/Laden (JSON) reproduziert Geld, Position, Fahrzeug, Standing, Flags und Missionsstand exakt (automatischer Test). (2) Story-Simulator läuft grün: alle Entscheidungen von Prolog bis W1 durchspielbar, keine Sackgasse. (3) Zwei verschiedene Entscheidungen bei W1 führen nachweislich zu unterschiedlichen Folgemissionen und Weltzustand-Tags. (4) Graph-Lint grün. (5) Savegame-Migrationstest für 2 Schemas besteht. (6) 3 Neulinge schließen den Prolog ohne externe Hilfe ab (≥ 80 %). (7) Kette A (A1–A3) ist mit zwei verschiedenen Entscheidungspfaden spielbar.

#### Meilenstein 7 – Atmosphäre: Wetter, Audio, Nacht, Nässe *(≈ 3–4 Wochen)*
- Wetter-Zustandsautomat, Nässe-Shader, Verschleiß-Look (M13 Ausbau), Motor-/Sirenen-/Umgebungsklang je Bezirk, Straßenbeleuchtung nach Bezirk; SDFGI-Test für „Hoch“.
- **Ergänzt (Kapitel 9/12):** reaktives Radio (3 Sender) mit Satire-Werbung; Fotomodus-MVP; Barrierefreiheit-MVP (Untertitel, Neubelegung, Farbenblind-Filter); lokale Telemetrie (opt-in); Nachtleben-Orte, 2 Aktivitäten, 2 Bezirks-Events.
- **Done:** Alle 5 Wetterzustände wechseln ohne fps-Einbruch > 15 %; jeder Bezirk hat eine eigene Audio-Umgebung (Hörtest: ≥ 80 % richtige Zuordnung ohne Bild); nachts nie mehr als 6 schattenwerfende Lichter; Preset „Hoch“ (mit SDFGI, falls bestanden) hält ≥ 45 fps, sonst wird SDFGI aus „Hoch“ gestrichen. (4) Radio-Nachrichten reagieren nachweislich auf mindestens 3 Weltzustand-Tags. (5) Untertitel und Tastenneubelegung sind vollständig nutzbar (Basis-Checkliste der Game Accessibility Guidelines).

#### Meilenstein 8 – Einzelspieler-Vollversion (Release-Kandidat) *(≈ 10+ Wochen)*
- Alle 6 Bezirke und Umland (Highway, Coast Road, Tunnel), Story komplett: Akt II–III, W2–W5, Enden E1–E4, Nebenaufträge, Weltzustand-Tags sichtbar, Aegis-Mechaniken (Kameras hacken, Kennzeichen), Tuning-Feinschliff; Windows-Export (Export-Templates, Installer/Zip).
- **Ergänzt (Kapitel 6, 9–12):** alle drei Ketten (A–C) und Konzernroute; 12 Nebenquests; 6 Verträge; Gig-Ausbau (8 Typen, Ketten); Inventar/Garage/Tuning; Module und Skills; 5 Sender; Balancing-Durchgang mit Telemetrie; Tests/CI; Release-Prozess (Export, Changelog, Lizenz-Check).
- **Done:** (1) Vollständiger Durchlauf der Kampagne durch 3 Testpersonen ohne Blocker; jedes Ende E1–E4 mindestens einmal erreicht. (2) Story-Simulator: 5000 zufällige Entscheidungsläufe, 0 Sackgassen, jedes Ende erreichbar. (3) Benchmark-Flug auf dem Referenzgerät erfüllt die Budgets aus M12 (Preset „Mittel“ 60 fps; „Niedrig“ ≥ 60 fps bei reduzierter Dichte). (4) Ladezeit bis Spielstart ≤ 15 s. (5) **Stabilitätstest:** 2 Stunden Dauerlauf ohne Absturz, Videospeicher/RAM stabil. (6) Alle drei Missionsketten ohne Blocker durchgespielt. (7) Economy-Simulator zeigt keine Engpässe (Fahrzeugkauf nach 3–4 Spielstunden). (8) Release-Checkliste (Kapitel 12.13) vollständig abgehakt.

### Teil B – Mehrspieler (erst nach Meilenstein 8)

#### Meilenstein 9 – Server-Fundament *(≈ 3 Wochen)*
- Einzelspieler läuft über `OfflineMultiplayerPeer`; **Dedicated Server** (`--headless`, Linux/Windows) mit 1 Realm und 1 Zone; ENet-Verbindung; Anmeldung und Charakter-Persistenz über Backend-Dienst; Prädiktion/Interpolation für Fahrzeuge.
- **Done:** (1) Einzelspieler-Regressionstest aus MS8 besteht unverändert über den Offline-Peer. (2) Zwei Clients fahren im selben Realm und sehen sich gegenseitig flüssig (≤ 150 ms Interpolationsverzug, keine Sprünge > 1 m bei 100 ms simulierter Latenz und 2 % Paketverlust). (3) Server-Neustart verliert keinen Charakterstand (Geld, Fahrzeuge, Standing, Flags).

#### Meilenstein 10 – Mehrspieler-Welt und Anti-Cheat *(≈ 5 Wochen)*
- Mehrere Zonenprozesse mit Handoff, Interessenverwaltung, serverseitiger Ambient-Verkehr in Spielernähe, Polizei-Dispatcher serverseitig, Story-Phasen pro Spieler, Plausibilitätsprüfungen, Rate-Limits, Chat.
- **Done:** (1) Lasttest mit 50 simulierten Spielern (headless Bots): Server-Tickzeit ≤ 30 ms (95. Perzentil), Bandbreite < 30 KB/s je Client. (2) Cheat-Testsuite: manipulierte Clients, die Teleport, Geld-Zuwachs oder Missionsabschluss melden, werden zu 100 % abgelehnt (automatische Tests). (3) Zwei Spieler spielen **verschiedene** Story-Stände im selben Realm ohne gegenseitige Spoiler.

#### Meilenstein 11 – Live-Ausbau *(≈ 5+ Wochen, offen)*
- Mehrere Realms, Fraktionskontrolle der Bezirke, Events/Saisons, Gruppen mit gemeinsamer Story-Phase, Telemetrie, Moderation, Datenschutz-/Betriebskonzept.
- **Done:** 100 Spieler im Realm stabil (Lasttest 2 h), Backups und Wiederherstellung getestet, DSGVO-Auskunft und -Löschung funktionieren.

### Abhängigkeitsfolge

```mermaid
flowchart LR
  M1["MS1 Fundament (Prototyp vorhanden)"] --> M2["MS2 Graph & Verkehr"]
  M2 --> M3["MS3 Bezirke & Look-Dev"]
  M3 --> M4["MS4 Streaming & große Welt"]
  M2 --> M5["MS5 Fahren & Polizei"]
  M4 --> M6["MS6 Missionen & Story-Engine"]
  M5 --> M6
  M6 --> M7["MS7 Wetter & Audio"]
  M7 --> M8["MS8 Einzelspieler-Vollversion"]
  M8 --> M9["MS9 Server-Fundament"]
  M9 --> M10["MS10 Mehrspieler & Anti-Cheat"]
  M10 --> M11["MS11 Live-Ausbau"]
```

MS4 und MS5 können parallel laufen (unterschiedliche Dateibereiche/Szenen), sofern zwei Personen/Sessions arbeiten und nicht dieselben Dateien berühren. **Vorarbeit für den Server** (Trennung Sim/Darstellung, serialisierbarer `GameState`, deklarative Story-Bedingungen) fällt bereits in MS1–MS6 an und kostet dort kaum Mehraufwand.

### Zuordnung zum Iterationsplan

| Iteration ([`ITERATIONSPLAN.md`](ITERATIONSPLAN.md)) | Entspricht Meilenstein |
|---|---|
| 1 – Prototyp lauffähig | Voraussetzung (erledigt) und Teil von MS1 |
| 2 – Spielgefühl und Details | MS5 (Fahren), Teile von MS7 (Sound, Licht) |
| 3 – Welt: Bezirke, Küste, Landmarken | MS3 und MS4 |
| 4 – Zu Fuß, Polizei, Missionen | MS5 und MS6 (Zu-Fuß-Spielen ist im Konzept ein **Ausbau** von M4/M5 und noch nicht eingeplant → als eigener Posten ergänzen, ca. 8–10 PT) |
| 5 – Modularisierung | MS1 (jetzt: Szenen/Autoloads statt Module; **vorziehen**, weil alle weiteren Meilensteine davon abhängen) |

**Empfehlung:** Iteration 5 (Struktur) vor Iteration 2–4 ziehen, sonst wird jede neue Funktion in `main.gd` gebaut und später doppelt angefasst.

---

## 15. Empfohlene Projektstruktur (Godot)

Ziel: von der einen `main.gd` auf eine **Szenen-/Autoload-Struktur** mit klar getrennten Ordnern. Der Projektordner ist `godot/` (`res://`); der Rest des Repositories (`docs/`, alter Browser-Prototyp) bleibt außerhalb. Die Struktur ist ein Vorschlag; Namen nach GDScript-Konvention (`snake_case` für Dateien).

```
E:\Projekte\GTA 6\
├─ docs/                          # WELTKONZEPT.md, ITERATIONSPLAN.md, TONFALL.md (Textleitfaden), BALANCING.md (Änderungsprotokoll), CHANGELOG.md
├─ index.html, game.js            # eingefrorener Browser-Prototyp (Referenz)
└─ godot/                         # res://  (project.godot liegt hier)
   ├─ project.godot               # Autoloads, Input-Map, Physik-/Render-Einstellungen, Global Shader Uniforms
   ├─ main.tscn                   # Einstiegsszene: lädt World, HUD, Kamera (dünn!)
   ├─ autoload/                   # Singletons (Project Settings → Autoload)
   │  ├─ event_bus.gd             # Signale: crime_committed, mission_completed, district_entered …
   │  ├─ game_state.gd            # GameState (serialisierbar)
   │  ├─ save_manager.gd          # Kapitel 11: Slots, Autosave, Migration, Prüfsumme
   │  ├─ config.gd                # Qualitäts-Preset, Budgets, Seed (aus Kommandozeile)
   │  ├─ rng.gd                   # Seed-Dienst (Seed je Chunk/Aufgabe)
   │  ├─ clock.gd                 # Tageszeit, Wetter-Zustandsautomat, Global Shader Uniforms
   │  ├─ audio_manager.gd         # Busse, Pools
   │  ├─ chunk_manager.gd         # M2: Chunk-Lebenszyklus, Worker-Threads
   │  ├─ traffic_system.gd        # M5: Agenten (Struktur-Arrays), MultiMesh-Anbindung
   │  ├─ police_dispatcher.gd     # M6
   │  ├─ aegis.gd                 # Kameras, Kennzeichen, Aegis-Profil
   │  ├─ mission_runner.gd        # M7
   │  ├─ story.gd                 # M14: Bedingungen/Effekte, Fraktionsstand, Enden
   │  ├─ dialog_runner.gd         # Dialog-Graph, Untertitel
   │  ├─ world_events.gd          # Bezirks-Events (Scheduler)
   │  ├─ radio_manager.gd         # Sender, Playlists, reaktive Nachrichten
   │  ├─ accessibility.gd         # Untertitel, Farbenblind, Hilfen
   │  ├─ telemetry.gd             # lokale Telemetrie (opt-in)
   │  ├─ net.gd                   # M15: Peer-Auswahl (Offline/ENet), Sitzung
   │  └─ debug_overlay.gd         # M12: F3-Overlay, Performance-Monitore
   ├─ scenes/                     # .tscn (Instanzierbare Bausteine)
   │  ├─ world/                   # world.tscn, chunk.tscn, district_zone.tscn, landmarks/*.tscn
   │  ├─ vehicles/                # car_player.tscn, car_npc.tscn, police_car.tscn, camera_rig.tscn
   │  ├─ characters/              # pedestrian.tscn, player_onfoot.tscn (später)
   │  ├─ props/                   # camera_aegis.tscn, traffic_light.tscn, …
   │  ├─ ui/                      # hud.tscn, minimap.tscn, fullmap.tscn, menu.tscn, dialog.tscn, options.tscn, journal.tscn, garage.tscn, photo_mode.tscn, character_creation.tscn
   │  └─ dev/                     # look_dev.tscn (Materialien/Beleuchtung), benchmark_flight.tscn
   ├─ scripts/                    # .gd (nach Modul gruppiert; sim/ ohne Mesh-/Material-Abhängigkeit)
   │  ├─ world/                   # generator.gd, city_data.gd, road_graph.gd, terrain.gd, districts.gd
   │  ├─ sim/                     # traffic.gd, pedestrians.gd, police.gd, law.gd, economy.gd, validation.gd
   │  ├─ vehicles/                # vehicle_body.gd, raycast_vehicle.gd, damage.gd
   │  ├─ render/                  # multimesh_builder.gd, decal_scatter.gd, quality_presets.gd
   │  ├─ ui/                      # hud.gd, minimap.gd, fullmap.gd
   │  ├─ net/                     # protocol.gd, prediction.gd, snapshots.gd (geteilt mit dem Server)
   │  └─ util/                    # math_utils.gd, spatial_hash.gd, pool.gd
   ├─ shaders/                    # .gdshader + .gdshaderinc
   │  ├─ ground.gdshader          # Straßen/Markierungen (aus dem Prototyp)
   │  ├─ building.gdshader        # Fassade, Fenster, Nachtlicht, wear
   │  ├─ wet_surface.gdshaderinc  # Nässe/Pfützen (include)
   │  ├─ sky.gdshader, water.gdshader, vehicle_paint.gdshader
   │  └─ common.gdshaderinc       # Konstanten über Global Shader Uniforms
   ├─ data/                       # reine Daten (Resources/JSON), keine Logik
   │  ├─ districts/               # core.tres, saltmarket.tres, lantern_hill.tres, greenmoor.tres, rustline.tres, sunstrip.tres
   │  ├─ vehicles/                # vehicle_def_*.tres (Datenblätter)
   │  ├─ materials/               # PBR-Materialbibliothek (.tres)
   │  ├─ missions/                # *.json (Schrittlisten)
   │  ├─ gigs/, events/, activities/   # Templates (Kapitel 10)
   │  ├─ dialogs/                 # Dialog-Graphen (6.9)
   │  ├─ skills/, modules/, items/     # Fortschritt, Implantate, Inventar (9.3, 12.3)
   │  ├─ radio/                   # Sender, Jingles, Nachrichtenpool, Werbung (9.6)
   │  ├─ story/                   # factions.json, npcs.json, missions/*.json, endings.json, text.de.json
   │  └─ economy.json             # Preise, Belohnungen
   ├─ assets/                     # Importquellen (nicht im Code referenziert, nur über Szenen/Ressourcen)
   │  ├─ audio/                   # OGG: Motorloops, Ambience, Musik
   │  ├─ textures/                # PNG/ORM-Maps, Decal-Atlas, Text-Atlas
   │  ├─ models/                  # .glb: Fahrzeuge, Props, Landmarken
   │  └─ LICENSES.md
   ├─ tests/                      # Headless-Tests (--headless --script oder GUT/gdUnit4)
   │  ├─ test_generator.gd        # Seed-Tests: kein Gebäude auf Straße, Graph verbunden
   │  ├─ test_savegame.gd
   │  ├─ story_sim.gd             # M14: alle Enden erreichbar, keine Sackgassen
   │  ├─ test_cheat.gd            # M15: manipulierte Eingaben werden abgelehnt
   │  ├─ test_graph_lint.gd       # Dialog-/Quest-/Flag-Graph-Prüfung (6.9)
   │  ├─ test_economy.gd          # Economy-Simulator
   │  └─ fixtures/                # alte Spielstände für Migrationstests
   ├─ server/                     # M15 (ab MS9): Einstieg und Konfiguration des Dedicated Servers
   │  ├─ server_main.tscn / .gd   # läuft headless, lädt nur sim/, world/, net/ (keine Render-Szenen)
   │  ├─ realm.gd, zone.gd        # Weltinstanz, Zonen, Handoff
   │  └─ persistence_client.gd    # HTTPRequest zum Backend (Charakter, Weltzustand)
   ├─ addons/                     # Drittanbieter-Add-ons (z. B. Test-Framework, Terrain3D – nach Prüfung)
   └─ export_presets.cfg          # Windows Desktop, Dedicated Server (Linux)
```

**Autoloads in `project.godot` (Reihenfolge = Ladereihenfolge):** `EventBus`, `Config`, `Rng`, `GameState`, `SaveManager`, `Clock`, `AudioManager`, `ChunkManager`, `TrafficSystem`, `PoliceDispatcher`, `Aegis`, `MissionRunner`, `Story`, `DialogRunner`, `WorldEvents`, `RadioManager`, `Accessibility`, `Telemetry`, `Net`, `DebugOverlay`. Autoloads, die Szenen/Rendering benötigen (Audio, Debug), werden im Server-Export weggelassen oder durch Stubs ersetzt (`OS.has_feature("dedicated_server")`).

**Wichtige Projekteinstellungen (zu setzen/prüfen):**
- `Rendering → Renderer`: **Forward+** (Voraussetzung für SDFGI, Cluster-Beleuchtung); `Rendering → Occlusion Culling`: an.
- `Physics → 3D → Physics Engine`: **Jolt** (Standard in neuen 4.6+-Projekten) vs. Godot Physics – bewusst entscheiden und am Fahrzeug testen; `physics_ticks_per_second`: 60 (ggf. 120).
- `Rendering → Global Shader Uniforms`: `world_half`, `block_pitch`, `night`, `wetness`, `time_of_day`, `wear_bias`.
- `Display → Window → Stretch`: Modus `canvas_items` für UI-Skalierung; V-Sync an.
- `Input Map`: nicht im Code anlegen, sondern im Projekt (der Prototyp legt sie in `_setup_input()` an; auslagern).
- **Export:** Windows-Desktop-Export mit Export-Templates; für den Server ein Export mit Feature-Tag `dedicated_server` (Linux) [Q32].

**Migrationsregeln:**
1. `scripts/sim/*`, `scripts/world/*` und `scripts/net/*` nutzen **keine** Mesh-/Material-/Kamera-/UI-Klassen, damit sie unverändert im headless Dedicated Server laufen.
2. **Kein Gott-Skript:** Die Wurzel von `main.tscn` enthält nur Instanzen und Verdrahtung; neue Funktionen kommen als Szene + Skript, nicht in `main.gd`.
3. **Strangler-Muster:** Funktionen blockweise aus `main.gd` herauslösen (zuerst Uhr/Umwelt → `clock.gd`, dann Fahrzeug → `car_player.tscn`, dann Verkehr, dann Stadt), jeder Schritt bleibt lauffähig; `main.gd` wird zur dünnen Verdrahtung.
4. **Typisierung:** GDScript-Typen (`var x: float`, `Array[Agent]`, `-> void`) verwenden – schneller und fehlerärmer.
5. **Versionskontrolle besteht bereits** (`.git`; `.gitignore` enthält `.godot/` und `*.import`). Konvention: ein Commit je abgeschlossenem Teilschritt; Exportordner und große Binärassets ausschließen bzw. später per Git-LFS (`assets/`).

---

## 16. Quellen

Die Recherche wurde per Websuche durchgeführt. Viele Treffer sind Blogs, Foren und Fachartikel unterschiedlicher Qualität; wo eine Quelle nur sekundär oder anekdotisch ist, ist das vermerkt. Godot-Dokumentationsseiten wurden in den Versionen 4.0–4.6 gefunden; die verlinkten „stable“-Adressen zeigen die aktuelle Version, Inhalte sind für **4.7.2** am Prototyp zu verifizieren. Konkrete Zahlen (Frame-Zeiten, Draw-Call-Budgets) sind **Richtwerte** und müssen auf der Zielhardware gemessen werden.

**Prozedurale Stadtgenerierung**
- [Q1] Parish, Y. & Müller, P.: *Procedural Modeling of Cities* (SIGGRAPH 2001) – Grundlagenarbeit (L-Systeme für Straßen, Parzellen, Gebäude). Zusammenfassungen u. a. in der Übersichtsarbeit der Purdue-Gruppe: https://www.cs.purdue.edu/cgvlab/papers/aliaga/uacs18.pdf
- [Q2] Chen, G., Esch, G., Wonka, P., Müller, P., Zhang, E.: *Interactive Procedural Street Modeling* (SIGGRAPH 2008) – Tensor-Felder für Raster-/Radial-/Küstenmuster: https://www.sci.utah.edu/~chengu/street_sig08/street_sig08.pdf
- [Q3] Aliaga, D. et al.: *Procedural generation of … urban analytics and city science* (Parzellen durch rekursive Teilung orientierter Rechtecke): https://www.cs.purdue.edu/cgvlab/papers/aliaga/uacs18.pdf
- [Q3b] *Procedural Urban Modeling of Population, Road Network and Land Use*: https://www.researchgate.net/publication/282283901_Procedural_Urban_Modeling_of_Population_Road_Network_and_Land_Use
- [Q3c] *Procedural generation as an approach for digital representation of a city* (Grammatik-Kette Straße → Block → Gebäude): https://link.springer.com/article/10.1007/s43762-026-00263-8

**KI-Ökosysteme (Verkehr, Fußgänger, Systemik)**
- [Q4] Game Developer: *AI Madness: Using AI to Bring Open-City Racing to Life* (Simulationsblasen, Verkehrsregeln trotz Kollisionen): https://www.gamedeveloper.com/programming/ai-madness-using-ai-to-bring-open-city-racing-to-life
- [Q5] Game Developer: *Bringing Washington D.C. to Life: The AI of The Division 2* (dynamische, systemische Open-World-KI): https://gamedeveloper.com/design/bringing-washington-d-c-to-life-the-ai-of-the-division-2
- [Q6] Game Developer: *How Watch Dogs: Legion's „Play as Anyone“ simulation works*: https://gamedeveloper.com/design/how-watch-dogs-legion-s-play-as-anyone-simulation-works · GDC Vault zu Assassin's Creed Unity (nicht spielerzentrierte Systemereignisse): https://www.gdcvault.com/play/1022211/contactUs *(nur Kurzbeschreibung gelesen)*
- [Q14] DEV Community: *Open-World Crowds – tiered simulation* (gestufte Detailtiefe nach Entfernung). **Sekundär/Erklärartikel, keine Entwicklerquelle** – nur als Beleg für das gängige Prinzip: https://dev.to/saeedjt/gta-6-npc-technology-how-open-world-crowds-are-actually-simulated *(Titel nennt ein kommerzielles Spiel; das Prinzip ist generisch, von dem Spiel wird nichts übernommen.)*

**Level-Design und Orientierung**
- [Q7] Disney-Prinzip „Weenie“/Landmarken im Level-Design, Landmarken-Hierarchie und Leitlinien: https://tancoque.design.blog/ und https://openskillindex.com/skills/vibeforge1111-vibeship-spawner-skills-level-design *(Blog-/Skill-Niveau, nicht peer-reviewed)*
- [Q15] Mission-Design-Checkliste (Zielbedingung, KI-Rollen, Checkpoints, Fehlschlagtext; „einfache KI, starke Reaktion“): https://app.studyraid.com/en/read/99643/4470902/game-design-document-mechanics-and-systems-outline *(Lehrmaterial)*

**Fahrphysik**
- [Q12] Monster, M.: *Car Physics for Games* (klassische Grundlage; Original-Verfügbarkeit unsicher) sowie Diskussion zu Pacejka-Reifenmodell und Niedriggeschwindigkeits-Problem: https://www.gamedev.net/forums/topic/612987-pacejka-tire-model-question/ · https://en.wikipedia.org/wiki/Hans_B._Pacejka
- [Q22] Godot-Dokumentation: *VehicleBody3D* (Raycast-Fahrzeug; Hinweis, dass die Klasse nicht für realistische Fahrphysik gedacht ist, eigene Umsetzung auf RigidBody3D als Alternative): https://docs.godotengine.org/en/stable/classes/class_vehiclebody3d.html
- [Q27] Godot-Dokumentation *Using Jolt Physics* (Jolt als Alternative seit 4.4) und Godot-Release 4.6 (Jolt als Standard für neue 3D-Projekte, Warnung vor abweichenden Simulationsergebnissen): https://docs.godotengine.org/en/stable/tutorials/physics/using_jolt_physics.html · https://godotengine.org/releases/4.6/ *(ob `VehicleBody3D` unter Jolt in 4.7.2 gleich robust ist, wurde nicht belegt)*

**Rendering, Streaming, Performance (Godot)**
- [Q23] Godot-Dokumentation *Optimization using MultiMeshes* (ein Draw Call für sehr viele Instanzen; einzelne Instanzen nicht cullbar; mehrere MultiMeshes je Weltbereich; Abschnitt als „in Arbeit“ markiert): https://docs.godotengine.org/en/stable/tutorials/performance/using_multimesh.html · Ergänzend (Drittquelle): https://www.strayspark.studio/blog/godot-3d-optimization-guide-2026
- [Q24] Godot-Dokumentation *Visibility ranges (HLOD)* (Fern-Stellvertreter, Visibility Parent, Fade-Modi, nur Alpha-Fading): https://docs.godotengine.org/en/stable/tutorials/3d/visibility_ranges.html
- [Q25] Godot-Dokumentation *Occlusion culling* (Projektsetting, `OccluderInstance3D`, Baking; Nutzen abhängig von der Kameraperspektive): https://docs.godotengine.org/en/stable/tutorials/3d/occlusion_culling.html
- [Q26] Godot-Dokumentation *Large world coordinates* (nur für Raum-/Planetensimulationen; Einzelpräzision für Open Worlds ausreichend; Origin-Shifting als Alternative): https://docs.godotengine.org/en/stable/tutorials/physics/large_world_coordinates.html
- [Q29] Godot-Dokumentation *Signed distance field global illumination (SDFGI)* (anspruchsvollstes GI-Verfahren, nur Forward+, Hinweise zu Cascades, Probe-Strahlen, Halbauflösung): https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/using_sdfgi.html
- [Q30] Godot-Dokumentation *Resolution scaling* (FSR 2.2 seit 4.2, langsamste/beste Option, Skalierung pro Achse): https://docs.godotengine.org/en/stable/tutorials/3d/resolution_scaling.html
- [Q28] Godot-Dokumentation *Optimizing navigation performance* (Kosten von Avoidance und Pfadabfragen, unerreichbare Ziele): https://docs.godotengine.org/en/stable/tutorials/navigation/navigation_optimizing_performance.html · Forum: https://forum.godotengine.org/t/godot-terrible-performance-on-navigationagent3d/64567 *(anekdotisch)*

**Audio**
- [Q16] Google web.dev: *Case Study – The Sounds of Racer* (Motorklang aus wenigen Loops, Crossfades, sample-genaues Looping – als Gestaltungsprinzip, auf Godots Audio-Bus-System übertragen): https://web.dev/case-studies/racer-sound

**Server und Mehrspieler (Godot)**
- [Q31] Godot-Dokumentation *High-level multiplayer* (ENet, WebSocket, WebRTC; RPCs, Autorität): https://docs.godotengine.org/en/stable/tutorials/networking/high_level_multiplayer.html · Godot-Artikel *Multiplayer in Godot 4.0: Scene Replication*: https://godotengine.org/article/multiplayer-in-godot-4-0-scene-replication/
- [Q32] Praxisleitfäden zu Dedicated Servern in Godot 4 (Headless-Export, Feature-Tag `dedicated_server`, Warnung vor ausschließlichem `MultiplayerSynchronizer`-Einsatz, Eingaben statt Positionen): https://gsb.supercraft.host/blog/godot-multiplayer-dedicated-server-backend/ *(Blog-Niveau; offizielle Exportdokumentation zusätzlich prüfen)*
- [Q33] Allgemeine Netzcode-Praxis für Tickraten und Interessenmanagement (Snapshot-Rate 15–20 Hz üblich): https://abratabia.com/multiplayer-web-games/websockets.php *(Blog-Niveau, engine-unabhängig)*
- [Q20] Gambetta, G.: *Fast-Paced Multiplayer (Part II): Client-Side Prediction and Server Reconciliation*: https://gabrielgambetta.com/client-side-prediction-server-reconciliation.html
- [Q21] Gambetta, G.: *Fast-Paced Multiplayer (Part III): Entity Interpolation*: https://gabrielgambetta.com/entity-interpolation.html · Teil IV (Lag-Kompensation): https://gabrielgambetta.com/lag-compensation.html

**Speichern, Barrierefreiheit, Design**
- [Q34] Godot-Dokumentation *Saving games* (Speichern per `FileAccess`/JSON, `ConfigFile` für Einstellungen): https://docs.godotengine.org/en/stable/tutorials/io/saving_games.html · Praxisleitfaden: https://gdquest.com/library/save_game_godot4 *(Sekundärquelle; Code-Details der Doku-Seite nicht im Wortlaut geprüft)*
- [Q35] Game Accessibility Guidelines (Maßnahmen nach Aufwand gestuft; Untertitel, Neubelegung, Schwierigkeitsgrade, getrennte Lautstärken): https://gameaccessibilityguidelines.com/basic/ *(Zuordnung einzelner Punkte, z. B. Farbenblind-Modus, zur Stufe nicht verifiziert)*
- Kapitel 9 (Design-Vorbilder) beruht auf **allgemeinem Genre-Wissen**; es werden keine Inhalte der Vorbilder verwendet und keine Quellen dazu zitiert.

**Nicht belegt / ausdrücklich offen:**
- Verhalten von **4.7.2** im Detail (Dokumentation recherchiert für 4.0–4.6).
- **VehicleBody3D unter Jolt**, Fahrzeug-Prädiktion mit `RigidBody3D` im Netzwerk.
- **SDFGI mit gestreamten Chunk-Meshes** (Neuberechnung/Flackern) und reale GPU-Kosten auf der RX 5700 XT – alle Frame-Zeiten in Kapitel 5.1 sind Schätzungen.
- **Volumetrischer Nebel** (Kosten/Einstellungen), **`CompositorEffect`**, **Terrain3D**-Add-on und **godot-sqlite**: nicht recherchiert.
- Nässe-/Pfützen-Shader in Godot, Zonen-Handoff bei Godot-Servern, tatsächliche Realm-Obergrenze (Spielerzahl), GDScript-Leistung bei Hunderten Agenten – jeweils am Prototyp bzw. per Lasttest zu validieren.
- Sicherheit beim Laden fremder Godot-Ressourcen (`.tres/.res`): nur Sekundärquelle; deshalb Spielstände ausschließlich als JSON.
- Story-Struktur („Perlenkette“), Ruf-/Aegis-System und Anti-Cheat-Liste sind eigene Entwürfe auf Basis allgemeiner Praxis, keine übernommenen Fremdkonzepte.

---

## 17. Zusammenfassung und die 5 nächsten Schritte

**Kurzfassung:** MTA 6 wird in **Godot 4.7.2** (GDScript, Forward+/Vulkan) zu einer eigenständigen, dystopisch-realistischen Küstenmetropole (**Kestrel Bay**, sechs Bezirke zwischen Konzernturm und Schrottgürtel, dazu Umland mit Küstenstraße, Bergen, Highway und Tunnel). Die Welt entsteht als **Hybrid aus handgesetzten Landmarken/Hauptachsen und prozeduraler Füllung** über einen gemeinsamen Straßengraph; technisch tragen **Chunk-Streaming mit `MultiMeshInstance3D`, Visibility Ranges/HLOD, Occlusion Culling und Sim-LOD** für Verkehr und Passanten. Realismus kommt aus einer disziplinierten Pipeline (PBR, Decals, Verschleiß, Nässe, Wetter) innerhalb eines **Frame-Budgets für die RX 5700 XT** (SDFGI nur optional im Preset „Hoch“, FSR 2.2 zur Skalierung). Die Story **„Nullstunde“** reagiert über Fraktionsstand, NPC-Vertrauen, Taten-Flags und Weltzustand mit fünf Wendepunkten und fünf Enden; sie ist als Daten modelliert und per Simulator testbar. Ein **autoritativer Dedicated Server (Godot headless, ENet)** mit Zonen und Realms macht daraus später eine geteilte Welt, in der die Story pro Spieler privat bleibt. Differenzierung: **Tagesrhythmus pro Bezirk**, **Bezirks-Ruf** und das **Aegis-Überwachungsprofil** als spielbare Dystopie.

Das Ganze besteht aus 15 Modulen, den Zusatzkapiteln zu Design-Vorbildern, Missionstypen, Speichern und Produktion sowie 11 überprüfbaren Meilensteinen; **Meilenstein 1 ist größtenteils bereits durch den Godot-Prototyp abgedeckt**, der Einzelspieler wird in MS1–MS8 zuerst stabil, Mehrspieler folgt in MS9–MS11. Aufwand der MVPs insgesamt ca. **126 Personentage** (±50 %) inklusive Spielsysteme, Design-Vorbilder und Prototypfortschritt (Kapitel 13); der Prototyp läuft laut Prototyp-Session mit ca. 160 FPS auf der RX 5700 XT.

### Die 5 wichtigsten nächsten Schritte

1. **Prototypstand sichern:** Die nicht eingecheckten Änderungen (Polizei, Fußgänger, Mission, Minimap) committen, `godot/README.md` aktualisieren – danach MS1 starten.
2. **`main.gd` in Szenen und Autoloads zerlegen** (Kapitel 15): `EventBus`, `Config`/`Rng`, `Clock`, Fahrzeug-Szene, `TrafficSystem`; Global Shader Uniforms statt doppelter Konstanten; Verhalten unverändert.
3. **Messgrundlage schaffen:** Debug-Overlay (F3) mit `Performance`-Monitoren, Benchmark-Flug und Qualitäts-Presets; erste echte Frame-Zeiten auf der RX 5700 XT festhalten (ersetzen die Planwerte aus Kapitel 5.1), inklusive Test **Jolt vs. Godot Physics** für `VehicleBody3D`.
4. **Gebäude auf `MultiMeshInstance3D` je Chunk umstellen** und den **Straßengraph (`CityData`)** einführen; Verkehr darauf umstellen (MS2) – Grundlage für KI, Karte, GPS, Missionen und später die Zonen.
5. **Story-Datenmodell und Bezirks-Definitionen festschreiben** (`data/story/`, `data/districts/`) und eine **Look-Dev-Szene** mit 12 PBR-Materialien aufbauen – damit Stimmung, Dystopie und Realismus früh am Bild geprüft werden, bevor Inhalte in Masse entstehen.
