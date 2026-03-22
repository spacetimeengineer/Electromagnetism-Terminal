#pragma once

#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/quaternion.hpp>

namespace godot {

class ChargeSimulator3D : public Node3D {
    GDCLASS(ChargeSimulator3D, Node3D)

public:
    // Topology modes
    enum Topology { TOPO_TORUS = 0, TOPO_SPHERE = 1, TOPO_SPHERE_WRAP = 2, TOPO_OPEN = 3 };

    // Ring source types
    enum RingSourceType { RING_CHARGE = 0, RING_CURRENT = 1, RING_MONOPOLE = 2 };

    ChargeSimulator3D();
    ~ChargeSimulator3D() override = default;

    void _ready() override;
    void _physics_process(double delta) override;

    void reset_world();
    void clear_neutrals();
    void add_particle(Vector3 p_pos, Vector3 p_vel, int p_charge, bool p_locked = false);
    void add_monopole(Vector3 p_pos, Vector3 p_vel, int p_mag_charge, bool p_locked = false);
    void add_bar_magnet(Vector3 p_pos, Vector3 p_dir, double p_strength);
    void lock_particle(int index, bool locked);
    bool is_particle_locked(int index) const;

    // Toroidal rings
    void add_ring(Vector3 p_pos, double p_major, double p_minor,
                  int p_source_type, double p_strength,
                  Vector3 p_axis = Vector3(0,1,0), bool p_locked = false);
    int get_ring_count() const;
    Array get_ring_data() const;
    void lock_ring(int index, bool locked);
    bool is_ring_locked(int index) const;

    // Removal
    void remove_particle(int index);
    void remove_ring(int index);
    void remove_magnet(int index);

    // Input gating (shell disengages sim keys)
    void set_input_enabled(bool enabled);
    bool get_input_enabled() const;

    // Getters for HUD
    int get_particle_count() const;
    int get_positive_count() const;
    int get_negative_count() const;
    int get_neutral_count() const;
    int get_north_count() const;
    int get_south_count() const;
    int get_magnet_count() const;
    int get_locked_count() const;
    double get_avg_speed() const;
    double get_total_kinetic_energy() const;
    bool get_paused() const;
    int get_topology() const;
    bool get_show_e_field() const;
    bool get_show_b_field() const;
    bool get_show_a_field() const;
    bool get_show_c_field() const;
    bool get_show_s_field() const;
    bool get_show_grid() const;
    int get_e_lines_per_charge() const;
    int get_b_lines_per_magnet() const;
    int get_a_lines_per_source() const;
    int get_c_lines_per_source() const;
    int get_s_lines_per_source() const;
    double get_sim_time() const;

    Array get_particle_data() const;

    // Tunable setters/getters
    void set_k_coulomb(double v);     double get_k_coulomb() const;
    void set_k_biot(double v);        double get_k_biot() const;
    void set_k_dipole(double v);      double get_k_dipole() const;
    void set_k_lorentz(double v);     double get_k_lorentz() const;
    void set_k_mag_coulomb(double v); double get_k_mag_coulomb() const;
    void set_max_speed(double v);     double get_max_speed() const;
    void set_global_damp(double v);   double get_global_damp() const;
    void set_soften_eps2(double v);   double get_soften_eps2() const;
    void set_fixed_dt(double v);      double get_fixed_dt() const;
    void set_k_dual_lorentz(double v);double get_k_dual_lorentz() const;
    void set_k_dual_biot(double v);   double get_k_dual_biot() const;

protected:
    static void _bind_methods();

private:
    struct Particle {
        Vector3 pos;
        Vector3 vel;
        Vector3 angular_vel;  // angular velocity (world-space)
        int charge;
        int mag_charge;
        float mass;
        float radius;
        bool locked = false;
        Quaternion orient;  // orientation (identity by default)
    };

    // Tunables
    Vector3 box_size = Vector3(1000, 1000, 1000);

    int init_positive = 0;
    int init_negative = 0;
    int init_neutral  = 0;
    int init_north    = 0;
    int init_south    = 0;

    double fixed_dt = 0.016;

    double k_coulomb = 100000.0;
    double soften_eps2 = 8.0;
    double global_damp = 0.999;
    double max_speed = 600.0;
    double max_angular_speed = 10.0;  // rad/s cap for ring rotation

