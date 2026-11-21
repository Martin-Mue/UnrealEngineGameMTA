# MTA 6 – Missionstypen

Gilt für `godot/data/story.json` und `Content/Data/story.json` (byte-identisch). Jede Mission trägt optional `type`, `district`, `giver`, `params`, `reward`, `outro`. Beide Engines ignorieren diese Felder heute. Sie sind die Datengrundlage für den künftigen `MissionRunner` (WELTKONZEPT Kap. 10). Story-relevante Folgen (Stand, Vertrauen, Flags, Tags, Aegis, Geld, Fahndung) stehen **immer** in `onSuccess` bzw. in den `effects` von Optionen, nie in `params`.

## Gemeinsame Regeln

| Thema | Regel |
|---|---|
| **Belohnung** | `Geld = Basis(Typ, Stufe) × Bezirksfaktor × (1 + Zeitbonus + Zielbonus) − Schadenskosten`. Bezirksfaktor: Core 1,3 · Greenmoor 1,2 · Sunstrip 1,1 · Lantern 0,9 · Saltmarket 0,8 · Rustline 0,7 · Umland 1,0. Der Story-Wert `money` in `onSuccess` ist die Basis. |
| **Skalierung** | Stufe 1–3 über Fraktionsstand (Stufe 2 ab +20, Stufe 3 ab +50). Je Stufe: +1 Verfolger, −15 % Zeitlimit, +1 Kamera auf der Route, Basisgeld ×2,3 / ×4,6. Akt 3 erhöht alle Stufen um 1. |
| **Fahndung (Heat 0–5)** | Effekt `heat` addiert direkt. Kamera-Treffer bei Verbrechen: +Heat je nach Bezirk (Core 1,0 · Greenmoor 0,8 · Sunstrip 0,6 · Saltmarket 0,4 · Lantern 0,2 · Rustline 0,1). `params.heat_start` setzt die Fahndung beim Start. |
| **Aegis-Profil (0–100)** | Hohes Profil (≥ 55) löst `ev_razzia` aus, ab 70 doppelte Kamera-Reichweite im Core. Hacks, Kennzeichentausch und Untertauchen senken es. |
| **Fraktionsstand** | Erfolg: Stand laut Daten. Fehlschlag: −25 % des Erfolgsbetrags beim Auftraggeber, nie unter −10. Feindliche Fraktion (≤ −30) schickt Abfangfahrzeuge. |
| **Fehlschlag** | Nie Totalverlust: Checkpoint-Neustart, 25 % Basisgeld als Kosten. Hauptmissionen sind wiederholbar, Entscheidungen fallen erst im Dialog **nach** dem Fahrteil. |
| **Entscheidungen** | Der Fahrteil wird zuerst gespielt, danach kommen Text und `choices`. So ist die Story-Engine unabhängig vom Erfolg des Fahrteils. |

## Typen

### `courier` – Lieferung / Personentransport
- **Pflicht-Parameter:** `from`, `to`, `time_limit` (s). **Optional:** `cargo`, `fragile`, `passengers`, `checkpoints`, `heat_start`, `escort`.
- **Belohnung:** Basis 250–1500. Zeitbonus bis +30 %, bei `fragile` −10 % je Schadensstufe.
- **Skalierung:** kürzeres Zeitlimit, mehr Checkpoints, Kamera-Route.
- **Fehlschlag:** Zeit abgelaufen, Fracht zerstört, Passagier verletzt oder ausgestiegen.
- **Fahndung/Stand:** Checkpoint ohne Pass bedeutet Aegis-Treffer (+5 Profil). Verfehlte Lieferung kostet Stand beim Auftraggeber.
- **Beispiele:** `p1`, `a1u_fruehschicht`, `a2k_angebot`, `a3_phase2`, `a3_uniform`.

