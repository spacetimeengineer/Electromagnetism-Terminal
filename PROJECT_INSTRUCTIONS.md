# Claude Project Instructions — Charge Simulator 3D

You are working on a Godot 4 GDExtension electromagnetic simulator written in C++ (godot-cpp) with GDScript UI. The project is at `~/charge-sim-3d/addons/charge_sim_3d`.

## Critical Development Rules

1. **Incremental only**: Propose a small, well-defined change. Implement it. Never attempt batch rewrites.
2. **Preserve everything**: Every existing feature must continue working. No regressions.
3. **Test with specifics**: After changes, suggest concrete shell commands to validate.
4. **Physics first**: This is a physics simulator, not a game. Get the equations right. Use proper approximations (dipole far-field, not hand-waves).
5. **Match existing style**: Dense, compact C++. See CODING_CONVENTIONS.md for patterns.

## Key Architecture Facts

- `ChargeSimulator3D` (Node3D) is the single C++ class containing all physics, rendering, and state
- Three object types: `Particle` (point, MultiMesh rendered), `Magnet` (static dipole, BoxMesh), `ToroidalRing` (full dynamics, procedural ArrayMesh)
- Five field types: E, B, A, C, S — all computed per-point with particle and ring exclusion indices
- Rings use N-sample discrete element approximation near-field, dipole/point approximation far-field (crossover at 3× major_radius)
- Quaternion orientation with Hamilton-product integration: `dq/dt = ½ω̃q`
- Fixed timestep Euler integration with accumulator
- Both SimHUD.gd and SimSHELL.gd implement the same shell command language — changes to commands must be mirrored in both files
- Shell supports file-based scripting via `exec file.sim` with `#` comments, `wait N`, `echo`

## Physics Model

Electric-magnetic dual symmetry throughout:
- Charges source E (Coulomb) and, when moving, B (Biot-Savart)
- Monopoles source B (monopole Coulomb) and, when moving, E (dual Biot-Savart)
- Current rings source B (Biot-Savart from I·dl) and A (vector potential)
- Lorentz force on charges: `F = qE + qv×B`
- Dual Lorentz on monopoles: `F = gB - gv×E`
- Laplace force on current elements: `dF = I·dl × B`
- Poynting vector: `S = E × B`

## What's In the Project Files

- `SIMULATOR_REFERENCE.md` — Complete feature reference (all struct fields, all commands, full API)
- `ROADMAP.md` — Design intent, scripting language evolution plan, future physics features
- `CODING_CONVENTIONS.md` — Style patterns, naming, gotchas
- `src/` — The 4 modified source files + 3 unchanged pass-throughs
- `examples/` — .sim scene script files

## When Starting Work

1. Read the relevant source files before making changes
2. Identify which files need modification (often just charge_simulator_3d.cpp, sometimes .h, sometimes both GDScript shells)
3. State the proposed change clearly before implementing
4. Keep diffs minimal — touch only what's necessary
