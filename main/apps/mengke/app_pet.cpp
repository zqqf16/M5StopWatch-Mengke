// 养萌可: a gentle virtual pet. Child picks a Mengke companion (A = next, B / tap = choose; long-press her to re-pick).
// Big icons: 喂一喂 (A), 摸摸 (tap/stroke her), 跳舞 (B, then shake). Happiness = 5 hearts, decays slowly over real time
// (RTC wall clock; floor 20 - she never gets sick, only says she misses you). Daily visit streak in NVS; milestones unlock
// accessories drawn over her picture: 2 days bow, 4 days crown, 7 days star wand. Hold A+B = home.
#include "apps_mengke.h"
#include "mengke_common.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <esp_random.h>
#include <ctime>
#include <cmath>
#include <algorithm>

using namespace mooncake;

static constexpr int kDecayPerHour = 3, kFloor = 20;

struct AppMengkePet::State {
    enum Phase { Select, Home, Dance } phase = Home;
    int pet = -1, pick = 0, hap = 60, streak = 0, acc = 0, shakes = 0;
    bool full_said = false;
    float last_mag = 1.0f;
    uint32_t imu_tick = 0, lock_until = 0, dance_until = 0, t_pet = 0, t_save = 0;
    lv_point_t press{0, 0};
    int stroke = 0;
    bool long_fired = false;
    lv_obj_t *img = nullptr, *acc_layer = nullptr, *hearts[5] = {}, *btn[3] = {}, *hint_a = nullptr, *hint_b = nullptr;
    lv_obj_t *bow = nullptr, *crown = nullptr, *wand = nullptr, *food = nullptr;
    volatile int tapped = -1;  // 0 feed 1 pet 2 dance 3 image 4 long-press
};

static uint32_t _pet_color = 0xFFB0C8;

AppMengkePet::AppMengkePet()
{
    setAppInfo().name     = "养萌可";
    setAppInfo().icon     = (void*)mk::decodedPinned(&mengke_icon_pet);
    setAppInfo().userData = &_pet_color;
}

void AppMengkePet::onCreate() { mclog::tagInfo("Pet", "on create"); }

static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static bool clock_ok(time_t t) { return t > 1704067200; }  // after 2024-01-01: RTC has a real time

static int today()
{
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    return (int)days_from_civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

static void paint_hearts(AppMengkePet::State* s)
{
    int n = std::clamp((s->hap + 19) / 20, 1, 5);
    for (int i = 0; i < 5; i++) lv_obj_set_style_text_color(s->hearts[i], lv_color_hex(i < n ? 0xFF4F8B : 0x3A3466), 0);
}

static void save_hap(AppMengkePet::State* s)
{
    mk::setInt("phap", s->hap);
    time_t t = time(nullptr);
    if (clock_ok(t)) mk::setInt("ptime", (int32_t)t);
}

static void paint_acc(AppMengkePet::State* s)
{
    auto vis = [](lv_obj_t* o, bool v) { if (v) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); };
    vis(s->bow, s->acc & 1);
    vis(s->crown, s->acc & 2);
    vis(s->wand, s->acc & 4);
}

