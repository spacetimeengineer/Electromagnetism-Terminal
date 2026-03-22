## SimShell.gd — In-game command console.
## Toggle with backtick (`). Disengages sim keys while open.

extends CanvasLayer

var sim: Node = null

var shell_panel: PanelContainer
var vbox: VBoxContainer
var output_label: RichTextLabel
var input_line: LineEdit

var shell_open: bool = false
var prev_backtick: bool = false

var history: PackedStringArray = []
var history_idx: int = -1
const MAX_HISTORY := 64
const MAX_OUTPUT_LINES := 200
var output_lines: PackedStringArray = []

var schedule_queue: Array[Dictionary] = []


func _ready() -> void:
	sim = _find_simulator(get_tree().root)
	_build_ui()
	_set_shell_visible(false)


func _process(delta: float) -> void:
	var bt := Input.is_key_pressed(KEY_QUOTELEFT)
	if bt and not prev_backtick:
		shell_open = not shell_open
		_set_shell_visible(shell_open)
	prev_backtick = bt

	# Schedule processing
	if sim and sim.has_method("get_sim_time") and not schedule_queue.is_empty():
		var t: float = sim.get_sim_time()
		var fired := []
		for i in range(schedule_queue.size()):
			if schedule_queue[i]["time"] <= t: fired.append(i)
		for i in range(fired.size() - 1, -1, -1):
			_exec_command(schedule_queue[fired[i]]["cmd"], true)
			schedule_queue.remove_at(fired[i])


func _unhandled_input(event: InputEvent) -> void:
	if not shell_open: return
	if event is InputEventKey and event.pressed:
		if event.keycode == KEY_UP:
			if history.size() > 0:
				history_idx = clampi(history_idx - 1, 0, history.size() - 1)
				input_line.text = history[history_idx]
				input_line.caret_column = input_line.text.length()
			get_viewport().set_input_as_handled()
		elif event.keycode == KEY_DOWN:
			if history.size() > 0:
				history_idx = clampi(history_idx + 1, 0, history.size())
				input_line.text = history[history_idx] if history_idx < history.size() else ""
				input_line.caret_column = input_line.text.length()
			get_viewport().set_input_as_handled()
		elif event.keycode == KEY_ESCAPE:
			shell_open = false; _set_shell_visible(false)
			get_viewport().set_input_as_handled()


func _build_ui() -> void:
	var fs := 13
	shell_panel = PanelContainer.new()
	shell_panel.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	shell_panel.position = Vector2(12, -270)
	shell_panel.grow_vertical = Control.GROW_DIRECTION_BEGIN
	shell_panel.custom_minimum_size = Vector2(600, 260)
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.04, 0.05, 0.04, 0.88)
	sb.border_color = Color(0.25, 0.35, 0.25, 0.5)
	sb.set_border_width_all(1); sb.set_corner_radius_all(4); sb.set_content_margin_all(8)
	shell_panel.add_theme_stylebox_override("panel", sb)
	add_child(shell_panel)

	vbox = VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 4)
	shell_panel.add_child(vbox)

	output_label = RichTextLabel.new()
	output_label.bbcode_enabled = true; output_label.scroll_active = true
	output_label.scroll_following = true; output_label.fit_content = false
	output_label.size_flags_vertical = Control.SIZE_EXPAND_FILL
	output_label.add_theme_font_size_override("normal_font_size", fs)
	output_label.add_theme_font_size_override("bold_font_size", fs)
	output_label.add_theme_color_override("default_color", Color(0.75, 0.80, 0.75))
	vbox.add_child(output_label)

	input_line = LineEdit.new()
	input_line.placeholder_text = "type a command... (help for list)"
	input_line.add_theme_font_size_override("font_size", fs)
	input_line.add_theme_color_override("font_color", Color(0.9, 0.95, 0.9))
	input_line.add_theme_color_override("font_placeholder_color", Color(0.45, 0.45, 0.45))
	var isb := StyleBoxFlat.new()
	isb.bg_color = Color(0.08, 0.10, 0.08, 0.9)
	isb.border_color = Color(0.3, 0.4, 0.3, 0.5)
	isb.set_border_width_all(1); isb.set_corner_radius_all(3); isb.set_content_margin_all(6)
	input_line.add_theme_stylebox_override("normal", isb)
	input_line.add_theme_stylebox_override("focus", isb)
	input_line.text_submitted.connect(_on_input_submitted)
	vbox.add_child(input_line)

	_print_output("[color=#66aa66]Shell ready. Type [b]help[/b] for commands.[/color]")


