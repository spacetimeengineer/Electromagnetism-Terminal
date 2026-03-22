# Charge Simulator 3D — Complete Feature Reference

## Overview

A real-time 3D electromagnetic simulator built as a Godot 4 GDExtension plugin (C++/godot-cpp). Models electric charges, magnetic monopoles, bar magnets, and toroidal rings with five field types, full collision dynamics, and angular momentum. Includes an in-game shell/interpreter with file-based scene scripting.

**Source files:**
- `charge_simulator_3d.h` / `charge_simulator_3d.cpp` — C++ core (physics, fields, rendering, GDScript bindings)
- `SimHUD.gd` — Primary UI: physics panel, model equations, keybinds, control sliders, shell, log, toast notifications
- `SimSHELL.gd` — Standalone shell (legacy, mirrors SimHUD shell)
- `SimControlPanel.gd` — Standalone control panel (legacy)
- `ShipController.gd` — FPS-style camera controller
- `register_types.cpp` — GDExtension registration

The plugin is at `~/charge-sim-3d/addons/charge_sim_3d`. The main node is `ChargeSimulator3D` (extends Node3D).

---

## Object Types

### Particles (`struct Particle`)
Point-like objects rendered as spheres via MultiMesh.

| Field | Type | Description |
|-------|------|-------------|
| `pos` | Vector3 | World position |
| `vel` | Vector3 | Linear velocity |
| `angular_vel` | Vector3 | Angular velocity (world-space, rad/s) |
| `orient` | Quaternion | Orientation (identity default) |
| `charge` | int | Electric charge: +1, -1, or 0 |
| `mag_charge` | int | Magnetic charge: +1 (north), -1 (south), or 0 |
| `mass` | float | Particle mass |
| `radius` | float | Collision/render radius |
| `locked` | bool | If true: no velocity integration, infinite mass in collisions, no binding |

**Particle subtypes by charge/mag_charge:**
- `charge=+1, mag=0` → Positive charge (red sphere, key `1`)
- `charge=-1, mag=0` → Negative charge (blue sphere, key `2`)
- `charge=0, mag=0` → Neutral (gray sphere, key `3`)
- `charge=0, mag=+1` → North monopole (orange sphere, key `4`)
- `charge=0, mag=-1` → South monopole (purple sphere, key `5`)

### Bar Magnets (`struct Magnet`)
Static magnetic dipoles rendered as red BoxMesh nodes.

| Field | Type | Description |
|-------|------|-------------|
| `pos` | Vector3 | World position |
| `m` | Vector3 | Magnetic moment vector (direction × strength) |
| `radius` | float | Collision radius |
| `length`, `thickness` | float | Visual dimensions |
| `visual` | MeshInstance3D* | Scene node |

Magnets are fully static — no velocity, no forces, no collisions. They only source B and A fields via the dipole formula.

### Toroidal Rings (`struct ToroidalRing`)
Thick torus-shaped objects with procedurally generated ArrayMesh. Full dynamics.

| Field | Type | Description |
|-------|------|-------------|
| `pos` | Vector3 | World position (center of torus) |
| `vel` | Vector3 | Linear velocity |
| `angular_vel` | Vector3 | Angular velocity (world-space, rad/s) |
| `orient` | Quaternion | Orientation (ring axis = local Y) |
| `major_radius` | float | Distance from torus center to tube center |
| `minor_radius` | float | Tube cross-section radius |
| `source_type` | RingSourceType | RING_CHARGE(0), RING_CURRENT(1), RING_MONOPOLE(2) |
| `strength` | float | Charge value, current magnitude, or monopole strength |
| `mass` | float | Derived from torus volume: `2π²Rr² × 0.01` |
| `moment_of_inertia` | float | Scalar: `m(R² + ¾r²)` |
| `locked` | bool | Same semantics as particle locking |
| `visual` | MeshInstance3D* | Scene node (procedural torus mesh) |

**Ring source types and their physics:**
- **RING_CHARGE** (`+`/`-`): Distributed static charge → sources E field (Coulomb). If ring moves, also sources B (Biot-Savart from moving charges). Experiences force `qE + q(v×B)`.
- **RING_CURRENT** (`I`): Current flowing around the loop, net charge zero → sources B field (Biot-Savart from tangential current elements I·dl), sources A field (vector potential). At far-field, collapses to magnetic dipole `m = I·π·R²·axis`. Experiences Laplace force `I·dl × B`.
- **RING_MONOPOLE** (`N`/`S`): Distributed magnetic monopoles → sources B field (monopole Coulomb-like). If ring moves, also sources E (dual Biot-Savart) and C field. Experiences force `gB - g(v×E)`.

