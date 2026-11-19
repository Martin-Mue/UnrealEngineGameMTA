# MTA 6 (Godot 4.7)

Eigenständiges Open-World-Fahrspiel, läuft ohne Konto. Engine: Godot 4.7.2 (MIT-Lizenz), installiert über `winget install GodotEngine.GodotEngine`.

## Starten
```powershell
godot --path "E:\Projekte\GTA 6\godot"
```
Oder `project.godot` im Godot-Editor öffnen (`godot --editor --path ...`).

## Steuerung
W/S Gas/Bremse · A/D Lenken · Leertaste Handbremse · R zurücksetzen

## Neu (Iteration 3): Story-Kern
- Datengetriebene Story (`story.gd`, `data/story.json`): 10 Hauptmissionen, 4 Fraktionsstände, Entscheidungen mit Folgen, 4 Enden (Goldener Käfig, Hafenrepublik, Blackout, Exil)
- Dialogfenster am Missionsziel: Optionen mit Tasten 1–4, Enter weiter; Story-Zustand wird mitgespeichert (Speicherstand v2)
- Story-Simulator: `godot_console --headless --path . --script res://tests/story_sim.gd` (3000 Durchläufe, prüft Enden und Sackgassen)

## Neu (Iteration 2)
- Fußgänger (60), Polizei mit Fahndungsstufen 0–5 (jagt per Straßenraster, Festnahme bei Stillstand), Kollisionen mit Verkehr
- Missionen: gelber Marker, Geld und Zeitbonus; Minimap (rotiert mit dem Auto)
- Speichern/Laden: F5 / F9 (`user://savegame.json`, Versionsfeld), Autosave nach jeder Mission
- Grafik: SDFGI, Volumennebel, Laternen mit Licht-Pool, Bäume, Gebäudedetails (~165 FPS auf RX 5700 XT)

## Inhalt (Stand Iteration 1)
- 8×8-Stadt mit Straßenmarkierungen (Shader), Hochhäusern mit leuchtenden Fenstern bei Nacht
- Auto mit Fahrphysik (`VehicleBody3D`), Verfolgerkamera, Scheinwerfer
- 28 Verkehrsautos auf dem Straßenraster
- Tag/Nacht-Wechsel (1 Stunde = 20 s), SSAO, SSR, Glow, Tonemapping (ACES)

Test ohne Fenster: `godot_console --headless --path . -- --test` (gibt Position/Geschwindigkeit nach 3 s aus).
