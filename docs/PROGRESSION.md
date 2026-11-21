# MTA 6 – Fortschrittssystem (Wirtschaft, Skills, Herkunft, Fahrzeuge, Garage, Inventar)

Stand: 21.11.2026 · Engine: Unreal Engine 5.8 (C++) · Modul `MTA6` · nicht kompiliert (Signaturen gegen die 5.8-Header geprüft)

Alles ist datengetrieben: Zahlen und Inhalte stehen in `Content/Data/*.json`, der Code kompiliert ohne Editor-Assets.
Alle Namen (Marken, Modelle, Herkünfte, Skills) sind frei erfunden (Weltkonzept Kap. 9, Abgrenzung).

## 1. Übersicht

| Klasse (GameInstanceSubsystem) | Dateien | Daten | Aufgabe |
|---|---|---|---|
| `UMTAEconomySubsystem` | `MTAEconomySubsystem.*` | `economy.json` | Konto, Preise (Bezirk × Knappheit × Tageszeit × Fraktion × Skill), Bußgelder, Bestechung, Reparatur/Tanken/Abschleppen, Schwarzmarktkurse, Aurelia-Kredit (Zins ∼ Aegis), Transaktions-Ring-Puffer, Kontostand-Verlauf, `RunEconomySimulation()` |
| `UMTAProgressionSubsystem` | `MTAProgressionSubsystem.*` | `skills.json` | XP/Level, Perk-Punkte, 4 Skillbäume × 12 Skills, nutzungsbasierte Baum-Meisterschaft, Modifikator-Aggregation, Ruf-Belohnungen/-Strafen, Fortschritts-Bundle für den SaveManager |
| `UMTACharacterOriginSubsystem` | `MTACharacterOriginSubsystem.*` | `origins.json` | 5 Herkünfte, `ApplyOrigin()` setzt Startzustand aller Systeme + Story |
| `UMTAVehicleCatalog` | `MTAVehicleCatalog.*` | `vehicles.json` | 24 Modelle, 8 Marken, 9 Klassen, 5 Raritäten, 31 Tuning-Teile |
| `UMTAGarageSubsystem` | `MTAGarageSubsystem.*` | `economy.json` (garages, prices) | Besitz, Garagen je Bezirk (2/4/8/16), Kennzeichenwechsel, Tuning, Reparatur, Versicherung, Abschleppen/Verwahrstelle, `ComputeStats()` |
| `UMTAInventorySubsystem` | `MTAInventorySubsystem.*` | `economy.json` (items, inventory) | 18 Gegenstände, Slots+Gewicht (Tasche, Kofferraum je Fahrzeug), Nutzung, Handel, Beschlagnahmung bei Festnahme |
| gemeinsame Typen | `MTAProgressionTypes.*` | – | `FMTAStatModifier`, `FMTAVehicleStats`, Enums, JSON-Helfer, Story-Brücke |

Abhängigkeiten (über `Collection.InitializeDependency`): Garage → Economy, Catalog, Progression; Inventory → Economy, Garage; Origin → alle. Keine Zyklen.

**Story-Brücke:** `UMTAStorySubsystem` bleibt unverändert. Fraktionsstand wird über `GetStanding()` gelesen; Aegis, Startstände und Dialogflags werden über `StateToJson()`/`StateFromJson()` (Rundreise, `MTAProgression::PatchStoryState`) geändert.

## 2. Wirtschaft (`economy.json`)

**Preisformel:** `Preis = Basis × districts.priceFactor × timeOfDay.price × Fraktionsrabatt × scarcity[Kategorie] × Skill(econ.buyPrice)`
- Fraktionsrabatt: Stand der Bezirksfraktion (`districts.*.faction`) ±100 → ∓15 %.
- Knappheit (fuel, parts, electronics, goods, vehicles): Random-Walk mit Rückstellkraft je Spielstunde, Ereignis-Schocks (Streik, Stromausfall, Raffinerie) pro Spieltag.
- Schwarzmarkt (nur saltmarket, lanternhill, rustline): Kurs folgt der Knappheit seiner Kategorie; Verkauf = Ankauf × `sellSpread` (0,78) → Gewinn entsteht durch Routen (billig in Rustline nachts, teuer in Lantern Hill tagsüber) und Kursschwankung. Jeder Deal kostet Aegis (`aegisRiskPerDeal`).

