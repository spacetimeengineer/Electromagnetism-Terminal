## SimHUD.gd — Unified tiling layout manager.
## Owns ALL panels: physics, model, keybinds, controls, shell, log, toast.
## Attach to a single CanvasLayer. Replaces SimControlPanel.gd & SimSHELL.gd.
##
## H = HUD toggle, Tab = control panel, ` = shell, L = particle log

extends CanvasLayer

var sim: Node = null
var ship_cam: Node = null   # ShipController ref — checked for .frozen

# ── Panel nodes ──
var physics_panel: PanelContainer;  var physics_label: RichTextLabel
var model_panel: PanelContainer;    var model_label: RichTextLabel
var keys_panel: PanelContainer;     var keys_label: RichTextLabel
var control_panel: PanelContainer;  var control_scroll: ScrollContainer; var control_vbox: VBoxContainer
var shell_panel: PanelContainer;    var shell_vbox: VBoxContainer
var shell_output: RichTextLabel;    var shell_input: LineEdit
var log_panel: PanelContainer;      var log_label: RichTextLabel
var toast_label: Label

# ── Visibility state ──
var hud_visible: bool = true
var control_visible: bool = false
var shell_open: bool = false
var log_visible: bool = false

# ── Key state ──
# (toggles now handled in _input, no polling needed)

# ── Refresh timer ──
var refresh_timer: float = 0.0
const REFRESH_INTERVAL := 0.1

# ── Slider refs ──
var sliders: Dictionary = {}

# ── Shell state ──
var shell_history: PackedStringArray = []
var shell_history_idx: int = -1
var shell_output_lines: PackedStringArray = []
var schedule_queue: Array[Dictionary] = []
const MAX_SHELL_HISTORY := 64
const MAX_OUTPUT_LINES := 200

# ── Toast state ──
var toast_queue: Array[String] = []
var toast_timer: float = 0.0
const TOAST_DURATION := 2.5

# ── Layout constants ──
const PAD := 10
const GAP := 6
const PHYS_W := 310
const MODEL_W := 350
const KEYS_W := 220
const CTRL_W := 360
const BOT_H := 250
const LOG_PARTICLES := 16

# ── Physics ──
const TOPO_NAMES: PackedStringArray = ["TORUS T\u00b3", "SPHERE S\u00b2", "SPH-WRAP", "OPEN R\u00b3"]
const TOPO_COLORS: PackedStringArray = ["#88aaff", "#88ffaa", "#cc88ff", "#ffcc44"]
const TOPO_DESC: PackedStringArray = [
	"Periodic BCs, flat 3-torus",
	"Hard sphere, reflective wall",
	"Spherical wrap (antipodal ID)",
	"Unbounded, no walls"
]

var DEFAULTS := {
	"k_coulomb": 100000.0, "k_biot": 25.0, "k_dipole": 5000.0,
	"k_lorentz": 1.0, "k_mag_coulomb": 100000.0, "max_speed": 600.0,
	"global_damp": 0.999, "soften_eps2": 8.0, "fixed_dt": 0.016,
	"k_dual_lorentz": 1.0, "k_dual_biot": 25.0,
}


# ═════════════════════════════════════════════════════════
# Lifecycle
# ═════════════════════════════════════════════════════════

func _ready() -> void:
	RenderingServer.set_default_clear_color(Color(0.02, 0.02, 0.03))
	sim = _find_sim(get_tree().root)
	ship_cam = _find_ship(get_tree().root)
	_build_all()
	for sig in ["topology_changed","simulation_reset","particle_spawned",
				 "monopole_spawned","magnet_spawned","ring_spawned","binding_event"]:
		if sim and sim.has_signal(sig):
			sim.connect(sig, Callable(self, "_on_" + sig))
	call_deferred("_tile_panels")


func _process(delta: float) -> void:
	_process_schedule()

	if not hud_visible:
		_update_toast(delta); return

	refresh_timer += delta
	if refresh_timer >= REFRESH_INTERVAL:
		refresh_timer = 0.0
		_refresh_physics()
		_refresh_model()
		if log_visible: _refresh_log()
		_tile_panels()
	_update_toast(delta)


func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		var changed := false
		# Only block H/L when actively typing in shell input
		var typing := shell_open and shell_input.has_focus()
		match event.keycode:
			KEY_H:
				if not typing:
					hud_visible = not hud_visible; changed = true
			KEY_L:
				if not typing:
					log_visible = not log_visible; changed = true
			KEY_TAB:
				# Tab always works — toggle controls even when shell is open
				control_visible = not control_visible; changed = true
				get_viewport().set_input_as_handled()
			KEY_QUOTELEFT:
				# Backtick always works — toggle shell even when controls are open
				shell_open = not shell_open; changed = true
				if shell_open:
					call_deferred("_focus_shell")
				else:
					shell_input.release_focus()
				get_viewport().set_input_as_handled()
			KEY_UP:
				if shell_open and shell_history.size() > 0:
					shell_history_idx = clampi(shell_history_idx - 1, 0, shell_history.size() - 1)
					shell_input.text = shell_history[shell_history_idx]
					shell_input.caret_column = shell_input.text.length()
					get_viewport().set_input_as_handled()
			KEY_DOWN:
				if shell_open and shell_history.size() > 0:
					shell_history_idx = clampi(shell_history_idx + 1, 0, shell_history.size())
					shell_input.text = shell_history[shell_history_idx] if shell_history_idx < shell_history.size() else ""
					shell_input.caret_column = shell_input.text.length()
					get_viewport().set_input_as_handled()
		if changed:
			_apply_visibility()
			_tile_panels()
			_update_input_mode()

func _focus_shell() -> void:
	shell_input.grab_focus()
	# Clear any backtick character that got typed into the input
	shell_input.text = shell_input.text.replace("`", "")
	shell_input.caret_column = shell_input.text.length()


func _update_input_mode() -> void:
	var interactive := shell_open or control_visible
	if sim and sim.has_method("set_input_enabled"):
		sim.set_input_enabled(not interactive)
	if interactive:
		Input.set_mouse_mode(Input.MOUSE_MODE_VISIBLE)
	elif ship_cam and ship_cam.get("frozen"):
		# Panels closed but camera is frozen — keep cursor visible
		Input.set_mouse_mode(Input.MOUSE_MODE_VISIBLE)
	else:
		Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)


func _apply_visibility() -> void:
	physics_panel.visible = hud_visible
	model_panel.visible = hud_visible
	keys_panel.visible = hud_visible
	control_panel.visible = hud_visible and control_visible
	shell_panel.visible = hud_visible and shell_open
	log_panel.visible = hud_visible and log_visible


# ═════════════════════════════════════════════════════════
# TILING — percentage-based, never reads stale .size.y
# ═════════════════════════════════════════════════════════

