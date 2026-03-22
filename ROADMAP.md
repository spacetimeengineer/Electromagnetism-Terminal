# Development Roadmap & Design Intent

## Project Identity

This is Michael's electromagnetic simulator — a physics sandbox for exploring electromagnetism, magnetic monopoles, and their dual symmetry. It is NOT a game engine physics system; it models actual EM field equations with tunable constants. The simulation supports Michael's decade-long theoretical physics work on unifying gravitation and electromagnetism via complexified Einstein field equations (g̃_μν = g_μν + iB_μν).

## Core Design Principles

1. **Unix philosophy**: Small, composable pieces. The `aa-*` naming convention and modular approach extends to the sim's architecture.
2. **Incremental changes only**: Never rewrite large sections. Propose a small change, implement it, validate, continue.
3. **Preserve existing functionality**: Every change must not break what already works. Test with existing scenarios.
4. **Physics correctness over performance**: Get the physics right first, optimize second. But when optimizing, use proper approximations (e.g., far-field dipole) not hacks.
5. **Dual symmetry**: The simulator treats electric and magnetic as symmetric. Magnetic monopoles exist alongside charges. Every electric equation has a magnetic dual (Biot-Savart ↔ dual Biot-Savart, Lorentz ↔ dual Lorentz, A field ↔ C field).

## Interpreter / Scripting Language Direction

Michael wants an OpenSCAD-style interpreter — declarative scene description files that can be loaded to construct complex electromagnetic configurations. Current state is a shell command language with file loading (`exec`). The planned evolution:

### Phase 1 (DONE): File loading
- `exec file.sim` loads and runs scripts
- `#` comments, `wait N` for timed sequences
- All shell commands work in files

### Phase 2 (NEXT): Variables and expressions
- `set NAME value` — define variables
- `$NAME` or `${NAME}` — variable substitution in any argument
- Arithmetic: `$(expr)` for inline math, e.g. `$(500 + R * cos(theta))`
- This enables parameterized scenes

### Phase 3: Control flow
- `for VAR START END [STEP] ... end` — counted loops
- `if CONDITION ... [else ...] end` — conditionals
- This enables procedural construction (solenoids, lattices, helical arrangements)

### Phase 4: Procedures
- `def NAME [args] ... end` — reusable procedures
- `call NAME [args]` — invocation
- This enables library-style scene components

### Phase 5: Includes and modules
- `include file.sim` — textual include (vs `exec` which runs at current time)
- Standard library of common configurations (solenoid, Helmholtz pair, charge lattice)

## Physics Features To Implement

### Near-term
- **Spinning charge → magnetic dipole**: A spinning charged sphere has moment `m ≈ qωr²/3`. This should feed into `compute_b_field_at`. Small change to `compute_accelerations` and the B field function.
- **Particle-particle EM torque**: Charged particles near field gradients should experience torque (beyond friction). This is subtle — point charges don't have intrinsic EM torque, but extended charged objects do. Consider whether particles should be treated as small current loops when spinning.

### Medium-term
- **Ring current from spinning charge rings**: A charge ring spinning around its own axis is equivalent to a current ring. The `source_type` could dynamically blend based on angular velocity.
- **Induced EMF / Faraday's law effects**: A changing B field through a ring should induce current. This is complex but physically important.
- **Radiation damping**: Accelerating charges radiate. A simple Abraham-Lorentz-like damping term.

### Long-term (connected to theoretical framework)
- **Complexified field visualization**: Visualize the imaginary part B_μν of the complexified metric alongside the real part. Show how EM stress-energy emerges geometrically.
- **Gravitational coupling**: If the unified theory is correct, strong EM fields should produce weak gravitational effects. Add a test mode for this.

## Performance Considerations

- Current bottleneck: O(N²) force computation for particles, O(rings × samples × sources) for ring forces
- Far-field dipole approximation (implemented) helps for rings at distance
- Next optimization: spatial hashing or octree for particle-particle forces
- Threading: the force computation loop is embarrassingly parallel — Godot's WorkerThreadPool could parallelize it
- GPU compute: field line tracing could be done on GPU via compute shaders, but this is a major architectural change

## UI/UX Direction

- The HUD (SimHUD.gd) is the primary interface — physics panel, model equations, keybinds, control sliders, shell, log, toast
- The shell is evolving into a full interpreter (see scripting roadmap above)
- Consider: visual scene editor (click to place objects, drag to set velocity) as an alternative to shell-only construction
- Consider: recording/playback of simulation states for reproducibility

## File Organization

```
~/charge-sim-3d/
├── addons/charge_sim_3d/
│   ├── src/                    # C++ source
│   │   ├── charge_simulator_3d.h
│   │   ├── charge_simulator_3d.cpp
│   │   └── register_types.cpp
│   ├── bin/                    # Compiled .so/.dll/.dylib
│   └── charge_sim_3d.gdextension
├── scripts/                    # GDScript
│   ├── SimHUD.gd
│   ├── SimSHELL.gd
│   ├── SimControlPanel.gd
│   └── ShipController.gd
├── examples/                   # .sim scene scripts
│   ├── dipole_demo.sim
│   ├── solenoid.sim
│   └── collision_stress.sim
├── SConstruct                  # Build file
└── project.godot
```

(Paths may differ — check Michael's actual layout)
