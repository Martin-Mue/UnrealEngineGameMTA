# Fototexturen (Boden, Fassaden, Fels, Gelände)

Die Stadt- und Wildnismaterialien verwenden echte PBR-Fototexturen statt reinem HLSL-Hash-Rauschen.

## Quellen und Lizenz

Alle Texturen stammen von **ambientCG** (https://ambientcg.com) und stehen unter **CC0 1.0** (Public Domain,
keine Namensnennung nötig, kommerziell frei). Verwendet wird die 1K-JPG-Variante.

| Id          | Verwendung                                                   | Karten                |
|-------------|--------------------------------------------------------------|-----------------------|
| Asphalt031  | Fahrbahn (M_MTA_Ground), Küstenstraße/Route 9 (M_MTA_Road)   | BC, N, R, AO          |
| Concrete034 | Gehweg, Beton-/Putzfassaden, Flachdächer, Kaimauern/Molen   | BC, N, R (kein AO)    |
| Bricks097   | Backsteinfassaden (M_MTA_Building)                           | BC, N, R, AO          |
| Metal064    | Rost (Rustline), Stahlbrüstungen (M_MTA_Building)            | BC, N, R              |
| Grass004    | Rasen (Ground außerhalb, Park), Waldboden/Wiese, Bergwiesen  | BC, N, R, AO          |
| Ground037   | Erde/Schlamm/Schotter (Terrain), Sand/Erde (M_MTA_Concrete) | BC, N, R, AO          |
| Rock064     | Fels nach Hangneigung (Terrain, Berge)                       | BC, N, R, AO          |

BC = Color, N = NormalDX (UE nutzt die DirectX-Konvention), R = Roughness, AO = AmbientOcclusion.

## Pipeline

1. **Quellen laden** (nur auf neuen Rechnern nötig, die Quellen sind nicht eingecheckt):
   `powershell -File tools/fetch_photo_textures.ps1` lädt `https://ambientcg.com/get?file=<Id>_1K-JPG.zip`
   nach `tools/photo_textures/<Id>/` und entpackt sie (`-Force` lädt erneut).
2. **Importieren** (Editor-Commandlet):
   ```
   UnrealEditor-Cmd.exe "E:\Projekte\GTA 6\MTA6.uproject" -run=pythonscript
       -script="E:/Projekte/GTA 6/tools/ue/import_photo_textures.py" -unattended -nosplash -stdout
   ```
   Ergebnis: `/Game/MTA6/Textures/Photo/<Id>/T_<Id>_{BC,N,R,AO}`.
   Einstellungen: BC sRGB + TC_Default, N TC_Normalmap (sRGB aus, kein Green-Flip), R/AO TC_Masks (sRGB aus),
   Wrap/Wrap, Mips aus der Texturgruppe (World / WorldNormalMap), Max Texture Size 1024.
   Die importierten `.uasset` unter `Content/MTA6/Textures/Photo` werden eingecheckt.
3. **Materialien bauen**: `tools/ue/build_materials.py` (gleicher Commandlet-Aufruf). Fehlt eine Textur,
   bricht das Skript mit einer Fehlermeldung ab.

## Aufbau der Materialien

- Je Material rechnet ein kleiner **UV-Custom-Node** world-aligned Kachel-UVs: Draufsicht für Böden,
  Wandprojektion (U entlang der Wand, V = Höhe) für Fassaden, Bordsteine und Kaimauern, zwei
  Seitenprojektionen (X/Y, gewichtet mit |n|⁴) für Fels. Jede Kachel hat eine zweite, 4.37-mal größere und
  versetzte Variante ("_Far"), die leicht eingemischt wird und Wiederholungsmuster bricht; dazu weiches
  Value-Noise für großflächige Helligkeitsvariation.
