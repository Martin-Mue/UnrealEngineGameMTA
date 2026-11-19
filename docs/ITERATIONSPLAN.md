# MTA 6 – Iterationsplan

Ziel: ein eigenständiges Open-World-Spiel (Browser/PC, Three.js) mit lebendiger Stadt. Kein Nachbau von GTA oder Rockstar-Inhalten.
Details zur Welt: `docs/WELTKONZEPT.md` (wird von der Session „mta6-weltkonzept“ geschrieben).

## Leitlinien des Nutzers (gelten für alle Iterationen)
- Extremer Realismus und Liebe zum Detail (PBR-Materialien, glaubwürdige Beleuchtung, Details wie Gullideckel, Werbung, Verschleiß)
- Ausgereifte Story mit vom Spieler abhängigen Wendungen (Entscheidungen, Ruf, Fraktionen, Konsequenzen, mehrere Enden)
- Open World und MMO gemischt, in einer realistischen, gern dystopischen Welt
- Ehrlicher Hinweis: Der MMO-Teil braucht einen Server (Node/WebSocket) und kommt erst nach stabiler Einzelspieler-Basis; Realismus ist im Browser durch Performance begrenzt, Ziel ist der bestmögliche Look innerhalb dieser Grenzen.

## Plattformwechsel (19.11.2026)
Auf Wunsch des Nutzers wird MTA 6 als **Unreal-Engine-5-Projekt** gebaut (siehe `docs/UNREAL_SETUP.md`). Der Browser-Prototyp bleibt als Testfeld. Projektgerüst steht (`MTA6.uproject`, `Source/`, `Config/`), UE ist noch nicht installiert, nichts ist kompiliert. Die folgenden Iterationen gelten jetzt für Unreal (Iteration 5 „ES-Module“ entfällt, stattdessen C++-Module und World Partition).

Status: [ ] offen · [~] in Arbeit · [x] fertig

## Iteration 1 – Prototyp lauffähig [x]
Geprüft per Simulation: keine Konsolenfehler, Fahren/Lenken, Verkehr, Polizei bei Wanted funktionieren. Visuelle Prüfung im Browser steht noch aus (Pane war verborgen).
- `node --check game.js` ohne Fehler
- Im Browser starten, Konsole ohne Fehler, Fahren/Verkehr/Fußgänger/Polizei/Marker funktionieren
- Gefundene Bugs beheben
- Done: Spiel startet, man kann fahren, Marker abholen, Polizei kommt bei Wanted

## Iteration 2 – Spielgefühl und Details [ ]
- Fahrphysik (Drift, Gewicht), Bremslichter, Reifenspuren/Rauch, Sound (Hupe, Crash, Sirene)
- Straßenlaternen, Ampeln, Fahrzeugvarianten (Limousine, SUV, Bus, Truck)
- Done: Fahren fühlt sich gut an, Stadt wirkt nachts beleuchtet

## Iteration 3 – Welt: Bezirke, Küste, Landmarken [ ]
- 5–6 Bezirke mit eigener Optik (Downtown, Hafen, Strand, Wohnviertel, Industrie, Hügel)
- Wasser/Küste, Brücke, Highway, Landmarken
- Done: Bezirke sind auf der Karte und im Spiel klar unterscheidbar

## Iteration 4 – Zu Fuß, Polizei, Missionen [ ]
- Aussteigen/Einsteigen, Laufen, Fahrzeuge übernehmen
- Polizei-Eskalation (Straßensperren), Missionsketten mit kleiner Story
- Done: Eine Missionskette von 3–5 Aufträgen ist spielbar

## Iteration 5 – Modularisierung [ ]
- `game.js` in ES-Module aufteilen (world, vehicles, ai, police, missions, ui, audio)
- Streaming/LOD für größere Welt vorbereiten
- Done: Struktur wie im Weltkonzept, Spiel läuft unverändert
