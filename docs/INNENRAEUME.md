# MTA 6 – Begehbare Innenräume (Unreal Engine 5.8)

Ziel: In Kestrel Bay kann man in Häuser hineingehen. Jeder Bezirk hat eigene Innenraum-Stimmung (Konzernglanz im Core, Arbeiterwohnungen in Saltmarket, warme Altstadt in Lantern Hill, Gated-Villen in Greenmoor, Schrott und Natriumlicht in Rustline, Neon in Sunstrip). Innenräume entstehen **bei Bedarf** aus Daten, nicht als handgebaute Levels.

## 1. Architekturentscheidung

### Verglichene Varianten

| Kriterium | (a) Versteckte Innenraum-Zellen („Interior Grid“) + Teleport mit Überblende | (b) Innenraum maßstabsgetreu in den Außenbau |
|---|---|---|
| Passform | Grundriss unabhängig vom Außenvolumen (Godot-Parzellen 22–54 m, Fassaden-Shader ohne echte Wände) | Innenraum muss exakt in Fassade, Fenster und Geschosshöhen der Außengebäude passen; die Außengebäude sind heute ISM-Quader mit Shader-Fenstern |
| Generator | Weltgenerator (`MTACityGenerator`) bleibt unberührt | Außenbau müsste hohl, mit Decken, Treppenhäusern und Türöffnungen erzeugt werden |
| Leistung | Zelle existiert nur nahe dem Spieler; Außenstadt bleibt ISM/HLOD | Jedes betretbare Gebäude trägt dauerhaft Innengeometrie, Lichter und Kollision; Lichtlecks durch Fassaden |
| Licht | Geschlossene Zelle, eigenes Licht-Budget, Fensterlicht simuliert | Sonnenlicht und Lumen zwischen innen und außen kontrollieren, schwierige Belichtung |
| Streaming | Zelle direkt **unter** dem Gebäude: World Partition hält die Umgebung geladen, Rückweg ohne Ladezeit | Jedes Gebäude ist eine eigene Streaming-Einheit mit innen/außen-Abhängigkeiten |
| Nahtlosigkeit | Kurze Überblende (0,35 s + 0,5 s); kein Durchblick vom Fenster auf die echte Straße (Kulisse) | Nahtlos sichtbar, Blick durch Fenster ins Freie |
| Mehrspieler (M15) | Zelle ist eigener Raum; Sichtbarkeit pro Zelle einfach | Volle Relevanz innen/außen |
| Risiko | gering, alles Daten + Primitive | hoch: Generator-Umbau, Kollisions-/Lichtprobleme, Pop-in |

**Entscheidung: Variante (a).** Sie ist robust, unabhängig vom parallel entwickelten Weltgenerator und skaliert über Daten (56 Vorlagen heute, beliebig mehr). Der Nachteil (Überblende, Fensterkulisse statt echter Straße) ist im Genre akzeptiert und lässt sich später für einzelne Showcase-Gebäude mit handgebauten Level Instances (Variante (b) punktuell) ergänzen: `ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr` ist dafür der vorgesehene Anschluss (siehe Abschnitt 8).

### Interior Grid

* Zellen liegen **senkrecht unter dem zugehörigen Gebäude** bei `InteriorGridDepth` (Standard −600 m), weitere Zellen in Ebenen mit `LayerSpacing` (50 m) Abstand. 50 m > größter Lichtradius (15 m), daher kein Durchleuchten zwischen Ebenen.
* Gleiche X/Y wie das Gebäude bedeutet: Die 2D-Streaming-Quellen von World Partition halten die Außenwelt geladen, die Rückkehr ist sofort möglich, Audio/Verkehr außen können weiterlaufen.
* `AllocateLayer` sucht die erste Ebene, in der das X/Y-Rechteck (plus 5 m Rand) frei ist.

## 2. Dateien

