# MTA 6: Figuren, Aussehen und Asset-Pipeline

Dieses Dokument beschreibt, wie aus den Daten und dem Code (`MTAAppearance*`, `MTAWardrobe*`, `MTACrowdLook*`, `MTACharacterCreator*`) echte Spielfiguren in Unreal Engine 5.8 werden. **Ehrliche Vorbemerkung:** Die KI kann keine fertigen 3D-Figuren erzeugen. Realistische Menschen entstehen über MetaHuman Creator oder Marktplatz-Pakete, die du selbst erstellst bzw. herunterlädst. Der Code ist deshalb assetunabhängig: Ohne Assets laufen Figuren als Zylinder-Platzhalter mit korrekten Größen, Farben und Daten; jedes importierte Asset ersetzt den Platzhalter, ohne dass Code geändert wird.

## 1. Überblick

| Baustein | Datei | Aufgabe |
|---|---|---|
| Aussehen als Daten | `MTAAppearance.h/.cpp`, `Content/Data/appearance.json` | `FMTAAppearance`, 45 Archetypen mit Bezirks-Wahrscheinlichkeiten, Haar-/Bart-/Marken-Kataloge, 9 Schlüsselfiguren, 6 Creator-Presets |
| Garderobe | `MTAWardrobe.h/.cpp`, `Content/Data/wardrobe.json` | 174 Kleidungsstücke, 71 Outfit-Presets, Kombinationsregeln, Wetteranpassung, Aegis-Auffälligkeit |
| Crowd | `MTACrowdLook.h/.cpp` | Seed-ID -> deterministisches Aussehen, Per-Instance-Custom-Data, Zylinder-Platzhalter |
| Charaktererstellung | `MTACharacterCreator.h/.cpp` | Slate-Widget ohne UMG, Live-Vorschau-Delegate, `IMTAOriginProvider`, JSON speichern/laden |
| Werkzeuge | `tools/ue_characters/*.py` | Daten-Assets anlegen, Meshes zuordnen, FBX importieren, Daten prüfen |

Datenfluss: `appearance.json` + `wardrobe.json` -> Subsysteme (laden beim Spielstart) -> `FMTAAppearance` -> Anwendung auf Komponenten (`UMTAAppearanceLibrary::ApplyMorphTargets / ApplyMaterialParameters`, `UMTAWardrobeSubsystem::ApplyOutfit`) bzw. auf Crowd-Instanzen (`UMTACrowdLookSubsystem::AddCrowdInstance`).

## 2. Hauptfiguren mit MetaHuman Creator

In Engine 5.8 liegt MetaHuman Creator als Engine-Plugin vor (`Engine/Plugins/MetaHuman/MetaHumanCharacter`, Anzeigename "MetaHuman Creator", Beta, standardmäßig **aus**). Zusätzlich gibt es "MetaHuman SDK" (standardmäßig an) und das experimentelle "MetaHuman Crowd". Das Plugin ist kostenlos, braucht aber ein Epic-Konto. Die genauen Menübezeichnungen können sich zwischen Patch-Versionen unterscheiden; prüfe sie im Editor.

1. **Plugin aktivieren:** `Bearbeiten > Plugins`, nach "MetaHuman" suchen, *MetaHuman Creator* aktivieren, Editor neu starten. Beim ersten Öffnen mit dem Epic-Konto anmelden und die Lizenzbedingungen selbst bestätigen.
2. **Figur anlegen:** Im Content Browser (z. B. `/Game/Characters/MetaHumans/`) per Rechtsklick/Add ein *MetaHuman Character* Asset erzeugen und öffnen. Dort Basis (Körper, Gesicht), Hautton, Haare, Augen, Kleidung gestalten.
3. **Zielwerte aus `appearance.json`** (Beispiel Mira Voss): Körpertyp athletisch, Größe 171 cm, Hautton 0.42 (Palette `skinTones`), Frisur Pferdeschwanz, Haarfarbe dunkelbraun, Narbe an der rechten Augenbraue. Die abstrakten Gesichtsparameter (`jaw_width`, `cheekbone_height` ...) sind Richtwerte für die eigenen Morph Targets; MetaHuman nutzt seine eigenen Regler, ordne sie sinngemäß zu.
4. **Zusammenbauen/Exportieren:** Das Asset im Editor assemblieren (Menüpunkt "Assemble" bzw. Build), damit Skeletal Meshes, Groom-Assets und Materialien als Projekt-Assets entstehen.
5. **Zuordnung in den Daten:** Hauptfiguren referenzieren das Mesh über ein Blueprint/Daten-Asset (`Characters/Hero/BP_Mira`), das den assemblierten MetaHuman enthält. In `appearance.json` bleibt `hair.groom` (Pfad-Platzhalter in `hairStyles`) leer, bis ein Groom zugeordnet ist.
6. **Aussehen-Daten-Assets:** `tools/ue_characters/create_appearance_assets.py` ausführen (siehe 5), dann `DA_mira_voss` etc. im Ordner `/Game/Characters/Appearance/Key`.

