# Audio und Wetter (M9/M10)

Stand: Datenmodell, Synthese-Skripte und C++-Schnittstellen. Alle Inhalte (Sender, Moderatoren, Bands, Songs,
Werbespots, Nachrichten, Durchsagen) sind frei erfunden; die Musik ist ausschließlich selbst synthetisiert.
Keine realen Songs, Künstler, Sender, Marken oder Zitate.

## 1. Überblick

| Baustein | Datei(en) | Zweck |
|---|---|---|
| Radio | `MTARadioSubsystem.*`, `Content/Data/radio.json` | 6 Sender, Sendeplan, reaktive Texte, Umschalten, Lautstärke, Persistenz |
| Ambience | `MTAAudioAmbienceSubsystem.*`, `Content/Data/ambience.json` | Layer-Mischung Bezirk × Tageszeit × Wetter, Hall nach Straßenbreite, Aegis-/Polizeifunk |
| Fahrzeug-Audio | `MTAAudioVehicleComponent.*` | Parameterschnittstelle (RPM, Last, Schlupf, Tempo, Hupe, Sirene, Aufprall) |
| Wetter | `MTAWeatherManager.*`, `MTAWeatherTypes.h`, `Content/Data/weather.json` | Markov-Wetter, Parameter, `IMTAWeatherConsumer` |
| Gemeinsames | `MTAAudioTypes.*` | `FMTAAudioContext`, Bedingungen (`when`), JSON-Helfer |
| Synthese | `tools/audio/*.py` | WAV-Loops nach `Content/Audio/Generated/` |

```
Spielzustand (Heat, Story-Flags, Bezirk, Stunde)
        |                         AMTAWeatherManager --(IMTAWeatherConsumer)--> Ambience, Fahrzeug, Verkehr ...
        v                                   |
UMTARadioSubsystem  <-- Wetter/Heat (optional, automatisch)
UMTAAmbienceSubsystem --> Layer-Gewichte, Reverb, Aegis-Zeilen
UMTAVehicleAudioComponent --> RPM-Schichten, Quietschen, Wind, ...
```

## 2. Synthese (`tools/audio`)

Nur Python-Standardbibliothek (`wave`, `array`, `math`, `random`, `multiprocessing`). Feste Seeds, daher reproduzierbar.

```
python tools/audio/gen_all.py            # alles (parallel), schreibt manifest.json
python tools/audio/gen_all.py --music    # nur Radio-Loops
python tools/audio/gen_all.py --ambience # nur Ambience
python tools/audio/verify_audio.py       # Format, Länge, Gesamtgröße prüfen
```

Ohne System-Python kann die in Unreal mitgelieferte Version genutzt werden:
`F:\UnrealEngine\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe`.

* `synth_common.py`: Wavetable-Oszillatoren, FM, Rauschen, Filter, Drums, zyklischer Kamm-Hall/Echo, WAV-Schreiber.
* `gen_radio_music.py`: 6 Sender × 4 Varianten = 24 Stereo-Loops (44,1 kHz, 16 Bit, 20-30 s). Genres: Synthwave,
  Hafen-Rock, Jazz-Lounge, Boom-Bap-Instrumental, Konzern-Nachrichtenbett, Funk-Talkbett.
* `gen_ambience.py`: 10 Mono-Loops (28 s) `city, rain, wind, harbor, night_city, siren_far, industry, surf, garden, drones`
  und der Einzelklang `thunder` (12 s).
* Nahtlos: Musiknoten laufen zyklisch über das Loop-Ende, Delay/Hall sind zyklisch gerechnet; Ambience wird per
  Equal-Power-Crossfade geschlossen. `manifest.json` enthält u. a. `seamJump` (Sprung Ende→Anfang relativ zum
  mittleren Sample-Abstand; bei Musik liegt dort ein beabsichtigter Schlagzeug-Einsatz).
* Gesamtgröße ca. 124 MB (Grenze 150 MB). Die WAVs werden nicht committet (groß, reproduzierbar).

### Import in Unreal

1. `Content/Audio/Generated/*.wav` in den Content Browser importieren (Zielordner `/Game/Audio/Generated/`, Assetname =
   Dateiname, z. B. `radio_neon_tide_a`). Looping wird zur Laufzeit gesetzt; im Editor zusätzlich "Looping" aktivieren.