| Datei | Inhalt |
|---|---|
| `Source/MTA6/MTAInteriorTypes.h/.cpp` | Enums (Eingangsarten, Sperrzustände, Interaktiv-Arten), `FMTAAccessRule`, `FMTANPCSlot`, Delegates (`FOnSecretFound(FName Id)`, Hack, Verstecke), Schnittstellen `IMTAEnterable`, `IMTAInteriorUsable`, `IMTAKeyHolder`, `IMTAHackable` |
| `Source/MTA6/MTAFurnishing.h/.cpp` | Möbelbibliothek aus `furnishing.json`: Objekte aus Primitiven, Wohnstile, Füllregeln je Raumart, JSON-Helfer |
| `Source/MTA6/MTAInteriorData.h/.cpp` | Vorlagen-Datenbank aus `interiors.json`: Raumtypen, Läden, Grundrisse, Zugangsregeln, Vorlagenwahl je Bezirk |
| `Source/MTA6/MTAInteriorBuilder.h/.cpp` | Reine Berechnung: Wände/Türen/Fenster aus dem Kantenraster, Böden/Decken mit Treppenlöchern, Treppen, Lichter, Möblierung, NPC-Slots, Interaktive |
| `Source/MTA6/MTAInteriorInstance.h/.cpp` | `AMTAInteriorInstance`: ISM je (Form, Ebene), Punkt-/Rect-Lichter, Fensterkulisse, Schlafmodus, Flackern, Laden-Komponente |
| `Source/MTA6/MTAInteriorInteractive.h/.cpp` | `AMTAInteriorInteractive`: Tür, Schalter, Terminal (Hack-Hook), Versteck, Geheimnis, Ausgang |
| `Source/MTA6/MTAInteriorSubsystem.h/.cpp` | `UMTAInteriorSubsystem` (Tickable World Subsystem): Bau bei Bedarf, Vorwärmen, Aufräumen, Übergänge, Tageszeit, Hack/Geheimnisse, Speichern |
| `Source/MTA6/MTABuildingEntry.h/.cpp` | `AMTABuildingEntry`: Eingänge am Außengebäude, Zugangsprüfung, Fahrzeug-Garagen, `SpawnForLot` für den Weltgenerator |
| `Source/MTA6/MTAShopComponent.h/.cpp` | `UMTAShopComponent`, `IMTAPriceProvider`, Öffnungszeiten, Lager, Ladenbesitzer-NPC-ID |
| `Content/Data/interiors.json` | 14 Raumtypen, 10 Läden, 56 Gebäudevorlagen, Bezirksstile, Baumaße |
| `Content/Data/furnishing.json` | 209 Möbel-/Dekoobjekte, 7 Wohnstile, 35 Raumarten mit Füllregeln |
| `tools/ue_interiors/build_data.py` | Autorenwerkzeug, erzeugt beide JSON-Dateien |
| `tools/ue_interiors/validate_interiors.py` | Prüfung: Raster, Überlappung, Türen auf gemeinsamer Wand, Fenster auf Außenwand, Treppen, Erreichbarkeit, Querverweise |
| `tools/ue_interiors/preview_interiors.py` | ASCII- und SVG-Vorschau der Grundrisse |

Python: Systempython fehlt auf dem Arbeitsrechner, daher den Interpreter der Engine benutzen:
`F:/UnrealEngine/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe tools/ue_interiors/validate_interiors.py`

## 3. Datenformat

### interiors.json (Längen in Metern, Raster 0,5 m)

```json
{ "id": "salt_dock_bar", "roomType": "bar_club", "label": "Kneipe Zum Ankerkettenriss",
  "districts": ["Saltmarket"], "style": "worker", "shop": "bar_drinks", "footprint": [16, 14],
  "access": { "open": [18, 4], "hack": 1, "faction": "union", "minStanding": 0, "key": "key_x" },
  "floors": [ { "level": 0, "height": 3.2,
    "rooms":   [ { "id": "bar", "kind": "bar", "rect": [0, 0, 16, 7.5] } ],
    "doors":   [ { "rooms": ["bar", "outside"], "at": [5, 0], "width": 2, "entrance": "front" },
                 { "rooms": ["bar", "backhall"], "at": [10, 7.5], "width": 1, "lock": { "key": "k", "hack": 2 } } ],
    "windows": [ { "room": "bar", "at": [12, 0], "width": 2 } ],
    "stairs":  [ { "room": "hall", "rect": [6, 8, 2, 4], "toLevel": 1, "axis": "y" } ] } ],
  "props": [ { "item": "jukebox", "floor": 0, "room": "bar", "at": [2, 6], "rot": 90 } ],
  "interactives": [ { "kind": "terminal", "room": "office", "at": [14, 9], "hack": 2, "links": "secret_id" } ],
  "secrets": [ { "id": "salt_dock_bar_cache", "room": "bar", "at": [13, 6], "kind": "collectible", "hidden": false } ],
  "npcSlots": [ { "room": "bar", "at": [8, 3], "rot": 0, "activity": "bartender", "role": "patron", "hours": [18, 4], "chance": 0.9 } ] }
```