**Belohnung (Weltkonzept 10.3):** `Geld = Basis(Klasse, Tier) × rewardFactor × (1 + Zeitbonus + Zielbonus) × Skill(econ.rewardMoney) − Schadenskosten`; Fehlschlag kostet `failPenaltyPct` der Basis.

**Gesetz:** Bußgelder je Typ × `fineFactor` × (1 + Aegis × 0,008). Was nicht bezahlt werden kann, wird zu „offenen Bußgeldern“, die zur Hälfte von Einnahmen gepfändet werden und Kredite sperren. Festnahme: `300 + 250 × Sterne + 6 % Kontostand`. Bestechung: `320 × Sterne^1,55 × bribeFactor × (1 + Aegis/200)`, Erfolg `0,90 − 0,13 × (Sterne−1) + KCG-Stand × 0,003 − (Aegis−50) × 0,002`; Erfolg senkt die Bezirks-Heat in `UMTAHeatSubsystem` um die Sternzahl, Misserfolg +1 Heat, Geld weg.

**Aurelia-Kredit („Phase Zwei“):** Limit `5000 + 1500 × (Level−1) + Aurelia-Stand × 60`, skaliert 1,2…0,6 mit Aegis. Tageszins `1 % + (Aegis − 50) × 0,025 % − Aurelia-Stand × 0,006 %`, geklemmt 0,4–4,5 %. Ab Tag 4 zieht Aurelia täglich 5 % automatisch ein; 5 verpasste Tage → Zahlungsausfall: Inkasso 30 %, Aegis +8, Aurelia-Stand −10.

**Protokoll:** Ring-Puffer (256 Buchungen, `GetRecentTransactions`), Kontostand-Stichproben alle 6 Spielstunden (512, `GetBalanceHistory`), Summen je Kategorie.

### Balancing-Tabelle (Planwerte, Quelle der Wahrheit ist `economy.json`)

| Größe | Wert | Ziel/Begründung |
|---|---|---|
| Startgeld | 500–2.500 (Herkunft) | erstes Werkzeug/Reparatur möglich, kein Autokauf |
| Gig Tier 1/2/3 | 150 / 350 / 700 | 10.3; Tier 2 ab 8 h, Tier 3 ab 30 h |
| Rennen / Verfolgung / Vertrag | 200–1.500 / 400–1.000 / 1.500–4.000 | 10.3 |
| Einkommen Mittelspiel | Ziel 4.000–6.000 $/h | 12.4 |
| Mittelklassewagen (Halvard Meridian 16.500 $) | nach 3–4 h | 12.4 |
| Fahrzeuge | 3.800 – 78.000 (Exot 138.000, nicht kaufbar) | 12.4 |
| Reparatur | 50 + 3,2 × Schaden% + 0,6 % Wert × Schaden | 50–480 $ Mittelklasse |
| Tuning | 250 – 11.000 $ je Teil | 12.3 |
| Garage 2/4/8/16 | 8.000 / 14.000 / 24.000 / 40.000 + Miete 40–200 $/Tag | 12.3 |
| Versicherung | 0,35 / 0,55 / 0,85 % Zeitwert pro Tag, Deckung 50/80/100 % | laufende Senke |
| Kennzeichenwechsel | 450 legal (−6 Aegis) / 1.400 Schwarzmarkt (−12 Aegis) | Aegis-Senke |
| Bezirksfaktoren Belohnung | Core 1,3 · Greenmoor 1,2 · Sunstrip 1,1 · Lantern 0,9 · Saltmarket 0,8 · Rustline 0,7 | 10.3 |

**Senken gegen Geldschwemme:** Fahrzeugleiter, Tuning, Garagenmiete, Versicherung (∝ Flottenwert), Modul-Wartung, Bußgelder mit Aegis-Aufschlag, Kreditzinsen, Kennzeichen.