func _set_shell_visible(vis: bool) -> void:
	shell_panel.visible = vis
	if sim and sim.has_method("set_input_enabled"):
		sim.set_input_enabled(not vis)
	if vis:
		input_line.grab_focus()
		Input.set_mouse_mode(Input.MOUSE_MODE_VISIBLE)
	else:
		input_line.release_focus()
		Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)


func _print_output(line: String) -> void:
	output_lines.append(line)
	if output_lines.size() > MAX_OUTPUT_LINES:
		output_lines = output_lines.slice(output_lines.size() - MAX_OUTPUT_LINES)
	output_label.text = "\n".join(output_lines)

func _print_error(msg: String) -> void: _print_output("[color=#ff6666]error: %s[/color]" % msg)
func _print_ok(msg: String) -> void: _print_output("[color=#88cc88]%s[/color]" % msg)
func _print_sched(msg: String) -> void: _print_output("[color=#aaaacc]%s[/color]" % msg)


func _on_input_submitted(text: String) -> void:
	var cmd := text.strip_edges(); input_line.clear()
	if cmd.is_empty(): return
	if history.is_empty() or history[history.size() - 1] != cmd:
		history.append(cmd)
		if history.size() > MAX_HISTORY: history = history.slice(history.size() - MAX_HISTORY)
	history_idx = history.size()
	_print_output("[color=#999999]> %s[/color]" % cmd)
	_exec_command(cmd, false)


func _exec_command(cmd: String, from_sched: bool) -> void:
	var stripped := cmd.strip_edges()
	if stripped.is_empty() or stripped.begins_with("#"): return
	var parts := stripped.split(" ", false)
	if parts.is_empty(): return
	match parts[0].to_lower():
		"help": _cmd_help()
		"time": _print_ok("sim time: %.4f s" % _get_time())
		"pause", "resume": _print_ok("Use Space key (sim time: %.3f)" % _get_time())
		"reset":
			if sim and sim.has_method("reset_world"):
				sim.reset_world(); schedule_queue.clear()
				_print_ok("World reset, queue cleared.")
		"clear":
			if sim and sim.has_method("clear_neutrals"):
				sim.clear_neutrals(); _print_ok("Neutrals cleared.")
		"queue": _cmd_queue()
		"flush": schedule_queue.clear(); _print_ok("Queue cleared.")
		"cls": output_lines.clear(); output_label.text = ""
		"spawn": _cmd_spawn(parts)
		"magnet": _cmd_magnet(parts)
		"repeat": _cmd_repeat(parts)
		"lock": _cmd_lock(parts, true)
		"unlock": _cmd_lock(parts, false)
		"ring": _cmd_ring(parts)
		"lockring": _cmd_lock_ring(parts, true)
		"unlockring": _cmd_lock_ring(parts, false)
		"info": _cmd_info(parts)
		"list": _cmd_list(parts)
		"delete": _cmd_delete(parts)
		"echo": _print_output(cmd.substr(5) if cmd.length() > 5 else "")
		"exec": _cmd_exec_file(parts)
		_: _print_error("Unknown: %s" % parts[0])


