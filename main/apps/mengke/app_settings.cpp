// 设置: battery level / voltage / charging, volume (NVS), brightness (NVS).
// KEYA = -10, KEYB = +10 on the focused row; tap a row to focus it, or drag its slider.
#include "apps_mengke.h"
#include "mengke_common.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <apps/app_setup/workers/workers.h>  // official date/time workers (system app removed)

using namespace mooncake;

struct AppMengkeSettings::State {
    lv_obj_t* arc = nullptr;
    lv_obj_t* pct = nullptr;
    lv_obj_t* volt = nullptr;
    lv_obj_t* chg = nullptr;
    lv_obj_t* row[2] = {};
    lv_obj_t* row_lbl[2] = {};
    lv_obj_t* slider[2] = {};
    int focus = 0;
    int vol = 80, bri = 80;
    uint32_t tick = 0, preview_at = 0;
    int preview_which = 0;
    volatile int tap_row = -1;
    volatile int slider_changed = -1;
    volatile bool time_tapped = false;
    std::unique_ptr<setup_workers::WorkerBase> worker;
    int worker_stage = 0;  // 1 = time, 2 = date
};

static uint32_t _settings_color = 0xFFE27A;

AppMengkeSettings::AppMengkeSettings()
{
    setAppInfo().name     = "设置";
    setAppInfo().icon     = (void*)mk::decodedPinned(&mengke_icon_settings);
    setAppInfo().userData = &_settings_color;
}

void AppMengkeSettings::onCreate() { mclog::tagInfo("MkSettings", "on create"); }

static void paint(AppMengkeSettings::State* s)
{
    lv_label_set_text_fmt(s->row_lbl[0], "音量  %d", s->vol);
    lv_label_set_text_fmt(s->row_lbl[1], "亮度  %d", s->bri);
    lv_slider_set_value(s->slider[0], s->vol, LV_ANIM_OFF);
    lv_slider_set_value(s->slider[1], s->bri, LV_ANIM_OFF);
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_border_color(s->row[i], lv_color_hex(i == s->focus ? 0xF5D46B : 0x2E2A4A), 0);
        lv_obj_set_style_bg_color(s->row[i], lv_color_hex(i == s->focus ? 0x2A2350 : 0x16132A), 0);
    }
}

static void update_battery(AppMengkeSettings::State* s)
{
    int lvl      = GetHAL().getBatteryLevel();
    int mv       = GetHAL().getBatteryVoltage();
    bool vin     = GetHAL().isBatteryCharging(false);
    bool chg     = GetHAL().isBatteryCharging(true);
    lv_arc_set_value(s->arc, lvl);
    uint32_t col = lvl <= 20 ? 0xFF6F7F : (lvl <= 50 ? 0xFFD36B : 0x7CFFB0);
    lv_obj_set_style_arc_color(s->arc, lv_color_hex(col), LV_PART_INDICATOR);
    lv_label_set_text_fmt(s->pct, "%d%%", lvl);
    lv_label_set_text_fmt(s->volt, "%d.%02dV", mv / 1000, (mv % 1000) / 10);
    lv_label_set_text(s->chg, chg ? "充电中" : (vin ? "已接电源" : "电池供电"));
    static int last = -1, last_mv = -1;
    if (lvl != last || mv / 50 != last_mv) {
        mclog::tagInfo("MkSettings", "battery {}% {}mV vin={} charging={}", lvl, mv, vin, chg);
        last = lvl;
        last_mv = mv / 50;
    }
}

