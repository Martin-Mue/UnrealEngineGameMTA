# Integration: SaveManager und Charakter-Oberfläche

Neue Dateien (nichts Bestehendes wurde geändert):

| Datei | Inhalt |
|---|---|
| `Source/MTA6/MTASaveManager.h/.cpp` | `UMTASaveManager` (GameInstance-Subsystem), `FMTASaveContext`, `FMTASaveSlotMeta`, `RunSaveSelfTest()` |
| `Source/MTA6/MTACharacterScreen.h/.cpp` | `AMTACharacterScreenController` (Actor, Taste I) und das Slate-Widget `SMTACharScreen` |

`MTA6.Build.cs` braucht keine neuen Module (Json, Slate, SlateCore, InputCore sind schon drin).

## 1. Spielstandformat (Schema 3)

Datei: `<Projekt>/Saved/SaveGames/MTA6_<Platz>.json` mit Plätzen `Auto` (Nummer 0), `Slot1` bis `Slot3`.
Daneben: `.bak` (letzter guter Stand), `.tmp` (nur während des Schreibens), `.corrupt` (beschädigte Datei, zur Diagnose).

```text
{ "schema": 3, "gameVersion": "0.5.0", "savedAt": "<ISO-8601>", "slot": 1,
  "meta": { "playtime", "mission", "district", "money", "hour" },     // für die Slot-Liste
  "checksum": "<MD5 über payload>",
  "payload": "<JSON-Text>" }
payload = { "context", "progression", "story", "npc", "discovery", "worldEvents", "interior", "heat" }
```

| Feld | Quelle | Inhalt |
|---|---|---|
| `context` | `FMTASaveContext` vom GameMode | Geld, Uhrzeit, Spielzeit, Spieler- und Fahrzeugtransform, Fahrzeugzustand, Fahndung, Bezirk, Mission |
| `progression` | `UMTAProgressionSubsystem::ExportBundleJson()` | Herkunft, Wirtschaft, Fortschritt, Garage, Inventar |
| `story` | `UMTAStorySubsystem::StateToJson()` | Story-Zustand |
| `npc` | `UMTANPCSubsystem::SaveStateToJson()` | NPC, **Beziehungen, Gerüchte, Dialogzustand** (Gossip und Relationship stecken schon darin) |
| `discovery` | `UMTADiscoverySubsystem::ToJson()` | Entdeckungen |
| `worldEvents` | `UMTAWorldEventSubsystem::ToJson()` | Weltereignisse |
| `interior` | `UMTAInteriorSubsystem::StateToJson()` | Innenräume (Weltsubsystem) |
| `heat` | `UMTAHeatSubsystem` (Welt) | globale Fahndung und Bezirks-Ruf |

Nicht gespeichert: Radio (hat eine eigene Datei), Verkehr/Passanten, Chaos, Tutorial-Hinweise.

Verhalten beim Laden:

* Fehlendes Feld: das Subsystem bleibt unverändert; nur Story und Entdeckungen werden zurückgesetzt. Unbekannte Felder werden ignoriert.
* Datei beschädigt (kein JSON, Prüfsumme falsch, Nutzdaten fehlen): automatisch `.bak` laden, defekte Datei als `.corrupt` beiseitelegen, Hauptdatei aus der Sicherung heilen. `GetLastError()` enthält dann eine deutsche Meldung, `DidLastLoadUseBackup()` ist true. Sind beide kaputt: `LoadFromSlot` liefert false, der Spielzustand bleibt unverändert.
* Schema neuer als `CurrentSchema`: Laden wird verweigert ("... neueren Version ..."), nichts wird gekürzt.
* Migration: Kette `v1 -> v2 -> v3`. v1/v2 entsprechen dem alten `UMTASaveGame` (Geld, Fahndung, Uhr, Fahrzeug, Mission, Story). Der alte Slot `MTA6_Slot1` wird beim Start einmalig nach Platz 1 übernommen (nur wenn Platz 1 leer ist; Marker `MTA6_legacy_migrated.flag`, die alte Datei bleibt liegen).
* Schreiben: `.tmp` schreiben, gegenlesen (Prüfsumme), alte Datei nach `.bak`, `.tmp` an die Stelle setzen.