**Regel:** Haare als Strand-Grooms nur bei Hauptfiguren (Mira, Jonas, Kandel, Okoye, Brann, Nox, Yara, Dutch, Pia). Alle anderen: Haarkarten (Cards) bzw. Mesh-Haar.

## 3. Alternativen ohne MetaHuman

- **Fab-Pakete:** Auf Fab (Epic-Marktplatz) nach modularen Charakterpaketen mit Skelett und austauschbarer Kleidung suchen; Lizenz (kommerzielle Nutzung) und Skelettkompatibilität prüfen. Import über den Fab-Eintrag im Editor.
- **Mannequin-Retargeting:** Das Standard-Mannequin des Third-Person-Templates als gemeinsames Skelett nutzen; fremde Meshes per IK-Retargeter (Plugin *IK Rig*, in 5.8 vorhanden) auf dieses Skelett bringen.
- **Eigene Meshes aus Blender o. ä.:** Kleidung auf demselben Skelett modellieren, als FBX exportieren, `import_clothing_fbx.py` benutzen.
- **Ohne jedes Asset:** Zylinder-Platzhalter (Crowd) bzw. nur die Morph-/Material-Parameter; alles Spielerische (NPC-Typen, Kleidung als Daten, Aegis-Auffälligkeit) funktioniert trotzdem.

## 4. Kleidung als modulare Meshes

Konvention: `/Game/Characters/Clothing/<slot>/SK_<itemId>` (z. B. `/Game/Characters/Clothing/jacket/SK_jkt_denim`). Alle Teile teilen das Skelett des Körpers und laufen per *Leader Pose* (`SetLeaderPoseComponent`) mit. Materialparameter für Tints: `MTA_Tint` (Vektor). Körper-/Haut-/Haarmaterial: `MTA_SkinTone, MTA_Freckles, MTA_HairGray, MTA_MakeupIntensity, MTA_Age` (Skalar) und `MTA_SkinColor, MTA_EyeColor, MTA_HairColor, MTA_BeardColor, MTA_LipColor, MTA_EyeShadow` (Vektor). Morph Targets heißen `mta_<name>` (z. B. `mta_jaw_width`, außerdem `mta_build`, `mta_muscle`, `mta_age`); fehlende werden ignoriert.

Ablauf: FBX nach dem Namensschema `<slot>__<itemId>.fbx` ablegen, `import_clothing_fbx.py` ausführen, danach `assign_clothing_meshes.py` (trägt die Pfade in `wardrobe.json` im Feld `mesh` ein). Teile ohne Mesh bleiben Platzhalter.

## 5. Skripte (`tools/ue_characters`)

| Skript | Wirkung | Aufruf |
|---|---|---|
| `validate_character_data.py` | Prüft JSON-Querverweise (Archetyp -> Preset -> Kleidung, Marken, Haare). Läuft auch ohne Unreal. | `python tools/ue_characters/validate_character_data.py` |
| `create_appearance_assets.py` | Legt `UMTAAppearanceAsset`-Daten-Assets für Schlüsselfiguren und Creator-Presets an (nutzt `unreal.DataAssetFactory`, `EditorAssetLibrary`). | Editor: `Tools > Execute Python Script` |
| `assign_clothing_meshes.py` | Sucht `SK_<id>` unter `/Game/Characters/Clothing` und schreibt Pfade in `wardrobe.json`. | wie oben |
| `import_clothing_fbx.py` | Importiert FBX-Dateien als Skeletal Meshes (`AssetImportTask`, `FbxImportUI`). | Python-Konsole: `ic.run(Quellordner, Skelett-Pfad)` |

Voraussetzung: Das Modul `MTA6` ist kompiliert (die Klasse erscheint in Python als `unreal.MTAAppearanceAsset`). Das Projekt aktiviert `PythonScriptPlugin` und `EditorScriptingUtilities` bereits.

## 6. Performance-Budget (RX 5700 XT, 8 GB, Ryzen 5 3600)