func _cmd_help() -> void:
	for l in [
		"[b]COMMANDS[/b]", "",
		"[color=#aaccaa]spawn[/color] +|-|o|N|S x,y,z vx,vy,vz [@time]",
		"  [color=#666666]Append ! to lock: spawn +! 500,500,500 0,0,0[/color]",
		"[color=#aaccaa]magnet[/color] x,y,z dx,dy,dz strength [@time]",
		"[color=#aaccaa]repeat[/color] N <command>",
		"[color=#aaccaa]ring[/color] +|-|I|N|S x,y,z major minor [str] [ax,ay,az] [!]",
		"[color=#aaccaa]lock[/color] <index|all>   [color=#aaccaa]unlock[/color] <index|all>",
		"[color=#aaccaa]lockring[/color] <index|all>   [color=#aaccaa]unlockring[/color] <index|all>",
		"[color=#aaccaa]info[/color] <particle|ring> <index>",
		"[color=#aaccaa]list[/color] [particles|rings|magnets]",
		"[color=#aaccaa]delete[/color] <particle|ring|magnet> <index>",
		"[color=#aaccaa]echo[/color] <text>",
		"[color=#aaccaa]exec[/color] <filepath.sim>",
		"", "[color=#aaccaa]time[/color] [color=#aaccaa]queue[/color] [color=#aaccaa]flush[/color] [color=#aaccaa]reset[/color] [color=#aaccaa]clear[/color] [color=#aaccaa]cls[/color]",
		"", "[color=#666666]Scripts: # comments, wait <sec>, exec <file>[/color]",
		"[color=#666666]Vectors: x,y,z (no spaces). @time = at sim time.[/color]",
		"[color=#666666]Example: spawn + 500,500,500 100,0,0 @2.0[/color]",
	]: _print_output(l)


func _cmd_queue() -> void:
	if schedule_queue.is_empty(): _print_ok("Queue empty."); return
	_print_output("[b]Scheduled (%d):[/b]" % schedule_queue.size())
	var s := schedule_queue.duplicate()
	s.sort_custom(func(a, b): return a["time"] < b["time"])
	for i in range(mini(s.size(), 30)):
		_print_output("  @%.3f  %s" % [s[i]["time"], s[i]["cmd"]])


func _cmd_spawn(parts: PackedStringArray) -> void:
	if parts.size() < 4: _print_error("spawn <type[!]> <pos> <vel> [@time]"); return
	var pos := _parse_vec3(parts[2]); var vel := _parse_vec3(parts[3])
	if pos == null or vel == null: _print_error("Bad vector. Use x,y,z"); return
	var raw_type := parts[1]
	var locked := raw_type.ends_with("!")
	var ptype := raw_type.trim_suffix("!")
	var st := _extract_time(parts)
	if st >= 0.0:
		var rc := "spawn %s %s %s" % [raw_type, parts[2], parts[3]]
		schedule_queue.append({"time": st, "cmd": rc})
		_print_sched("Scheduled: %s @%.3f" % [rc, st]); return
	if not sim: _print_error("No simulator."); return
	var lbl := " (locked)" if locked else ""
	match ptype:
		"+": sim.add_particle(pos, vel, 1, locked); _print_ok("Spawned +%s at %s" % [lbl, _fv(pos)])
		"-": sim.add_particle(pos, vel, -1, locked); _print_ok("Spawned -%s at %s" % [lbl, _fv(pos)])
		"o": sim.add_particle(pos, vel, 0, locked); _print_ok("Spawned o%s at %s" % [lbl, _fv(pos)])
		"N": sim.add_monopole(pos, vel, 1, locked); _print_ok("Spawned N%s at %s" % [lbl, _fv(pos)])
		"S": sim.add_monopole(pos, vel, -1, locked); _print_ok("Spawned S%s at %s" % [lbl, _fv(pos)])
		_: _print_error("Type '%s'? Use + - o N S (add ! to lock)" % ptype)


func _cmd_magnet(parts: PackedStringArray) -> void:
	if parts.size() < 4: _print_error("magnet <pos> <dir> <str> [@time]"); return
	var pos := _parse_vec3(parts[1]); var dir := _parse_vec3(parts[2])
	if pos == null or dir == null: _print_error("Bad vector."); return
	var strength := parts[3].to_float()
	var st := _extract_time(parts)
	if st >= 0.0:
		var rc := "magnet %s %s %s" % [parts[1], parts[2], parts[3]]
		schedule_queue.append({"time": st, "cmd": rc})
		_print_sched("Scheduled: %s @%.3f" % [rc, st]); return
	if not sim or not sim.has_method("add_bar_magnet"): _print_error("No add_bar_magnet."); return
	sim.add_bar_magnet(pos, dir, strength)
	_print_ok("Magnet at %s dir %s str %.0f" % [_fv(pos), _fv(dir), strength])