**Ring visual indicators:**
- Axis line through center: bright white on positive (north) side, dim gray on negative side
- Small crosshair at positive tip for orientation clarity
- Color by type: red/blue (±charge), cyan (current), orange/purple (±monopole)

---

## Field Types

Five field types, all computed and visualized:

| Field | Color | Sources | Formula context |
|-------|-------|---------|----------------|
| **E** (Electric) | White | Charges (Coulomb), moving monopoles (dual Biot-Savart) | `F = qE` |
| **B** (Magnetic) | Cyan | Magnets (dipole), moving charges (Biot-Savart), monopoles (monopole Coulomb), current rings | `F = qv×B` |
| **A** (Vector potential) | Green | Magnets (dipole A), moving charges (`A ∝ qv/r`), current rings | `B = ∇×A` |
| **C** (Electric vector potential) | Pink | Moving monopoles (`C ∝ gv/r`) | `E = -∇×C` |
| **S** (Poynting) | Yellow-orange | Computed as `E × B` | Energy flow direction |

**Field computation architecture:**
- Each `compute_X_field_at(p, exclude_particle, exclude_ring)` evaluates the field at point `p`
- Particle self-exclusion via `exclude_particle` index
- Ring self-exclusion via `exclude_ring` index
- **Far-field optimization**: Rings beyond `3 × major_radius` use closed-form approximations (point charge, magnetic dipole, point monopole) instead of N-sample sums. Current rings collapse to dipole `m = I·π·R²·axis` using the existing bar magnet dipole formula.
- Near-field uses N discrete samples around the major circle: `N = clamp(major_radius / 4, 8, 64)`

**Field line rendering:**
- ImmediateMesh with PRIMITIVE_LINES + PRIMITIVE_TRIANGLES (arrows)
- Shared macro `FIELD_REBUILD_BEGIN/END` handles tracing, arrow collection, adaptive step sizing
- Anti-crumple: kills trace if direction flips >90°
- Decay cutoff: stops at 5% of starting field strength
- Adaptive step: smaller steps in weak fields for smooth curves
- `too_close` lambda checks proximity to particles, magnets, AND ring torus surfaces
- Each field has independent line count tunables adjustable via keys

**Field line seeding per source:**
- Charges: Fibonacci sphere around particle
- Moving charges: Ring of seeds perpendicular to velocity
- Magnets: Fibonacci sphere around dipole
- Current rings: Seeds radially outward, above/below torus surface, through center hole
- Charge rings: Seeds radially + above/below
- Monopole rings: Radial seeds in all directions

---

## Physics System

### Force computation

**Particles** (`compute_accelerations`):
- Charged particles: `F = qE + q·k_lorentz·(v × B)` (Lorentz force)
- Magnetic monopoles: `F = gB - g·k_dual_lorentz·(v × E)` (dual Lorentz force)
- Acceleration capped at `max_mag_accel = 5000`

**Rings** (in `step_fixed`):
- N sample points around major circle, transformed by orientation quaternion
- Each sample computes force from external fields (self-excluded)
- Net force → linear acceleration
- Net torque (`Σ lever × dF`) → angular acceleration / moment_of_inertia
- Angular acceleration capped at `max_mag_accel`

### Integration (Euler, fixed timestep)
- `fixed_dt = 0.016` (configurable)
- Velocity: `v += a·dt`, then `v *= global_damp`, then speed-capped
- Position: `p += v·dt`, then boundary handling
- Angular velocity: `ω += α·dt`, then `ω *= global_damp`, then capped at `max_angular_speed = 10 rad/s`
- Orientation: quaternion integration via `dq/dt = ½·ω̃·q` (Hamilton product, renormalized each step)

### Boundary topologies (cycle with `O` key)
| Mode | Behavior |
|------|----------|
| TOPO_TORUS (0) | Periodic wrapping on all axes (3-torus) |
| TOPO_SPHERE (1) | Reflective sphere boundary |
| TOPO_SPHERE_WRAP (2) | Antipodal wrapping sphere |
| TOPO_OPEN (3) | No boundaries |

