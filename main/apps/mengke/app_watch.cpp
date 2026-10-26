// 萌可表盘: clock watch face with a Mengke companion. KEYA/KEYB (or swipe) switch companion, tap = say hello.
#include "apps_mengke.h"
#include "mengke_common.h"
#include <hal/hal.h>
#include <mooncake_log.h>

using namespace mooncake;

struct AppMengkeWatch::State {
    int idx = 0;
    lv_obj_t* time = nullptr;
    lv_obj_t* date = nullptr;
    lv_obj_t* img = nullptr;
    lv_obj_t* sec = nullptr;
    uint32_t tick = 0;
    int last_sec = -1;
    mk::Swipe swipe;
    volatile bool tapped = false;
};

static uint32_t _watch_color = 0x7FB2FF;

AppMengkeWatch::AppMengkeWatch()
{
    setAppInfo().name     = "萌可表盘";
    setAppInfo().icon     = (void*)mk::decodedPinned(&mengke_icon_watch);
    setAppInfo().userData = &_watch_color;
}

void AppMengkeWatch::onCreate() { mclog::tagInfo("Watch", "on create"); }

static const char* kWeek[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
static int weekday(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static void set_companion(AppMengkeWatch::State* s)
{
    const Mengke& m = mengke_list[s->idx];
    lv_image_set_src(s->img, mk::decoded(&m.mid));
    lv_obj_set_style_arc_color(s->sec, lv_color_hex(m.color), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(s->time, lv_color_hex(0xFFFFFF), 0);
    _watch_color = m.color;
}

void AppMengkeWatch::onOpen()
{
    mclog::tagInfo("Watch", "on open");
    mk::play(mengke_sfx_enter_watch, "enter watch");
    _keys = std::make_unique<input::KeyManager>();
    _s    = std::make_unique<State>();
    _s->idx = mk::watchIndex();
    LvglLockGuard lock;
    _root   = mk::makeRoot();
    auto* s = _s.get();

    s->sec = lv_arc_create(_root);
    lv_obj_set_size(s->sec, 460, 460);
    lv_obj_center(s->sec);
    lv_arc_set_rotation(s->sec, 270);
    lv_arc_set_bg_angles(s->sec, 0, 360);
    lv_arc_set_range(s->sec, 0, 60);
    lv_obj_remove_style(s->sec, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s->sec, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s->sec, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s->sec, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s->sec, lv_color_hex(0x1C1C2C), LV_PART_MAIN);

    s->time = mk::makeLabel(_root, &mengke_font_digits, 0xFFFFFF, "--:--");
    lv_obj_align(s->time, LV_ALIGN_TOP_MID, 0, 36);
    s->date = mk::makeLabel(_root, &mengke_font_26, 0xC8C8E0, "");
    lv_obj_align(s->date, LV_ALIGN_TOP_MID, 0, 140);

    s->img = lv_image_create(_root);
    lv_obj_align(s->img, LV_ALIGN_CENTER, 0, 84);
    lv_obj_add_flag(s->img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s->img, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(
        s->img,
        [](lv_event_t* e) {
            auto* st = (State*)lv_event_get_user_data(e);
            if (mk::isTap(&st->swipe)) st->tapped = true;
        },
        LV_EVENT_SHORT_CLICKED, s);
    mk::attachSwipe(_root, &s->swipe);
    mk::attachSwipe(s->img, &s->swipe);
    set_companion(s);
}

void AppMengkeWatch::onRunning()
{
    int step = 0;
    switch (_keys->update()) {
        case input::KeyEvent::GoHome: close(); return;
        case input::KeyEvent::GoPrevious: step = -1; break;
        case input::KeyEvent::GoNext: step = 1; break;
        default: break;
    }
    auto* s = _s.get();
    uint32_t now = GetHAL().millis();
    LvglLockGuard lock;
    if (s->swipe.step) {
        step          = s->swipe.step;
        s->swipe.step = 0;
    }
    if (step) {
        s->idx = mk::wrap(s->idx + step);
        set_companion(s);
        mk::setWatchIndex(s->idx);
        mk::play(mengke_list[s->idx].name_clip);
        mclog::tagInfo("Watch", "companion -> {}", mengke_list[s->idx].en);
    }
    if (s->tapped) {
        s->tapped = false;
        auto t    = GetHAL().getTimeHms();
        mk::sayTime(t.hour, t.minute);  // "现在是 X点Y分" for kids who can't read the clock yet
    }
    if (now - s->tick >= 200) {
        s->tick = now;
        auto t  = GetHAL().getTimeHms();
        if (t.second != s->last_sec) {
            s->last_sec = t.second;
            auto d      = GetHAL().getDateYmd();
            lv_label_set_text_fmt(s->time, "%02d:%02d", t.hour, t.minute);
            lv_label_set_text_fmt(s->date, "%d月%d日 %s  电量%d%%", d.month, d.day, kWeek[weekday(d.year, d.month, d.day)],
                                  GetHAL().getBatteryLevel());
            lv_arc_set_value(s->sec, t.second == 0 ? 60 : t.second);
        }
    }
}

void AppMengkeWatch::onClose()
{
    mclog::tagInfo("Watch", "on close");
    _keys.reset();
    LvglLockGuard lock;
    lv_obj_delete(_root);
    _root = nullptr;
    _s.reset();
}

AppMengkeWatch::~AppMengkeWatch() = default;