func _tile_panels() -> void:
	var vp := get_viewport().get_visible_rect().size
	if vp.x < 200 or vp.y < 200: return

	# ── Zones ──
	# Bottom strip: shell and/or log (fixed height)
	var bot_active := hud_visible and (shell_open or log_visible)
	var bot_h: float = BOT_H if bot_active else 0.0
	var bot_y: float = vp.y - PAD - bot_h

	# Left column splits the space above the bottom zone:
	#   Top 45%: physics + model stacked
	#   Bottom 55%: control panel (when visible)
	var left_top: float = PAD
	var left_bottom: float = bot_y - GAP if bot_active else vp.y - PAD
	var left_total: float = left_bottom - left_top
	var mid_split: float = left_top + left_total * 0.45

	# ── TOP-LEFT: Physics State ──
	physics_panel.position = Vector2(PAD, left_top)
	physics_panel.custom_minimum_size = Vector2(PHYS_W, 0)

	# ── TOP-RIGHT: Keybinds ──
	keys_panel.position = Vector2(vp.x - PAD - KEYS_W, PAD)
	keys_panel.custom_minimum_size = Vector2(KEYS_W, 0)

	# ── MODEL: below midpoint of top zone ──
	# Physics gets top portion, model gets lower portion of the top zone
	var phys_zone_h: float = (mid_split - left_top - GAP) * 0.55
	var model_y: float = left_top + phys_zone_h + GAP
	model_panel.position = Vector2(PAD, model_y)
	model_panel.custom_minimum_size = Vector2(MODEL_W, 0)

	# ── CONTROL PANEL: from mid_split to left_bottom ──
	if control_visible and hud_visible:
		var ctrl_top: float = mid_split + GAP
		var ctrl_h: float = maxf(left_bottom - ctrl_top, 80.0)
		control_panel.position = Vector2(PAD, ctrl_top)
		control_panel.size = Vector2(CTRL_W, ctrl_h)
		control_scroll.custom_minimum_size = Vector2(CTRL_W - 24, ctrl_h - 24)

	# ── LEFT COLUMN width (for bottom offset) ──
	var left_col_w: float = maxf(PHYS_W, MODEL_W)
	if control_visible and hud_visible:
		left_col_w = maxf(left_col_w, CTRL_W)

	# ── BOTTOM ROW ──
	var bot_left: float = PAD
	if control_visible and hud_visible:
		# Push bottom panels right of the left column
		bot_left = PAD + left_col_w + GAP
	var bot_avail: float = maxf(vp.x - bot_left - PAD, 200.0)

	if shell_open and hud_visible and log_visible:
		var half: float = (bot_avail - GAP) * 0.5
		shell_panel.position = Vector2(bot_left, bot_y)
		shell_panel.size = Vector2(half, bot_h)
		log_panel.position = Vector2(bot_left + half + GAP, bot_y)
		log_panel.size = Vector2(half, bot_h)
	elif shell_open and hud_visible:
		shell_panel.position = Vector2(bot_left, bot_y)
		shell_panel.size = Vector2(bot_avail, bot_h)
	elif log_visible and hud_visible:
		log_panel.position = Vector2(bot_left, bot_y)
		log_panel.size = Vector2(bot_avail, bot_h)

	# ── TOAST ──
	var toast_y: float = bot_y - 35 if bot_active else vp.y - 50
	toast_label.position = Vector2(vp.x * 0.5 - 200, toast_y)


# ═════════════════════════════════════════════════════════
# UI Construction
# ═════════════════════════════════════════════════════════

func _build_all() -> void:
	_build_physics_panel()
	_build_model_panel()
	_build_keys_panel()
	_build_control_panel()
	_build_shell_panel()
	_build_log_panel()
	_build_toast()
	_apply_visibility()


func _build_physics_panel() -> void:
	physics_panel = PanelContainer.new()
	physics_panel.add_theme_stylebox_override("panel", _sbox(Color(0.06, 0.06, 0.09, 0.82)))
	add_child(physics_panel)
	physics_label = _rtl(PHYS_W, 12, Color(0.82, 0.82, 0.82))
	physics_panel.add_child(physics_label)


func _build_model_panel() -> void:
	model_panel = PanelContainer.new()
	model_panel.add_theme_stylebox_override("panel", _sbox(Color(0.05, 0.05, 0.08, 0.78)))
	add_child(model_panel)
	model_label = _rtl(MODEL_W, 11, Color(0.72, 0.72, 0.75))
	model_panel.add_child(model_label)


func _build_keys_panel() -> void:
	keys_panel = PanelContainer.new()
	keys_panel.add_theme_stylebox_override("panel", _sbox(Color(0.06, 0.06, 0.08, 0.72)))
	add_child(keys_panel)
	keys_label = _rtl(KEYS_W, 11, Color(0.65, 0.65, 0.68))
	keys_panel.add_child(keys_label)
	_populate_keys()


func _build_control_panel() -> void:
	control_panel = PanelContainer.new()
	control_panel.add_theme_stylebox_override("panel", _sbox(Color(0.05, 0.055, 0.07, 0.88)))
	control_panel.visible = false
	add_child(control_panel)

	control_scroll = ScrollContainer.new()
	control_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	control_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	control_panel.add_child(control_scroll)

	control_vbox = VBoxContainer.new()
	control_vbox.add_theme_constant_override("separation", 1)
	control_vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	control_scroll.add_child(control_vbox)

	_ct("[Tab] PARAMETER CONTROL")
	_cs(4)

	_cx("ELECTROSTATICS",
		"F = ke \u00b7 q\u2081q\u2082 / (r\u00b2 + \u03b5\u00b2) \u00b7 r\u0302",
		"Coulomb force with softened singularity")
	_csl("k_coulomb", "ke  (force const)", "m\u00b7u\u00b3/s\u00b2\u00b7q\u00b2", 0, 500000, 100000, 1000)
	_csl("soften_eps2", "\u03b5\u00b2  (softening)", "u\u00b2", 0.01, 64.0, 8.0, 0.5)

	_cx("MAGNETOSTATICS",
		"dB = kb \u00b7 q(v \u00d7 r\u0302) / r\u00b2",
		"Moving charges \u2192 B via Biot-Savart law")
	_csl("k_biot", "kb  (Biot-Savart)", "T\u00b7u\u00b2\u00b7s/q", 0, 200, 25, 1)
	_csl("k_dipole", "kd  (dipole)", "T\u00b7u\u2075/A\u00b7m\u00b2", 0, 50000, 5000, 100)
	_csl("k_mag_coulomb", "km  (monopole)", "T\u00b7u\u00b2/g", 0, 500000, 100000, 1000)

	_cx("FORCE COUPLING",
		"F = q(E + kL v\u00d7B)   g(B \u2212 kDL v\u00d7E)",
		"Lorentz & dual Lorentz cross-coupling")
	_csl("k_lorentz", "kL  (Lorentz)", "dimensionless", 0, 10, 1.0, 0.1)
	_csl("k_dual_lorentz", "kDL (dual Lorentz)", "dimensionless", 0, 10, 1.0, 0.1)
	_csl("k_dual_biot", "kDB (dual Biot)", "T\u00b7u\u00b2\u00b7s/g", 0, 200, 25, 1)

	_cx("INTEGRATION",
		"Velocity-Verlet:  x(t+dt) = x + v\u00b7dt + \u00bda\u00b7dt\u00b2",
		"Semi-implicit, global damping v *= damp each step")
	_csl("max_speed", "vmax", "u/s", 10, 3000, 600, 10)
	_csl("global_damp", "damping", "per step", 0.9, 1.0, 0.999, 0.001)
	_csl("fixed_dt", "dt", "s", 0.001, 0.05, 0.016, 0.001)

	_cx("SPAWN",
		"Placed at camera, v\u2080 = 0",
		"q = \u00b11 (charge)   g = \u00b11 (mag. charge)")
	_ctrl_spawn_row()

	_cx("FIELD VISUALIZATION",
		"Seeds at sources, traced along field",
		"Density \u223c magnitude, arrows \u223c local |F|")
	_ctrl_field_indicators()

	_cs(6)
	_cx("ACTIONS", "", "")
	_ctrl_action_buttons()
	_cs(4)
	var hl := Label.new(); hl.text = "Scroll for more \u2022 [Tab] close"
	hl.add_theme_font_size_override("font_size", 10)
	hl.add_theme_color_override("font_color", Color(0.42, 0.42, 0.48))
	hl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	control_vbox.add_child(hl)


func _build_shell_panel() -> void:
	shell_panel = PanelContainer.new()
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.04, 0.05, 0.04, 0.9)
	sb.border_color = Color(0.25, 0.35, 0.25, 0.5)
	sb.set_border_width_all(1); sb.set_corner_radius_all(4); sb.set_content_margin_all(8)
	shell_panel.add_theme_stylebox_override("panel", sb)
	shell_panel.visible = false
	add_child(shell_panel)

	shell_vbox = VBoxContainer.new()
	shell_vbox.add_theme_constant_override("separation", 4)
	shell_vbox.size_flags_vertical = Control.SIZE_EXPAND_FILL
	shell_vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	shell_panel.add_child(shell_vbox)

	var hdr := Label.new()
	hdr.text = "COMMAND SHELL  [`]"
	hdr.add_theme_font_size_override("font_size", 11)
	hdr.add_theme_color_override("font_color", Color(0.5, 0.65, 0.5))
	shell_vbox.add_child(hdr)

	shell_output = RichTextLabel.new()
	shell_output.bbcode_enabled = true; shell_output.scroll_active = true
	shell_output.scroll_following = true; shell_output.fit_content = false
	shell_output.size_flags_vertical = Control.SIZE_EXPAND_FILL
	shell_output.add_theme_font_size_override("normal_font_size", 12)
	shell_output.add_theme_font_size_override("bold_font_size", 12)
	shell_output.add_theme_color_override("default_color", Color(0.75, 0.8, 0.75))
	shell_vbox.add_child(shell_output)

	shell_input = LineEdit.new()
	shell_input.placeholder_text = "type a command... (help for list)"
	shell_input.add_theme_font_size_override("font_size", 12)
	shell_input.add_theme_color_override("font_color", Color(0.9, 0.95, 0.9))
	shell_input.add_theme_color_override("font_placeholder_color", Color(0.4, 0.4, 0.4))
	var isb := StyleBoxFlat.new()
	isb.bg_color = Color(0.08, 0.10, 0.08, 0.9)
	isb.border_color = Color(0.3, 0.4, 0.3, 0.5)
	isb.set_border_width_all(1); isb.set_corner_radius_all(3); isb.set_content_margin_all(6)
	shell_input.add_theme_stylebox_override("normal", isb)
	shell_input.add_theme_stylebox_override("focus", isb)
	shell_input.text_submitted.connect(_on_shell_submitted)
	shell_vbox.add_child(shell_input)

	_shell_print("[color=#66aa66]Shell ready. Type [b]help[/b] for commands.[/color]")


