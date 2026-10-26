// 萌可躲猫猫: voice-led memory match. Face-down cards hide Mengke; tap to flip (she says her name).
// Match -> cheer + sparkles, cards stay open. Mismatch -> gentle "再找找看" and both flip back. No fail state.
// Win -> celebration + 1 star (shared with 萌可问答's star counter), level grows 2 -> 3 -> 4 pairs (NVS "mpairs").
// A/B = hear the hint (or next round after a win). Hold A+B = home.
#include "apps_mengke.h"
#include "mengke_common.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <esp_random.h>
#include <algorithm>
#include <string>

using namespace mooncake;

static constexpr int kMaxCards = 8;

struct AppMengkeMemory::State {
    enum Phase { Play, Wait, Win } phase = Play;
    int pairs = 2, n = 4, first = -1, second = -1, matched = 0, misses = 0, stars = 0;
    int who[kMaxCards] = {};
    bool open[kMaxCards] = {}, done[kMaxCards] = {};
    int cx[kMaxCards] = {}, cy[kMaxCards] = {}, size = 140;
    uint32_t until = 0, lock_until = 0;
    lv_obj_t* card[kMaxCards] = {};
    lv_obj_t* face[kMaxCards] = {};
    lv_obj_t* back[kMaxCards] = {};
    lv_obj_t* slot[5] = {};
    lv_obj_t* count = nullptr;
    volatile int tapped = -1;
};

static uint32_t _mem_color = 0xF6B8E0;

AppMengkeMemory::AppMengkeMemory()
{
    setAppInfo().name     = "萌可躲猫猫";
    setAppInfo().icon     = (void*)mk::decodedPinned(&mengke_icon_memory);
    setAppInfo().userData = &_mem_color;
}

void AppMengkeMemory::onCreate() { mclog::tagInfo("Memory", "on create"); }

static void paint_stars(AppMengkeMemory::State* s)
{
    int filled = (s->stars > 0 && s->stars % 5 == 0) ? 5 : s->stars % 5;
    for (int i = 0; i < 5; i++) lv_obj_set_style_bg_color(s->slot[i], lv_color_hex(i < filled ? 0xF5D46B : 0x3A3466), 0);
    lv_label_set_text_fmt(s->count, "%d", s->stars);
}

