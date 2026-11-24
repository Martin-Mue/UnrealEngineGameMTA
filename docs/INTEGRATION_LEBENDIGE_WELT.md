# Integration: Lebendige Welt

Neue Dateien (nichts Bestehendes geändert):

| Datei | Inhalt |
|---|---|
| `Source/MTA6/MTALivingWorld.h/.cpp` | `AMTALivingWorldManager`: Orte → Weltpositionen, Tagesabläufe, Wege, Sim-LOD/Budget, NPC-NPC-Begegnungen, Barks, Selbsttest |
| `Source/MTA6/MTALivingNPC.h/.cpp` | `AMTALivingNPC` (`ACharacter`, `IMTAInteractable`, `IMTANPCActor`): laufende, ansprechbare Figur mit Namens-, Aktivitäts- und Sprechblasen-Schild |
| `Source/MTA6/MTAStreetCrowd.h/.cpp` | Strassenfiguren: Zufallsnamen, Archetyp aus `appearance.json` (nur Text), einfache Tagesabläufe |

Keine neuen Modulabhängigkeiten (kein AIModule, kein NavMesh): die Figuren laufen ohne Controller (`bRunPhysicsWithNoController`) per `AddMovementInput`.

## Was die Figuren tun

- **Orte:** Die 52 Tags aus `npcs.json/locations` werden deterministisch auf Gehwegpunkte gelegt: Bezirk → gemischte Blockliste des Bezirks (Stadtgenerator `GetBlockDistrict`), Art → Blockseite (home Süd, work Nord, Treffpunkte West/Ost), Hash → Position entlang der Seite. `FVector AMTALivingWorldManager::ResolveLocation(FName Tag)` (ZeroVector bei unbekanntem Tag).
- **Tagesablauf:** alle 65 Profile folgen `UMTANPCSubsystem::EvaluateSchedule` (Wetter/Ereignisse inklusive), 48 Strassenfiguren (`NumStreetNPCs`) eigenen Plänen (Tagarbeiter, Nachteule, Spaziergänger, Händler; Regen → Unterstand). Schlafen/Zuhause/Verstecken und Büroarbeit = drinnen (kein Actor; Büroleute machen stundenweise "Pause vor der Tür").
- **Bewegung:** Laufspuren 1150 cm neben der Strassenmitte (zwischen Laternen und Hauswand), L-förmige Wege über das Raster, Querung an Kreuzungen mit kurzem Warten am Bordstein, Ausweichen (RVO + Seitenschritt, Notversatz nach 6 s Hängen).
- **Sim-LOD/Budget:** höchstens `MaxVisibleNPCs` = 80 Actors (Pool, keine Neuerzeugung nach dem Aufwärmen, max. 6 Spawns je 0,5 s), nur im Radius 120 m (Freigabe ab 135 m). Unsichtbare Figuren reisen rechnerisch mit 18 m/s weiter und erscheinen beim Näherkommen an ihrer aktuellen Wegposition.
- **Sichtbare Aktivität:** Gruppen im Kreis zueinander gedreht (Bar, Freizeit, Markt), Reihe an der Hauswand mit Blick zur Strasse (Stand, Essen, Arbeit), Streife/Spaziergang pendelt den Gehweg entlang, gelegentliches Umsehen. Über dem Kopf: Name (gold = Dialog, blau = Profil, grau = Passant mit Beruf), Aktivität ("liest Zeitung", "isst Nudeln", "unterwegs: Bar Ballast"), Sprechblase. Schilder nur bis 22 m (Blasen 35 m).
- **NPC-NPC:** Stehen zwei sichtbare Figuren nah (3,5 m) oder treffen sich Bekannte (Zuneigung ≥ 25) unterwegs, wählt bei Schlüsselfiguren `UMTARelationshipSubsystem::ChooseInteraction` den Typ (Plaudern, Handeln, Streiten, Vermitteln, Zusammenarbeiten, Beschatten, Verraten), `ApplyInteraction` wirkt, Geruechte werden per `ShareBetween` weitererzählt (und sichtbar: "Hast du schon gehört? …"), `SetBusy`, `BroadcastInteraction`. Beide bleiben stehen, drehen sich zueinander, wechseln 2–4 Blasen (Barks `ambient_chat/argue/mediate` + Antwortvorlagen). Sichtbare Ereignisse der stündlichen Simulation (`IMTANPCActor::PlayInteraction`) werden genauso abgespielt.
- **Barks:** Spieler zu Fuss (6,5 m) oder im Auto (15 m): Kategorien nach Fahndung (`wanted_low/high`), Fahrverhalten (`reckless_driving`, `near_miss`, `sidewalk`, `nice_car`), Wetter (`weather_*`), Tageszeit (`time_morning/night`), Fraktion, sonst `greeting` (bei Bekanntheit `recognize_player`). Abklingzeit 25–35 s je Figur, 4 s global, plus Kategorie-Cooldowns aus `barks.json`.
- **Taste E:** Dialogfiguren starten `StartConversation` (nicht verfügbar → Bark `not_available`), Figur bleibt stehen und schaut den Spieler an; alle anderen grüssen.

