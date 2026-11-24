# Integration: Missionssystem (UMTAMissionRunner)

Neue Dateien (nur diese wurden angelegt, keine bestehende Klasse geändert):

| Datei | Inhalt |
|---|---|
| `Source/MTA6/MTAMissionTypes.h` | Enums (`EMTAMissionType`, `EMTAMissionRunState`, `EMTAMissionStepKind`, `EMTAChaseCaptureMode`, `EMTAMissionFailReason`, `EMTAMissionMarkerKind`), Structs (`FMTAMissionStep`, `FMTAMissionSpec`, `FMTAMissionOffer`, `FMTAMissionMarker`), Delegates (`FMTAMissionObjectiveSignature`, `FMTAMissionFinishedSignature`, `FMTAMissionRunStateSignature`) |
| `Source/MTA6/MTAMissionRunner.h/.cpp` | `UMTAMissionRunner` (UActorComponent + `IMTAMinimapProvider`): Auswahl, Missionsbau aus `story.json`, Ablauf, Anzeige, Selbsttest |
| `Source/MTA6/MTAChaseTarget.h/.cpp` | `AMTAChaseTarget`: kinematisches KI-Fahrzeug auf dem Raster (Flucht, Rennstrecke, Kolonne) |
| `Source/MTA6/MTAMissionPickup.h/.cpp` | `AMTAMissionPickup`: Leuchtsäule + Bodenscheibe, `IMTAInteractable` (Taste E zu Fuß) |

`story.json` wurde **nicht** geändert: alle 67 Missionen haben bereits `type` und `params`. Orte werden deterministisch aufgelöst (siehe Kommentar in `MTAMissionRunner.h`). Optional kann `registry.locations` ergänzt werden, z. B. `"locations": {"saltmarket_pension": [1, 4]}`. Optional pro Mission `onFail` (Effekt-Array wie `onSuccess`); der Runner wendet es über `ApplyEffectsJson` an. Beides ignoriert der Lint (`story_sim.gd`, aktuell GRÜN).

## Ablauf

```
Idle --Select(Id)--> Briefing --(erster Tick ohne offenen Dialog)--> Running
Running --alle Schritte--> Success --OnMissionFinished(true)--> (Komponente: Story-Dialog + CompleteMission)
Running --Zeit/Wrack/Ziel weg/Festnahme/Tod/Fracht/Fahrgast/Heat-Cap/letzter Platz--> Failed (25 % Basisgeld Kosten)
Success/Failed --5 s--> Idle;  Abort() jederzeit --> Idle (OnMissionFinished(false), keine Kosten)
```

Der Runner zahlt nur Zuschläge (Bezirksfaktor, Zeitbonus bis +30 %, Rennplatz, Einsatz, Fracht-Abzug). Das `money` aus `onSuccess` zahlt weiter die Story-Engine über `UMTAMissionComponent::FinishStoryMission`.

## 1. AMTA6GameMode

**MTA6GameMode.h** (bei den Komponenten):

```cpp
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Mission")
	TObjectPtr<class UMTAMissionRunner> MissionRunner;

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	UMTAMissionRunner* GetMissionRunner() const { return MissionRunner; }
```

**MTA6GameMode.cpp**:

```cpp
#include "MTAMissionRunner.h"

// Konstruktor, nach MissionComponent:
	MissionRunner = CreateDefaultSubobject<UMTAMissionRunner>(TEXT("MissionRunner"));

// BeginPlay (nach SpawnWorldAI): Missionsziele auf der Minimap (optional)
	RegisterMinimapProvider(MissionRunner);

// HandleMenuLoad und HandleMenuNew, vor dem Laden: laufende Fahrt verwerfen (wird nicht gespeichert)
	if (MissionRunner) { MissionRunner->Abort(); }

// CheckMissionBriefing ersetzen: Briefing fuer den gewaehlten Auftrag (der Runner wartet im Zustand
// Briefing, solange die Seite offen ist, und startet beim ersten Tick danach)
void AMTA6GameMode::CheckMissionBriefing()
{
	if (!MissionRunner || IsStoryPageOpen() || IsDialogOpen())
	{
		return;
	}
	if (MissionRunner->GetState() != EMTAMissionRunState::Briefing)
	{
		return;
	}
	const FMTAMissionSpec& Spec = MissionRunner->GetActiveMissionRef();
	if (Spec.Id == LastBriefedMissionId)
	{
		return;
	}
	LastBriefedMissionId = Spec.Id;
	FMTAStoryPage Page;
	Page.Title = Spec.Title;
	Page.Body = Spec.Text + TEXT("\n\nAuftrag: ") + UMTAMissionRunner::GetTypeDisplayName(Spec.Type)
		+ TEXT("   Belohnung: ") + Spec.RewardText
		+ TEXT("\nErstes Ziel: ") + (Spec.Steps.Num() > 0 ? Spec.Steps[0].Label : FString());
	ShowStoryPages({ Page }, TEXT("AUFTRAG"));
}
// Hinweis: LastBriefedMissionId in MissionRunner->OnMissionFinished leeren, damit ein Neustart
// (Retry) wieder ein Briefing zeigt: im Abschluss-Handler LastBriefedMissionId.Reset();

// RunSelfTest, vor "SELFTEST ERGEBNIS":
	if (MissionRunner)
	{
		const FString MissionReport = MissionRunner->RunMissionSelfTest();
		Check(TEXT("Missionssystem"), MissionReport.Contains(TEXT("MISSIONTEST ERGEBNIS"))
			&& !MissionReport.Contains(TEXT(": FEHLER")));
	}
```

`RunMissionSelfTest()` läuft synchron in einem Frame (simulierter Spieler, die echte Figur wird mitteleportiert und am Ende zurückgesetzt). Geld, Fahndung und Story-Zustand werden wiederhergestellt bzw. nicht verändert. Jede Zeile wird zusätzlich als `MISSIONTEST ...` geloggt. Geprüft werden: story.json geladen, alle Story-Missionen baubar, je Typ eine simulierte Mission (courier, chase ram/block/observe, escape über Versteck und über Abkühlen, race mit Gegnern, hunt, hack mit Flucht, gig delivery/sticker, side, event), Fehlschläge (Zeit, Zielverlust, Festnahme, Wrack, Tod, Fracht), Auftragsliste/Auswahl/Abbruch, freie Kurierfahrt und das Durchspielen **aller** Story-Missionen.

## 2. AMTAHUD

**MTAHUD.h** (private):

```cpp
	void UpdateMissionListInput(AMTA6GameMode* GM);
	void DrawMissionList(AMTA6GameMode* GM);
	void DrawMissionObjective(AMTA6GameMode* GM, float& Y);
```

**MTAHUD.cpp**:

```cpp
#include "MTAMissionRunner.h"

// DrawHUD, nach DrawDialog(GM):
	UpdateMissionListInput(GM);
	DrawMissionList(GM);

// DrawStatus: den bisherigen Block "if (const UMTAMissionComponent* Mission = ...)" ersetzen durch
	DrawMissionObjective(GM, Y);

void AMTAHUD::UpdateMissionListInput(AMTA6GameMode* GM)
{
	UMTAMissionRunner* Runner = GM->GetMissionRunner();
	if (!PlayerOwner || !Runner || GM->IsStoryPageOpen() || GM->IsDialogOpen())
	{
		return;
	}
	if (PlayerOwner->WasInputKeyJustPressed(EKeys::Tab))
	{
		Runner->ToggleSelection();
		return;
	}
	if (!Runner->IsSelectionOpen())
	{
		return;
	}
	if (PlayerOwner->WasInputKeyJustPressed(EKeys::Escape) || PlayerOwner->WasInputKeyJustPressed(EKeys::BackSpace))
	{
		Runner->CloseSelection();
		return;
	}
	if (Runner->HasActiveMission())
	{
		if (PlayerOwner->WasInputKeyJustPressed(EKeys::Zero))
		{
			Runner->SelectByIndex(-1);   // Abbruch
		}
		return;
	}
	static const FKey Keys[9] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 i = 0; i < 9; ++i)
	{
		if (PlayerOwner->WasInputKeyJustPressed(Keys[i]))
		{
			Runner->SelectByIndex(i);
			return;
		}
	}
}

void AMTAHUD::DrawMissionList(AMTA6GameMode* GM)
{
	const UMTAMissionRunner* Runner = GM->GetMissionRunner();
	if (!Runner || !Runner->IsSelectionOpen())
	{
		return;
	}
	const float W = FMath::Min(1000.f * UIScale, Canvas->ClipX - 80.f * UIScale);
	const float X = (Canvas->ClipX - W) * 0.5f;
	float Y = Canvas->ClipY * 0.18f;
	DrawRect(FLinearColor(0.f, 0.f, 0.02f, 0.82f), X - 20.f * UIScale, Y - 20.f * UIScale, W + 40.f * UIScale, Canvas->ClipY * 0.64f);
	Y += DrawShadowText(TEXT("AUFTRAEGE"), X, Y, ColAccent, 1.3f * UIScale) + 12.f * UIScale;
	if (Runner->HasActiveMission())
	{
		Y += DrawShadowText(TEXT("Laufend: ") + Runner->GetTitleText(), X, Y, ColText, 0.9f * UIScale);
		DrawShadowText(TEXT("[0] Auftrag abbrechen    [Esc] Schliessen"), X, Y + 10.f * UIScale, ColDanger, 0.8f * UIScale);
		return;
	}
	const TArray<FMTAMissionOffer> Offers = Runner->GetSelectionOffers();
	for (const FMTAMissionOffer& O : Offers)
	{
		Y += DrawShadowText(O.DisplayLine, X, Y, O.bMain ? ColAccent : ColText, 0.85f * UIScale) + 4.f * UIScale;
	}
	if (Offers.Num() == 0)
	{
		Y += DrawShadowText(TEXT("Keine Auftraege verfuegbar."), X, Y, ColText, 0.85f * UIScale);
	}
	DrawShadowText(TEXT("[1-9] Annehmen    [Esc] Schliessen"), X, Y + 14.f * UIScale, FLinearColor(0.85f, 0.85f, 0.85f, 1.f), 0.8f * UIScale);
}

void AMTAHUD::DrawMissionObjective(AMTA6GameMode* GM, float& Y)
{
	const float S = 0.9f * UIScale;
	const float X = 20.f * UIScale;
	const UMTAMissionRunner* Runner = GM->GetMissionRunner();
	if (!Runner || Runner->GetState() == EMTAMissionRunState::Idle)
	{
		Y += DrawShadowText(TEXT("Auftrag: keiner  [Tab] Auftragsliste"), X, Y, ColText, S);
		return;
	}
	const EMTAMissionRunState RunState = Runner->GetState();
	const FLinearColor C = RunState == EMTAMissionRunState::Failed ? ColDanger : (RunState == EMTAMissionRunState::Success ? ColAccent : ColText);
	Y += DrawShadowText(Runner->GetTitleText(), X, Y, ColAccent, S);
	Y += DrawShadowText(Runner->GetObjectiveText(), X, Y, C, S);
	FString Line = Runner->GetStatusText();
	const FString Progress = Runner->GetProgressText();
	if (!Progress.IsEmpty())
	{
		Line += (Line.IsEmpty() ? TEXT("") : TEXT("   ")) + Progress;
	}
	const float TimeLeft = Runner->GetTimeLeft();
	if (TimeLeft >= 0.f)
	{
		const int32 Sec = FMath::CeilToInt(TimeLeft);
		Line += FString::Printf(TEXT("%sZeit %d:%02d"), Line.IsEmpty() ? TEXT("") : TEXT("   "), Sec / 60, Sec % 60);
	}
	if (!Line.IsEmpty())
	{
		Y += DrawShadowText(Line, X, Y, TimeLeft >= 0.f && TimeLeft < 15.f ? ColDanger : ColText, S);
	}
}

// DrawMinimap: den Block "Missionsziel, am Rand geklemmt" ersetzen durch
	if (const UMTAMissionRunner* Runner = GM->GetMissionRunner())
	{
		for (const FMTAMissionMarker& M : Runner->GetObjectiveMarkers())
		{
			FVector2D D(M.Location.X - CarLoc.X, M.Location.Y - CarLoc.Y);
			const float Lim = MinimapWorldRadius * 0.88f;
			const bool bClamp = M.Kind == EMTAMissionMarkerKind::Objective || M.Kind == EMTAMissionMarkerKind::Target || M.Kind == EMTAMissionMarkerKind::SafeZone;
			if (D.Size() > Lim)
			{
				if (!bClamp) { continue; }
				D = D.GetSafeNormal() * Lim;
			}
			FLinearColor MC = ColAccent;
			float R = 6.f;
			switch (M.Kind)
			{
			case EMTAMissionMarkerKind::NextObjective: MC = FLinearColor(0.2f, 0.85f, 1.f, 0.6f); R = 4.f; break;
			case EMTAMissionMarkerKind::Target:        MC = ColDanger; R = 7.f; break;
			case EMTAMissionMarkerKind::Escort:        MC = FLinearColor(0.6f, 0.15f, 0.15f, 1.f); R = 4.f; break;
			case EMTAMissionMarkerKind::Opponent:      MC = FLinearColor(0.3f, 0.5f, 1.f, 1.f); R = 4.f; break;
			case EMTAMissionMarkerKind::SafeZone:      MC = FLinearColor(0.3f, 0.45f, 1.f, 1.f); R = 7.f; break;
			default: break;
			}
			AddCircle(ToMap(CarLoc.X + D.X, CarLoc.Y + D.Y), R * UIScale, MC, Tris);
		}
	}
```

