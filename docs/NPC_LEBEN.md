# NPC-Leben in Kestrel Bay

Datengetriebenes System für Schlüsselfiguren mit eigenem Tagesablauf, Beziehungen untereinander, Gerüchten und Gesprächen. Massen-Passanten und -Verkehr gehören zum WorldAI-Agenten; dieses System kümmert sich um die **65 benannten Figuren**, die man kennenlernen, belauschen und verärgern kann.

## Dateien

| Datei | Inhalt |
|---|---|
| `Content/Data/npcs.json` | 6 Bezirke (Nachbarn mit Verzögerung in Stunden), 52 Orts-Tags, 26 Zeitplan-Vorlagen, Wetter-/Ereignisregeln, 65 Figuren, 108 Beziehungen |
| `Content/Data/barks.json` | 458 Zurufe in 27 Kategorien, 16 Gerüchtetypen mit je 3 Verzerrungsstufen (96 Textvarianten), Cooldowns |
| `Content/Data/dialogues/*.json` | 16 verzweigte Dialoge (230 Knoten, 27 Fertigkeitsprüfungen) |
| `Source/MTA6/MTANPCTypes.h` | gemeinsame Enums/Structs (Profil, Persönlichkeit, Zeitplanblock, Sim-Statistik) |
| `Source/MTA6/MTANPCActor.h` | Schnittstelle `IMTANPCActor` für sichtbare Figuren |
| `Source/MTA6/MTANPCSubsystem.*` | Profile, Uhr, Zeitplanauswertung, Sim-LOD, Test, Speichern |
| `Source/MTA6/MTAScheduleComponent.*` | Komponente am NPC-Actor (Aktivität, Ort, Verfügbarkeit) |
| `Source/MTA6/MTARelationshipSubsystem.*` | Beziehungsgraph, NPC-NPC-Interaktionen, Spieler-Vertrauen |
| `Source/MTA6/MTAGossipSubsystem.*` | Gerüchte: Entstehung, Weitererzählen, Verzerrung, Bezirkswirkung |
| `Source/MTA6/MTADialogueSubsystem.*` | Dialoggraphen, Bedingungen/Effekte, Prüfungen, Themen, Zurufe |

## Figuren

Je Bezirk 10–12 Figuren, alle fünf Fraktionen plus Parteilose (Greenmoor-Anwohner, Händler). Jede Figur hat Persönlichkeit (Neugier, Misstrauen, Humor, Mut, Gier, Loyalität, Gesprächigkeit, Reizbarkeit, je 0..1), Gewohnheiten, Geheimnisse mit Schwere, Wohn-/Arbeitsort und Stammorte, Aussehen-Archetyp, Zeitplan, optional Laden, Kurzbiografie und Konflikte. Die Story-Figuren (`jonas`, `brann`, `nox`, `ravn`, `kandel`, `yara`, `dutch`, `pia`) tragen `storyKey` = Schlüssel im `npcTrust` der Story-Engine.

## Tagesablauf

Zeitplanblock: `[von, bis, Aktivität, Ort, Flags]`, Flags `t` = ansprechbar, `h` = handelt. Orte sind Tags (`saltmarket.bar_ballast`) oder Platzhalter `@home`, `@work`, `@haunt`, `@haunt2`, `@shelter`. Die Welt-/Gebäude-Agenten lösen Tags auf Positionen auf; das NPC-System kennt keine Koordinaten.

Abweichungen:
- **Wetter** (`SetWeather`: clear/rain/storm/fog/heat): z. B. Regen → Straßenmusik wird zu `Shelter` am überdachten Stammort, Markt → Zuhause.
- **Ereignisse** (`SetDistrictEvent`): `strike` (nur Union: Arbeit → Streik an Tor 4), `raid` (Freizeit → Verstecken), `blackout`, `lockdown`, `festival`.
- **Verfügbarkeit**: `IsAvailableForDialogue`, `IsTrading`; Schlafen und Verstecken sind nie ansprechbar.

## NPC-NPC-Leben

Jede Spielstunde bilden alle anwesenden Figuren je Ort eine Gruppe. Interaktionstypen: **Plaudern, Handeln, Streiten, Vermitteln, Zusammenarbeiten, Beschatten, Verraten** (7). Das Gewicht hängt von Persönlichkeit, Beziehung (Freundschaft, Rivalität, Liebe, Schulden, Verwandtschaft), Ort/Aktivität (Bar und Abend fördern Plaudern und Streit, Markt fördert Handel, Schicht fördert Zusammenarbeit) und Tageszeit ab. Wirkung: Werte ändern sich; Beschatten deckt Geheimnisse auf (Gerücht `npc_secret`), Verrat erzeugt `npc_secret` und `betrayal`, Streit manchmal `npc_feud`.

**Sim-LOD:** Near = Actor registriert, Mid = im Bezirk des Spielers, Far = Rest. Orte mit Near/Mid-Figur werden paarweise simuliert und als `OnNPCInteraction` mit Ambient-Satz gemeldet; reine Far-Orte statistisch (erwartete Anzahl je Paar und Stunde, gleiche Wirkung, keine Delegates).

## Gerüchte