    double mass_pos = 1.0;
    double mass_neg = 1.0;
    double mass_north = 1.0;
    double mass_south = 1.0;
    double radius_north = 6.0;
    double radius_south = 6.0;
    double k_mag_coulomb = 100000.0;

    double neu_mass_per_radius = 0.2;

    double bind_radius = 18.0;
    double bind_relax  = 1.0;
    double bind_mag_radius = 18.0;
    double bind_mag_relax  = 1.0;

    double radius_pos = 6.0;
    double radius_neg = 6.0;
    double radius_neu = 5.0;

    // Topology
    Topology topology = TOPO_TORUS;

    // Sphere boundary (derived from box_size)
    Vector3 sphere_center() const;
    float sphere_radius() const;
    bool point_inside_boundary(const Vector3 &p) const;
    void reflect_at_sphere(Vector3 &pos, Vector3 &vel) const;
    void wrap_at_sphere(Vector3 &pos) const;

    // Grid
    bool show_grid = false;
    float grid_spacing = 100.0f;
    MeshInstance3D *grid_mi = nullptr;
    Ref<ImmediateMesh> grid_mesh;
    Ref<StandardMaterial3D> grid_mat;
    void ensure_grid_nodes();
    void rebuild_grid();

    // Magnets
    struct Magnet {
        Vector3 pos;
        Vector3 m;
        float radius;
        float length = 80.0f;
        float thickness = 18.0f;
        MeshInstance3D *visual = nullptr;
    };
    std::vector<Magnet> magnets;

    // Toroidal rings
    struct ToroidalRing {
        Vector3 pos;
        Vector3 vel;
        Vector3 angular_vel;  // angular velocity (world-space, rad/s)
        Quaternion orient;
        float major_radius;
        float minor_radius;
        RingSourceType source_type = RING_CHARGE;
        float strength = 1.0f;
        float mass = 5.0f;
        float moment_of_inertia = 1.0f;  // scalar I for rotation
        bool locked = false;
        MeshInstance3D *visual = nullptr;
    };
    std::vector<ToroidalRing> rings;
    static Ref<ArrayMesh> build_torus_mesh(float major_r, float minor_r,
                                            int ring_segments = 32,
                                            int tube_segments = 16);

    double k_biot = 25.0;
    double k_dipole = 5000.0;
    double k_lorentz = 1.0;
    double k_dual_lorentz = 1.0;
    double k_dual_biot = 25.0;
    double max_mag_accel = 5000.0;

    // E field
    bool show_field_lines = false;
    int field_steps = 80;
    float field_ds = 18.0f;
    float field_stop_r = 10.0f;
    int field_lines_per_charge = 12;
    int e_lines_per_monopole = 4;
    float field_seed_gap = 2.0f;
    MeshInstance3D *field_mi = nullptr;
    Ref<ImmediateMesh> field_mesh;
    Ref<StandardMaterial3D> field_mat;
    Vector3 compute_e_field_at(const Vector3 &p, int exclude_particle = -1, int exclude_ring = -1) const;
    void ensure_field_nodes();
    void rebuild_field_lines();

    // B field
    bool show_b_field_lines = false;
    int b_field_steps = 80;
    float b_field_ds = 18.0f;
    float b_field_stop_buffer = 2.0f;
    int b_lines_per_magnet = 6;
    int b_lines_per_charge = 4;
    int b_lines_per_monopole = 8;
    float b_seed_gap = 3.0f;
    MeshInstance3D *bfield_mi = nullptr;
    Ref<ImmediateMesh> bfield_mesh;
    Ref<StandardMaterial3D> bfield_mat;
    Vector3 compute_b_field_at(const Vector3 &p, int exclude_particle = -1, int exclude_ring = -1) const;
    void ensure_bfield_nodes();
    void rebuild_b_field_lines();

    // A field (magnetic vector potential)
    bool show_a_field_lines = false;
    int a_field_steps = 80;
    float a_field_ds = 18.0f;
    float a_field_stop_buffer = 2.0f;
    int a_lines_per_magnet = 8;
    int a_lines_per_charge = 4;
    float a_seed_gap = 3.0f;
    double k_a_dipole = 5000.0;
    double k_a_biot = 25.0;
    MeshInstance3D *afield_mi = nullptr;
    Ref<ImmediateMesh> afield_mesh;
    Ref<StandardMaterial3D> afield_mat;
    Vector3 compute_a_field_at(const Vector3 &p, int exclude_particle = -1, int exclude_ring = -1) const;
    void ensure_afield_nodes();
    void rebuild_a_field_lines();