* `rect` = `[x, y, Breite, Tiefe]`, lokale X/Y-Achse = Unreal X/Y der Zelle.
* Türen liegen auf der gemeinsamen Wand; `"outside"` = Außenwand; `entrance` ∈ `front, back, roof, emergency, garage`; `vehicles: true` für Garagentore (Breite ≥ 3 m wird als Tor mit bis zu 4 m Höhe gebaut).
* `lock` erzeugt ein Türblatt (interaktiv, Schlüssel und/oder Hack).
* `access.open` fehlt oder `null` = keine Öffnungszeiten (nur Schlüssel/Hack/Fraktion).

### furnishing.json

* `items[]`: `id`, `category`, `mount` (`floor|wall|ceiling|surface`), `footprint` `[Tiefe, Breite]` (Front = lokale +X), `parts[]` mit `shape` (`cube|cylinder|sphere|cone|plane`), `pos` (Mitte), `size`, `rot` `[pitch, yaw, roll]`, `color` (Hex, sRGB), `emissive`; optional `light`, `activity` (erzeugt NPC-Slot), `interactive` (`hiding|terminal|switch`), `styles` (erlaubte Stil-Tags), `tags` (`trash`).
* `styles[]`: Wohnstil mit `tags`, `wear`, `clutter`, `trash`, `plants`, `neon`, `flicker`, Wand-/Boden-/Deckenfarbe, Lichtfarbe und -dichte.
* `roomKinds[]`: `{ "kind": "...", "rules": [ { "pick": [...], "place": "wall|corner|center|scatter|wallDecor|ceiling|surface", "count": [min, max], "chance": 0.7, "clutter": true } ] }` (Array statt Objekt-Map, damit kein Iterieren über `FJsonObject::Values` nötig ist).

### Bezirksstile

| Bezirk | Stil | Charakter |
|---|---|---|
| Kestrel Core | `corporate` | kaltweißes Licht, LED-Panels, Aegis-Kameras, Glas, sauber |
| Saltmarket | `worker` | warmes Glühlicht, Gewerkschaftsplakate, etwas Unordnung |
| Lantern Hill | `bohemian` | Ocker, Kerzen, Lichterketten, Pflanzen, Widerstandsplakate |
| Greenmoor | `suburban` | hell, sauber, Teppiche, Gemälde, kein Müll |
| Rustline | `industrial` (Slum-Vorlagen: `slum`) | Natriumorange, flackernde Röhren, Müll, Pfützen, Verschleiß 0,75–0,9 |
| Sunstrip | `neon_tourist` | Neonschilder, Palmen, Pastell |

Verschleiß (`wear`) dunkelt und verschmutzt Farben, kippt Kisten leicht; `clutter` skaliert Kleinkram; `trash` streut Müll je Raumfläche.

## 4. Laufzeit-Ablauf

1. `AMTABuildingEntry` (am Außengebäude) meldet sich beim Subsystem an, wählt per `PickTemplateId(District, RoomType, Seed, LotSize)` eine Vorlage und legt Eingänge passend zur Vorlage an (Vorder-/Hintertür, Dach, Notausgang, Garage).
2. Nähert sich der Spieler auf `PrewarmRadius` (35 m), baut das Subsystem den Innenraum im Voraus (max. einer pro Aktualisierung, schlafend).
3. Interaktion: `IMTAEnterable::Enter(Pawn)` → Zugangsprüfung → `EnterInterior` → Kamera-Überblende → Teleport zum Ankunftspunkt der gleichen Eingangsart → Zelle aktiv (Licht, Kollision, Tick).
4. Hinaus über den Ausgang (Interaktiv `Exit` an jeder Eingangstür) → Rückkehr zur gleichen Eingangsart außen → Zelle schläft.
5. Aufräumen alle 0,5 s: schlafende Zellen weiter als `KeepAliveRadius` (150 m) werden zerstört; danach LRU, bis `MaxCachedInteriors` (6) und `MemoryBudgetKB` (12 MB Schätzung) passen.

Zugang (`EvaluateAccess`): Schlüssel (`IMTAKeyHolder`) > dauerhaft entriegelt > Notausgang nur von innen > Fraktionsstand (`UMTAStorySubsystem::GetStanding`) > Öffnungszeiten > Schlüssel nötig. Abgeleitet je Eingang: Hintertür immer hackbar, Dach ohne Öffnungszeiten aber leichter zu hacken, Notausgang nur mit Hack (+1) von außen, Garage für Fahrzeuge (Hineinfahren genügt).

## 5. Geometrie und Licht