1. `ReportPlayerEvent` (Zeugen- und Chaos-Systeme) oder ein Dialog-Effekt `rumor` erzeugt ein Gerücht.
2. Beim Plaudern, Handeln und Zusammenarbeiten erzählen Figuren es weiter: Wahrscheinlichkeit nach Gesprächigkeit und Glaubwürdigkeit. Jede Weitergabe verzerrt (wenig Loyalität, viel Humor = mehr Ausschmückung); die Schwere wächst, die Glaubwürdigkeit sinkt. Der Text wechselt über drei Stufen von nüchtern bis grotesk.
3. Statistisch wächst die Bekanntheit je Bezirk logistisch. Ab 35 % springt das Gerücht nach der Kantenverzögerung in die Nachbarbezirke (Saltmarket → Rustline 2 h, Core → Greenmoor 2 h, Lantern → Greenmoor 6 h …).
4. Wirkung je Bezirk: Preisfaktor (`GetPriceModifier` für die Wirtschaft), Haltung (Barks „recognize_player“, Spieler-Haltung), Fahndungsdruck. Ab 50 % Bekanntheit ruft das System einmalig `UMTAHeatSubsystem::AddHeat(Bezirk, heat × Schwere)`.
5. Wer ein Geheimnis-Gerücht hört, bekommt das lokale Flag `dlg.clue.<geheimnis-id>`; damit öffnen sich Dialogoptionen anderswo (Beispiel: Ozren verkauft den Hinweis auf Piets Spitzeldienst, Niko lässt sich damit warnen).

## Dialoge

Knotenarten `line`, `choice`, `check`, `effect`, `topics` („Fragen zu…“), `rumor` (Gerücht als Gesprächsthema), `trade`, `end`; Einstiegspunkte `entries` nach Zustand. Bedingungen im Schema der Story-Engine (`standing`, `npcTrust`, `flag`, `done`, `tag`, `aegis`, `any`/`all`/`not`) plus `skill`, `hour`, `weather`, `district`, `rumor`, `relation`; `@npc` steht für die angesprochene Figur.

Story-Werte werden **nur gelesen** (`UMTAStorySubsystem::StateToJson()` als Schnappschuss bei Gesprächsbeginn). Story-Effekte gehen als `OnStoryEffectRequested` (JSON im Story-Schema) an den Besitzer der Story-Engine und werden bis zum nächsten Schnappschuss lokal überlagert. Lokale Flags beginnen mit `dlg.` oder `npc.`.

**Prüfungen ohne Zufall:** `Wert = Fertigkeit + Boni (when) ≥ Schwierigkeit`. Die Anzeige kennt das Ergebnis vorher (`bWillSucceed`), Fehlschläge führen immer in einen eigenen, spielbaren Zweig. Fertigkeiten: `persuade`, `intimidate`, `hack`, `driving` (Start 2, Progression setzt sie per `SetPlayerSkill`).

Inhalte: Kiosk (Händler, Gerüchte, Themen), Branns Auftrag, Zeugin am Kran, Verhör durch die Civil Guard, Streit im Ballast (Vermitteln), Hackerin Nox, Straßenmusikerin (Freundschaft), Café Ocker (Flirt ohne Anzüglichkeit), Mechaniker (Fahrprüfung), Ärztin ohne Lizenz, Schmuggler am Pier, Greenmoor-Vorsitzende (Zeugin, die meldet), Commander Ravn, Radiomoderatorin Pia, Hehler, Rennfahrerin.

**Unterbrechung:** `InterruptConversation(Grund)` → Zuruf `conversation_interrupt`, Gespräch endet, beim nächsten Ansprechen geht es am selben Knoten weiter.

## Anbindung

```cpp
// Interaktions-Komponente (anderer Agent): Spieler drückt "Sprechen"
GetGameInstance()->GetSubsystem<UMTADialogueSubsystem>()->StartConversation(NPCId);

// UI: Ansicht abonnieren, Continue() / ChooseOption(i)
Dialogue->OnDialogueView.AddDynamic(this, &UMyDialogueWidget::Show);

// Zeugen/Chaos -> Gerüchte
FMTAPlayerEventReport R; R.Type = TEXT("hit_pedestrian"); R.District = TEXT("saltmarket");
R.Witnesses = { TEXT("hedda_marlow") }; R.AnonymousWitnesses = 8; R.Severity = 0.7f;
GI->GetSubsystem<UMTAGossipSubsystem>()->ReportPlayerEvent(R);

// Tageszeit/Wetter/Bezirk
NPCs->SetGameTime(Day, Hour); NPCs->SetWeather(TEXT("rain")); NPCs->SetPlayerDistrict(TEXT("lantern"));

// Wirtschaft: Preisfaktor; Story-Besitzer: Effekte übernehmen
float Mult = Gossip->GetPriceModifier(TEXT("rustline"), TEXT("wrenchers"));
Dialogue->OnStoryEffectRequested.AddDynamic(this, &AMyGameMode::ApplyStoryEffect);

// Speichern
FString Blob = NPCs->SaveStateToJson();  NPCs->LoadStateFromJson(Blob);

// Test
FMTANPCSimStats S = NPCs->RunNPCSimulation(7, /*bRestoreState*/ true);  // S.Report
```

Sichtbare Figuren implementieren `IMTANPCActor` und tragen eine `UMTAScheduleComponent` (meldet den Actor beim Spielstart als Near an).

## Grenzen und offene Punkte

- Bezirks-IDs (`core`, `saltmarket`, `lantern`, `greenmoor`, `rustline`, `sunstrip`) müssen mit den Bezirks-IDs der Fahndung übereinstimmen.
- Die Aussehen-Archetypen (`arch_*`) sind noch nicht mit `appearance.json` des Erscheinungsbild-Agenten abgeglichen.
- Der Zeitplan kennt noch keine Wochentage.