void AppMengkeSettings::onOpen()
{
    mclog::tagInfo("MkSettings", "on open");
    _keys = std::make_unique<input::KeyManager>();
    _s    = std::make_unique<State>();
    auto* s = _s.get();
    s->vol  = GetHAL().getSpeakerVolume();
    s->bri  = GetHAL().getBackLightBrightness();
    mk::play(mengke_sfx_enter_settings, "enter settings");

    LvglLockGuard lock;
    _root = mk::makeRoot(0x0B0820);

    lv_obj_t* title = mk::makeLabel(_root, &mengke_font_26, 0xF5D46B, "设置");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);

    s->arc = lv_arc_create(_root);
    lv_obj_set_size(s->arc, 170, 170);
    lv_obj_align(s->arc, LV_ALIGN_TOP_MID, 0, 58);
    lv_arc_set_rotation(s->arc, 135);
    lv_arc_set_bg_angles(s->arc, 0, 270);
    lv_arc_set_range(s->arc, 0, 100);
    lv_obj_remove_style(s->arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s->arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s->arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s->arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s->arc, lv_color_hex(0x2A2350), LV_PART_MAIN);
    s->pct = mk::makeLabel(s->arc, &mengke_font_40, 0xFFFFFF, "");
    lv_obj_align(s->pct, LV_ALIGN_CENTER, 0, -14);
    s->volt = mk::makeLabel(s->arc, &mengke_font_26, 0xBFB8E8, "");
    lv_obj_align(s->volt, LV_ALIGN_CENTER, 0, 24);
    s->chg = mk::makeLabel(_root, &mengke_font_26, 0x9FE8FF, "");
    lv_obj_align(s->chg, LV_ALIGN_TOP_MID, 0, 214);

    for (int i = 0; i < 2; i++) {
        s->row[i] = lv_obj_create(_root);
        lv_obj_remove_style_all(s->row[i]);
        lv_obj_set_size(s->row[i], i == 0 ? 360 : 300, 78);
        lv_obj_align(s->row[i], LV_ALIGN_TOP_MID, 0, 246 + i * 86);
        lv_obj_set_style_radius(s->row[i], 24, 0);
        lv_obj_set_style_bg_opa(s->row[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s->row[i], 3, 0);
        lv_obj_add_flag(s->row[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(s->row[i], LV_OBJ_FLAG_SCROLLABLE);
        s->row_lbl[i] = mk::makeLabel(s->row[i], &mengke_font_26, 0xFFFFFF, "");
        lv_obj_align(s->row_lbl[i], LV_ALIGN_TOP_MID, 0, 6);
        s->slider[i] = lv_slider_create(s->row[i]);
        lv_obj_set_size(s->slider[i], i == 0 ? 280 : 230, 12);
        lv_obj_align(s->slider[i], LV_ALIGN_BOTTOM_MID, 0, -16);
        lv_slider_set_range(s->slider[i], i == 0 ? 0 : 10, 100);
        lv_obj_set_style_bg_color(s->slider[i], lv_color_hex(0x3A3466), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s->slider[i], lv_color_hex(i == 0 ? 0xFF6FA8 : 0xFFE27A), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s->slider[i], lv_color_hex(0xFFFFFF), LV_PART_KNOB);
        lv_obj_add_event_cb(
            s->row[i],
            [](lv_event_t* e) {
                auto* st = (State*)lv_event_get_user_data(e);
                st->tap_row = (lv_event_get_target(e) == st->row[0]) ? 0 : 1;
            },
            LV_EVENT_CLICKED, s);
        lv_obj_add_event_cb(
            s->slider[i],
            [](lv_event_t* e) {
                auto* st = (State*)lv_event_get_user_data(e);
                st->slider_changed = (lv_event_get_target(e) == st->slider[0]) ? 0 : 1;
            },
            LV_EVENT_RELEASED, s);
    }
    // Date/time entry (replaces the official 系统 app's Set Time / Set Date)
    lv_obj_t* tbtn = lv_obj_create(_root);
    lv_obj_remove_style_all(tbtn);
    lv_obj_set_size(tbtn, 170, 40);
    lv_obj_align(tbtn, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_radius(tbtn, 20, 0);
    lv_obj_set_style_bg_color(tbtn, lv_color_hex(0x2A2350), 0);
    lv_obj_set_style_bg_opa(tbtn, LV_OPA_COVER, 0);
    lv_obj_add_flag(tbtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* tl = mk::makeLabel(tbtn, &mengke_font_26, 0x9FE8FF, "时间日期");
    lv_obj_center(tl);
    lv_obj_add_event_cb(
        tbtn, [](lv_event_t* e) { ((State*)lv_event_get_user_data(e))->time_tapped = true; }, LV_EVENT_CLICKED, s);

    paint(s);
    update_battery(s);
}

static void apply(AppMengkeSettings::State* s, int which, uint32_t now)
{
    if (which == 0) {
        GetHAL().setSpeakerVolume(s->vol, true);
    } else {
        GetHAL().setBackLightBrightness(s->bri, true);
    }
    s->preview_at    = now + 250;  // speak the new level once the user stops pressing
    s->preview_which = which;
    mclog::tagInfo("MkSettings", "volume={} brightness={} (saved to NVS)", s->vol, s->bri);
}

void AppMengkeSettings::onRunning()
{
    auto ev = _keys->update();
    if (ev == input::KeyEvent::GoHome) {
        close();
        return;
    }
    auto* s = _s.get();
    uint32_t now = GetHAL().millis();
    LvglLockGuard lock;
    if (s->time_tapped && !s->worker) {
        s->time_tapped  = false;
        s->worker_stage = 1;
        lv_obj_add_flag(_root, LV_OBJ_FLAG_HIDDEN);
        s->worker = std::make_unique<setup_workers::SetTimeWorker>();
        mclog::tagInfo("MkSettings", "open set-time worker");
    }
    if (s->worker) {
        s->worker->update();
        if (s->worker->isDone()) {
            s->worker.reset();
            if (s->worker_stage == 1) {
                s->worker_stage = 2;
                s->worker = std::make_unique<setup_workers::SetDateWorker>();
                mclog::tagInfo("MkSettings", "open set-date worker");
            } else {
                s->worker_stage = 0;
                lv_obj_remove_flag(_root, LV_OBJ_FLAG_HIDDEN);
                mclog::tagInfo("MkSettings", "date/time set");
            }
        }
        return;
    }
    int delta = ev == input::KeyEvent::GoPrevious ? -10 : (ev == input::KeyEvent::GoNext ? 10 : 0);
    if (delta) {
        if (s->focus == 0) s->vol = std::clamp(s->vol + delta, 0, 100);
        else s->bri = std::clamp(s->bri + delta, 10, 100);
        apply(s, s->focus, now);
        paint(s);
    }
    if (s->tap_row >= 0) {
        s->focus   = s->tap_row;
        s->tap_row = -1;
        paint(s);
    }
    if (s->slider_changed >= 0) {
        int i = s->slider_changed;
        s->slider_changed = -1;
        s->focus = i;
        if (i == 0) s->vol = lv_slider_get_value(s->slider[0]);
        else s->bri = lv_slider_get_value(s->slider[1]);
        apply(s, i, now);
        paint(s);
    }
    if (s->preview_at && now >= s->preview_at) {
        s->preview_at = 0;
        // "音量六十" / "亮度八十" (at volume 0 nothing plays: muted)
        mk::sayLevel(s->preview_which == 0, s->preview_which == 0 ? s->vol : s->bri);
    }
    if (now - s->tick > 1000) {
        s->tick = now;
        update_battery(s);
    }
}

void AppMengkeSettings::onClose()
{
    mclog::tagInfo("MkSettings", "on close");
    _keys.reset();
    LvglLockGuard lock;
    if (_s) _s->worker.reset();
    lv_obj_delete(_root);
    _root = nullptr;
    _s.reset();
}

AppMengkeSettings::~AppMengkeSettings() = default;