static void show_face(AppMengkeMemory::State* s, int i, bool up)
{
    if (up) {
        lv_obj_remove_flag(s->face[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s->back[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s->card[i], lv_color_hex(0x000000), 0);
        lv_obj_set_style_border_color(s->card[i], lv_color_hex(mengke_list[s->who[i]].color), 0);
    } else {
        lv_obj_add_flag(s->face[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s->back[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s->card[i], lv_color_hex(0x6A4FC8), 0);
        lv_obj_set_style_border_color(s->card[i], lv_color_hex(0xFFFFFF), 0);
    }
}

// flip: squash horizontally, swap side at the middle, unsquash
static void flip(AppMengkeMemory::State* s, int i, bool up)
{
    s->open[i] = up;
    lv_obj_t* c = s->card[i];
    lv_obj_set_style_transform_pivot_x(c, s->size / 2, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c);
    lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_style_transform_scale_x((lv_obj_t*)o, v, 0); });
    lv_anim_set_values(&a, 256, 10);
    lv_anim_set_duration(&a, 130);
    lv_anim_set_reverse_duration(&a, 130);
    lv_anim_start(&a);
    show_face(s, i, up);  // content swaps while squashed (visually ~instant)
}

static void deal(AppMengkeMemory::State* s, lv_obj_t* root)
{
    for (auto*& c : s->card) if (c) { lv_obj_delete(c); c = nullptr; }
    s->n = s->pairs * 2;
    s->first = s->second = -1; s->matched = 0; s->misses = 0;
    // pick distinct Mengke
    int pick[4]; 
    for (int k = 0; k < s->pairs; k++) {
        bool dup;
        do { pick[k] = esp_random() % MENGKE_COUNT; dup = false; for (int j = 0; j < k; j++) dup |= pick[j] == pick[k]; } while (dup);
    }
    for (int i = 0; i < s->n; i++) s->who[i] = pick[i / 2];
    for (int i = s->n - 1; i > 0; i--) std::swap(s->who[i], s->who[esp_random() % (i + 1)]);
    // layout for the round screen: 2x2 (140), 3x2 (120), 4x2 (92)
    int cols = s->pairs == 2 ? 2 : s->pairs == 3 ? 3 : 4;
    s->size = s->pairs == 2 ? 140 : s->pairs == 3 ? 120 : 92;
    int gap = s->pairs == 4 ? 8 : 12;
    std::string lay;
    for (int i = 0; i < s->n; i++) {
        int r = i / cols, c = i % cols;
        s->cx[i] = 233 + (int)((c - (cols - 1) / 2.0f) * (s->size + gap));
        s->cy[i] = 245 + (int)((r - 0.5f) * (s->size + gap));
        s->open[i] = s->done[i] = false;
        auto* o = lv_obj_create(root);
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, s->size, s->size);
        lv_obj_set_pos(o, s->cx[i] - s->size / 2, s->cy[i] - s->size / 2);
        lv_obj_set_style_radius(o, 24, 0);
        lv_obj_set_style_clip_corner(o, true, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(o, 5, 0);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(o, gap / 2);
        lv_obj_add_event_cb(o, [](lv_event_t* e) {
            auto* st = (AppMengkeMemory::State*)lv_event_get_user_data(e);
            for (int k = 0; k < kMaxCards; k++) if (st->card[k] == lv_event_get_target(e)) st->tapped = k;
        }, LV_EVENT_SHORT_CLICKED, s);
        s->card[i] = o;
        s->face[i] = lv_image_create(o);
        lv_image_set_src(s->face[i], mk::decoded(&mengke_list[s->who[i]].mid));
        lv_image_set_scale(s->face[i], 256 * (s->size + 16) / 220);
        lv_obj_center(s->face[i]);
        lv_obj_remove_flag(s->face[i], LV_OBJ_FLAG_CLICKABLE);
        s->back[i] = mk::makeStar(o, s->size * 6 / 10, 0xF5D46B);
        lv_obj_center(s->back[i]);
        show_face(s, i, false);
        lay += std::to_string(s->who[i]) + (i + 1 < s->n ? "," : "");
    }
    s->phase = AppMengkeMemory::State::Play;
    mclog::tagInfo("Memory", "deal pairs={} size={} who=[{}]", s->pairs, s->size, lay);
    for (int i = 0; i < s->n; i++) mclog::tagInfo("Memory", "card {} at {},{}", i, s->cx[i], s->cy[i]);
}

void AppMengkeMemory::onOpen()
{
    mclog::tagInfo("Memory", "on open");
    _keys = std::make_unique<input::KeyManager>();
    _s    = std::make_unique<State>();
    auto* s = _s.get();
    s->pairs = std::clamp(mk::getInt("mpairs", 2), 2, 4);
    s->stars = mk::quizStars();
    mclog::tagInfo("Memory", "level loaded pairs={} stars={}", s->pairs, s->stars);
    LvglLockGuard lock;
    _root = mk::makeRoot(0x1A1240);
    for (int i = 0; i < 5; i++) {
        s->slot[i] = mk::makeStar(_root, 34, 0x3A3466);
        lv_obj_set_pos(s->slot[i], 233 - 110 + i * 40, 30);
    }
    s->count = mk::makeLabel(_root, &mengke_font_26, 0xF5D46B, "0");
    lv_obj_set_pos(s->count, 233 + 96, 34);
    paint_stars(s);
    deal(s, _root);
    mk::play(mengke_sfx_m_intro, "memory intro");
}

void AppMengkeMemory::onRunning()
{
    auto ev = _keys->update();
    if (ev == input::KeyEvent::GoHome) { close(); return; }
    auto* s = _s.get();
    uint32_t now = GetHAL().millis();
    bool key = ev == input::KeyEvent::GoPrevious || ev == input::KeyEvent::GoNext;
    LvglLockGuard lock;
    int tap = s->tapped; s->tapped = -1;
    switch (s->phase) {
    case State::Play:
        if (key) mk::play(mengke_sfx_m_hint, "memory hint");
        if (tap < 0 || tap >= s->n || now < s->lock_until) break;
        if (s->open[tap]) { mk::play(mengke_list[s->who[tap]].name_clip, "memory name"); mk::bounce(s->card[tap], 1); break; }
        flip(s, tap, true);
        s->lock_until = now + 300;
        mclog::tagInfo("Memory", "flip card {} = {}", tap, mengke_list[s->who[tap]].en);
        if (s->first < 0) { s->first = tap; mk::play(mengke_list[s->who[tap]].name_clip, "memory name"); break; }
        s->second = tap;
        if (s->who[s->first] == s->who[tap]) {
            s->done[s->first] = s->done[tap] = true;
            s->matched++;
            for (int k : {s->first, tap}) { mk::starBurst(_root, s->cx[k], s->cy[k], 8, 90); mk::bounce(s->card[k], 2); }
            GetHAL().vibrate(60);
            mclog::tagInfo("Memory", "match {} ({}/{})", mengke_list[s->who[tap]].en, s->matched, s->pairs);
            s->first = s->second = -1;
            if (s->matched == s->pairs) {
                s->stars++;
                mk::setQuizStars(s->stars);
                paint_stars(s);
                bool grow = s->pairs < 4;
                if (grow) mk::setInt("mpairs", s->pairs + 1);
                mk::playSeq({&mengke_list[s->who[tap]].name_clip, &mengke_sfx_sparkle, &mengke_sfx_m_win, grow ? &mengke_sfx_m_more : nullptr}, "memory win");
                mk::starBurst(_root, 233, 233, 24, 230);
                mk::starBurst(_root, 233, 46, 10, 110);
                for (auto* st : s->slot) mk::bounce(st, 3);
                s->phase = State::Win;
                s->until = now + 9000;
                s->lock_until = now + 2500;
                mclog::tagInfo("Memory", "win pairs={} stars={} next_pairs={}", s->pairs, s->stars, grow ? s->pairs + 1 : s->pairs);
                if (grow) s->pairs++;
            } else {
                static const MengkeClip* ok[2] = {&mengke_sfx_m_match1, &mengke_sfx_m_match2};
                mk::playSeq({&mengke_list[s->who[tap]].name_clip, &mengke_sfx_sparkle, ok[esp_random() % 2]}, "memory match");
            }
        } else {
            s->misses++;
            mk::playSeq({&mengke_list[s->who[tap]].name_clip, s->misses % 3 == 0 ? &mengke_sfx_m_again2 : &mengke_sfx_m_again1}, "memory again");
            mk::shake(s->card[tap]);
            s->phase = State::Wait;
            s->until = now + 2400;
            mclog::tagInfo("Memory", "mismatch {} / {}", mengke_list[s->who[s->first]].en, mengke_list[s->who[tap]].en);
        }
        break;
    case State::Wait:
        if (now >= s->until) {
            flip(s, s->first, false);
            flip(s, s->second, false);
            s->first = s->second = -1;
            s->phase = State::Play;
            mclog::tagInfo("Memory", "flip back");
        }
        break;
    case State::Win:
        if (now >= s->until || (now >= s->lock_until && (key || tap >= 0))) {
            deal(s, _root);
            mk::play(mengke_sfx_m_next, "memory next");
        }
        break;
    }
}

void AppMengkeMemory::onClose()
{
    mclog::tagInfo("Memory", "on close");
    _keys.reset();
    LvglLockGuard lock;
    lv_obj_delete(_root);
    _root = nullptr;
    _s.reset();
}

AppMengkeMemory::~AppMengkeMemory() = default;