- Die Texturen werden über echte **TextureSampleParameter2D**-Knoten (Parametergruppe "Photo", geteilter
  Wrap-Sampler, zählt nicht gegen das 16-Sampler-Limit) gesampelt. Der Haupt-Custom-Node bekommt nur die
  Sample-Ergebnisse als Eingänge und mischt sie mit den bisherigen prozeduralen Masken (Straßenraster,
  Markierungen, Zebrastreifen, Fenster/Läden/Neon, Bezirksdetails, Nässe, Nacht über `MPC_MTA6_World`).
- Normalen werden im Custom-Node in den **Weltraum** gedreht (`tangent_space_normal = False`), damit keine
  Mesh-Tangenten nötig sind (ISM-Würfel, ProceduralMesh). Nässe glättet die Normalen.
- Gebäude: Backstein oder Beton je Bezirk und Seed (Saltmarket immer Backstein, Greenmoor 55 %, Industrie
  ≥ 50 %, Lantern Hill 40 %, Core 12 %, Sunstrip nie, Glas/Stahl nie). Beton wird mit der Custom-Data-Tönung
  multipliziert, Backstein nur leicht getönt. Fenster und Emissive sind unverändert prozedural.
- Terrain: die Vertex-Farbe entscheidet Gras (grünlicher Anteil) oder Erde und tönt die Textur
  (`VertexColorInfluence`), Fels nach Hangneigung (nicht auf Wegen, Vertex-Alpha).

Texturen pro Material: Ground 13, Building 11, Terrain 17, Mountain 10, Concrete 6, Park 5, Road 4.

## Parameter zum Tunen (Gruppe "Photo", per Material-Instanz überschreibbar)

| Material        | Parameter                                   | Standard | Wirkung                                   |
|-----------------|---------------------------------------------|----------|-------------------------------------------|
| alle            | NormalStrength                              | 1.0      | Stärke der Normalmaps                     |
| Ground/Building/Park/Terrain | AOStrength                      | 1.0      | Einfluss der AO-Karten                    |
| Ground, Road    | AsphaltTile / AsphaltBrightness             | 3 m / 0.6| Kachelgröße / Albedo-Faktor Asphalt       |
| Ground, Building, Concrete | ConcreteTile                     | 4 m      | Breite einer Betonkachel (Höhe = halbe)   |
| Ground          | ConcreteBrightness, MarkingBrightness       | 0.8, 1.0 | Gehweg-Helligkeit, Markierungsfarbe       |
| Ground, Park, Terrain, Mountain | GrassTile, GrassTint        | 2.5 m, (1.15,1.3,1.9) | Rasenkachel, Farbkorrektur (Grass004 ist sehr gelb) |
| Building        | BrickTile, BrickBrightness, BrickShare      | 2.4 m, 1.0, 1.0 | Backsteinkachel, Helligkeit, Anteil (0 = kein Backstein) |
| Building        | ConcreteGain, MetalTile                     | 2.0, 2 m | Beton-Helligkeit relativ zur Tönung       |
| Terrain         | RockTile, RockBrightness, DirtTile, DirtTint, VertexColorInfluence | 10 m, 1.4, 3 m, (0.75,0.72,0.9), 0.5 | Fels/Erde |
| Mountain        | RockTile, RockBrightness                    | 14 m, 1.6| Fels an Bergen                            |

Faustregeln: Albedo von Asphalt ≈ 0.08–0.12, Beton ≈ 0.3–0.4, Rasen ≈ 0.1–0.2 (linear). Sieht eine Fläche
gekachelt aus, Kachelgröße erhöhen oder den "_Far"-Anteil im HLSL (lerp-Faktor 0.35–0.45) anheben.

## Bekannte Grenzen

- Rock064 ist eine moosige Bruchsteinmauer; an großen Klippen wirkt das wie geschichtetes Mauerwerk.
  Eine echte Klippen-Textur (z. B. ambientCG "Rock0xx" mit Schichtung) wäre besser.
- Die Screenshot-Kontrolle (`-MTA6Shots`) läuft derzeit bei Nacht; Fassaden sind dort fast nur über die
  Fenster sichtbar.
