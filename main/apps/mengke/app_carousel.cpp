// 萌可图鉴: browse the 24 Mengke. KEYA/KEYB or swipe = prev/next, tap = info card, long-press = favourite.
#include "apps_mengke.h"
#include "mengke_common.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <cstdio>
#include <esp_timer.h>

using namespace mooncake;

struct AppMengkeCarousel::State {
    int idx = 0;
    lv_obj_t* img = nullptr;
    lv_obj_t* name = nullptr;
    lv_obj_t* counter = nullptr;
    lv_obj_t* heart = nullptr;
    lv_obj_t* info = nullptr;
    lv_obj_t* info_text = nullptr;
    mk::Swipe swipe;
    volatile bool pending_info = false;
    volatile bool pending_fav = false;
    bool info_shown = false;
    uint32_t prefetch_at = 0;
};

static uint32_t _carousel_color = 0xFF6FA8;

AppMengkeCarousel::AppMengkeCarousel()
{
    setAppInfo().name     = "萌可图鉴";
    setAppInfo().icon     = (void*)mk::decodedPinned(&mengke_icon_carousel);
    setAppInfo().userData = &_carousel_color;
}

void AppMengkeCarousel::onCreate() { mclog::tagInfo("Carousel", "on create"); }


static void refresh(AppMengkeCarousel* app, AppMengkeCarousel::State* s, int dir);

