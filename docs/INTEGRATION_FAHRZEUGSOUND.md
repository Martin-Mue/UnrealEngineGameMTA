# Integration Fahrzeugsound

Prozeduraler Fahrzeugklang für den Spieler-Wagen (`AMTAVehiclePawn`): Motor (4 Drehzahl-Loops), Reifenquietschen, Fahrtwind, Hupe, Polizeisirene, Aufprall, Türschlag. Es braucht weder ein Blueprint noch eine MetaSound-Quelle.

## Dateien

| Datei | Zweck |
|---|---|
| `tools/audio/make_vehicle_sounds.py` | erzeugt die WAVs (nur Standardbibliothek, 44,1 kHz mono 16 Bit, feste Seeds) |
| `Content/Audio/Generated/Vehicle/*.wav` + `manifest.json` | erzeugte Quellen; die WAVs stehen in `.gitignore`, das Skript ist die Quelle |
| `tools/ue_audio/import_vehicle_sounds.py` | Editor-Import nach `/Game/MTA6/Audio/Vehicle/`, legt die Dämpfung `ATT_Vehicle` an |
| `Content/MTA6/Audio/Vehicle/*.uasset` | importierte SoundWaves + `ATT_Vehicle` |
| `Source/MTA6/MTAAudioVehicleComponent.h/.cpp` | `UMTAVehicleAudioComponent` (Mischung, Steuerung, Selbsttest) |
| `Source/MTA6/MTAVehiclePawn.h/.cpp` | legt `VehicleAudio` an, füttert sie jeden Frame, Hupe, Aufprall |

Assets (alle unter `/Game/MTA6/Audio/Vehicle/`):

| Asset | Art | Länge |
|---|---|---|
| `veh_engine_0800`, `_2000`, `_3500`, `_5500` | Loop, Motor bei 800/2000/3500/5500 U/min | je ca. 2 s, ganze Zahl Arbeitsspiele |
| `veh_tire_squeal` | Loop | 2 s |
| `veh_wind` | Loop | 4 s |
| `veh_horn` | Loop (zwei Töne, ca. 420/525 Hz) | 1 s |
| `veh_siren` | Loop (Wail 650–1450 Hz) | 4 s |
| `veh_collision` | Einzelklang | 1,4 s |
| `veh_door` | Einzelklang (noch nicht verdrahtet) | 0,6 s |
| `ATT_Vehicle` | SoundAttenuation, Kugel innen 1200 cm, außen 5000 cm, natürlicher Abfall | – |

Loops haben `looping = true`, Kompressionsqualität 80 und Virtualisierung „Restart“. Einzelklänge haben Qualität 60. Alle SoundWaves verweisen auf `ATT_Vehicle`.

## Skripte ausführen

Vom Projektordner aus (PowerShell). Falls kein System-Python installiert ist, nimm die Engine-Python:

```powershell
& "F:\UnrealEngine\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" tools/audio/make_vehicle_sounds.py
```

Import ohne Fenster. Dabei darf kein Editor laufen und kein Build aktiv sein, weil der Editor die Modul-DLL sperrt:

```powershell
& "F:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "E:\Projekte\GTA 6\MTA6.uproject" `
  -run=pythonscript -script="E:/Projekte/GTA 6/tools/ue_audio/import_vehicle_sounds.py" `
  -unattended -nosplash -nullrhi -stdout -abslog=F:\UnrealEngine\mta6_audio.log
```

Bei Erfolg steht im Log: `[MTA6] Fahrzeugsound importiert: 10 von 10, Daempfung ok`.

## Verhalten im Spiel