func _build_log_panel() -> void:
	log_panel = PanelContainer.new()
	log_panel.add_theme_stylebox_override("panel", _sbox(Color(0.05, 0.05, 0.07, 0.82)))
	log_panel.visible = false
	add_child(log_panel)

	var vb := VBoxContainer.new()
	vb.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vb.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	log_panel.add_child(vb)

	var hdr := Label.new()
	hdr.text = "PARTICLE LOG  [L]"
	hdr.add_theme_font_size_override("font_size", 11)
	hdr.add_theme_color_override("font_color", Color(0.5, 0.55, 0.7))
	vb.add_child(hdr)

	log_label = RichTextLabel.new()
	log_label.bbcode_enabled = true; log_label.fit_content = false
	log_label.scroll_active = true; log_label.scroll_following = false
	log_label.size_flags_vertical = Control.SIZE_EXPAND_FILL
	log_label.add_theme_font_size_override("normal_font_size", 11)
	log_label.add_theme_font_size_override("bold_font_size", 11)
	log_label.add_theme_color_override("default_color", Color(0.68, 0.68, 0.68))
	vb.add_child(log_label)


func _build_toast() -> void:
	toast_label = Label.new()
	toast_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	toast_label.custom_minimum_size = Vector2(400, 0)
	toast_label.add_theme_font_size_override("font_size", 13)
	toast_label.add_theme_color_override("font_color", Color(1, 1, 1, 0.9))
	toast_label.visible = false
	add_child(toast_label)


# ═════════════════════════════════════════════════════════
# Physics State Panel
# ═════════════════════════════════════════════════════════

func _refresh_physics() -> void:
	if not sim:
		physics_label.text = "[color=#ff6666]No simulator[/color]"; return
	var _g := func(m: String): return sim.call(m) if sim.has_method(m) else 0
	var paused: bool = _g.call("get_paused")
	var topo: int = _g.call("get_topology")
	var n_pos: int = _g.call("get_positive_count")
	var n_neg: int = _g.call("get_negative_count")
	var n_neu: int = _g.call("get_neutral_count")
	var n_N: int = _g.call("get_north_count")
	var n_S: int = _g.call("get_south_count")
	var total: int = _g.call("get_particle_count")
	var n_mag: int = _g.call("get_magnet_count")
	var n_lck: int = _g.call("get_locked_count")
	var n_ring: int = _g.call("get_ring_count")
	var avg_spd: float = _g.call("get_avg_speed")
	var ke: float = _g.call("get_total_kinetic_energy")
	var sim_t: float = _g.call("get_sim_time")
	var grid_on: bool = _g.call("get_show_grid")
	var sc := "#66ff66" if not paused else "#ffaa44"
	var st := "RUNNING" if not paused else "PAUSED"
	var ti := clampi(topo, 0, 3)

	var L := PackedStringArray()
	L.append("[b]SIMULATION STATE[/b]")
	L.append("[color=%s]%s[/color]   t = [color=#aaaacc]%.3f s[/color]" % [sc, st, sim_t])
	L.append("")
	L.append("[color=#777799]TOPOLOGY[/color]  [color=%s]%s[/color]" % [TOPO_COLORS[ti], TOPO_NAMES[ti]])
	L.append("  [color=#555566]%s[/color]%s" % [TOPO_DESC[ti], "  GRID" if grid_on else ""])
	L.append("")
	L.append("[color=#777799]PARTICLES[/color]  N = %d" % total)
	L.append("  [color=#ff6b6b]q+=%d[/color]  [color=#5b9bd5]q-=%d[/color]  [color=#999999]n=%d[/color]" % [n_pos, n_neg, n_neu])
	if n_N > 0 or n_S > 0:
		L.append("  [color=#ffa626]gN=%d[/color]  [color=#b34de6]gS=%d[/color]" % [n_N, n_S])
	if n_mag > 0:
		L.append("  [color=#88aacc]magnets=%d[/color]" % n_mag)
	if n_lck > 0:
		L.append("  [color=#ccaa66]locked=%d[/color]" % n_lck)
	if n_ring > 0:
		L.append("  [color=#55cccc]rings=%d[/color]" % n_ring)
	L.append("  [color=#555566]net Q=%+d  net g=%+d[/color]" % [n_pos - n_neg, n_N - n_S])
	L.append("")
	L.append("[color=#777799]MEASUREMENTS[/color]")
	L.append("  [color=#ccddaa]%.1f[/color] u/s avg   [color=#ccddaa]%.1f[/color] mu\u00b2/s\u00b2 KE" % [avg_spd, ke])
	L.append("")
	var fp := PackedStringArray()
	if _g.call("get_show_e_field"): fp.append("[color=#fff]E[/color](%d)" % _g.call("get_e_lines_per_charge"))
	if _g.call("get_show_b_field"): fp.append("[color=#59bfff]B[/color](%d)" % _g.call("get_b_lines_per_magnet"))
	if _g.call("get_show_a_field"): fp.append("[color=#8cff4d]A[/color](%d)" % _g.call("get_a_lines_per_source"))
	if _g.call("get_show_c_field"): fp.append("[color=#ff8cd9]C[/color](%d)" % _g.call("get_c_lines_per_source"))
	if _g.call("get_show_s_field"): fp.append("[color=#ffcc33]S[/color](%d)" % _g.call("get_s_lines_per_source"))
	L.append("[color=#777799]FIELDS[/color]  %s" % ("  ".join(fp) if fp.size() > 0 else "[color=#555]none[/color]"))
	L.append("")
	L.append("[color=#777799]CONSTANTS[/color]")
	var kc: float = _g.call("get_k_coulomb") if sim.has_method("get_k_coulomb") else 0
	var kb: float = _g.call("get_k_biot") if sim.has_method("get_k_biot") else 0
	var kl: float = _g.call("get_k_lorentz") if sim.has_method("get_k_lorentz") else 0
	var ms: float = _g.call("get_max_speed") if sim.has_method("get_max_speed") else 0
	var dt: float = _g.call("get_fixed_dt") if sim.has_method("get_fixed_dt") else 0
	L.append("  ke=[color=#aab4cc]%.0f[/color]  kb=[color=#aab4cc]%.0f[/color]  kL=[color=#aab4cc]%.2f[/color]" % [kc, kb, kl])
	L.append("  vmax=[color=#aab4cc]%.0f[/color] u/s  dt=[color=#aab4cc]%.3f[/color] s" % [ms, dt])
	physics_label.text = "\n".join(L)


# ═════════════════════════════════════════════════════════
# Model / Equations Panel
# ═════════════════════════════════════════════════════════

