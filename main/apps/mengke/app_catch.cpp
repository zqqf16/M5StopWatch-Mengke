// 捕捉萌可: voice-led effort search for pre-readers (no directional arrow).
// Child shakes / spins / moves the watch (BMI270 accel+gyro magnitude) or mashes A to fill a progress ring.
// Cute searching animation (swirling stars + peeking silhouette). Rotating encouragement voices while active;
// silence for a few seconds -> gentle nudge. After ~3–6 s of sustained effort (randomized need), reveal:
//   Success (~70%): star burst, "抓到啦！是XX！" + intro, update catch collection (prefer new), celebration.
//   Miss (~30%): gentle fade / she slips away, soft "哎呀，她跑掉啦！再试一次吧！" — no punishment.
// B = collection gallery. Hold A+B = home. Speech interrupts; volume 0 mutes.
#include "apps_mengke.h"
#include "mengke_common.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <esp_random.h>
#include <cmath>
#include <algorithm>

using namespace mooncake;

static constexpr int kOrbit = 6;

struct AppMengkeCatch::State {
    enum Phase { Search, Reveal, Miss, Gallery } phase = Search;
    float progress = 0;     // 0..need
    float need = 100;       // randomized fill target (~3–6 s of activity)
    int quarter = 0, go_i = 0, result = 0;
    float last_mag = 1.0f;
    uint32_t imu_tick = 0, t_act = 0, t_hint = 0, t_spark = 0, t_anim = 0;
    uint32_t until = 0, say_more_at = 0, lock_until = 0;
    bool was_new = false;
    uint32_t mask = 0;
    lv_obj_t *search = nullptr, *ring = nullptr, *core = nullptr, *peek = nullptr;
    lv_obj_t *orbit[kOrbit] = {};
    lv_obj_t *reveal = nullptr, *img = nullptr, *badge = nullptr;
    lv_obj_t *miss = nullptr, *miss_star = nullptr;
    lv_obj_t *gallery = nullptr, *gimg = nullptr, *cells[MENGKE_COUNT] = {};
    lv_obj_t *count = nullptr;
    volatile int tapped = -1;  // -3 screen, -4 close gallery img, >=0 cell
};

static uint32_t _catch_color = 0xFF6FA8;

AppMengkeCatch::AppMengkeCatch()
{
    setAppInfo().name     = "捕捉萌可";
    setAppInfo().icon     = (void*)mk::decodedPinned(&mengke_icon_catch);
    setAppInfo().userData = &_catch_color;
}

void AppMengkeCatch::onCreate() { mclog::tagInfo("Catch", "on create"); }

static int popcount24(uint32_t m) { return __builtin_popcount(m & 0xFFFFFF); }

static void update_count(AppMengkeCatch::State* s) { lv_label_set_text_fmt(s->count, "%d/24", popcount24(s->mask)); }

