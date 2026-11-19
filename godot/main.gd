extends Node3D
## MTA 6 (Godot) - Stadt, Auto, Verkehr, Tag/Nacht.
## Spieler-Auto (VehicleBody3D) faehrt in +Z, Verkehrsautos (Node3D) haben ihre Front bei -Z.

const P := 80.0
const N := 8
const HALF := N * P / 2.0

const GROUND_SHADER := """
shader_type spatial;
varying vec3 wp;
const float P = 80.0;
const float HALF = 320.0;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void vertex() { wp = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz; }
void fragment() {
	vec2 q = wp.xz + HALF;
	vec2 m = mod(q, P);
	vec2 d = min(m, P - m);
	bool inside = abs(wp.x) < HALF + 8.0 && abs(wp.z) < HALF + 8.0;
	bool roadx = d.x < 8.0;
	bool roadz = d.y < 8.0;
	vec3 col = vec3(0.16, 0.27, 0.12);
	float rough = 1.0;
	if (inside) {
		col = vec3(0.52, 0.52, 0.50);
		if (roadx || roadz) {
			col = vec3(0.11, 0.11, 0.12);
			rough = 0.8;
			if (!(roadx && roadz)) {
				float along = roadx ? q.y : q.x;
				float across = roadx ? d.x : d.y;
				float seg = mod(along, P);
				if (across < 0.35 && seg > 12.0 && seg < 68.0) col = vec3(0.9, 0.7, 0.1);
				if (abs(across - 7.4) < 0.15 && seg > 9.0 && seg < 71.0) col = vec3(0.75);
				if (((seg > 8.0 && seg < 11.0) || (seg > 69.0 && seg < 72.0)) && across < 7.0 && mod(across * 1.2, 2.0) < 1.0) col = vec3(0.85);
			}
		}
	}
	float n = hash(floor(wp.xz * 2.5)) * 0.5 + hash(floor(wp.xz * 0.4)) * 0.5;
	col *= 0.88 + 0.24 * n;
	if (inside && !(roadx || roadz) && min(d.x, d.y) < 8.5) col *= 0.7;
	ALBEDO = col;
	ROUGHNESS = rough - 0.1 * n;
}
"""

const BUILDING_SHADER := """
shader_type spatial;
instance uniform vec4 tint : source_color = vec4(0.8, 0.8, 0.8, 1.0);
uniform float night = 0.0;
varying vec3 wp;
varying vec3 wn;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void vertex() {
	wp = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	wn = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
}
void fragment() {
	vec3 col = tint.rgb;
	float emit = 0.0;
	float rough = 0.85;
	float metal = 0.0;
	if (abs(wn.y) < 0.5) {
		float u = abs(wn.x) > 0.5 ? wp.z : wp.x;
		float w = abs(wn.x) > 0.5 ? wp.x : wp.z;
		vec2 cell = vec2(floor(u / 4.0), floor(wp.y / 3.5));
		vec2 f = vec2(fract(u / 4.0), fract(wp.y / 3.5));
		if (f.x > 0.2 && f.x < 0.8 && f.y > 0.25 && f.y < 0.8 && wp.y > 1.0) {
			col = vec3(0.06, 0.09, 0.13);
			rough = 0.1;
			metal = 0.6;
			if (hash(cell + vec2(floor(w * 0.5), 3.0)) < 0.45) emit = 1.0;
		}
	} else {
		col = vec3(0.22);
	}
	ALBEDO = col;
	ROUGHNESS = rough;
	METALLIC = metal;
	EMISSION = emit * night * vec3(1.0, 0.82, 0.5) * 1.6;
}
"""

class Agent extends RefCounted:
	var axis := 0
	var sign := 1
	var line := 0
	var pos := 0.0
	var v := 0.0
	var cruise := 12.0
	var off := 0.0
	var kind := "traffic"
	var side := 1
	var stun := 0.0
	var wait := 0.0
	var ghost := 0.0
	var node: Node3D

var car: VehicleBody3D
var cam: Camera3D
var sun: DirectionalLight3D
var env: Environment
var sky_mat: ProceduralSkyMaterial
var bld_mat: ShaderMaterial
var headlight: SpotLight3D
var hud: Label
var hour := 18.0
var agents: Array[Agent] = []
var rng := RandomNumberGenerator.new()
var test_mode := false
var test_time := 0.0
var fps_mode := false
var fps_time := 0.0
var lamp_positions: Array[Vector3] = []
var lamp_lights: Array[OmniLight3D] = []
var lamp_head_mat: StandardMaterial3D
var lamp_timer := 0.0
var tree_xforms: Array[Vector3] = []

func rr(k: float) -> float:
	return k * P - HALF

func _ready() -> void:
	rng.seed = 6
	test_mode = "--test" in OS.get_cmdline_user_args()
	if test_mode:
		save_path = "user://test_save.json"
	fps_mode = "--fps" in OS.get_cmdline_user_args()
	story = StoryScript.new()
	if not story.load_data("res://data/story.json"):
		push_error("story.json konnte nicht geladen werden")
	_setup_input()
	_build_environment()
	_build_ground()
	_build_city()
	_build_lamps()
	_build_car()
	_build_traffic()
	_build_hud()
	print("MTA6 bereit: Gebaeude und Auto erzeugt")

func _setup_input() -> void:
	var map := {
		"accel": [KEY_W, KEY_UP], "brake": [KEY_S, KEY_DOWN],
		"left": [KEY_A, KEY_LEFT], "right": [KEY_D, KEY_RIGHT],
		"handbrake": [KEY_SPACE], "reset": [KEY_R],
		"save": [KEY_F5], "load": [KEY_F9],
	}
	for action in map:
		if not InputMap.has_action(action):
			InputMap.add_action(action)
		for k in map[action]:
			var ev := InputEventKey.new()
			ev.physical_keycode = k
			InputMap.action_add_event(action, ev)