func _refresh_model() -> void:
	if not sim: model_label.text = ""; return
	var _g := func(m: String): return sim.call(m) if sim.has_method(m) else 0
	var show_e: bool = _g.call("get_show_e_field")
	var show_b: bool = _g.call("get_show_b_field")
	var show_a: bool = _g.call("get_show_a_field")
	var show_c: bool = _g.call("get_show_c_field")
	var show_s: bool = _g.call("get_show_s_field")
	var has_mono: bool = (_g.call("get_north_count") > 0 or _g.call("get_south_count") > 0)

	var L := PackedStringArray()
	L.append("[b]MAXWELL'S EQUATIONS[/b]  [color=#555566](sim subset)[/color]")
	L.append("")
	L.append("[color=#fff]Gauss (E):[/color]  \u2207\u00b7E = \u03c1/\u03b5\u2080")
	L.append("  [color=#666677]E = ke \u00b7 q \u00b7 r\u0302 / (r\u00b2+\u03b5\u00b2)[/color]")
	L.append("")
	if has_mono:
		L.append("[color=#59bfff]Gauss (B):[/color]  \u2207\u00b7B = \u03c1m  [color=#ffa626]MONOPOLES[/color]")
		L.append("  [color=#666677]B = km \u00b7 g \u00b7 r\u0302 / r\u00b2[/color]")
	else:
		L.append("[color=#59bfff]Gauss (B):[/color]  \u2207\u00b7B = 0")
		L.append("  [color=#666677]B solenoidal, no monopoles[/color]")
	L.append("")
	L.append("[color=#59bfff]Biot-Savart:[/color]  dB = kb \u00b7 q(v\u00d7r\u0302)/r\u00b2")
	L.append("[color=#59bfff]Dipole:[/color]  B = kd[3(m\u00b7r)r/r\u2075 \u2212 m/r\u00b3]")
	L.append("")
	L.append("[color=#aaccff]Lorentz:[/color]  F = q(E + kL v\u00d7B)")
	if has_mono:
		L.append("[color=#ffa626]Dual:[/color]    F = g(B \u2212 kDL v\u00d7E)")
	L.append("")
	L.append("[b]FIELD SEMANTICS[/b]")
	if show_e: L.append("[color=#fff]E[/color]  Coulomb field, \u2207\u00d7E=0 (static)")
	if show_b:
		L.append("[color=#59bfff]B[/color]  Biot-Savart + dipole, B \u221d qv")
		L.append("  [color=#666677]Line density scales with |v|[/color]")
	if show_a: L.append("[color=#8cff4d]A[/color]  B = \u2207\u00d7A, Coulomb gauge")
	if show_c: L.append("[color=#ff8cd9]C[/color]  E = \u2212\u2207\u00d7C (dual potential)")
	if show_s: L.append("[color=#ffcc33]S[/color]  Poynting: S = E\u00d7B [W/m\u00b2]")
	L.append("")
	L.append("[b]ASSUMPTIONS[/b]")
	L.append("[color=#666677]\u2022 Non-relativistic (v \u226a c)[/color]")
	L.append("[color=#666677]\u2022 Instantaneous fields, no retardation[/color]")
	L.append("[color=#666677]\u2022 Softened singularity r\u00b2\u2192r\u00b2+\u03b5\u00b2[/color]")
	L.append("[color=#666677]\u2022 No radiation reaction / self-force[/color]")
	L.append("[color=#666677]\u2022 Velocity-Verlet, fixed dt[/color]")
	if has_mono:
		L.append("[color=#666677]\u2022 Extended Maxwell: magnetic charge[/color]")
	model_label.text = "\n".join(L)


# ═════════════════════════════════════════════════════════
# Keybinds Panel
# ═════════════════════════════════════════════════════════

func _populate_keys() -> void:
	var L := PackedStringArray()
	L.append("[b]KEYBINDS[/b]")
	L.append("")
	L.append("[color=#8899bb]CAMERA[/color]")
	L.append("[color=#aaccff]WASD[/color]  translate")
	L.append("[color=#aaccff]Q/E[/color]   up / down")
	L.append("[color=#aaccff]Mouse[/color]  look")
	L.append("[color=#aaccff]Scroll[/color]  zoom")
	L.append("[color=#aaccff]MMB drag[/color]  pan")
	L.append("[color=#aaccff]Shift+Mouse[/color]  pan")
	L.append("[color=#aaccff]Shift[/color]  4x boost")
	L.append("[color=#aaccff]Esc[/color]  freeze view")
	L.append("[color=#aaccff]LClick[/color]  unfreeze")
	L.append("")
	L.append("[color=#8899bb]SIMULATION[/color]")
	L.append("[color=#aaccff]Space[/color] pause  [color=#aaccff]R[/color] reset")
	L.append("[color=#aaccff]C[/color] clear  [color=#aaccff]O[/color] topology")
	L.append("[color=#aaccff]X[/color] grid")
	L.append("")
	L.append("[color=#8899bb]SPAWN[/color]")
	L.append("[color=#aaccff]1[/color][color=#ff6b6b]q+[/color] [color=#aaccff]2[/color][color=#5b9bd5]q-[/color] [color=#aaccff]3[/color][color=#999]n[/color] [color=#aaccff]4[/color][color=#ffa626]gN[/color] [color=#aaccff]5[/color][color=#b34de6]gS[/color]")
	L.append("[color=#aaccff]M[/color]  bar magnet  [color=#aaccff]6[/color][color=#55cccc]ring[/color]")
	L.append("")
	L.append("[color=#8899bb]FIELDS[/color]")
	L.append("[color=#aaccff]F[/color][color=#fff]E[/color] [color=#aaccff]G[/color][color=#59bfff]B[/color] [color=#aaccff]V[/color][color=#8cff4d]A[/color] [color=#aaccff]J[/color][color=#ff8cd9]C[/color] [color=#aaccff]P[/color][color=#ffcc33]S[/color]")
	L.append("[color=#aaccff]T[/color] rebuild  [color=#aaccff]-/=[/color] E")
	L.append("[color=#aaccff][][/color] B  [color=#aaccff];'[/color] Bc  [color=#aaccff],.[/color] A  [color=#aaccff]90[/color] C")
	L.append("")
	L.append("[color=#8899bb]PANELS[/color]")
	L.append("[color=#aaccff]H[/color] HUD  [color=#aaccff]L[/color] log")
	L.append("[color=#aaccff]Tab[/color] controls  [color=#aaccff]`[/color] shell")
	keys_label.text = "\n".join(L)


# ═════════════════════════════════════════════════════════
# Control Panel Builders (short names to keep lines tight)
# ═════════════════════════════════════════════════════════

func _ct(text: String) -> void:
	var lbl := Label.new(); lbl.text = text
	lbl.add_theme_font_size_override("font_size", 12)
	lbl.add_theme_color_override("font_color", Color(0.7, 0.75, 0.9))
	lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	control_vbox.add_child(lbl)

func _cx(title: String, eq: String, desc: String) -> void:
	_cs(6)
	var lbl := Label.new(); lbl.text = title
	lbl.add_theme_font_size_override("font_size", 11)
	lbl.add_theme_color_override("font_color", Color(0.55, 0.7, 0.9))
	control_vbox.add_child(lbl)
	var sep := HSeparator.new()
	sep.add_theme_constant_override("separation", 2)
	var ssb := StyleBoxFlat.new()
	ssb.bg_color = Color(0.3, 0.35, 0.45, 0.3)
	ssb.content_margin_top = 1; ssb.content_margin_bottom = 1
	sep.add_theme_stylebox_override("separator", ssb)
	control_vbox.add_child(sep)
	if eq != "":
		var el := Label.new(); el.text = eq
		el.add_theme_font_size_override("font_size", 11)
		el.add_theme_color_override("font_color", Color(0.6, 0.65, 0.5))
		el.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		control_vbox.add_child(el)
	if desc != "":
		var dl := Label.new(); dl.text = desc
		dl.add_theme_font_size_override("font_size", 10)
		dl.add_theme_color_override("font_color", Color(0.45, 0.48, 0.55))
		dl.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		control_vbox.add_child(dl)

func _csl(param: String, label_text: String, unit: String,
		min_val: float, max_val: float, default_val: float, step_val: float) -> void:
	var hbox := HBoxContainer.new()
	hbox.add_theme_constant_override("separation", 4)
	control_vbox.add_child(hbox)
	var lbl := Label.new(); lbl.text = label_text
	lbl.custom_minimum_size = Vector2(120, 0)
	lbl.add_theme_font_size_override("font_size", 11)
	lbl.add_theme_color_override("font_color", Color(0.78, 0.78, 0.78))
	hbox.add_child(lbl)
	var ulbl := Label.new(); ulbl.text = "[%s]" % unit
	ulbl.add_theme_font_size_override("font_size", 9)
	ulbl.add_theme_color_override("font_color", Color(0.42, 0.46, 0.52))
	ulbl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hbox.add_child(ulbl)
	var vlbl := Label.new()
	vlbl.custom_minimum_size = Vector2(60, 0)
	vlbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	vlbl.add_theme_font_size_override("font_size", 11)
	vlbl.add_theme_color_override("font_color", Color(0.9, 0.88, 0.6))
	vlbl.text = _fmt(default_val)
	hbox.add_child(vlbl)
	var sl := HSlider.new()
	sl.min_value = min_val; sl.max_value = max_val; sl.step = step_val
	sl.value = default_val
	sl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	sl.custom_minimum_size = Vector2(0, 16)
	control_vbox.add_child(sl)
	var getter := "get_" + param
	if sim and sim.has_method(getter):
		var cur = sim.call(getter); sl.value = cur; vlbl.text = _fmt(cur)
	var setter := "set_" + param
	sl.value_changed.connect(func(v: float):
		vlbl.text = _fmt(v)
		if sim and sim.has_method(setter): sim.call(setter, v)
	)
	sliders[param] = sl

