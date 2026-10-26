// 你是哪只萌可？ Shake the watch (BMI270), press KEYB or tap to draw a random Mengke.
#include "apps_mengke.h"
#include "mengke_common.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <esp_random.h>
#include <cmath>

using namespace mooncake;

struct AppMengkeDraw::State {
    enum Phase { Idle, Rolling, Result } phase = Idle;
    lv_obj_t* title = nullptr;
    lv_obj_t* img = nullptr;
    lv_obj_t* text = nullptr;
    lv_obj_t* sub = nullptr;
    lv_obj_t* ring = nullptr;
    int cur = 0, result = 0, rolls = 0, rolls_total = 1;
    uint32_t next_tick = 0, imu_tick = 0;
    float last_mag = 1.0f;
    volatile bool tapped = false;
};

static uint32_t _draw_color = 0xF5D46B;

AppMengkeDraw::AppMengkeDraw()
{
    setAppInfo().name     = "你是哪只萌可";
    setAppInfo().icon     = (void*)mk::decodedPinned(&mengke_icon_draw);
    setAppInfo().userData = &_draw_color;
}

void AppMengkeDraw::onCreate() { mclog::tagInfo("Draw", "on create"); }

static void show_idle(AppMengkeDraw::State* s)
{
    s->phase = AppMengkeDraw::State::Idle;
    lv_label_set_text(s->title, "你是哪只萌可？");
    lv_image_set_src(s->img, mk::decoded(&mengke_icon_draw));
    lv_label_set_text(s->text, "摇一摇手表");
    lv_label_set_text(s->sub, "或按B键 / 点击屏幕抽取");
}

void AppMengkeDraw::onOpen()
{
    mclog::tagInfo("Draw", "on open");
    _keys = std::make_unique<input::KeyManager>();
    _s    = std::make_unique<State>();
    LvglLockGuard lock;
    _root  = mk::makeRoot(0x000000);
    auto* s = _s.get();

    s->ring = lv_arc_create(_root);
    lv_obj_set_size(s->ring, 456, 456);
    lv_obj_center(s->ring);
    lv_arc_set_bg_angles(s->ring, 0, 360);
    lv_arc_set_angles(s->ring, 0, 0);
    lv_obj_remove_style(s->ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s->ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s->ring, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s->ring, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s->ring, lv_color_hex(0x2A2350), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s->ring, lv_color_hex(0xF5D46B), LV_PART_INDICATOR);

    s->title = mk::makeLabel(_root, &mengke_font_26, 0xF5D46B, "");
    lv_obj_align(s->title, LV_ALIGN_TOP_MID, 0, 40);
    s->img = lv_image_create(_root);
    lv_obj_align(s->img, LV_ALIGN_CENTER, 0, -38);
    s->text = mk::makeLabel(_root, &mengke_font_40, 0xFFFFFF, "");
    lv_obj_align(s->text, LV_ALIGN_TOP_MID, 0, 308);
    s->sub = mk::makeLabel(_root, &mengke_font_26, 0xBFB8E8, "");
    lv_obj_align(s->sub, LV_ALIGN_TOP_MID, 0, 358);

    lv_obj_add_event_cb(
        _root, [](lv_event_t* e) { ((State*)lv_event_get_user_data(e))->tapped = true; }, LV_EVENT_SHORT_CLICKED, s);
    show_idle(s);
    mk::play(mengke_sfx_draw_hint);
}

static void start_roll(AppMengkeDraw::State* s)
{
    s->phase  = AppMengkeDraw::State::Rolling;
    s->result = esp_random() % MENGKE_COUNT;
    s->rolls  = 18 + esp_random() % 6;
    s->rolls_total = s->rolls;
    s->cur    = esp_random() % MENGKE_COUNT;
    s->next_tick = 0;
    lv_label_set_text(s->title, "星星魔法发动中…");
    lv_label_set_text(s->sub, "");
    mclog::tagInfo("Draw", "rolling, result will be #{} {}", s->result + 1, mengke_list[s->result].en);
    GetHAL().vibrate(80);
}

void AppMengkeDraw::onRunning()
{
    auto ev = _keys->update();
    if (ev == input::KeyEvent::GoHome) {
        close();
        return;
    }
    auto* s = _s.get();
    uint32_t now = GetHAL().millis();

    bool trigger = (ev == input::KeyEvent::GoNext || ev == input::KeyEvent::GoPrevious);
    // Shake detection: |a| deviates strongly from 1 g, or a large jump between samples
    if (now - s->imu_tick > 40) {
        s->imu_tick = now;
        GetHAL().updateImuData();
        auto& d   = GetHAL().getImuData();
        float mag = sqrtf(d.accelX * d.accelX + d.accelY * d.accelY + d.accelZ * d.accelZ);
        if (fabsf(mag - 1.0f) > 0.9f || fabsf(mag - s->last_mag) > 0.8f) {
            if (s->phase != State::Rolling) {
                mclog::tagInfo("Draw", "shake detected |a|={:.2f}g", mag);
                trigger = true;
            }
        }
        s->last_mag = mag;
    }

    LvglLockGuard lock;
    if (s->tapped) {
        s->tapped = false;
        trigger   = true;
    }
    if (trigger && s->phase != State::Rolling) start_roll(s);

    if (s->phase == State::Rolling && now >= s->next_tick) {
        s->rolls--;
        int total = s->rolls_total;
        int done  = total - s->rolls;
        lv_arc_set_angles(s->ring, 0, std::min(360, done * 360 / total));
        if (s->rolls <= 0) {
            s->phase = State::Result;
            s->cur   = s->result;
            const Mengke& m = mengke_list[s->result];
            lv_image_set_src(s->img, mk::decoded(&m.mid));
            lv_label_set_text(s->title, "你是");
            lv_label_set_text_fmt(s->text, "%s！", m.name);
            lv_obj_set_style_text_color(s->text, lv_color_hex(m.color), 0);
            lv_label_set_text_fmt(s->sub, "代表：%s\n%s", m.attr, m.desc);
            lv_arc_set_angles(s->ring, 0, 360);
            lv_obj_set_style_arc_color(s->ring, lv_color_hex(m.color), LV_PART_INDICATOR);
            _draw_color = m.color;
            GetHAL().vibrate(150);
            mk::playSeq({&m.you_clip, &m.intro_clip}, "draw reveal + intro");  // "你是X！" + one-line intro
            mk::starBurst(_root, 233, 195, 14, 210);
            mk::bounce(s->img, 2);
            mclog::tagInfo("Draw", "result: {} ({})", m.en, m.name);
        } else {
            s->cur = mk::wrap(s->cur + 1 + esp_random() % 5);
            lv_image_set_src(s->img, mk::decoded(&mengke_list[s->cur].mid));
            lv_label_set_text(s->text, mengke_list[s->cur].name);
            lv_obj_set_style_text_color(s->text, lv_color_hex(0xFFFFFF), 0);
            // slow down towards the end
            s->next_tick = now + 60 + (s->rolls < 8 ? (8 - s->rolls) * 45 : 0);
        }
    }
}

void AppMengkeDraw::onClose()
{
    mclog::tagInfo("Draw", "on close");
    _keys.reset();
    LvglLockGuard lock;
    lv_obj_delete(_root);
    _root = nullptr;
    _s.reset();
}

AppMengkeDraw::~AppMengkeDraw() = default;