func _build_environment() -> void:
	sky_mat = ProceduralSkyMaterial.new()
	var sky := Sky.new()
	sky.sky_material = sky_mat
	env = Environment.new()
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env.ssao_enabled = true
	env.ssr_enabled = true
	env.ssao_radius = 2.0
	env.ssao_intensity = 2.0
	env.glow_enabled = true
	env.glow_intensity = 0.9
	env.glow_bloom = 0.08
	env.sdfgi_enabled = true
	env.sdfgi_cascades = 3
	env.sdfgi_use_occlusion = true
	env.fog_enabled = true
	env.fog_density = 0.0006
	env.volumetric_fog_enabled = true
	env.volumetric_fog_density = 0.004
	env.volumetric_fog_gi_inject = 1.0
	env.adjustment_enabled = true
	env.adjustment_contrast = 1.08
	env.adjustment_saturation = 1.12
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	sun = DirectionalLight3D.new()
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 260.0
	add_child(sun)

func _build_ground() -> void:
	var sh := Shader.new()
	sh.code = GROUND_SHADER
	var mat := ShaderMaterial.new()
	mat.shader = sh
	var mi := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(4000, 4000)
	mi.mesh = pm
	mi.material_override = mat
	add_child(mi)
	var body := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var bs := BoxShape3D.new()
	bs.size = Vector3(4000, 1, 4000)
	cs.shape = bs
	cs.position.y = -0.5
	body.add_child(cs)
	add_child(body)

func _build_city() -> void:
	var sh := Shader.new()
	sh.code = BUILDING_SHADER
	bld_mat = ShaderMaterial.new()
	bld_mat.shader = sh
	var wall_cols := [Color(0.81, 0.81, 0.83), Color(0.85, 0.78, 0.69), Color(0.72, 0.76, 0.82), Color(0.79, 0.73, 0.69), Color(0.66, 0.69, 0.72), Color(0.88, 0.85, 0.78)]
	for i in N:
		for j in N:
			var cx := rr(i) + P / 2.0
			var cz := rr(j) + P / 2.0
			if rng.randf() < 0.14:
				_make_park(cx, cz)
				continue
			var dist := Vector2(cx, cz).length() / (HALF * 1.2)
			var max_h := lerpf(80.0, 16.0, clampf(dist, 0.0, 1.0))
			var lots: Array = []
			var n: int = [1, 2, 2, 4][rng.randi() % 4]
			if n == 1:
				lots.append([cx, cz, rng.randf_range(40, 54), rng.randf_range(40, 54)])
			elif n == 2:
				if rng.randf() < 0.5:
					lots.append([cx - 14, cz, 26.0, rng.randf_range(44, 54)])
					lots.append([cx + 14, cz, 26.0, rng.randf_range(44, 54)])
				else:
					lots.append([cx, cz - 14, rng.randf_range(44, 54), 26.0])
					lots.append([cx, cz + 14, rng.randf_range(44, 54), 26.0])
			else:
				for sx in [-1, 1]:
					for sz in [-1, 1]:
						lots.append([cx + sx * 14, cz + sz * 14, rng.randf_range(22, 26), rng.randf_range(22, 26)])
			for lot in lots:
				_make_building(lot[0], lot[1], lot[2], lot[3], roundf(rng.randf_range(10.0, max_h)), wall_cols[rng.randi() % wall_cols.size()])

func _make_building(x: float, z: float, w: float, d: float, h: float, color: Color) -> void:
	var body := StaticBody3D.new()
	body.position = Vector3(x, h / 2.0, z)
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = Vector3(w, h, d)
	mi.mesh = bm
	mi.material_override = bld_mat
	mi.set_instance_shader_parameter("tint", color)
	body.add_child(mi)
	var cs := CollisionShape3D.new()
	var bs := BoxShape3D.new()
	bs.size = Vector3(w, h, d)
	cs.shape = bs
	body.add_child(cs)
	add_child(body)
	# Details: Dachaufbau, bei Hochhaeusern ein zurueckgesetztes Obergeschoss und eine Antenne
	if h > 38.0:
		var tier := MeshInstance3D.new()
		var tm := BoxMesh.new()
		tm.size = Vector3(w * 0.7, h * 0.22, d * 0.7)
		tier.mesh = tm
		tier.material_override = bld_mat
		tier.set_instance_shader_parameter("tint", color.darkened(0.08))
		tier.position = Vector3(0, h / 2.0 + tm.size.y / 2.0, 0)
		body.add_child(tier)
		var ant := MeshInstance3D.new()
		var cm := CylinderMesh.new()
		cm.top_radius = 0.15
		cm.bottom_radius = 0.3
		cm.height = 12.0
		ant.mesh = cm
		var am := StandardMaterial3D.new()
		am.albedo_color = Color(0.2, 0.2, 0.22)
		ant.material_override = am
		ant.position = Vector3(0, h / 2.0 + tm.size.y + 6.0, 0)
		body.add_child(ant)
	else:
		var ac := MeshInstance3D.new()
		var am2 := BoxMesh.new()
		am2.size = Vector3(w * 0.25, 1.6, d * 0.2)
		ac.mesh = am2
		var acm := StandardMaterial3D.new()
		acm.albedo_color = Color(0.4, 0.42, 0.45)
		ac.material_override = acm
		ac.position = Vector3(w * 0.2, h / 2.0 + 0.8, d * 0.15)
		body.add_child(ac)

func _multimesh(mesh: Mesh, xforms: Array, mat: Material) -> void:
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.mesh = mesh
	mm.instance_count = xforms.size()
	for i in xforms.size():
		mm.set_instance_transform(i, xforms[i])
	var mmi := MultiMeshInstance3D.new()
	mmi.multimesh = mm
	mmi.material_override = mat
	add_child(mmi)