2. Kompression: Radio-Loops "Vorbis" Qualität ~40, Ambience ähnlich. Streaming für Loops > 5 MB empfohlen.
3. Ein Radio-Bus/Submix mit Tiefpass außerhalb des Fahrzeugs ist Sache des Mixers (nicht Teil dieses Moduls).

## 3. Radio (`radio.json`)

```jsonc
{ "settings": { "defaultStation": "neon_tide", "audioPath": "/Game/Audio/Generated/", "duckDuringSpeech": 0.35,
                "wordsPerSecond": 2.6, "jingleEveryTracks": 2, "defaultVolume": 0.7 },
  "stations": [ {
    "id": "neon_tide", "name": "Neon Tide FM", "frequency": 98.6, "genre": "...", "tagline": "...",
    "hosts":   [ { "id": "juno", "name": "DJ Juno Marlow", "bio": "..." } ],
    "jingles": [ "..." ],
    "tracks":  [ { "id": "nt_1", "title": "...", "artist": "...", "durationSec": 196, "bpm": 100, "key": "Am", "loop": "radio_neon_tide_a" } ],
    "ads":     [ { "id": "nt_ad1", "sponsor": "...", "text": "...", "when": { } } ],
    "scripts": [ { "id": "nt_s2", "type": "weather|news|talk|traffic", "host": "juno", "text": "... {district} ...",
                   "when": { "weather": ["Rain"], "wantedMin": 2 }, "cooldownMin": 30 } ],
    "schedule":[ { "from": 20, "to": 5, "show": "Nachtflut", "host": "juno", "mix": { "music": 8, "talk": 2, "ad": 1, "jingle": 1 } } ] } ] }
```

Sender: *Neon Tide FM* (Synthwave), *Dockline Radio* (Hafen-Rock), *Velvet Quay Lounge* (Jazz-Lounge),
*Lantern Pirate Radio* (Untergrund-Beats), *Aurelia Direct* (Konzern-Nachrichten/Propaganda), *Open Line* (Talk/Satire).
Pro Sender 4 Tracks (jeder verweist per `loop` auf einen synthetisierten Loop; `bpm`/`key` stimmen mit `manifest.json`
überein), 3 Jingles, 3 Werbespots, 5-9 Textbausteine, Sendeplan nach Spielstunde.

**Bedingungen (`when`)**, alle gesetzten Felder gelten gleichzeitig: `wantedMin`/`wantedMax` (0..5), `weather[]`
(`Clear, Cloudy, Haze, Rain, Thunderstorm, SmogFog, DustStorm`), `district[]` (`core, saltmarket, lanternhill, greenmoor,
rustline, sunstrip`), `flagsAll[]`, `flagsNone[]` (Story-Flags wie `leakedLedger`, `savedJonas`, `killedCivilians`,
`betrayedUnion`), `hourFrom`/`hourTo` (mit Mitternachtsüberlauf), `districtMinDensity` (nur Aegis).
Platzhalter im Text: `{district} {weather} {hour} {wanted}`.

**Ablauf:** Musik läuft als Loop für die nominelle Titeldauer. Beim Titelwechsel wählt das Radio nach Sendeplan-Gewichten
Jingle/Werbung/Nachrichten/Talk; Bausteine mit passenden Bedingungen werden bevorzugt, Cooldowns verhindern
Wiederholungen. Gesprochene Segmente sind Text + geschätzte Dauer (Musik wird währenddessen abgesenkt); eine
Sprachausgabe/Untertitel hängt sich an `OnSegmentStarted`. Steigt die Fahndung oder wird ein Story-Flag gesetzt, wird
sofort eine passende reaktive Zeile eingeschoben. Der letzte Sender und die Lautstärke liegen in
`Saved/MTA6/radio_state.json`.

```cpp
UMTARadioSubsystem* Radio = GetGameInstance()->GetSubsystem<UMTARadioSubsystem>();
Radio->TurnOn();                         // letzter Sender, sonst erster
Radio->NextStation();
Radio->SetVolume(0.6f);
Radio->SetDistrict(TEXT("rustline"));    // aus dem Bezirkssystem
Radio->SetGameHour(TimeOfDayHours);      // aus dem Tageszeit-System
Radio->SetStoryFlag(TEXT("leakedLedger"));   // löst die passende Zeile aus
Radio->OnSegmentStarted.AddDynamic(this, &AMyHud::ShowRadioLine);   // (const FMTARadioSegment&)
Radio->OnTrackChanged.AddDynamic(this, &AMyHud::ShowTrack);         // (const FMTARadioTrack&)
Radio->InjectLine(TEXT("Open Line"), TEXT("Eilmeldung ..."), EMTARadioSegmentType::News);
```
Mit `bAutoReadWorld` (Standard an) liest das Radio Fahndung (`UMTAHeatSubsystem::GetHeat(Bezirk)`) und Wetter
(`AMTAWeatherManager`) jede Sekunde selbst.