## 1. `AMTA6GameMode::SpawnDialogueNPCs()` ersetzen

In `MTA6GameMode.cpp` oben:

```cpp
#include "MTALivingWorld.h"
#include "MTALivingNPC.h"
```

Funktion komplett ersetzen:

```cpp
void AMTA6GameMode::SpawnDialogueNPCs()
{
	// Lebendige Welt: 65 Profile + Strassenfiguren mit Tagesablauf, Budget 80 sichtbare Figuren.
	if (AMTALivingWorldManager* Living = AMTALivingWorldManager::GetOrSpawn(this))
	{
		UE_LOG(LogTemp, Display, TEXT("MTA6: Lebendige Welt gestartet (%d Figuren)"), Living->GetAgentCount());
	}
}
```

Die Hilfsfunktion `DistrictFromId` im anonymen Namespace wird danach nicht mehr benutzt; entfernen (sonst evtl. Warnung "nicht referenzierte Funktion").

Die Figuren holen die Uhrzeit aus `UMTANPCSubsystem::GetHour()`; `TickWorldSystems` setzt sie bereits (`Npcs->SetGameTime`). Optional Wetter durchreichen, damit Barks/Unterstand das echte Wetter kennen:

```cpp
// in TickWorldSystems, im Block "if (UMTANPCSubsystem* Npcs = ...)"
// Npcs->SetWeather(<Wetter-ID: clear/rain/storm/fog/heat>);
```

## 2. Selbsttest in `AMTA6GameMode::RunSelfTest()`

Der alte Abschnitt zählt `AMTADialogueNPC` (`== 16`); diese Klasse wird nicht mehr gespawnt. Abschnitt "2) NPC-Gespräch" bis einschliesslich `Check(TEXT("Gespraech endet"), ...)` ersetzen durch:

```cpp
	// 2) Lebendige Welt und NPC-Gespraech
	AMTALivingWorldManager* Living = AMTALivingWorldManager::Get(this);
	Check(TEXT("Lebendige Welt vorhanden"), Living != nullptr);
	UMTADialogueSubsystem* Dialog = GI ? GI->GetSubsystem<UMTADialogueSubsystem>() : nullptr;
	if (Living)
	{
		const FString LivingReport = Living->RunLivingWorldSelfTest();
		Check(TEXT("Lebendige Welt Selbsttest"), LivingReport.Contains(TEXT("ERGEBNIS: 0 Fehler")));
	}
	if (Hero && Living && Dialog)
	{
		// Eine ansprechbare Dialogfigur neben die Spielfigur holen und das Gespraech durchklicken
		AMTALivingNPC* Npc = Living->BringDialogueNPCTo(Hero->GetActorLocation() + Hero->GetActorForwardVector() * 150.f);
		Check(TEXT("Dialogfigur geholt"), Npc != nullptr);
		if (Npc)
		{
			IMTAInteractable::Execute_Interact(Npc, Hero);
			if (!Dialog->IsInConversation())
			{
				// Figur hat laut Tagesablauf gerade keine Zeit: Dialog direkt starten
				Dialog->StartDialogue(Dialog->GetDialogueForNPC(Npc->NPCId), Npc->NPCId);
			}
			Check(TEXT("Gespraech startet"), Dialog->IsInConversation());
			int32 Steps = 0;
			while (Dialog->IsInConversation() && Steps < 40)
			{
				const FMTADialogueView View = Dialog->GetCurrentView();
				if (View.bIsChoice && View.Options.Num() > 0) { Dialog->ChooseOption(View.Options.Last().Index); }
				else { Dialog->Continue(); }
				++Steps;
			}
			Check(TEXT("Gespraech endet"), !Dialog->IsInConversation());
		}
	}
```

Der Bericht steht zusätzlich zeilenweise im Log (`SELFTEST LebendigeWelt ...`). Er prüft: alle Orts-Tags auf dem Gehweg, unbekannter Tag = Nullvektor, `core.noodle_vault` im Core, jede Schlüsselfigur hat um 3, 8:30, 12:30 und 20 Uhr eine Position, nachts sind ≥ 50 % drinnen, nach 6:00 → 12:30 laufen Figuren los und bewegen sich (30 s Sim-LOD), Wege sind achsparallel, eine Begegnung zweier Schlüsselfiguren mit Beziehung (über das Beziehungssystem) und zweier Strassenfiguren läuft durch, Barks vorhanden, ≥ 40 Strassenfiguren, Budget eingehalten. Simulierte Änderungen (Beziehungen, Gerüchte, Agentenzustand) werden danach zurückgesetzt.

