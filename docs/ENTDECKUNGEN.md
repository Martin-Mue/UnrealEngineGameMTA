# MTA 6 – Entdeckungen, Weltereignisse, Orientierung (Paket K)

Stand: 21.11.2026 · Daten: `Content/Data/secrets.json`, `Content/Data/world_events.json`, `Content/Data/wilderness.json` · Code: `Source/MTA6/MTADiscovery*`, `MTAWorldEventSubsystem`, `MTACollectibleComponent`, `MTAWildernessRegion`

## 1. Leitidee

Kestrel Bay belohnt Neugier, nicht Abarbeiten. Jeder Bezirk hat **16 handgeschriebene Überraschungen** mit eigenem Charakter, die Karte zeigt sie **erst nach dem Fund**. Hinweise kommen aus der Welt: Geräusche, Gerüchte, Radio, Notizen, Gerüche, Licht. Der Ton ist ironisch-wehmütig: Die Dystopie ist ernst, aber in ihren Ritzen wächst etwas (Apfelbäume auf Bürotürmen, ein Hund, der Leute aus dem Wasser holt, ein Klavier um drei Uhr nachts).

| Bezirk | Charakter der Geheimnisse | Beispiele |
|---|---|---|
| Kestrel Core | Was unter Glas und Kontrolle überlebt: Gegenzählung, vergessene Infrastruktur, leise Rebellion | Glasobstgarten (44. Stock), Zählwerk-ARG, nie eröffnete U-Bahn, Namen hinter der Werbung |
| Saltmarket | Gedächtnis der Arbeit: Schiffe, Schichten, Schulden, die erlassen wurden | Geisterschiff Morgenrot, Flaschenhöhle, Schichtchor, Fragenmarkt um vier |
| Lantern Hill | Widerstand als Handwerk: Kreide, Bleisatz, Glocken, Musik | Glockencode (4 Schritte), Hinterhofkonzert, Bibliothek in den Mauern, Katzenparlament |
| Greenmoor | Satire der Angst, und was hinter den Hecken trotzdem lebt | Bunker des Gründers mit Cabrio, Kreidedrache durch den Zaun, Musterhaus, Füchsin unter dem Clubhaus |
| Rustline | Hässlich und ehrlich: Schrott, Feuer, Krähen, Freiheit | Die Elster (versteckte Werkstatt), Sprung über den Güterzug, Nachtmarkt unter der Brücke, Krähenkönig |
| Sunstrip | Die Rückseite der Postkarte | Gondel 13 im Riesenrad, Stadt unter dem Pier, Neonfriedhof, Rafes Akkordeon zum Sonnenuntergang |

## 2. Zahlen

| | Anzahl |
|---|---|
| Entdeckungen Stadt | 96 (je Bezirk 16) |
| Entdeckungen Wildnis Verdanta | 20 (12 Planung + 8 aus dem Wildnis-Builder) |
| Arten | 22 (Dachgarten, Wandbild, Aussichtspunkt, Tunnel, Stunt, verlassene Station, Hacker-Terminal, Konzert, Schmugglerhöhle, Schwarzmarkt, Stadtlegende, Tier, Echo, Zeitkapsel, Liebesbrief, Geisterschiff, versteckte Werkstatt, Gedenkort, Kuriosität, Ruine, Wasserfall, Höhle) |
| Rätselketten | 11 (6 bezirksintern, 3 stadtweit, 1 Zeitkapsel, 1 Wildnis) |
| Sammlungen | 7 (6 Bezirke + Verdanta, zusammen 72 Objekte) |
| Landmarken | 10 (3 Hierarchiestufen) |
| Weltereignisse | 20 |

### Rätselketten