## 4. Wetter (`weather.json`)

Zustände: `Clear, Cloudy, Haze, Rain, Thunderstorm, SmogFog, DustStorm`. Pro Zustand: `rain, fog, wind, visibilityM,
gripFactor, soundDamping, traffic, cloud, lightning, dust, skyDarkening, precip, dwellMin/Max, transitionSeconds`.
`markov` ist die Übergangsmatrix (Zeile = aktuell, Spalte = nächster Zustand, Gewichte werden normiert), `hourBias`
(Tageszeit) und `districtBias` (z. B. Rustline: mehr Smog/Staub, Sunstrip: mehr Sonne) multiplizieren die Gewichte.
`wetness` steuert die träge Nässe (steigt bei Regen, trocknet langsam), `lightning` das Gewitter.

`FMTAWeatherParams` (alle geblendet): `RainIntensity, Wetness, RoadRoughness, FogDensity, WindStrength, WindGust,
WindDirectionDegrees, VisibilityMeters, GripMultiplier, SoundDamping, TrafficDensity, CloudCover, LightningActivity,
DustAmount, SkyDarkening, TransitionAlpha, State, TargetState`.
`GripMultiplier = Lerp(1, 1 - wetGripLoss, Wetness) * gripFactor` – nasse Straßen bleiben also nach dem Regen rutschig.

```cpp
// Level: AMTAWeatherManager platzieren oder erzeugen lassen
AMTAWeatherManager* W = AMTAWeatherManager::GetOrSpawn(this);
W->SetGameHour(Hours);  W->SetDistrict(TEXT("rustline"));
W->SetWeather(EMTAWeatherState::Thunderstorm, 15.f);       // erzwingen (bAutoCycle bleibt aktiv)
W->OnLightning.AddDynamic(this, &AMyActor::OnBolt);        // (float Intensity, float ThunderDelaySeconds)

// Fahrzeug (nicht Teil dieses Moduls): Haftung multiplizieren
const float Grip = AMTAWeatherManager::GetGripMultiplierFor(this);   // 1.0 ohne Manager

// Eigener Verbraucher
UCLASS() class UMyTraffic : public UActorComponent, public IMTAWeatherConsumer {
  GENERATED_BODY()
  virtual void OnWeatherUpdated_Implementation(const FMTAWeatherParams& P) override { Density = Base * P.TrafficDensity; }
  virtual void BeginPlay() override { Super::BeginPlay(); if (auto* M = AMTAWeatherManager::Find(this)) M->RegisterConsumer(this); }
};
```
Verbraucher, die vor dem Manager starten, binden sich an `AMTAWeatherManager::OnManagerReady()`.

**Visuals (alles optional):** `WeatherParameters` (Material Parameter Collection mit Skalaren `Wetness, RoadRoughness,
RainIntensity, FogDensity, WindStrength, DustAmount`), `FogActor` (setzt `SetFogDensity`), `RainSystem`/`DustSystem`
(`TSoftObjectPtr<UFXSystemAsset>`, nimmt Niagara-Systeme an; User-Parameter `Intensity`, `WindStrength`). Ist das
Modul `Niagara` in `MTA6.Build.cs` eingebunden, spawnt der Manager die Systeme selbst (Erkennung per `__has_include`);
sonst feuert er `OnPrecipitationChanged(Regen, Staub)` für Blueprint-Niederschlag. Ohne Niagara kompiliert alles.

## 5. Ambience (`ambience.json`)

* `layers`: Datei, `loop`, `baseGainDb`, `dampedByWeather`.
* `timeBands`: `dawn 5-8, day 8-17, dusk 17-21, night 21-5`; an den Grenzen wird über eine Stunde übergeblendet.
* `districts.<id>.<band>.<layer>`: Gewicht 0..1. `weatherMods.<Zustand>.<layer>`: `mul` dann `add`. Beim
  Wetterübergang werden Start- und Zielzustand mit `TransitionAlpha` gemischt; `SoundDamping` dämpft Stadt-Layer.
