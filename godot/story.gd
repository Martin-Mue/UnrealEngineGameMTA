extends RefCounted
## Story-Engine (datengetrieben): Bedingungen, Effekte, Entscheidungen, Enden.
## Inhalte stehen in res://data/story.json (siehe docs/WELTKONZEPT.md, Kapitel 6).

var data: Dictionary = {}
var state: Dictionary = {}
var out_money := 0   # von der Engine angesammelte Geldaenderung (Spiel holt sie ab)
var out_heat := 0.0  # angesammelte Fahndungsaenderung

func load_data(path: String) -> bool:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return false
	var parsed: Variant = JSON.parse_string(f.get_as_text())
	f.close()
	if typeof(parsed) != TYPE_DICTIONARY:
		return false
	data = parsed
	reset()
	return true

func reset() -> void:
	var s: Dictionary = data["start"]
	state = {
		"schema": 1,
		"standing": (s["standing"] as Dictionary).duplicate(),
		"npcTrust": (s["npcTrust"] as Dictionary).duplicate(),
		"flags": {},
		"tags": {},
		"completed": [],
		"decisions": [],
		"aegis": s["aegis"],
	}
	out_money = 0
	out_heat = 0.0

func _cmp(a: Variant, op: String, b: Variant) -> bool:
	match op:
		">=": return a >= b
		">": return a > b
		"<=": return a <= b
		"<": return a < b
		"==": return a == b
		"!=": return a != b
	return false

func check(c: Dictionary) -> bool:
	if c.has("standing"):
		var a: Array = c["standing"]
		return _cmp(state["standing"].get(a[0], 0), a[1], a[2])
	if c.has("npcTrust"):
		var a: Array = c["npcTrust"]
		return _cmp(state["npcTrust"].get(a[0], 0), a[1], a[2])
	if c.has("flag"):
		var a: Array = c["flag"]
		return _cmp(state["flags"].get(a[0], false), a[1], a[2])
	if c.has("aegis"):
		var a: Array = c["aegis"]
		return _cmp(state["aegis"], a[0], a[1])
	if c.has("done"):
		return (state["completed"] as Array).has(c["done"])
	if c.has("tag"):
		var a: Array = c["tag"]
		return (state["tags"].get(a[0], []) as Array).has(a[2])
	if c.has("any"):
		for sub in c["any"]:
			if check(sub):
				return true
		return false
	if c.has("all"):
		for sub in c["all"]:
			if not check(sub):
				return false
		return true
	if c.has("not"):
		return not check(c["not"])
	return false

func check_all(conds: Array) -> bool:
	for c in conds:
		if not check(c):
			return false
	return true

func apply(e: Dictionary) -> void:
	if e.has("standing"):
		var a: Array = e["standing"]
		state["standing"][a[0]] = clampf(float(state["standing"].get(a[0], 0)) + float(a[1]), -100.0, 100.0)
	if e.has("npcTrust"):
		var a: Array = e["npcTrust"]
		state["npcTrust"][a[0]] = clampf(float(state["npcTrust"].get(a[0], 0)) + float(a[1]), -3.0, 3.0)
	if e.has("flag"):
		var a: Array = e["flag"]
		state["flags"][a[0]] = a[1]
	if e.has("tag"):
		var a: Array = e["tag"]
		var arr: Array = state["tags"].get(a[0], [])
		if a[1] == "add" and not arr.has(a[2]):
			arr.append(a[2])
		elif a[1] == "remove":
			arr.erase(a[2])
		state["tags"][a[0]] = arr
	if e.has("aegis"):
		state["aegis"] = clampf(float(state["aegis"]) + float(e["aegis"]), 0.0, 100.0)
	if e.has("money"):
		out_money += int(e["money"])
	if e.has("heat"):
		out_heat += float(e["heat"])

func apply_all(effects: Array) -> void:
	for e in effects:
		apply(e)

## Verfuegbare Missionen: Hauptmissionen zuerst, dann nach Akt.
func available() -> Array:
	var res: Array = []
	for m in data["missions"]:
		if (state["completed"] as Array).has(m["id"]):
			continue
		if not check_all(m.get("requires", [])):
			continue
		var blocked := false
		for c in m.get("excludes", []):
			if check(c):
				blocked = true
		if blocked:
			continue
		res.append(m)
	res.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		if a.get("main", false) != b.get("main", false):
			return a.get("main", false)
		return int(a.get("act", 0)) < int(b.get("act", 0)))
	return res

func options_available(choice: Dictionary) -> Array:
	var res: Array = []
	for o in choice["options"]:
		if check_all(o.get("when", [])):
			res.append(o)
	return res

func choose(choice: Dictionary, option: Dictionary) -> void:
	apply_all(option.get("effects", []))
	(state["decisions"] as Array).append({"id": choice["id"], "choice": option["id"]})

func complete(mission: Dictionary) -> void:
	apply_all(mission.get("onSuccess", []))
	if not (state["completed"] as Array).has(mission["id"]):
		(state["completed"] as Array).append(mission["id"])

func evaluate_ending() -> Dictionary:
	var best: Dictionary = {}
	var best_p := -999
	for e in data["endings"]:
		if int(e["priority"]) > best_p and check_all(e.get("when", [])):
			best = e
			best_p = int(e["priority"])
	return best

func find_mission(id: String) -> Dictionary:
	for m in data["missions"]:
		if m["id"] == id:
			return m
	return {}

func to_dict() -> Dictionary:
	return state.duplicate(true)

func from_dict(d: Dictionary) -> bool:
	if int(d.get("schema", 0)) != 1:
		return false
	state = d.duplicate(true)
	return true
