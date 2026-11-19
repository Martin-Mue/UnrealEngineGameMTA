# MTA 6 – Unreal-Engine-5-Setup

Der Prototyp im Browser (`index.html`, `game.js`) bleibt als Spielideen-Testfeld. Das eigentliche Spiel entsteht als Unreal-Projekt (`MTA6.uproject`, C++).

## Stand des PCs (geprüft am 19.11.2026)
- GPU: AMD Radeon RX 5700 XT (RDNA 1, 8 GB, kein Hardware-Raytracing) → Lumen läuft nur per Software-Raytracing, Nanite und Virtual Shadow Maps müssen auf Funktion geprüft werden. Als Zielbild: 1080p, Preset „Mittel/Hoch“, Upscaler (FSR/TSR).
- CPU/RAM: Ryzen 5 3600, 64 GB
- Platz: E: 381 GB frei, D: 522 GB frei
- Visual Studio 2022 Community mit C++-Desktop-Workload ist da. Für Unreal fehlt wahrscheinlich noch die Workload „Spieleentwicklung mit C++“ bzw. Windows-SDK und .NET-Komponenten.
- Unreal Engine ist **nicht installiert**.

## Was du einmalig selbst tun musst
Die Installation braucht dein Epic-Konto, die Lizenzbestätigung und mehr als 100 GB Download. Das kann ich nicht für dich erledigen.
1. Epic Games Launcher installieren, anmelden, unter „Unreal Engine → Bibliothek“ UE 5.5 oder neuer (empfohlen: aktuelle 5.x) installieren, Zielplattform Windows. Installationsort z. B. `D:\UE`.
2. Visual Studio Installer öffnen → „Ändern“ → Workload „Spieleentwicklung mit C++“ aktivieren (inkl. Windows-SDK, MSVC).
3. `MTA6.uproject` mit Rechtsklick → „Visual-Studio-Projektdateien generieren“, dann doppelklicken. Passt die Engine-Version nicht zur Datei, im Dialog die installierte Version wählen.

## Projektstruktur
- `Source/MTA6/` – C++-Modul (GameMode, Bezirks-Fahndung `UMTAHeatSubsystem`)
- `Config/` – Rendering-Einstellungen (DX12, SM6, Lumen, Nanite, VSM)
- `docs/` – Konzept, Iterationsplan, dieses Dokument

## Hinweise
- Das Gerüst wurde von Hand geschrieben und **noch nicht kompiliert**, weil Unreal fehlt. Erster Schritt nach der Installation: Projekt öffnen und bauen, Fehler beheben.
- Welt: Streaming über World Partition, Straßen/Verkehr über Mass-Entity-Systeme, Fahrzeuge über Chaos Vehicles. Mehrspieler später über Dedicated Server (Unreal-Replikation).
- Asset-Quellen für Realismus: Quixel Megascans (Fab), eigene Modelle, Marktplatz-Pakete. Ich liefere Code, Systeme und Konfiguration, keine fertigen AAA-Modelle.