static lv_obj_t* circle(lv_obj_t* p, int d, uint32_t col, lv_opa_t opa)
{
    lv_obj_t* o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void place_orbit(AppMengkeCatch::State* s, uint32_t now)
{
    int base = (int)((now / 18) % 360);
    float pct = s->need > 1 ? s->progress / s->need : 0;
    int rr = 110 + (int)(pct * 40);
    for (int i = 0; i < kOrbit; i++) {
        int a = base + i * (360 / kOrbit);
        int x = 233 + rr * lv_trigo_cos(a) / 32768;
        int y = 233 + rr * lv_trigo_sin(a) / 32768;
        lv_obj_set_pos(s->orbit[i], x - lv_obj_get_width(s->orbit[i]) / 2, y - lv_obj_get_height(s->orbit[i]) / 2);
        lv_obj_set_style_opa(s->orbit[i], (lv_opa_t)(120 + pct * 100), 0);
    }
    // peeking silhouette grows / peeks as search fills
    int pd = 48 + (int)(pct * 70);
    lv_obj_set_size(s->peek, pd, pd);
    lv_obj_set_style_opa(s->peek, (lv_opa_t)(70 + pct * 140), 0);
    lv_obj_set_pos(s->peek, 233 - pd / 2, 233 - pd / 2 + (int)((1.0f - pct) * 18));
}

static void start_search(AppMengkeCatch::State* s, bool intro, uint32_t now)
{
    // need ~70..130 units: with motion fill rate ~20–35/s of vigorous shake → ~3–6 s
    s->need = 95.0f + (float)(esp_random() % 56);  // ~3–6 s of vigorous effort
    s->progress = 0; s->quarter = 0; s->go_i = 0;
    s->t_act = s->t_hint = now; s->lock_until = now + 500;
    s->phase = AppMengkeCatch::State::Search;
    lv_obj_add_flag(s->reveal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s->miss, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s->gallery, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s->search, LV_OBJ_FLAG_HIDDEN);
    lv_arc_set_value(s->ring, 0);
    lv_obj_set_size(s->core, 100, 100);
    lv_obj_center(s->core);
    lv_obj_set_style_bg_opa(s->core, 50, 0);
    place_orbit(s, now);
    mclog::tagInfo("Catch", "search start need={}", (int)s->need);
    if (intro) mk::play(mengke_sfx_c_intro, "catch intro");
    else mk::playSeq({&mengke_sfx_sparkle, &mengke_sfx_c_intro}, "catch next");
}

static void open_gallery(AppMengkeCatch::State* s)
{
    s->phase = AppMengkeCatch::State::Gallery;
    lv_obj_add_flag(s->search, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s->reveal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s->miss, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s->gallery, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s->gimg, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < MENGKE_COUNT; i++) {
        bool got = s->mask & (1u << i);
        lv_obj_set_style_bg_color(s->cells[i], lv_color_hex(got ? mengke_list[i].color : 0x2C2850), 0);
        lv_obj_set_style_border_color(s->cells[i], lv_color_hex(got ? 0xFFFFFF : 0x4A4478), 0);
        lv_obj_t* star = lv_obj_get_child(s->cells[i], 0);
        if (got) lv_obj_remove_flag(star, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(star, LV_OBJ_FLAG_HIDDEN);
    }
    int n = popcount24(s->mask);
    if (n == 0) mk::play(mengke_sfx_c_none, "catch gallery empty");
    else {
        std::vector<const MengkeClip*> v{&mengke_sfx_c_gallery, &mengke_sfx_c_have};
        mk::sayNumber(n, v);
        v.push_back(&mengke_sfx_c_zhi);
        mk::playList(v, "catch gallery");
    }
    mclog::tagInfo("Catch", "gallery open, caught={}", n);
}

static void add_progress(AppMengkeCatch::State* s, lv_obj_t* root, float e, uint32_t now, const char* how)
{
    if (e <= 0 || s->phase != AppMengkeCatch::State::Search) return;
    s->progress = std::min(s->need, s->progress + e);
    s->t_act = now;
    int pct = (int)(s->progress * 100.0f / s->need);
    lv_arc_set_value(s->ring, pct);
    int size = 100 + pct;
    lv_obj_set_size(s->core, size, size);
    lv_obj_center(s->core);
    lv_obj_set_style_bg_opa(s->core, (lv_opa_t)(50 + pct * 1.8f), 0);
    if (now - s->t_spark > 280) {
        s->t_spark = now;
        int ang = pct * 36 / 10 - 90;
        mk::starBurst(root, 233 + 200 * lv_trigo_cos(ang) / 32768, 233 + 200 * lv_trigo_sin(ang) / 32768, 3, 36);
    }
    int q = std::min(4, pct / 25);
    if (q > s->quarter) {
        s->quarter = q;
        static const MengkeClip* go[4] = {&mengke_sfx_c_go1, &mengke_sfx_c_go2, &mengke_sfx_c_go3, &mengke_sfx_c_go4};
        static const MengkeClip* ch[4] = {&mengke_sfx_ch1, &mengke_sfx_ch2, &mengke_sfx_ch3, &mengke_sfx_ch4};
        const MengkeClip* line = go[s->go_i % 4];
        s->go_i++;
        if (q < 4) mk::playSeq({ch[q - 1], line}, "catch cheer");
        else mk::play(*ch[3], "catch cheer");
        GetHAL().vibrate(28);
        mclog::tagInfo("Catch", "progress {}% (q{}) by {}", pct, q, how);
    } else if (how[0] == 'm' && pct > 0 && pct % 10 < 3) {
        // occasional motion heartbeat for QEMU/tests (gyro/shake without new quarter)
        static uint32_t last_hb = 0;
        if (now - last_hb > 400) { last_hb = now; mclog::tagInfo("Catch", "progress {}% (q{}) by {}", pct, q, how); }
    }
}

static void do_catch(AppMengkeCatch::State* s, lv_obj_t* root, uint32_t now, const char* how)
{
    int r = esp_random() % MENGKE_COUNT;
    if (popcount24(s->mask) < MENGKE_COUNT && (esp_random() % 4) != 0)
        while (s->mask & (1u << r)) r = (r + 1) % MENGKE_COUNT;
    s->result = r;
    s->was_new = !(s->mask & (1u << r));
    s->mask |= (1u << r);
    mk::setCaughtMask(s->mask);
    int n = popcount24(s->mask);
    const Mengke& m = mengke_list[r];
    s->phase = AppMengkeCatch::State::Reveal;
    lv_obj_add_flag(s->search, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s->miss, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s->reveal, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(s->img, mk::decoded(&m.mid));
    lv_obj_set_style_border_color(s->reveal, lv_color_hex(m.color), 0);
    lv_obj_set_style_border_width(s->reveal, 14, 0);
    lv_obj_set_style_bg_color(s->reveal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s->reveal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(s->reveal, LV_OPA_80, 0);
    if (s->was_new) lv_obj_remove_flag(s->badge, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s->badge, LV_OBJ_FLAG_HIDDEN);
    update_count(s);
    mk::starBurst(root, 233, 220, s->was_new ? 22 : 12, 220);
    if (s->was_new) mk::starBurst(root, 233, 220, 14, 130);
    mk::bounce(s->img, s->was_new ? 3 : 2);
    GetHAL().vibrate(s->was_new ? 120 : 60);
    uint32_t smp = mengke_sfx_c_got.samples + m.name_clip.samples + m.intro_clip.samples;
    if (s->was_new) {
        mk::playSeq({&mengke_sfx_sparkle, &mengke_sfx_c_got, &m.name_clip, &mengke_sfx_c_new, &m.intro_clip}, "catch reveal new");
        smp += mengke_sfx_sparkle.samples + mengke_sfx_c_new.samples;
    } else {
        mk::playSeq({&mengke_sfx_c_got, &m.name_clip, &mengke_sfx_c_again, &m.intro_clip}, "catch reveal repeat");
        smp += mengke_sfx_c_again.samples;
    }
    s->say_more_at = now + smp * 1000 / MENGKE_CLIP_RATE + 600;
    s->until = s->say_more_at + 15000;
    mclog::tagInfo("Catch", "outcome catch; caught {} ({}) by {} new={} total={}", m.en, r, how, s->was_new ? "yes" : "no", n);
}

static void do_miss(AppMengkeCatch::State* s, lv_obj_t* root, uint32_t now)
{
    s->phase = AppMengkeCatch::State::Miss;
    lv_obj_add_flag(s->search, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s->reveal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s->miss, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s->miss_star, LV_OPA_COVER, 0);
    mk::shake(s->miss_star);
    // soft sparkle that fades (small burst, no scary flash)
    mk::starBurst(root, 233, 233, 8, 100);
    GetHAL().vibrate(35);
    mk::play(mengke_sfx_c_miss, "catch miss");
    s->lock_until = now + 1800;
    s->until = now + 20000;
    mclog::tagInfo("Catch", "outcome miss; she slipped away — try again");
}

static void finish_effort(AppMengkeCatch::State* s, lv_obj_t* root, uint32_t now, const char* how)
{
    // ~70% success / ~30% miss — never lose stars, never punish
    if ((esp_random() % 10) < 7) do_catch(s, root, now, how);
    else do_miss(s, root, now);
}

void AppMengkeCatch::onOpen()
{
    mclog::tagInfo("Catch", "on open");
    _keys = std::make_unique<input::KeyManager>();
    _s    = std::make_unique<State>();
    auto* s = _s.get();
    s->mask = mk::caughtMask();
    mclog::tagInfo("Catch", "collection loaded {}", popcount24(s->mask));
    LvglLockGuard lock;
    _root = mk::makeRoot(0x0B0A24);
    lv_obj_add_event_cb(_root, [](lv_event_t* e) { ((State*)lv_event_get_user_data(e))->tapped = -3; }, LV_EVENT_SHORT_CLICKED, s);

    // ---- search layer: progress ring + swirling stars + peeking silhouette ----
    s->search = lv_obj_create(_root);
    lv_obj_remove_style_all(s->search);
    lv_obj_set_size(s->search, 466, 466);
    lv_obj_remove_flag(s->search, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < 12; i++) {
        auto* st = mk::makeStar(s->search, 8 + (i * 5) % 10, 0x4A4478);
        int a = i * 360 / 12 + (i * 17) % 25, rr = 175 + (i * 29) % 50;
        lv_obj_set_pos(st, 233 + rr * lv_trigo_cos(a) / 32768 - 6, 233 + rr * lv_trigo_sin(a) / 32768 - 6);
    }
    s->ring = lv_arc_create(s->search);
    lv_obj_set_size(s->ring, 430, 430);
    lv_obj_center(s->ring);
    lv_arc_set_rotation(s->ring, 270);
    lv_arc_set_bg_angles(s->ring, 0, 360);
    lv_arc_set_range(s->ring, 0, 100);
    lv_arc_set_value(s->ring, 0);
    lv_obj_remove_style(s->ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s->ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s->ring, 28, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s->ring, 28, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s->ring, lv_color_hex(0x2C2456), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s->ring, lv_color_hex(0xFF6FA8), LV_PART_INDICATOR);
    s->core = circle(s->search, 100, 0xFFB8D8, 50);
    lv_obj_center(s->core);
    s->peek = mk::makeStar(s->search, 70, 0xC59BFF);  // shy peeking silhouette
    lv_obj_set_style_opa(s->peek, LV_OPA_50, 0);
    lv_obj_center(s->peek);
    static const uint32_t oc[] = {0xF5D46B, 0xFF6FA8, 0x7FB2FF, 0xFFFFFF, 0x5ED6C0, 0xFFB8D8};
    for (int i = 0; i < kOrbit; i++) {
        s->orbit[i] = mk::makeStar(s->search, 22 + (i % 3) * 6, oc[i]);
        lv_obj_set_style_opa(s->orbit[i], LV_OPA_70, 0);
    }

    // ---- reveal layer ----
    s->reveal = lv_obj_create(_root);
    lv_obj_remove_style_all(s->reveal);
    lv_obj_set_size(s->reveal, 466, 466);
    lv_obj_set_style_radius(s->reveal, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(s->reveal, LV_OBJ_FLAG_CLICKABLE);
    s->img = lv_image_create(s->reveal);
    lv_obj_align(s->img, LV_ALIGN_CENTER, 0, -10);
    s->badge = mk::makeStar(s->reveal, 80, 0xF5D46B);
    lv_obj_set_pos(s->badge, 320, 70);
    auto* la = mk::makeLabel(s->reveal, &mengke_font_26, 0xFFFFFF, "A");
    lv_label_set_text(la, "A");
    lv_obj_set_pos(la, 120, 395);
    auto* lb = mk::makeLabel(s->reveal, &mengke_font_26, 0xFFFFFF, "B");
    lv_obj_set_pos(lb, 330, 395);
    auto* sa = mk::makeStar(s->reveal, 34, 0x7FB2FF);  lv_obj_set_pos(sa, 150, 392);
    auto* sb = mk::makeStar(s->reveal, 34, 0xF5D46B);  lv_obj_set_pos(sb, 282, 392);
    lv_obj_add_flag(s->reveal, LV_OBJ_FLAG_HIDDEN);

    // ---- miss layer (gentle, never scary) ----
    s->miss = lv_obj_create(_root);
    lv_obj_remove_style_all(s->miss);
    lv_obj_set_size(s->miss, 466, 466);
    lv_obj_remove_flag(s->miss, LV_OBJ_FLAG_CLICKABLE);
    s->miss_star = mk::makeStar(s->miss, 120, 0x9A90C8);
    lv_obj_center(s->miss_star);
    auto* ma = mk::makeStar(s->miss, 40, 0x7FB2FF);
    lv_obj_set_pos(ma, 200, 380);  // hint: A / shake to retry
    lv_obj_add_flag(s->miss, LV_OBJ_FLAG_HIDDEN);

    // ---- gallery: 24 discs on a 6x4 grid ----
    s->gallery = lv_obj_create(_root);
    lv_obj_remove_style_all(s->gallery);
    lv_obj_set_size(s->gallery, 466, 466);
    lv_obj_remove_flag(s->gallery, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < MENGKE_COUNT; i++) {
        int row = i / 6, col = i % 6;
        auto* c = circle(s->gallery, 56, 0x2C2850, LV_OPA_COVER);
        lv_obj_set_style_border_width(c, 3, 0);
        lv_obj_set_pos(c, 233 - 3 * 64 + col * 64 + 4, 233 - 2 * 64 + row * 64 + 4);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        auto* st = mk::makeStar(c, 30, 0xFFFFFF);
        lv_obj_center(st);
        lv_obj_remove_flag(st, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, [](lv_event_t* e) {
            auto* st = (State*)lv_event_get_user_data(e);
            for (int k = 0; k < MENGKE_COUNT; k++) if (st->cells[k] == lv_event_get_target(e)) st->tapped = k;
        }, LV_EVENT_SHORT_CLICKED, s);
        s->cells[i] = c;
    }
    s->gimg = lv_image_create(s->gallery);
    lv_obj_set_style_bg_color(s->gimg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s->gimg, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s->gimg, 30, 0);
    lv_obj_set_style_radius(s->gimg, LV_RADIUS_CIRCLE, 0);
    lv_obj_center(s->gimg);
    lv_obj_add_flag(s->gimg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s->gimg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s->gimg, [](lv_event_t* e) { ((State*)lv_event_get_user_data(e))->tapped = -4; }, LV_EVENT_SHORT_CLICKED, s);
    lv_obj_add_flag(s->gallery, LV_OBJ_FLAG_HIDDEN);

    s->count = mk::makeLabel(_root, &mengke_font_26, 0xF5D46B, "0/24");
    lv_obj_align(s->count, LV_ALIGN_TOP_MID, 0, 26);
    update_count(s);
    start_search(s, true, GetHAL().millis());
}

void AppMengkeCatch::onRunning()
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
        float spin = sqrtf(d.gyroX * d.gyroX + d.gyroY * d.gyroY + d.gyroZ * d.gyroZ);
        s->last_mag = mag;
        if (jerk > 0.35f) motion += std::min(jerk, 2.0f) * 0.95f;
        if (spin > 150.0f) motion += std::min(spin, 600.0f) / 280.0f;
    }
    LvglLockGuard lock;
    int tap = s->tapped; s->tapped = -1;
    switch (s->phase) {
    case State::Search: {
        if (now - s->t_anim >= 40) { s->t_anim = now; place_orbit(s, now); }
        if (now < s->lock_until) break;
        // B early -> gallery (before much progress)
        if (keyB) { open_gallery(s); break; }  // B = collection anytime while searching
        if (motion > 0) add_progress(s, _root, motion, now, "motion");
        if (keyA) add_progress(s, _root, 14, now, "button");  // mash A fallback without IMU
        if (tap == -3) add_progress(s, _root, 8, now, "tap");
        if (s->progress >= s->need) finish_effort(s, _root, now, motion > 0 ? "motion" : "button");
        else if (now - s->t_act > 4500 && now - s->t_hint > 4500) {
            s->t_hint = now;
            mk::play(s->go_i % 2 ? mengke_sfx_c_hint1 : mengke_sfx_c_hint2, "catch hint");
            mk::bounce(s->peek, 2);
            mclog::tagInfo("Catch", "nudge after idle");
        }
        break;
    }
    case State::Reveal:
        if (s->say_more_at && now >= s->say_more_at) {
            s->say_more_at = 0;
            int n = popcount24(s->mask);
            if (n >= MENGKE_COUNT && s->was_new) { mk::play(mengke_sfx_c_all, "catch all"); mk::starBurst(_root, 233, 233, 24, 230); }
            else {
                std::vector<const MengkeClip*> v{&mengke_sfx_c_have};
                mk::sayNumber(n, v);
                v.push_back(&mengke_sfx_c_zhi);
                v.push_back(&mengke_sfx_c_more);
                mk::playList(v, "catch count");
            }
        }
        if (keyA || tap == -3 || now >= s->until) start_search(s, false, now);
        else if (keyB) open_gallery(s);
        break;
    case State::Miss: {
        // soft fade of the slipped-away star (ghost remains)
        if (now < s->lock_until) {
            int left = (int)(s->lock_until - now);
            lv_obj_set_style_opa(s->miss_star, (lv_opa_t)std::max(70, left / 7), 0);
        }
        // allow retry after ~500ms so a toddler mash isn't ignored during the fade
        bool miss_ready = now >= (s->lock_until > 1300 ? s->lock_until - 1300 : now);
        if (miss_ready && (keyA || keyB || tap == -3 || motion > 1.8f || now >= s->until))
            start_search(s, false, now);
        break;
    }
    case State::Gallery:
        if (tap >= 0) {
            bool got = s->mask & (1u << tap);
            if (got) {
                lv_image_set_src(s->gimg, mk::decoded(&mengke_list[tap].mid));
                lv_obj_remove_flag(s->gimg, LV_OBJ_FLAG_HIDDEN);
                mk::bounce(s->gimg, 1);
                mk::play(mengke_list[tap].intro_clip, "catch gallery item");
            } else { mk::shake(s->cells[tap]); mk::play(mengke_sfx_c_secret, "catch gallery secret"); }
        } else if (tap == -4) lv_obj_add_flag(s->gimg, LV_OBJ_FLAG_HIDDEN);
        else if (keyA || keyB) start_search(s, false, now);
        break;
    }
}

void AppMengkeCatch::onClose()
{
    mclog::tagInfo("Catch", "on close");
    _keys.reset();
    LvglLockGuard lock;
    lv_obj_delete(_root);
    _root = nullptr;
    _s.reset();
}

AppMengkeCatch::~AppMengkeCatch() = default;
