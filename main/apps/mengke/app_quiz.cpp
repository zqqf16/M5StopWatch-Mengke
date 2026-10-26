// 萌可问答 v3: voice-led yes/no quiz for pre-readers.
// A = 是 (✓), B = 否 (✗), tap ✓/✗ also answers, tap the picture to hear the question again. Hold A+B = home.
// Correct -> cheer + star burst + bounce + 1 star (NVS); every 5 stars a special celebration.
// Wrong -> gentle "再想想" and the question is asked again; second miss -> "没关系，正确答案是…" then next question.
#include "apps_mengke.h"
#include "mengke_common.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <esp_random.h>
#include <cstring>

using namespace mooncake;

struct AppMengkeQuiz::State {
    enum Phase { Ask, Feedback } phase = Ask;
    enum Type { Name, Royal } type = Name;
    int shown = 0, asked = 0, qno = 0, tries = 0, stars = 0;
    bool expect_yes = true;
    uint32_t until = 0, lock_until = 0;
    lv_obj_t* img = nullptr;
    lv_obj_t* yes = nullptr;
    lv_obj_t* no = nullptr;
    lv_obj_t* slot[5] = {};
    lv_obj_t* count = nullptr;
    volatile int tapped = -1;  // 1 = yes, 0 = no, 2 = replay
};

static uint32_t _quiz_color = 0x5ED6C0;

AppMengkeQuiz::AppMengkeQuiz()
{
    setAppInfo().name     = "萌可问答";
    setAppInfo().icon     = (void*)mk::decodedPinned(&mengke_icon_quiz);
    setAppInfo().userData = &_quiz_color;
}

void AppMengkeQuiz::onCreate() { mclog::tagInfo("Quiz", "on create"); }

static int idx_of(const char* en)
{
    for (int i = 0; i < MENGKE_COUNT; i++)
        if (!strcmp(mengke_list[i].en, en)) return i;
    return -1;
}
// Only facts we are sure about: the 4 royal Mengke from ep.1 (yes) and the collected 星星 Mengke (no).
// 流浪/王子 (same person) and 极光 are excluded from the royal question and never confused with each other.
static bool royal_ok(int i, bool& yes)
{
    const Mengke& m = mengke_list[i];
    if (!strcmp(m.en, "Princeping") || !strcmp(m.en, "Wanderping") || !strcmp(m.group, "传奇")) return false;
    if (!strcmp(m.group, "皇室")) { yes = true; return true; }
    if (!strcmp(m.group, "星星")) { yes = false; return true; }
    return false;
}
static bool confusable(int a, int b)
{
    int l = idx_of("Wanderping"), w = idx_of("Princeping");
    return (a == l && b == w) || (a == w && b == l);
}

static const MengkeClip* question_clip(AppMengkeQuiz::State* s)
{
    return s->type == AppMengkeQuiz::State::Royal ? &mengke_sfx_q_royal : &mengke_ask[s->asked];
}

static void paint_stars(AppMengkeQuiz::State* s)
{
    int filled = (s->stars > 0 && s->stars % 5 == 0) ? 5 : s->stars % 5;  // full row while celebrating
    for (int i = 0; i < 5; i++) {
        lv_obj_set_style_bg_color(s->slot[i], lv_color_hex(i < filled ? 0xF5D46B : 0x3A3466), 0);
        lv_obj_invalidate(s->slot[i]);
    }
    lv_label_set_text_fmt(s->count, "%d", s->stars);
}

static void new_question(AppMengkeQuiz::State* s, bool with_intro)
{
    s->phase = AppMengkeQuiz::State::Ask;
    s->tries = 0;
    s->qno++;
    int prev = s->shown;
    do s->shown = esp_random() % MENGKE_COUNT; while (s->shown == prev && s->qno > 1);
    bool yes = false;
    if ((esp_random() % 4) == 0 && royal_ok(s->shown, yes)) {
        s->type = AppMengkeQuiz::State::Royal;
        s->expect_yes = yes;
        s->asked = s->shown;
    } else {
        s->type = AppMengkeQuiz::State::Name;
        s->expect_yes = (esp_random() & 1);
        s->asked = s->shown;
        if (!s->expect_yes) do s->asked = esp_random() % MENGKE_COUNT; while (s->asked == s->shown || confusable(s->asked, s->shown));
    }
    lv_image_set_src(s->img, mk::decoded(&mengke_list[s->shown].mid));
    if (with_intro) mk::playSeq({&mengke_sfx_q_intro, question_clip(s)}, "quiz intro + question");
    else mk::playSeq({&mengke_sfx_q_next, question_clip(s)}, "quiz question");
    mclog::tagInfo("Quiz", "q#{} type={} shown={} asked={} expect={}", s->qno,
                   s->type == AppMengkeQuiz::State::Royal ? "royal" : "name", mengke_list[s->shown].en,
                   mengke_list[s->asked].en, s->expect_yes ? "yes" : "no");
}

