extends SceneTree
## Story-Simulator und Graph-Lint fuer res://data/story.json
## Start: godot_console --headless --path godot --script res://tests/story_sim.gd
## Teil 1 (Lint): JSON, Pflichtfelder, doppelte IDs, Referenzen, Flags/Tags/NPCs, C++-Kompatibilitaet.
## Teil 2 (Simulation): mehrere Spielerstrategien, Sackgassen, Erreichbarkeit aller Missionen/Optionen/Enden.
const StoryScript := preload("res://story.gd")
const PATH := "res://data/story.json"
const TYPES := ["courier", "chase", "escape", "race", "hunt", "hack", "gig", "side", "event"]
const OPS := [">=", ">", "<=", "<", "==", "!="]
const MIN_MAIN := 30
const MIN_SIDE := 25
const RUNS_PER_STRATEGY := 1500
const GUARD := 400

var errors: Array = []
var warnings: Array = []
var data: Dictionary = {}
var mission_ids := {}
var flags_read := {}
var flags_written := {}
var tags_read := {}
var tags_written := {}

func err(s: String) -> void:
	errors.append(s)

func warn(s: String) -> void:
	warnings.append(s)

func _init() -> void:
	if not _load():
		_finish()
		return
	_lint()
	if errors.is_empty():
		_simulate()
	_finish()

func _finish() -> void:
	for w in warnings:
		print("WARNUNG: ", w)
	for e in errors:
		print("FEHLER: ", e)
	print("STORYSIM ergebnis=", "GRUEN" if errors.is_empty() else "ROT", " fehler=", errors.size(), " warnungen=", warnings.size())
	quit(0 if errors.is_empty() else 1)

func _load() -> bool:
	var f := FileAccess.open(PATH, FileAccess.READ)
	if f == null:
		err("story.json nicht lesbar")
		return false
	var txt := f.get_as_text()
	f.close()
	var j := JSON.new()
	if j.parse(txt) != OK:
		err("JSON-Fehler Zeile %d: %s" % [j.get_error_line(), j.get_error_message()])
		return false
	if typeof(j.data) != TYPE_DICTIONARY:
		err("Wurzel ist kein Objekt")
		return false
	data = j.data
	for k in ["start", "missions", "endings"]:
		if not data.has(k):
			err("Pflichtschluessel fehlt: " + k)
	return errors.is_empty()

