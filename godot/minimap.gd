extends Control
## Minimap: Auto zeigt immer nach oben, Weltausschnitt dreht sich mit.

var main: Node3D
const R_MAP := 150.0

func _ready() -> void:
	clip_contents = true

func _process(_delta: float) -> void:
	queue_redraw()

func _tf(wx: float, wz: float, cp: Vector3, yaw: float, s: float) -> Vector2:
	var dx := wx - cp.x
	var dz := wz - cp.z
	var fwd := dx * sin(yaw) + dz * cos(yaw)
	var lat := dx * cos(yaw) - dz * sin(yaw)
	var c := size / 2.0
	return Vector2(c.x - lat * s, c.y - fwd * s)

func _draw() -> void:
	if main == null or main.car == null:
		return
	var s: float = size.x / 2.0 / R_MAP
	var cp: Vector3 = main.car.global_position
	var yaw: float = main.car.global_rotation.y
	draw_rect(Rect2(Vector2.ZERO, size), Color(0.09, 0.12, 0.16, 0.92))
	var P: float = main.P
	var HALF: float = main.HALF
	for i in main.N:
		for j in main.N:
			var cx: float = main.rr(i) + P / 2.0
			var cz: float = main.rr(j) + P / 2.0
			if Vector2(cx - cp.x, cz - cp.z).length() > R_MAP + 60.0:
				continue
			var pts := PackedVector2Array()
			for corner in [Vector2(-32, -32), Vector2(32, -32), Vector2(32, 32), Vector2(-32, 32)]:
				pts.append(_tf(cx + corner.x, cz + corner.y, cp, yaw, s))
			draw_colored_polygon(pts, Color(0.2, 0.26, 0.32))
	for a in main.agents:
		if a.kind == "ped":
			continue
		var p: Vector3 = a.node.global_position
		var q := _tf(p.x, p.z, cp, yaw, s)
		if a.kind == "police":
			var blink := int(main.gtime * 4.0) % 2 == 0
			draw_circle(q, 4.0, Color(1, 0.2, 0.2) if blink else Color(0.25, 0.4, 1))
		else:
			draw_rect(Rect2(q - Vector2(1.5, 1.5), Vector2(3, 3)), Color(0.55, 0.6, 0.66))
	var mp: Vector3 = main.mission_pos
	var d := Vector2(mp.x - cp.x, mp.z - cp.z)
	var lim := R_MAP * 0.88
	if d.length() > lim:
		d = d.normalized() * lim
	draw_circle(_tf(cp.x + d.x, cp.z + d.y, cp, yaw, s), 6.0, Color(1, 0.82, 0.2))
	var c := size / 2.0
	draw_colored_polygon(PackedVector2Array([c + Vector2(0, -9), c + Vector2(-6, 7), c + Vector2(6, 7)]), Color.WHITE)
	draw_rect(Rect2(Vector2.ZERO, size), Color(1, 1, 1, 0.8), false, 2.0)