func _make_park(cx: float, cz: float) -> void:
	var gr := MeshInstance3D.new()
	var gm := BoxMesh.new()
	gm.size = Vector3(56, 0.2, 56)
	gr.mesh = gm
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.24, 0.42, 0.18)
	mat.roughness = 1.0
	gr.material_override = mat
	gr.position = Vector3(cx, 0.1, cz)
	add_child(gr)
	for i in 16:
		tree_xforms.append(Vector3(cx + rng.randf_range(-25, 25), 0, cz + rng.randf_range(-25, 25)))

func _build_trees() -> void:
	var trunk := CylinderMesh.new()
	trunk.top_radius = 0.3
	trunk.bottom_radius = 0.45
	trunk.height = 3.0
	var crown := SphereMesh.new()
	crown.radius = 2.8
	crown.height = 5.6
	var tmat := StandardMaterial3D.new()
	tmat.albedo_color = Color(0.3, 0.2, 0.12)
	var cmat := StandardMaterial3D.new()
	cmat.albedo_color = Color(0.17, 0.42, 0.16)
	cmat.roughness = 1.0
	var trunks: Array = []
	var crowns: Array = []
	for p in tree_xforms:
		var s := rng.randf_range(0.8, 1.4)
		trunks.append(Transform3D(Basis().scaled(Vector3(s, s, s)), p + Vector3(0, 1.5 * s, 0)))
		crowns.append(Transform3D(Basis().scaled(Vector3(s, s, s)), p + Vector3(0, 5.2 * s, 0)))
	_multimesh(trunk, trunks, tmat)
	_multimesh(crown, crowns, cmat)

func _build_lamps() -> void:
	_build_trees()
	for k in N + 1:
		for j in N:
			var mid := rr(j) + P / 2.0
			for s in [-1.0, 1.0]:
				lamp_positions.append(Vector3(rr(k) + s * 9.0, 0.0, mid))
				lamp_positions.append(Vector3(mid, 0.0, rr(k) + s * 9.0))
	for i in N + 1:
		for j in N + 1:
			for sx in [-1.0, 1.0]:
				for sz in [-1.0, 1.0]:
					lamp_positions.append(Vector3(rr(i) + sx * 9.5, 0.0, rr(j) + sz * 9.5))
	var pole := CylinderMesh.new()
	pole.top_radius = 0.1
	pole.bottom_radius = 0.16
	pole.height = 7.0
	var head := BoxMesh.new()
	head.size = Vector3(0.9, 0.18, 0.5)
	var pmat := StandardMaterial3D.new()
	pmat.albedo_color = Color(0.18, 0.19, 0.2)
	pmat.metallic = 0.6
	lamp_head_mat = StandardMaterial3D.new()
	lamp_head_mat.albedo_color = Color(1.0, 0.85, 0.6)
	lamp_head_mat.emission_enabled = true
	lamp_head_mat.emission = Color(1.0, 0.8, 0.5)
	lamp_head_mat.emission_energy_multiplier = 0.0
	var poles: Array = []
	var heads: Array = []
	for p in lamp_positions:
		poles.append(Transform3D(Basis(), p + Vector3(0, 3.5, 0)))
		heads.append(Transform3D(Basis(), p + Vector3(0, 7.1, 0)))
	_multimesh(pole, poles, pmat)
	_multimesh(head, heads, lamp_head_mat)
	for i in 8:
		var l := OmniLight3D.new()
		l.omni_range = 30.0
		l.light_color = Color(1.0, 0.8, 0.55)
		l.light_energy = 0.0
		add_child(l)
		lamp_lights.append(l)

func _update_lamp_lights(night: float) -> void:
	lamp_head_mat.emission_energy_multiplier = night * 6.0
	var cp := car.global_position
	var sorted := lamp_positions.duplicate()
	sorted.sort_custom(func(a: Vector3, b: Vector3) -> bool: return a.distance_squared_to(cp) < b.distance_squared_to(cp))
	for i in lamp_lights.size():
		lamp_lights[i].global_position = sorted[i] + Vector3(0, 6.5, 0)
		lamp_lights[i].light_energy = night * 4.0

func _paint(c: Color, metal: float = 0.5) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.metallic = metal
	m.roughness = 0.35
	return m

func _lamp(parent: Node3D, pos: Vector3, c: Color) -> void:
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = Vector3(0.4, 0.2, 0.1)
	mi.mesh = bm
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.emission_enabled = true
	m.emission = c
	m.emission_energy_multiplier = 1.5
	mi.material_override = m
	mi.position = pos
	parent.add_child(mi)

func _car_visual(parent: Node3D, color: Color) -> void:
	var body := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = Vector3(2.0, 0.7, 4.4)
	body.mesh = bm
	body.material_override = _paint(color)
	body.position.y = 0.6
	parent.add_child(body)
	var cab := MeshInstance3D.new()
	var cm := BoxMesh.new()
	cm.size = Vector3(1.75, 0.65, 2.2)
	cab.mesh = cm
	cab.material_override = _paint(Color(0.08, 0.12, 0.18), 0.7)
	cab.position = Vector3(0, 1.25, 0.25)
	parent.add_child(cab)
	for sx in [-1.0, 1.0]:
		_lamp(parent, Vector3(sx * 0.7, 0.7, -2.2), Color(1.0, 0.95, 0.75))
		_lamp(parent, Vector3(sx * 0.7, 0.75, 2.2), Color(1.0, 0.1, 0.1))