### `chase` – Verfolgung
- **Pflicht:** `target`, `route_profile`, `capture_mode` (`ram`, `block`, `stop`, `observe`). **Optional:** `escape_skill` (0–1), `reinforcements`, `time_limit`, `min_distance`/`max_distance` (bei `observe`).
- **Belohnung:** 400–1000, Standing ±10–15.
- **Skalierung:** `escape_skill` +0,1 je Stufe, Verstärkung ab Stufe 2.
- **Fehlschlag:** Ziel entkommt (Distanz > 250 m für 10 s), Zeit läuft ab, bei `observe` wird man entdeckt.
- **Fahndung/Stand:** Rammen im Bezirk mit Kameras gibt +1 Heat. Tötet ein Rammmanöver Unbeteiligte, setzt die Story das Flag `killedCivilians` (nur über Optionen, nie systemisch).
- **Beispiele:** `a1u_streikbrecher`, `a2u_lohn`, `w2_nox`, `a3_hafen_ohne`, `w4_doppelagent`, `w4_verlust`.

### `escape` – Flucht
- **Pflicht:** `heat_level`, `pursuers`, `safe_zone`. **Optional:** `time_limit`, `hide_spots`, `helicopter`, `roadblocks`, `weather`, `civilian_reporters` (Passanten melden Mira).
- **Belohnung:** Überleben plus 400–1500. Bonus, wenn man ohne Kamera-Treffer entkommt.
- **Skalierung:** +2 Verfolger je Stufe, ab Stufe 3 Hubschrauber.
- **Fehlschlag:** Festnahme (Aegis +10, Geldstrafe 20 %) oder Totalschaden.
- **Fahndung/Stand:** Endet mit Heat 0 in der `safe_zone`. Die Zone gehört einer Fraktion, die mindestens neutral sein muss, sonst wird sie zur Falle.
- **Beispiele:** `a1_kontrolle`, `w2_brann`, `w2_hinterhalt`, `a3_freiwild`, `a3_route9`, `w4_rettung`, `w5_finale`.

### `race` – Rennen
- **Pflicht:** `route`, `opponents`, `laps`. **Optional:** `stake`, `weather`, `time_of_day`, `traffic_density`, `rules` (`ramming`, `camera_detection_fails`), `rival`.
- **Belohnung:** 200–1500 + Einsatz, Platzfaktor [1,0 / 0,5 / 0,25].
- **Skalierung:** Gegner-KI +10 % je Stufe, Wetterwechsel ab Stufe 2.
- **Fehlschlag:** letzter Platz, Disqualifikation, Einsatz verloren.
- **Fahndung/Stand:** Rennen in Rustline/Salzebene erzeugen keine Fahndung, Küstenstraße +1 Heat bei Kamera-Treffer. Sieg bringt Stand bei den Wrenchers.
- **Beispiele:** `a1w_probefahrt`, `a1_blinde_flecken` (Kamera-Slalom), `race_salzebene`, `race_coastroad`, `ev_rennnacht`.

### `hunt` – Auftragsjagd (Vertrag)
- **Pflicht:** `target_profile`, `investigation_nodes`, `prep_slots`. **Optional:** `heat_cap`, `routine`, `bonus_objectives`.
- **Phasen:** Ermittlung → Vorbereitung → Jagd → Entscheidung (Entscheidung = `choices`).
- **Belohnung:** 1500–4000, Standing ±15–25.
- **Skalierung:** mehr Ermittlungsknoten, Ziel wechselt die Route, Leibwächter.
- **Fehlschlag:** Ziel entkommt, `heat_cap` überschritten (Ziel taucht ab, Vertrag kühlt 1 Tag ab).
- **Fahndung/Stand:** Übergabe an KCG = KCG +, Fraktion des Ziels −. Laufenlassen kostet Stand beim Auftraggeber.
- **Beispiele:** `a2k_saeuberung`, `hunt_silberfisch`, `hunt_deserteur`.

### `hack` – Hack
- **Pflicht:** `target` oder `targets`, `hack_time` (s im Sichtkegel halten). **Optional:** `cameras`, `detection_radius`, `escape_after`, `heat_after`, `carry`.
- **Belohnung:** 150–1200 und Aegis-Profil −5…−20.
- **Skalierung:** mehr Kameras, kürzere Fenster, Wartungsdrohnen ab Stufe 2.
- **Fehlschlag:** Entdeckung = Übergang in `escape` mit `heat_after`. Wer die Flucht schafft, hat den Hack trotzdem erledigt (weiches Scheitern).
- **Fahndung/Stand:** Collective +, Aurelia −. Das Kamera-Hack-Modul (`hackModule`) halbiert `hack_time`.
- **Beispiele:** `a2_postfach`, `a2c_archiv`, `a2c_kaskade`, `a3_zwei_gesichter`, `a3_probe_*`, `g_ampelchor`.