func _ctrl_spawn_row() -> void:
	var grid := GridContainer.new(); grid.columns = 5
	grid.add_theme_constant_override("h_separation", 4)
	grid.add_theme_constant_override("v_separation", 4)
	control_vbox.add_child(grid)
	for t in [["q+", Color(1, 0.42, 0.42), "charge", 1],
			  ["q-", Color(0.36, 0.61, 0.84), "charge", -1],
			  ["n",  Color(0.7, 0.7, 0.7), "neutral", 0],
			  ["gN", Color(1, 0.65, 0.15), "monopole", 1],
			  ["gS", Color(0.7, 0.3, 0.9), "monopole", -1]]:
		var btn := Button.new(); btn.text = t[0]
		btn.custom_minimum_size = Vector2(55, 24)
		btn.add_theme_font_size_override("font_size", 12)
		btn.add_theme_color_override("font_color", t[1])
		btn.add_theme_stylebox_override("normal", _btn(t[1].darkened(0.72)))
		btn.add_theme_stylebox_override("hover", _btn(t[1].darkened(0.5)))
		var tn: String = t[2]; var tv: int = t[3]
		btn.pressed.connect(_spawn_at_cam.bind(tn, tv))
		grid.add_child(btn)
	_cs(2)
	var mb := Button.new(); mb.text = "Spawn Bar Magnet"
	mb.custom_minimum_size = Vector2(0, 24)
	mb.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	mb.add_theme_font_size_override("font_size", 10)
	mb.add_theme_stylebox_override("normal", _btn(Color(0.2, 0.22, 0.3)))
	mb.add_theme_stylebox_override("hover", _btn(Color(0.3, 0.32, 0.4)))
	mb.pressed.connect(_spawn_magnet_cam)
	control_vbox.add_child(mb)

func _ctrl_field_indicators() -> void:
	for f in [["E", "Electric  E=ke\u00b7q\u00b7r\u0302/r\u00b2", "get_show_e_field", Color(1, 1, 1)],
			  ["B", "Magnetic  B\u221dq(v\u00d7r\u0302)/r\u00b2", "get_show_b_field", Color(0.35, 0.75, 1)],
			  ["A", "Vec pot  B=\u2207\u00d7A", "get_show_a_field", Color(0.55, 1, 0.3)],
			  ["C", "Dual pot  E=\u2212\u2207\u00d7C", "get_show_c_field", Color(1, 0.55, 0.85)],
			  ["S", "Poynting  S=E\u00d7B", "get_show_s_field", Color(1, 0.75, 0.2)]]:
		var hb := HBoxContainer.new()
		hb.add_theme_constant_override("separation", 6)
		control_vbox.add_child(hb)
		var s := Label.new(); s.text = f[0]; s.custom_minimum_size = Vector2(14, 0)
		s.add_theme_font_size_override("font_size", 12)
		s.add_theme_color_override("font_color", f[3])
		hb.add_child(s)
		var ind := ColorRect.new(); ind.custom_minimum_size = Vector2(8, 8)
		ind.color = f[3].darkened(0.65)
		ind.set_meta("fg", f[2]); ind.set_meta("oc", f[3])
		hb.add_child(ind)
		var d := Label.new(); d.text = f[1]
		d.add_theme_font_size_override("font_size", 10)
		d.add_theme_color_override("font_color", Color(0.55, 0.55, 0.58))
		d.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		hb.add_child(d)
	var tmr := Timer.new(); tmr.wait_time = 0.3; tmr.autostart = true
	tmr.timeout.connect(func():
		if not sim: return
		for ch in control_vbox.get_children():
			if ch is HBoxContainer:
				for sub in ch.get_children():
					if sub is ColorRect and sub.has_meta("fg"):
						var g: String = sub.get_meta("fg")
						if sim.has_method(g):
							var on: bool = sim.call(g)
							var c: Color = sub.get_meta("oc")
							sub.color = c if on else c.darkened(0.65)
	)
	add_child(tmr)

func _ctrl_action_buttons() -> void:
	var hbox := HBoxContainer.new()
	hbox.add_theme_constant_override("separation", 6)
	control_vbox.add_child(hbox)
	for a in [["Reset", Color(0.9, 0.4, 0.4), "reset_world"],
			  ["Clear", Color(0.8, 0.7, 0.3), "clear_neutrals"]]:
		var b := Button.new(); b.text = a[0]
		b.custom_minimum_size = Vector2(0, 24)
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		b.add_theme_font_size_override("font_size", 11)
		b.add_theme_color_override("font_color", a[1])
		b.add_theme_stylebox_override("normal", _btn(a[1].darkened(0.75)))
		b.add_theme_stylebox_override("hover", _btn(a[1].darkened(0.55)))
		var m: String = a[2]
		b.pressed.connect(func(): if sim and sim.has_method(m): sim.call(m))
		hbox.add_child(b)
	_cs(3)
	var db := Button.new(); db.text = "Reset All to Defaults"
	db.custom_minimum_size = Vector2(0, 24)
	db.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	db.add_theme_font_size_override("font_size", 10)
	db.add_theme_color_override("font_color", Color(0.5, 0.5, 0.6))
	db.add_theme_stylebox_override("normal", _btn(Color(0.12, 0.12, 0.18)))
	db.add_theme_stylebox_override("hover", _btn(Color(0.22, 0.22, 0.28)))
	db.pressed.connect(_reset_defaults)
	control_vbox.add_child(db)

func _cs(h: int) -> void:
	var sp := Control.new(); sp.custom_minimum_size = Vector2(0, h)
	control_vbox.add_child(sp)


# ═════════════════════════════════════════════════════════
# Particle Log
# ═════════════════════════════════════════════════════════

func _refresh_log() -> void:
	if not sim or not sim.has_method("get_particle_data"):
		log_label.text = "no data"; return
	var data: Array = sim.get_particle_data()
	var count := mini(data.size(), LOG_PARTICLES)
	if count == 0: log_label.text = "[i]no particles[/i]"; return
	var L := PackedStringArray()
	L.append("[b]%d / %d PARTICLES[/b]" % [count, data.size()])
	L.append("[color=#555566]  #  type  pos [u]               vel [u/s]          |v|    m     r[/color]")
	for i in range(count):
		var d: Dictionary = data[i]
		var pos: Vector3 = d["pos"]; var vel: Vector3 = d["vel"]
		var spd: float = d["speed"]; var ch: int = d["charge"]
		var mc: int = d.get("mag_charge", 0)
		var mass: float = d.get("mass", 1.0); var rd: float = d.get("radius", 6.0)
		var ts := " n "; var tc := "#777"
		if mc > 0: ts = "gN "; tc = "#ffa626"
		elif mc < 0: ts = "gS "; tc = "#b34de6"
		elif ch > 0: ts = "q+ "; tc = "#ff6b6b"
		elif ch < 0: ts = "q- "; tc = "#5b9bd5"
		L.append("[color=%s]%3d %s[/color](%6.0f %6.0f %6.0f) (%5.0f %5.0f %5.0f) %5.0f %5.1f %4.1f" % [
			tc, i, ts, pos.x, pos.y, pos.z, vel.x, vel.y, vel.z, spd, mass, rd])
	if data.size() > count:
		L.append("[color=#555566]... %d more[/color]" % (data.size() - count))
	log_label.text = "\n".join(L)


# ═════════════════════════════════════════════════════════
# Shell
# ═════════════════════════════════════════════════════════