Hinweis: Der Runner meldet seine Punkte zusätzlich über `IMTAMinimapProvider` als `Other`. Wird `RegisterMinimapProvider(MissionRunner)` im GameMode genutzt, erscheinen sie zusätzlich als kleine Verkehrsquadrate; wer das nicht will, lässt die Registrierung weg.

Tasten: `Tab` Liste, `1`-`9` annehmen, `0` laufenden Auftrag abbrechen (in der Liste), `Esc` schließen, `E` zu Fuß an Abholpunkten/Terminals (Interaktion beschleunigt Hacks auf doppelte Geschwindigkeit).

## 3. UMTAMissionComponent

Die Komponente wählt keine Mission mehr selbst und prüft keine Ankunft mehr. Sie zeigt nach einem erfolgreichen Fahrteil den Story-Dialog (Text + `choices`) und ruft wie bisher `CompleteMission`.

**MTAMissionComponent.h** (public):

```cpp
	UFUNCTION()
	void HandleRunnerFinished(bool bSuccess);
```

(private):

```cpp
	UPROPERTY(Transient)
	TObjectPtr<class UMTAMissionRunner> Runner;
```

**MTAMissionComponent.cpp**:

```cpp
#include "MTAMissionRunner.h"

// BeginPlay, am Ende:
	Runner = UMTAMissionRunner::Get(this);
	if (Runner)
	{
		Runner->OnMissionFinished.AddUniqueDynamic(this, &UMTAMissionComponent::HandleRunnerFinished);
	}

// TickComponent: nach "if (!bInitialized) { ... StartNextMission(); }" einfuegen
	if (Runner)
	{
		// Ziel und Zeit kommen vom Runner; eigener Marker nur als Hinweis ausblenden
		FVector Obj;
		if (Runner->GetPrimaryObjectiveLocation(Obj))
		{
			TargetLocation = Obj;
		}
		TimeLeft = Runner->GetTimeLeft();
		if (MarkerActor)
		{
			MarkerActor->SetActorHiddenInGame(true);
		}
		return;   // keine Ankunftspruefung mehr
	}

// StartNextMission: am Anfang einfuegen (keine automatische Missionswahl mehr)
	if (Runner)
	{
		bHasStoryMission = false;
		CurrentMission = FMTAMission();
		OnMissionChanged.Broadcast();
		return;
	}

// Neue Funktion
void UMTAMissionComponent::HandleRunnerFinished(bool bSuccess)
{
	if (!bSuccess || !Runner || !Runner->WasLastFinishedFromStory())
	{
		return;
	}
	UMTAStorySubsystem* Story = GetStory();
	if (!Story)
	{
		return;
	}
	const FString Id = Runner->GetLastFinishedMissionId();
	for (const FMTAMission& M : Story->GetAvailableMissions())
	{
		if (M.Id == Id)
		{
			CurrentMission = M;
			bHasStoryMission = true;
			BeginStoryDialog();   // Text -> choices -> FinishStoryMission (CompleteMission, Geld, Fahndung)
			return;
		}
	}
}
```

`GetMissionDisplayTitle()` kann für das alte HUD bleiben; mit der neuen HUD-Anbindung wird es nicht mehr gebraucht. `RestoreCourier` ist ohne Wirkung, solange ein Runner existiert (freie Kurierfahrten sind jetzt ein Listeneintrag des Runners).

Optional `AMTAVehiclePawn::SelectChoice`: unverändert lassen. Die Ziffern im Fahrzeug rufen weiterhin `SelectOption`, das ohne offenen Dialog nichts tut; die Auftragsliste liest die Ziffern selbst im HUD.