func _build_car() -> void:
	car = VehicleBody3D.new()
	car.mass = 1200.0
	car.linear_damp = 0.05
	car.position = Vector3(-4, 1.2, -40)
	car.center_of_mass_mode = RigidBody3D.CENTER_OF_MASS_MODE_CUSTOM
	car.center_of_mass = Vector3(0, 0.1, 0)
	# VehicleBody3D faehrt in +Z: Optik um 180 Grad drehen, damit die Front vorne ist
	var vis := Node3D.new()
	vis.rotation.y = PI
	car.add_child(vis)
	_car_visual(vis, Color(0.85, 0.15, 0.17))
	var cs := CollisionShape3D.new()
	var bs := BoxShape3D.new()
	bs.size = Vector3(2.0, 0.9, 4.4)
	cs.shape = bs
	cs.position.y = 0.65
	car.add_child(cs)
	for sx in [-1.0, 1.0]:
		for sz in [-1.4, 1.4]:
			var w := VehicleWheel3D.new()
			w.position = Vector3(sx * 1.0, 0.35, sz)
			w.use_as_traction = true
			w.use_as_steering = sz > 0.0
			w.wheel_radius = 0.4
			w.wheel_rest_length = 0.2
			w.suspension_travel = 0.3
			w.suspension_stiffness = 45.0
			w.suspension_max_force = 9000.0
			w.damping_compression = 2.4
			w.damping_relaxation = 3.0
			w.wheel_friction_slip = 3.2
			var wm := MeshInstance3D.new()
			var cyl := CylinderMesh.new()
			cyl.top_radius = 0.4
			cyl.bottom_radius = 0.4
			cyl.height = 0.3
			wm.mesh = cyl
			wm.rotation.z = PI / 2.0
			var tm := StandardMaterial3D.new()
			tm.albedo_color = Color(0.05, 0.05, 0.05)
			wm.material_override = tm
			w.add_child(wm)
			car.add_child(w)
	headlight = SpotLight3D.new()
	headlight.position = Vector3(0, 1.0, 2.3)
	headlight.rotation = Vector3(deg_to_rad(-4), PI, 0)
	headlight.spot_range = 80.0
	headlight.spot_angle = 35.0
	headlight.light_energy = 0.0
	car.add_child(headlight)
	add_child(car)
	cam = Camera3D.new()
	cam.fov = 68.0
	cam.far = 1500.0
	add_child(cam)
	cam.make_current()

var money := 1000
var heat := 0.0
var last_crime := -99.0
var gtime := 0.0
var hit_cool := 0.0
var bust_t := 0.0
var police: Array[Agent] = []
var mission_pos := Vector3.ZERO
var mission_time := 0.0
var marker: Node3D
var msg_label: Label
var msg_timer := 0.0

const StoryScript := preload("res://story.gd")
var story
var current_mission: Dictionary = {}   # leer = freie Kurierfahrt
var dialog_open := false
var dialog_panel: PanelContainer
var dialog_label: Label
var dialog_options: Array = []
var dialog_queue: Array = []
var dialog_choice: Dictionary = {}
var dialog_mission: Dictionary = {}
var dialog_stage := ""

func _build_traffic() -> void:
	for i in 28:
		_spawn_agent("traffic")
	for i in 60:
		_spawn_agent("ped")
	_build_marker()
	_new_mission()

func _spawn_agent(kind: String) -> Agent:
	var cols: Array = [Color(0.23, 0.43, 0.88), Color(0.94, 0.94, 0.94), Color(0.13, 0.13, 0.13), Color(0.88, 0.75, 0.19), Color(0.23, 0.66, 0.38), Color(0.53, 0.55, 0.58)]
	var a := Agent.new()
	a.kind = kind
	a.axis = rng.randi() % 2
	a.line = rng.randi() % (N + 1)
	a.sign = 1 if rng.randf() < 0.5 else -1
	a.pos = rng.randf_range(-HALF + 5.0, HALF - 5.0)
	a.side = 1 if rng.randf() < 0.5 else -1
	if kind == "ped":
		a.cruise = rng.randf_range(1.6, 2.6)
	elif kind == "police":
		a.cruise = 26.0
	else:
		a.cruise = rng.randf_range(10.0, 17.0)
	a.v = a.cruise
	_set_off(a)
	a.node = Node3D.new()
	if kind == "ped":
		_ped_visual(a.node)
	elif kind == "police":
		_car_visual(a.node, Color(0.06, 0.06, 0.09))
		_police_lights(a.node)
	else:
		_car_visual(a.node, cols[rng.randi() % cols.size()])
	add_child(a.node)
	var t := _agent_target(a)
	a.node.position = Vector3(t.x, 0.0, t.y)
	a.node.rotation.y = _agent_heading(a)
	agents.append(a)
	return a

func _remove_agent(a: Agent) -> void:
	agents.erase(a)
	police.erase(a)
	a.node.queue_free()

func _ped_visual(parent: Node3D) -> void:
	var shirts: Array = [Color(0.8, 0.25, 0.25), Color(0.25, 0.45, 0.8), Color(0.25, 0.7, 0.45), Color(0.8, 0.75, 0.25), Color(0.6, 0.3, 0.75), Color(0.9, 0.9, 0.9), Color(0.2, 0.2, 0.2)]
	var mi := MeshInstance3D.new()
	var cm := CapsuleMesh.new()
	cm.radius = 0.26
	cm.height = 1.75
	mi.mesh = cm
	var m := StandardMaterial3D.new()
	m.albedo_color = shirts[rng.randi() % shirts.size()]
	m.roughness = 0.9
	mi.material_override = m
	mi.position.y = 0.88
	parent.add_child(mi)

