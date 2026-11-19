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
	fps_mode = "--fps" in OS.get_cmdline_user_args()
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

func _build_traffic() -> void:
	var cols := [Color(0.23, 0.43, 0.88), Color(0.94, 0.94, 0.94), Color(0.13, 0.13, 0.13), Color(0.88, 0.75, 0.19), Color(0.23, 0.66, 0.38), Color(0.53, 0.55, 0.58)]
	for i in 28:
		var a := Agent.new()
		a.axis = rng.randi() % 2
		a.line = rng.randi() % (N + 1)
		a.sign = 1 if rng.randf() < 0.5 else -1
		a.pos = rng.randf_range(-HALF + 5.0, HALF - 5.0)
		a.cruise = rng.randf_range(10.0, 17.0)
		a.v = a.cruise
		_set_off(a)
		a.node = Node3D.new()
		_car_visual(a.node, cols[rng.randi() % cols.size()])
		add_child(a.node)
		var t := _agent_target(a)
		a.node.position = Vector3(t.x, 0.0, t.y)
		a.node.rotation.y = _agent_heading(a)
		agents.append(a)

func _set_off(a: Agent) -> void:
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
	var opts: Array = []
	var straight_ok := not (m == N and a.sign > 0) and not (m == 0 and a.sign < 0)
	if straight_ok:
		opts.append([0, 0.6])
	for s in [1, -1]:
		if (k == 0 and s < 0) or (k == N and s > 0):
			continue
		opts.append([s, 0.2])
	var total := 0.0
	for o in opts:
		total += o[1]
	var r := rng.randf() * total
	var ch: int = opts[opts.size() - 1][0]
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
	_set_off(a)

func _update_agents(dt: float) -> void:
	for a in agents:
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

func _build_hud() -> void:
	var layer := CanvasLayer.new()
	hud = Label.new()
	hud.position = Vector2(20, 14)
	hud.add_theme_font_size_override("font_size", 28)
	layer.add_child(hud)
	var help := Label.new()
	help.text = "W/S Gas/Bremse   A/D Lenken   Leertaste Handbremse   R Zuruecksetzen"
	help.position = Vector2(20, 860)
	layer.add_child(help)
	add_child(layer)

func _physics_process(delta: float) -> void:
	var throttle := Input.get_axis("brake", "accel")
	var steer_in := Input.get_axis("right", "left")
	if test_mode:
		throttle = 1.0
		test_time += delta
	var speed := car.linear_velocity.length()
	# engine_force gilt pro Antriebsrad (4 Raeder); Hoechstgeschwindigkeit ca. 150 km/h
	car.engine_force = throttle * 2000.0 if speed < 42.0 or throttle < 0.0 else 0.0
	car.brake = 45.0 if Input.is_action_pressed("handbrake") else 0.0
	var max_steer := 0.55 * (1.0 - clampf(speed / 60.0, 0.0, 0.7))
	car.steering = lerpf(car.steering, steer_in * max_steer, 0.2)
	if Input.is_action_just_pressed("reset"):
		car.global_position.y += 1.5
		car.rotation = Vector3(0, car.rotation.y, 0)
		car.linear_velocity = Vector3.ZERO
		car.angular_velocity = Vector3.ZERO
	_update_agents(delta)
	if test_mode and test_time > 3.0:
		var fwd := car.global_transform.basis.z
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
	hud.text = "%d km/h    %02d:%02d" % [int(car.linear_velocity.length() * 3.6), hh, mm]
