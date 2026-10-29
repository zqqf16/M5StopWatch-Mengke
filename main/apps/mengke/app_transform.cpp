// 萌可变身: shake / spin the watch (BMI270) or press A to charge a magic energy ring; sparkles + rising chimes.
// Full -> transformation (light rays, colour flash, star bursts) reveals a random Mengke + spoken name/intro.
// Reveal: A/B/tap or shake -> charge again. Energy never drains (no failure). Hold A+B = home.
#include "apps_mengke.h"
#include "mengke_common.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <esp_random.h>
#include <cmath>
#include <algorithm>

using namespace mooncake;

static constexpr int kRays = 12;

struct AppMengkeTransform::State {
    enum Phase { Charge, Cast, Reveal } phase = Charge;
    float energy = 0;               // 0..100
    int quarter = 0, result = 0;
    float last_mag = 1.0f;
    uint32_t imu_tick = 0, t_act = 0, t_hint = 0, t_cast = 0, t_spark = 0, until = 0, lock_until = 0;
    lv_obj_t *charge = nullptr, *ring = nullptr, *core = nullptr, *pct = nullptr;
    lv_obj_t *cast = nullptr, *rays[kRays] = {}, *flash = nullptr;
    lv_obj_t *reveal = nullptr, *img = nullptr;
    volatile bool tapped = false;
};

static uint32_t _tr_color = 0xFF6FA8;

AppMengkeTransform::AppMengkeTransform()
{
    setAppInfo().name     = "萌可变身";
    setAppInfo().icon     = (void*)mk::decodedPinned(&mengke_icon_transform);
    setAppInfo().userData = &_tr_color;
}

void AppMengkeTransform::onCreate() { mclog::tagInfo("Transform", "on create"); }

static void start_charge(AppMengkeTransform::State* s, bool first, uint32_t now)
{
    s->phase = AppMengkeTransform::State::Charge;
    s->energy = 0; s->quarter = 0; s->t_act = s->t_hint = now; s->lock_until = now + 600;
    lv_obj_remove_flag(s->charge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s->cast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s->reveal, LV_OBJ_FLAG_HIDDEN);
    lv_arc_set_value(s->ring, 0);
    mk::play(first ? mengke_sfx_t_intro : mengke_sfx_t_again, first ? "transform intro" : "transform again");
    mclog::tagInfo("Transform", "charge start");
}

static void add_energy(AppMengkeTransform::State* s, lv_obj_t* root, float e, uint32_t now, const char* how)
{
    if (e <= 0) return;
    GetHAL().noteUserActivity();
    s->energy = std::min(100.0f, s->energy + e);
    s->t_act = now;
    lv_arc_set_value(s->ring, (int)s->energy);
    int size = 120 + (int)(s->energy * 1.2f);
    lv_obj_set_size(s->core, size, size);
    lv_obj_center(s->core);
    lv_obj_set_style_bg_opa(s->core, (lv_opa_t)(60 + s->energy * 1.9f), 0);
    lv_label_set_text_fmt(s->pct, "%d", (int)s->energy);
    if (now - s->t_spark > 250) {  // sparkles at the tip of the ring
        s->t_spark = now;
        int ang = (int)(s->energy * 3.6f) - 90;
        mk::starBurst(root, 233 + 205 * lv_trigo_cos(ang) / 32768, 233 + 205 * lv_trigo_sin(ang) / 32768, 4, 40);
    }
    int q = std::min(4, (int)(s->energy / 25));
    if (q > s->quarter) {
        s->quarter = q;
        static const MengkeClip* ch[4] = {&mengke_sfx_ch1, &mengke_sfx_ch2, &mengke_sfx_ch3, &mengke_sfx_ch4};
        if (q == 2) mk::playSeq({&mengke_sfx_ch2, &mengke_sfx_t_half}, "transform half");
        else if (q == 3) mk::playSeq({&mengke_sfx_ch3, &mengke_sfx_t_almost}, "transform almost");
        else if (q < 4) mk::play(*ch[q - 1], "transform chime");
        GetHAL().vibrate(30);
        mclog::tagInfo("Transform", "energy {} ({}) by {}", (int)s->energy, q, how);
    }
}

static lv_point_precise_t _ray_pts[kRays][2];
static void set_rays(lv_obj_t* cast, int off, int len)
{
    for (int i = 0; i < kRays; i++) {
        int ang = (i * 360 / kRays + off) % 360;
        _ray_pts[i][0] = {(lv_value_precise_t)(233 + 50 * lv_trigo_cos(ang) / 32768), (lv_value_precise_t)(233 + 50 * lv_trigo_sin(ang) / 32768)};
        _ray_pts[i][1] = {(lv_value_precise_t)(233 + len * lv_trigo_cos(ang) / 32768), (lv_value_precise_t)(233 + len * lv_trigo_sin(ang) / 32768)};
        lv_obj_t* l = lv_obj_get_child(cast, i + 1);  // child 0 = flash
        if (l) lv_line_set_points(l, _ray_pts[i], 2);
    }
}
static void anim_rot(void* o, int32_t v) { set_rays((lv_obj_t*)o, v / 10, 80 + std::min<int>(160, v / 4)); }
static void anim_bg(void* o, int32_t v)
{
    static const uint32_t cols[] = {0xFFFFFF, 0xFF6FA8, 0xF5D46B, 0x7FB2FF, 0xC59BFF, 0x5ED6C0};
    lv_obj_set_style_bg_color((lv_obj_t*)o, lv_color_hex(cols[(v / 40) % 6]), 0);
    lv_obj_set_style_bg_opa((lv_obj_t*)o, (lv_opa_t)(v < 400 ? 255 - v / 2 : 55), 0);
}