func _process_schedule() -> void:
	if not sim or not sim.has_method("get_sim_time") or schedule_queue.is_empty(): return
	var t: float = sim.get_sim_time()
	var fired := []
	for i in range(schedule_queue.size()):
		if schedule_queue[i]["time"] <= t: fired.append(i)
	for i in range(fired.size() - 1, -1, -1):
		_shell_exec(schedule_queue[fired[i]]["cmd"], true)
		schedule_queue.remove_at(fired[i])

func _on_shell_submitted(text: String) -> void:
	var cmd := text.strip_edges(); shell_input.clear()
	if cmd.is_empty(): return
	if shell_history.is_empty() or shell_history[shell_history.size() - 1] != cmd:
		shell_history.append(cmd)
		if shell_history.size() > MAX_SHELL_HISTORY:
			shell_history = shell_history.slice(shell_history.size() - MAX_SHELL_HISTORY)
	shell_history_idx = shell_history.size()
	_shell_print("[color=#999]> %s[/color]" % cmd)
	_shell_exec(cmd, false)

func _shell_print(line: String) -> void:
	shell_output_lines.append(line)
	if shell_output_lines.size() > MAX_OUTPUT_LINES:
		shell_output_lines = shell_output_lines.slice(shell_output_lines.size() - MAX_OUTPUT_LINES)
	shell_output.text = "\n".join(shell_output_lines)

func _shell_ok(msg: String) -> void: _shell_print("[color=#88cc88]%s[/color]" % msg)
func _shell_err(msg: String) -> void: _shell_print("[color=#ff6666]error: %s[/color]" % msg)

func _shell_exec(cmd: String, _from_sched: bool) -> void:
	var stripped := cmd.strip_edges()
	if stripped.is_empty() or stripped.begins_with("#"): return
	var parts := stripped.split(" ", false)
	if parts.is_empty(): return
	match parts[0].to_lower():
		"help":
			for l in ["[b]COMMANDS[/b]", "",
				"[color=#aaccaa]spawn[/color] +|-|o|N|S x,y,z vx,vy,vz [@t]",
				"  [color=#666]Append ! to lock: spawn +! 500,500,500 0,0,0[/color]",
				"[color=#aaccaa]magnet[/color] x,y,z dx,dy,dz strength [@t]",
				"[color=#aaccaa]repeat[/color] N <command>",
				"[color=#aaccaa]lock[/color] <index|all>   [color=#aaccaa]unlock[/color] <index|all>",
				"[color=#aaccaa]ring[/color] +|-|I|N|S x,y,z major minor [str] [ax,ay,az] [!]",
				"[color=#aaccaa]lockring[/color] <index|all>   [color=#aaccaa]unlockring[/color] <index|all>",
				"[color=#aaccaa]info[/color] <particle|ring> <index>",
				"[color=#aaccaa]list[/color] [particles|rings|magnets]",
				"[color=#aaccaa]delete[/color] <particle|ring|magnet> <index>",
				"[color=#aaccaa]echo[/color] <text>",
				"[color=#aaccaa]exec[/color] <filepath.sim>",
				"", "[color=#aaccaa]time[/color] [color=#aaccaa]queue[/color] [color=#aaccaa]flush[/color] [color=#aaccaa]reset[/color] [color=#aaccaa]clear[/color] [color=#aaccaa]cls[/color]",
				"", "[color=#666]Scripts: # comments, wait <sec>, exec <file>[/color]",
				"[color=#666]Vectors: x,y,z (no spaces). @t = sim time.[/color]",
				"[color=#666]Example: spawn + 500,500,500 100,0,0 @2.0[/color]"]:
				_shell_print(l)
		"time": _shell_ok("sim time: %.4f s" % _stime())
		"pause", "resume": _shell_ok("Use Space key (t=%.3f)" % _stime())
		"reset":
			if sim and sim.has_method("reset_world"):
				sim.reset_world(); schedule_queue.clear(); _shell_ok("World reset.")
		"clear":
			if sim and sim.has_method("clear_neutrals"):
				sim.clear_neutrals(); _shell_ok("Neutrals cleared.")
		"queue":
			if schedule_queue.is_empty(): _shell_ok("Queue empty.")
			else:
				_shell_print("[b]Scheduled (%d):[/b]" % schedule_queue.size())
				var s := schedule_queue.duplicate()
				s.sort_custom(func(a, b): return a["time"] < b["time"])
				for i in range(mini(s.size(), 30)):
					_shell_print("  @%.3f  %s" % [s[i]["time"], s[i]["cmd"]])
		"flush": schedule_queue.clear(); _shell_ok("Queue cleared.")
		"cls": shell_output_lines.clear(); shell_output.text = ""
		"spawn": _sh_spawn(parts)
		"magnet": _sh_magnet(parts)
		"repeat": _sh_repeat(parts)
		"lock": _sh_lock(parts, true)
		"unlock": _sh_lock(parts, false)
		"ring": _sh_ring(parts)
		"lockring": _sh_lock_ring(parts, true)
		"unlockring": _sh_lock_ring(parts, false)
		"info": _sh_info(parts)
		"list": _sh_list(parts)
		"delete": _sh_delete(parts)
		"echo": _shell_print(cmd.substr(5) if cmd.length() > 5 else "")
		"exec": _sh_exec_file(parts)
		_: _shell_err("Unknown: %s" % parts[0])

func _sh_spawn(parts: PackedStringArray) -> void:
	if parts.size() < 4: _shell_err("spawn <type[!]> <pos> <vel> [@t]"); return
	var pos = _pvec(parts[2]); var vel = _pvec(parts[3])
	if pos == null or vel == null: _shell_err("Bad vector x,y,z"); return
	var raw_type := parts[1]
	var locked := raw_type.ends_with("!")
	var ptype := raw_type.trim_suffix("!")
	var st := _ptime(parts)
	if st >= 0.0:
		var rc := "spawn %s %s %s" % [raw_type, parts[2], parts[3]]
		schedule_queue.append({"time": st, "cmd": rc})
		_shell_print("[color=#aaaacc]Scheduled @%.3f[/color]" % st); return
	if not sim: _shell_err("No simulator."); return
	var lbl := " locked" if locked else ""
	match ptype:
		"+": sim.add_particle(pos, vel, 1, locked); _shell_ok("q+%s at %s" % [lbl, _fv(pos)])
		"-": sim.add_particle(pos, vel, -1, locked); _shell_ok("q-%s at %s" % [lbl, _fv(pos)])
		"o": sim.add_particle(pos, vel, 0, locked); _shell_ok("n%s at %s" % [lbl, _fv(pos)])
		"N": sim.add_monopole(pos, vel, 1, locked); _shell_ok("gN%s at %s" % [lbl, _fv(pos)])
		"S": sim.add_monopole(pos, vel, -1, locked); _shell_ok("gS%s at %s" % [lbl, _fv(pos)])
		_: _shell_err("Type '%s'? Use + - o N S (add ! to lock)" % ptype)

func _sh_magnet(parts: PackedStringArray) -> void:
	if parts.size() < 4: _shell_err("magnet <pos> <dir> <str> [@t]"); return
	var pos = _pvec(parts[1]); var dir = _pvec(parts[2])
	if pos == null or dir == null: _shell_err("Bad vector."); return
	var strength := parts[3].to_float()
	var st := _ptime(parts)
	if st >= 0.0:
		var rc := "magnet %s %s %s" % [parts[1], parts[2], parts[3]]
		schedule_queue.append({"time": st, "cmd": rc})
		_shell_print("[color=#aaaacc]Scheduled @%.3f[/color]" % st); return
	if not sim or not sim.has_method("add_bar_magnet"): _shell_err("No sim."); return
	sim.add_bar_magnet(pos, dir, strength)
	_shell_ok("Magnet at %s" % _fv(pos))

func _sh_repeat(parts: PackedStringArray) -> void:
	if parts.size() < 3: _shell_err("repeat <N> <cmd>"); return
	var count := parts[1].to_int()
	if count <= 0 or count > 1000: _shell_err("Count 1-1000."); return
	var inner := parts.slice(2)
	var bt := _ptime(inner); if bt < 0: bt = _stime()
	var clean := PackedStringArray()
	for p in inner:
		if not p.begins_with("@"): clean.append(p)
	var cc := " ".join(clean)
	for i in range(count):
		schedule_queue.append({"time": bt + float(i) * 0.016, "cmd": cc})
	_shell_print("[color=#aaaacc]Queued %d x '%s'[/color]" % [count, cc])

