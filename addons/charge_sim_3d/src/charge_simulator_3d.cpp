#include "charge_simulator_3d.h"

#include <cmath>
#include <algorithm>
#include <tuple>
#include <unordered_set>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/classes/environment.hpp>

namespace godot {

// =====================================================================
// File-scope helpers
// =====================================================================

static inline Vector3 min_image_delta_3d(const Vector3 &d, const Vector3 &L) {
    Vector3 out = d;
    if (out.x >  0.5f * L.x) out.x -= L.x;
    if (out.x < -0.5f * L.x) out.x += L.x;
    if (out.y >  0.5f * L.y) out.y -= L.y;
    if (out.y < -0.5f * L.y) out.y += L.y;
    if (out.z >  0.5f * L.z) out.z -= L.z;
    if (out.z < -0.5f * L.z) out.z += L.z;
    return out;
}

static void emit_arrow(const Ref<ImmediateMesh> &mesh, const Vector3 &pos,
                        const Vector3 &dir, float size = 8.0f) {
    Vector3 d = dir.normalized();
    if (d.length() < 1e-6f) return;
    Vector3 tip = pos + d * size;
    Vector3 up = Vector3(0, 1, 0);
    if (Math::abs(d.dot(up)) > 0.95f) up = Vector3(1, 0, 0);
    Vector3 p1 = up.cross(d).normalized();
    Vector3 p2 = d.cross(p1).normalized();
    float w = size * 0.35f;
    mesh->surface_add_vertex(tip);
    mesh->surface_add_vertex(pos + p1 * w);
    mesh->surface_add_vertex(pos - p1 * w);
    mesh->surface_add_vertex(tip);
    mesh->surface_add_vertex(pos + p2 * w);
    mesh->surface_add_vertex(pos - p2 * w);
}

// =====================================================================
// Construction / binding
// =====================================================================

ChargeSimulator3D::ChargeSimulator3D() {}

void ChargeSimulator3D::_bind_methods() {
    ClassDB::bind_method(D_METHOD("reset_world"), &ChargeSimulator3D::reset_world);
    ClassDB::bind_method(D_METHOD("clear_neutrals"), &ChargeSimulator3D::clear_neutrals);
    ClassDB::bind_method(D_METHOD("add_particle", "pos", "vel", "charge", "locked"), &ChargeSimulator3D::add_particle, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("add_monopole", "pos", "vel", "mag_charge", "locked"), &ChargeSimulator3D::add_monopole, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("add_bar_magnet", "pos", "dir", "strength"), &ChargeSimulator3D::add_bar_magnet);
    ClassDB::bind_method(D_METHOD("lock_particle", "index", "locked"), &ChargeSimulator3D::lock_particle);
    ClassDB::bind_method(D_METHOD("is_particle_locked", "index"), &ChargeSimulator3D::is_particle_locked);
    ClassDB::bind_method(D_METHOD("set_input_enabled", "enabled"), &ChargeSimulator3D::set_input_enabled);
    ClassDB::bind_method(D_METHOD("get_input_enabled"), &ChargeSimulator3D::get_input_enabled);

    ClassDB::bind_method(D_METHOD("get_particle_count"), &ChargeSimulator3D::get_particle_count);
    ClassDB::bind_method(D_METHOD("get_positive_count"), &ChargeSimulator3D::get_positive_count);
    ClassDB::bind_method(D_METHOD("get_negative_count"), &ChargeSimulator3D::get_negative_count);
    ClassDB::bind_method(D_METHOD("get_neutral_count"), &ChargeSimulator3D::get_neutral_count);
    ClassDB::bind_method(D_METHOD("get_north_count"), &ChargeSimulator3D::get_north_count);
    ClassDB::bind_method(D_METHOD("get_south_count"), &ChargeSimulator3D::get_south_count);
    ClassDB::bind_method(D_METHOD("get_magnet_count"), &ChargeSimulator3D::get_magnet_count);
    ClassDB::bind_method(D_METHOD("get_locked_count"), &ChargeSimulator3D::get_locked_count);
    ClassDB::bind_method(D_METHOD("get_avg_speed"), &ChargeSimulator3D::get_avg_speed);
    ClassDB::bind_method(D_METHOD("get_total_kinetic_energy"), &ChargeSimulator3D::get_total_kinetic_energy);
    ClassDB::bind_method(D_METHOD("get_paused"), &ChargeSimulator3D::get_paused);
    ClassDB::bind_method(D_METHOD("get_topology"), &ChargeSimulator3D::get_topology);
    ClassDB::bind_method(D_METHOD("get_show_e_field"), &ChargeSimulator3D::get_show_e_field);
    ClassDB::bind_method(D_METHOD("get_show_b_field"), &ChargeSimulator3D::get_show_b_field);
    ClassDB::bind_method(D_METHOD("get_show_a_field"), &ChargeSimulator3D::get_show_a_field);
    ClassDB::bind_method(D_METHOD("get_show_c_field"), &ChargeSimulator3D::get_show_c_field);
    ClassDB::bind_method(D_METHOD("get_show_s_field"), &ChargeSimulator3D::get_show_s_field);
    ClassDB::bind_method(D_METHOD("get_show_grid"), &ChargeSimulator3D::get_show_grid);
    ClassDB::bind_method(D_METHOD("get_e_lines_per_charge"), &ChargeSimulator3D::get_e_lines_per_charge);
    ClassDB::bind_method(D_METHOD("get_b_lines_per_magnet"), &ChargeSimulator3D::get_b_lines_per_magnet);
    ClassDB::bind_method(D_METHOD("get_a_lines_per_source"), &ChargeSimulator3D::get_a_lines_per_source);
    ClassDB::bind_method(D_METHOD("get_c_lines_per_source"), &ChargeSimulator3D::get_c_lines_per_source);
    ClassDB::bind_method(D_METHOD("get_s_lines_per_source"), &ChargeSimulator3D::get_s_lines_per_source);
    ClassDB::bind_method(D_METHOD("get_sim_time"), &ChargeSimulator3D::get_sim_time);
    ClassDB::bind_method(D_METHOD("get_particle_data"), &ChargeSimulator3D::get_particle_data);

    // Ring methods
    ClassDB::bind_method(D_METHOD("add_ring", "pos", "major_radius", "minor_radius", "source_type", "strength", "axis", "locked"), &ChargeSimulator3D::add_ring, DEFVAL(Vector3(0,1,0)), DEFVAL(false));
    ClassDB::bind_method(D_METHOD("get_ring_count"), &ChargeSimulator3D::get_ring_count);
    ClassDB::bind_method(D_METHOD("get_ring_data"), &ChargeSimulator3D::get_ring_data);
    ClassDB::bind_method(D_METHOD("lock_ring", "index", "locked"), &ChargeSimulator3D::lock_ring);
    ClassDB::bind_method(D_METHOD("is_ring_locked", "index"), &ChargeSimulator3D::is_ring_locked);
    ClassDB::bind_method(D_METHOD("remove_particle", "index"), &ChargeSimulator3D::remove_particle);
    ClassDB::bind_method(D_METHOD("remove_ring", "index"), &ChargeSimulator3D::remove_ring);
    ClassDB::bind_method(D_METHOD("remove_magnet", "index"), &ChargeSimulator3D::remove_magnet);

    // Tunable setters/getters
    ClassDB::bind_method(D_METHOD("set_k_coulomb", "value"), &ChargeSimulator3D::set_k_coulomb);
    ClassDB::bind_method(D_METHOD("get_k_coulomb"), &ChargeSimulator3D::get_k_coulomb);
    ClassDB::bind_method(D_METHOD("set_k_biot", "value"), &ChargeSimulator3D::set_k_biot);
    ClassDB::bind_method(D_METHOD("get_k_biot"), &ChargeSimulator3D::get_k_biot);
    ClassDB::bind_method(D_METHOD("set_k_dipole", "value"), &ChargeSimulator3D::set_k_dipole);
    ClassDB::bind_method(D_METHOD("get_k_dipole"), &ChargeSimulator3D::get_k_dipole);
    ClassDB::bind_method(D_METHOD("set_k_lorentz", "value"), &ChargeSimulator3D::set_k_lorentz);
    ClassDB::bind_method(D_METHOD("get_k_lorentz"), &ChargeSimulator3D::get_k_lorentz);
    ClassDB::bind_method(D_METHOD("set_k_mag_coulomb", "value"), &ChargeSimulator3D::set_k_mag_coulomb);
    ClassDB::bind_method(D_METHOD("get_k_mag_coulomb"), &ChargeSimulator3D::get_k_mag_coulomb);
    ClassDB::bind_method(D_METHOD("set_max_speed", "value"), &ChargeSimulator3D::set_max_speed);
    ClassDB::bind_method(D_METHOD("get_max_speed"), &ChargeSimulator3D::get_max_speed);
    ClassDB::bind_method(D_METHOD("set_global_damp", "value"), &ChargeSimulator3D::set_global_damp);
    ClassDB::bind_method(D_METHOD("get_global_damp"), &ChargeSimulator3D::get_global_damp);
    ClassDB::bind_method(D_METHOD("set_soften_eps2", "value"), &ChargeSimulator3D::set_soften_eps2);
    ClassDB::bind_method(D_METHOD("get_soften_eps2"), &ChargeSimulator3D::get_soften_eps2);
    ClassDB::bind_method(D_METHOD("set_fixed_dt", "value"), &ChargeSimulator3D::set_fixed_dt);
    ClassDB::bind_method(D_METHOD("get_fixed_dt"), &ChargeSimulator3D::get_fixed_dt);
    ClassDB::bind_method(D_METHOD("set_k_dual_lorentz", "value"), &ChargeSimulator3D::set_k_dual_lorentz);
    ClassDB::bind_method(D_METHOD("get_k_dual_lorentz"), &ChargeSimulator3D::get_k_dual_lorentz);
    ClassDB::bind_method(D_METHOD("set_k_dual_biot", "value"), &ChargeSimulator3D::set_k_dual_biot);
    ClassDB::bind_method(D_METHOD("get_k_dual_biot"), &ChargeSimulator3D::get_k_dual_biot);

    ADD_SIGNAL(MethodInfo("topology_changed", PropertyInfo(Variant::INT, "mode")));
    ADD_SIGNAL(MethodInfo("simulation_reset"));
    ADD_SIGNAL(MethodInfo("particle_spawned", PropertyInfo(Variant::INT, "charge")));
    ADD_SIGNAL(MethodInfo("monopole_spawned", PropertyInfo(Variant::INT, "mag_charge")));
    ADD_SIGNAL(MethodInfo("magnet_spawned"));
    ADD_SIGNAL(MethodInfo("ring_spawned", PropertyInfo(Variant::INT, "source_type")));
    ADD_SIGNAL(MethodInfo("binding_event", PropertyInfo(Variant::INT, "new_neutral_count")));
}

// =====================================================================
// Getters
// =====================================================================

void ChargeSimulator3D::set_input_enabled(bool e) { input_enabled = e; }
bool ChargeSimulator3D::get_input_enabled() const { return input_enabled; }

int ChargeSimulator3D::get_particle_count() const { return (int)particles.size(); }
int ChargeSimulator3D::get_positive_count() const {
    int n = 0; for (const auto &p : particles) if (p.charge > 0) n++; return n;
}
int ChargeSimulator3D::get_negative_count() const {
    int n = 0; for (const auto &p : particles) if (p.charge < 0) n++; return n;
}
int ChargeSimulator3D::get_neutral_count() const {
    int n = 0; for (const auto &p : particles) if (p.charge == 0 && p.mag_charge == 0) n++; return n;
}
int ChargeSimulator3D::get_north_count() const {
    int n = 0; for (const auto &p : particles) if (p.mag_charge > 0) n++; return n;
}
int ChargeSimulator3D::get_south_count() const {
    int n = 0; for (const auto &p : particles) if (p.mag_charge < 0) n++; return n;
}
int ChargeSimulator3D::get_magnet_count() const { return (int)magnets.size(); }
int ChargeSimulator3D::get_locked_count() const {
    int n = 0;
    for (const auto &p : particles) if (p.locked) n++;
    for (const auto &r : rings) if (r.locked) n++;
    return n;
}
void ChargeSimulator3D::lock_particle(int index, bool locked) {
    if (index >= 0 && index < (int)particles.size()) particles[index].locked = locked;
}
bool ChargeSimulator3D::is_particle_locked(int index) const {
    if (index >= 0 && index < (int)particles.size()) return particles[index].locked;
    return false;
}
int ChargeSimulator3D::get_ring_count() const { return (int)rings.size(); }
Array ChargeSimulator3D::get_ring_data() const {
    Array arr; arr.resize((int)rings.size());
    for (int i = 0; i < (int)rings.size(); i++) {
        const auto &r = rings[i]; Dictionary d;
        d["pos"] = r.pos; d["vel"] = r.vel; d["orientation"] = r.orient;
        d["major_radius"] = (double)r.major_radius;
        d["minor_radius"] = (double)r.minor_radius;
        d["source_type"] = (int)r.source_type;
        d["strength"] = (double)r.strength;
        d["mass"] = (double)r.mass; d["locked"] = r.locked;
        d["angular_vel"] = r.angular_vel;
        d["moment_of_inertia"] = (double)r.moment_of_inertia;
        arr[i] = d;
    }
    return arr;
}
void ChargeSimulator3D::lock_ring(int index, bool locked) {
    if (index >= 0 && index < (int)rings.size()) rings[index].locked = locked;
}
bool ChargeSimulator3D::is_ring_locked(int index) const {
    if (index >= 0 && index < (int)rings.size()) return rings[index].locked;
    return false;
}
void ChargeSimulator3D::remove_particle(int index) {
    if (index >= 0 && index < (int)particles.size()) {
        particles.erase(particles.begin() + index);
        rebuild_all_fields();
    }
}
void ChargeSimulator3D::remove_ring(int index) {
    if (index >= 0 && index < (int)rings.size()) {
        if (rings[index].visual) { rings[index].visual->queue_free(); rings[index].visual = nullptr; }
        rings.erase(rings.begin() + index);
        rebuild_all_fields();
    }
}
void ChargeSimulator3D::remove_magnet(int index) {
    if (index >= 0 && index < (int)magnets.size()) {
        if (magnets[index].visual) { magnets[index].visual->queue_free(); magnets[index].visual = nullptr; }
        magnets.erase(magnets.begin() + index);
        rebuild_all_fields();
    }
}
double ChargeSimulator3D::get_avg_speed() const {
    int total = (int)particles.size() + (int)rings.size();
    if (total == 0) return 0.0;
    double s = 0;
    for (const auto &p : particles) s += (double)p.vel.length();
    for (const auto &r : rings) s += (double)r.vel.length();
    return s / (double)total;
}
double ChargeSimulator3D::get_total_kinetic_energy() const {
    double ke = 0;
    for (const auto &p : particles) { double v = p.vel.length(); ke += 0.5 * p.mass * v * v; }
    for (const auto &r : rings) { double v = r.vel.length(); ke += 0.5 * r.mass * v * v; }
    return ke;
}
bool ChargeSimulator3D::get_paused() const { return paused; }
int ChargeSimulator3D::get_topology() const { return (int)topology; }
bool ChargeSimulator3D::get_show_e_field() const { return show_field_lines; }
bool ChargeSimulator3D::get_show_b_field() const { return show_b_field_lines; }
bool ChargeSimulator3D::get_show_a_field() const { return show_a_field_lines; }
bool ChargeSimulator3D::get_show_c_field() const { return show_c_field_lines; }
bool ChargeSimulator3D::get_show_s_field() const { return show_s_field_lines; }
bool ChargeSimulator3D::get_show_grid() const { return show_grid; }
int ChargeSimulator3D::get_e_lines_per_charge() const { return field_lines_per_charge; }
int ChargeSimulator3D::get_b_lines_per_magnet() const { return b_lines_per_magnet; }
int ChargeSimulator3D::get_a_lines_per_source() const { return a_lines_per_magnet; }
int ChargeSimulator3D::get_c_lines_per_source() const { return c_lines_per_monopole; }
int ChargeSimulator3D::get_s_lines_per_source() const { return s_lines_per_source; }
double ChargeSimulator3D::get_sim_time() const { return sim_time; }

Array ChargeSimulator3D::get_particle_data() const {
    Array arr; arr.resize((int)particles.size());
    for (int i = 0; i < (int)particles.size(); i++) {
        const auto &p = particles[i]; Dictionary d;
        d["pos"] = p.pos; d["vel"] = p.vel; d["speed"] = (double)p.vel.length();
        d["charge"] = p.charge; d["mag_charge"] = p.mag_charge;
        d["mass"] = (double)p.mass; d["radius"] = (double)p.radius;
        d["locked"] = p.locked; d["orientation"] = p.orient;
        d["angular_vel"] = p.angular_vel;
        arr[i] = d;
    }
    return arr;
}

// Tunable setters/getters
void ChargeSimulator3D::set_k_coulomb(double v)     { k_coulomb = v; }
double ChargeSimulator3D::get_k_coulomb() const      { return k_coulomb; }
void ChargeSimulator3D::set_k_biot(double v)         { k_biot = v; }
double ChargeSimulator3D::get_k_biot() const          { return k_biot; }
void ChargeSimulator3D::set_k_dipole(double v)       { k_dipole = v; }
double ChargeSimulator3D::get_k_dipole() const        { return k_dipole; }
void ChargeSimulator3D::set_k_lorentz(double v)      { k_lorentz = v; }
double ChargeSimulator3D::get_k_lorentz() const       { return k_lorentz; }
void ChargeSimulator3D::set_k_mag_coulomb(double v)  { k_mag_coulomb = v; }
double ChargeSimulator3D::get_k_mag_coulomb() const   { return k_mag_coulomb; }
void ChargeSimulator3D::set_max_speed(double v)      { max_speed = std::max(1.0, v); }
double ChargeSimulator3D::get_max_speed() const       { return max_speed; }
void ChargeSimulator3D::set_global_damp(double v)    { global_damp = v; }
double ChargeSimulator3D::get_global_damp() const     { return global_damp; }
void ChargeSimulator3D::set_soften_eps2(double v)    { soften_eps2 = std::max(0.01, v); }
double ChargeSimulator3D::get_soften_eps2() const     { return soften_eps2; }
void ChargeSimulator3D::set_fixed_dt(double v)       { fixed_dt = std::max(0.001, std::min(0.1, v)); }
double ChargeSimulator3D::get_fixed_dt() const        { return fixed_dt; }
void ChargeSimulator3D::set_k_dual_lorentz(double v) { k_dual_lorentz = v; }
double ChargeSimulator3D::get_k_dual_lorentz() const  { return k_dual_lorentz; }
void ChargeSimulator3D::set_k_dual_biot(double v)    { k_dual_biot = v; }
double ChargeSimulator3D::get_k_dual_biot() const     { return k_dual_biot; }

// =====================================================================
// Neutral helpers
// =====================================================================

float ChargeSimulator3D::neutral_mass_from_radius(float r) const {
    return (float)(neu_mass_per_radius * (double)r);
}
float ChargeSimulator3D::neutral_radius_from_mass(float m) const {
    if (neu_mass_per_radius <= 0.0) return 1.0f;
    return (float)((double)m / neu_mass_per_radius);
}

// =====================================================================
// Topology helpers
// =====================================================================

Vector3 ChargeSimulator3D::sphere_center() const {
    return box_size * 0.5f;
}

float ChargeSimulator3D::sphere_radius() const {
    return std::min({(float)box_size.x, (float)box_size.y, (float)box_size.z}) * 0.5f;
}

bool ChargeSimulator3D::point_inside_boundary(const Vector3 &p) const {
    switch (topology) {
        case TOPO_TORUS: {
            const float m = 20.0f;
            return p.x > m && p.x < box_size.x - m
                && p.y > m && p.y < box_size.y - m
                && p.z > m && p.z < box_size.z - m;
        }
        case TOPO_SPHERE:
        case TOPO_SPHERE_WRAP: {
            Vector3 d = p - sphere_center();
            return d.length() < sphere_radius() - 20.0f;
        }
        default: return true;
    }
}

void ChargeSimulator3D::reflect_at_sphere(Vector3 &pos, Vector3 &vel) const {
    Vector3 c = sphere_center();
    float R = sphere_radius();
    Vector3 d = pos - c;
    float dist = d.length();
    if (dist > R && dist > 1e-6f) {
        Vector3 n = d / dist;
        pos = c + n * (2.0f * R - dist);
        float vn = vel.dot(n);
        if (vn > 0.0f) vel -= n * (2.0f * vn);
    }
}

void ChargeSimulator3D::wrap_at_sphere(Vector3 &pos) const {
    Vector3 c = sphere_center();
    float R = sphere_radius();
    Vector3 d = pos - c;
    float dist = d.length();
    if (dist > R && dist > 1e-6f) {
        // Antipodal wrapping: exit at c + R*n, enter at c - R*n
        // with overshoot preserved on the other side
        Vector3 n = d / dist;
        float overshoot = dist - R;
        pos = c - n * (R - overshoot);
        // velocity direction is PRESERVED (no reflection)
    }
}

void ChargeSimulator3D::wrap_position(Vector3 &p) const {
    if (topology == TOPO_TORUS) {
        if (box_size.x > 0) p.x = std::fmod(std::fmod(p.x, box_size.x) + box_size.x, box_size.x);
        if (box_size.y > 0) p.y = std::fmod(std::fmod(p.y, box_size.y) + box_size.y, box_size.y);
        if (box_size.z > 0) p.z = std::fmod(std::fmod(p.z, box_size.z) + box_size.z, box_size.z);
    }
    // sphere reflect, sphere wrap, and open handled in step_fixed / wrap_at_sphere
}

Vector3 ChargeSimulator3D::min_image_vec(const Vector3 &d) const {
    if (topology == TOPO_TORUS) {
        Vector3 out = d;
        auto wc = [](double dx, double L) -> double {
            if (L <= 0.0) return dx;
            dx = std::fmod(dx + L * 0.5, L); if (dx < 0.0) dx += L; dx -= L * 0.5; return dx;
        };
        out.x = (float)wc(out.x, box_size.x);
        out.y = (float)wc(out.y, box_size.y);
        out.z = (float)wc(out.z, box_size.z);
        return out;
    }
    if (topology == TOPO_SPHERE_WRAP) {
        // The antipodal image of displacement d is the path through
        // the center: image_d = -(2*center_offset - d) where we compare
        // direct vs going through the antipodal point.
        // For two points p1, p2 inside the sphere: p2 = p1 + d
        // Antipodal image of p2 is p2' = 2*center - p2
        // So d' = p2' - p1 = 2*center - p2 - p1 = 2*center - (p1 + d) - p1
        // But we only have d, not p1. We need to just return d here —
        // the min_image optimization for sphere_wrap is less critical than torus
        // since particles are always inside the ball. Return direct displacement.
        return d;
    }
    return d; // TOPO_SPHERE, TOPO_OPEN
}

void ChargeSimulator3D::draw_field_segment(
    const Ref<ImmediateMesh> &mesh, const Vector3 &a, const Vector3 &b
) const {
    if (!point_inside_boundary(a) || !point_inside_boundary(b)) return;

    if (topology == TOPO_TORUS) {
        const Vector3 d_raw = b - a;
        const Vector3 L((float)box_size.x, (float)box_size.y, (float)box_size.z);
        const Vector3 d_min = min_image_delta_3d(d_raw, L);
        bool wrapped = (Math::abs(d_min.x - d_raw.x) > 1e-6f) ||
                       (Math::abs(d_min.y - d_raw.y) > 1e-6f) ||
                       (Math::abs(d_min.z - d_raw.z) > 1e-6f);
        if (wrapped) return;
    }

    mesh->surface_add_vertex(a);
    mesh->surface_add_vertex(b);
}

void ChargeSimulator3D::limit_speed(Vector3 &v, double vmax) const {
    double s = v.length(); if (s > vmax && s > 1e-12) v *= (float)(vmax / s);
}

void ChargeSimulator3D::rebuild_all_fields() {
    if (show_field_lines)   rebuild_field_lines();
    if (show_b_field_lines) rebuild_b_field_lines();
    if (show_a_field_lines) rebuild_a_field_lines();
    if (show_c_field_lines) rebuild_c_field_lines();
    if (show_s_field_lines) rebuild_s_field_lines();
}

// =====================================================================
// _ready
// =====================================================================

void ChargeSimulator3D::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) return;
    ensure_render_nodes();
    ensure_field_nodes();
    ensure_bfield_nodes();
    ensure_afield_nodes();
    ensure_cfield_nodes();
    ensure_sfield_nodes();
    ensure_grid_nodes();

    particles.clear(); magnets.clear(); rings.clear(); sim_time = 0.0;
    show_field_lines = false; show_b_field_lines = false;
    show_a_field_lines = false; show_c_field_lines = false;
    show_s_field_lines = false; show_grid = false;
    field_rebuild_realtime = true; field_rebuild_interval = 0.016;
    paused = false; input_enabled = true;

    // Create WorldEnvironment with subtle glow/bloom
    auto *we = memnew(WorldEnvironment);
    Ref<Environment> env;
    env.instantiate();
    env->set_background(Environment::BG_COLOR);
    env->set_bg_color(Color(0.02, 0.02, 0.03));
    env->set_glow_enabled(true);
    env->set_glow_intensity(0.4);
    env->set_glow_strength(1.0);
    env->set_glow_bloom(0.075);
    env->set_glow_blend_mode(Environment::GLOW_BLEND_MODE_ADDITIVE);
    env->set_glow_hdr_bleed_threshold(0.9);
    we->set_environment(env);
    add_child(we);
}