func _police_lights(parent: Node3D) -> void:
	var stripe := MeshInstance3D.new()
	var sm := BoxMesh.new()
	sm.size = Vector3(2.02, 0.25, 2.6)
	stripe.mesh = sm
	stripe.material_override = _paint(Color(0.92, 0.92, 0.92), 0.2)
	stripe.position.y = 0.65
	parent.add_child(stripe)
	var mats: Array = []
	var colors := [Color(1.0, 0.1, 0.1), Color(0.15, 0.3, 1.0)]
	for i in 2:
		var bar := MeshInstance3D.new()
		var bm := BoxMesh.new()
		bm.size = Vector3(0.7, 0.18, 0.4)
		bar.mesh = bm
		var m := StandardMaterial3D.new()
		m.albedo_color = colors[i]
		m.emission_enabled = true
		m.emission = colors[i]
		m.emission_energy_multiplier = 0.2
		bar.material_override = m
		bar.position = Vector3(-0.45 + 0.9 * i, 1.65, 0.2)
		parent.add_child(bar)
		mats.append(m)
	parent.set_meta("lights", mats)

func _set_off(a: Agent) -> void:
	if a.kind == "ped":
		a.off = a.side * 9.5
	elif a.kind == "police":
		a.off = 0.0
	else:
		a.off = 4.0 * a.sign if a.axis == 0 else -4.0 * a.sign

func _agent_target(a: Agent) -> Vector2:
	var c := rr(a.line)
	if a.axis == 0:
		return Vector2(a.pos, c + a.off)
	return Vector2(c + a.off, a.pos)

func _agent_heading(a: Agent) -> float:
	var dx := float(a.sign) if a.axis == 0 else 0.0
	var dz := float(a.sign) if a.axis == 1 else 0.0
	return atan2(-dx, -dz)

func _agent_intersection(a: Agent, m: int) -> void:
	var k := a.line
	var ix := rr(m) if a.axis == 0 else rr(k)
	var iz := rr(k) if a.axis == 0 else rr(m)
	var opts: Array = []
	var straight_ok := not (m == N and a.sign > 0) and not (m == 0 and a.sign < 0)
	if straight_ok:
		var sd := Vector2(a.sign, 0) if a.axis == 0 else Vector2(0, a.sign)
		opts.append([0, 0.6, Vector2(ix, iz) + sd * P * 0.7])
	for s in [1, -1]:
		if (k == 0 and s < 0) or (k == N and s > 0):
			continue
		var td := Vector2(0, s) if a.axis == 0 else Vector2(s, 0)
		opts.append([s, 0.2, Vector2(ix, iz) + td * P * 0.7])
	var ch: int = opts[opts.size() - 1][0]
	if a.kind == "police" and rng.randf() > 0.1:
		var target := Vector2(car.global_position.x, car.global_position.z)
		var best := INF
		for o in opts:
			var dd: float = (o[2] as Vector2).distance_to(target)
			if dd < best:
				best = dd
				ch = o[0]
	else:
		var total := 0.0
		for o in opts:
			total += o[1]
		var r := rng.randf() * total
		for o in opts:
			r -= o[1]
			if r <= 0.0:
				ch = o[0]
				break
	if ch == 0:
		return
	a.axis = 1 - a.axis
	a.line = m
	a.sign = ch
	a.pos = rr(k) + ch * 1.0
	if a.kind == "ped":
		a.side = 1 if rng.randf() < 0.5 else -1
	_set_off(a)

func _ahead(p: Vector3, fwd: Vector3, q: Vector3) -> bool:
	var d := q - p
	d.y = 0.0
	var f := d.dot(fwd)
	if f <= 0.0 or f > 11.0:
		return false
	return absf(d.x * fwd.z - d.z * fwd.x) < 2.6

func _blocked(a: Agent) -> bool:
	var fwd := -a.node.global_transform.basis.z
	var p := a.node.global_position
	for o in agents:
		if o != a and o.kind != "ped" and _ahead(p, fwd, o.node.global_position):
			return true
	return a.kind == "traffic" and _ahead(p, fwd, car.global_position)

func _update_agents(dt: float) -> void:
	var cp := car.global_position
	var speed := car.linear_velocity.length()
	for a: Agent in agents.duplicate():
		if a.stun > 0.0:
			a.stun -= dt
			a.v = move_toward(a.v, 0.0, 40.0 * dt)
			if a.kind == "ped":
				if a.stun <= 0.0:
					a.node.rotation.x = 0.0
					a.node.position.y = 0.0
				continue
		else:
			var target := a.cruise
			if a.kind == "police":
				target = 24.0 + heat * 2.5
			if a.kind != "ped":
				if a.ghost > 0.0:
					a.ghost -= dt
				elif _blocked(a):
					target = 0.0
					a.wait += dt
					if a.wait > 4.0:
						a.ghost = 3.0
						a.wait = 0.0
				else:
					a.wait = 0.0
			a.v = move_toward(a.v, target, (14.0 if target > a.v else 30.0) * dt)
		var prev := a.pos
		a.pos += a.sign * a.v * dt
		var k1 := int(floor((prev + HALF) / P))
		var k2 := int(floor((a.pos + HALF) / P))
		if k1 != k2:
			_agent_intersection(a, k2 if a.sign > 0 else k1)
		var t := _agent_target(a)
		var f := 1.0 - exp(-dt * 12.0)
		a.node.position.x += (t.x - a.node.position.x) * f
		a.node.position.z += (t.y - a.node.position.z) * f
		a.node.rotation.y = lerp_angle(a.node.rotation.y, _agent_heading(a), f)
		if a.kind == "police":
			var lm: Array = a.node.get_meta("lights")
			var on := int(gtime * 8.0) % 2 == 0
			lm[0].emission_energy_multiplier = 4.0 if on else 0.2
			lm[1].emission_energy_multiplier = 0.2 if on else 4.0
		var d := a.node.global_position - cp
		d.y = 0.0
		var dist := d.length()
		if a.kind == "ped":
			if dist < 2.2 and speed > 4.0 and a.stun <= 0.0:
				a.stun = 5.0
				a.v = 0.0
				a.node.rotation.x = -PI / 2.0
				a.node.position.y = 0.3
				_add_heat(0.7)
		elif dist < 3.5:
			_on_car_hit(a)