### `RunEconomySimulation(Hours = 1000, Seed = 1337)`
BlueprintCallable. Sichert den Zustand (`ToJson`), simuliert einen neutralen Charakter (ohne Skill-Modifikatoren, Level ≈ 1 + 1,5·√h) mit gewichteten Aktionen (`simulation.weights`/`minutes`; Hauptmissionen/Nebenquests/Verträge gedeckelt), Unfällen, Bußgeldern, Festnahmen/Bestechung, Schwarzmarktfahrten, Fahrzeugleiter, Tuning, Garagenausbau, Versicherung/Miete, Kennzeichenwechseln und Kredit bei Pleite; stellt danach den echten Zustand wieder her. Report `FMTAEconomySimReport`: Einkommen und Netto je Abschnitt (0–10, 10–50, 50–200, 200+ h), Preisindex Start/Ende/Min/Max, Pleite-Anteil, Stunde des ersten Mittelklassewagens, Befunde (`bInflation`: Preisindex > 1,25 oder Spätspiel-Netto > 9.000 $/h; `bDeflation`: Preisindex < 0,8 oder > 10 % der Stunden pleite), CSV-Verlauf alle 25 h im `Log` und als `ECONSIM`-Zeile im Output-Log. **Noch nicht ausgeführt** (keine Compiler-/Editor-Läufe erlaubt) – erster Lauf nach dem Kompilieren, Werte dann in `economy.json` justieren.

## 3. Skills und Fortschritt (`skills.json`)

- **XP/Level:** `XP(Level) = Σ 900 × l^1,32`, max. Level 50, 1 Perk-Punkt je Stufe, +1 alle 5 Stufen. Quellen in `xpSources` (Gig 120 … Vertrag 650), `AwardXpFromSource("drift_100m", Meter/100)`.
- **Meisterschaft (nutzungsbasiert, 9.3):** `treeXpSources` verbinden Tätigkeiten mit Bäumen (Drift → Fahren, Kamera-Hack → Technik, Dialog → Netzwerk, Untertauchen → Tarnung); Schwellen 250/700/1.600/3.200/6.000. Tier-2/3/4-Skills verlangen Meisterschaft 1/2/3–4 → Skills folgen dem Spielstil, Perk-Punkte begrenzen.
- **4 Bäume × 12 Skills = 48 Skills**: Fahren, Technik/Hacking, Verhandeln/Netzwerk, Untertauchen/Aegis-Tarnung; jeweils Ränge, Kosten je Rang, Voraussetzungen (`"id"` oder `"id:Rang"`), Stufe, Meisterschaft, Effekte (Modifikatoren je Rang, gestapelt) und Tags (`ability.pit`, `ability.hack_traffic`, `ability.camera_hack_remote`, `unlock.gig_tier3_all` …).
- **Umschulung:** `RespecTree()` erstattet Punkte gegen 750 $ + 120 $/Punkt.
- **Ruf:** Stufen Feind/Misstrauisch/Neutral/Verbündet/Vertraut (−100/−40/−10/25/60). 14 Belohnungen (Union, Collective, Wrenchers, Aurelia, KCG) zahlen einmalig Geld/Perk-Punkte/Gegenstände und halten Effekte/Tags aktiv, solange die Schwelle gehalten wird; 5 Strafen (≤ −40) wirken, solange der Stand darunter liegt. `EvaluateReputationRewards()` nach jeder Mission aufrufen.

## 4. Modifikatoren (Stat-Namen)

Endwert = `(Basis + ΣAdd) × ΠMul` über Skills, Ruf, Herkunft (`origin`) und externe Quellen (`SetExternalModifiers`).

| Gruppe | Stats |
|---|---|
| Fahrzeug (`FMTAVehicleStats::ApplyModifier`) | `vehicle.mass, power, torque, rpm, topSpeed, shiftTime, grip, cornering, brake, springRate, damping, steer, drag, downforce, durability, damageResist, visibility, noise, cargoKg, cargoSlots, fuelUse` |
| Wirtschaft | `econ.buyPrice, sellPrice, rewardMoney, repairCost, fuelCost, towCost, tuningCost, insuranceCost, plateCost, fineAmount, bribeCost, bribeSuccess(add), loanRate, blackMarketBuy, blackMarketSell` |
| Aegis/Heat (für Heat-/Kamera-Systeme anderer Agenten) | `aegis.gain, aegis.decay, aegis.plateDetect, aegis.plateSwap, aegis.trafficCover, aegis.cap, heat.gain, heat.decay, heat.arrestTime` |
| Technik/Sozial | `tech.hackSpeed, tech.hackRange, tech.hackTrace, social.persuade(add)` |
| Inventar/XP | `inv.slots, inv.weight, inv.confiscateChance, inv.trunkHide, item.repairKit, xp.gain, xp.drift, standing.gain` |