func _cmd_repeat(parts: PackedStringArray) -> void:
	if parts.size() < 3: _print_error("repeat <count> <command>"); return
	var count := parts[1].to_int()
	if count <= 0 or count > 1000: _print_error("Count 1-1000."); return
	var inner := parts.slice(2)
	var bt := _extract_time(inner); if bt < 0: bt = _get_time()
	var clean := PackedStringArray()
	for p in inner:
		if not p.begins_with("@"): clean.append(p)
	var cc := " ".join(clean)
	for i in range(count):
		schedule_queue.append({"time": bt + float(i) * 0.016, "cmd": cc})
	_print_sched("Queued %d x '%s' from @%.3f" % [count, cc, bt])


func _cmd_lock(parts: PackedStringArray, do_lock: bool) -> void:
	var verb := "lock" if do_lock else "unlock"
	if not sim or not sim.has_method("lock_particle"): _print_error("No lock_particle."); return
	if parts.size() < 2: _print_error("%s <index|all>" % verb); return
	if parts[1].to_lower() == "all":
		var cnt: int = sim.get_particle_count() if sim.has_method("get_particle_count") else 0
		for i in range(cnt): sim.lock_particle(i, do_lock)
		_print_ok("%sed all %d particles" % [verb, cnt]); return
	var idx := parts[1].to_int()
	var cnt: int = sim.get_particle_count() if sim.has_method("get_particle_count") else 0
	if idx < 0 or idx >= cnt: _print_error("Index %d out of range (0-%d)" % [idx, cnt - 1]); return
	sim.lock_particle(idx, do_lock)
	_print_ok("%sed particle %d" % [verb, idx])


func _cmd_ring(parts: PackedStringArray) -> void:
	# ring <type[!]> <pos> <major> <minor> [str] [ax,ay,az]
	if parts.size() < 5: _print_error("ring <+|-|I|N|S[!]> <pos> <major> <minor> [str] [axis]"); return
	var pos := _parse_vec3(parts[2])
	if pos == null: _print_error("Bad position vector x,y,z"); return
	var major := parts[3].to_float()
	var minor := parts[4].to_float()
	if major <= 0 or minor <= 0: _print_error("Radii must be positive"); return
	var raw_type := parts[1]
	var locked := raw_type.ends_with("!")
	var rtype := raw_type.trim_suffix("!")
	var source_type := 0  # RING_CHARGE
	var strength := 1.0
	match rtype:
		"+": source_type = 0; strength = 1.0
		"-": source_type = 0; strength = -1.0
		"I": source_type = 1; strength = 1.0
		"N": source_type = 2; strength = 1.0
		"S": source_type = 2; strength = -1.0
		_: _print_error("Type '%s'? Use + - I N S" % rtype); return
	var axis := Vector3(0, 1, 0)
	# Scan remaining args for strength (plain number) and axis (x,y,z)
	for i in range(5, parts.size()):
		if parts[i].begins_with("@"): continue
		if "," in parts[i]:
			var av := _parse_vec3(parts[i])
			if av != null: axis = av
		else:
			strength = parts[i].to_float()
	if not sim or not sim.has_method("add_ring"): _print_error("No add_ring."); return
	sim.add_ring(pos, major, minor, source_type, strength, axis, locked)
	var lbl := " (locked)" if locked else ""
	_print_ok("Ring %s%s at %s R=%.0f r=%.0f" % [rtype, lbl, _fv(pos), major, minor])


func _cmd_lock_ring(parts: PackedStringArray, do_lock: bool) -> void:
	var verb := "lockring" if do_lock else "unlockring"
	if not sim or not sim.has_method("lock_ring"): _print_error("No lock_ring."); return
	if parts.size() < 2: _print_error("%s <index|all>" % verb); return
	if parts[1].to_lower() == "all":
		var cnt: int = sim.get_ring_count() if sim.has_method("get_ring_count") else 0
		for i in range(cnt): sim.lock_ring(i, do_lock)
		_print_ok("%s all %d rings" % [verb, cnt]); return
	var idx := parts[1].to_int()
	var cnt: int = sim.get_ring_count() if sim.has_method("get_ring_count") else 0
	if idx < 0 or idx >= cnt: _print_error("Index %d out of range (0-%d)" % [idx, cnt - 1]); return
	sim.lock_ring(idx, do_lock)
	_print_ok("%s ring %d" % [verb, idx])