func _add_heat(v: float) -> void:
	heat = minf(5.0, heat + v)
	last_crime = gtime

func _on_car_hit(a: Agent) -> void:
	if hit_cool > 0.0:
		return
	hit_cool = 0.5
	var speed := car.linear_velocity.length()
	var dir := car.global_position - a.node.global_position
	dir.y = 0.0
	car.apply_central_impulse(dir.normalized() * 1500.0)
	car.linear_velocity *= 0.75
	a.stun = 0.6 if a.kind == "police" else 1.5
	if a.kind != "police" and speed > 6.0:
		_add_heat(0.35)

func _spawn_police() -> void:
	for t in 30:
		var a := _spawn_agent("police")
		var d := Vector2(a.node.global_position.x - car.global_position.x, a.node.global_position.z - car.global_position.z).length()
		if d > 90.0 and d < 200.0:
			a.v = 15.0
			police.append(a)
			return
		_remove_agent(a)

func _bust() -> void:
	var fine := mini(money, 300)
	money -= fine
	heat = 0.0
	bust_t = 0.0
	for p: Agent in police.duplicate():
		_remove_agent(p)
	car.global_position = Vector3(-4, 1.2, -40)
	car.rotation = Vector3.ZERO
	car.linear_velocity = Vector3.ZERO
	car.angular_velocity = Vector3.ZERO
	toast("FESTGENOMMEN  -$%d" % fine)

func _update_game(dt: float) -> void:
	gtime += dt
	hit_cool -= dt
	msg_timer -= dt
	msg_label.modulate.a = clampf(msg_timer, 0.0, 1.0)
	var stars := int(ceil(heat - 0.001))
	var desired := 0 if heat <= 0.0 else mini(8, 1 + stars)
	if police.size() < desired and rng.randf() < dt * 0.8:
		_spawn_police()
	var cp := car.global_position
	var near_police := false
	var close_police := false
	for p in police:
		var dd := p.node.global_position.distance_to(cp)
		if dd < 60.0:
			near_police = true
		if dd < 7.0:
			close_police = true
	if heat > 0.0 and gtime - last_crime > 8.0 and not near_police:
		heat = maxf(0.0, heat - 0.12 * dt)
	if heat <= 0.0:
		for p: Agent in police.duplicate():
			if p.node.global_position.distance_to(cp) > 150.0:
				_remove_agent(p)
	if heat > 0.0 and close_police and car.linear_velocity.length() < 3.0:
		bust_t += dt
		if bust_t > 2.5:
			_bust()
	else:
		bust_t = 0.0
	mission_time -= dt
	marker.rotation.y += dt
	if Vector2(mission_pos.x - cp.x, mission_pos.z - cp.z).length() < 9.0:
		if not current_mission.is_empty():
			_complete_story_mission()
		else:
			var gain := 300 + int(maxf(0.0, mission_time) * 8.0)
			money += gain
			toast("Ziel erreicht!  +$%d" % gain)
			_new_mission()
			save_game()

func _build_marker() -> void:
	marker = Node3D.new()
	var mi := MeshInstance3D.new()
	var cyl := CylinderMesh.new()
	cyl.top_radius = 5.0
	cyl.bottom_radius = 5.0
	cyl.height = 70.0
	cyl.cap_top = false
	cyl.cap_bottom = false
	mi.mesh = cyl
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.albedo_color = Color(1.0, 0.82, 0.2, 0.28)
	m.cull_mode = BaseMaterial3D.CULL_DISABLED
	mi.material_override = m
	mi.position.y = 35.0
	marker.add_child(mi)
	add_child(marker)

func _new_mission() -> void:
	var av: Array = story.available()
	if av.size() > 0:
		current_mission = av[0]
		var cell: Array = current_mission["cell"]
		mission_pos = Vector3(rr(cell[0]), 0.0, rr(cell[1]))
		mission_time = 0.0
		marker.global_position = mission_pos
		return
	current_mission = {}
	for t in 30:
		var x := rr(rng.randi() % (N + 1))
		var z := rr(rng.randi() % (N + 1))
		var dist := Vector2(x - car.global_position.x, z - car.global_position.z).length()
		if dist > 150.0:
			mission_pos = Vector3(x, 0.0, z)
			mission_time = dist / 14.0 + 20.0
			marker.global_position = mission_pos
			return

var save_path := "user://savegame.json"
const SAVE_VERSION := 2

func save_game() -> bool:
	var data := {
		"version": SAVE_VERSION,
		"money": money,
		"heat": heat,
		"hour": hour,
		"car": {
			"pos": [car.global_position.x, car.global_position.y, car.global_position.z],
			"yaw": car.global_rotation.y,
		},
		"mission": {"pos": [mission_pos.x, mission_pos.z], "time": mission_time},
		"story": story.to_dict(),
	}
	var f := FileAccess.open(save_path, FileAccess.WRITE)
	if f == null:
		toast("Speichern fehlgeschlagen")
		return false
	f.store_string(JSON.stringify(data, "\t"))
	f.close()
	toast("Gespeichert (F5)")
	return true

