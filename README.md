# Charge Simulator 3D

A real-time 3D electromagnetic simulator built as a **Godot 4 GDExtension** plugin (C++/godot-cpp). Models electric charges, magnetic monopoles, bar magnets, and toroidal rings with five field types, full collision dynamics, angular momentum, and an in-game scripting interpreter.

## Features

- **Five particle types**: positive/negative charges, neutrals, north/south magnetic monopoles
- **Bar magnets**: static magnetic dipoles with dipole field formula
- **Toroidal rings**: thick torus objects with three source modes (charge, current, monopole), full linear + angular dynamics, procedural mesh generation
- **Five field visualizations**: E (electric), B (magnetic), A (vector potential), C (electric vector potential), S (Poynting vector) — all with adaptive field line tracing, arrows, decay cutoff
- **Collision system**: particle-particle, ring-particle, ring-ring with elastic impulse, friction-based spin transfer, locked-object support
- **Angular momentum**: quaternion orientation, torque from fields, collision-induced spin, visual spin indicators
- **Four boundary topologies**: 3-torus (periodic), reflective sphere, antipodal-wrap sphere, open
- **Particle binding**: opposite charges/monopoles merge into neutrals within binding radius
- **In-game shell**: full command language with file-based scene scripting (`.sim` files), `wait` for timed sequences, `#` comments, `echo`, object queries, deletion
- **Far-field optimization**: rings beyond 3× major radius use dipole/point approximations instead of N-sample sums
- **11 tunable physics constants**: adjustable at runtime via control panel sliders

## Building

Requires [godot-cpp](https://github.com/godotengine/godot-cpp) as a submodule or sibling directory.

```bash
scons platform=linux target=template_debug    # or macos, windows
```

## Quick Start

1. Open the project in Godot 4
2. Run the main scene
3. Press number keys to spawn particles: `1` +charge, `2` -charge, `3` neutral, `4` N monopole, `5` S monopole, `6` ring, `M` magnet
4. Press `F` for E field lines, `G` for B field lines
5. Press `` ` `` to open the shell — type `help` for commands
6. Load a scene script: `exec examples/solenoid.sim`

## Project Structure

```
├── src/                            # C++ plugin source
│   ├── charge_simulator_3d.h       # Header — structs, enums, declarations
│   ├── charge_simulator_3d.cpp     # Implementation — physics, fields, rendering
│   └── register_types.cpp          # GDExtension registration
├── scripts/                        # GDScript
│   ├── SimHUD.gd                   # Primary UI (HUD, shell, controls, log)
│   ├── SimSHELL.gd                 # Standalone shell (legacy)
│   ├── SimControlPanel.gd          # Standalone control panel (legacy)
│   └── ShipController.gd           # FPS camera controller
├── examples/                       # Scene scripts (.sim files)
│   ├── dipole_demo.sim
│   ├── solenoid.sim
│   └── collision_stress.sim
├── docs/
│   ├── SIMULATOR_REFERENCE.md      # Complete feature reference
│   ├── ROADMAP.md                  # Design intent and future plans
│   └── CODING_CONVENTIONS.md       # Style guide and gotchas
└── .gitignore
```

## Documentation

- **[SIMULATOR_REFERENCE.md](docs/SIMULATOR_REFERENCE.md)** — Every struct field, all commands, full GDScript API, keyboard shortcuts, physics model details
- **[ROADMAP.md](docs/ROADMAP.md)** — Scripting language evolution plan, future physics features, performance considerations
- **[CODING_CONVENTIONS.md](docs/CODING_CONVENTIONS.md)** — C++ and GDScript style patterns, naming conventions, known gotchas

## Shell Commands (summary)

```
spawn +|-|o|N|S  pos  vel  [@time]     # Spawn particle (! to lock)
ring  +|-|I|N|S  pos  R r  [str] [axis] [!]  # Spawn ring
magnet  pos  dir  strength               # Spawn bar magnet
lock/unlock  index|all                   # Lock/unlock particles
lockring/unlockring  index|all           # Lock/unlock rings
info  particle|ring  index               # Inspect object state
list  [particles|rings|magnets]          # Overview of all objects
delete  particle|ring|magnet  index      # Remove object
exec  filepath.sim                       # Run script file
echo  text                               # Print to shell
repeat  N  command                       # Repeat command N times
reset / clear / time / queue / flush     # Simulation control
```

## License

Private project.