# ---------------------------------------------------------------- Lint
func _lint() -> void:
	var start: Dictionary = data["start"]
	var reg: Dictionary = data.get("registry", {})
	var districts: Array = reg.get("districts", [])
	var npcs: Dictionary = reg.get("npcs", {})
	var flag_reg: Dictionary = reg.get("flags", {})
	var n_main := 0
	var n_side := 0
	var n_choices := 0
	var n_finale := 0
	for m in data["missions"]:
		var id: String = str(m.get("id", ""))
		if id == "":
			err("Mission ohne id")
			continue
		if mission_ids.has(id):
			err("doppelte Missions-ID: " + id)
		mission_ids[id] = true
	for m in data["missions"]:
		var id: String = m["id"]
		for k in ["title", "cell", "requires", "text"]:
			if not m.has(k):
				err("%s: Pflichtfeld fehlt: %s" % [id, k])
		var cell: Variant = m.get("cell", [])
		if typeof(cell) != TYPE_ARRAY or cell.size() != 2 or int(cell[0]) < 0 or int(cell[0]) > 8 or int(cell[1]) < 0 or int(cell[1]) > 8:
			err("%s: cell ausserhalb 0..8" % id)
		if m.has("type") and not TYPES.has(m["type"]):
			err("%s: unbekannter type %s" % [id, m["type"]])
		if m.has("district") and not districts.is_empty() and not districts.has(m["district"]):
			err("%s: unbekannter district %s" % [id, m["district"]])
		if m.has("giver") and not npcs.is_empty() and not npcs.has(m["giver"]):
			err("%s: unbekannter giver %s" % [id, m["giver"]])
		if m.get("main", false):
			n_main += 1
		else:
			n_side += 1
		if m.get("finale", false):
			n_finale += 1
		for c in m.get("requires", []):
			_cond(c, id + ".requires", start)
		for c in m.get("excludes", []):
			_cond(c, id + ".excludes", start)
		for e in m.get("onSuccess", []):
			_eff(e, id + ".onSuccess", start)
		var choice_ids := {}
		for ch in m.get("choices", []):
			n_choices += 1
			var cid: String = str(ch.get("id", ""))
			if cid == "" or choice_ids.has(cid):
				err("%s: Choice-ID fehlt/doppelt: %s" % [id, cid])
			choice_ids[cid] = true
			if not ch.has("text"):
				err("%s/%s: Choice ohne text" % [id, cid])
			var opt_ids := {}
			var has_fallback := false
			for o in ch.get("options", []):
				var oid: String = str(o.get("id", ""))
				if oid == "" or opt_ids.has(oid):
					err("%s/%s: Option-ID fehlt/doppelt: %s" % [id, cid, oid])
				opt_ids[oid] = true
				if not o.has("text"):
					err("%s/%s/%s: Option ohne text" % [id, cid, oid])
				if (o.get("when", []) as Array).is_empty():
					has_fallback = true
				for c in o.get("when", []):
					_cond(c, "%s/%s/%s.when" % [id, cid, oid], start)
				for e in o.get("effects", []):
					_eff(e, "%s/%s/%s.effects" % [id, cid, oid], start)
			if not has_fallback:
				err("%s/%s: keine Option ohne Bedingung (moegliche Sackgasse)" % [id, cid])
	var end_ids := {}
	var has_default := false
	for e in data["endings"]:
		var eid: String = str(e.get("id", ""))
		if eid == "" or end_ids.has(eid):
			err("Ende-ID fehlt/doppelt: " + eid)
		end_ids[eid] = true
		for k in ["title", "priority", "epilogue"]:
			if not e.has(k):
				err("Ende %s: Pflichtfeld fehlt: %s" % [eid, k])
		if (e.get("when", []) as Array).is_empty():
			has_default = true
		for c in e.get("when", []):
			_cond(c, "Ende " + eid, start)
	if not has_default:
		err("kein Fallback-Ende ohne Bedingung")
	for base in ["E1", "E2", "E3", "E4", "E5"]:
		var found := false
		for k in end_ids:
			if str(k).begins_with(base):
				found = true
		if not found:
			err("Ende %s fehlt" % base)
	if n_finale < 1:
		err("keine Finale-Mission")
	if not mission_ids.has("w5_finale"):
		err("w5_finale fehlt (wird von der C++-Simulation erwartet)")
	for fl in flags_read:
		if not flags_written.has(fl):
			err("Flag wird gelesen, aber nie gesetzt: " + fl)
	var info_flags: Array = []
	for fl in flags_written:
		if not flag_reg.is_empty() and not flag_reg.has(fl):
			err("Flag nicht in registry.flags: " + fl)
		if not flags_read.has(fl):
			info_flags.append(fl)
	for fl in flag_reg:
		if not flags_written.has(fl):
			warn("Flag in registry, aber nie gesetzt: " + fl)
	print("LINT info_flags (nur fuer Dialog/Radio/Rueckblick, ", info_flags.size(), "): ", ", ".join(info_flags))
	for t in tags_read:
		if not tags_written.has(t):
			err("Tag wird gelesen, aber nie gesetzt: " + t)
	if n_main < MIN_MAIN:
		err("zu wenige Haupt-/Zweigmissionen: %d < %d" % [n_main, MIN_MAIN])
	if n_side < MIN_SIDE:
		err("zu wenige Nebenauftraege: %d < %d" % [n_side, MIN_SIDE])
	print("LINT missionen=", n_main + n_side, " haupt=", n_main, " neben=", n_side, " entscheidungen=", n_choices, " enden=", end_ids.size(), " flags=", flags_written.size())