## 3. HUD (`MTAHUD.h/.cpp`)

Der Interaktions-Prompt kommt bereits über `AMTACharacter::Interaction` (die Figuren implementieren `IMTAInteractable`). Nur der Rückfall `FindNearbyNPC` sucht noch `AMTADialogueNPC`.

`MTAHUD.h`: Deklaration ändern

```cpp
	class AActor* FindNearbyNPC() const;
	void DrawOverheardSpeech();
```

`MTAHUD.cpp`: Includes

```cpp
#include "MTALivingWorld.h"
#include "MTALivingNPC.h"
```

`FindNearbyNPC` ersetzen:

```cpp
AActor* AMTAHUD::FindNearbyNPC() const
{
	const APawn* Pawn = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;
	if (!Pawn || !Pawn->IsA<AMTACharacter>())
	{
		return nullptr;
	}
	const AMTALivingWorldManager* Living = AMTALivingWorldManager::Get(this);
	return Living ? Living->FindNearestTalkableNPC(Pawn->GetActorLocation(), 500.f) : nullptr;
}
```

In `UpdateConversationInput` bleibt `if (AActor* Near = FindNearbyNPC()) { IMTAInteractable::Execute_Interact(Near, PlayerOwner->GetPawn()); }` (nur den Typ `AMTADialogueNPC*` → `AActor*` ändern). In `DrawInteractionPrompt`:

```cpp
		if (const AMTALivingNPC* Near = Cast<AMTALivingNPC>(FindNearbyNPC()))
		{
			PromptText = Near->bHasDialogue ? FString::Printf(TEXT("Mit %s sprechen"), *Near->DisplayName)
				: FString::Printf(TEXT("%s ansprechen"), *Near->DisplayName);
		}
```

Optional Untertitel für belauschte Sätze (lesbarer als die Blasen im Auto/auf Distanz); in `DrawHUD` nach `DrawInteractionPrompt();` `DrawOverheardSpeech();` aufrufen:

```cpp
void AMTAHUD::DrawOverheardSpeech()
{
	if (DialogueView.bActive || !PlayerOwner || !PlayerOwner->GetPawn())
	{
		return;
	}
	const AMTALivingWorldManager* Living = AMTALivingWorldManager::Get(this);
	FString Speaker, Text;
	if (Living && Living->GetNearestSpeech(PlayerOwner->GetPawn()->GetActorLocation(), 1200.f, Speaker, Text))
	{
		DrawCenteredText(FString::Printf(TEXT("%s: %s"), *Speaker, *Text), Canvas->ClipY * 0.78f, FLinearColor(0.9f, 0.9f, 0.9f, 0.9f), 0.8f * UIScale);
	}
}
```

Alternativ `Living->OnLivingSpeech` (NPC-ID, Name, Text) abonnieren.

## Leistung (Ryzen 5 3600 + RX 5700 XT)

- Max. 80 Mannequins mit `OnlyTickPoseWhenRendered` und Update-Rate-Optimierung; Schilder nur nah sichtbar, ohne Schatten.
- Pro Frame nur Steuerung der sichtbaren Figuren (Vektormathe + `AddMovementInput`). Zeitpläne, Sim-LOD-Reisen, Barks und Begegnungen im 4-Hz-/2-Hz-Takt; Paarprüfung O(n²) über ≤ 80 Figuren alle 1,5 s.
- Bei Bedarf: `MaxVisibleNPCs` 40–60, `SpawnRadius` 9000.

## Risiken / offene Punkte

- Ohne Controller: falls die AnimBP Werte aus dem Controller liest (z. B. `IsLocallyControlled`), bleibt die Lauf-Animation aus; dann AIModule + `AutoPossessAI = PlacedInWorldOrSpawned` nutzen.
- Fahrzeuge der Welt-KI kennen die Figuren nicht: an Querungen kann ein KI-Auto durch eine Figur fahren (sie wartet nur zufällig am Bordstein).
- Laufspur bei 1150 cm setzt Gehweg 800–1300 cm voraus (Generator-Standard); Parks/Sonderblöcke (Aurelia Tower) können Hindernisse haben → Seitenschritt/Notversatz.
- Bark-Cooldowns im Dialogsystem gelten global je Kategorie; deshalb fallen viele Barks auf `greeting` zurück oder bleiben aus.
- Sitzen ist nur über Position (an der Hauswand) und Ausrichtung angedeutet; eigene Animationen fehlen.
- `AMTADialogueNPC` bleibt im Code, wird aber nicht mehr gespawnt (HUD-Rückfall und Selbsttest wie oben anpassen).