func load_game() -> bool:
	if not FileAccess.file_exists(save_path):
		toast("Kein Spielstand vorhanden")
		return false
	var f := FileAccess.open(save_path, FileAccess.READ)
	var parsed: Variant = JSON.parse_string(f.get_as_text())
	f.close()
	if typeof(parsed) != TYPE_DICTIONARY or not [1, SAVE_VERSION].has(int(parsed.get("version", 0))):
		toast("Spielstand unbrauchbar")
		return false
	money = int(parsed["money"])
	heat = float(parsed["heat"])
	hour = float(parsed["hour"])
	var c: Dictionary = parsed["car"]
	car.global_position = Vector3(c["pos"][0], c["pos"][1] + 0.3, c["pos"][2])
	car.global_rotation = Vector3(0.0, float(c["yaw"]), 0.0)
	car.linear_velocity = Vector3.ZERO
	car.angular_velocity = Vector3.ZERO
	if parsed.has("story"):
		if not story.from_dict(parsed["story"]):
			story.reset()
	else:
		story.reset()
	if dialog_open:
		_close_dialog()
	_new_mission()
	for p: Agent in police.duplicate():
		_remove_agent(p)
	toast("Geladen (F9)")
	return true

func _apply_story_outputs() -> void:
	money += story.out_money
	story.out_money = 0
	if story.out_heat > 0.0:
		_add_heat(story.out_heat)
	story.out_heat = 0.0

func _show_dialog(title: String, text: String, options: Array) -> void:
	dialog_options = options
	var s := "%s\n\n%s\n\n" % [title, text]
	for i in options.size():
		s += "[%d] %s\n" % [i + 1, options[i]["text"]]
	if options.is_empty():
		s += "[Enter] Weiter"
	dialog_label.text = s
	dialog_panel.visible = true
	dialog_open = true

func _close_dialog() -> void:
	dialog_panel.visible = false
	dialog_open = false
	dialog_stage = ""

func _complete_story_mission() -> void:
	dialog_mission = current_mission
	dialog_queue = (current_mission.get("choices", []) as Array).duplicate()
	dialog_stage = "text"
	_show_dialog(current_mission["title"], current_mission.get("text", ""), [])

func _next_choice_or_finish() -> void:
	while not dialog_queue.is_empty():
		var ch: Dictionary = dialog_queue.pop_front()
		var opts: Array = story.options_available(ch)
		if opts.is_empty():
			continue
		dialog_choice = ch
		dialog_stage = "choice"
		_show_dialog(dialog_mission["title"], ch["text"], opts)
		return
	_finish_story_mission()

func _finish_story_mission() -> void:
	story.complete(dialog_mission)
	_apply_story_outputs()
	var finale: bool = dialog_mission.get("finale", false)
	toast("Auftrag abgeschlossen: %s" % dialog_mission["title"])
	current_mission = {}
	_new_mission()
	save_game()
	if finale:
		var e: Dictionary = story.evaluate_ending()
		dialog_stage = "ending"
		_show_dialog("ENDE: " + e["title"], e["epilogue"] + "\n\nDu kannst weiter frei fahren.", [])
	else:
		_close_dialog()

func _unhandled_input(event: InputEvent) -> void:
	if not dialog_open or not (event is InputEventKey) or not event.pressed or event.echo:
		return
	if dialog_options.is_empty():
		if event.keycode == KEY_ENTER or event.keycode == KEY_KP_ENTER:
			if dialog_stage == "text":
				_next_choice_or_finish()
			elif dialog_stage == "ending":
				_close_dialog()
		return
	var idx: int = event.keycode - KEY_1
	if idx >= 0 and idx < dialog_options.size():
		story.choose(dialog_choice, dialog_options[idx])
		_apply_story_outputs()
		_next_choice_or_finish()

func toast(txt: String) -> void:
	msg_label.text = txt
	msg_timer = 3.0

func _build_hud() -> void:
	var layer := CanvasLayer.new()
	hud = Label.new()
	hud.position = Vector2(20, 14)
	hud.add_theme_font_size_override("font_size", 26)
	hud.add_theme_color_override("font_outline_color", Color.BLACK)
	hud.add_theme_constant_override("outline_size", 6)
	layer.add_child(hud)
	msg_label = Label.new()
	msg_label.anchor_right = 1.0
	msg_label.position.y = 130.0
	msg_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	msg_label.add_theme_font_size_override("font_size", 38)
	msg_label.add_theme_color_override("font_outline_color", Color.BLACK)
	msg_label.add_theme_constant_override("outline_size", 8)
	msg_label.modulate.a = 0.0
	layer.add_child(msg_label)
	var mm := Control.new()
	mm.set_script(load("res://minimap.gd"))
	mm.set("main", self)
	mm.anchor_left = 1.0
	mm.anchor_right = 1.0
	mm.offset_left = -230.0
	mm.offset_right = -20.0
	mm.offset_top = 20.0
	mm.offset_bottom = 230.0
	layer.add_child(mm)
	dialog_panel = PanelContainer.new()
	dialog_panel.visible = false
	dialog_panel.set_anchors_preset(Control.PRESET_CENTER)
	dialog_panel.grow_horizontal = Control.GROW_DIRECTION_BOTH
	dialog_panel.grow_vertical = Control.GROW_DIRECTION_BOTH
	dialog_panel.custom_minimum_size = Vector2(780, 0)
	dialog_label = Label.new()
	dialog_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	dialog_label.custom_minimum_size = Vector2(740, 0)
	dialog_label.add_theme_font_size_override("font_size", 24)
	dialog_panel.add_child(dialog_label)
	layer.add_child(dialog_panel)
	var help := Label.new()
	help.text = "W/S Gas/Bremse   A/D Lenken   Leertaste Handbremse   R Zuruecksetzen   F5 Speichern   F9 Laden"
	help.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	help.offset_left = 20.0
	help.offset_top = -36.0
	layer.add_child(help)
	add_child(layer)