| Kategorie | Richtwert | Anmerkung |
|---|---|---|
| Hauptfiguren (nah, < 10 m) | 80.000-120.000 Dreiecke, LOD0 | Nur hier Strand-Grooms, höchstens 1-2 gleichzeitig |
| NPCs mittlere Distanz | 15.000-25.000 Dreiecke (LOD1-2) | Haarkarten, keine Strands, keine Cloth |
| Crowd (Instanzen) | 1.500-5.000 Dreiecke | Instanced Static Mesh/Vertex-Animation, 1 Material, Variation über Custom Data |
| Skinning-Zone | höchstens ca. 30-60 voll animierte Skeletal Meshes gleichzeitig | Rest als Crowd-Instanzen, Animation per Vertex Animation Texture oder Mass |
| Texturen | 2K Haupt, 1K Crowd, 512 Kinder-/Nebenrollen | VRAM sparen, Streaming an |
| Materialien je Figur | 3-5 (Haut, Augen, Haar, Kleidung ×1-2) | Kleidung teilt Atlas-Material |

LODs für alle Meshes erzeugen (Automatik im Mesh-Editor, mindestens 3-4 Stufen), Screen-Size-Wechsel aggressiv wählen. Schatten für Crowd nur bis 25 m. Messen mit `stat gpu`, `stat unit` und Unreal Insights, Werte sind Planwerte und per Messung zu bestätigen.

## 7. Animations-Retargeting

1. Quell- und Ziel-Skelett je einen *IK Rig* zuweisen (Plugin IK Rig, in 5.8 vorhanden), Retarget-Chain-Namen angleichen (Spine, Arms, Legs).
2. Einen *IK Retargeter* anlegen, Quell-Rig = Mannequin, Ziel-Rig = MetaHuman/Fremdskelett, Pose angleichen.
3. Animationen exportieren (Batch-Retarget im Retargeter) nach `/Game/Characters/Animations/`.
4. Der Zu-Fuß-Agent nutzt diese Animationen; Crowd braucht nur Walk/Idle-Zyklen.

## 8. Anbindung im Code

```cpp
// Spieler/NPC aus Daten
UMTAAppearanceSubsystem* Look = GetGameInstance()->GetSubsystem<UMTAAppearanceSubsystem>();
FMTAAppearance Mira;
Look->GetKeyCharacter(TEXT("mira_voss"), Mira);
UMTAAppearanceLibrary::ApplyMorphTargets(BodyMesh, Mira);
UMTAAppearanceLibrary::ApplyMaterialParameters(BodyMesh, Mira);
UMTAAppearanceLibrary::ApplyBodyScale(BodyMesh, Mira);
TMap<EMTAWardrobeSlot, USkeletalMeshComponent*> SlotComps;
GetGameInstance()->GetSubsystem<UMTAWardrobeSubsystem>()->ApplyOutfit(BodyMesh, Mira, SlotComps);

// Passanten
UMTACrowdLookSubsystem* Crowd = GetGameInstance()->GetSubsystem<UMTACrowdLookSubsystem>();
Crowd->ConfigureInstancedComponent(Ism);                       // Platzhalter + 16 Custom-Data-Floats
FMTACrowdLook L = Crowd->GenerateStableLook(PedestrianId, EMTADistrict::Saltmarket);
Crowd->AddCrowdInstance(Ism, L, FootLocation, YawDeg);

// Charaktererstellung (Slate)
SNew(SMTACharacterCreator)
	.Appearances(Look).Wardrobe(Wardrobe).OriginProvider(OriginProviderObject)
	.OnAppearanceChanged_Lambda([this](const FMTAAppearance& A) { UpdatePreview(A); })
	.OnConfirmed_Lambda([this](const FMTAAppearance& A) { SavePlayer(A); });
```

Custom-Data-Layout (`MTACrowdData`): 0 Hautton, 1 Höhenfaktor, 2 Breitenfaktor, 3-5 Oberteil-Tint, 6-8 Hosen-Tint, 9-11 Haarfarbe (inkl. Ergrauung), 12 Mesh-Variante, 13-15 Akzent-Tint. Im Material über *PerInstanceCustomData* lesen.

Herkunft: `IMTAOriginProvider` (`GetOriginChoices`, `GetSelectedOrigin`, `SelectOrigin`) wird vom Fortschritts-Agenten implementiert. Die Herkunfts-IDs in `appearance.json` unter `originOutfits` (`racer`, `dockkid`, `corp_defector`) sind Vorschläge und an dessen echte IDs anzupassen.

## 9. Datenpflege

- Neue Archetypen/Kleidung direkt in den JSON-Dateien ergänzen und `validate_character_data.py` ausführen.
- `mesh`, `groom`, `cards`, `decal` und `crowdMeshGroups` sind absichtlich leere Pfad-Platzhalter, bis Assets existieren.
- Bezirksgewichte der Archetypen stehen in der Reihenfolge Core, Saltmarket, Lantern Hill, Greenmoor, Rustline, Sunstrip.
- Alle Figuren, Namen und Symbole sind frei erfunden; keine Nachbildung bekannter Figuren oder Marken.