* **Kantenraster**: Räume werden auf 0,5-m-Zellen gerastert; jede Zellkante mit Besitzerwechsel ist Wand. Türen/Fenster ersetzen Kantenabschnitte; gleiche Kanten werden zu Segmenten verschmolzen. Gemeinsame Wände entstehen so genau einmal, Ecken schließen durch Verlängerung um die Wandstärke.
* Tür: Sturz, Zarge, optional Türblatt (`AMTAInteriorInteractive`, Scharnier, Kollision). Fenster: Brüstung, Sturz, Glas (eigene ISM, Glasmaterial), Rahmen, **Fensterkulisse** 1,5 m außen (emissive, Farbe aus Tageszeit).
* Böden und Decken je Raum, Treppenlöcher per Rechteck-Subtraktion; Treppen als massive Stufen (≤ 18 cm Steigung) mit Geländer.
* Alle Primitive laufen als **ISM je (Form × Ebene)**: Solid (BlockAll, Schatten, Navigation), Deko (keine Kollision), Glas, Kulisse. Ein typischer Innenraum hat 300–1500 Instanzen in ≤ 20 Komponenten.
* Material (Platzhalter, zuweisbar über BP-Ableitung von `AMTAInteriorInstance`): `PrimMaterial` liest `PerInstanceCustomData` 0–2 (Farbe linear) und 3 (Emissive), `GlassMaterial`, `BackdropMaterial`, `InteractiveMaterial` (liest `CustomPrimitiveData` 0–3). Ohne Material wird das Engine-Standardmaterial verwendet.
* Licht-Budget je Zelle: 24 Lichter, davon ≤ 4 mit Schatten, ≤ 8 Fenster-Rect-Lights. Raumlichter je 10 m² nach Stil, Möbellichter (Lampen, Neon, Feuertonnen) solange Budget frei. Flackern über Actor-Tick nur in aktiven Zellen.
* **Tageszeit**: Das Subsystem sucht einen Actor mit `IMTATimeProvider` (z. B. `AMTATimeOfDay`, Datei unverändert) und ruft `Execute_GetTimeOfDayHours`. Alternativ `SetHourOfDay`/`SetTimeProvider`. `SetWeatherDim` dunkelt Fensterlicht. Tageswechsel füllt Läden auf.

## 6. Anbindung für andere Agenten

**Zu-Fuß-Agent (Interaktion)**

```cpp
// In der IMTAInteractable-Implementierung bzw. im Interaktions-Overlap:
if (IMTAEnterable* Door = Cast<IMTAEnterable>(Actor))
{
    Prompt = Door->GetEnterPrompt(Pawn);         // "Betreten: Club Kellerlicht" / "Geschlossen - oeffnet um 18:00"
    if (bPressed) Door->Enter(Pawn);
}
else if (IMTAInteriorUsable* Usable = Cast<IMTAInteriorUsable>(Actor))
{
    Prompt = Usable->GetUsePrompt(Pawn);
    if (bPressed && Usable->CanUse(Pawn)) Usable->Use(Pawn);
}
```
Trigger: Eingänge haben Kugeln, Interaktive eine Box (`UseVolume`), beide Overlap mit `ECC_Pawn`/`ECC_Vehicle` (keine Visibility-Blockade, daher Overlap-Abfrage statt Line-Trace benutzen). Verstecke melden `OnHidingChanged(Pawn, bHidden, Spot)`; Bewegungssperre übernimmt der Charakter.

**Hack-Minispiel**

```cpp
Sub->OnHackRequested.AddDynamic(this, &UMyHackUI::Start);   // (UObject* Target, int32 Difficulty, FName ContextId)
// ...nach dem Minispiel:
UMTAInteriorSubsystem::Get(this)->ResolveHack(Target, bSuccess);
```
Ohne gebundenes Minispiel gelingen Stufe 1–2 automatisch (Prototyp).

**Entdeckungssystem**: `Sub->OnSecretFound.AddDynamic(this, &UDiscovery::OnSecret)`; IDs sind `vorlagenId@gebaeudeschluessel`, gefundene Geheimnisse erscheinen nicht erneut. Speichern über `StateToJson()`/`StateFromJson()` (Geheimnisse, entriegelte Türen/Eingänge).

**NPC-Agent**: `OnInteriorBuilt(AMTAInteriorInstance*)` → `GetNPCSlots()` liefert Weltposition, Aktivität (`sleep, cook, drink, dance, bartender, repair, pray, hack, guard, shopkeeper, patient, ...`), Rolle, Stundenfenster und Wahrscheinlichkeit. `OnInteriorDestroyed(EntryKey)` zum Aufräumen. Ladenbesitzer: `Instance->Shop->OwnerNpcId`, Anwesenheit via `SetOwnerPresent`.

