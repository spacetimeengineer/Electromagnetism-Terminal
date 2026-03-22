# Coding Conventions & Patterns

## C++ Style (charge_simulator_3d.h/cpp)

### General
- Dense, compact style — multiple statements per line when logically grouped
- Single-letter loop variables (`i`, `j`, `k`, `N`) for tight numerical loops
- `const auto &` for iteration over vectors
- `(float)` and `(double)` explicit casts everywhere — no implicit narrowing
- `std::max`, `std::min`, `std::clamp` — never raw ternaries for clamping
- `1e-6f` as zero threshold, `1e-12` for double-precision checks

### Naming
- Struct fields: `snake_case` (e.g., `major_radius`, `angular_vel`, `source_type`)
- Methods: `snake_case` (e.g., `compute_e_field_at`, `add_particle`)
- Local variables: short, contextual (e.g., `sp` for sample point, `lp` for local point, `rot` for Basis)
- Constants/tunables: `k_` prefix for physics constants (e.g., `k_coulomb`, `k_biot`)
- Godot nodes: suffixed by type abbreviation (e.g., `field_mi`, `bfield_mesh`, `grid_mat`)

### Struct layout
```cpp
struct Name {
    Vector3 pos;
    Vector3 vel;
    // ... fields grouped by: spatial, dynamic, type, config, visual
    MeshInstance3D *visual = nullptr;  // visual node always last
};
```

### Method binding pattern
```cpp
ClassDB::bind_method(D_METHOD("method_name", "param1", "param2"), &Class::method_name, DEFVAL(default));
```

### Field computation pattern
- Function signature: `Vector3 compute_X_field_at(const Vector3 &p, int exclude_particle = -1, int exclude_ring = -1) const`
- Particle loop with exclusion index
- Ring loop with exclusion index + far-field branch at `3 * major_radius`
- Returns accumulated field vector

### Field rebuild pattern (macro-based)
```
FIELD_REBUILD_BEGIN(show_flag, mesh_instance, mesh, compute_call, steps, ds, stop_buffer)
    // seed loops here — iterate sources, create seed points, call trace(seed, direction)
FIELD_REBUILD_END(mesh, arrow_size)
```

### Ring sample pattern
```cpp
int N = std::clamp((int)(rng.major_radius * 0.25f), 8, 64);
Basis rot(rng.orient);
for (int i = 0; i < N; i++) {
    float theta = Math_TAU * (float)i / (float)N;
    Vector3 lp(std::cos(theta) * rng.major_radius, 0.0f, std::sin(theta) * rng.major_radius);
    Vector3 sp = rng.pos + rot.xform(lp);
    // ... compute with sp
}
```

### Collision pattern
- Check overlap: `dist < r_i + r_j && dist > 1e-6`
- Check approaching: `vr < 0`
- Four-way locked branch: `both locked → nothing, i locked → j bounces, j locked → i bounces, neither → impulse split`
- Separation: same four-way branch, shift by overlap

## GDScript Style (SimHUD.gd, SimSHELL.gd)

### General
- Compact — multiple assignments on one line when related
- `var x := value` typed inference where possible
- `func name() -> ReturnType:` explicit return types
- `_` prefix for private/internal methods
- Match statements for command dispatch (not if-elif chains)

### Shell command pattern
```gdscript
func _sh_commandname(parts: PackedStringArray) -> void:
    if parts.size() < N: _shell_err("usage string"); return
    # parse arguments
    if not sim or not sim.has_method("method"): _shell_err("No method."); return
    # call sim method
    _shell_ok("result message")
```

### Shell dispatch pattern
```gdscript
match parts[0].to_lower():
    "cmd1": _sh_cmd1(parts)
    "cmd2": _sh_cmd2(parts)
    _: _shell_err("Unknown: %s" % parts[0])
```

### UI building pattern
- Panels built programmatically in `_build_*` methods
- StyleBoxFlat for all panel backgrounds
- RichTextLabel with BBCode for colored output
- Font size overrides on all controls (typically 11-13)
- Color palette: green for OK, red for errors, gray for comments, teal for scheduled

### HUD refresh pattern
```gdscript
var _g := func(m: String): return sim.call(m) if sim.has_method(m) else 0
var value: Type = _g.call("get_something")
```

## Important Gotchas for New Sessions

1. **Quaternion API varies by godot-cpp version**: Use `Basis(quat)` for rotation, not `quat.xform()`. The `Basis` constructor from Quaternion is universally available.

2. **ImmediateMesh requires surface_begin/surface_end pairs**: Missing end = crash. The FIELD_REBUILD macros handle this — don't add early returns between BEGIN and END.

3. **Particle vector reallocation**: `particles.erase()` invalidates iterators. The collision loop uses index-based iteration with manual `n--` tracking for this reason.

4. **Ring visual cleanup**: When removing rings, must `queue_free()` the visual MeshInstance3D before erasing from the vector. Same for magnets. Particles use MultiMesh so no individual cleanup needed.

5. **Field function default parameters**: All callers that existed before `exclude_ring` was added still work because it defaults to -1. Don't break this by making it required.

6. **Topology wrapping**: `min_image_vec` handles periodic boundary minimum image convention. ALL distance computations between objects must use it, not raw subtraction.

7. **The `_ready` function clears state**: `particles.clear(); magnets.clear(); rings.clear()` — this means objects added in the editor inspector won't persist. Everything is runtime-spawned.

8. **Global damp applies every step**: It's multiplicative, so `0.999^60 ≈ 0.94` per second. This is intentional — prevents runaway energy accumulation.