void AppMengkeCarousel::onOpen()
{
    mclog::tagInfo("Carousel", "on open");
    _keys = std::make_unique<input::KeyManager>();
    _s    = std::make_unique<State>();
    _s->idx = mk::lastIndex();

    LvglLockGuard lock;
    _root = mk::makeRoot();
    auto* s = _s.get();

    s->img = lv_image_create(_root);
    lv_obj_align(s->img, LV_ALIGN_CENTER, 0, -26);
    lv_obj_add_flag(s->img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s->img, LV_OBJ_FLAG_GESTURE_BUBBLE);

    s->counter = mk::makeLabel(_root, &mengke_font_26, 0xBFC8FF, "");
    lv_obj_align(s->counter, LV_ALIGN_TOP_MID, 0, 12);

    s->name = mk::makeLabel(_root, &mengke_font_40, 0xFFFFFF, "");
    lv_obj_align(s->name, LV_ALIGN_BOTTOM_MID, 0, -22);

    s->heart = mk::makeLabel(_root, &mengke_font_40, 0xFF4F8B, "♥");
    lv_obj_align(s->heart, LV_ALIGN_CENTER, 128, -150);

    // Info overlay (tap the avatar)
    s->info = lv_obj_create(_root);
    lv_obj_remove_style_all(s->info);
    lv_obj_set_size(s->info, 340, 340);
    lv_obj_align(s->info, LV_ALIGN_CENTER, 0, -26);
    lv_obj_set_style_radius(s->info, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s->info, lv_color_hex(0x14102A), 0);
    lv_obj_set_style_bg_opa(s->info, 215, 0);
    lv_obj_add_flag(s->info, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s->info, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s->info, LV_OBJ_FLAG_GESTURE_BUBBLE);
    s->info_text = mk::makeLabel(s->info, &mengke_font_26, 0xFFFFFF, "");
    lv_obj_set_width(s->info_text, 280);
    lv_obj_set_style_text_align(s->info_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s->info_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(s->info_text, 6, 0);
    lv_obj_center(s->info_text);

    auto click_cb = [](lv_event_t* e) {
        auto* st = (State*)lv_event_get_user_data(e);
        if (mk::isTap(&st->swipe)) st->pending_info = true;
    };
    lv_obj_add_event_cb(s->img, click_cb, LV_EVENT_SHORT_CLICKED, s);
    lv_obj_add_event_cb(s->info, click_cb, LV_EVENT_SHORT_CLICKED, s);
    lv_obj_add_event_cb(
        s->img,
        [](lv_event_t* e) {
            auto* st = (State*)lv_event_get_user_data(e);
            if (mk::isTap(&st->swipe)) st->pending_fav = true;
        },
        LV_EVENT_LONG_PRESSED, s);
    mk::attachSwipe(_root, &s->swipe);
    mk::attachSwipe(s->img, &s->swipe);
    mk::attachSwipe(s->info, &s->swipe);

    refresh(this, s, 0);
    mk::play(mengke_list[s->idx].intro_clip, mengke_list[s->idx].en);
    s->prefetch_at = 1;
}

static void refresh(AppMengkeCarousel* app, AppMengkeCarousel::State* s, int dir)
{
    const Mengke& m = mengke_list[s->idx];
    lv_image_set_src(s->img, mk::decoded(&m.avatar));
    lv_label_set_text(s->name, m.name);
    lv_obj_set_style_text_color(s->name, lv_color_hex(m.color), 0);
    lv_label_set_text_fmt(s->counter, "%d / %d", s->idx + 1, MENGKE_COUNT);
    if (mk::isFav(s->idx)) lv_obj_remove_flag(s->heart, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s->heart, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(s->info_text, "%s\n%s\n代表：%s\n%s萌可 · %s登场\n\n%s", m.name, m.en, m.attr, m.group, m.ep,
                          m.intro);
    _carousel_color = m.color;
    mclog::tagInfo("Carousel", "show #{} {} ({})", s->idx + 1, m.en, m.name);
}

void AppMengkeCarousel::onRunning()
{
    int step = 0;
    switch (_keys->update()) {
        case input::KeyEvent::GoHome: close(); return;
        case input::KeyEvent::GoPrevious: step = -1; break;
        case input::KeyEvent::GoNext: step = 1; break;
        default: break;
    }
    LvglLockGuard lock;
    auto* s = _s.get();
    if (s->swipe.step) {
        step          = s->swipe.step;
        s->swipe.step = 0;
    }
    if (step) {
        s->idx = mk::wrap(s->idx + step);
        s->info_shown = false;
        lv_obj_add_flag(s->info, LV_OBJ_FLAG_HIDDEN);
        int64_t t0 = esp_timer_get_time();
        refresh(this, s, step);  // instant switch: avatar is a fully decoded RGB565 buffer
        lv_refr_now(NULL);  // render + flush the first frame synchronously (timing probe)
        int64_t t1 = esp_timer_get_time();
        lv_obj_invalidate(s->img);
        lv_refr_now(NULL);  // cost of one more full redraw of the avatar (= per animation frame)
        int64_t t2 = esp_timer_get_time();
        mclog::tagInfo("Carousel", "switch timing: first frame {} ms, avatar redraw {} ms", (int)((t1 - t0) / 1000),
                       (int)((t2 - t1) / 1000));
        mk::setLastIndex(s->idx);
        mk::play(mengke_list[s->idx].intro_clip, mengke_list[s->idx].en);
        s->prefetch_at = GetHAL().millis() + 150;
    }
    if (s->prefetch_at && GetHAL().millis() >= s->prefetch_at) {
        // Decode neighbours ahead of time so the next A/B press is a pure blit
        s->prefetch_at = 0;
        mk::prefetch(&mengke_list[mk::wrap(s->idx + 1)].avatar);
        mk::prefetch(&mengke_list[mk::wrap(s->idx - 1)].avatar);
    }
    if (s->pending_info) {
        s->pending_info = false;
        s->info_shown   = !s->info_shown;
        if (s->info_shown) lv_obj_remove_flag(s->info, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s->info, LV_OBJ_FLAG_HIDDEN);
        if (s->info_shown) mk::play(mengke_list[s->idx].intro_clip, mengke_list[s->idx].en);
    }
    if (s->pending_fav) {
        s->pending_fav = false;
        mk::toggleFav(s->idx);
        GetHAL().vibrate(60);
        refresh(this, s, 0);
    }
}

void AppMengkeCarousel::onClose()
{
    mclog::tagInfo("Carousel", "on close");
    _keys.reset();
    LvglLockGuard lock;
    lv_obj_delete(_root);
    _root = nullptr;
    _s.reset();
}

AppMengkeCarousel::~AppMengkeCarousel() = default;