// =====================================================================
// _physics_process
// =====================================================================

void ChargeSimulator3D::_physics_process(double delta) {
    if (Engine::get_singleton()->is_editor_hint()) { update_render(); return; }

    Input *in = Input::get_singleton();
    auto just_pressed = [&](Key key, bool &prev) -> bool {
        bool now = in->is_key_pressed(key); bool jp = now && !prev; prev = now; return jp;
    };

    // --- Sim keys only when input enabled ---
    if (input_enabled) {
        if (just_pressed(Key::KEY_SPACE, prev_space)) paused = !paused;

        if (just_pressed(Key::KEY_R, prev_r)) {
            reset_world(); rebuild_all_fields(); emit_signal("simulation_reset");
        }
        if (just_pressed(Key::KEY_C, prev_c)) { clear_neutrals(); rebuild_all_fields(); }

        if (just_pressed(Key::KEY_X, prev_x)) { show_grid = !show_grid; rebuild_grid(); }

        if (just_pressed(Key::KEY_F, prev_f)) { show_field_lines = !show_field_lines; rebuild_field_lines(); field_rebuild_accum = 0; }
        if (just_pressed(Key::KEY_T, prev_t)) { field_rebuild_realtime = !field_rebuild_realtime; field_rebuild_accum = 0; }
        if (just_pressed(Key::KEY_MINUS, prev_minus)) { field_lines_per_charge = std::max(2, field_lines_per_charge - 2); if (show_field_lines) rebuild_field_lines(); }
        if (just_pressed(Key::KEY_EQUAL, prev_equal)) { field_lines_per_charge = std::min(128, field_lines_per_charge + 2); if (show_field_lines) rebuild_field_lines(); }

        if (just_pressed(Key::KEY_G, prev_g)) { show_b_field_lines = !show_b_field_lines; rebuild_b_field_lines(); field_rebuild_accum = 0; }
        if (just_pressed(Key::KEY_BRACKETLEFT, prev_lbracket)) { b_lines_per_magnet = std::max(2, b_lines_per_magnet - 2); if (show_b_field_lines) rebuild_b_field_lines(); }
        if (just_pressed(Key::KEY_BRACKETRIGHT, prev_rbracket)) { b_lines_per_magnet = std::min(64, b_lines_per_magnet + 2); if (show_b_field_lines) rebuild_b_field_lines(); }
        if (just_pressed(Key::KEY_SEMICOLON, prev_semicolon)) { b_lines_per_charge = std::max(2, b_lines_per_charge - 2); if (show_b_field_lines) rebuild_b_field_lines(); }
        if (just_pressed(Key::KEY_APOSTROPHE, prev_apostrophe)) { b_lines_per_charge = std::min(64, b_lines_per_charge + 2); if (show_b_field_lines) rebuild_b_field_lines(); }

        if (just_pressed(Key::KEY_V, prev_v)) { show_a_field_lines = !show_a_field_lines; rebuild_a_field_lines(); field_rebuild_accum = 0; }
        if (just_pressed(Key::KEY_COMMA, prev_comma)) { a_lines_per_magnet = std::max(2, a_lines_per_magnet - 2); a_lines_per_charge = std::max(2, a_lines_per_charge - 2); if (show_a_field_lines) rebuild_a_field_lines(); }
        if (just_pressed(Key::KEY_PERIOD, prev_period)) { a_lines_per_magnet = std::min(64, a_lines_per_magnet + 2); a_lines_per_charge = std::min(64, a_lines_per_charge + 2); if (show_a_field_lines) rebuild_a_field_lines(); }

        if (just_pressed(Key::KEY_J, prev_j)) { show_c_field_lines = !show_c_field_lines; rebuild_c_field_lines(); field_rebuild_accum = 0; }
        if (just_pressed(Key::KEY_9, prev_9)) { c_lines_per_monopole = std::max(2, c_lines_per_monopole - 2); if (show_c_field_lines) rebuild_c_field_lines(); }
        if (just_pressed(Key::KEY_0, prev_0)) { c_lines_per_monopole = std::min(64, c_lines_per_monopole + 2); if (show_c_field_lines) rebuild_c_field_lines(); }

        // Poynting vector: P
        if (just_pressed(Key::KEY_P, prev_p)) { show_s_field_lines = !show_s_field_lines; rebuild_s_field_lines(); field_rebuild_accum = 0; }

        // Topology cycle: O (torus → sphere → open → torus)
        if (just_pressed(Key::KEY_O, prev_o)) {
            topology = (Topology)(((int)topology + 1) % 4);
            emit_signal("topology_changed", (int)topology);
            rebuild_all_fields();
            if (show_grid) rebuild_grid();
            field_rebuild_accum = 0;
        }

        // Spawn bar magnet: M
        if (just_pressed(Key::KEY_M, prev_m)) {
            Camera3D *cam = get_viewport()->get_camera_3d();
            Vector3 origin = cam ? cam->get_global_transform().origin : Vector3(600,600,1800);
            Vector3 forward = cam ? -cam->get_global_transform().basis.get_column(2) : Vector3(0,0,-1);
            add_bar_magnet(origin + forward * 160.0f, forward, 120.0);
            emit_signal("magnet_spawned");
        }

        // Spawn particles
        auto spawn_from_cam = [&](int charge) {
            Camera3D *cam = get_viewport()->get_camera_3d();
            Vector3 o = cam ? cam->get_global_transform().origin : Vector3(600,600,1800);
            Vector3 f = cam ? -cam->get_global_transform().basis.get_column(2) : Vector3(0,0,-1);
            add_particle(o + f * 120.0f, f * 80.0f, charge);
            rebuild_all_fields(); emit_signal("particle_spawned", charge);
        };
        auto spawn_mono_cam = [&](int mc) {
            Camera3D *cam = get_viewport()->get_camera_3d();
            Vector3 o = cam ? cam->get_global_transform().origin : Vector3(600,600,1800);
            Vector3 f = cam ? -cam->get_global_transform().basis.get_column(2) : Vector3(0,0,-1);
            add_monopole(o + f * 120.0f, f * 80.0f, mc);
            rebuild_all_fields(); emit_signal("monopole_spawned", mc);
        };

        if (just_pressed(Key::KEY_1, prev_1)) spawn_from_cam(+1);
        if (just_pressed(Key::KEY_2, prev_2)) spawn_from_cam(-1);
        if (just_pressed(Key::KEY_3, prev_3)) spawn_from_cam(0);
        if (just_pressed(Key::KEY_4, prev_4)) spawn_mono_cam(+1);
        if (just_pressed(Key::KEY_5, prev_5)) spawn_mono_cam(-1);

        // Spawn charge ring facing camera: 6
        if (just_pressed(Key::KEY_6, prev_6)) {
            Camera3D *cam = get_viewport()->get_camera_3d();
            Vector3 o = cam ? cam->get_global_transform().origin : Vector3(600,600,1800);
            Vector3 f = cam ? -cam->get_global_transform().basis.get_column(2) : Vector3(0,0,-1);
            add_ring(o + f * 160.0f, 40.0, 10.0, RING_CHARGE, 1.0, f);
        }
    } else {
        // Still update prev_ states so we don't get phantom presses on re-enable
        auto eat_key = [&](Key key, bool &prev) { prev = in->is_key_pressed(key); };
        eat_key(Key::KEY_SPACE, prev_space); eat_key(Key::KEY_R, prev_r);
        eat_key(Key::KEY_C, prev_c); eat_key(Key::KEY_X, prev_x);
        eat_key(Key::KEY_F, prev_f); eat_key(Key::KEY_T, prev_t);
        eat_key(Key::KEY_MINUS, prev_minus); eat_key(Key::KEY_EQUAL, prev_equal);
        eat_key(Key::KEY_G, prev_g); eat_key(Key::KEY_BRACKETLEFT, prev_lbracket);
        eat_key(Key::KEY_BRACKETRIGHT, prev_rbracket);
        eat_key(Key::KEY_SEMICOLON, prev_semicolon); eat_key(Key::KEY_APOSTROPHE, prev_apostrophe);
        eat_key(Key::KEY_V, prev_v); eat_key(Key::KEY_COMMA, prev_comma);
        eat_key(Key::KEY_PERIOD, prev_period); eat_key(Key::KEY_J, prev_j);
        eat_key(Key::KEY_9, prev_9); eat_key(Key::KEY_0, prev_0);
        eat_key(Key::KEY_P, prev_p); eat_key(Key::KEY_O, prev_o);
        eat_key(Key::KEY_M, prev_m);
        eat_key(Key::KEY_1, prev_1); eat_key(Key::KEY_2, prev_2);
        eat_key(Key::KEY_3, prev_3); eat_key(Key::KEY_4, prev_4);
        eat_key(Key::KEY_5, prev_5); eat_key(Key::KEY_6, prev_6);
    }

    // Sim stepping
    if (!paused) {
        accumulator += delta;
        while (accumulator >= fixed_dt) { step_fixed(); accumulator -= fixed_dt; }
    }

    // Realtime field rebuild
    bool any = show_field_lines || show_b_field_lines || show_a_field_lines
             || show_c_field_lines || show_s_field_lines;
    if (field_rebuild_realtime && any) {
        field_rebuild_accum += delta;
        if (field_rebuild_interval <= 0.0 || field_rebuild_accum >= field_rebuild_interval) {
            rebuild_all_fields();
            field_rebuild_accum = 0.0;
        }
    }

    update_render();
}