func _cond(c: Variant, where: String, start: Dictionary) -> void:
	if typeof(c) != TYPE_DICTIONARY or c.size() != 1:
		err(where + ": Bedingung muss Objekt mit genau einem Schluessel sein")
		return
	var k: String = c.keys()[0]
	var v: Variant = c[k]
	match k:
		"standing", "npcTrust":
			if typeof(v) != TYPE_ARRAY or v.size() != 3 or not OPS.has(v[1]) or typeof(v[2]) not in [TYPE_INT, TYPE_FLOAT]:
				err(where + ": ungueltig " + str(c))
			elif not (start[k] as Dictionary).has(v[0]):
				err(where + ": unbekannter Schluessel %s in %s" % [v[0], k])
		"flag":
			if typeof(v) != TYPE_ARRAY or v.size() != 3:
				err(where + ": ungueltig " + str(c))
				return
			if v[1] != "==" and v[1] != "!=":
				err(where + ": Flags nur mit ==/!= vergleichen (C++-Engine) " + str(c))
			if typeof(v[2]) != TYPE_BOOL:
				err(where + ": Flag-Vergleich nur mit bool (Typgleichheit Godot/C++) " + str(c))
			flags_read[v[0]] = true
		"aegis":
			if typeof(v) != TYPE_ARRAY or v.size() != 2 or not OPS.has(v[0]):
				err(where + ": ungueltig " + str(c))
		"done":
			if typeof(v) != TYPE_STRING or not mission_ids.has(v):
				err(where + ": unbekannte Mission in done: " + str(v))
		"tag":
			if typeof(v) != TYPE_ARRAY or v.size() != 3 or v[1] != "has":
				err(where + ": ungueltig " + str(c))
			else:
				tags_read["%s:%s" % [v[0], v[2]]] = true
		"any", "all":
			if typeof(v) != TYPE_ARRAY or v.is_empty():
				err(where + ": leeres " + k)
			else:
				for sub in v:
					_cond(sub, where, start)
		"not":
			_cond(v, where, start)
		_:
			err(where + ": unbekannte Bedingung " + k)

func _eff(e: Variant, where: String, start: Dictionary) -> void:
	if typeof(e) != TYPE_DICTIONARY or e.size() != 1:
		err(where + ": Effekt muss Objekt mit genau einem Schluessel sein")
		return
	var k: String = e.keys()[0]
	var v: Variant = e[k]
	match k:
		"standing", "npcTrust":
			if typeof(v) != TYPE_ARRAY or v.size() != 2:
				err(where + ": ungueltig " + str(e))
			elif not (start[k] as Dictionary).has(v[0]):
				err(where + ": unbekannter Schluessel %s in %s" % [v[0], k])
		"flag":
			if typeof(v) != TYPE_ARRAY or v.size() != 2:
				err(where + ": ungueltig " + str(e))
			else:
				flags_written[v[0]] = true
		"tag":
			if typeof(v) != TYPE_ARRAY or v.size() != 3 or not ["add", "remove"].has(v[1]):
				err(where + ": ungueltig " + str(e))
			elif v[1] == "add":
				tags_written["%s:%s" % [v[0], v[2]]] = true
		"aegis", "money", "heat":
			if typeof(v) not in [TYPE_INT, TYPE_FLOAT]:
				err(where + ": Zahl erwartet " + str(e))
		_:
			err(where + ": unbekannter Effekt " + k)

