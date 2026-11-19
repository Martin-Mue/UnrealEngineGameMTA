extends SceneTree
## Story-Simulator: spielt tausende zufaellige Entscheidungswege durch.
## Start: godot_console --headless --path godot --script res://tests/story_sim.gd
const StoryScript := preload("res://story.gd")

func _init() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	var counts := {}
	var dead := 0
	var runs := 3000
	for r in runs:
		var s = StoryScript.new()
		if not s.load_data("res://data/story.json"):
			print("FEHLER: story.json nicht ladbar")
			quit(2)
			return
		var guard := 0
		while guard < 100:
			guard += 1
			var av: Array = s.available()
			if av.is_empty():
				break
			var m: Dictionary = av[rng.randi() % av.size()]
			for ch in m.get("choices", []):
				var opts: Array = s.options_available(ch)
				if opts.is_empty():
					dead += 1
					print("SACKGASSE: keine Option bei ", ch["id"])
					break
				s.choose(ch, opts[rng.randi() % opts.size()])
			s.complete(m)
		if not (s.state["completed"] as Array).has("w5_finale"):
			dead += 1
		var e: Dictionary = s.evaluate_ending()
		counts[e["id"]] = counts.get(e["id"], 0) + 1
	print("STORYSIM runs=", runs, " enden=", counts, " sackgassen=", dead)
	var all_found := counts.has("E1") and counts.has("E2") and counts.has("E3") and counts.has("E4")
	print("STORYSIM alle_enden_erreichbar=", all_found)
	quit(0 if dead == 0 and all_found else 1)