// =====================================================================
// World setup
// =====================================================================

void ChargeSimulator3D::reset_world() {
    particles.clear(); sim_time = 0.0;
    // Destroy magnet visuals
    for (auto &mag : magnets) {
        if (mag.visual) { mag.visual->queue_free(); mag.visual = nullptr; }
    }
    magnets.clear();
    // Destroy ring visuals
    for (auto &r : rings) {
        if (r.visual) { r.visual->queue_free(); r.visual = nullptr; }
    }
    rings.clear();
}

void ChargeSimulator3D::clear_neutrals() {
    std::vector<Particle> keep;
    for (const auto &p : particles)
        if (p.charge != 0 || p.mag_charge != 0) keep.push_back(p);
    particles.swap(keep);
}

void ChargeSimulator3D::add_particle(Vector3 p_pos, Vector3 p_vel, int p_charge, bool p_locked) {
    Particle p; p.pos = p_pos; p.vel = p_locked ? Vector3() : p_vel;
    p.charge = (p_charge > 0) ? 1 : (p_charge < 0) ? -1 : 0;
    p.mag_charge = 0; p.locked = p_locked;
    if (p.charge > 0) { p.mass = (float)mass_pos; p.radius = (float)radius_pos; }
    else if (p.charge < 0) { p.mass = (float)mass_neg; p.radius = (float)radius_neg; }
    else { p.radius = (float)radius_neu; p.mass = neutral_mass_from_radius(p.radius); }
    wrap_position(p.pos); particles.push_back(p);
}

void ChargeSimulator3D::add_monopole(Vector3 p_pos, Vector3 p_vel, int p_mc, bool p_locked) {
    Particle p; p.pos = p_pos; p.vel = p_locked ? Vector3() : p_vel; p.charge = 0;
    p.mag_charge = (p_mc > 0) ? 1 : (p_mc < 0) ? -1 : 0; p.locked = p_locked;
    if (p.mag_charge > 0) { p.mass = (float)mass_north; p.radius = (float)radius_north; }
    else if (p.mag_charge < 0) { p.mass = (float)mass_south; p.radius = (float)radius_south; }
    else { p.radius = (float)radius_neu; p.mass = neutral_mass_from_radius(p.radius); }
    wrap_position(p.pos); particles.push_back(p);
}

void ChargeSimulator3D::add_bar_magnet(Vector3 p_pos, Vector3 p_dir, double p_str) {
    Magnet mag; mag.pos = p_pos; wrap_position(mag.pos);
    mag.radius = 18.0f; mag.m = p_dir.normalized() * (float)p_str;
    mag.length = 80.0f; mag.thickness = 18.0f;

    MeshInstance3D *mi = memnew(MeshInstance3D);
    Ref<BoxMesh> box; box.instantiate();
    box->set_size(Vector3(mag.length, mag.thickness, mag.thickness));
    Ref<StandardMaterial3D> mat; mat.instantiate();
    mat->set_albedo(Color(1.0, 0.2, 0.2));
    mat->set_feature(StandardMaterial3D::FEATURE_EMISSION, true);
    mat->set_emission(Color(1.0, 0.2, 0.2));
    mat->set_emission_energy_multiplier(0.3);
    box->surface_set_material(0, mat); mi->set_mesh(box); add_child(mi);

    Vector3 dir = mag.m.length() > 1e-6f ? mag.m.normalized() : Vector3(1,0,0);
    Vector3 up = Vector3(0,1,0);
    Vector3 xa = dir, za = xa.cross(up);
    if (za.length() < 1e-6f) za = Vector3(0,0,1);
    za = za.normalized(); Vector3 ya = za.cross(xa).normalized();
    Transform3D t; t.basis = Basis(xa, ya, za); t.origin = mag.pos;
    mi->set_global_transform(t); mag.visual = mi;
    magnets.push_back(mag);
    if (show_b_field_lines) rebuild_b_field_lines();
    if (show_a_field_lines) rebuild_a_field_lines();
}

// =====================================================================
// Toroidal ring mesh + spawning
// =====================================================================

Ref<ArrayMesh> ChargeSimulator3D::build_torus_mesh(float major_r, float minor_r,
                                                    int ring_seg, int tube_seg) {
    Ref<ArrayMesh> mesh; mesh.instantiate();

    PackedVector3Array verts, normals;
    PackedInt32Array indices;

    int vc = (ring_seg + 1) * (tube_seg + 1);
    verts.resize(vc); normals.resize(vc);

    int idx = 0;
    for (int i = 0; i <= ring_seg; i++) {
        float theta = Math_TAU * (float)i / (float)ring_seg;
        float ct = std::cos(theta), st = std::sin(theta);
        Vector3 center(ct * major_r, 0.0f, st * major_r);
        Vector3 radial(ct, 0.0f, st);  // outward from ring axis
        for (int j = 0; j <= tube_seg; j++) {
            float phi = Math_TAU * (float)j / (float)tube_seg;
            float cp = std::cos(phi), sp = std::sin(phi);
            Vector3 n = radial * cp + Vector3(0, 1, 0) * sp;
            verts.set(idx, center + n * minor_r);
            normals.set(idx, n);
            idx++;
        }
    }

    int stride = tube_seg + 1;
    for (int i = 0; i < ring_seg; i++) {
        for (int j = 0; j < tube_seg; j++) {
            int a = i * stride + j;
            int b = a + stride;
            int c = a + 1;
            int d = b + 1;
            indices.push_back(a); indices.push_back(b); indices.push_back(c);
            indices.push_back(c); indices.push_back(b); indices.push_back(d);
        }
    }

    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = verts;
    arrays[Mesh::ARRAY_NORMAL] = normals;
    arrays[Mesh::ARRAY_INDEX] = indices;
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    return mesh;
}

void ChargeSimulator3D::add_ring(Vector3 p_pos, double p_major, double p_minor,
                                  int p_source_type, double p_strength,
                                  Vector3 p_axis, bool p_locked) {
    ToroidalRing r;
    r.pos = p_pos; wrap_position(r.pos);
    r.major_radius = (float)std::max(1.0, p_major);
    r.minor_radius = (float)std::max(0.5, std::min(p_minor, p_major * 0.9));
    r.source_type = (RingSourceType)std::clamp(p_source_type, 0, 2);
    r.strength = (float)p_strength;
    r.locked = p_locked;
    r.vel = Vector3();

    // Compute orientation from axis (ring normal = axis direction)
    // Default mesh has ring axis along Y, so rotate from Y to p_axis
    Vector3 ax = p_axis.length() > 1e-6f ? p_axis.normalized() : Vector3(0,1,0);
    Vector3 from(0,1,0);
    float dot = from.dot(ax);
    if (dot > 0.9999f) {
        r.orient = Quaternion();  // identity — already aligned
    } else if (dot < -0.9999f) {
        // 180° flip — rotate around any perpendicular axis
        r.orient = Quaternion(Vector3(1,0,0), Math_PI);
    } else {
        Vector3 cross = from.cross(ax).normalized();
        float angle = std::acos(std::clamp((double)dot, -1.0, 1.0));
        r.orient = Quaternion(cross, angle);
    }
    // Mass proportional to volume of torus: V = 2*pi^2 * R * r^2
    r.mass = (float)(2.0 * Math_PI * Math_PI * r.major_radius * r.minor_radius * r.minor_radius * 0.01);
    r.mass = std::max(1.0f, r.mass);
    // Moment of inertia: I ≈ m * (R² + ¾r²) for a solid torus
    r.moment_of_inertia = r.mass * (r.major_radius * r.major_radius + 0.75f * r.minor_radius * r.minor_radius);
    r.moment_of_inertia = std::max(0.1f, r.moment_of_inertia);

    // Color by source type
    Color col;
    switch (r.source_type) {
        case RING_CHARGE:   col = (r.strength >= 0) ? Color(1.0, 0.35, 0.35) : Color(0.35, 0.6, 1.0); break;
        case RING_CURRENT:  col = Color(0.3, 0.9, 0.9); break;
        case RING_MONOPOLE: col = (r.strength >= 0) ? Color(1.0, 0.65, 0.15) : Color(0.7, 0.3, 0.9); break;
    }

    // Determine segment counts from size
    int ring_seg = std::clamp((int)(r.major_radius * 1.5f), 16, 64);
    int tube_seg = std::clamp((int)(r.minor_radius * 2.0f), 8, 32);
    Ref<ArrayMesh> torus = build_torus_mesh(r.major_radius, r.minor_radius, ring_seg, tube_seg);

    Ref<StandardMaterial3D> mat; mat.instantiate();
    mat->set_albedo(col);
    mat->set_feature(StandardMaterial3D::FEATURE_EMISSION, true);
    mat->set_emission(col);
    mat->set_emission_energy_multiplier(0.35);
    torus->surface_set_material(0, mat);

    MeshInstance3D *mi = memnew(MeshInstance3D);
    mi->set_mesh(torus); add_child(mi);

    Transform3D t;
    t.basis = Basis(r.orient);
    t.origin = r.pos;
    mi->set_global_transform(t);
    r.visual = mi;

    rings.push_back(r);
    emit_signal("ring_spawned", (int)r.source_type);
    rebuild_all_fields();
}

// =====================================================================
// Field computations
// =====================================================================