func _cmd_info(parts: PackedStringArray) -> void:
	if parts.size() < 2: _print_error("info <particle|ring> <index>"); return
	if not sim: _print_error("No simulator."); return
	var kind := parts[0 + 1].to_lower()  # "particle" or "ring"
	if parts.size() < 3: _print_error("info %s <index>" % kind); return
	var idx := parts[2].to_int()
	if kind == "particle" or kind == "p":
		if not sim.has_method("get_particle_data"): _print_error("No get_particle_data."); return
		var data: Array = sim.get_particle_data()
		if idx < 0 or idx >= data.size(): _print_error("Index %d out of range (0-%d)" % [idx, data.size() - 1]); return
		var d: Dictionary = data[idx]
		var tp := "neutral"
		if d["charge"] > 0: tp = "q+"
		elif d["charge"] < 0: tp = "q-"
		elif d["mag_charge"] > 0: tp = "gN"
		elif d["mag_charge"] < 0: tp = "gS"
		_print_output("[b]Particle %d[/b] [%s]%s" % [idx, tp, " LOCKED" if d["locked"] else ""])
		_print_output("  pos %s  vel %s" % [_fv(d["pos"]), _fv(d["vel"])])
		_print_output("  speed=%.1f  mass=%.2f  radius=%.1f" % [d["speed"], d["mass"], d["radius"]])
		var av: Vector3 = d.get("angular_vel", Vector3.ZERO)
		_print_output("  ang_vel %s (%.3f rad/s)" % [_fv(av), av.length()])
	elif kind == "ring" or kind == "r":
		if not sim.has_method("get_ring_data"): _print_error("No get_ring_data."); return
		var data: Array = sim.get_ring_data()
		if idx < 0 or idx >= data.size(): _print_error("Index %d out of range (0-%d)" % [idx, data.size() - 1]); return
		var d: Dictionary = data[idx]
		var types := ["CHARGE", "CURRENT", "MONOPOLE"]
		var st: int = d["source_type"]
		_print_output("[b]Ring %d[/b] [%s str=%.1f]%s" % [idx, types[clampi(st, 0, 2)], d["strength"], " LOCKED" if d["locked"] else ""])
		_print_output("  pos %s  vel %s" % [_fv(d["pos"]), _fv(d["vel"])])
		_print_output("  R=%.1f r=%.1f  mass=%.2f  I=%.2f" % [d["major_radius"], d["minor_radius"], d["mass"], d["moment_of_inertia"]])
		var av: Vector3 = d.get("angular_vel", Vector3.ZERO)
		var q: Quaternion = d.get("orientation", Quaternion())
		var axis := Basis(q).xform(Vector3(0, 1, 0))
		_print_output("  ang_vel %s (%.3f rad/s)" % [_fv(av), av.length()])
		_print_output("  axis %s" % _fv(axis))
	else:
		_print_error("info <particle|ring> <index>")