## Missionstypen im Spiel

| Typ | Schritte | Erfolg | Fehlschlag |
|---|---|---|---|
| courier | Abholen (`from`, anhalten oder E) -> Kontrollpunkte (`checkpoints`) -> Abliefern (`to`; `any`/`variable` = entfernte Kreuzung) | Abgeliefert | Zeit (ab Abholen), Wrack, `fragile`: Schaden > 35 %, Fahrgast > 4 s außerhalb des Autos |
| chase | Treffpunkt -> `AMTAChaseTarget` flieht auf dem Raster (biegt weg vom Spieler ab, `escape_skill`), `reinforcements` als Kolonne | `ram`: Zielzustand 0 % (Rammen ab 4 m/s Annäherung); `block`/`ram_or_block`/`stop`: 3 s vor dem Ziel stehen; `observe`: Abstand `min_distance`..`max_distance` halten bis zur Ankunft | Ziel > 250 m für 10 s, Zeit, beim Beschatten 3 s zu nah oder gerammt |
| escape | Ausgangspunkt -> (optional `location` abholen) -> Fahndung auf `heat_level` | Fahndung 0 oder Versteck (`safe_zone`, `hide_spots`) erreicht | Festnahme (Aegis +10), Wrack, Zeit |
| race | Startlinie -> Rundkurs (achsparallel auf dem Raster, `laps`), `opponents` als Geister-KI mit Gummiband | Ziel erreicht, nicht Letzter | Letzter Platz, Zeit |
| hunt | `investigation_nodes` Spuren -> `prep_slots` Ausrüstung -> Zielfahrzeug stellen | gestellt | `heat_cap` überschritten, Ziel entkommt |
| hack | (optional `carry`/`from` abholen) -> Terminal(s) `hack_time` s im Bereich halten (E zu Fuß = doppelt schnell) -> bei `escape_after`/`heat_after`/`pursuers` untertauchen | alles erledigt | Zeit, Festnahme |
| gig | je `template`: delivery/camera_courier (Lager -> `stops`), tow, taxi, smuggle_passengers, sticker_run (`targets` Durchfahrten), traffic_light_hack | wie Kurier | wie Kurier |
| side | Treffen (`location`) -> optional `from`/`to`, Fahrgast, `target` -> restliche `beats` als Stationen | alle Etappen | Wrack/Festnahme |
| event | Ort (`location`) -> optional `supply_runs`/`photo_targets`/`gate_count` -> vor Ort bleiben (`duration_min` x 5 s, 15..60 s) -> bei `heat_increase` entkommen | durchgehalten | Wrack/Festnahme |

## Risiken / offene Punkte

- Nicht kompiliert (wie vereinbart). Kritische Stellen zum Prüfen: `FJsonSerializer::Serialize` mit `TCondensedJsonPrintPolicy`, Implementierung von `IMTAMinimapProvider` in einer Komponente, `UFUNCTION` mit `FIntPoint`-freien Signaturen (BuildSpec ist bewusst kein UFUNCTION).
- Orte sind Kreuzungen im 9x9-Raster, abgeleitet aus Bezirkspräfixen der Namen; sie passen zur Bezirksverteilung der Missionszellen, nicht zu konkreten Gebäuden. Feinsteuerung über `registry.locations`.
- `AMTAChaseTarget` ist kinematisch (kein Chaos-Fahrzeug). Mit `bPhysicalCollision` blockiert der Körper (BlockAllDynamic) das Spielerauto; bei harten Teleport-Schritten kann das Spielerauto weggeschoben werden. Renngegner sind Geister.
- Kein Speichern laufender Aufträge: beim Laden `Abort()`.
- `hackModule`-Flag halbiert `hack_time` noch nicht (Story-Flags sind nicht öffentlich lesbar; dafür bräuchte `UMTAStorySubsystem` einen `GetFlag`).
- Kameras (`cameras`, `max_detections`, `camera_detection_fails`), Hubschrauber, Straßensperren, Wetter/Tageszeit-Parameter werden gelesen, aber noch nicht simuliert.
- Spielertod gibt es noch nicht als System: `NotifyPlayerDied()` aufrufen, sobald vorhanden; zusätzlich gilt "kein Pawn > 3 s" als Tod.