Vector3 ChargeSimulator3D::compute_e_field_at(const Vector3 &p, int excl, int excl_ring) const {
    Vector3 E;
    for (int j = 0; j < (int)particles.size(); j++) {
        if (j == excl) continue;
        const auto &pj = particles[j];
        Vector3 R = min_image_vec(p - pj.pos);
        double r2s = (double)R.length_squared() + soften_eps2;
        if (pj.charge != 0) {
            double d3 = std::pow(r2s, 1.5); if (d3 > 1e-12)
                E += (float)(k_coulomb * (double)pj.charge / d3) * R;
        }
        if (pj.mag_charge != 0) {
            double ir3 = 1.0 / std::pow(r2s, 1.5);
            E -= (float)(k_dual_biot * (double)pj.mag_charge * ir3) * pj.vel.cross(R);
        }
    }
    // Ring contributions to E field
    for (int ri = 0; ri < (int)rings.size(); ri++) {
        if (ri == excl_ring) continue;
        const auto &rng = rings[ri];
        Basis rot(rng.orient);
        Vector3 R_center = min_image_vec(p - rng.pos);
        double dist2 = (double)R_center.length_squared();
        double far_thr = 3.0 * (double)rng.major_radius;

        if (rng.source_type == RING_CHARGE) {
            if (dist2 > far_thr * far_thr) {
                // Far-field: point charge Q at center
                double r2s = dist2 + soften_eps2;
                double d3 = std::pow(r2s, 1.5); if (d3 > 1e-12)
                    E += (float)(k_coulomb * (double)rng.strength / d3) * R_center;
            } else {
                int N = std::clamp((int)(rng.major_radius * 0.25f), 8, 64);
                double qn = (double)rng.strength / (double)N;
                for (int i = 0; i < N; i++) {
                    float theta = Math_TAU * (float)i / (float)N;
                    Vector3 lp(std::cos(theta) * rng.major_radius, 0.0f, std::sin(theta) * rng.major_radius);
                    Vector3 sp = rng.pos + rot.xform(lp);
                    Vector3 R = min_image_vec(p - sp);
                    double r2s = (double)R.length_squared() + soften_eps2;
                    double d3 = std::pow(r2s, 1.5); if (d3 > 1e-12)
                        E += (float)(k_coulomb * qn / d3) * R;
                }
            }
        }
        if (rng.source_type == RING_MONOPOLE && rng.vel.length() > 0.01f) {
            if (dist2 > far_thr * far_thr) {
                // Far-field: moving monopole at center
                double r2s = dist2 + soften_eps2;
                double ir3 = 1.0 / std::pow(r2s, 1.5);
                E -= (float)(k_dual_biot * (double)rng.strength * ir3) * rng.vel.cross(R_center);
            } else {
                int N = std::clamp((int)(rng.major_radius * 0.25f), 8, 64);
                double gn = (double)rng.strength / (double)N;
                for (int i = 0; i < N; i++) {
                    float theta = Math_TAU * (float)i / (float)N;
                    Vector3 lp(std::cos(theta) * rng.major_radius, 0.0f, std::sin(theta) * rng.major_radius);
                    Vector3 sp = rng.pos + rot.xform(lp);
                    Vector3 R = min_image_vec(p - sp);
                    double r2s = (double)R.length_squared() + soften_eps2;
                    double ir3 = 1.0 / std::pow(r2s, 1.5);
                    E -= (float)(k_dual_biot * gn * ir3) * rng.vel.cross(R);
                }
            }
        }
    }
    return E;
}

Vector3 ChargeSimulator3D::compute_b_field_at(const Vector3 &p, int excl, int excl_ring) const {
    Vector3 B;
    for (const auto &mag : magnets) {
        Vector3 R = min_image_vec(p - mag.pos);
        double r2s = (double)R.length_squared() + soften_eps2;
        double ir3 = 1.0 / std::pow(r2s, 1.5), ir5 = 1.0 / std::pow(r2s, 2.5);
        double mR = (double)mag.m.dot(R);
        B += (float)(k_dipole * 3.0 * mR * ir5) * R;
        B -= (float)(k_dipole * ir3) * mag.m;
    }
    for (int j = 0; j < (int)particles.size(); j++) {
        if (j == excl) continue;
        const auto &src = particles[j];
        Vector3 R = min_image_vec(p - src.pos);
        double r2s = (double)R.length_squared() + soften_eps2;
        double ir3 = 1.0 / std::pow(r2s, 1.5);
        if (src.charge != 0) B += (float)(k_biot * (double)src.charge * ir3) * src.vel.cross(R);
        if (src.mag_charge != 0) B += (float)(k_mag_coulomb * (double)src.mag_charge / std::pow(r2s, 1.5)) * R;
    }
    // Ring contributions to B field
    for (int ri = 0; ri < (int)rings.size(); ri++) {
        if (ri == excl_ring) continue;
        const auto &rng = rings[ri];
        Basis rot(rng.orient);
        Vector3 R_center = min_image_vec(p - rng.pos);
        double dist2 = (double)R_center.length_squared();
        double far_thr = 3.0 * (double)rng.major_radius;

        if (rng.source_type == RING_CURRENT) {
            if (dist2 > far_thr * far_thr) {
                // Far-field: magnetic dipole m = I * π * R² * axis
                Vector3 axis = rot.xform(Vector3(0,1,0));
                Vector3 m_vec = axis * (float)(rng.strength * Math_PI * rng.major_radius * rng.major_radius);
                double r2s = dist2 + soften_eps2;
                double ir3 = 1.0 / std::pow(r2s, 1.5), ir5 = 1.0 / std::pow(r2s, 2.5);
                double mR = (double)m_vec.dot(R_center);
                B += (float)(k_dipole * 3.0 * mR * ir5) * R_center;
                B -= (float)(k_dipole * ir3) * m_vec;
            } else {
                int N = std::clamp((int)(rng.major_radius * 0.25f), 8, 64);
                float dl = Math_TAU * rng.major_radius / (float)N;
                for (int i = 0; i < N; i++) {
                    float theta = Math_TAU * (float)i / (float)N;
                    Vector3 lp(std::cos(theta) * rng.major_radius, 0.0f, std::sin(theta) * rng.major_radius);
                    Vector3 lt(-std::sin(theta), 0.0f, std::cos(theta));
                    Vector3 sp = rng.pos + rot.xform(lp);
                    Vector3 tang = rot.xform(lt);
                    Vector3 R = min_image_vec(p - sp);
                    double r2s = (double)R.length_squared() + soften_eps2;
                    double ir3 = 1.0 / std::pow(r2s, 1.5);
                    B += (float)(k_biot * (double)rng.strength * (double)dl * ir3) * tang.cross(R);
                }
            }
        }
        if (rng.source_type == RING_CHARGE && rng.vel.length() > 0.01f) {
            if (dist2 > far_thr * far_thr) {
                // Far-field: moving point charge at center
                double r2s = dist2 + soften_eps2;
                double ir3 = 1.0 / std::pow(r2s, 1.5);
                B += (float)(k_biot * (double)rng.strength * ir3) * rng.vel.cross(R_center);
            } else {
                int N = std::clamp((int)(rng.major_radius * 0.25f), 8, 64);
                double qn = (double)rng.strength / (double)N;
                for (int i = 0; i < N; i++) {
                    float theta = Math_TAU * (float)i / (float)N;
                    Vector3 lp(std::cos(theta) * rng.major_radius, 0.0f, std::sin(theta) * rng.major_radius);
                    Vector3 sp = rng.pos + rot.xform(lp);
                    Vector3 R = min_image_vec(p - sp);
                    double r2s = (double)R.length_squared() + soften_eps2;
                    double ir3 = 1.0 / std::pow(r2s, 1.5);
                    B += (float)(k_biot * qn * ir3) * rng.vel.cross(R);
                }
            }
        }
        if (rng.source_type == RING_MONOPOLE) {
            if (dist2 > far_thr * far_thr) {
                // Far-field: net monopole at center
                double r2s = dist2 + soften_eps2;
                B += (float)(k_mag_coulomb * (double)rng.strength / std::pow(r2s, 1.5)) * R_center;
            } else {
                int N = std::clamp((int)(rng.major_radius * 0.25f), 8, 64);
                double gn = (double)rng.strength / (double)N;
                for (int i = 0; i < N; i++) {
                    float theta = Math_TAU * (float)i / (float)N;
                    Vector3 lp(std::cos(theta) * rng.major_radius, 0.0f, std::sin(theta) * rng.major_radius);
                    Vector3 sp = rng.pos + rot.xform(lp);
                    Vector3 R = min_image_vec(p - sp);
                    double r2s = (double)R.length_squared() + soften_eps2;
                    B += (float)(k_mag_coulomb * gn / std::pow(r2s, 1.5)) * R;
                }
            }
        }
    }
    return B;
}

Vector3 ChargeSimulator3D::compute_a_field_at(const Vector3 &p, int excl, int excl_ring) const {
    Vector3 A;
    for (const auto &mag : magnets) {
        Vector3 R = min_image_vec(p - mag.pos);
        double ir3 = 1.0 / std::pow((double)R.length_squared() + soften_eps2, 1.5);
        A += (float)(k_a_dipole * ir3) * mag.m.cross(R);
    }
    for (int j = 0; j < (int)particles.size(); j++) {
        if (j == excl) continue;
        const auto &s = particles[j]; if (s.charge == 0) continue;
        Vector3 R = min_image_vec(p - s.pos);
        double ir = 1.0 / std::sqrt((double)R.length_squared() + soften_eps2);
        A += (float)(k_a_biot * (double)s.charge * ir) * s.vel;
    }
    // Ring contributions to A field
    for (int ri = 0; ri < (int)rings.size(); ri++) {
        if (ri == excl_ring) continue;
        const auto &rng = rings[ri];
        if (rng.source_type != RING_CURRENT) continue;
        Basis rot(rng.orient);
        Vector3 R_center = min_image_vec(p - rng.pos);
        double dist2 = (double)R_center.length_squared();
        double far_thr = 3.0 * (double)rng.major_radius;

        if (dist2 > far_thr * far_thr) {
            // Far-field: dipole A = k_a_dipole * m × R / |R|^3
            Vector3 axis = rot.xform(Vector3(0,1,0));
            Vector3 m_vec = axis * (float)(rng.strength * Math_PI * rng.major_radius * rng.major_radius);
            double ir3 = 1.0 / std::pow(dist2 + soften_eps2, 1.5);
            A += (float)(k_a_dipole * ir3) * m_vec.cross(R_center);
        } else {
            int N = std::clamp((int)(rng.major_radius * 0.25f), 8, 64);
            float dl = Math_TAU * rng.major_radius / (float)N;
            for (int i = 0; i < N; i++) {
                float theta = Math_TAU * (float)i / (float)N;
                Vector3 lp(std::cos(theta) * rng.major_radius, 0.0f, std::sin(theta) * rng.major_radius);
                Vector3 lt(-std::sin(theta), 0.0f, std::cos(theta));
                Vector3 sp = rng.pos + rot.xform(lp);
                Vector3 tang = rot.xform(lt);
                Vector3 R = min_image_vec(p - sp);
                double ir = 1.0 / std::sqrt((double)R.length_squared() + soften_eps2);
                A += (float)(k_a_biot * (double)rng.strength * (double)dl * ir) * tang;
            }
        }
    }
    return A;
}

Vector3 ChargeSimulator3D::compute_c_field_at(const Vector3 &p, int excl, int excl_ring) const {
    Vector3 C;
    for (int j = 0; j < (int)particles.size(); j++) {
        if (j == excl) continue;
        const auto &s = particles[j]; if (s.mag_charge == 0) continue;
        Vector3 R = min_image_vec(p - s.pos);
        double ir = 1.0 / std::sqrt((double)R.length_squared() + soften_eps2);
        C += (float)(k_c_mono * (double)s.mag_charge * ir) * s.vel;
    }
    // Ring contributions to C field (monopole rings with velocity)
    for (int ri = 0; ri < (int)rings.size(); ri++) {
        if (ri == excl_ring) continue;
        const auto &rng = rings[ri];
        if (rng.source_type != RING_MONOPOLE || rng.vel.length() < 0.01f) continue;
        Vector3 R_center = min_image_vec(p - rng.pos);
        double dist2 = (double)R_center.length_squared();
        double far_thr = 3.0 * (double)rng.major_radius;
        if (dist2 > far_thr * far_thr) {
            // Far-field: net monopole at center
            double ir = 1.0 / std::sqrt(dist2 + soften_eps2);
            C += (float)(k_c_mono * (double)rng.strength * ir) * rng.vel;
        } else {
            int N = std::clamp((int)(rng.major_radius * 0.25f), 8, 64);
            Basis rot(rng.orient);
            double gn = (double)rng.strength / (double)N;
            for (int i = 0; i < N; i++) {
                float theta = Math_TAU * (float)i / (float)N;
                Vector3 lp(std::cos(theta) * rng.major_radius, 0.0f, std::sin(theta) * rng.major_radius);
                Vector3 sp = rng.pos + rot.xform(lp);
                Vector3 R = min_image_vec(p - sp);
                double ir = 1.0 / std::sqrt((double)R.length_squared() + soften_eps2);
                C += (float)(k_c_mono * gn * ir) * rng.vel;
            }
        }
    }
    return C;
}

Vector3 ChargeSimulator3D::compute_s_field_at(const Vector3 &p) const {
    return compute_e_field_at(p).cross(compute_b_field_at(p));
}

// =====================================================================
// Physics
// =====================================================================

void ChargeSimulator3D::compute_accelerations(std::vector<Vector3> &out) const {
    int N = (int)particles.size(); out.assign(N, Vector3());
    for (int i = 0; i < N; i++) {
        const auto &pi = particles[i];
        if (pi.charge == 0 && pi.mag_charge == 0) continue;
        Vector3 F;
        if (pi.charge != 0) {
            Vector3 E = compute_e_field_at(pi.pos, i), B = compute_b_field_at(pi.pos, i);
            F += (float)pi.charge * E;
            F += (float)((double)pi.charge * k_lorentz) * pi.vel.cross(B);
        }
        if (pi.mag_charge != 0) {
            Vector3 B = compute_b_field_at(pi.pos, i), E = compute_e_field_at(pi.pos, i);
            F += (float)pi.mag_charge * B;
            F -= (float)((double)pi.mag_charge * k_dual_lorentz) * pi.vel.cross(E);
        }
        Vector3 a = F / pi.mass;
        double am = a.length();
        if (am > max_mag_accel && am > 1e-12) a *= (float)(max_mag_accel / am);
        out[i] = a;
    }
}