    // C field (electric vector potential)
    bool show_c_field_lines = false;
    int c_field_steps = 80;
    float c_field_ds = 18.0f;
    float c_field_stop_buffer = 2.0f;
    int c_lines_per_monopole = 8;
    float c_seed_gap = 3.0f;
    double k_c_mono = 25.0;
    MeshInstance3D *cfield_mi = nullptr;
    Ref<ImmediateMesh> cfield_mesh;
    Ref<StandardMaterial3D> cfield_mat;
    Vector3 compute_c_field_at(const Vector3 &p, int exclude_particle = -1, int exclude_ring = -1) const;
    void ensure_cfield_nodes();
    void rebuild_c_field_lines();

    // S field (Poynting vector, E × B)
    bool show_s_field_lines = false;
    int s_field_steps = 60;
    float s_field_ds = 18.0f;
    float s_field_stop_buffer = 2.0f;
    int s_lines_per_source = 6;
    float s_seed_gap = 3.0f;
    MeshInstance3D *sfield_mi = nullptr;
    Ref<ImmediateMesh> sfield_mesh;
    Ref<StandardMaterial3D> sfield_mat;
    Vector3 compute_s_field_at(const Vector3 &p) const;
    void ensure_sfield_nodes();
    void rebuild_s_field_lines();

    // Realtime rebuild
    bool field_rebuild_realtime = true;
    double field_rebuild_interval = 0.016;
    double field_rebuild_accum = 0.0;

    // Core state
    std::vector<Particle> particles;
    double accumulator = 0.0;
    double sim_time = 0.0;
    bool paused = false;
    bool input_enabled = true;

    // Key edge detection
    bool prev_space = false;
    bool prev_r = false;
    bool prev_c = false;
    bool prev_1 = false;
    bool prev_2 = false;
    bool prev_3 = false;
    bool prev_4 = false;
    bool prev_5 = false;
    bool prev_f = false;
    bool prev_t = false;
    bool prev_minus = false;
    bool prev_equal = false;
    bool prev_g = false;
    bool prev_m = false;
    bool prev_lbracket = false;
    bool prev_rbracket = false;
    bool prev_semicolon = false;
    bool prev_apostrophe = false;
    bool prev_o = false;
    bool prev_v = false;
    bool prev_comma = false;
    bool prev_period = false;
    bool prev_j = false;
    bool prev_9 = false;
    bool prev_0 = false;
    bool prev_x = false;
    bool prev_p = false;      // Poynting vector toggle
    bool prev_6 = false;      // Ring spawn

    // Rendering
    MultiMeshInstance3D *mmi_pos = nullptr;
    MultiMeshInstance3D *mmi_neg = nullptr;
    MultiMeshInstance3D *mmi_neu = nullptr;
    MultiMeshInstance3D *mmi_north = nullptr;
    MultiMeshInstance3D *mmi_south = nullptr;

    // Ring axis indicators
    MeshInstance3D *ring_axis_mi = nullptr;
    Ref<ImmediateMesh> ring_axis_mesh;
    Ref<StandardMaterial3D> ring_axis_mat;

    // Particle spin indicators
    MeshInstance3D *spin_mi = nullptr;
    Ref<ImmediateMesh> spin_mesh;
    Ref<StandardMaterial3D> spin_mat;

    void step_fixed();
    void compute_accelerations(std::vector<Vector3> &out_accel) const;

    void wrap_position(Vector3 &p) const;
    Vector3 min_image_vec(const Vector3 &d) const;

    void draw_field_segment(const Ref<ImmediateMesh> &mesh,
                            const Vector3 &a, const Vector3 &b) const;

    void limit_speed(Vector3 &v, double vmax) const;
    void handle_collisions();
    void do_binding();

    float neutral_mass_from_radius(float r) const;
    float neutral_radius_from_mass(float m) const;

    void ensure_render_nodes();
    void update_render();

    // Shared rebuild helpers
    void rebuild_all_fields();
};

} // namespace godot