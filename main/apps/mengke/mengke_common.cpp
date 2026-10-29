#include "mengke_common.h"
#include <assets/mengke/mengke_sfx5.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <mooncake_log.h>
#include <vector>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <src/libs/tjpgd/tjpgd.h>
#include <src/misc/cache/instance/lv_image_cache.h>

namespace mk {

static const int8_t kIdx[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static const int16_t kStep[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,    31,
    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,   130,   143,
    157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,   544,   598,   658,
    724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
    3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

static void decodeAppend(const MengkeClip& clip, std::vector<int16_t>& pcm16)
{
    int pred = 0, idx = 0;
    for (uint32_t i = 0; i < clip.samples; i++) {
        uint8_t code = (clip.adpcm[i >> 1] >> ((i & 1) * 4)) & 0x0F;
        int step = kStep[idx];
        int diff = step >> 3;
        if (code & 4) diff += step;
        if (code & 2) diff += step >> 1;
        if (code & 1) diff += step >> 2;
        pred = (code & 8) ? pred - diff : pred + diff;
        pred = std::clamp(pred, -32768, 32767);
        idx  = std::clamp(idx + kIdx[code & 7], 0, 88);
        pcm16.push_back((int16_t)pred);
    }
}

void playSeq(std::initializer_list<const MengkeClip*> clips, const char* tag)
{
    if (GetHAL().isDisplayAsleep()) {
        mclog::tagInfo("Mengke-Audio", "speak [{}] skipped: display asleep", tag ? tag : "?");
        return;
    }
    if (GetHAL().getSpeakerVolume() <= 0) {
        mclog::tagInfo("Mengke-Audio", "speak [{}] skipped: muted", tag ? tag : "?");
        return;
    }
    std::vector<int16_t> pcm16;
    int parts = 0;
    for (auto* c : clips) {
        if (!c || !c->adpcm || !c->samples) continue;
        parts++;
        if (!pcm16.empty()) pcm16.insert(pcm16.end(), MENGKE_CLIP_RATE / 20, 0);  // 50 ms gap
        decodeAppend(*c, pcm16);
    }
    if (pcm16.empty()) return;
    const size_t n_in = pcm16.size();
    const int out_rate = GetHAL().getAudioSampleRate();
    const size_t n_out = (size_t)((uint64_t)n_in * out_rate / MENGKE_CLIP_RATE);
    std::vector<int16_t> out(n_out);
    for (size_t i = 0; i < n_out; i++) {
        uint32_t pos_q16 = (uint32_t)(((uint64_t)i * MENGKE_CLIP_RATE << 16) / out_rate);
        uint32_t p = pos_q16 >> 16, f = pos_q16 & 0xFFFF;
        int a = pcm16[std::min<size_t>(p, n_in - 1)];
        int b = pcm16[std::min<size_t>(p + 1, n_in - 1)];
        out[i] = (int16_t)(a + (((b - a) * (int)f) >> 16));
    }
    mclog::tagInfo("Mengke-Audio", "speak [{}]: {} parts -> {} samples @{}Hz (vol {}) (interrupts previous)",
                   tag ? tag : "?", parts, out.size(), out_rate, GetHAL().getSpeakerVolume());
    GetHAL().audioPlay(out, true);
}

void play(const MengkeClip& clip, const char* tag) { playSeq({&clip}, tag); }

void sayNumber(int v, std::vector<const MengkeClip*>& out)
{
    v = std::clamp(v, 0, 100);
    if (v <= 59 || v % 10 == 0) { out.push_back(&mengke_num[v]); return; }
    out.push_back(&mengke_num[v - v % 10]);
    out.push_back(&mengke_num[v % 10]);
}

void playList(const std::vector<const MengkeClip*>& v, const char* tag)
{
    // small fixed fan-out helper
    const MengkeClip* a[6] = {};
    for (size_t i = 0; i < v.size() && i < 6; i++) a[i] = v[i];
    playSeq({a[0], a[1], a[2], a[3], a[4], a[5]}, tag);
}

void sayTime(int h, int m)
{
    std::vector<const MengkeClip*> v{&mengke_sfx_w_now, &mengke_num[h % 24], &mengke_sfx_w_dian};
    if (m == 0) v.push_back(&mengke_sfx_w_zheng);
    else { if (m < 10) v.push_back(&mengke_num[0]); v.push_back(&mengke_num[m]); v.push_back(&mengke_sfx_w_fen); }
    char t[24]; snprintf(t, sizeof t, "time %02d:%02d", h, m);
    playList(v, t);
}

void sayLevel(bool volume, int pct)
{
    std::vector<const MengkeClip*> v{volume ? &mengke_sfx_w_vol : &mengke_sfx_w_bri};
    sayNumber(pct, v);
    char t[24]; snprintf(t, sizeof t, "%s %d", volume ? "volume" : "brightness", pct);
    playList(v, t);
}

static const MengkeClip* appClip(const char* name)
{
    if (!name) return nullptr;
    if (!strcmp(name, "萌可图鉴")) return &mengke_sfx_app_carousel;
    if (!strcmp(name, "你是哪只萌可")) return &mengke_sfx_app_draw;
    if (!strcmp(name, "萌可表盘")) return &mengke_sfx_app_watch;
    if (!strcmp(name, "萌可问答")) return &mengke_sfx_app_quiz;
    if (!strcmp(name, "捕捉萌可")) return &mengke_sfx_app_catch;
    if (!strcmp(name, "设置")) return &mengke_sfx_app_settings;
    if (!strcmp(name, "萌可变身")) return &mengke_sfx_app_transform;
    if (!strcmp(name, "萌可躲猫猫")) return &mengke_sfx_app_memory;
    if (!strcmp(name, "养萌可")) return &mengke_sfx_app_pet;
    return nullptr;
}

void sayAppName(const char* name)
{
    auto* c = appClip(name);
    if (c) { char t[48]; snprintf(t, sizeof t, "app %s", name); playSeq({c}, t); }
}

/* ---------------------------- Decoded image cache ---------------------------- */
namespace {
struct DecCtx {
    const uint8_t* src;
    size_t len, pos;
    uint16_t* out;
    int w;
};
size_t jd_in(JDEC* jd, uint8_t* buf, size_t n)
{
    auto* c = (DecCtx*)jd->device;
    n = std::min(n, c->len - c->pos);
    if (buf) memcpy(buf, c->src + c->pos, n);
    c->pos += n;
    return n;
}
int jd_out(JDEC* jd, void* bitmap, JRECT* r)
{
    auto* c = (DecCtx*)jd->device;
    const uint8_t* p = (const uint8_t*)bitmap;  // RGB888 (JD_FORMAT 0)
    for (int y = r->top; y <= r->bottom; y++) {
        uint16_t* d = c->out + y * c->w + r->left;
        for (int x = r->left; x <= r->right; x++, p += 3)
            *d++ = (uint16_t)(((p[2] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[0] >> 3));  // LVGL tjpgd emits B,G,R
    }
    return 1;
}
struct Entry {
    const lv_image_dsc_t* key;
    lv_image_dsc_t dsc;
    uint32_t used;
};
constexpr int kCacheSlots = 8;  // 8 x 360x360 RGB565 = 2 MB PSRAM worst case
Entry _cache[kCacheSlots] = {};
uint32_t _use_seq = 0;
}  // namespace

static Entry* cache_find(const lv_image_dsc_t* k)
{
    for (auto& e : _cache)
        if (e.key == k) return &e;
    return nullptr;
}

static Entry* decode_into_cache(const lv_image_dsc_t* jpg)
{
    int64_t t0 = esp_timer_get_time();
    const int w = jpg->header.w, h = jpg->header.h;
    uint16_t* px = (uint16_t*)heap_caps_malloc((size_t)w * h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!px) return nullptr;
    static uint8_t* pool = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    JDEC jd;
    DecCtx ctx{jpg->data, jpg->data_size, 0, px, w};
    if (jd_prepare(&jd, jd_in, pool, 4096, &ctx) != JDR_OK || jd_decomp(&jd, jd_out, 0) != JDR_OK) {
        heap_caps_free(px);
        mclog::tagError("Mengke-Img", "jpeg decode failed");
        return nullptr;
    }
    // evict LRU
    Entry* slot = &_cache[0];
    for (auto& e : _cache) {
        if (!e.key) { slot = &e; break; }
        if (e.used < slot->used) slot = &e;
    }
    if (slot->key) {
        lv_image_cache_drop(&slot->dsc);  // LVGL must not keep pointing at the freed pixels
        heap_caps_free((void*)slot->dsc.data);
    }
    slot->key              = jpg;
    slot->dsc              = {};
    slot->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    slot->dsc.header.cf    = LV_COLOR_FORMAT_RGB565;
    slot->dsc.header.w     = w;
    slot->dsc.header.h     = h;
    slot->dsc.header.stride = w * 2;
    slot->dsc.data_size    = w * h * 2;
    slot->dsc.data         = (const uint8_t*)px;
    mclog::tagInfo("Mengke-Img", "pre-decoded {}x{} jpeg ({} B) -> RGB565 PSRAM in {} ms", w, h, jpg->data_size,
                   (int)((esp_timer_get_time() - t0) / 1000));
    return slot;
}

const lv_image_dsc_t* decoded(const lv_image_dsc_t* jpg)
{
    Entry* e = cache_find(jpg);
    if (!e) e = decode_into_cache(jpg);
    if (!e) return jpg;
    e->used = ++_use_seq;
    return &e->dsc;
}

const lv_image_dsc_t* decodedPinned(const lv_image_dsc_t* jpg)
{
    // Decode once into a never-evicted buffer (launcher icons keep this pointer forever)
    Entry* e = decode_into_cache(jpg);
    if (!e) return jpg;
    auto* d = new lv_image_dsc_t(e->dsc);
    e->key  = nullptr;  // detach: slot no longer owns the pixels
    e->dsc  = {};
    e->used = 0;
    return d;
}

void prefetch(const lv_image_dsc_t* jpg)
{
    if (!cache_find(jpg)) {
        Entry* e = decode_into_cache(jpg);
        if (e) e->used = _use_seq;  // keep as old-ish but present
    }
}

/* --------------------------------- NVS ------------------------------------ */
static int32_t get_int(const char* key, int32_t def)
{
    Settings s("mengke", false);
    return s.GetInt(key, def);
}
static void set_int(const char* key, int32_t v)
{
    Settings s("mengke", true);
    s.SetInt(key, v);
}

static int32_t _fav_cache = -1;
uint32_t favMask()
{
    if (_fav_cache < 0) _fav_cache = get_int("fav", 0) & 0xFFFFFF;
    return (uint32_t)_fav_cache;
}
bool isFav(int idx) { return (favMask() >> idx) & 1; }
void toggleFav(int idx)
{
    _fav_cache = (int32_t)(favMask() ^ (1u << idx));
    set_int("fav", _fav_cache);
    mclog::tagInfo("Mengke", "favorites mask -> 0x{:06x}", (unsigned)_fav_cache);
}
int lastIndex() { return wrap(get_int("last", 0)); }
void setLastIndex(int idx) { set_int("last", idx); }
int watchIndex() { return wrap(get_int("watch", 0)); }
void setWatchIndex(int idx) { set_int("watch", idx); }
int quizBest() { return get_int("qbest", 0); }
void setQuizBest(int v) { set_int("qbest", v); }

int quizStars() { return get_int("stars", 0); }
void setQuizStars(int v) { set_int("stars", v); }

uint32_t caughtMask() { return (uint32_t)get_int("catch", 0) & 0xFFFFFF; }
void setCaughtMask(uint32_t m) { set_int("catch", (int32_t)(m & 0xFFFFFF)); }

int getInt(const char* key, int32_t def) { return get_int(key, def); }
void setInt(const char* key, int32_t v) { set_int(key, v); }

int wrap(int i) { return ((i % MENGKE_COUNT) + MENGKE_COUNT) % MENGKE_COUNT; }

/* ---------------------------------- UI ------------------------------------ */
lv_obj_t* makeRoot(uint32_t bg)
{
    lv_obj_t* root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 466, 466);
    lv_obj_center(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    return root;
}

lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const char* text)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, text);
    return l;
}

lv_dir_t gestureDir()
{
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return LV_DIR_NONE;
    return lv_indev_get_gesture_dir(indev);
}

static void swipe_cb(lv_event_t* e)
{
    auto* sw   = (Swipe*)lv_event_get_user_data(e);
    auto code  = lv_event_get_code(e);
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (code == LV_EVENT_PRESSED) {
        sw->start = p;
        sw->maxd  = 0;
    } else if (code == LV_EVENT_PRESSING) {
        int d = LV_MAX(LV_ABS(p.x - sw->start.x), LV_ABS(p.y - sw->start.y));
        if (d > sw->maxd) sw->maxd = d;
    } else if (code == LV_EVENT_RELEASED) {
        int dx = p.x - sw->start.x, dy = p.y - sw->start.y;
        if (LV_ABS(dx) > 70 && LV_ABS(dx) > LV_ABS(dy)) sw->step = dx < 0 ? 1 : -1;
    }
}

void attachSwipe(lv_obj_t* obj, Swipe* sw)
{
    lv_obj_add_event_cb(obj, swipe_cb, LV_EVENT_PRESSED, sw);
    lv_obj_add_event_cb(obj, swipe_cb, LV_EVENT_PRESSING, sw);
    lv_obj_add_event_cb(obj, swipe_cb, LV_EVENT_RELEASED, sw);
}


/* --------------------------------- Effects -------------------------------- */
static void star_draw_cb(lv_event_t* e)
{
    lv_obj_t* obj = (lv_obj_t*)lv_event_get_current_target(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    int R = lv_area_get_width(&a) / 2, r = R * 2 / 5;
    lv_point_precise_t outer[5], inner[5];
    for (int i = 0; i < 5; i++) {
        int ao = -90 + i * 72, ai = -54 + i * 72;
        outer[i].x = cx + R * lv_trigo_cos(ao) / 32768;
        outer[i].y = cy + R * lv_trigo_sin(ao) / 32768;
        inner[i].x = cx + r * lv_trigo_cos(ai) / 32768;
        inner[i].y = cy + r * lv_trigo_sin(ai) / 32768;
    }
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
    d.opa   = lv_obj_get_style_opa_recursive(obj, LV_PART_MAIN);
    for (int i = 0; i < 5; i++) {  // tips
        d.p[0] = outer[i]; d.p[1] = inner[i]; d.p[2] = inner[(i + 4) % 5];
        lv_draw_triangle(layer, &d);
        d.p[0] = {cx, cy}; d.p[1] = inner[i]; d.p[2] = inner[(i + 4) % 5];  // centre pentagon fan
        lv_draw_triangle(layer, &d);
    }
}

lv_obj_t* makeStar(lv_obj_t* parent, int size, uint32_t color)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(o, star_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    return o;
}

lv_obj_t* makeMarkIcon(lv_obj_t* parent, bool check, int size)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(check ? 0x2FBF71 : 0xFF6F91), 0);
    lv_obj_set_style_border_width(o, 5, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(0xFFFFFF), 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    static lv_point_precise_t ck[3], x1[2], x2[2];
    auto mk_line = [&](lv_point_precise_t* pts, int n) {
        lv_obj_t* l = lv_line_create(o);
        lv_line_set_points(l, pts, n);
        lv_obj_set_style_line_width(l, size / 9, 0);
        lv_obj_set_style_line_rounded(l, true, 0);
        lv_obj_set_style_line_color(l, lv_color_hex(0xFFFFFF), 0);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(l, 0, 0);
        lv_obj_set_size(l, size, size);
    };
    int s = size;
    if (check) {
        ck[0] = {s * 27 / 100, s * 52 / 100}; ck[1] = {s * 44 / 100, s * 68 / 100}; ck[2] = {s * 72 / 100, s * 34 / 100};
        mk_line(ck, 3);
    } else {
        x1[0] = {s * 32 / 100, s * 32 / 100}; x1[1] = {s * 68 / 100, s * 68 / 100};
        x2[0] = {s * 68 / 100, s * 32 / 100}; x2[1] = {s * 32 / 100, s * 68 / 100};
        mk_line(x1, 2);
        mk_line(x2, 2);
    }
    return o;
}

static void anim_tx(void* o, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t*)o, v, 0); }
static void anim_ty(void* o, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t*)o, v, 0); }
static void anim_opa(void* o, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }

void starBurst(lv_obj_t* parent, int cx, int cy, int n, int radius)
{
    static const uint32_t cols[] = {0xF5D46B, 0xFF6FA8, 0x7FB2FF, 0x5ED6C0, 0xFFFFFF, 0xC59BFF};
    for (int i = 0; i < n; i++) {
        int sz = 22 + (int)(esp_timer_get_time() / 7 + i * 13) % 20;
        lv_obj_t* st = makeStar(parent, sz, cols[i % 6]);
        lv_obj_set_pos(st, cx - sz / 2, cy - sz / 2);
        int ang = i * 360 / n + (i & 1) * 17;
        int rr  = radius * (70 + (i * 37) % 30) / 100;
        int dx = rr * lv_trigo_cos(ang) / 32768, dy = rr * lv_trigo_sin(ang) / 32768;
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, st);
        lv_anim_set_duration(&a, 900);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, anim_tx);
        lv_anim_set_values(&a, 0, dx);
        lv_anim_start(&a);
        lv_anim_set_exec_cb(&a, anim_ty);
        lv_anim_set_values(&a, 0, dy);
        lv_anim_start(&a);
        lv_anim_set_exec_cb(&a, anim_opa);
        lv_anim_set_values(&a, 255, 0);
        lv_anim_set_delay(&a, 700);
        lv_anim_set_duration(&a, 600);
        lv_anim_set_path_cb(&a, lv_anim_path_linear);
        lv_anim_set_completed_cb(&a, [](lv_anim_t* an) { lv_obj_delete_async((lv_obj_t*)an->var); });
        lv_anim_start(&a);
    }
    mclog::tagInfo("Fx", "star burst x{}", n);
}

void bounce(lv_obj_t* obj, int times)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, anim_ty);
    lv_anim_set_values(&a, 0, -28);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_reverse_duration(&a, 260);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_repeat_count(&a, times);
    lv_anim_start(&a);
    mclog::tagInfo("Fx", "bounce");
}

void shake(lv_obj_t* obj)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, anim_tx);
    lv_anim_set_values(&a, -12, 12);
    lv_anim_set_duration(&a, 90);
    lv_anim_set_reverse_duration(&a, 90);
    lv_anim_set_repeat_count(&a, 3);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, [](lv_anim_t* an) { lv_obj_set_style_translate_x((lv_obj_t*)an->var, 0, 0); });
    lv_anim_start(&a);
    mclog::tagInfo("Fx", "gentle shake");
}

}  // namespace mk
