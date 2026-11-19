# MTA 6 (Godot 4.7)

Eigenständiges Open-World-Fahrspiel, läuft ohne Konto. Engine: Godot 4.7.2 (MIT-Lizenz), installiert über `winget install GodotEngine.GodotEngine`.

## Starten
```powershell
godot --path "E:\Projekte\GTA 6\godot"
```
Oder `project.godot` im Godot-Editor öffnen (`godot --editor --path ...`).

## Steuerung
W/S Gas/Bremse · A/D Lenken · Leertaste Handbremse · R zurücksetzen

## Inhalt (Stand Iteration 1)
- 8×8-Stadt mit Straßenmarkierungen (Shader), Hochhäusern mit leuchtenden Fenstern bei Nacht
- Auto mit Fahrphysik (`VehicleBody3D`), Verfolgerkamera, Scheinwerfer
- 28 Verkehrsautos auf dem Straßenraster
- Tag/Nacht-Wechsel (1 Stunde = 20 s), SSAO, SSR, Glow, Tonemapping (ACES)

Test ohne Fenster: `godot_console --headless --path . -- --test` (gibt Position/Geschwindigkeit nach 3 s aus).