Abfrage aus jedem System: `GetGameInstance()->GetSubsystem<UMTAProgressionSubsystem>()->ApplyStat("heat.decay", BaseDecay)`.

## 5. Herkünfte (`origins.json`)

| Id | Name | Geld/Schuld | Aegis | Stände | Startfahrzeug | Garage | Vorteil / Nachteil |
|---|---|---|---|---|---|---|---|
| `racer` | Rennfahrerin | 800 / 0 | ±0 | Wrenchers +20, KCG −5 | Rookfield Bulwark 440 | Rustline | Grip +3 %, Drift-XP +20 % / Heat +5 % |
| `harbor_kid` | Hafenkind | 600 / 0 | −5 | Union +20, Aurelia −5 | Halvard Wren Kombi | Saltmarket | Überzeugen, Hehlerpreise / Belohnung −3 % |
| `corporate_dropout` | Konzern-Aussteigerin | 2.500 / 3.000 | +20 | Aurelia +10, Collective −5 | Nauvik Kettle EV | – | Hack +20 %, Spur −15 % / Kennzeichenerkennung +10 % |
| `ex_guard` | Ex-Streifenfahrerin | 1.400 / 0 | +5 | KCG +10, Union −10, Collective −10 | Halvard Meridian | – | Heat-Zerfall, Bestechung / Standgewinn −10 % |
| `lantern_courier` | Lantern-Kurierin | 500 / 0 | −15 | Collective +20, Aurelia −10, KCG −5 | Velloro Strix 900 | Lantern Hill | Blinde Routen / Kreditzins +25 % |

Jede Herkunft: 2 Perk-Punkte, 2 freie Startskills, Baum-XP, Startgegenstände, eigene Dialogflags (`origin.<flag>` im Story-Flag-Speicher, plus `origin` und `prolog` = Prolog-Variante).

## 6. Fahrzeuge, Tuning, Pawn-Abbildung

24 Modelle (Kompakt 3, Limousine 3, SUV 3, Sport 3, Muscle 3, Van 2, Truck 2, Motorrad 3, Streife 2), Raritäten Gewöhnlich…Einzelstück, Händler je Bezirk, Fraktionsanforderungen, alternative Beschaffung (`stolen_only`, `contract`, `reward`, `auction`). 31 Tuning-Teile in 9 Kategorien (Motor 4, Getriebe 3, Reifen 4, Bremsen 3, Federung 3, Panzerung 3, Lack 5, Unterboden 3, Module 3), mit Kosten, Fraktionsanforderung, `illegalIn` und Wartungskosten. Klassenregeln legen erlaubte Kategorien fest (Motorrad ohne Panzerung, Truck ohne Unterbodenbeleuchtung).

`UMTAGarageSubsystem::ComputeStats(Id)` = Katalog-Basis → Tuning → `vehicle.*`-Skills/Herkunft/Ruf → Schadensabzug (ab 30 % Schaden bis −35 % Leistung, −15 % Grip) → heißes Kennzeichen (+0,25 Auffälligkeit) → `Finalize()` (0–100-Schätzung, Leistungsindex 100–999).

**Abbildung auf Chaos (im Pawn, z. B. `AMTAVehiclePawn::ApplyProgressionStats`, Datei gehört einem anderen Agenten):**

| `FMTAVehicleStats` | Chaos/Pawn (UE 5.8, geprüft) |
|---|---|
| `MassKg` | `UChaosVehicleMovementComponent::Mass` (vor Physik-Init) bzw. `UpdatedPrimitive->SetMassOverrideInKg` |
| `MaxTorqueNm`, `MaxRPM` | `UChaosWheeledVehicleMovementComponent::SetMaxEngineTorque()`, `EngineSetup.MaxRPM` |
| `GearShiftTime` | `TransmissionSetup.GearChangeTime` |
| `Grip` | `SetWheelFrictionMultiplier(i, Grip)` je Rad |
| `CorneringStiffness` | `UChaosVehicleWheel::CorneringStiffness` (Rad-Klasse/Instanz) |
| `BrakeTorqueNm` | `SetWheelMaxBrakeTorque(i, …)` |
| `SpringRate`, `SuspensionDamping` | `SetSuspensionParams(Rate, Damping, Preload, MaxRaise, MaxDrop, i)` |
| `SteerAngleDeg` | `SetWheelMaxSteerAngle(i, …)` (nur Vorderräder) |
| `DragCoefficient`, `Downforce` | `SetDragCoefficient()`, `SetDownforceCoefficient()` |
| `TopSpeedKmh` | `AMTAVehiclePawn::MaxSpeedKmh` |
| `Durability`, `DamageResist` | `MaxHealth`, `DamagePerMps × (1 − DamageResist)` |
| `AegisVisibility`, `Noise` | Kamera-Erkennungsrate / Hörweite (Heat-/KI-Gruppe) |
| `CargoSlots`, `CargoKg` | Kofferraum im Inventar |