**Wirtschafts-Agent**

```cpp
class UMyEconomyBridge : public UObject, public IMTAPriceProvider
{
    virtual int32 GetPrice(const FMTAPriceQuery& Q) const override
    { return (int32)Economy->GetPrice(Q.BasePrice, MTAInterior::DistrictToName(Q.District), Q.Category); }
    virtual bool TryCharge(AActor* Buyer, int32 Amount) override { /* Geld abbuchen */ }
    virtual void GrantItem(AActor* Buyer, FName ItemId, int32 Qty) override { /* Inventar */ }
};
UMTAInteriorSubsystem::Get(this)->SetPriceProvider(Bridge);
```
Ohne Provider: Grundpreis × Bezirksfaktor (Core 1,6, Greenmoor 1,8, Rustline 0,75 …), Kauf ohne Abbuchung.

**Welt-Agent (Generator)**

```cpp
AMTABuildingEntry::SpawnForLot(World, LotCenter, FVector2D(LotW, LotD), Height, DirToStreet, District, Seed /*, RoomType*/);
```
Die Eingänge richten sich zur Straße aus; `RoomType` leer = beliebige Vorlage des Bezirks.

**Fahrzeuge**: Garageneingänge (`bAllowsVehicles`) reagieren auf spielergesteuerte Nicht-Charakter-Pawns per Overlap; der Garagen-Ausgang innen ebenso. Abkühlzeit `TravelCooldown` verhindert Pendeln.

## 7. Navigation, Packaging, Konfiguration

* Navigation: Projekt-Einstellung *Runtime Generation = Dynamic* (oder Navigation Invokers am NPC) und ein NavMeshBoundsVolume, das das Interior Grid unter der Stadt umfasst (z. B. Z −600 m bis −1 800 m). Solid-ISMs beeinflussen die Navigation, Deko nicht.
* Packaging: `Content/Data` unter *Additional Non-Asset Directories to Package* eintragen (gilt auch für `story.json`).
* `Config/DefaultGame.ini`:

```ini
[/Script/MTA6.MTAInteriorSubsystem]
InstanceClass=/Game/Interiors/BP_InteriorInstance.BP_InteriorInstance_C
InteriorGridDepth=-60000
PrewarmRadius=3500
KeepAliveRadius=15000
MaxCachedInteriors=6
MemoryBudgetKB=12288
```

## 8. Risiken und nächste Schritte

* **Nicht kompiliert** (Vorgabe: keine UBT-Läufe). Signaturen wurden gegen die 5.8-Header geprüft (`StartCameraFade`, `TeleportTo`, `AddInstances`, `SetCustomData`, `SetNumCustomDataFloats`, `ULocalLightComponent::SetIntensityUnits`, `URectLightComponent`, `FJsonObject::TryGet*Field(FStringView, …)`, `SpawnActorDeferred`, `FComponentBeginOverlapSignature`, `IMTATimeProvider::Execute_GetTimeOfDayHours`).
* Bau ist synchron (ein Innenraum ≈ einige hundert bis 1 500 Instanzen); Vorwärmen begrenzt das auf einen Bau pro 0,5 s. Falls Hitches messbar werden: Builder (reines C++) auf einen Task auslagern, nur die Komponenten-Erzeugung im Game Thread.
* Rect-Lights und Lumen in geschlossenen Zellen: Belichtung (Auto-Exposure) innen prüfen; ggf. Post-Process-Volume pro Zelle.
* Fensterkulisse ersetzt keinen echten Ausblick; für Showcase-Orte (Aurelia-Lobby, St. Merrin) später handgebaute Level Instances über `LoadLevelInstanceBySoftObjectPtr` an derselben Grid-Position.
* Möbel-Kollision aus Primitiven ist grob; Kleinteile < 6 cm haben keine Kollision.
* Mehrspieler: Zellen sind derzeit Client-lokal gedacht; für M15 muss der Server Bau und Zustand (Türen, Geheimnisse) replizieren.

## 9. Werkzeuge

```text
build_data.py         -> erzeugt furnishing.json + interiors.json
validate_interiors.py -> Prüfungen, Exitcode 1 bei Fehlern (aktuell: OK, 0 Fehler)
preview_interiors.py  salt_dock_bar               (ASCII, 1 Zeichen = 0,5 m)
preview_interiors.py  core_penthouse --svg x.svg  (SVG je Etage)
preview_interiors.py  --all --svg-dir Saved/InteriorPreview
```