static void set_mode(AppMengkePet::State* s, bool select)
{
    for (auto* b : s->btn) { if (select) lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(b, LV_OBJ_FLAG_HIDDEN); }
    for (auto* h : s->hearts) { if (select) lv_obj_add_flag(h, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(h, LV_OBJ_FLAG_HIDDEN); }
    for (auto* h : {s->hint_a, s->hint_b}) { if (select) lv_obj_remove_flag(h, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(h, LV_OBJ_FLAG_HIDDEN); }
    if (select) lv_obj_add_flag(s->acc_layer, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(s->acc_layer, LV_OBJ_FLAG_HIDDEN);
}

static void show_pick(AppMengkePet::State* s, bool first)
{
    const Mengke& m = mengke_list[s->pick];
    lv_image_set_src(s->img, mk::decoded(&m.mid));
    mk::bounce(s->img, 1);
    if (first) mk::playSeq({&mengke_sfx_p_choose, &m.name_clip}, "pet choose");
    else mk::play(m.name_clip, "pet pick name");
    mclog::tagInfo("Pet", "select showing {} ({})", m.en, s->pick);
}

static void enter_select(AppMengkePet::State* s, uint32_t now)
{
    s->phase = AppMengkePet::State::Select;
    s->pick = s->pet >= 0 ? s->pet : esp_random() % MENGKE_COUNT;
    set_mode(s, true);
    show_pick(s, true);
    s->lock_until = now + 500;
}

static void enter_home(AppMengkePet::State* s, uint32_t now)
{
    s->phase = AppMengkePet::State::Home;
    set_mode(s, false);
    lv_image_set_src(s->img, mk::decoded(&mengke_list[s->pet].mid));
    paint_hearts(s);
    paint_acc(s);
    s->lock_until = now + 400;
}

static lv_obj_t* disc(lv_obj_t* p, int d, uint32_t col)
{
    lv_obj_t* o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t* polyline(lv_obj_t* p, const lv_point_precise_t* pts, int n, int w, uint32_t col)
{
    lv_obj_t* l = lv_line_create(p);
    lv_line_set_points(l, pts, n);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(col), 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static void build_accessories(AppMengkePet::State* s)
{
    // accessory layer: 220x220 over the picture (top-left = picture top-left)
    // crown (gold zigzag + band) on top of her head
    s->crown = lv_obj_create(s->acc_layer);
    lv_obj_remove_style_all(s->crown);
    lv_obj_set_size(s->crown, 120, 80);
    lv_obj_set_pos(s->crown, 50, -30);
    static const lv_point_precise_t cz[] = {{10, 60}, {14, 14}, {37, 42}, {60, 6}, {83, 42}, {106, 14}, {110, 60}, {10, 60}};
    polyline(s->crown, cz, 8, 10, 0xF5C542);
    for (int i = 0; i < 3; i++) { auto* g = disc(s->crown, 14, i == 1 ? 0xFF4F8B : 0x7FB2FF); lv_obj_set_pos(g, 26 + i * 27, 44); }
    // bow (two pink loops + knot) at the upper right
    s->bow = lv_obj_create(s->acc_layer);
    lv_obj_remove_style_all(s->bow);
    lv_obj_set_size(s->bow, 110, 70);
    lv_obj_set_pos(s->bow, 108, -4);
    auto* l1 = disc(s->bow, 50, 0xFF3D8B); lv_obj_set_pos(l1, 4, 10);
    auto* l2 = disc(s->bow, 50, 0xFF3D8B); lv_obj_set_pos(l2, 56, 10);
    for (auto* l : {l1, l2}) { lv_obj_set_style_border_width(l, 4, 0); lv_obj_set_style_border_color(l, lv_color_hex(0xFFFFFF), 0); }
    auto* k = disc(s->bow, 26, 0xFFE27A); lv_obj_set_pos(k, 42, 22);
    lv_obj_set_style_border_width(k, 4, 0);
    lv_obj_set_style_border_color(k, lv_color_hex(0xFF4F8B), 0);
    // star wand at the lower left
    s->wand = lv_obj_create(s->acc_layer);
    lv_obj_remove_style_all(s->wand);
    lv_obj_set_size(s->wand, 100, 120);
    lv_obj_set_pos(s->wand, -40, 110);
    static const lv_point_precise_t st[] = {{30, 110}, {62, 40}};
    polyline(s->wand, st, 2, 9, 0xFFFFFF);
    auto* star = mk::makeStar(s->wand, 56, 0xF5D46B); lv_obj_set_pos(star, 38, 4);
}

void AppMengkePet::onOpen()
{
    mclog::tagInfo("Pet", "on open");
    _keys = std::make_unique<input::KeyManager>();
    _s    = std::make_unique<State>();
    auto* s = _s.get();
    uint32_t now = GetHAL().millis();
    s->pet = mk::getInt("pet", -1);
    if (s->pet >= MENGKE_COUNT) s->pet = -1;
    s->hap = std::clamp(mk::getInt("phap", 60), kFloor, 100);
    s->streak = mk::getInt("pstreak", 0);
    s->acc = mk::getInt("pacc", 0);
    // gentle decay over real time (RTC), never below kFloor
    time_t t = time(nullptr);
    int last_t = mk::getInt("ptime", 0);
    int away_h = (clock_ok(t) && last_t > 0 && t > last_t) ? (int)((t - last_t) / 3600) : 0;
    if (away_h > 0) s->hap = std::max(kFloor, s->hap - away_h * kDecayPerHour);
    // daily visit streak
    int day = today(), last_day = mk::getInt("pday", 0), new_acc = 0;
    if (clock_ok(t) && day != last_day) {
        s->streak = (last_day > 0 && day == last_day + 1) ? s->streak + 1 : 1;
        mk::setInt("pday", day);
        mk::setInt("pstreak", s->streak);
        int want = (s->streak >= 2 ? 1 : 0) | (s->streak >= 4 ? 2 : 0) | (s->streak >= 7 ? 4 : 0);
        new_acc = want & ~s->acc;
        if (new_acc) { s->acc |= new_acc; mk::setInt("pacc", s->acc); }
    }
    save_hap(s);
    mclog::tagInfo("Pet", "loaded pet={} hap={} away_h={} day={} last_day={} streak={} acc={} new_acc={}", s->pet, s->hap, away_h,
                   day, last_day, s->streak, s->acc, new_acc);

    LvglLockGuard lock;
    _root = mk::makeRoot(0x2A1238);
    s->img = lv_image_create(_root);
    lv_obj_set_pos(s->img, 123, 62);
    lv_obj_set_style_radius(s->img, LV_RADIUS_CIRCLE, 0);  // hide the picture's black corners
    lv_obj_set_style_clip_corner(s->img, true, 0);
    lv_obj_add_flag(s->img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s->img, [](lv_event_t* e) {
        auto* st = (State*)lv_event_get_user_data(e);
        lv_point_t p; lv_indev_get_point(lv_indev_active(), &p);
        auto code = lv_event_get_code(e);
        if (code == LV_EVENT_PRESSED) { st->press = p; st->stroke = 0; st->long_fired = false; }
        else if (code == LV_EVENT_PRESSING) st->stroke = std::max<int>(st->stroke, abs(p.x - st->press.x) + abs(p.y - st->press.y));
        else if (code == LV_EVENT_LONG_PRESSED && st->stroke < 30) { st->tapped = 4; st->long_fired = true; }
        else if (code == LV_EVENT_RELEASED && !st->long_fired) st->tapped = 3;
    }, LV_EVENT_ALL, s);
    s->acc_layer = lv_obj_create(_root);
    lv_obj_remove_style_all(s->acc_layer);
    lv_obj_set_size(s->acc_layer, 220, 220);
    lv_obj_set_pos(s->acc_layer, 123, 62);
    lv_obj_remove_flag(s->acc_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s->acc_layer, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    build_accessories(s);
    for (int i = 0; i < 5; i++) {
        s->hearts[i] = mk::makeLabel(_root, &mengke_font_40, 0x3A3466, "♥");
        lv_obj_set_pos(s->hearts[i], 233 - 105 + i * 42, 14);
    }
    // three big action buttons
    static const uint32_t bc[3] = {0xFFE3A0, 0xFFD0E2, 0xC8D8FF};
    for (int i = 0; i < 3; i++) {
        auto* b = disc(_root, 104, bc[i]);
        lv_obj_set_style_border_width(b, 5, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_pos(b, 233 - 52 + (i - 1) * 122, 296 - (i == 1 ? 0 : 14));
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(b, 10);
        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            auto* st = (State*)lv_event_get_user_data(e);
            for (int k = 0; k < 3; k++) if (st->btn[k] == lv_event_get_target(e)) st->tapped = k;
        }, LV_EVENT_SHORT_CLICKED, s);
        s->btn[i] = b;
    }
    {   // feed: apple
        auto* ap = disc(s->btn[0], 58, 0xFF4B4B); lv_obj_align(ap, LV_ALIGN_CENTER, 0, 6);
        auto* lf = disc(s->btn[0], 22, 0x3FBF5A); lv_obj_set_size(lf, 26, 14); lv_obj_align(lf, LV_ALIGN_CENTER, 12, -26);
        auto* hl = disc(ap, 14, 0xFFB0B0); lv_obj_set_pos(hl, 12, 10);
        // pet: heart
        auto* h = mk::makeLabel(s->btn[1], &mengke_font_40, 0xFF4F8B, "♥");
        lv_obj_set_style_transform_scale(h, 420, 0);
        lv_obj_center(h);
        lv_obj_set_style_transform_pivot_x(h, LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(h, LV_PCT(50), 0);
        // dance: star
        auto* sd = mk::makeStar(s->btn[2], 70, 0x5B7CFF); lv_obj_center(sd);
    }
    s->food = disc(_root, 50, 0xFF4B4B);
    lv_obj_add_flag(s->food, LV_OBJ_FLAG_HIDDEN);
    // selection hints: A (blue star, next) / B (gold star, choose)
    s->hint_a = mk::makeLabel(_root, &mengke_font_40, 0x7FB2FF, "A ★");
    lv_obj_set_pos(s->hint_a, 90, 330);
    s->hint_b = mk::makeLabel(_root, &mengke_font_40, 0xF5D46B, "★ B");
    lv_obj_set_pos(s->hint_b, 290, 330);

    if (s->pet < 0) { enter_select(s, now); return; }
    enter_home(s, now);
    const Mengke& m = mengke_list[s->pet];
    if (new_acc) {
        std::vector<const MengkeClip*> v{&m.name_clip, &mengke_sfx_p_streak_a};
        mk::sayNumber(s->streak, v);
        v.push_back(&mengke_sfx_p_streak_b);
        int top = new_acc & 4 ? 4 : new_acc & 2 ? 2 : 1;
        v.push_back(top == 4 ? &mengke_sfx_p_gift_wand : top == 2 ? &mengke_sfx_p_gift_crown : &mengke_sfx_p_gift_bow);
        mk::playList(v, "pet streak gift");
        mk::starBurst(_root, 233, 160, 20, 200);
        mk::bounce(s->acc_layer, 3);
        mclog::tagInfo("Pet", "accessory unlocked mask={} streak={}", new_acc, s->streak);
    } else if (s->hap < 40) mk::playSeq({&m.name_clip, &mengke_sfx_p_miss, &mengke_sfx_p_menu}, "pet miss");
    else mk::playSeq({&m.name_clip, &mengke_sfx_p_hello, &mengke_sfx_p_menu}, "pet hello");
    mk::bounce(s->img, 2);
}

static void make_happy(AppMengkePet::State* s, lv_obj_t* root, int add, const char* what)
{
    int before = s->hap;
    s->hap = std::min(100, s->hap + add);
    paint_hearts(s);
    int n = std::clamp((s->hap + 19) / 20, 1, 5);
    if (n > std::clamp((before + 19) / 20, 1, 5)) mk::bounce(s->hearts[n - 1], 2);
    save_hap(s);
    mclog::tagInfo("Pet", "{} -> hap {}", what, s->hap);
}

void AppMengkePet::onRunning()
{
    auto ev = _keys->update();
    if (ev == input::KeyEvent::GoHome) { close(); return; }
    auto* s = _s.get();
    uint32_t now = GetHAL().millis();
    bool keyA = ev == input::KeyEvent::GoPrevious, keyB = ev == input::KeyEvent::GoNext;
    bool shake = false;
    if (now - s->imu_tick >= 40) {
        s->imu_tick = now;
        GetHAL().updateImuData();
        auto& d = GetHAL().getImuData();
        float mag = sqrtf(d.accelX * d.accelX + d.accelY * d.accelY + d.accelZ * d.accelZ);
        shake = fabsf(mag - 1.0f) > 0.6f || fabsf(mag - s->last_mag) > 0.6f;
        s->last_mag = mag;
    }
    LvglLockGuard lock;
    int tap = s->tapped; s->tapped = -1;
    if (now < s->lock_until) return;
    const Mengke* m = s->pet >= 0 ? &mengke_list[s->pet] : nullptr;
    switch (s->phase) {
    case State::Select:
        if (keyA) { s->pick = mk::wrap(s->pick + 1); show_pick(s, false); s->lock_until = now + 300; }
        else if (keyB || tap == 3) {
            s->pet = s->pick;
            mk::setInt("pet", s->pet);
            enter_home(s, now);
            mk::playSeq({&mengke_list[s->pet].name_clip, &mengke_sfx_p_chosen, &mengke_sfx_p_menu}, "pet chosen");
            mk::starBurst(_root, 233, 170, 18, 200);
            mk::bounce(s->img, 3);
            mclog::tagInfo("Pet", "chosen {} ({})", mengke_list[s->pet].en, s->pet);
            s->lock_until = now + 1200;
        }
        break;
    case State::Home:
        if (tap == 4) { enter_select(s, now); break; }
        if (keyA || tap == 0) {  // feed: apple flies up to her
            lv_obj_remove_flag(s->food, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s->food, 233 - 122 - 25, 330);
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, s->food);
            lv_anim_set_duration(&a, 600);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
            lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_pos((lv_obj_t*)o, 86 + (208 - 86) * v / 100, 330 - (330 - 210) * v / 100); });
            lv_anim_set_values(&a, 0, 100);
            lv_anim_set_completed_cb(&a, [](lv_anim_t* an) { lv_obj_add_flag((lv_obj_t*)an->var, LV_OBJ_FLAG_HIDDEN); });
            lv_anim_start(&a);
            mk::bounce(s->btn[0], 1);
            mk::bounce(s->img, 2);
            mk::play((esp_random() & 1) ? mengke_sfx_p_eat1 : mengke_sfx_p_eat2, "pet eat");
            make_happy(s, _root, 15, "feed");
            s->lock_until = now + 1200;
        } else if (tap == 1 || tap == 3) {  // 摸摸 (tap heart, tap or stroke her)
            if (now - s->t_pet > 1200) {
                s->t_pet = now;
                mk::starBurst(_root, 233, 150, 8, 120);
                mk::shake(s->img);
                mk::play((esp_random() & 1) ? mengke_sfx_p_pet1 : mengke_sfx_p_pet2, s->stroke >= 30 ? "pet stroke" : "pet pat");
                make_happy(s, _root, 10, s->stroke >= 30 ? "stroke" : "pat");
            }
        } else if (keyB || tap == 2) {
            s->phase = State::Dance;
            s->shakes = 0;
            s->dance_until = now + 9000;
            mk::bounce(s->btn[2], 1);
            mk::play(mengke_sfx_p_dance, "pet dance prompt");
            s->lock_until = now + 600;
            mclog::tagInfo("Pet", "dance start");
        }
        if (s->hap >= 100 && !s->full_said && s->phase == State::Home && now >= s->lock_until) {
            s->full_said = true;
            mk::play(mengke_sfx_p_full, "pet full");
            mk::starBurst(_root, 233, 170, 24, 220);
        }
        break;
    case State::Dance:
        if (shake || keyB || tap >= 0) {
            s->shakes++;
            mk::bounce(s->img, 1);
            mk::starBurst(_root, 233, 170, 6, 150);
            static const MengkeClip* ch[4] = {&mengke_sfx_ch1, &mengke_sfx_ch2, &mengke_sfx_ch3, &mengke_sfx_ch4};
            mk::play(*ch[std::min(s->shakes - 1, 3)], "pet dance step");
            s->lock_until = now + 350;
            mclog::tagInfo("Pet", "dance step {}", s->shakes);
        }
        if (s->shakes >= 4 || now > s->dance_until) {
            mk::playSeq({&mengke_sfx_sparkle, &mengke_sfx_p_dance_ok}, "pet dance done");
            mk::starBurst(_root, 233, 170, 18, 210);
            mk::bounce(s->img, 3);
            make_happy(s, _root, s->shakes >= 4 ? 15 : 8, "dance");
            s->phase = State::Home;
            s->lock_until = now + 1500;
        }
        break;
    }
    (void)m;
}

void AppMengkePet::onClose()
{
    mclog::tagInfo("Pet", "on close");
    _keys.reset();
    if (_s) save_hap(_s.get());
    LvglLockGuard lock;
    lv_obj_delete(_root);
    _root = nullptr;
    _s.reset();
}

AppMengkePet::~AppMengkePet() = default;