* `reverb.byStreetWidth`: Tabelle Straßenbreite (m) → Nachhallzeit, Wet, Pre-Delay, Reflexionsgewicht, Höhenfilter
  (linear interpoliert); `tunnel`, `openField`, `districtOffset.wetMul`.
* `aegis`: Dichte je Bezirk, Intervalle, `announcements[]` (Aegis-Durchsagen) und `police[]` (KCG-Funk, abhängig von
  der Fahndungsstufe), alle mit `when`.

```cpp
UMTAAmbienceSubsystem* Amb = GetWorld()->GetSubsystem<UMTAAmbienceSubsystem>();
Amb->SetListenerContext(EMTADistrict::Rustline, Hours, /*StreetWidthM*/ 14.f, /*Tunnel*/ false);
Amb->SetWantedLevel(3);
Amb->SetPlaybackEnabled(true);                 // spielt die importierten amb_*-Assets selbst
const float Rain = Amb->GetLayerGain(TEXT("rain"));   // oder nur die Zahlen für MetaSound/Mixer nutzen
const FMTAReverbParams& Rv = Amb->GetReverb();        // -> Submix-Reverb-Parameter setzen
Amb->OnAegisLine.AddDynamic(this, &AMyHud::ShowAegisLine);   // (const FMTAAegisLine&)
Weather->OnLightning.AddDynamic(this, &AMyActor::OnBolt);    // dort: Amb->PlayThunder(Intensity, Delay)
```
Das Subsystem registriert sich selbst beim Wettermanager (`IMTAWeatherConsumer`).

## 6. Fahrzeug-Audio

```cpp
// Im Fahrzeug (jedes Frame), Komponente "UMTAVehicleAudioComponent" am Pawn:
Audio->SetRPM(Movement->GetEngineRotationSpeed());       // ChaosWheeledVehicleMovementComponent
Audio->SetLoad(ThrottleInput);
Audio->SetSlip(MaxWheelSlip01);
Audio->SetSpeed(Movement->GetForwardSpeed());            // cm/s
Audio->SetHorn(bHornPressed);  Audio->SetSiren(bPoliceSiren);
// OnHit: Audio->NotifyCollision(NormalImpulse.Size());
```
`GetOutput()` liefert `EngineGainLow/Mid/High` (Equal-Power-Crossfade, kein Lautstärkeloch), `EnginePitch`,
`EngineVolume`, `TireSquealGain/Pitch` (nasse Straße quietscht früher), `WindGain`, `RainOnBodyGain`, `HornGain`,
`SirenGain`, `CollisionStrength`. Sind `EngineSound` & Co. (z. B. MetaSound-Quellen) gesetzt, erzeugt die Komponente
AudioComponents und schreibt die Parameter `RPM, Load, GainLow, GainMid, GainHigh` (Namen einstellbar) sowie Lautstärke
und Pitch. Ohne Assets bleibt sie reine Datenquelle. Aufpralle kommen über `OnCollisionSound(Strength, PitchScale)`.

## 7. Build.cs / Projekt

Benötigt (bereits vorhanden): `Core, CoreUObject, Engine, Json`. **Optional** für Niagara-Niederschlag: in
`MTA6.Build.cs` `"Niagara"` zu `PublicDependencyModuleNames` hinzufügen und das Niagara-Plugin im `.uproject` aktivieren
(ohne diese Änderung kompiliert das Modul trotzdem, der Manager feuert dann nur `OnPrecipitationChanged`).

## 8. Risiken und offene Punkte

* Keine Sprachausgabe: Moderatorentexte sind Text-Events (Untertitel/TTS/Aufnahmen später).
* Die Radio-Loops wiederholen sich innerhalb eines Titels (20-30 s Material für 2-4 min nominelle Dauer); für mehr
  Abwechslung die Skripte mit weiteren Varianten/Seeds erweitern.
* Aegis-Zeilen und Radio-Zeilen sind erfunden und unabhängig vom Story-Modul; Flag-Namen müssen mit `story.json`
  abgeglichen und per `SetStoryFlag` gemeldet werden.
* Nicht kompiliert/getestet (keine UBT-Läufe vorgesehen); API-Signaturen wurden gegen die UE-5.8-Header geprüft.