void ChargeSimulator3D::step_fixed() {
    sim_time += fixed_dt;
    if (particles.empty() && rings.empty()) return;

    int nb = 0; for (const auto &p : particles) if (p.charge == 0 && p.mag_charge == 0) nb++;

    // --- Particle dynamics ---
    std::vector<Vector3> a; compute_accelerations(a);
    for (size_t i = 0; i < particles.size(); i++) {
        Particle &p = particles[i];
        if (p.locked) continue;
        p.vel += a[i] * (float)fixed_dt;
        p.vel *= (float)global_damp;
        limit_speed(p.vel, max_speed);
        p.pos += p.vel * (float)fixed_dt;
        wrap_position(p.pos);
        if (topology == TOPO_SPHERE) reflect_at_sphere(p.pos, p.vel);
        if (topology == TOPO_SPHERE_WRAP) wrap_at_sphere(p.pos);
        // Integrate angular velocity into orientation (if spinning)
        double omega_p = p.angular_vel.length();
        if (omega_p > 1e-8) {
            p.angular_vel *= (float)global_damp;
            if (omega_p > max_angular_speed) p.angular_vel *= (float)(max_angular_speed / omega_p);
            float hw = -p.angular_vel.x * p.orient.x - p.angular_vel.y * p.orient.y - p.angular_vel.z * p.orient.z;
            float hx =  p.angular_vel.x * p.orient.w + p.angular_vel.y * p.orient.z - p.angular_vel.z * p.orient.y;
            float hy = -p.angular_vel.x * p.orient.z + p.angular_vel.y * p.orient.w + p.angular_vel.z * p.orient.x;
            float hz =  p.angular_vel.x * p.orient.y - p.angular_vel.y * p.orient.x + p.angular_vel.z * p.orient.w;
            float half_dt = 0.5f * (float)fixed_dt;
            p.orient.x += hx * half_dt;
            p.orient.y += hy * half_dt;
            p.orient.z += hz * half_dt;
            p.orient.w += hw * half_dt;
            p.orient = p.orient.normalized();
        }
    }

    // --- Ring dynamics ---
    for (int ridx = 0; ridx < (int)rings.size(); ridx++) {
        auto &rng = rings[ridx];
        if (rng.locked) continue;
        int N = std::clamp((int)(rng.major_radius * 0.25f), 8, 64);
        Basis rot(rng.orient);
        float dl = Math_TAU * rng.major_radius / (float)N;
        Vector3 F_net, T_net;  // net force and torque

        for (int i = 0; i < N; i++) {
            float theta = Math_TAU * (float)i / (float)N;
            Vector3 lp(std::cos(theta) * rng.major_radius, 0.0f, std::sin(theta) * rng.major_radius);
            Vector3 sp = rng.pos + rot.xform(lp);
            Vector3 dF;

            switch (rng.source_type) {
                case RING_CHARGE: {
                    double qn = (double)rng.strength / (double)N;
                    Vector3 E = compute_e_field_at(sp, -1, ridx);
                    Vector3 B = compute_b_field_at(sp, -1, ridx);
                    dF = (float)qn * E;
                    dF += (float)(qn * k_lorentz) * rng.vel.cross(B);
                } break;
                case RING_CURRENT: {
                    Vector3 lt(-std::sin(theta), 0.0f, std::cos(theta));
                    Vector3 tang = rot.xform(lt);
                    Vector3 B = compute_b_field_at(sp, -1, ridx);
                    dF = (float)((double)rng.strength * (double)dl) * tang.cross(B);
                } break;
                case RING_MONOPOLE: {
                    double gn = (double)rng.strength / (double)N;
                    Vector3 B = compute_b_field_at(sp, -1, ridx);
                    Vector3 E = compute_e_field_at(sp, -1, ridx);
                    dF = (float)gn * B;
                    dF -= (float)(gn * k_dual_lorentz) * rng.vel.cross(E);
                } break;
            }

            F_net += dF;
            // Torque: r × F where r is the lever arm from ring center to sample point
            Vector3 lever = rot.xform(lp);  // sp - rng.pos in world space
            T_net += lever.cross(dF);
        }

        // --- Linear dynamics ---
        Vector3 acc = F_net / rng.mass;
        double am = acc.length();
        if (am > max_mag_accel && am > 1e-12) acc *= (float)(max_mag_accel / am);

        rng.vel += acc * (float)fixed_dt;
        rng.vel *= (float)global_damp;
        limit_speed(rng.vel, max_speed);
        rng.pos += rng.vel * (float)fixed_dt;
        wrap_position(rng.pos);
        if (topology == TOPO_SPHERE) reflect_at_sphere(rng.pos, rng.vel);
        if (topology == TOPO_SPHERE_WRAP) wrap_at_sphere(rng.pos);

        // --- Angular dynamics ---
        Vector3 alpha = T_net / rng.moment_of_inertia;
        double alm = alpha.length();
        if (alm > max_mag_accel && alm > 1e-12) alpha *= (float)(max_mag_accel / alm);

        rng.angular_vel += alpha * (float)fixed_dt;
        rng.angular_vel *= (float)global_damp;
        // Clamp angular speed
        double omega = rng.angular_vel.length();
        if (omega > max_angular_speed && omega > 1e-12)
            rng.angular_vel *= (float)(max_angular_speed / omega);

        // Integrate orientation: dq/dt = 0.5 * Quaternion(ω, 0) * q
        if (omega > 1e-8) {
            // Hamilton product of (ωx, ωy, ωz, 0) * (qx, qy, qz, qw)
            float hw = -rng.angular_vel.x * rng.orient.x - rng.angular_vel.y * rng.orient.y - rng.angular_vel.z * rng.orient.z;
            float hx =  rng.angular_vel.x * rng.orient.w + rng.angular_vel.y * rng.orient.z - rng.angular_vel.z * rng.orient.y;
            float hy = -rng.angular_vel.x * rng.orient.z + rng.angular_vel.y * rng.orient.w + rng.angular_vel.z * rng.orient.x;
            float hz =  rng.angular_vel.x * rng.orient.y - rng.angular_vel.y * rng.orient.x + rng.angular_vel.z * rng.orient.w;
            float half_dt = 0.5f * (float)fixed_dt;
            rng.orient.x += hx * half_dt;
            rng.orient.y += hy * half_dt;
            rng.orient.z += hz * half_dt;
            rng.orient.w += hw * half_dt;
            rng.orient = rng.orient.normalized();
        }
    }

    handle_collisions(); do_binding();

    int na = 0; for (const auto &p : particles) if (p.charge == 0 && p.mag_charge == 0) na++;
    if (na > nb) emit_signal("binding_event", na);
}

// =====================================================================
// Collisions & binding (unchanged logic)
// =====================================================================

void ChargeSimulator3D::handle_collisions() {
    int n = (int)particles.size();
    // Particle-particle collisions
    if (n >= 2) {
    int i = 0;
    while (i < n) {
        int j = i + 1;
        while (j < n) {
            Vector3 rij = min_image_vec(particles[j].pos - particles[i].pos);
            double dist = rij.length(), ri = particles[i].radius, rj = particles[j].radius;
            if (dist < ri + rj && dist > 1e-6) {
                bool in_ = (particles[i].charge == 0 && particles[i].mag_charge == 0);
                bool jn = (particles[j].charge == 0 && particles[j].mag_charge == 0);
                bool il = particles[i].locked, jl = particles[j].locked;
                if (in_ && jn) {
                    // Neutral-neutral merge — skip if either is locked
                    if (!il && !jl) {
                        double mi = particles[i].mass, mj = particles[j].mass, mt = mi + mj;
                        particles[i].vel = (float)(mi/mt) * particles[i].vel + (float)(mj/mt) * particles[j].vel;
                        Vector3 com = particles[i].pos + (float)(mj/mt) * rij; wrap_position(com);
                        particles[i].pos = com; particles[i].mass = (float)mt;
                        particles[i].radius = neutral_radius_from_mass((float)mt);
                        particles[i].charge = 0; particles[i].mag_charge = 0;
                        particles.erase(particles.begin() + j); n--; continue;
                    }
                } else if (in_ ^ jn) {
                    Vector3 nh = rij / (float)dist;
                    double vr = (particles[j].vel - particles[i].vel).dot(nh);
                    if (vr < 0) {
                        if (il && jl) {
                            // Both locked — just separate, no impulse
                        } else if (il) {
                            // i locked (infinite mass): j gets full reflection
                            particles[j].vel -= (float)(2.0 * vr) * nh;
                        } else if (jl) {
                            // j locked (infinite mass): i gets full reflection
                            particles[i].vel += (float)(2.0 * vr) * nh;
                        } else {
                            double mi = particles[i].mass, mj = particles[j].mass;
                            double imp = 2.0 * vr / (mi + mj);
                            particles[i].vel += (float)(imp * mj) * nh;
                            particles[j].vel -= (float)(imp * mi) * nh;
                        }

                        // Friction-based spin transfer from glancing impacts
                        const float friction = 0.3f;
                        Vector3 r_i = nh * (float)ri;   // lever: center of i to contact
                        Vector3 r_j = -nh * (float)rj;  // lever: center of j to contact
                        // Surface velocity at contact = v + ω × r
                        Vector3 v_ci = particles[i].vel + particles[i].angular_vel.cross(r_i);
                        Vector3 v_cj = particles[j].vel + particles[j].angular_vel.cross(r_j);
                        Vector3 v_rel = v_cj - v_ci;
                        // Tangential component (remove normal)
                        Vector3 v_tan = v_rel - nh * (float)v_rel.dot(nh);
                        float vt_len = v_tan.length();
                        if (vt_len > 0.01f) {
                            // I = 2/5 * m * r^2 for a solid sphere
                            float Ii = 0.4f * particles[i].mass * (float)(ri * ri);
                            float Ij = 0.4f * particles[j].mass * (float)(rj * rj);
                            Ii = std::max(0.01f, Ii); Ij = std::max(0.01f, Ij);
                            float J_tan = friction * (float)std::abs(vr);  // tangential impulse magnitude
                            Vector3 t_dir = v_tan / vt_len;
                            if (!il) {
                                particles[i].angular_vel += r_i.cross(t_dir * J_tan) / Ii;
                                particles[i].vel += t_dir * (J_tan / particles[i].mass) * 0.5f;
                            }
                            if (!jl) {
                                particles[j].angular_vel -= r_j.cross(t_dir * J_tan) / Ij;
                                particles[j].vel -= t_dir * (J_tan / particles[j].mass) * 0.5f;
                            }
                        }

                        // Separation: locked particles don't move
                        double ov = (ri + rj) - dist;
                        if (il && jl) { /* both locked, no separation */ }
                        else if (il) { particles[j].pos += (float)ov * nh; wrap_position(particles[j].pos); }
                        else if (jl) { particles[i].pos -= (float)ov * nh; wrap_position(particles[i].pos); }
                        else {
                            double cr = ov / 2.0;
                            particles[i].pos -= (float)cr * nh; particles[j].pos += (float)cr * nh;
                            wrap_position(particles[i].pos); wrap_position(particles[j].pos);
                        }
                    }
                }
            }
            j++;
        }
        i++;
    }
    } // end particle-particle collisions

    // Ring-particle collisions (with spin transfer)
    for (auto &rng : rings) {
        Basis rot(rng.orient);
        Basis inv_rot = rot.inverse();
        for (auto &pt : particles) {
            Vector3 d = min_image_vec(pt.pos - rng.pos);
            Vector3 local = inv_rot.xform(d);
            // Project onto major circle in XZ plane
            Vector3 flat(local.x, 0.0f, local.z);
            float fl = flat.length();
            Vector3 nearest;
            if (fl > 1e-6f)
                nearest = flat * (rng.major_radius / fl);
            else
                nearest = Vector3(rng.major_radius, 0, 0);
            Vector3 delta = local - nearest;
            float dist = delta.length();
            float overlap = (rng.minor_radius + pt.radius) - dist;
            if (overlap > 0.0f && dist > 1e-6f) {
                Vector3 n_local = delta / dist;
                Vector3 n_world = rot.xform(n_local);
                // Contact point in world space (on ring surface toward particle)
                Vector3 contact_world = rng.pos + rot.xform(nearest + n_local * rng.minor_radius);
                Vector3 lever_ring = contact_world - rng.pos;  // lever arm for ring torque

                double vr = (pt.vel - rng.vel).dot(n_world);
                if (vr < 0.0) {
                    bool pl = pt.locked, rl = rng.locked;
                    float impulse_mag = 0.0f;
                    if (pl && rl) {
                        // both locked
                    } else if (pl) {
                        impulse_mag = (float)(2.0 * vr);
                        rng.vel += impulse_mag * n_world;
                    } else if (rl) {
                        impulse_mag = (float)(-2.0 * vr);
                        pt.vel -= (float)(2.0 * vr) * n_world;
                    } else {
                        double mp = pt.mass, mr = rng.mass;
                        double imp = 2.0 * vr / (mp + mr);
                        impulse_mag = (float)(imp * mr);
                        pt.vel -= (float)(imp * mr) * n_world;
                        rng.vel += (float)(imp * mp) * n_world;
                    }
                    // Collision-induced spin from normal impulse (off-center hits)
                    if (std::abs(impulse_mag) > 1e-6f) {
                        if (!rl) {
                            Vector3 spin_impulse = lever_ring.cross(n_world * impulse_mag);
                            rng.angular_vel += spin_impulse / rng.moment_of_inertia;
                        }
                    }
                    // Friction-based tangential spin transfer
                    const float friction = 0.3f;
                    Vector3 r_pt = -n_world * pt.radius;  // lever from particle center to contact
                    Vector3 v_cp = pt.vel + pt.angular_vel.cross(r_pt);
                    Vector3 v_cr = rng.vel + rng.angular_vel.cross(lever_ring);
                    Vector3 v_rel_t = (v_cp - v_cr);
                    v_rel_t -= n_world * v_rel_t.dot(n_world);  // remove normal
                    float vt_len = v_rel_t.length();
                    if (vt_len > 0.01f) {
                        float J_tan = friction * (float)std::abs(vr);
                        Vector3 t_dir = v_rel_t / vt_len;
                        float Ip = 0.4f * pt.mass * pt.radius * pt.radius;
                        Ip = std::max(0.01f, Ip);
                        if (!pl) {
                            pt.angular_vel += r_pt.cross(-t_dir * J_tan) / Ip;
                        }
                        if (!rl) {
                            rng.angular_vel += lever_ring.cross(t_dir * J_tan) / rng.moment_of_inertia;
                        }
                    }
                }
                // Separation
                if (pt.locked && rng.locked) { /* nothing */ }
                else if (pt.locked) {
                    rng.pos -= n_world * overlap; wrap_position(rng.pos);
                } else if (rng.locked) {
                    pt.pos += n_world * overlap; wrap_position(pt.pos);
                } else {
                    float frac = rng.mass / (pt.mass + rng.mass);
                    pt.pos += n_world * (overlap * frac); wrap_position(pt.pos);
                    rng.pos -= n_world * (overlap * (1.0f - frac)); wrap_position(rng.pos);
                }
            }
        }
    }

    // Ring-ring collisions
    for (int ri = 0; ri < (int)rings.size(); ri++) {
        for (int rj = ri + 1; rj < (int)rings.size(); rj++) {
            auto &ra = rings[ri], &rb = rings[rj];
            // Broad phase: bounding sphere check
            Vector3 dd = min_image_vec(rb.pos - ra.pos);
            float bnd = (ra.major_radius + ra.minor_radius) + (rb.major_radius + rb.minor_radius);
            if (dd.length_squared() > bnd * bnd) continue;

            // Narrow phase: sample ring a, find closest tube-to-tube distance
            Basis rot_a(ra.orient), rot_b(rb.orient);
            Basis inv_b = rot_b.inverse();
            int Nsamp = 16;
            float best_dist = 1e30f;
            Vector3 best_n_world;
            float best_overlap = 0.0f;
            Vector3 best_contact_a, best_contact_b;

            for (int si = 0; si < Nsamp; si++) {
                float theta_a = Math_TAU * (float)si / (float)Nsamp;
                Vector3 lp_a(std::cos(theta_a) * ra.major_radius, 0.0f, std::sin(theta_a) * ra.major_radius);
                Vector3 wp_a = ra.pos + rot_a.xform(lp_a);

                // Find nearest point on ring b's major circle to wp_a
                Vector3 d_ab = min_image_vec(wp_a - rb.pos);
                Vector3 local_b = inv_b.xform(d_ab);
                Vector3 flat_b(local_b.x, 0.0f, local_b.z);
                float fl_b = flat_b.length();
                Vector3 nearest_b;
                if (fl_b > 1e-6f)
                    nearest_b = flat_b * (rb.major_radius / fl_b);
                else
                    nearest_b = Vector3(rb.major_radius, 0, 0);
                Vector3 wp_b = rb.pos + rot_b.xform(nearest_b);

                Vector3 sep = min_image_vec(wp_a - wp_b);
                float tube_dist = sep.length();
                float ov = (ra.minor_radius + rb.minor_radius) - tube_dist;
                if (ov > best_overlap && tube_dist > 1e-6f) {
                    best_overlap = ov;
                    best_dist = tube_dist;
                    best_n_world = sep / tube_dist;  // normal from b toward a
                    best_contact_a = wp_a;
                    best_contact_b = wp_b;
                }
            }

            if (best_overlap > 0.0f) {
                Vector3 n = best_n_world;
                double vr = (ra.vel - rb.vel).dot(n);
                bool al = ra.locked, bl = rb.locked;

                if (vr < 0.0) {
                    Vector3 lever_a = best_contact_a - ra.pos;
                    Vector3 lever_b = best_contact_b - rb.pos;
                    float impulse_mag = 0.0f;

                    if (al && bl) {
                        // both locked
                    } else if (al) {
                        impulse_mag = (float)(-2.0 * vr);
                        rb.vel -= (float)(2.0 * vr) * n;
                    } else if (bl) {
                        impulse_mag = (float)(2.0 * vr);
                        ra.vel += (float)(2.0 * vr) * n;
                    } else {
                        double ma = ra.mass, mb = rb.mass;
                        double imp = 2.0 * vr / (ma + mb);
                        ra.vel -= (float)(imp * mb) * n;
                        rb.vel += (float)(imp * ma) * n;
                        impulse_mag = (float)(imp * mb);
                    }

                    // Spin from off-center impact
                    if (std::abs(impulse_mag) > 1e-6f) {
                        if (!al) {
                            Vector3 spin_a = lever_a.cross(n * (-impulse_mag));
                            ra.angular_vel += spin_a / ra.moment_of_inertia;
                        }
                        if (!bl) {
                            Vector3 spin_b = lever_b.cross(n * impulse_mag);
                            rb.angular_vel += spin_b / rb.moment_of_inertia;
                        }
                    }
                }

                // Separation
                if (al && bl) { /* nothing */ }
                else if (al) {
                    rb.pos -= n * best_overlap; wrap_position(rb.pos);
                } else if (bl) {
                    ra.pos += n * best_overlap; wrap_position(ra.pos);
                } else {
                    float frac = rb.mass / (ra.mass + rb.mass);
                    ra.pos += n * (best_overlap * frac); wrap_position(ra.pos);
                    rb.pos -= n * (best_overlap * (1.0f - frac)); wrap_position(rb.pos);
                }
            }
        }
    }
}