```cpp
// Pawn (BeginPlay und bei OnVehicleStatsChanged):
const FMTAVehicleStats S = GetGameInstance()->GetSubsystem<UMTAGarageSubsystem>()->GetActiveVehicleStats();
if (auto* Move = Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent()))
{
    Move->SetMaxEngineTorque(S.MaxTorqueNm);
    Move->SetDragCoefficient(S.DragCoefficient);
    Move->SetDownforceCoefficient(S.Downforce);
    for (int32 i = 0; i < Move->WheelSetups.Num(); ++i)
    {
        Move->SetWheelFrictionMultiplier(i, S.Grip);
        Move->SetWheelMaxBrakeTorque(i, S.BrakeTorqueNm);
    }
}
MaxSpeedKmh = S.TopSpeedKmh; MaxHealth = S.Durability;
// beim Aussteigen/Speichern: Garage->SyncFromPawn(Id, GetHealthFraction(), Fuel, DrivenKm);
```

**Kennzeichen:** `ChangePlate(Id, bBlackMarket, District, Reason)` kostet 450/1.400 $ (mit Rohling/Klon im Inventar 35 %), senkt das Aegis-Profil um 6/12 + 10 % der Kennzeichen-Heat (+4 mit Klon, × `aegis.plateSwap`), setzt `PlateHeat` und „heiß“ zurück. Kamera-Treffer meldet das Aegis-System über `RegisterPlateSighting(Id, Amount, bDuringCrime)`.

## 7. Inventar und Festnahme

Tasche 12/20/30 Slots (Ausbau) und 25 kg, Kofferraum je Fahrzeug-Instanz (Slots/kg aus Fahrzeugwerten). Kategorien: Werkzeug, Hacking-Dongle (Ladungen), Kennzeichen, Beweise, Schmuggelware, Reparatursets, Verbrauchsgüter, Schlüssel. Story-Gegenstände passen immer und sind geschützt. Handel: legal beim Händler (Preisformel), illegal nur in Schwarzmarkt-Bezirken (Kurse aus Economy, Aegis-Risiko).

**Festnahme:** `ProcessArrest(District, Sterne)`: Festnahmestrafe; je illegalem Stück Chance `(0,35 + 0,12 × Sterne) × Risiko` (Kofferraum × 1,25 × `inv.trunkHide`, gesamt × `inv.confiscateChance`); Strafe 50 % des beschlagnahmten Werts; ab 3 Sternen oder heißem Fahrzeug → Verwahrstelle. Anbindung an die KI-Gruppe **ohne deren Dateien zu ändern** – im GameMode:

```cpp
// MTATrafficTypes.h: FOnArrested(), FOnHeatChanged(float), FOnWantedLevelChanged(int32)
UMTAInventorySubsystem* Inv = GetGameInstance()->GetSubsystem<UMTAInventorySubsystem>();
WorldAI->OnArrested.AddDynamic(Inv, &UMTAInventorySubsystem::HandleArrested);
WorldAI->OnHeatChanged.AddDynamic(Inv, &UMTAInventorySubsystem::HandleHeatChanged);
WorldAI->OnWantedLevelChanged.AddDynamic(Inv, &UMTAInventorySubsystem::HandleWantedLevelChanged);
// bei Bezirkswechsel:
Inv->SetCurrentDistrict(MTAProgression::DistrictToId(MTADistrict::ClassifyNormalized(U, V)));
```

## 8. GameMode/HUD-Anbindung