func _sh_lock(parts: PackedStringArray, do_lock: bool) -> void:
	var verb := "lock" if do_lock else "unlock"
	if not sim or not sim.has_method("lock_particle"): _shell_err("No lock_particle."); return
	if parts.size() < 2: _shell_err("%s <index|all>" % verb); return
	if parts[1].to_lower() == "all":
		var cnt: int = sim.get_particle_count() if sim.has_method("get_particle_count") else 0
		for i in range(cnt): sim.lock_particle(i, do_lock)
		_shell_ok("%sed all %d particles" % [verb, cnt]); return
	var idx := parts[1].to_int()
	var cnt: int = sim.get_particle_count() if sim.has_method("get_particle_count") else 0
	if idx < 0 or idx >= cnt: _shell_err("Index %d out of range (0-%d)" % [idx, cnt - 1]); return
	sim.lock_particle(idx, do_lock)
	_shell_ok("%sed particle %d" % [verb, idx])

func _sh_ring(parts: PackedStringArray) -> void:
	# ring <type[!]> <pos> <major> <minor> [str] [ax,ay,az]
	if parts.size() < 5: _shell_err("ring <+|-|I|N|S[!]> <pos> <major> <minor> [str] [axis]"); return
	var pos = _pvec(parts[2])
	if pos == null: _shell_err("Bad position vector x,y,z"); return
	var major := parts[3].to_float()
	var minor := parts[4].to_float()
	if major <= 0 or minor <= 0: _shell_err("Radii must be positive"); return
	var raw_type := parts[1]
	var locked := raw_type.ends_with("!")
	var rtype := raw_type.trim_suffix("!")
	var source_type := 0
	var strength := 1.0
	match rtype:
		"+": source_type = 0; strength = 1.0
		"-": source_type = 0; strength = -1.0
		"I": source_type = 1; strength = 1.0
		"N": source_type = 2; strength = 1.0
		"S": source_type = 2; strength = -1.0
		_: _shell_err("Type '%s'? Use + - I N S" % rtype); return
	var axis := Vector3(0, 1, 0)
	for i in range(5, parts.size()):
		if parts[i].begins_with("@"): continue
		if "," in parts[i]:
			var av = _pvec(parts[i])
			if av != null: axis = av
		else:
			strength = parts[i].to_float()
	if not sim or not sim.has_method("add_ring"): _shell_err("No add_ring."); return
	sim.add_ring(pos, major, minor, source_type, strength, axis, locked)
	var lbl := " locked" if locked else ""
	_shell_ok("Ring %s%s at %s R=%.0f r=%.0f" % [rtype, lbl, _fv(pos), major, minor])

func _sh_lock_ring(parts: PackedStringArray, do_lock: bool) -> void:
	var verb := "lockring" if do_lock else "unlockring"
	if not sim or not sim.has_method("lock_ring"): _shell_err("No lock_ring."); return
	if parts.size() < 2: _shell_err("%s <index|all>" % verb); return
	if parts[1].to_lower() == "all":
		var cnt: int = sim.get_ring_count() if sim.has_method("get_ring_count") else 0
		for i in range(cnt): sim.lock_ring(i, do_lock)
		_shell_ok("%s all %d rings" % [verb, cnt]); return
	var idx := parts[1].to_int()
	var cnt: int = sim.get_ring_count() if sim.has_method("get_ring_count") else 0
	if idx < 0 or idx >= cnt: _shell_err("Index %d out of range (0-%d)" % [idx, cnt - 1]); return
	sim.lock_ring(idx, do_lock)
	_shell_ok("%s ring %d" % [verb, idx])

func _sh_info(parts: PackedStringArray) -> void:
	if parts.size() < 2: _shell_err("info <particle|ring> <index>"); return
	if not sim: _shell_err("No simulator."); return
	var kind := parts[1].to_lower()
	if parts.size() < 3: _shell_err("info %s <index>" % kind); return
	var idx := parts[2].to_int()
	if kind == "particle" or kind == "p":
		if not sim.has_method("get_particle_data"): _shell_err("No get_particle_data."); return
		var data: Array = sim.get_particle_data()
		if idx < 0 or idx >= data.size(): _shell_err("Index %d out of range (0-%d)" % [idx, data.size() - 1]); return
		var d: Dictionary = data[idx]
		var tp := "neutral"
		if d["charge"] > 0: tp = "q+"
		elif d["charge"] < 0: tp = "q-"
		elif d["mag_charge"] > 0: tp = "gN"
		elif d["mag_charge"] < 0: tp = "gS"
		_shell_print("[b]Particle %d[/b] [%s]%s" % [idx, tp, " LOCKED" if d["locked"] else ""])
		_shell_print("  pos %s  vel %s" % [_fv(d["pos"]), _fv(d["vel"])])
		_shell_print("  speed=%.1f  mass=%.2f  radius=%.1f" % [d["speed"], d["mass"], d["radius"]])
		var av: Vector3 = d.get("angular_vel", Vector3.ZERO)
		_shell_print("  ang_vel %s (%.3f rad/s)" % [_fv(av), av.length()])
	elif kind == "ring" or kind == "r":
		if not sim.has_method("get_ring_data"): _shell_err("No get_ring_data."); return
		var data: Array = sim.get_ring_data()
		if idx < 0 or idx >= data.size(): _shell_err("Index %d out of range (0-%d)" % [idx, data.size() - 1]); return
		var d: Dictionary = data[idx]
		var types := ["CHARGE", "CURRENT", "MONOPOLE"]
		var st: int = d["source_type"]
		_shell_print("[b]Ring %d[/b] [%s str=%.1f]%s" % [idx, types[clampi(st, 0, 2)], d["strength"], " LOCKED" if d["locked"] else ""])
		_shell_print("  pos %s  vel %s" % [_fv(d["pos"]), _fv(d["vel"])])
		_shell_print("  R=%.1f r=%.1f  mass=%.2f  I=%.2f" % [d["major_radius"], d["minor_radius"], d["mass"], d["moment_of_inertia"]])
		var av: Vector3 = d.get("angular_vel", Vector3.ZERO)
		var q: Quaternion = d.get("orientation", Quaternion())
		var axis := Basis(q) * Vector3(0, 1, 0)
		_shell_print("  ang_vel %s (%.3f rad/s)" % [_fv(av), av.length()])
		_shell_print("  axis %s" % _fv(axis))
	else:
		_shell_err("info <particle|ring> <index>")

func _sh_list(parts: PackedStringArray) -> void:
	if not sim: _shell_err("No simulator."); return
	var kind := parts[1].to_lower() if parts.size() >= 2 else "all"
	if kind == "all" or kind == "particles" or kind == "p":
		var data: Array = sim.get_particle_data() if sim.has_method("get_particle_data") else []
		_shell_print("[b]Particles (%d)[/b]" % data.size())
		for i in range(mini(data.size(), 20)):
			var d: Dictionary = data[i]
			var tp := "n"
			if d["charge"] > 0: tp = "q+"
			elif d["charge"] < 0: tp = "q-"
			elif d["mag_charge"] > 0: tp = "gN"
			elif d["mag_charge"] < 0: tp = "gS"
			var lk := " L" if d["locked"] else ""
			_shell_print("  [%d] %s%s pos=%s spd=%.0f" % [i, tp, lk, _fv(d["pos"]), d["speed"]])
		if data.size() > 20: _shell_print("  ... +%d more" % (data.size() - 20))
	if kind == "all" or kind == "rings" or kind == "r":
		var data: Array = sim.get_ring_data() if sim.has_method("get_ring_data") else []
		var types := ["Q", "I", "M"]
		_shell_print("[b]Rings (%d)[/b]" % data.size())
		for i in range(mini(data.size(), 20)):
			var d: Dictionary = data[i]
			var st: int = d["source_type"]
			var lk := " L" if d["locked"] else ""
			_shell_print("  [%d] %s%s str=%.1f R=%.0f r=%.0f pos=%s" % [i, types[clampi(st, 0, 2)], lk, d["strength"], d["major_radius"], d["minor_radius"], _fv(d["pos"])])
		if data.size() > 20: _shell_print("  ... +%d more" % (data.size() - 20))
	if kind == "all" or kind == "magnets" or kind == "m":
		var cnt: int = sim.get_magnet_count() if sim.has_method("get_magnet_count") else 0
		_shell_print("[b]Magnets (%d)[/b]" % cnt)