void ChargeSimulator3D::do_binding() {
    int N = (int)particles.size(); if (N < 2) return;
    std::unordered_set<int> used; used.reserve(N);
    std::vector<std::pair<int,int>> bp;

    auto try_bind = [&](auto &idx, auto pred, double thr2) {
        for (int a = 0; a < (int)idx.size(); a++) {
            int i = idx[a]; if (used.count(i)) continue;
            int best = -1; double bd = 1e300;
            for (int b = 0; b < (int)idx.size(); b++) {
                int j = idx[b]; if (j == i || used.count(j)) continue;
                if (!pred(i, j)) continue;
                double d2 = min_image_vec(particles[j].pos - particles[i].pos).length_squared();
                if (d2 < bd) { bd = d2; best = j; }
            }
            if (best != -1 && bd <= thr2) { bp.push_back({i,best}); used.insert(i); used.insert(best); }
        }
    };

    std::vector<int> ic, im;
    for (int i = 0; i < N; i++) {
        if (particles[i].locked) continue;  // locked particles never bind
        if (particles[i].charge != 0) ic.push_back(i);
        if (particles[i].mag_charge != 0) im.push_back(i);
    }

    double t2e = (bind_radius + bind_relax) * (bind_radius + bind_relax);
    try_bind(ic, [&](int i, int j) { return particles[i].charge * particles[j].charge == -1; }, t2e);

    double t2m = (bind_mag_radius + bind_mag_relax) * (bind_mag_radius + bind_mag_relax);
    try_bind(im, [&](int i, int j) { return particles[i].mag_charge * particles[j].mag_charge == -1; }, t2m);

    if (bp.empty()) return;
    std::vector<bool> keep(N, true);
    std::vector<Particle> nn; nn.reserve(bp.size());
    for (auto &pr : bp) {
        int i = pr.first, j = pr.second; if (!keep[i] || !keep[j]) continue;
        Vector3 rij = min_image_vec(particles[j].pos - particles[i].pos);
        double mi = particles[i].mass, mj = particles[j].mass, mt = mi + mj;
        Vector3 mp = particles[i].pos + (float)(mj/mt) * rij; wrap_position(mp);
        Particle p; p.pos = mp;
        p.vel = (float)(mi/mt) * particles[i].vel + (float)(mj/mt) * particles[j].vel;
        p.charge = 0; p.mag_charge = 0; p.mass = (float)mt;
        p.radius = neutral_radius_from_mass((float)mt);
        nn.push_back(p); keep[i] = false; keep[j] = false;
    }
    std::vector<Particle> next;
    for (int i = 0; i < N; i++) if (keep[i]) next.push_back(particles[i]);
    for (auto &p : nn) next.push_back(p);
    particles.swap(next);
}

// =====================================================================
// Rendering
// =====================================================================