### Collisions

**Particle-particle:**
- Neutral-neutral: merge into larger neutral (mass/momentum conserved), blocked if either locked
- Charged/neutral elastic: impulse-based, locked = infinite mass wall
- Friction spin transfer: glancing impacts generate angular velocity via tangential impulse (coefficient 0.3). Uses `I = 2/5·m·r²` for solid sphere.

**Ring-particle:**
- Proper toroidal distance: particle position → ring local space → project onto major circle → tube distance
- Normal impulse + overlap separation, respects locked flags on both sides
- Off-center impacts induce ring spin: `Δω = (lever × J) / I`
- Friction spin transfer to both particle and ring

**Ring-ring:**
- Broad phase: bounding sphere check (sum of major+minor radii)
- Narrow phase: 16 sample points on ring A projected onto ring B's tube surface, finds deepest overlap
- Elastic impulse with locked handling
- Collision-induced spin on both rings

### Binding
- Opposite charges within `bind_radius + bind_relax` merge into neutrals
- Opposite monopoles merge similarly
- Locked particles excluded from binding candidates

---

## Tunables (exposed to GDScript, adjustable via control panel sliders)

| Parameter | Default | Description |
|-----------|---------|-------------|
| `k_coulomb` | 100000 | Electric force constant |
| `k_biot` | 25 | Biot-Savart constant |
| `k_dipole` | 5000 | Magnetic dipole constant |
| `k_lorentz` | 1.0 | Lorentz force multiplier |
| `k_mag_coulomb` | 100000 | Monopole Coulomb constant |
| `k_dual_lorentz` | 1.0 | Dual Lorentz force multiplier |
| `k_dual_biot` | 25 | Dual Biot-Savart constant |
| `max_speed` | 600 | Velocity cap (units/s) |
| `max_angular_speed` | 10 | Angular velocity cap (rad/s) |
| `global_damp` | 0.999 | Per-step velocity damping factor |
| `soften_eps2` | 8.0 | Force softening (prevents singularities) |
| `fixed_dt` | 0.016 | Physics timestep |
| `max_mag_accel` | 5000 | Acceleration cap |

---

## Rendering

- Particles: 5 MultiMeshInstance3D pools (pos/neg/neutral/north/south), transforms include orientation quaternion
- Magnets: Individual BoxMesh MeshInstance3D nodes
- Rings: Individual procedural ArrayMesh torus MeshInstance3D nodes, segment count adapts to size
- Ring axis indicators: ImmediateMesh lines rebuilt each frame (bright/dim axis + crosshair at tip)
- Particle spin indicators: ImmediateMesh lines, visible when `|ω| > 0.1`, length/brightness scale with spin speed
- Grid: ImmediateMesh wireframe, topology-aware (box edges for torus, great circles for sphere)
- WorldEnvironment: dark background with bloom/glow (additive, threshold 0.9)
- All materials use `FEATURE_EMISSION` for glow

---

## Keyboard Controls

| Key | Action |
|-----|--------|
| **Camera** | |
| WASD | Translate |
| Q/E | Up/down |
| Mouse | Look |
| Scroll | Zoom |
| Shift | 4× speed boost |
| Esc | Freeze camera |
| LClick | Unfreeze |
| **Simulation** | |
| Space | Pause/resume |
| R | Reset world |
| C | Clear neutrals |
| O | Cycle topology |
| X | Toggle grid |
| **Spawn** | |
| 1 | Positive charge |
| 2 | Negative charge |
| 3 | Neutral |
| 4 | North monopole |
| 5 | South monopole |
| 6 | Charge ring (facing camera) |
| M | Bar magnet (facing camera) |
| **Fields** | |
| F | Toggle E field lines |
| G | Toggle B field lines |
| V | Toggle A field lines |
| J | Toggle C field lines |
| P | Toggle S (Poynting) field lines |
| T | Toggle realtime field rebuild |
| -/= | Decrease/increase E line count |
| [/] | Decrease/increase B line count (magnets) |
| ;/' | Decrease/increase B line count (charges) |
| ,/. | Decrease/increase A line count |
| 9/0 | Decrease/increase C line count |
| **Panels** | |
| H | Toggle HUD |
| L | Toggle particle log |
| Tab | Toggle control panel |
| ` | Toggle shell |

---

## Shell / Interpreter

Both SimHUD and SimSHELL implement the same command language. Open with `` ` `` key.