static void start_cast(AppMengkeTransform::State* s, lv_obj_t* root, uint32_t now)
{
    s->phase = AppMengkeTransform::State::Cast;
    s->t_cast = now;
    lv_obj_add_flag(s->charge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s->cast, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s->cast);
    lv_anim_set_exec_cb(&a, anim_rot);
    lv_anim_set_values(&a, 0, 1800);
    lv_anim_set_duration(&a, 2400);
    lv_anim_start(&a);
    lv_anim_set_var(&a, s->flash);
    lv_anim_set_exec_cb(&a, anim_bg);
    lv_anim_set_values(&a, 0, 480);
    lv_anim_start(&a);
    mk::starBurst(root, 233, 233, 20, 230);
    mk::starBurst(root, 233, 233, 12, 120);
    GetHAL().vibrate(150);
    static const MengkeClip* casts[3] = {&mengke_sfx_t_cast1, &mengke_sfx_t_cast2, &mengke_sfx_t_cast3};
    mk::playSeq({&mengke_sfx_t_full, &mengke_sfx_sparkle, casts[esp_random() % 3]}, "transform cast");
    mclog::tagInfo("Transform", "full energy -> transformation");
}

static void do_reveal(AppMengkeTransform::State* s, lv_obj_t* root, uint32_t now)
{
    s->phase = AppMengkeTransform::State::Reveal;
    s->result = esp_random() % MENGKE_COUNT;
    const Mengke& m = mengke_list[s->result];
    lv_obj_add_flag(s->cast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s->reveal, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(s->img, mk::decoded(&m.avatar));
    lv_obj_set_style_border_color(s->reveal, lv_color_hex(m.color), 0);
    mk::starBurst(root, 233, 233, 24, 230);
    mk::bounce(s->img, 3);
    mk::playSeq({&mengke_sfx_sparkle, &mengke_sfx_t_become, &m.name_clip, &m.intro_clip}, "transform reveal");
    s->lock_until = now + 3000;  // let her name be heard first
    s->until = now + 30000;
    mclog::tagInfo("Transform", "became {} ({})", m.en, s->result);
}

void AppMengkeTransform::onOpen()
{
    mclog::tagInfo("Transform", "on open");
    _keys = std::make_unique<input::KeyManager>();
    _s    = std::make_unique<State>();
    auto* s = _s.get();
    LvglLockGuard lock;
    _root = mk::makeRoot(0x120A2E);
    lv_obj_add_event_cb(_root, [](lv_event_t* e) { ((State*)lv_event_get_user_data(e))->tapped = true; }, LV_EVENT_SHORT_CLICKED, s);

    // charge layer: big energy ring + glowing core with a star
    s->charge = lv_obj_create(_root);
    lv_obj_remove_style_all(s->charge);
    lv_obj_set_size(s->charge, 466, 466);
    lv_obj_remove_flag(s->charge, LV_OBJ_FLAG_CLICKABLE);
    s->ring = lv_arc_create(s->charge);
    lv_obj_set_size(s->ring, 440, 440);
    lv_obj_center(s->ring);
    lv_arc_set_rotation(s->ring, 270);
    lv_arc_set_bg_angles(s->ring, 0, 360);
    lv_arc_set_range(s->ring, 0, 100);
    lv_arc_set_value(s->ring, 0);
    lv_obj_remove_style(s->ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s->ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s->ring, 34, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s->ring, 34, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s->ring, lv_color_hex(0x2C2456), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s->ring, lv_color_hex(0xFF6FA8), LV_PART_INDICATOR);
    s->core = lv_obj_create(s->charge);
    lv_obj_remove_style_all(s->core);
    lv_obj_set_size(s->core, 120, 120);
    lv_obj_set_style_radius(s->core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s->core, lv_color_hex(0xFFB8D8), 0);
    lv_obj_set_style_bg_opa(s->core, 60, 0);
    lv_obj_center(s->core);
    lv_obj_remove_flag(s->core, LV_OBJ_FLAG_CLICKABLE);
    auto* st = mk::makeStar(s->charge, 150, 0xF5D46B);
    lv_obj_center(st);
    s->pct = mk::makeLabel(s->charge, &mengke_font_26, 0xFFFFFF, "0");
    lv_obj_align(s->pct, LV_ALIGN_CENTER, 0, 120);

    // cast layer: colour flash + rotating light rays
    s->cast = lv_obj_create(_root);
    lv_obj_remove_style_all(s->cast);
    lv_obj_set_size(s->cast, 466, 466);
    lv_obj_remove_flag(s->cast, LV_OBJ_FLAG_CLICKABLE);
    s->flash = lv_obj_create(s->cast);
    lv_obj_remove_style_all(s->flash);
    lv_obj_set_size(s->flash, 466, 466);
    lv_obj_set_style_radius(s->flash, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s->flash, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s->flash, lv_color_hex(0xFFFFFF), 0);
    static const uint32_t rc[] = {0xFFFFFF, 0xF5D46B, 0xFF6FA8};
    for (int i = 0; i < kRays; i++) {
        s->rays[i] = lv_line_create(s->cast);
        lv_obj_set_pos(s->rays[i], 0, 0);
        lv_obj_set_size(s->rays[i], 466, 466);
        lv_obj_set_style_line_width(s->rays[i], i % 2 ? 14 : 26, 0);
        lv_obj_set_style_line_rounded(s->rays[i], true, 0);
        lv_obj_set_style_line_color(s->rays[i], lv_color_hex(rc[i % 3]), 0);
        lv_obj_set_style_line_opa(s->rays[i], LV_OPA_80, 0);
    }
    set_rays(s->cast, 0, 240);
    auto* big = mk::makeStar(s->cast, 200, 0xFFFFFF);
    lv_obj_center(big);
    lv_obj_add_flag(s->cast, LV_OBJ_FLAG_HIDDEN);

    // reveal layer
    s->reveal = lv_obj_create(_root);
    lv_obj_remove_style_all(s->reveal);
    lv_obj_set_size(s->reveal, 466, 466);
    lv_obj_set_style_radius(s->reveal, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s->reveal, 18, 0);
    lv_obj_set_style_bg_color(s->reveal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s->reveal, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s->reveal, LV_OBJ_FLAG_CLICKABLE);
    s->img = lv_image_create(s->reveal);
    lv_obj_set_style_radius(s->img, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(s->img, true, 0);
    lv_obj_center(s->img);
    lv_obj_add_flag(s->reveal, LV_OBJ_FLAG_HIDDEN);
    start_charge(s, true, GetHAL().millis());
}

void AppMengkeTransform::onRunning()
{
    auto ev = _keys->update();
    if (ev == input::KeyEvent::GoHome) { close(); return; }
    auto* s = _s.get();
    uint32_t now = GetHAL().millis();
    bool keyA = ev == input::KeyEvent::GoPrevious, keyB = ev == input::KeyEvent::GoNext;
    float motion = 0;
    if (now - s->imu_tick >= 40) {
        s->imu_tick = now;
        GetHAL().updateImuData();
        auto& d = GetHAL().getImuData();
        float mag = sqrtf(d.accelX * d.accelX + d.accelY * d.accelY + d.accelZ * d.accelZ);
        float jerk = fabsf(mag - 1.0f) + fabsf(mag - s->last_mag);
        float spin = sqrtf(d.gyroX * d.gyroX + d.gyroY * d.gyroY + d.gyroZ * d.gyroZ);  // dps
        s->last_mag = mag;
        if (jerk > 0.35f) motion += std::min(jerk, 2.0f) * 1.5f;   // shake/wave: ~1.5-3 s of vigorous shaking to fill
        if (spin > 150.0f) motion += std::min(spin, 600.0f) / 300.0f;  // spin / twist the wrist
    }
    LvglLockGuard lock;
    bool tap = s->tapped; s->tapped = false;
    switch (s->phase) {
    case State::Charge:
        if (now < s->lock_until) break;
        if (motion > 0) add_energy(s, _root, motion, now, "motion");
        if (keyA || keyB) add_energy(s, _root, 10, now, "button");
        if (tap) add_energy(s, _root, 6, now, "tap");
        if (s->energy >= 100) start_cast(s, _root, now);
        else if (now - s->t_act > 8000 && now - s->t_hint > 8000) {
            s->t_hint = now;
            if (GetHAL().isDisplayAsleep()) break;
            mk::play(mengke_sfx_t_hint, "transform hint");
            mk::bounce(s->core, 2);
        }
        break;
    case State::Cast:
        if (now - s->t_cast > 2600) do_reveal(s, _root, now);
        break;
    case State::Reveal:
        if (now < s->lock_until) break;
        if (keyA || keyB || tap || motion > 2.4f || now > s->until) start_charge(s, false, now);
        break;
    }
}

void AppMengkeTransform::onClose()
{
    mclog::tagInfo("Transform", "on close");
    _keys.reset();
    LvglLockGuard lock;
    lv_obj_delete(_root);
    _root = nullptr;
    _s.reset();
}

AppMengkeTransform::~AppMengkeTransform() = default;