void AppMengkeQuiz::onOpen()
{
    mclog::tagInfo("Quiz", "on open");
    _keys = std::make_unique<input::KeyManager>();
    _s    = std::make_unique<State>();
    LvglLockGuard lock;
    _root   = mk::makeRoot(0x000000);
    auto* s = _s.get();
    s->stars = mk::quizStars();
    mclog::tagInfo("Quiz", "stars loaded {}", s->stars);

    // star progress row (5 slots) + total count
    for (int i = 0; i < 5; i++) {
        s->slot[i] = mk::makeStar(_root, 40, 0x3A3466);
        lv_obj_set_pos(s->slot[i], 233 - 130 + i * 48, 26);
    }
    s->count = mk::makeLabel(_root, &mengke_font_26, 0xF5D46B, "0");
    lv_obj_set_pos(s->count, 233 + 118, 32);

    s->img = lv_image_create(_root);
    lv_obj_align(s->img, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_add_flag(s->img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        s->img, [](lv_event_t* e) { ((State*)lv_event_get_user_data(e))->tapped = 2; }, LV_EVENT_SHORT_CLICKED, s);

    s->yes = mk::makeMarkIcon(_root, true, 100);
    lv_obj_set_pos(s->yes, 233 - 60 - 100, 318);
    s->no = mk::makeMarkIcon(_root, false, 100);
    lv_obj_set_pos(s->no, 233 + 60, 318);
    lv_obj_add_event_cb(
        s->yes, [](lv_event_t* e) { ((State*)lv_event_get_user_data(e))->tapped = 1; }, LV_EVENT_SHORT_CLICKED, s);
    lv_obj_add_event_cb(
        s->no, [](lv_event_t* e) { ((State*)lv_event_get_user_data(e))->tapped = 0; }, LV_EVENT_SHORT_CLICKED, s);
    auto* la = mk::makeLabel(_root, &mengke_font_26, 0xBFB8E8, "A");
    lv_obj_set_pos(la, 233 - 60 - 58, 420);
    auto* lb = mk::makeLabel(_root, &mengke_font_26, 0xBFB8E8, "B");
    lv_obj_set_pos(lb, 233 + 60 + 42, 420);

    paint_stars(s);
    new_question(s, true);
}

static void answer(AppMengkeQuiz::State* s, lv_obj_t* root, bool said_yes, uint32_t now)
{
    bool ok = (said_yes == s->expect_yes);
    lv_obj_t* icon = said_yes ? s->yes : s->no;
    if (ok) {
        s->stars++;
        mk::setQuizStars(s->stars);
        paint_stars(s);
        static const MengkeClip* cheers[3] = {&mengke_sfx_q_ok1, &mengke_sfx_q_ok2, &mengke_sfx_q_ok3};
        const MengkeClip* cheer = cheers[esp_random() % 3];
        bool five = (s->stars % 5) == 0;
        if (five) {
            mk::playSeq({cheer, &mengke_sfx_q_five}, "quiz five stars");
            mk::starBurst(root, 233, 200, 20, 230);
            mk::starBurst(root, 233, 46, 10, 120);
            for (auto* st : s->slot) mk::bounce(st, 3);
            mclog::tagInfo("Quiz", "five-star celebration at {}", s->stars);
        } else {
            mk::playSeq({cheer, &mengke_sfx_q_star}, "quiz correct");
            mk::starBurst(root, 233, 186, 12, 190);
        }
        mk::bounce(s->img, 2);
        mk::bounce(icon, 1);
        GetHAL().vibrate(60);
        s->phase = AppMengkeQuiz::State::Feedback;
        s->until = now + (five ? 6500 : 3400);
        mclog::tagInfo("Quiz", "answer {} -> correct, stars={}", said_yes ? "yes" : "no", s->stars);
        return;
    }
    s->tries++;
    mk::shake(s->img);
    mk::shake(icon);
    GetHAL().vibrate(40);
    if (s->tries < 2) {
        static const MengkeClip* oops[2] = {&mengke_sfx_q_bad1, &mengke_sfx_q_bad2};
        mk::playSeq({oops[esp_random() % 2], question_clip(s)}, "quiz try again");
        s->lock_until = now + 800;
        mclog::tagInfo("Quiz", "answer {} -> wrong, try again", said_yes ? "yes" : "no");
    } else {
        if (s->type == AppMengkeQuiz::State::Royal)
            mk::play(s->expect_yes ? mengke_sfx_q_royal_yes : mengke_sfx_q_royal_no, "quiz reveal royal");
        else
            mk::playSeq({&mengke_sfx_q_last, &mengke_list[s->shown].name_clip}, "quiz reveal answer");
        mk::bounce(s->expect_yes ? s->yes : s->no, 2);  // point at the right answer
        s->phase = AppMengkeQuiz::State::Feedback;
        s->until = now + 3800;
        mclog::tagInfo("Quiz", "answer {} -> wrong again, reveal", said_yes ? "yes" : "no");
    }
}

void AppMengkeQuiz::onRunning()
{
    auto ev = _keys->update();
    if (ev == input::KeyEvent::GoHome) {
        close();
        return;
    }
    auto* s = _s.get();
    uint32_t now = GetHAL().millis();
    LvglLockGuard lock;
    int tap   = s->tapped;
    s->tapped = -1;
    if (s->phase == State::Ask) {
        if (tap == 2) {
            mk::play(*question_clip(s), "quiz replay");
            mk::bounce(s->img, 1);
        } else if (now >= s->lock_until) {
            if (ev == input::KeyEvent::GoPrevious || tap == 1) answer(s, _root, true, now);
            else if (ev == input::KeyEvent::GoNext || tap == 0) answer(s, _root, false, now);
        }
    } else if (now >= s->until) {
        new_question(s, false);
    }
}

void AppMengkeQuiz::onClose()
{
    mclog::tagInfo("Quiz", "on close");
    _keys.reset();
    LvglLockGuard lock;
    lv_obj_delete(_root);
    _root = nullptr;
    _s.reset();
}

AppMengkeQuiz::~AppMengkeQuiz() = default;