Wichtig: `Money` im Kontext ist das Geld des GameModes. Die Wirtschaft (`UMTAEconomySubsystem`) speichert ihr eigenes Konto im Bundle. Solange beide getrennt laufen, bleiben beide erhalten.

## 2. GameMode: `SaveGameToSlot` / `LoadGameFromSlot`

`MTA6GameMode.h`: Signaturen mit Vorgabewerten erweitern (alte Aufrufe F5/F9/Autosave bleiben gültig) und einen Kontext-Helfer ergänzen.

```cpp
// Vorab-Deklaration oben: struct FMTASaveContext;
UFUNCTION(BlueprintCallable, Category = "MTA6|Save")
bool SaveGameToSlot(bool bAutosave = false, int32 Slot = -1);   // -1: Autosave -> Platz 0, sonst Platz 1

UFUNCTION(BlueprintCallable, Category = "MTA6|Save")
bool LoadGameFromSlot(int32 Slot = -1);                         // -1: neuester Platz

private:
void BuildSaveContext(FMTASaveContext& OutCtx) const;
void ApplySaveContext(const FMTASaveContext& Ctx);

UPROPERTY()
TObjectPtr<class AMTACharacterScreenController> CharacterScreen;
```

`MTA6GameMode.cpp`: `#include "MTASaveManager.h"` ergänzen, `MTASaveGame.h` darf bleiben. `SaveGameToSlot`/`LoadGameFromSlot` ersetzen:

```cpp
void AMTA6GameMode::BuildSaveContext(FMTASaveContext& C) const
{
	C.Money = Money;
	C.Hour = Hour;
	C.PlaytimeSeconds = GameTime;
	C.Heat = GetHeat();
	if (const UMTAHeatSubsystem* HeatSub = GetHeatSubsystem())
	{
		C.District = HeatSub->GetCurrentDistrict(0);
	}
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		C.bHasPlayerTransform = true;
		C.PlayerTransform = Pawn->GetActorTransform();
	}
	if (const AMTAVehiclePawn* Vehicle = GetPlayerVehicle())
	{
		C.bHasVehicle = true;
		C.bInVehicle = true;
		C.VehicleTransform = Vehicle->GetActorTransform();
		C.VehicleHealth = Vehicle->Health;
	}
	if (MissionComponent)
	{
		C.MissionLabel = MissionComponent->GetMissionDisplayTitle();
		C.bStoryMission = MissionComponent->IsStoryMission();
		C.MissionTarget = MissionComponent->GetTargetLocation();
		C.MissionTimeLeft = MissionComponent->GetTimeLeft();
	}
}

bool AMTA6GameMode::SaveGameToSlot(bool bAutosave, int32 Slot)
{
	UGameInstance* GI = GetGameInstance();
	UMTASaveManager* SaveMgr = GI ? GI->GetSubsystem<UMTASaveManager>() : nullptr;
	if (!SaveMgr) { ShowToast(TEXT("Speichern fehlgeschlagen")); return false; }
	if (Slot < 0) { Slot = bAutosave ? UMTASaveManager::AutosaveSlot : 1; }

	FMTASaveContext Ctx;
	BuildSaveContext(Ctx);
	const bool bOk = SaveMgr->SaveToSlot(Slot, Ctx);
	if (!bOk) { ShowToast(SaveMgr->GetLastError().IsEmpty() ? FString(TEXT("Speichern fehlgeschlagen")) : SaveMgr->GetLastError()); }
	else if (!bAutosave) { ShowToast(TEXT("Gespeichert")); }
	return bOk;
}

bool AMTA6GameMode::LoadGameFromSlot(int32 Slot)
{
	UGameInstance* GI = GetGameInstance();
	UMTASaveManager* SaveMgr = GI ? GI->GetSubsystem<UMTASaveManager>() : nullptr;
	if (!SaveMgr) { return false; }
	if (Slot < 0) { Slot = SaveMgr->GetNewestSlot(); }
	if (Slot < 0) { ShowToast(TEXT("Kein Spielstand vorhanden")); return false; }

	FMTASaveContext Ctx;
	if (!SaveMgr->LoadFromSlot(Slot, Ctx))
	{
		ShowToast(SaveMgr->GetLastError());
		return false;
	}
	ApplySaveContext(Ctx);
	OnGameLoaded.Broadcast();
	ShowToast(SaveMgr->DidLastLoadUseBackup() ? SaveMgr->GetLastError() : FString(TEXT("Geladen")));
	return true;
}

void AMTA6GameMode::ApplySaveContext(const FMTASaveContext& Ctx)
{
	Money = Ctx.Money;
	SetHeat(Ctx.Heat);
	SetHour(Ctx.Hour);
	GameTime = Ctx.PlaytimeSeconds;
	LastCrimeTime = GameTime;
	BustTimer = 0.f;
	WreckTimer = 0.f;

	if (Ctx.bHasVehicle)
	{
		const FVector Loc = Ctx.VehicleTransform.GetLocation() + FVector(0.f, 0.f, 30.f);
		const FRotator Rot(0.f, Ctx.VehicleTransform.Rotator().Yaw, 0.f);
		if (AMTAVehiclePawn* Vehicle = GetPlayerVehicle())
		{
			Vehicle->Repair();
			Vehicle->Health = FMath::Clamp(Ctx.VehicleHealth, 1.f, Vehicle->MaxHealth);
			Vehicle->TeleportVehicle(Loc, Rot);
		}
		else if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			Pawn->SetActorLocationAndRotation(Loc, Rot, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	if (MissionComponent)
	{
		MissionComponent->CloseDialog();
		MissionComponent->StartNextMission();
		if (!MissionComponent->IsStoryMission() && !Ctx.MissionTarget.IsNearlyZero())
		{
			MissionComponent->RestoreCourier(Ctx.MissionTarget, Ctx.MissionTimeLeft);
		}
	}
}
```