func _physics_process(delta: float) -> void:
	var throttle := Input.get_axis("brake", "accel")
	var steer_in := Input.get_axis("right", "left")
	if test_mode:
		throttle = 1.0
		test_time += delta
	if dialog_open:
		throttle = 0.0
		steer_in = 0.0
	var speed := car.linear_velocity.length()
	# engine_force gilt pro Antriebsrad (4 Raeder); Hoechstgeschwindigkeit ca. 150 km/h
	car.engine_force = throttle * 2000.0 if speed < 42.0 or throttle < 0.0 else 0.0
	car.brake = 60.0 if dialog_open else (45.0 if Input.is_action_pressed("handbrake") else 0.0)
	var max_steer := 0.55 * (1.0 - clampf(speed / 60.0, 0.0, 0.7))
	car.steering = lerpf(car.steering, steer_in * max_steer, 0.2)
	if Input.is_action_just_pressed("reset"):
		car.global_position.y += 1.5
		car.rotation = Vector3(0, car.rotation.y, 0)
		car.linear_velocity = Vector3.ZERO
		car.angular_velocity = Vector3.ZERO
	if Input.is_action_just_pressed("save"):
		save_game()
	if Input.is_action_just_pressed("load"):
		load_game()
	if not dialog_open:
		_update_agents(delta)
		_update_game(delta)
	if test_mode and test_time > 3.0:
		var fwd := car.global_transform.basis.z
		var m0 := money
		money = 4242
		heat = 2.5
		var ok1 := save_game()
		money = 1
		heat = 0.0
		var ok2 := load_game()
		print("SAVETEST save=", ok1, " load=", ok2, " money=", money, " heat=", heat, " (erwartet 4242 / 2.5)")
		money = m0
		var steps := 0
		var enter := InputEventKey.new()
		enter.pressed = true
		enter.keycode = KEY_ENTER
		var one := InputEventKey.new()
		one.pressed = true
		one.keycode = KEY_1
		while not current_mission.is_empty() and steps < 30:
			steps += 1
			_complete_story_mission()
			_unhandled_input(enter)
			var guard := 0
			while dialog_open and dialog_stage == "choice" and guard < 10:
				guard += 1
				_unhandled_input(one)
		if dialog_open:
			_unhandled_input(enter)
		print("STORYRUN steps=", steps, " completed=", story.state["completed"].size(), " ending=", story.state["flags"].get("ending"), " dialog_open=", dialog_open)
		_spawn_police()
		print("POLICE=", police.size(), " agents=", agents.size())
		print("TEST pos=", car.global_position, " vel=", car.linear_velocity, " fwd=", fwd, " dot=", car.linear_velocity.normalized().dot(fwd))
		get_tree().quit()

func _process(delta: float) -> void:
	hour = fmod(hour + delta / 20.0, 24.0)
	var a := (hour - 6.0) / 24.0 * TAU
	var elev := sin(a)
	var day := clampf((elev + 0.15) / 0.45, 0.0, 1.0)
	var night := 1.0 - day
	sun.rotation = Vector3(-a, deg_to_rad(35), 0)
	sun.light_energy = maxf(0.0, elev) * 1.4
	sun.visible = elev > 0.0
	var dusk := clampf(1.0 - absf(elev - 0.07) / 0.22, 0.0, 1.0) * 0.7
	var top := Color(0.07, 0.1, 0.25).lerp(Color(0.25, 0.5, 0.85), day)
	var horizon := Color(0.04, 0.05, 0.12).lerp(Color(0.65, 0.78, 0.9), day).lerp(Color(1.0, 0.55, 0.3), dusk)
	sky_mat.sky_top_color = top
	sky_mat.sky_horizon_color = horizon
	sky_mat.ground_horizon_color = horizon
	sky_mat.ground_bottom_color = horizon.darkened(0.4)
	env.ambient_light_energy = lerpf(0.25, 1.0, day)
	env.fog_light_color = horizon
	bld_mat.set_shader_parameter("night", night)
	headlight.light_energy = night * 6.0
	lamp_timer -= delta
	if lamp_timer <= 0.0:
		lamp_timer = 0.25
		_update_lamp_lights(night)
	if fps_mode:
		fps_time += delta
		if fps_time > 6.0:
			print("FPS=", Engine.get_frames_per_second())
			get_tree().quit()
	# Kamera folgt dem Auto (Front = -Z)
	var fwd := car.global_transform.basis.z
	fwd.y = 0.0
	fwd = fwd.normalized()
	var target := car.global_position - fwd * 11.0 + Vector3.UP * 4.6
	cam.global_position = cam.global_position.lerp(target, 1.0 - exp(-delta * 6.0))
	cam.look_at(car.global_position + fwd * 5.0 + Vector3.UP * 1.5, Vector3.UP)
	var hh := int(hour)
	var mm := int((hour - hh) * 60.0)
	var stars := int(ceil(heat - 0.001))
	var dist_m := int(Vector2(mission_pos.x - car.global_position.x, mission_pos.z - car.global_position.z).length())
	var title := "Hauptstory: " + str(current_mission["title"]) if not current_mission.is_empty() else "Kurierfahrt"
	var time_txt := "" if not current_mission.is_empty() else ("%d s" % int(mission_time) if mission_time > 0.0 else "ohne Bonus")
	var st: Dictionary = story.state["standing"]
	hud.text = "%d km/h    %02d:%02d\n$ %d\nFahndung: %s%s\nAuftrag: %s\nZiel: %d m   %s\nUnion %d  Aurelia %d  Collective %d  Wrenchers %d" % [
		int(car.linear_velocity.length() * 3.6), hh, mm, money,
		"*".repeat(stars), "-".repeat(5 - stars), title, dist_m, time_txt,
		int(st["union"]), int(st["aurelia"]), int(st["collective"]), int(st["wrenchers"])]