func _sh_delete(parts: PackedStringArray) -> void:
	if not sim: _shell_err("No simulator."); return
	if parts.size() < 3: _shell_err("delete <particle|ring|magnet> <index>"); return
	var kind := parts[1].to_lower()
	var idx := parts[2].to_int()
	if kind == "particle" or kind == "p":
		var cnt: int = sim.get_particle_count() if sim.has_method("get_particle_count") else 0
		if idx < 0 or idx >= cnt: _shell_err("Index %d out of range (0-%d)" % [idx, cnt - 1]); return
		sim.remove_particle(idx)
		_shell_ok("Deleted particle %d" % idx)
	elif kind == "ring" or kind == "r":
		var cnt: int = sim.get_ring_count() if sim.has_method("get_ring_count") else 0
		if idx < 0 or idx >= cnt: _shell_err("Index %d out of range (0-%d)" % [idx, cnt - 1]); return
		sim.remove_ring(idx)
		_shell_ok("Deleted ring %d" % idx)
	elif kind == "magnet" or kind == "m":
		var cnt: int = sim.get_magnet_count() if sim.has_method("get_magnet_count") else 0
		if idx < 0 or idx >= cnt: _shell_err("Index %d out of range (0-%d)" % [idx, cnt - 1]); return
		sim.remove_magnet(idx)
		_shell_ok("Deleted magnet %d" % idx)
	else:
		_shell_err("delete <particle|ring|magnet> <index>")


func _sh_exec_file(parts: PackedStringArray) -> void:
	if parts.size() < 2: _shell_err("exec <filepath>"); return
	var path := parts[1]
	# Try common prefixes if bare filename
	var try_paths := [path, "res://" + path, "user://" + path]
	var file: FileAccess = null
	var used_path := ""
	for tp in try_paths:
		file = FileAccess.open(tp, FileAccess.READ)
		if file: used_path = tp; break
	if not file:
		_shell_err("Cannot open: %s" % path); return
	var lines := PackedStringArray()
	while not file.eof_reached():
		lines.append(file.get_line())
	file.close()

	# Process lines: comments (#) and blank lines skipped.
	# `wait N` defers all subsequent lines by N sim-time seconds.
	var base_time := _stime()
	var offset := 0.0
	var executed := 0
	var scheduled := 0
	for line in lines:
		var stripped := line.strip_edges()
		if stripped.is_empty() or stripped.begins_with("#"): continue
		var lparts := stripped.split(" ", false)
		if lparts.is_empty(): continue
		if lparts[0].to_lower() == "wait":
			if lparts.size() >= 2:
				offset += lparts[1].to_float()
			continue
		if offset > 0.001:
			schedule_queue.append({"time": base_time + offset, "cmd": stripped})
			scheduled += 1
			# Add tiny sub-ordering offset to preserve line order
			offset += 0.0001
		else:
			_shell_exec(stripped, false)
			executed += 1
	_shell_ok("Loaded %s: %d exec, %d sched" % [used_path.get_file(), executed, scheduled])


# ═════════════════════════════════════════════════════════
# Toast
# ═════════════════════════════════════════════════════════

func _push_toast(msg: String) -> void:
	toast_queue.append(msg)
	if not toast_label.visible: _show_next_toast()
func _show_next_toast() -> void:
	if toast_queue.is_empty(): toast_label.visible = false; return
	toast_label.text = toast_queue.pop_front()
	toast_label.visible = true; toast_timer = TOAST_DURATION
func _update_toast(delta: float) -> void:
	if not toast_label.visible:
		if not toast_queue.is_empty(): _show_next_toast()
		return
	toast_timer -= delta
	toast_label.modulate.a = maxf(0.0, toast_timer / 0.5) if toast_timer <= 0.5 else 1.0
	if toast_timer <= 0.0:
		toast_label.visible = false; toast_label.modulate.a = 1.0; _show_next_toast()


# ═════════════════════════════════════════════════════════
# Signals
# ═════════════════════════════════════════════════════════

func _on_topology_changed(mode: int) -> void: _push_toast(TOPO_NAMES[clampi(mode, 0, 3)])
func _on_simulation_reset() -> void: _push_toast("World reset")
func _on_particle_spawned(ch: int) -> void:
	_push_toast("Spawned " + ("q+" if ch > 0 else ("q-" if ch < 0 else "neutral")))
func _on_monopole_spawned(mc: int) -> void:
	_push_toast("Spawned " + ("gN" if mc > 0 else "gS") + " monopole")
func _on_magnet_spawned() -> void: _push_toast("Spawned bar magnet")
func _on_ring_spawned(st: int) -> void:
	var names := ["charge", "current", "monopole"]
	_push_toast("Spawned %s ring" % names[clampi(st, 0, 2)])
func _on_binding_event(n: int) -> void: _push_toast("Binding \u2014 neutrals: %d" % n)


# ═════════════════════════════════════════════════════════
# Actions
# ═════════════════════════════════════════════════════════

func _reset_defaults() -> void:
	for param in DEFAULTS:
		var val: float = DEFAULTS[param]
		if sliders.has(param): sliders[param].value = val
		var setter: String = "set_" + param
		if sim and sim.has_method(setter): sim.call(setter, val)

func _spawn_at_cam(type_name: String, type_val: int) -> void:
	if not sim: return
	var cam := get_viewport().get_camera_3d()
	if not cam: return
	var pos := cam.global_position + cam.global_transform.basis.z * -80.0
	match type_name:
		"charge": if sim.has_method("add_particle"): sim.add_particle(pos, Vector3.ZERO, type_val)
		"neutral": if sim.has_method("add_particle"): sim.add_particle(pos, Vector3.ZERO, 0)
		"monopole": if sim.has_method("add_monopole"): sim.add_monopole(pos, Vector3.ZERO, type_val)

func _spawn_magnet_cam() -> void:
	if not sim or not sim.has_method("add_bar_magnet"): return
	var cam := get_viewport().get_camera_3d()
	if not cam: return
	var pos := cam.global_position + cam.global_transform.basis.z * -80.0
	sim.add_bar_magnet(pos, cam.global_transform.basis.z * -1.0, 1.0)


# ═════════════════════════════════════════════════════════
# Helpers
# ═════════════════════════════════════════════════════════

func _rtl(min_w: int, fs: int, col: Color) -> RichTextLabel:
	var r := RichTextLabel.new()
	r.bbcode_enabled = true; r.fit_content = true; r.scroll_active = false
	r.custom_minimum_size = Vector2(min_w, 0)
	r.add_theme_font_size_override("normal_font_size", fs)
	r.add_theme_font_size_override("bold_font_size", fs)
	r.add_theme_color_override("default_color", col)
	return r

func _sbox(bg: Color) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg; sb.border_color = Color(0.22, 0.22, 0.28, 0.45)
	sb.set_border_width_all(1); sb.set_corner_radius_all(4); sb.set_content_margin_all(10)
	return sb

func _btn(bg: Color) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg; sb.set_corner_radius_all(3); sb.set_content_margin_all(4)
	return sb

func _fmt(v: float) -> String:
	if absf(v) >= 1000: return "%.0f" % v
	if absf(v) >= 10: return "%.1f" % v
	if absf(v) >= 1: return "%.2f" % v
	return "%.3f" % v

func _fv(v: Vector3) -> String: return "(%.0f, %.0f, %.0f)" % [v.x, v.y, v.z]
func _stime() -> float: return sim.get_sim_time() if sim and sim.has_method("get_sim_time") else 0.0
func _pvec(s: String) -> Variant:
	var p := s.split(","); if p.size() != 3: return null
	return Vector3(p[0].to_float(), p[1].to_float(), p[2].to_float())
func _ptime(parts: PackedStringArray) -> float:
	for p in parts:
		if p.begins_with("@"): return p.substr(1).to_float()
	return -1.0

func _find_sim(node: Node) -> Node:
	if node.get_class() == "ChargeSimulator3D": return node
	for child in node.get_children():
		var f := _find_sim(child)
		if f: return f
	return null

func _find_ship(node: Node) -> Node:
	if node is Node3D and node.get("frozen") != null and node.get("zoom_speed") != null:
		return node
	for child in node.get_children():
		var f := _find_ship(child)
		if f: return f
	return null