### Object Commands

```
spawn <type[!]> <pos> <vel> [@time]
    Types: + - o N S (append ! to lock)
    Example: spawn +! 500,500,500 0,0,0

magnet <pos> <dir> <strength> [@time]
    Example: magnet 500,500,500 1,0,0 200

ring <type[!]> <pos> <major> <minor> [strength] [ax,ay,az]
    Types: + - I N S (append ! to lock)
    Axis: ring normal direction (default 0,1,0)
    Example: ring I! 500,500,500 60 8 5.0 0,0,1

repeat N <command>
    Example: repeat 20 spawn o 400,500,500 200,50,0
```

### Query Commands

```
info <particle|ring|p|r> <index>
    Full state dump: pos, vel, angular_vel, mass, etc.

list [particles|rings|magnets|p|r|m]
    Summary of all objects (caps at 20 entries)

time
    Current sim time
```

### Modification Commands

```
lock <index|all>          unlock <index|all>
lockring <index|all>      unlockring <index|all>
delete <particle|ring|magnet|p|r|m> <index>
```

### Control Commands

```
reset       — Clear all objects, reset time
clear       — Remove neutrals only
queue       — Show scheduled commands
flush       — Clear schedule queue
cls         — Clear shell output
echo <text> — Print text to shell
```

### File Scripting

```
exec <filepath>
    Loads and runs a .sim script file
    Tries: bare path, res:// prefix, user:// prefix
```

**Script syntax:**
- `#` — Comment (full line)
- Blank lines ignored
- `wait N` — Defer subsequent commands by N sim-seconds (cumulative)
- `echo text` — Print progress messages
- All shell commands work in scripts
- Scripts can `exec` other scripts (nesting)

**Example `.sim` file:**
```
# solenoid.sim
reset
echo Building solenoid...
ring I! 500,500,400 35 6 4.0
ring I! 500,500,450 35 6 4.0
ring I! 500,500,500 35 6 4.0
ring I! 500,500,550 35 6 4.0
ring I! 500,500,600 35 6 4.0

wait 1.0
echo Firing charge through bore...
spawn + 500,500,250 0,0,300
```

---

## GDScript API

All methods are bound via ClassDB and callable from GDScript.

### Spawning
```gdscript
add_particle(pos: Vector3, vel: Vector3, charge: int, locked: bool = false)
add_monopole(pos: Vector3, vel: Vector3, mag_charge: int, locked: bool = false)
add_bar_magnet(pos: Vector3, dir: Vector3, strength: float)
add_ring(pos: Vector3, major_radius: float, minor_radius: float,
         source_type: int, strength: float,
         axis: Vector3 = Vector3(0,1,0), locked: bool = false)
```

### Removal
```gdscript
remove_particle(index: int)
remove_ring(index: int)
remove_magnet(index: int)
reset_world()       # Clears everything
clear_neutrals()    # Removes only neutrals
```

### Locking
```gdscript
lock_particle(index: int, locked: bool)
is_particle_locked(index: int) -> bool
lock_ring(index: int, locked: bool)
is_ring_locked(index: int) -> bool
```

### Queries
```gdscript
get_particle_count() -> int
get_positive_count() -> int
get_negative_count() -> int
get_neutral_count() -> int
get_north_count() -> int
get_south_count() -> int
get_magnet_count() -> int
get_ring_count() -> int
get_locked_count() -> int    # Particles + rings
get_avg_speed() -> float     # Particles + rings
get_total_kinetic_energy() -> float  # Particles + rings
get_paused() -> bool
get_topology() -> int
get_sim_time() -> float
get_show_e_field() -> bool   # (and b, a, c, s variants)
get_e_lines_per_charge() -> int  # (and b, a, c, s variants)
```

### Data Export
```gdscript
get_particle_data() -> Array[Dictionary]
    # Each: {pos, vel, angular_vel, speed, charge, mag_charge, mass, radius, locked, orientation}

get_ring_data() -> Array[Dictionary]
    # Each: {pos, vel, angular_vel, orientation, major_radius, minor_radius,
    #        source_type, strength, mass, moment_of_inertia, locked}
```