| Kette | Schritte | Verlauf | Abschluss |
|---|---|---|---|
| `core_counter` Gegenzählung | 3 | Parkuhr → Lampe ohne Schatten (nachts) → warmer Raum (Schlüssel aus Schritt 2) | Skillpunkt Technik, Aegis −5, Abzeichen |
| `ghost_ship` Die Morgenrot | 3 | Nebel + Nacht am Pier → Register → Wrack (Taucherbrille) | Item `morgenrot_contracts` (Story-Hook Brann), Union +5 |
| `bell_code` Glockencode | 4 | Wandbild → dunkle Stufe (nachts) → Glockenturm um 2:14 → Druckerei | Skillpunkt, Collective +8 |
| `hedge_maze` Die Angst des Gründers | 3 | Pflanzliste → Statue → Bunker | Fahrzeug `veh_founder_cabrio` |
| `last_racer` Die Elster | 3 | Foto → Haube auf dem Schrottberg → Werkstatt | Fahrzeug `veh_magpie`, Wrenchers +15 |
| `wheel_legend` Gondel 13 | 3 | Nummerierung → Schaltplan (morgens) → Achse um Mitternacht bei klarem Wetter | Unterschlupf, Outfit |
| `love_letters` Briefe an R. | 6 | Core → Saltmarket (17 Uhr) → Lantern → Greenmoor → Rustline → Sunstrip (Sonnenuntergang) | gelber Schal, Radiotitel, 1000 $ |
| `aegis_echo` Echos | 6 | je Bezirk ein Echo um 2–4 Uhr | Item `echo_coordinates` → führt nach Verdanta |
| `lantern_dog` Der Laternenhund | 4 | Netzflickerin → Treppe im Nebel → Pfoten im Ruß → Strandhütte | Begleiter-Kosmetik `companion_lantern_dog` |
| `time_capsule_9b` Klasse 9b | 2 | Tafel → Eiche jenseits des Zauns | Collective +3 |
| `verdanta_wurzelwerk` | 5 | Gewächshäuser → Halsbandbaum → Feldstation → Höhle → Der Keim | Aegis −20, 2 Skillpunkte |

Regel: Schritt n ist unsichtbar, bis Schritt n−1 gefunden ist; der Fundtext von n−1 ist der Hinweis auf n (Zustand „angedeutet“). Benötigte Items stammen immer aus einem früheren Schritt oder einer anderen Entdeckung (vom Validator geprüft).

## 3. Datenformat (`secrets.json`)

```json
{"id": "lantern_bell_3", "district": "lantern", "type": "mural", "title": "Um 2:14 im Glockenturm",
 "chain": {"id": "bell_code", "step": 3},
 "pos": {"block": [3, 7], "at": [0.5, 0.6], "heightM": 38, "radiusM": 4},
 "discover": "interact",
 "visibleWhen": [], "unlockWhen": [{"item": "bell_code_time"}, {"hour": [2.1, 2.5]}],
 "hints": [{"kind": "sound", "cue": "sfx_single_bell", "radiusM": 300, "text": "…"}],
 "rewards": [{"type": "item", "id": "press_key"}],
 "lore": "…", "accent": {"color": "#E0A84A", "light": "landmark", "sightline": "…"}}
```

- **Position (Parzellenraster):** `block = [bu, bv]` (bu West→Ost = Unreal +Y, bv Süd→Nord = Unreal +X, 0..7 wie `AMTACityGenerator`), `at` = Lage im Block 0..1, `heightM` über Gehweg. Ein Level-Actor mit **Tag = id** (oder `anchorTag`) überschreibt die Rasterposition, das ist der empfohlene Weg fürs Level-Design.
- **Bezirkszuordnung** folgt `MTADistrict::ClassifyNormalized` an der Blockmitte (wie der Generator): Core bu 2–5/bv 2–5, Saltmarket bu 0–1/bv 2–4, Sunstrip bu 0–1/bv 5–7, Lantern bu 2–4/bv 6–7, Greenmoor bu 5–7/bv 6–7 und bu 6–7/bv 4–5, Rustline bv 0–1 und bu 6–7/bv 2–3. Bei `NoisyLayout` können Randblöcke kippen, dann Anker-Actor setzen.
- **Auslösung:** `proximity`, `dwell` (Verweilen, z. B. Sonnenuntergang), `interact`, `photo` (Fotomodus, Blickkegel 15°), `script`.
- **Bedingungen:** Story-Schema 1:1 (`standing`, `npcTrust`, `flag`, `aegis`, `done`, `tag`, `any`, `all`, `not`) plus `hour [von, bis]` (über Mitternacht erlaubt), `weekday [0=Mo..6]`, `weather [clear|overcast|rain|storm|fog]`, `item`, `discovered`, `event`, `heat [bezirk, op, wert]`, `collection [id, op, n]`, `regionState [key, op, wert]`.
- **Belohnungen:** `money`, `vehicle`, `skillPoint` (`driving|tech|street|stealth`), `lore`, `standing`, `cosmetic`, `item`, `aegis`.

## 4. Orientierung und Blickfang