```cpp
// Neues Spiel (Charaktererstellung):
GI->GetSubsystem<UMTACharacterOriginSubsystem>()->ApplyOrigin("harbor_kid", TEXT("Mira Voss"), 2);
// Spielzeit (Tag/Nacht-System) jede Spielminute/-stunde:
Econ->AdvanceTime(DeltaGameHours);            // Märkte, Zinsen, Tageswechsel -> Versicherung/Miete
// Story-Geld weiterreichen statt int-Feld im GameMode:
Econ->AddMoney(Story->ConsumeMoney(), EMTATransactionCategory::Mission, MissionId);
Prog->AwardXpFromSource("main"); Prog->EvaluateReputationRewards();
// Gigs:
Econ->PayMissionReward("gig", Tier, District, TimeBonus, GoalBonus, DamageCost, bSuccess);
// Bestechung im Verfolgungs-Dialog:
FMTABribeResult R = Econ->TryBribe(District, Stars);
```

HUD bindet `OnMoneyChanged`, `OnTransaction` (Toast „+$350 Gig“), `OnLevelUp`, `OnSkillUnlocked`, `OnReputationReward`, `OnVehicleEvent`, `OnItemsConfiscated`; Konto-App: `GetRecentTransactions(20)`, `GetBalanceHistory()`, `GetLoanState()`.

## 9. Persistenz (SaveManager / `MTASaveGame`, gehört einem anderen Agenten)

Jede Einheit hat `TSharedRef<FJsonObject> ToJson() const` / `bool FromJson(const TSharedPtr<FJsonObject>&)` mit Feld `schema` (aktuell 1; neuere Version → Laden verweigert, fehlende Felder → Defaults, entfernte Skills/Modelle/Gegenstände werden verworfen) sowie BP-Wrapper `SaveStateToString()/LoadStateFromString()`.
Für den SaveManager gibt es ein Bündel:

```cpp
// Speichern
const FString Progress = GI->GetSubsystem<UMTAProgressionSubsystem>()->ExportProgressionBundle();
SaveGame->ProgressionJson = Progress;   // FString-UPROPERTY im SaveGame oder Teilobjekt "progression" im JSON-Spielstand (11.2)
// Laden (nach Story->StateFromJson, da Ruf-Effekte den Story-Stand lesen)
GI->GetSubsystem<UMTAProgressionSubsystem>()->ImportProgressionBundle(SaveGame->ProgressionJson);
```

Bündel: `{ "schema":1, "origin":{…}, "economy":{money, loan, scarcity, blackMarket, transactions, balanceHistory, …}, "progression":{xp, level, perkPoints, skills, treeXp, claimedRewards, activeRep, external}, "garage":{garages, vehicles[…]}, "inventory":{personal, trunks, slotLevel} }`. Import-Reihenfolge: Herkunft → Fortschritt → Wirtschaft → Garage → Inventar. Migrationen laufen je Teilobjekt über dessen `schema` (Kap. 11.4).

## 10. Risiken und offene Punkte

- Nicht kompiliert; UHT/Compiler-Fehler sind möglich (Signaturen gegen 5.8-Header geprüft: `FJsonObject` mit `FStringView`-Schlüsseln, `FSharedString`-Schlüssel in `Values` → `*Pair.Key`, `InitializeDependency<T>`, Chaos-Setter).
- `MTAStorySubsystem.cpp` nutzt `Standing.Add(Pair.Key, …)` mit `Pair.Key` vom Typ `UE::FSharedString` (UE 5.8 Standard) – vermutlich Compilerfehler in der Story-Datei (anderer Agent); Abhilfe `FString(*Pair.Key)` oder `UE_JSONOBJECT_LEGACY_STRING_KEYS=1`.
- Story-Brücke per JSON-Rundreise ist korrekt, aber nicht billig (Aegis-Lesen parst den Story-Zustand); bei Bedarf `SetAegisOverride()` vom GameMode setzen oder der Story-Engine später echte Setter geben.
- Balancing unerprobt, bis `RunEconomySimulation` einmal lief; Spätspiel-Geldschwemme ist das wahrscheinlichste Ergebnis (Senken ergänzen: Immobilien, Prestige-Fahrzeuge).
- Heat-/Aegis-Modifikatoren (`heat.*`, `aegis.*`) wirken erst, wenn Heat-/Kamera-Systeme `ApplyStat` aufrufen.