### `gig` – systemischer Kleinauftrag
- **Pflicht:** `template` (`delivery`, `tow`, `taxi`, `sticker_run`, `camera_courier`, `smuggle_passengers`, `traffic_light_hack`), `tier`. **Optional:** `cooldown_h`, `time_limit`, `stops`, `mood`, `tip_rules`.
- **Belohnung:** 150 / 350 / 700 je Stufe, Standing ±3 / ±6 / ±10.
- **Regeln:** höchstens 3 Gigs gleichzeitig je Bezirk. Wiederholbar nach `cooldown_h`, die Story-Effekte gelten nur beim ersten Abschluss (Story-Engine markiert `completed`).
- **Fehlschlag:** Abbruch oder Zeitablauf, −3 Standing, Abkühlzeit verdoppelt.
- **Beispiele:** `g_kistentanz`, `g_abschlepp`, `g_kleber`, `g_kurier_core`, `g_drohnenausfall`, `g_influencer_taxi`, `g_checkpointlotse`.

### `side` – Nebenquest
- **Pflicht:** `beats`. **Optional:** `location`, `delayed_from` (Folgequest), `requires_deed` (Konsequenzquest).
- **Regeln:** eigene Figur, eigene Frage, mindestens eine Entscheidung ohne richtige Antwort, eine verzögerte Folge in der Welt.
- **Belohnung:** 600–1500 + Folge.
- **Fehlschlag:** Beat-spezifisch, die Quest bleibt offen.
- **Beispiele:** `sq_frachtbrief` → `sq_frachtbrief_2`, `sq_witwe` (nur nach `killedCivilians`), `sq_nox_schwester`, `sq_ravn_akte`, `sq_pia_quelle`, `sq_jonas_wohnung`.

### `event` – Bezirks-Event / Szene
- **Pflicht:** `trigger` (`tag_strike`, `tag_lockdown`, `tag_blackout`, `aegis_profile_high`, `weekend_evening`, …). **Optional:** `duration_min`/`duration_h`/`duration_days`, `world_tags`, `barricades`, `roadblocks`.
- **Regeln:** Viele Events hängen an einem Weltzustand-Tag (`requires: [{"tag": [...]}]`). Ein Event läuft auch ohne Spieler aus, die Folgen treten dann trotzdem ein (Engine-Erweiterung, siehe STORY_SCHEMA_CHANGES.md).
- **Belohnung:** 300–1200 + Weltwirkung, Standing ±10–30.
- **Beispiele:** `p3`, `a2u_blockade`, `w3_price`, `a3_hafen_brann`, `ev_hafenstreik`, `ev_razzia`, `ev_torsperre`, `ev_stromausfall`, `ev_nachtmarkt`.

## Weltzustand-Tags (von der Story gesetzt)

| Bezirk | Tag | Gesetzt durch | Wirkung (Welt/KI) |
|---|---|---|---|
| saltmarket | `strike` | `a1u_streikbrecher` | LKW-Stau, Streikposten, Event `ev_hafenstreik` |
| saltmarket | `blockade` | `a2u_blockade` | Container-Barrikaden, KCG meidet Kais |
| saltmarket | `free_port` | `a3_hafen_*` | keine Aegis-Kameras, Union-Patrouillen |
| saltmarket | `evicted` | `a3_raeumung_hafen` | leere Kais, Konzern-Security |
| lantern | `blind` | `a2c_kaskade` | Kameras aus, Collective-Gigs häufiger |
| lantern | `protest` | `sq_glockenturm` | Demos, Fußgängerdichte hoch |
| lantern | `gentrified` | `a3_raeumung_altstadt` | Neubau-Fassaden, neue Kameras |
| greenmoor | `lockdown` | `a2c_kaskade` (Kaskade) | Tore zu, Event `ev_torsperre` |
| core | `blackout` | `a3_probe_*` | Ampeln/Aufzüge aus, Event `ev_stromausfall` |