- **Landmarken-Hierarchie** (Weltkonzept 8.4): Stufe 1 Aurelia Tower; Stufe 2 Kranreihe, St. Merrin, Wasserturm, Raffineriefackel, Kestrel Wheel; Stufe 3 Nachbarschaftsmarken. Daten mit Nachtlicht und Sichtlinienregel in `secrets.json → landmarks`, abrufbar über `GetLandmarks(District)`.
- **Blickfang-Regeln** (`blickfangRules`): ein Akzent in der Bezirks-Gegenfarbe je Ort; Sichtlinie auf ein Detail (Leiter, Lichtfleck), nie auf das Geheimnis; drei Brotkrumen (Geräusch, Umweltdetail, Gerücht); gerahmter Belohnungsblick; keine Minimap-Marker vor dem Fund; Zeit-Spuren außerhalb des Zeitfensters; ein Drittel nicht auf Straßenniveau.
- **Hinweis-Hooks ohne Kartenspam:**
  - Audio: `GetSoundHooksNear(Location, Radius)` → SoundCue, Position, Radius. Wer den Hörradius betritt, löst „angedeutet“ aus (`OnSecretHinted`).
  - NPC/Radio: `GetRumors(District, Max)` liefert Gerüchte zu sichtbaren, ungefundenen Orten; nach dem Erzählen `MarkHintHeard(Id)`.
  - Sammlungen: `UMTACollectibleComponent` (Kugel-Trigger) an beliebigen Actors, Fortschritt `GetCollectionProgress`.

## 5. Weltereignisse (`world_events.json`)

20 Ereignisse: Fest der Laternen, Razzia, Blackout-Viertel, Rennnacht, Demonstration, Unfall mit Hilferuf, Sturm, Geldtransporter-Überfall, Hafenstreik, Aegis-Wartungsnacht, Neon-Nachtmarkt, Heringsschwemme, Sicherheitssperre Greenmoor, Werbedreh, Chemischer Nebel, Hitzewelle, Wohlfühllauf, Netzpriorität, Megaschiff, Drohnenregen.

Jedes Ereignis hat Auslöser (Wochentag, Stundenfenster, Wetter, Story-Bedingungen, Chance je Spielstunde, Cooldown), Dauer, Bezirk(e) und **Konsequenzen** als Faktoren: Verkehr, Fußgänger, Polizei, Heat-Zuwachs, Kameras, Licht, Sicht, Ampeln, Preise je Warengruppe, Straßensperren, Welt-Tags. Dazu Spieleroptionen mit Belohnungen und Hinweise (Radio, Geräusch, Geruch).

Abfrage (Pull-Prinzip, das Subsystem schreibt nicht in fremde Systeme):
```cpp
const FMTADistrictModifiers M = GetGameInstance()->GetSubsystem<UMTAWorldEventSubsystem>()->GetDistrictModifiers(TEXT("rustline"));
TrafficDensity *= M.Traffic;   // KI
HeatAmount     *= M.HeatGain;  // vor UMTAHeatSubsystem::AddHeat
Price          *= M.Prices.FindRef(TEXT("fuel")) > 0.f ? M.Prices.FindRef(TEXT("fuel")) : 1.f; // Wirtschaft
```

## 6. Anbindung (für Paket N)

```cpp
// Speichern (SaveManager / UMTASaveGame): zwei neue FString-Felder vorschlagen
Save->DiscoveryJson  = GI->GetSubsystem<UMTADiscoverySubsystem>()->ToJson();
Save->WorldEventJson = GI->GetSubsystem<UMTAWorldEventSubsystem>()->ToJson();
// Laden
Disc->FromJson(Save->DiscoveryJson);  Events->FromJson(Save->WorldEventJson);

// Zeit/Wetter (sonst Stunde vom AMTA6GameMode, Wetter "clear")
Disc->SetEnvironment(Hour, DayIndex, TEXT("fog"));

// Belohnungen pro Frame/Sekunde abholen (GameMode)
Money += Disc->ConsumeMoney();
const FString Effects = Disc->ConsumeStoryEffectsJson(); // [{"standing":["union",5]},{"aegis":-3}]
// -> braucht eine öffentliche Story-Funktion, z. B. UMTAStorySubsystem::ApplyEffectsJson(Effects)

// Interaktion / Fotomodus
Disc->TryInteract(PawnLocation);
Disc->ReportPhoto(CameraLocation, CameraForward);

// HUD
Disc->OnSecretFound.AddDynamic(this, &AMTAHUD::HandleSecretFound); // Progress.Summary = "Entdeckt 12 von 16"
```

## 7. Validierung

Node-Skript (Scratchpad, `validate_secrets.js <Projektpfad>`): JSON-Syntax, eindeutige IDs (auch gegen Ereignisse), gültige Bezirks-IDs, Block liegt im angegebenen Bezirk, Bedingungsschlüssel bekannt, `done`/`flag`/Fraktionen existieren in `story.json`, jedes geforderte Item wird irgendwo vergeben (oder ist als extern gelistet), Ketten lückenlos 1..n, kein Item aus einem späteren Kettenschritt, Belohnungstypen gültig, Ereignisse mit Verkehr/Heat und gültiger Dauer. Ergebnis 21.11.2026: **keine Fehler**.