# ---------------------------------------------------------- Simulation
## Strategien: zufaellig, haupt (wie main.gd: Hauptmission zuerst), komplett (Nebenauftraege zuerst),
## selten (bevorzugt bedingte Optionen), godot (deterministisch av[0] + Option 1, exakt wie main.gd).
func _simulate() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	var mission_hits := {}
	var option_hits := {}
	var end_counts := {}
	var dead := 0
	var strategies := ["zufall", "haupt", "komplett", "selten"]
	for strat in strategies:
		var counts := {}
		for r in RUNS_PER_STRATEGY:
			var res := _run(strat, rng, mission_hits, option_hits)
			if res["dead"] != "":
				dead += 1
				if dead <= 5:
					print("SACKGASSE [", strat, "]: ", res["dead"])
			var eid: String = res["ending"]
			counts[eid] = counts.get(eid, 0) + 1
			end_counts[eid] = end_counts.get(eid, 0) + 1
		print("STRATEGIE ", strat, ": ", counts)
	var g := _run("godot", rng, mission_hits, option_hits)
	print("GODOT-PFAD (av[0], Option 1): schritte_bis_finale=", g["finale_step"], " ende=", g["ending"], " sackgasse=", g["dead"])
	if int(g["finale_step"]) > 30:
		warn("main.gd-Testlauf braucht mehr als 30 Schritte bis zum Finale")
	print("STORYSIM runs=", RUNS_PER_STRATEGY * strategies.size(), " enden=", end_counts, " sackgassen=", dead)
	if dead > 0:
		err("%d Laeufe mit Sackgasse" % dead)
	for e in data["endings"]:
		if not end_counts.has(e["id"]):
			err("Ende nie erreicht: %s (%s)" % [e["id"], e["title"]])
	for base in ["E1", "E2", "E3", "E4", "E5"]:
		var ok := false
		for k in end_counts:
			if str(k).begins_with(base):
				ok = true
		print("STORYSIM ende_", base, "_erreichbar=", ok)
	for m in data["missions"]:
		if not mission_hits.has(m["id"]):
			err("Mission nie erreichbar: " + m["id"])
		for ch in m.get("choices", []):
			for o in ch["options"]:
				var key := "%s/%s/%s" % [m["id"], ch["id"], o["id"]]
				if not option_hits.has(key):
					warn("Option nie gewaehlt: " + key)
	print("STORYSIM missionen_erreicht=", mission_hits.size(), "/", mission_ids.size(), " optionen_gewaehlt=", option_hits.size())

func _run(strat: String, rng: RandomNumberGenerator, mission_hits: Dictionary, option_hits: Dictionary) -> Dictionary:
	var s = StoryScript.new()
	s.data = data
	s.reset()
	var ending := ""
	var dead := ""
	var finale_step := -1
	var step := 0
	while step < GUARD:
		step += 1
		var av: Array = s.available()
		if av.is_empty():
			break
		var m: Dictionary = _pick_mission(strat, av, rng)
		for ch in m.get("choices", []):
			var opts: Array = s.options_available(ch)
			if opts.is_empty():
				dead = "keine Option bei %s/%s" % [m["id"], ch["id"]]
				break
			var o: Dictionary = _pick_option(strat, opts, rng)
			option_hits["%s/%s/%s" % [m["id"], ch["id"], o["id"]]] = true
			s.choose(ch, o)
		s.complete(m)
		mission_hits[m["id"]] = true
		if m.get("finale", false) and ending == "":
			ending = s.evaluate_ending().get("id", "?")
			finale_step = step
			if strat == "godot":
				break
	if ending == "" and dead == "":
		dead = "Finale nicht erreicht, letzter Stand: " + str(s.state["completed"].slice(-3))
	if ending == "":
		ending = "KEIN_ENDE"
	return {"ending": ending, "dead": dead, "finale_step": finale_step}

func _pick_mission(strat: String, av: Array, rng: RandomNumberGenerator) -> Dictionary:
	match strat:
		"godot":
			return av[0]
		"haupt":
			var mains := av.filter(func(x): return x.get("main", false))
			if not mains.is_empty() and rng.randf() < 0.8:
				return mains[rng.randi() % mains.size()]
		"komplett":
			var sides := av.filter(func(x): return not x.get("main", false))
			if not sides.is_empty():
				return sides[rng.randi() % sides.size()]
	return av[rng.randi() % av.size()]

func _pick_option(strat: String, opts: Array, rng: RandomNumberGenerator) -> Dictionary:
	if strat == "godot":
		return opts[0]
	if strat == "selten" or strat == "komplett":
		var cond := opts.filter(func(x): return not (x.get("when", []) as Array).is_empty())
		if not cond.is_empty() and rng.randf() < 0.7:
			return cond[rng.randi() % cond.size()]
	return opts[rng.randi() % opts.size()]