func _cmd_list(parts: PackedStringArray) -> void:
	if not sim: _print_error("No simulator."); return
	var kind := parts[1].to_lower() if parts.size() >= 2 else "all"
	if kind == "all" or kind == "particles" or kind == "p":
		var data: Array = sim.get_particle_data() if sim.has_method("get_particle_data") else []
		_print_output("[b]Particles (%d)[/b]" % data.size())
		for i in range(mini(data.size(), 20)):
			var d: Dictionary = data[i]
			var tp := "n"
			if d["charge"] > 0: tp = "q+"
			elif d["charge"] < 0: tp = "q-"
			elif d["mag_charge"] > 0: tp = "gN"
			elif d["mag_charge"] < 0: tp = "gS"
			var lk := " L" if d["locked"] else ""
			_print_output("  [%d] %s%s pos=%s spd=%.0f" % [i, tp, lk, _fv(d["pos"]), d["speed"]])
		if data.size() > 20: _print_output("  ... +%d more" % (data.size() - 20))
	if kind == "all" or kind == "rings" or kind == "r":
		var data: Array = sim.get_ring_data() if sim.has_method("get_ring_data") else []
		var types := ["Q", "I", "M"]
		_print_output("[b]Rings (%d)[/b]" % data.size())
		for i in range(mini(data.size(), 20)):
			var d: Dictionary = data[i]
			var st: int = d["source_type"]
			var lk := " L" if d["locked"] else ""
			_print_output("  [%d] %s%s str=%.1f R=%.0f r=%.0f pos=%s" % [i, types[clampi(st, 0, 2)], lk, d["strength"], d["major_radius"], d["minor_radius"], _fv(d["pos"])])
		if data.size() > 20: _print_output("  ... +%d more" % (data.size() - 20))
	if kind == "all" or kind == "magnets" or kind == "m":
		var cnt: int = sim.get_magnet_count() if sim.has_method("get_magnet_count") else 0
		_print_output("[b]Magnets (%d)[/b]" % cnt)


func _cmd_delete(parts: PackedStringArray) -> void:
	if not sim: _print_error("No simulator."); return
	if parts.size() < 3: _print_error("delete <particle|ring|magnet> <index>"); return
	var kind := parts[1].to_lower()
	var idx := parts[2].to_int()
	if kind == "particle" or kind == "p":
		var cnt: int = sim.get_particle_count() if sim.has_method("get_particle_count") else 0
		if idx < 0 or idx >= cnt: _print_error("Index %d out of range (0-%d)" % [idx, cnt - 1]); return
		sim.remove_particle(idx)
		_print_ok("Deleted particle %d" % idx)
	elif kind == "ring" or kind == "r":
		var cnt: int = sim.get_ring_count() if sim.has_method("get_ring_count") else 0
		if idx < 0 or idx >= cnt: _print_error("Index %d out of range (0-%d)" % [idx, cnt - 1]); return
		sim.remove_ring(idx)
		_print_ok("Deleted ring %d" % idx)
	elif kind == "magnet" or kind == "m":
		var cnt: int = sim.get_magnet_count() if sim.has_method("get_magnet_count") else 0
		if idx < 0 or idx >= cnt: _print_error("Index %d out of range (0-%d)" % [idx, cnt - 1]); return
		sim.remove_magnet(idx)
		_print_ok("Deleted magnet %d" % idx)
	else:
		_print_error("delete <particle|ring|magnet> <index>")


func _cmd_exec_file(parts: PackedStringArray) -> void:
	if parts.size() < 2: _print_error("exec <filepath>"); return
	var path := parts[1]
	var try_paths := [path, "res://" + path, "user://" + path]
	var file: FileAccess = null
	var used_path := ""
	for tp in try_paths:
		file = FileAccess.open(tp, FileAccess.READ)
		if file: used_path = tp; break
	if not file:
		_print_error("Cannot open: %s" % path); return
	var lines := PackedStringArray()
	while not file.eof_reached():
		lines.append(file.get_line())
	file.close()

	var base_time := _get_time()
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
			offset += 0.0001
		else:
			_exec_command(stripped, false)
			executed += 1
	_print_ok("Loaded %s: %d exec, %d sched" % [used_path.get_file(), executed, scheduled])


func _parse_vec3(s: String) -> Variant:
	var p := s.split(","); if p.size() != 3: return null
	return Vector3(p[0].to_float(), p[1].to_float(), p[2].to_float())

func _extract_time(parts: PackedStringArray) -> float:
	for p in parts:
		if p.begins_with("@"): return p.substr(1).to_float()
	return -1.0

func _get_time() -> float:
	return sim.get_sim_time() if sim and sim.has_method("get_sim_time") else 0.0

func _fv(v: Vector3) -> String: return "(%.0f, %.0f, %.0f)" % [v.x, v.y, v.z]

func _find_simulator(node: Node) -> Node:
	if node.get_class() == "ChargeSimulator3D": return node
	for child in node.get_children():
		var f := _find_simulator(child)
		if f: return f
	return null