- **Aktivierung:** Die Komponente prüft jeden Frame `IsPlayerControlled()` des Besitzers. Steigt der Spieler ein (`PC->Possess`), startet der Motor: Die Drehzahl läuft von 40 % Leerlauf hoch, Einblendung 0,35 s. Steigt er aus, blendet alles in 0,4 s aus. Geparkte oder verlassene Fahrzeuge kosten nur diesen einen Test, und ihre AudioComponents werden erst beim ersten Einsteigen angelegt. Verkehrsfahrzeuge (Instanzen) haben keine Komponente.
- **Motor:** Zwei AudioComponents (Slot A/B) spielen die beiden benachbarten Drehzahl-Loops mit Equal-Power-Überblendung. Der Pitch jedes Loops ist `RPM / Loop-RPM` (0,5–2). Die Lautstärke ist `Lerp(IdleLoudness, 1, Last)`. Schicht K liegt immer in Slot K % 2. Bei einem Bandwechsel wird nur der Slot neu belegt, dessen Schicht gerade das Gewicht 0 hat, deshalb klickt es nicht. Die Drehzahl kommt aus `UChaosWheeledVehicleMovementComponent::GetEngineRotationSpeed()`, die Last aus dem Gaspedal. Ist der Wagen ein Wrack, ist der Motor aus.
- **Reifen:** Der Schlupf ist das Maximum aus drei Werten:
  - Chaos-Radschlupf (`SkidMagnitude / AudioSkidFullCms`)
  - Driftwinkel
  - Querbeschleunigung (`AudioLateralGStart` bis `AudioLateralGFull`)

  Ab `Config.SquealSlipThreshold` und `SquealMinSpeed` quietschen die Reifen. Auf nasser Straße fängt es früher an, ist aber leiser.
- **Wind:** Ab 40 km/h, voll bei 200 km/h, dazu etwas Wetter-Böe.
- **Hupe:** Taste **H** oder Gamepad **linker Stick drücken**, gebunden per `BindKey` in `AMTAVehiclePawn::SetupPlayerInputComponent`. `DefaultInput.ini` bleibt unverändert. Im Dialog und als Wrack hupt nichts.
- **Sirene:** `VehicleAudio->SetSiren(true)`. Die Komponente dafür wird erst bei Bedarf angelegt, als sechste.
- **Aufprall:** `AMTAVehiclePawn::NotifyHit` meldet die Aufprallgeschwindigkeit an `NotifyImpactSpeed`. Der Bereich geht von 250 cm/s (leise) bis 2500 cm/s (laut, tiefer gepitcht), mit 0,12 s Sperrzeit. Der Klang ist ein `PlaySoundAtLocation` ohne eigene Komponente. Nur das aktive Fahrzeug spielt ihn.
- **Budget:** höchstens 6 AudioComponents pro Fahrzeug (2 Motor, Reifen, Wind, Hupe, Sirene).
- **Fehlende Assets:** `FPackageName::DoesPackageExist` wird vor dem Laden geprüft. Fehlt ein Asset, bleibt dieser Teil stumm, und `LogTemp` gibt eine Warnung aus („MTA Fahrzeugsound: … fehlt“). Es gibt keinen Absturz.

## Was du im GameMode tun musst

Für die Funktion selbst nichts. Der Pawn legt die Komponente im Konstruktor an, und die Standard-Assets sind als Soft-Pfade gesetzt.

Optional kannst du den Selbsttest in den vorhandenen GameMode-Selbsttest aufnehmen, z. B. neben „Radio-Musik importiert“:

```cpp
#include "MTAAudioVehicleComponent.h"
// ...
const FString VehReport = UMTAVehicleAudioComponent::RunVehicleAudioSelfTest();
Check(TEXT("Fahrzeugsound"), VehReport.EndsWith(TEXT("FAHRZEUGSOUND ERGEBNIS: 0 Fehler")));
```

Der Test prüft:
- dass alle 10 SoundWaves und `ATT_Vehicle` ladbar sind
- dass die Drehzahl-Mischung bei 0, 2500 und 6500 U/min plausibel ist: Summe > 0, höchstens zwei Schichten über 0,05, Leistungssumme ≈ 1
- dass die Standard-Schichten gesetzt sind

Der Bericht endet mit `FAHRZEUGSOUND ERGEBNIS: n Fehler`.

Polizeiwagen, die als `AMTAVehiclePawn` vom Spieler gefahren werden, können `GetPlayerVehicle()->VehicleAudio->SetSiren(true)` aufrufen.

## Abstimmung

`VehicleAudio->Config` (Leerlauf-/Begrenzerdrehzahl, Glättung, Quietsch-Schwelle, Wind, Aufprallbereich) und `MasterVolume` sind im Editor bzw. Blueprint änderbar. Mit `EngineLayerSounds` und `EngineLayerRPMs` lassen sich die Motor-Loops austauschen (aufsteigende Drehzahl). Andere Wagentypen bekommen eigene Loops, wenn das Skript mit anderen `ENGINE_RPMS` bzw. anderen Klangparametern läuft.
