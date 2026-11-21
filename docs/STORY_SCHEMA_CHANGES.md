# Story-Schema: Konventionen und gewünschte Engine-Änderungen

Die Story (`godot/data/story.json` = `Content/Data/story.json`) nutzt **ausschließlich** das bestehende Schema 1. Die Engines (`godot/story.gd`, `Source/MTA6/MTAStorySubsystem.cpp`) müssen dafür **nicht** geändert werden. Dieses Dokument beschreibt die Konventionen und Wünsche für spätere Engine-Iterationen.

## 1. Verbindliche Konventionen (heute schon umgesetzt, vom Simulator geprüft)

1. **Flags in Bedingungen nur boolesch und nur mit `==`/`!=`.** C++ vergleicht Flags als String (`"true"`/`"false"`) und kennt bei Flags nur `==`/`!=`. Godot vergleicht Variants, und ein Vergleich `false == "union"` wäre typfremd. Deshalb wurde das alte String-Flag `ally` durch `ally_union`/`ally_collective`/`ally_corp` ersetzt und `ending` durch `endTakeOver` usw. Das String-Flag `ending` wird nur noch zu Debug-Zwecken gesetzt (`main.gd` gibt es aus) und nie verglichen.
2. **Schwellen für Taten** (`killedCivilians`-Schwellen) laufen über **gestufte Bool-Flags**: `killedCivilians` (Stufe 1) und `massacre` (Stufe 2). Ein numerischer Zähler ist im Schema nicht möglich (siehe 2.1).
3. **Varianten statt Textverzweigung:** Wendepunkte mit mehreren Ausprägungen sind eigene Missionen mit disjunkten `requires` und einem Sperr-Flag in `excludes` (z. B. `w2_*` mit `w2_done`, `w4_*` mit `w4_done`, `a3_hafen_*` mit `portHeld`, `a3_probe_*` mit `jammerReady`).
4. **Jede Choice hat mindestens eine Option ohne `when`.** So gibt es keine Sackgassen (`main.gd` überspringt leere Choices, der Simulator wertet sie als Fehler).
5. **Haupt-Missionen tragen `excludes: [{"done": "w5_finale"}]`**, damit nach dem Ende nur noch Nebenaufträge angeboten werden.
6. **Enden-Varianten:** Mehrere Enden teilen sich ein Präfix (`E1`, `E1_blut`, `E1_verkauf` …). Das Ende mit der höchsten `priority` gewinnt. Der Simulator prüft E1–E5 über das Präfix und jede Variante einzeln.
7. **Top-Level `title` und `registry`** (Bezirke, NPCs, Flags mit Bedeutung) werden von beiden Engines ignoriert. Der Simulator nutzt sie als Flag-Registry (unbekannte Flags sind Fehler).

## 2. Gewünschte Engine-Erweiterungen (nicht umgesetzt, nur beschrieben)

| # | Erweiterung | Zweck | Vorschlag |
|---|---|---|---|
| 2.1 | **Zähler** `counter` | echte Schwellen (`killedCivilians >= 3`), Gig-Ketten | Bedingung `{"counter": ["civilians", ">=", 3]}`, Effekt `{"counter": ["civilians", 1]}`, eigener Zustand `counters` (Godot + C++ + Speicherstand) |
| 2.2 | **Missionsauswahl durch den Spieler** | `main.gd` nimmt immer `available()[0]`, Hauptmissionen zuerst. Dadurch sind Nebenaufträge im Prototyp erst nach dem Finale spielbar, und das geheime Ende E5 ist faktisch nur mit Nebenaufträgen erreichbar | Marker für **alle** verfügbaren Missionen setzen (Karte/Telefon), die Spielerin fährt einen davon an |
| 2.3 | **`onFail`** | Folgen bei Misserfolg | Effektliste wie `onSuccess`, vom MissionRunner ausgelöst |
| 2.4 | **Verzögerte Effekte** | Konsequenzen nach Spieltagen (WELTKONZEPT 9.2) | `{"delay": {"days": 2}, "effect": {...}, "announce": "radio"}`, Warteschlange im Zustand |
| 2.5 | **Ende-Auswertung mit Weltzustand** | `worldTagsAfter` setzt nach dem Ende Bezirks-Tags | Feld am Ende, Engine wendet es nach `evaluate_ending()` an |
| 2.6 | **Entscheidungsprotokoll in C++** | Godot speichert `decisions`, C++ nicht | `ChooseOption` hängt `{id, choice}` an, `StateToJson` schreibt es |
| 2.7 | **Stabile Sortierung** | `sort_custom` in Godot ist nicht stabil, C++ `Sort` auch nicht. Bei gleichem `main`/`act` kann die Reihenfolge zwischen den Engines abweichen | Sortierschlüssel `(main, act, Index in der Datei)` |
| 2.8 | **Radio-/Dialog-Pool mit Bedingungen** | Info-Flags (41 Stück, z. B. `brannCovered`, `sourceBurned`, `looted`) sollen Dialoge und Open-Line-Meldungen färben | `data/radio.json` mit `when: [Cond]` (gleiche Bedingungssprache) |
| 2.9 | **Varianten-Texte** | Kleinere Textvarianten ohne eigene Mission | `variants: [{when, text}]` an der Mission, erste passende überschreibt `text` |

## 3. Kompatibilitätsnotizen

- `heat` ist eine Zahl (`{"heat": 2}`), nicht `[bezirk, wert]` wie in WELTKONZEPT 6.5 skizziert. Beide Engines lesen nur die Zahl.
- `npcTrust` ist auf −3…+3 begrenzt, `standing` auf −100…+100, `aegis` auf 0…100 (in beiden Engines identisch).
- Neue NPC-Schlüssel in `start.npcTrust`: `ravn`, `dutch`, `kandel`, `pia`. `main.gd` zeigt nur die vier Fraktionen an, das ist unverändert kompatibel.
