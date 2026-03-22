extends Node3D

@export var move_speed := 25.0
@export var boost_mult := 4.0
@export var mouse_sens := 0.0025
@export var accel := 10.0
@export var zoom_speed := 5.0
@export var zoom_accel := 12.0
@export var pan_speed := 0.15

var vel: Vector3 = Vector3.ZERO
var zoom_vel: float = 0.0
var yaw := 0.0
var pitch := 0.0
var frozen: bool = false       # persistent freeze flag — survives panel toggles
var mmb_held: bool = false     # middle-mouse-button held for CAD-style pan

func _ready() -> void:
	Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)


# ─────────────────────────────────────────────────────────
# _input — runs BEFORE UI nodes so scroll-wheel zoom and
#           MMB pan can never be eaten by ScrollContainer.
# ─────────────────────────────────────────────────────────
func _input(e: InputEvent) -> void:
	if e is InputEventMouseButton:
		# Scroll-wheel zoom — always works, frozen or not
		if e.button_index == MOUSE_BUTTON_WHEEL_UP and e.pressed:
			zoom_vel += zoom_speed
			get_viewport().set_input_as_handled()
		elif e.button_index == MOUSE_BUTTON_WHEEL_DOWN and e.pressed:
			zoom_vel -= zoom_speed
			get_viewport().set_input_as_handled()
		# Middle mouse button — track held state for CAD pan
		elif e.button_index == MOUSE_BUTTON_MIDDLE:
			mmb_held = e.pressed
			get_viewport().set_input_as_handled()

	# Middle-mouse drag pan — works in 5all modes (frozen, panels open, etc.)
	if e is InputEventMouseMotion and mmb_held:
		var right := global_transform.basis.x
		var up := global_transform.basis.y
		global_position -= right * e.relative.x * pan_speed
		global_position += up * e.relative.y * pan_speed
		get_viewport().set_input_as_handled()


# ─────────────────────────────────────────────────────────
# _unhandled_input — runs AFTER UI; handles look, freeze,
#                     unfreeze, and shift-pan.
# ─────────────────────────────────────────────────────────
func _unhandled_input(e: InputEvent) -> void:
	# Escape → freeze viewport
	if e.is_action_pressed("ui_cancel"):
		frozen = true
		Input.set_mouse_mode(Input.MOUSE_MODE_VISIBLE)
		return

	# Left click → unfreeze viewport when frozen
	if e is InputEventMouseButton and e.button_index == MOUSE_BUTTON_LEFT and e.pressed:
		if frozen:
			frozen = false
			Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)
			return

	# Mouse look / shift-pan — only when NOT frozen and mouse is captured
	if e is InputEventMouseMotion:
		if not frozen and Input.get_mouse_mode() == Input.MOUSE_MODE_CAPTURED:
			if Input.is_key_pressed(KEY_SHIFT):
				var right := global_transform.basis.x
				var up := global_transform.basis.y
				global_position -= right * e.relative.x * pan_speed
				global_position += up * e.relative.y * pan_speed
			else:
				yaw -= e.relative.x * mouse_sens
				pitch -= e.relative.y * mouse_sens
				pitch = clamp(pitch, -1.55, 1.55)
				rotation = Vector3(pitch, yaw, 0.0)


func _physics_process(dt: float) -> void:
	# WASD movement — only when NOT frozen and mouse is captured
	if not frozen and Input.get_mouse_mode() == Input.MOUSE_MODE_CAPTURED:
		var input_dir := Vector3.ZERO
		if Input.is_key_pressed(KEY_W): input_dir.z -= 1.0
		if Input.is_key_pressed(KEY_S): input_dir.z += 1.0
		if Input.is_key_pressed(KEY_A): input_dir.x -= 1.0
		if Input.is_key_pressed(KEY_D): input_dir.x += 1.0
		if Input.is_key_pressed(KEY_E): input_dir.y += 1.0
		if Input.is_key_pressed(KEY_Q): input_dir.y -= 1.0

		input_dir = input_dir.normalized()

		var spd := move_speed
		if Input.is_key_pressed(KEY_SHIFT):
			spd *= boost_mult

		var wish_vel := (global_transform.basis * input_dir) * spd
		vel = vel.lerp(wish_vel, 1.0 - exp(-accel * dt))
	else:
		# Frozen or panels open: bleed off any residual velocity
		vel = vel.lerp(Vector3.ZERO, 1.0 - exp(-accel * dt))

	# Zoom along forward axis — works in ALL modes
	if absf(zoom_vel) > 0.01:
		global_position += global_transform.basis.z * -zoom_vel
	zoom_vel = lerpf(zoom_vel, 0.0, 1.0 - exp(-zoom_accel * dt))

	global_position += vel * dt