Hinweise:

* Der Story-Reset und alle Subsystem-Zustände erledigt `LoadFromSlot` selbst; der alte Block "Story-Zustand" in `LoadGameFromSlot` entfällt.
* Fahndung: der Manager stellt den Bezirks-Ruf und die globale Fahndung im `UMTAHeatSubsystem` her; `SetHeat(Ctx.Heat)` danach ist harmlos.
* `Ctx.bInVehicle` und `Ctx.VehicleFuel` sind für späteres Aussteigen/Tanken vorgesehen; der bisherige Pawn kennt beides nicht.
* Autosave-Auslöser (Missionsende, Safehouse, alle 5 Spielminuten außerhalb von Verfolgungen): `BuildSaveContext(Ctx); SaveMgr->TryAutosave(Ctx, 30.f);` (nie bei Fahndung >= 3 oder im Kampf).
* Die alten Aufrufer in `RunSelfTest` (`SaveGameToSlot(true)` / `LoadGameFromSlot()`) funktionieren unverändert (Autosave-Platz 0 wird gespeichert, danach der neueste Platz geladen; das ist derselbe Platz, solange kein manueller Stand jünger ist). Für den Selbsttest besser `SaveGameToSlot(false, 1)` und `LoadGameFromSlot(1)` nehmen.

### Selbsttest einhängen

In `AMTA6GameMode::RunSelfTest()`, nach Abschnitt 3 (Speichern und Laden):

```cpp
if (UMTASaveManager* SaveMgr = GI ? GI->GetSubsystem<UMTASaveManager>() : nullptr)
{
	const FString Report = SaveMgr->RunSaveSelfTest();
	UE_LOG(LogTemp, Display, TEXT("%s"), *Report);
	Check(TEXT("SaveManager-Selbsttest"), Report.Contains(TEXT(": 0 Fehler,")));
}
```

Der Bericht listet jede Prüfung als `OK` oder `FEHLER ...` und endet mit `SELBSTTEST SPEICHERN: n Fehler, m Pruefungen bestanden`. Der Test speichert in den versteckten Platz 99 (`MTA6_Slot99.json`), räumt ihn danach weg und stellt den ursprünglichen Zustand aller Subsysteme wieder her.

## 3. Menü: Slot-Dialog

Das Menü ruft `QuerySlots`, `OnSaveRequested(FString Slot)` und `OnLoadRequested(FString Slot)`; der Slot-Name ist `"Auto"`, `"Slot1"`, `"Slot2"`, `"Slot3"`. In `SpawnMenu()` bleiben die Bindungen, nur die Handler ändern sich:

```cpp
void AMTA6GameMode::HandleMenuSave(FString Slot)
{
	const int32 Index = UMTASaveManager::NameToSlot(Slot);
	if (Index > UMTASaveManager::AutosaveSlot)   // Autosave-Platz ist im Speichern-Dialog gesperrt
	{
		SaveGameToSlot(false, Index);
	}
}

void AMTA6GameMode::HandleMenuLoad(FString Slot)
{
	const int32 Index = UMTASaveManager::NameToSlot(Slot);
	if (Index >= 0)
	{
		LoadGameFromSlot(Index);
	}
}

void AMTA6GameMode::FillSlotInfos(TArray<FMTASaveSlotInfo>& OutSlots)
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (const UMTASaveManager* SaveMgr = GI->GetSubsystem<UMTASaveManager>())
		{
			SaveMgr->GetSlotInfos(OutSlots);   // Autosave + Slot1..3, mit Spielzeit, Mission, "Bezirk | $Geld", Datum
		}
	}
}
```

Hauptmenü ("Weiter" aktiv, wenn es einen Stand gibt), z. B. beim Aufruf von `ShowMainMenu`:

```cpp
const UMTASaveManager* SaveMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMTASaveManager>() : nullptr;
Menu->ShowMainMenu(SaveMgr && SaveMgr->GetNewestSlot() != INDEX_NONE);
```

Die Slot-Liste markiert beschädigte Plätze mit "(beschädigt)" und Plätze aus einer neueren Version mit "(neuere Version)"; beide sind im Lade-Dialog nicht wählbar, im Speichern-Dialog schon (überschreiben ohne Rückfrage). `FMTASaveSlotInfo` hat kein Feld für Geld; es steht deshalb in `Location` ("Saltmarket  |  $1234"). Die vollen Metadaten (Version, Geld, Uhrzeit) liefert `GetSlotMetas()`.

## 4. Charakter-Oberfläche

In `MTA6GameMode.h` den Actor deklarieren (siehe oben, `CharacterScreen`), in `SpawnMenu()` nach dem Menü-Controller erzeugen:

```cpp
#include "MTACharacterScreen.h"   // in MTA6GameMode.cpp

// am Ende von SpawnMenu():
CharacterScreen = World->SpawnActor<AMTACharacterScreenController>(AMTACharacterScreenController::StaticClass(), FTransform::Identity, Params);
if (CharacterScreen)
{
	CharacterScreen->SetMenuController(Menu);   // optional, sonst sucht sich der Controller das Menü selbst
}
```

* Taste **I** (Gamepad: Steuerkreuz oben) öffnet, I/Esc/B/Start schließt. Q/E (LB/RB, Bild auf/ab) wechseln den Reiter; Pfeile, Steuerkreuz und linker Stick wählen Schaltflächen, Enter/A bestätigt, Mausrad und rechter Stick scrollen. Alle Tasten sind per `UPROPERTY` (`OpenKey`, `OpenGamepadKey`, `bPollHotkeys`) änderbar.
* Das Spiel wird pausiert, solange der Bildschirm offen ist (nur wenn es nicht schon pausiert war). Er öffnet sich nicht bei offenem Menü, Fotomodus, Gespräch oder Pause.
* Im Fotomodus oder in Zwischensequenzen: `CharacterScreen->bSuppressHotkeys = true;`.
* Reiter: Fähigkeiten (Skillbäume, Voraussetzungen farbig, Lernen mit Perk-Punkten, Baum zurücksetzen mit Bestätigung), Garage (Fahrzeugliste, Werte, aktives Fahrzeug, Reparatur, Tuning kaufen/einbauen/ausbauen), Inventar (Tasche und Kofferraum, Gewicht, Benutzen, Umlagern), Konto (Kontostand, Kredit, letzte 25 Buchungen), Fraktionen (Stände, Stufen, Belohnungen, Aegis-Profil), Herkunft (Geschichte, Eigenschaften, aktive Boni).
* Schrift: Fließtext 22 Punkt, Überschriften 30 bis 44 Punkt, jeweils mal der UI-Skalierung aus den Barrierefreiheits-Optionen; bei Änderung baut sich der Bildschirm neu auf.
* Texte sind fest auf Deutsch in `MTACharacterScreen.cpp` (nicht über `MTATr`), weil die Lokalisierungstabellen nicht verändert werden sollten. Fürs Übersetzen später genügt es, die Literale durch `MTATr("...")` zu ersetzen.