### Tunables (set/get pairs)
```gdscript
set_k_coulomb(v) / get_k_coulomb()
set_k_biot(v) / get_k_biot()
set_k_dipole(v) / get_k_dipole()
set_k_lorentz(v) / get_k_lorentz()
set_k_mag_coulomb(v) / get_k_mag_coulomb()
set_k_dual_lorentz(v) / get_k_dual_lorentz()
set_k_dual_biot(v) / get_k_dual_biot()
set_max_speed(v) / get_max_speed()
set_global_damp(v) / get_global_damp()
set_soften_eps2(v) / get_soften_eps2()
set_fixed_dt(v) / get_fixed_dt()
```

### Input Gating
```gdscript
set_input_enabled(enabled: bool)  # Shell disengages sim keys
get_input_enabled() -> bool
```

### Signals
```gdscript
topology_changed(mode: int)
simulation_reset()
particle_spawned(charge: int)
monopole_spawned(mag_charge: int)
magnet_spawned()
ring_spawned(source_type: int)
binding_event(new_neutral_count: int)
```

---

## Architecture Notes

- All physics runs in `_physics_process` via fixed-timestep accumulator
- Field lines rebuild on a configurable interval (`field_rebuild_interval = 0.016`)
- Field rebuild uses a shared macro (`FIELD_REBUILD_BEGIN/END`) with a trace lambda that handles adaptive stepping, anti-crumple, decay cutoff, boundary awareness, and arrow collection
- Torus mesh is generated procedurally via `build_torus_mesh()` (static method, ArrayMesh with positions + normals + indices)
- Ring orientation stored as Quaternion, converted to Basis for transforms and field computations
- Quaternion integration uses explicit Hamilton product: `dq/dt = ½(ωx,ωy,ωz,0)·q`, renormalized each step
- The `too_close` check in field tracing handles toroidal geometry: transforms to ring-local space, projects onto major circle, checks tube distance
- Collision detection is O(n²) for particles, O(rings × particles) for ring-particle, O(rings²) with broad-phase culling for ring-ring

---

## Development History (incremental steps)

1. **Particle locking + orientation** — `locked` bool, `Quaternion orient`, locked particles skip integration / act as infinite mass
2. **ToroidalRing struct** — Data, spawning, procedural torus mesh, shell `ring` command, HUD display
3. **Ring field contributions** — N-sample discrete element approach for E, B, A, C fields from all ring source types. Field line seeding for rings.
4. **Ring linear dynamics** — Force computation, velocity integration, boundary handling, ring-particle collisions with toroidal distance
5. **Rotational dynamics** — Angular velocity, torque from fields (`Σ lever × dF`), moment of inertia, quaternion orientation integration for both particles and rings
6. **Ring-ring collisions** — Broad/narrow phase with 16-sample overlap detection, impulse + separation, collision-induced spin on both rings. `lockring`/`unlockring` shell commands.
7. **Far-field optimization + self-exclusion** — Dipole approximation for rings at distance > 3R. `exclude_ring` parameter on all field functions. `info` shell command.
8. **Visual indicators + particle friction spin** — Ring axis indicator lines (bright/dim + crosshair). Particle-particle glancing collisions transfer angular momentum via friction. Ring-particle collisions also transfer friction spin.
9. **`list`/`delete` commands + spin indicators** — `remove_particle/ring/magnet` C++ methods. `list` overview, `delete` by index. Particle spin axis lines (visible when spinning).
10. **File-based scene scripting** — `exec` command loads `.sim` files. `#` comments, `wait N` for timed sequences, `echo` for output. Example scene files included.

---

## Potential Future Work

- **Spinning charge → dipole feedback**: Spinning charged particles act as magnetic dipoles (`m ≈ qωr²/3`), feeding back into B field
- **Variables and expressions**: `set R 40`, `spawn + $CX,$CY,$CZ 0,0,0` in the script language
- **Loops**: `for i 1 6 ring I! 500,500,{380+i*40} 35 6 4.0`
- **Procedure definitions**: `def solenoid cx cy cz n ...`
- **Adaptive sub-stepping**: Sub-step rings experiencing high angular acceleration
- **Particle-particle spin from EM torque**: Spinning monopoles experiencing torque from B field gradients