void ChargeSimulator3D::ensure_render_nodes() {
    auto make = [&](const Color &c) -> MultiMeshInstance3D* {
        auto *mmi = memnew(MultiMeshInstance3D);
        Ref<SphereMesh> sp; sp.instantiate(); sp->set_radius(1.0); sp->set_height(2.0);
        Ref<StandardMaterial3D> mt; mt.instantiate(); mt->set_albedo(c);
        mt->set_feature(StandardMaterial3D::FEATURE_EMISSION, true);
        mt->set_emission(c);
        mt->set_emission_energy_multiplier(0.4);
        sp->surface_set_material(0, mt);
        Ref<MultiMesh> mm; mm.instantiate(); mm->set_mesh(sp);
        mm->set_transform_format(MultiMesh::TRANSFORM_3D); mm->set_instance_count(0);
        mmi->set_multimesh(mm); add_child(mmi); return mmi;
    };
    if (!mmi_pos) mmi_pos = make(Color(1.0,0.31,0.31));
    if (!mmi_neg) mmi_neg = make(Color(0.31,0.63,1.0));
    if (!mmi_neu) mmi_neu = make(Color(0.86,0.86,0.86));
    if (!mmi_north) mmi_north = make(Color(1.0,0.65,0.15));
    if (!mmi_south) mmi_south = make(Color(0.7,0.3,0.9));

    // Ring axis indicators
    if (!ring_axis_mi) {
        ring_axis_mi = memnew(MeshInstance3D); add_child(ring_axis_mi);
        ring_axis_mesh.instantiate(); ring_axis_mi->set_mesh(ring_axis_mesh);
        ring_axis_mat.instantiate();
        ring_axis_mat->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
        ring_axis_mat->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
        ring_axis_mat->set_albedo(Color(1, 1, 1, 0.6));
        ring_axis_mat->set_flag(StandardMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        ring_axis_mat->set_feature(StandardMaterial3D::FEATURE_EMISSION, true);
        ring_axis_mat->set_emission(Color(1, 1, 1));
        ring_axis_mat->set_emission_energy_multiplier(0.2);
        ring_axis_mi->set_material_override(ring_axis_mat);
    }

    // Particle spin indicators
    if (!spin_mi) {
        spin_mi = memnew(MeshInstance3D); add_child(spin_mi);
        spin_mesh.instantiate(); spin_mi->set_mesh(spin_mesh);
        spin_mat.instantiate();
        spin_mat->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
        spin_mat->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
        spin_mat->set_albedo(Color(1, 1, 1, 0.5));
        spin_mat->set_flag(StandardMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        spin_mat->set_feature(StandardMaterial3D::FEATURE_EMISSION, true);
        spin_mat->set_emission(Color(1, 1, 1));
        spin_mat->set_emission_energy_multiplier(0.15);
        spin_mi->set_material_override(spin_mat);
    }
}

void ChargeSimulator3D::update_render() {
    if (!mmi_pos) return;
    int np=0,nn_=0,nz=0,nN=0,nS=0;
    for (const auto &p : particles) {
        if (p.mag_charge > 0) nN++; else if (p.mag_charge < 0) nS++;
        else if (p.charge > 0) np++; else if (p.charge < 0) nn_++; else nz++;
    }
    auto mp = mmi_pos->get_multimesh(), mn = mmi_neg->get_multimesh(), mz = mmi_neu->get_multimesh();
    auto mN = mmi_north->get_multimesh(), mS = mmi_south->get_multimesh();
    mp->set_instance_count(np); mn->set_instance_count(nn_); mz->set_instance_count(nz);
    mN->set_instance_count(nN); mS->set_instance_count(nS);
    int ip=0,in_=0,iz=0,iN=0,iS=0;
    for (const auto &p : particles) {
        Transform3D t; t.origin = p.pos;
        Basis b(p.orient); b.scale_local(Vector3(p.radius,p.radius,p.radius)); t.basis = b;
        if (p.mag_charge > 0) mN->set_instance_transform(iN++, t);
        else if (p.mag_charge < 0) mS->set_instance_transform(iS++, t);
        else if (p.charge > 0) mp->set_instance_transform(ip++, t);
        else if (p.charge < 0) mn->set_instance_transform(in_++, t);
        else mz->set_instance_transform(iz++, t);
    }

    // Particle spin indicators (visible when |ω| > threshold)
    if (spin_mesh.is_valid()) {
        spin_mesh->clear_surfaces();
        const float spin_threshold = 0.1f;
        bool any_spinning = false;
        for (const auto &p : particles) {
            if (p.angular_vel.length() > spin_threshold) { any_spinning = true; break; }
        }
        if (any_spinning) {
            spin_mesh->surface_begin(Mesh::PRIMITIVE_LINES);
            for (const auto &p : particles) {
                float omega = p.angular_vel.length();
                if (omega <= spin_threshold) continue;
                Vector3 axis = p.angular_vel / omega;
                // Line length scales with spin speed, capped at 3× radius
                float len = p.radius * std::min(3.0f, 1.0f + omega * 0.5f);
                // Brightness scales with spin speed
                float alpha = std::min(0.7f, 0.2f + omega * 0.1f);
                Color col(0.9f, 0.9f, 1.0f, alpha);
                spin_mesh->surface_set_color(col);
                spin_mesh->surface_add_vertex(p.pos + axis * len);
                spin_mesh->surface_set_color(col);
                spin_mesh->surface_add_vertex(p.pos - axis * len);
            }
            spin_mesh->surface_end();
            spin_mi->set_visible(true);
        } else {
            spin_mi->set_visible(false);
        }
    }

    // Update ring visuals and axis indicators
    if (ring_axis_mesh.is_valid()) {
        ring_axis_mesh->clear_surfaces();
        if (!rings.empty()) {
            ring_axis_mesh->surface_begin(Mesh::PRIMITIVE_LINES);
            for (const auto &r : rings) {
                if (!r.visual) continue;
                Transform3D t; t.basis = Basis(r.orient); t.origin = r.pos;
                r.visual->set_global_transform(t);

                // Draw axis line: center ± axis * (major_radius * 0.8)
                Basis rot(r.orient);
                Vector3 axis = rot.xform(Vector3(0, 1, 0));
                float len = r.major_radius * 0.8f;

                // Positive axis (bright) — indicates "north" side
                Color col_pos(0.9f, 0.95f, 1.0f, 0.55f);
                Color col_neg(0.4f, 0.4f, 0.5f, 0.3f);
                ring_axis_mesh->surface_set_color(col_pos);
                ring_axis_mesh->surface_add_vertex(r.pos);
                ring_axis_mesh->surface_set_color(col_pos);
                ring_axis_mesh->surface_add_vertex(r.pos + axis * len);
                // Negative axis (dim)
                ring_axis_mesh->surface_set_color(col_neg);
                ring_axis_mesh->surface_add_vertex(r.pos);
                ring_axis_mesh->surface_set_color(col_neg);
                ring_axis_mesh->surface_add_vertex(r.pos - axis * len);

                // Small crosshair at positive tip for orientation clarity
                Vector3 tip = r.pos + axis * len;
                Vector3 u = rot.xform(Vector3(1, 0, 0)) * (r.minor_radius * 1.5f);
                Vector3 v = rot.xform(Vector3(0, 0, 1)) * (r.minor_radius * 1.5f);
                ring_axis_mesh->surface_set_color(col_pos);
                ring_axis_mesh->surface_add_vertex(tip - u);
                ring_axis_mesh->surface_set_color(col_pos);
                ring_axis_mesh->surface_add_vertex(tip + u);
                ring_axis_mesh->surface_set_color(col_pos);
                ring_axis_mesh->surface_add_vertex(tip - v);
                ring_axis_mesh->surface_set_color(col_pos);
                ring_axis_mesh->surface_add_vertex(tip + v);
            }
            ring_axis_mesh->surface_end();
            ring_axis_mi->set_visible(true);
        } else {
            ring_axis_mi->set_visible(false);
        }
    } else {
        for (const auto &r : rings) {
            if (!r.visual) continue;
            Transform3D t; t.basis = Basis(r.orient); t.origin = r.pos;
            r.visual->set_global_transform(t);
        }
    }
}

// =====================================================================
// Grid
// =====================================================================

void ChargeSimulator3D::ensure_grid_nodes() {
    if (grid_mi) return;
    grid_mi = memnew(MeshInstance3D); add_child(grid_mi);
    grid_mesh.instantiate(); grid_mi->set_mesh(grid_mesh);
    grid_mat.instantiate();
    grid_mat->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
    grid_mat->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
    grid_mat->set_albedo(Color(1,1,1,1));
    grid_mat->set_flag(StandardMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
    grid_mi->set_material_override(grid_mat); grid_mi->set_visible(false);
}

void ChargeSimulator3D::rebuild_grid() {
    ensure_grid_nodes(); grid_mesh->clear_surfaces();
    if (!show_grid) { grid_mi->set_visible(false); return; }
    grid_mi->set_visible(true);
    grid_mesh->surface_begin(Mesh::PRIMITIVE_LINES);

    float bx = (float)box_size.x, by = (float)box_size.y, bz = (float)box_size.z;
    float sp = grid_spacing;

    auto add_line = [&](const Vector3 &a, const Vector3 &b, const Color &c) {
        grid_mesh->surface_set_color(c); grid_mesh->surface_add_vertex(a);
        grid_mesh->surface_set_color(c); grid_mesh->surface_add_vertex(b);
    };

    if (topology == TOPO_SPHERE || topology == TOPO_SPHERE_WRAP) {
        Vector3 c = sphere_center(); float R = sphere_radius();
        // Reflect = green-teal, Wrap = violet-blue (portal feel)
        Color sc = (topology == TOPO_SPHERE) ? Color(0.35, 0.55, 0.4, 0.12)
                                              : Color(0.5, 0.35, 0.6, 0.14);
        int seg = 64;
        for (int i = 0; i < seg; i++) {
            float a0 = Math_TAU * (float)i / seg, a1 = Math_TAU * (float)(i+1) / seg;
            add_line(c + Vector3(Math::cos(a0),Math::sin(a0),0)*R,
                     c + Vector3(Math::cos(a1),Math::sin(a1),0)*R, sc);
            add_line(c + Vector3(Math::cos(a0),0,Math::sin(a0))*R,
                     c + Vector3(Math::cos(a1),0,Math::sin(a1))*R, sc);
            add_line(c + Vector3(0,Math::cos(a0),Math::sin(a0))*R,
                     c + Vector3(0,Math::cos(a1),Math::sin(a1))*R, sc);
        }
        Color lc = (topology == TOPO_SPHERE) ? Color(0.3, 0.45, 0.35, 0.06)
                                              : Color(0.4, 0.3, 0.5, 0.06);
        for (float lat = -0.6f; lat <= 0.61f; lat += 0.3f) {
            if (Math::abs(lat) < 0.01f) continue;
            float rr = R * std::sqrt(1.0f - lat*lat);
            float y = c.y + R * lat;
            for (int i = 0; i < seg; i++) {
                float a0 = Math_TAU * (float)i / seg, a1 = Math_TAU * (float)(i+1) / seg;
                add_line(Vector3(c.x + Math::cos(a0)*rr, y, c.z + Math::sin(a0)*rr),
                         Vector3(c.x + Math::cos(a1)*rr, y, c.z + Math::sin(a1)*rr), lc);
            }
        }
        // For wrap mode, add antipodal markers: small circles at poles
        if (topology == TOPO_SPHERE_WRAP) {
            Color pc(0.6, 0.4, 0.7, 0.1);
            for (float off : {-0.92f, 0.92f}) {
                float rr = R * std::sqrt(1.0f - off*off);
                float y = c.y + R * off;
                for (int i = 0; i < seg; i++) {
                    float a0 = Math_TAU * (float)i / seg, a1 = Math_TAU * (float)(i+1) / seg;
                    add_line(Vector3(c.x + Math::cos(a0)*rr, y, c.z + Math::sin(a0)*rr),
                             Vector3(c.x + Math::cos(a1)*rr, y, c.z + Math::sin(a1)*rr), pc);
                }
            }
        }
    } else {
        Color gc, bc;
        if (topology == TOPO_TORUS) {
            gc = Color(0.25, 0.30, 0.55, 0.06); bc = Color(0.35, 0.45, 0.75, 0.18);
        } else {
            gc = Color(0.40, 0.35, 0.20, 0.05); bc = Color(0.50, 0.45, 0.25, 0.08);
        }
        float yf = by * 0.5f;
        for (float x = 0; x <= bx + 0.1f; x += sp) add_line(Vector3(x,yf,0), Vector3(x,yf,bz), gc);
        for (float z = 0; z <= bz + 0.1f; z += sp) add_line(Vector3(0,yf,z), Vector3(bx,yf,z), gc);
        float xs = bx * 0.5f;
        for (float y = 0; y <= by + 0.1f; y += sp) add_line(Vector3(xs,y,0), Vector3(xs,y,bz), gc);
        for (float z = 0; z <= bz + 0.1f; z += sp) add_line(Vector3(xs,0,z), Vector3(xs,by,z), gc);

        if (topology == TOPO_TORUS) {
            // 12 box edges
            Vector3 c[8] = {{0,0,0},{bx,0,0},{bx,0,bz},{0,0,bz},{0,by,0},{bx,by,0},{bx,by,bz},{0,by,bz}};
            int e[][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
            for (auto &ed : e) add_line(c[ed[0]], c[ed[1]], bc);
        }
    }
    grid_mesh->surface_end();
}

// =====================================================================
// Generic field line tracer (macro-like lambda factory)
// =====================================================================

// Each rebuild function follows the same pattern:
// 1. ensure nodes, clear, early-out if off
// 2. begin PRIMITIVE_LINES surface
// 3. trace lines, collecting arrow positions
// 4. end lines surface
// 5. draw arrows as PRIMITIVE_TRIANGLES

#define FIELD_REBUILD_BEGIN(SHOW, MI, MESH, COMPUTE, STEPS, DS, STOP_BUF) \
    MESH->clear_surfaces(); \
    if (!(SHOW)) { MI->set_visible(false); return; } \
    MI->set_visible(true); \
    MESH->surface_begin(Mesh::PRIMITIVE_LINES); \
    std::vector<std::tuple<Vector3,Vector3,float>> _arrows_; \
    int _trace_steps_ = STEPS; \
    auto too_close = [&](const Vector3 &x) -> bool { \
        for (const auto &mag : magnets) { \
            Vector3 d = min_image_vec(x - mag.pos); \
            if (d.length_squared() < (mag.radius + STOP_BUF) * (mag.radius + STOP_BUF)) return true; \
        } \
        for (const auto &q : particles) { \
            if (q.charge == 0 && q.mag_charge == 0) continue; \
            Vector3 d = min_image_vec(x - q.pos); \
            if (d.length_squared() < (q.radius + STOP_BUF) * (q.radius + STOP_BUF)) return true; \
        } \
        for (const auto &rng : rings) { \
            Vector3 d = min_image_vec(x - rng.pos); \
            Vector3 local = Basis(rng.orient).inverse().xform(d); \
            Vector3 flat(local.x, 0.0f, local.z); \
            float fl = flat.length(); \
            if (fl > 1e-6f) flat = flat * (rng.major_radius / fl); \
            else flat = Vector3(rng.major_radius, 0, 0); \
            float dist2 = (local - flat).length_squared(); \
            float thr = rng.minor_radius + STOP_BUF; \
            if (dist2 < thr * thr) return true; \
        } \
        return false; \
    }; \
    auto trace = [&](Vector3 start, int dir_sign) { \
        Vector3 p = start; wrap_position(p); \
        for (int k = 0; k < 6 && too_close(p); k++) { \
            Vector3 F = COMPUTE; float mag = F.length(); if (mag < 1e-6f) break; \
            p += (float)dir_sign * (F / mag) * (DS * 0.75f); wrap_position(p); \
            if (topology == TOPO_SPHERE) { Vector3 dummy; reflect_at_sphere(p, dummy); } \
            if (topology == TOPO_SPHERE_WRAP) wrap_at_sphere(p); \
        } \
        if (too_close(p)) return; \
        /* Reference magnitude at trace start — used for decay cutoff & adaptive step */ \
        float ref_mag = (COMPUTE).length(); \
        if (ref_mag < 1e-6f) return; \
        Vector3 prev_dir(0,0,0); \
        int arrow_step = _trace_steps_ / 3; \
        for (int s = 0; s < _trace_steps_; s++) { \
            Vector3 F = COMPUTE; float mag = F.length(); if (mag < 1e-6f) break; \
            /* Stop tracing once field decays to <5% of starting strength */ \
            if (mag < ref_mag * 0.05f) break; \
            Vector3 sd = (float)dir_sign * (F / mag); \
            /* Anti-crumple: kill the line if direction flips >90° between steps */ \
            if (s > 0 && sd.dot(prev_dir) < 0.0f) break; \
            prev_dir = sd; \
            /* Adaptive step: shrink in weak field to keep curves smooth */ \
            float adapt = std::min(1.0f, mag / ref_mag); \
            float step_ds = DS * (0.3f + 0.7f * adapt); \
            Vector3 pn = p + sd * step_ds; wrap_position(pn); \
            if (topology == TOPO_SPHERE) { Vector3 dv; reflect_at_sphere(pn, dv); } \
            if (topology == TOPO_SPHERE_WRAP) wrap_at_sphere(pn); \
            draw_field_segment(MESH, p, pn); \
            if (s == arrow_step && point_inside_boundary(p)) \
                _arrows_.push_back(std::make_tuple(p, sd, mag)); \
            p = pn; if (too_close(p)) break; \
            if (!point_inside_boundary(p)) break; \
        } \
    };

#define FIELD_REBUILD_END(MESH, ARROW_SIZE) \
    MESH->surface_end(); \
    if (!_arrows_.empty()) { \
        float _max_mag_ = 0.0f; \
        for (const auto &ar : _arrows_) _max_mag_ = std::max(_max_mag_, std::get<2>(ar)); \
        if (_max_mag_ < 1e-6f) _max_mag_ = 1.0f; \
        MESH->surface_begin(Mesh::PRIMITIVE_TRIANGLES); \
        for (const auto &ar : _arrows_) { \
            float frac = std::get<2>(ar) / _max_mag_; \
            float sz = ARROW_SIZE * (0.25f + 0.75f * frac); \
            emit_arrow(MESH, std::get<0>(ar), std::get<1>(ar), sz); \
        } \
        MESH->surface_end(); \
    }

// =====================================================================
// E field lines
// =====================================================================

void ChargeSimulator3D::ensure_field_nodes() {
    if (field_mi) return;
    field_mi = memnew(MeshInstance3D); add_child(field_mi);
    field_mesh.instantiate(); field_mi->set_mesh(field_mesh);
    field_mat.instantiate();
    field_mat->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
    field_mat->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
    field_mat->set_albedo(Color(1,1,1,0.85));
    field_mat->set_feature(StandardMaterial3D::FEATURE_EMISSION, true);
    field_mat->set_emission(Color(1,1,1));
    field_mat->set_emission_energy_multiplier(0.25);
    field_mi->set_material_override(field_mat); field_mi->set_visible(false);
}

void ChargeSimulator3D::rebuild_field_lines() {
    ensure_field_nodes();

    auto fib = [](int i, int n) -> Vector3 {
        float g=2.39996322972865332f, y=1.0f-2.0f*(float(i)+0.5f)/float(n);
        float r=std::sqrt(std::max(0.0f,1.0f-y*y)), th=g*float(i);
        return Vector3(std::cos(th)*r, y, std::sin(th)*r);
    };

    int nlines = std::max(2, field_lines_per_charge);
    float sg = field_seed_gap;

    FIELD_REBUILD_BEGIN(show_field_lines, field_mi, field_mesh,
        compute_e_field_at(p), field_steps, field_ds, field_stop_r)

    for (const auto &q : particles) {
        if (q.charge == 0) continue;
        int ds = (q.charge > 0) ? +1 : -1;
        for (int i = 0; i < nlines; i++) {
            Vector3 seed = q.pos + fib(i, nlines) * (q.radius + sg);
            wrap_position(seed); trace(seed, ds);
        }
    }
    // Moving monopoles (ring seed) — dual Biot-Savart: E curls around g*v
    {
        int nmo = std::max(2, e_lines_per_monopole);
        for (const auto &q : particles) {
            if (q.mag_charge == 0 || q.vel.length() < 0.01f) continue;
            float speed_frac = std::min(1.0f, (float)(q.vel.length() / max_speed));
            _trace_steps_ = std::max(4, (int)(field_steps * speed_frac));
            Vector3 ax = q.vel.normalized(), up(0,1,0);
            if (Math::abs(ax.dot(up)) > 0.95f) up = Vector3(1,0,0);
            Vector3 u = up.cross(ax).normalized(), v = ax.cross(u).normalized();
            for (int i = 0; i < nmo; i++) {
                float a = (float)i * Math_TAU / (float)nmo;
                Vector3 seed = q.pos + (Math::cos(a)*u + Math::sin(a)*v) * 32.0f;
                wrap_position(seed); trace(seed, +1); trace(seed, -1);
            }
        }
        _trace_steps_ = field_steps;
    }
    // Charge ring seeds for E field
    for (const auto &rng : rings) {
        if (rng.source_type != RING_CHARGE) continue;
        int ns = std::max(4, nlines / 2);
        Basis rot(rng.orient);
        float off = rng.minor_radius + sg;
        int ds = (rng.strength >= 0) ? +1 : -1;
        for (int i = 0; i < ns; i++) {
            float theta = Math_TAU * (float)i / (float)ns;
            Vector3 lp(std::cos(theta) * rng.major_radius, 0.0f, std::sin(theta) * rng.major_radius);
            Vector3 radial(std::cos(theta), 0.0f, std::sin(theta));
            // Seed outward radially from tube surface
            Vector3 seed = rng.pos + rot.xform(lp + radial * off);
            wrap_position(seed); trace(seed, ds);
            // Seed above and below ring plane
            seed = rng.pos + rot.xform(lp + Vector3(0, off, 0));
            wrap_position(seed); trace(seed, ds);
            seed = rng.pos + rot.xform(lp + Vector3(0, -off, 0));
            wrap_position(seed); trace(seed, ds);
        }
    }

    FIELD_REBUILD_END(field_mesh, 8.0f)
}

// =====================================================================
// B field lines
// =====================================================================

void ChargeSimulator3D::ensure_bfield_nodes() {
    if (bfield_mi) return;
    bfield_mi = memnew(MeshInstance3D); add_child(bfield_mi);
    bfield_mesh.instantiate(); bfield_mi->set_mesh(bfield_mesh);
    bfield_mat.instantiate();
    bfield_mat->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
    bfield_mat->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
    bfield_mat->set_albedo(Color(0.35,0.75,1.0,0.85));
    bfield_mat->set_feature(StandardMaterial3D::FEATURE_EMISSION, true);
    bfield_mat->set_emission(Color(0.35,0.75,1.0));
    bfield_mat->set_emission_energy_multiplier(0.25);
    bfield_mi->set_material_override(bfield_mat); bfield_mi->set_visible(false);
}

void ChargeSimulator3D::rebuild_b_field_lines() {
    ensure_bfield_nodes();

    auto fib = [](int i, int n) -> Vector3 {
        float g=2.39996322972865332f, y=1.0f-2.0f*(float(i)+0.5f)/float(n);
        float r=std::sqrt(std::max(0.0f,1.0f-y*y)), th=g*float(i);
        return Vector3(std::cos(th)*r, y, std::sin(th)*r);
    };

    int nm = std::max(2, b_lines_per_magnet);
    int nc = std::max(2, b_lines_per_charge);
    int nmo = std::max(2, b_lines_per_monopole);
    float sg = b_seed_gap;

    FIELD_REBUILD_BEGIN(show_b_field_lines, bfield_mi, bfield_mesh,
        compute_b_field_at(p), b_field_steps, b_field_ds, b_field_stop_buffer)

    // Magnets
    for (const auto &m : magnets) {
        for (int i = 0; i < nm; i++) {
            Vector3 seed = m.pos + fib(i, nm) * (m.radius + sg);
            wrap_position(seed); trace(seed, +1); trace(seed, -1);
        }
    }
    // Moving charges (ring) — full seed count preserved for detail,
    // but trace length scales with |v|/max_speed (B ∝ qv).
    for (const auto &q : particles) {
        if (q.charge == 0 || q.vel.length() < 0.01f) continue;
        float speed_frac = std::min(1.0f, (float)(q.vel.length() / max_speed));
        _trace_steps_ = std::max(4, (int)(b_field_steps * speed_frac));
        Vector3 ax = q.vel.normalized(), up(0,1,0);
        if (Math::abs(ax.dot(up)) > 0.95f) up = Vector3(1,0,0);
        Vector3 u = up.cross(ax).normalized(), v = ax.cross(u).normalized();
        for (int i = 0; i < nc; i++) {
            float a = (float)i * Math_TAU / (float)nc;
            Vector3 seed = q.pos + (Math::cos(a)*u + Math::sin(a)*v) * 32.0f;
            wrap_position(seed); trace(seed, +1); trace(seed, -1);
        }
    }
    _trace_steps_ = b_field_steps;
    // Monopoles
    for (const auto &q : particles) {
        if (q.mag_charge == 0) continue;
        int ds = (q.mag_charge > 0) ? +1 : -1;
        for (int i = 0; i < nmo; i++) {
            Vector3 seed = q.pos + fib(i, nmo) * (q.radius + sg);
            wrap_position(seed); trace(seed, ds);
        }
    }
    // Ring seeds for B field
    for (const auto &rng : rings) {
        Basis rot(rng.orient);
        float off = rng.minor_radius + sg;
        if (rng.source_type == RING_CURRENT) {
            // Current ring: B loops through center and outside — seed above, below, outward
            int ns = std::max(4, nm / 2);
            for (int i = 0; i < ns; i++) {
                float theta = Math_TAU * (float)i / (float)ns;
                Vector3 lp(std::cos(theta) * rng.major_radius, 0.0f, std::sin(theta) * rng.major_radius);
                Vector3 radial(std::cos(theta), 0.0f, std::sin(theta));
                Vector3 seed = rng.pos + rot.xform(lp + radial * off);
                wrap_position(seed); trace(seed, +1); trace(seed, -1);
                seed = rng.pos + rot.xform(lp + Vector3(0, off, 0));
                wrap_position(seed); trace(seed, +1); trace(seed, -1);
            }
            // Seed at center of ring (B goes straight through the hole)
            Vector3 center_above = rng.pos + rot.xform(Vector3(0, off * 0.5f, 0));
            wrap_position(center_above); trace(center_above, +1); trace(center_above, -1);
        }
        if (rng.source_type == RING_MONOPOLE) {
            // Monopole ring: radial B lines from each sample
            int ns = std::max(4, nmo / 2);
            int ds = (rng.strength >= 0) ? +1 : -1;
            for (int i = 0; i < ns; i++) {
                float theta = Math_TAU * (float)i / (float)ns;
                Vector3 lp(std::cos(theta) * rng.major_radius, 0.0f, std::sin(theta) * rng.major_radius);
                Vector3 radial(std::cos(theta), 0.0f, std::sin(theta));
                Vector3 seed = rng.pos + rot.xform(lp + radial * off);
                wrap_position(seed); trace(seed, ds);
                seed = rng.pos + rot.xform(lp + Vector3(0, off, 0));
                wrap_position(seed); trace(seed, ds);
                seed = rng.pos + rot.xform(lp + Vector3(0, -off, 0));
                wrap_position(seed); trace(seed, ds);
            }
        }
    }

    FIELD_REBUILD_END(bfield_mesh, 8.0f)
}

// =====================================================================
// A field
// =====================================================================

void ChargeSimulator3D::ensure_afield_nodes() {
    if (afield_mi) return;
    afield_mi = memnew(MeshInstance3D); add_child(afield_mi);
    afield_mesh.instantiate(); afield_mi->set_mesh(afield_mesh);
    afield_mat.instantiate();
    afield_mat->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
    afield_mat->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
    afield_mat->set_albedo(Color(0.55,1.0,0.3,0.85));
    afield_mat->set_feature(StandardMaterial3D::FEATURE_EMISSION, true);
    afield_mat->set_emission(Color(0.55,1.0,0.3));
    afield_mat->set_emission_energy_multiplier(0.25);
    afield_mi->set_material_override(afield_mat); afield_mi->set_visible(false);
}

void ChargeSimulator3D::rebuild_a_field_lines() {
    ensure_afield_nodes();
    auto fib = [](int i, int n) -> Vector3 {
        float g=2.39996322972865332f, y=1.0f-2.0f*(float(i)+0.5f)/float(n);
        float r=std::sqrt(std::max(0.0f,1.0f-y*y)), th=g*float(i);
        return Vector3(std::cos(th)*r, y, std::sin(th)*r);
    };
    int nm = std::max(2, a_lines_per_magnet), nc = std::max(2, a_lines_per_charge);
    float sg = a_seed_gap;

    FIELD_REBUILD_BEGIN(show_a_field_lines, afield_mi, afield_mesh,
        compute_a_field_at(p), a_field_steps, a_field_ds, a_field_stop_buffer)

    for (const auto &m : magnets) {
        Vector3 ax = m.m.length() > 1e-6f ? m.m.normalized() : Vector3(1,0,0);
        Vector3 up(0,1,0); if (Math::abs(ax.dot(up))>0.95f) up=Vector3(1,0,0);
        Vector3 u=up.cross(ax).normalized(), v=ax.cross(u).normalized();
        for (int i=0; i<nm; i++) {
            float a=(float)i*Math_TAU/(float)nm;
            Vector3 seed=m.pos+(Math::cos(a)*u+Math::sin(a)*v)*(m.radius+sg);
            wrap_position(seed); trace(seed,+1); trace(seed,-1);
        }
        for (int i=0; i<nm/2; i++) {
            Vector3 seed=m.pos+fib(i,nm/2)*(m.radius+sg+20.0f);
            wrap_position(seed); trace(seed,+1); trace(seed,-1);
        }
    }
    for (const auto &q : particles) {
        if (q.charge==0||q.vel.length()<0.01f) continue;
        float speed_frac = std::min(1.0f, (float)(q.vel.length() / max_speed));
        _trace_steps_ = std::max(4, (int)(a_field_steps * speed_frac));
        for (int i=0; i<nc; i++) {
            Vector3 seed=q.pos+fib(i,nc)*(q.radius+sg);
            wrap_position(seed); trace(seed,+1); trace(seed,-1);
        }
    }
    _trace_steps_ = a_field_steps;
    // Current ring seeds for A field (A circulates around current loops)
    for (const auto &rng : rings) {
        if (rng.source_type != RING_CURRENT) continue;
        int ns = std::max(4, nm / 2);
        Basis rot(rng.orient);
        float off = rng.minor_radius + sg;
        for (int i = 0; i < ns; i++) {
            float theta = Math_TAU * (float)i / (float)ns;
            Vector3 lp(std::cos(theta) * rng.major_radius, 0.0f, std::sin(theta) * rng.major_radius);
            Vector3 radial(std::cos(theta), 0.0f, std::sin(theta));
            Vector3 seed = rng.pos + rot.xform(lp + radial * off);
            wrap_position(seed); trace(seed, +1); trace(seed, -1);
            seed = rng.pos + rot.xform(lp + Vector3(0, off, 0));
            wrap_position(seed); trace(seed, +1); trace(seed, -1);
        }
    }

    FIELD_REBUILD_END(afield_mesh, 8.0f)
}

// =====================================================================
// C field
// =====================================================================

void ChargeSimulator3D::ensure_cfield_nodes() {
    if (cfield_mi) return;
    cfield_mi = memnew(MeshInstance3D); add_child(cfield_mi);
    cfield_mesh.instantiate(); cfield_mi->set_mesh(cfield_mesh);
    cfield_mat.instantiate();
    cfield_mat->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
    cfield_mat->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
    cfield_mat->set_albedo(Color(1.0,0.55,0.85,0.85));
    cfield_mat->set_feature(StandardMaterial3D::FEATURE_EMISSION, true);
    cfield_mat->set_emission(Color(1.0,0.55,0.85));
    cfield_mat->set_emission_energy_multiplier(0.25);
    cfield_mi->set_material_override(cfield_mat); cfield_mi->set_visible(false);
}

void ChargeSimulator3D::rebuild_c_field_lines() {
    ensure_cfield_nodes();
    auto fib = [](int i, int n) -> Vector3 {
        float g=2.39996322972865332f, y=1.0f-2.0f*(float(i)+0.5f)/float(n);
        float r=std::sqrt(std::max(0.0f,1.0f-y*y)), th=g*float(i);
        return Vector3(std::cos(th)*r, y, std::sin(th)*r);
    };
    int nm = std::max(2, c_lines_per_monopole); float sg = c_seed_gap;

    FIELD_REBUILD_BEGIN(show_c_field_lines, cfield_mi, cfield_mesh,
        compute_c_field_at(p), c_field_steps, c_field_ds, c_field_stop_buffer)

    for (const auto &q : particles) {
        if (q.mag_charge==0||q.vel.length()<0.01f) continue;
        float speed_frac = std::min(1.0f, (float)(q.vel.length() / max_speed));
        _trace_steps_ = std::max(4, (int)(c_field_steps * speed_frac));
        for (int i=0; i<nm; i++) {
            Vector3 seed=q.pos+fib(i,nm)*(q.radius+sg);
            wrap_position(seed); trace(seed,+1); trace(seed,-1);
        }
    }
    _trace_steps_ = c_field_steps;

    FIELD_REBUILD_END(cfield_mesh, 8.0f)
}

// =====================================================================
// S field (Poynting vector: E × B)
// =====================================================================

void ChargeSimulator3D::ensure_sfield_nodes() {
    if (sfield_mi) return;
    sfield_mi = memnew(MeshInstance3D); add_child(sfield_mi);
    sfield_mesh.instantiate(); sfield_mi->set_mesh(sfield_mesh);
    sfield_mat.instantiate();
    sfield_mat->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
    sfield_mat->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
    sfield_mat->set_albedo(Color(1.0, 0.75, 0.2, 0.85));
    sfield_mat->set_feature(StandardMaterial3D::FEATURE_EMISSION, true);
    sfield_mat->set_emission(Color(1.0, 0.75, 0.2));
    sfield_mat->set_emission_energy_multiplier(0.25);
    sfield_mi->set_material_override(sfield_mat); sfield_mi->set_visible(false);
}

void ChargeSimulator3D::rebuild_s_field_lines() {
    ensure_sfield_nodes();
    auto fib = [](int i, int n) -> Vector3 {
        float g=2.39996322972865332f, y=1.0f-2.0f*(float(i)+0.5f)/float(n);
        float r=std::sqrt(std::max(0.0f,1.0f-y*y)), th=g*float(i);
        return Vector3(std::cos(th)*r, y, std::sin(th)*r);
    };
    int ns = std::max(2, s_lines_per_source); float sg = s_seed_gap;

    FIELD_REBUILD_BEGIN(show_s_field_lines, sfield_mi, sfield_mesh,
        compute_s_field_at(p), s_field_steps, s_field_ds, s_field_stop_buffer)

    for (const auto &q : particles) {
        if (q.charge == 0 && q.mag_charge == 0) continue;
        for (int i = 0; i < ns; i++) {
            Vector3 seed = q.pos + fib(i, ns) * (q.radius + sg + 10.0f);
            wrap_position(seed); trace(seed, +1); trace(seed, -1);
        }
    }
    for (const auto &m : magnets) {
        for (int i = 0; i < ns; i++) {
            Vector3 seed = m.pos + fib(i, ns) * (m.radius + sg + 10.0f);
            wrap_position(seed); trace(seed, +1); trace(seed, -1);
        }
    }
    // Ring seeds for S field
    for (const auto &rng : rings) {
        Basis rot(rng.orient);
        float off = rng.minor_radius + sg + 10.0f;
        int nrs = std::max(4, ns / 2);
        for (int i = 0; i < nrs; i++) {
            float theta = Math_TAU * (float)i / (float)nrs;
            Vector3 lp(std::cos(theta) * rng.major_radius, 0.0f, std::sin(theta) * rng.major_radius);
            Vector3 radial(std::cos(theta), 0.0f, std::sin(theta));
            Vector3 seed = rng.pos + rot.xform(lp + radial * off);
            wrap_position(seed); trace(seed, +1); trace(seed, -1);
            seed = rng.pos + rot.xform(lp + Vector3(0, off, 0));
            wrap_position(seed); trace(seed, +1); trace(seed, -1);
        }
    }

    FIELD_REBUILD_END(sfield_mesh, 10.0f)
}

#undef FIELD_REBUILD_BEGIN
#undef FIELD_REBUILD_END

} // namespace godot