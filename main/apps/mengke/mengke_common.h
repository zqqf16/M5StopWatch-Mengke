// 奇妙萌可之闪耀流星 fan firmware - shared helpers
#pragma once
#include <assets/mengke/mengke_data.h>
#include <assets/mengke/mengke_sfx5.h>
#include <lvgl.h>
#include <cstdint>
#include <vector>
#include <initializer_list>

namespace mk {

// Decode an IMA-ADPCM clip (16 kHz mono), resample to the codec rate and queue it on the ES8311 (async).
void play(const MengkeClip& clip, const char* tag = nullptr);
// Concatenate clips into one utterance. Any new speech interrupts the previous one; muted at volume 0.
void playSeq(std::initializer_list<const MengkeClip*> clips, const char* tag = nullptr);
void playList(const std::vector<const MengkeClip*>& v, const char* tag = nullptr);  // max 6 parts
void sayNumber(int v, std::vector<const MengkeClip*>& out);
void sayTime(int h, int m);
void sayLevel(bool volume, int pct);
void sayAppName(const char* name);  // launcher app name

// Persistent state (NVS namespace "mengke")
uint32_t favMask();
bool isFav(int idx);
void toggleFav(int idx);
int lastIndex();
void setLastIndex(int idx);
int watchIndex();
void setWatchIndex(int idx);
int quizBest();
void setQuizBest(int v);

// Pre-decoded image cache: JPEG (LV_COLOR_FORMAT_RAW) -> RGB565 buffer in PSRAM, LRU.
// Returns a ready-to-draw descriptor (falls back to the JPEG on failure). Not thread-safe: call from app task.
const lv_image_dsc_t* decoded(const lv_image_dsc_t* jpeg);
const lv_image_dsc_t* decodedPinned(const lv_image_dsc_t* jpeg);  // never evicted (app icons)
void prefetch(const lv_image_dsc_t* jpeg);  // decode now if not cached (no-op otherwise)

// UI helpers (call with LVGL lock held)
lv_obj_t* makeRoot(uint32_t bg = 0x000000);
lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const char* text);
// Returns LV_DIR_LEFT / LV_DIR_RIGHT etc. if the current event is a gesture, else LV_DIR_NONE
lv_dir_t gestureDir();

int wrap(int i);
// generic NVS int (namespace "mengke"), keys <= 15 chars
int getInt(const char* key, int32_t def);
void setInt(const char* key, int32_t v);

// ---- Kid-friendly effects (call with LVGL lock held) ----
lv_obj_t* makeStar(lv_obj_t* parent, int size, uint32_t color);           // filled 5-point star
lv_obj_t* makeMarkIcon(lv_obj_t* parent, bool check, int size);           // big ✓ (green) / ✗ (pink) disc
void starBurst(lv_obj_t* parent, int cx, int cy, int n, int radius);      // sparkles fly out & fade (auto-deleted)
void bounce(lv_obj_t* obj, int times = 2);                                // happy jump
void shake(lv_obj_t* obj);                                                // gentle side-to-side wiggle
int quizStars();
void setQuizStars(int v);
uint32_t caughtMask();          // 捕捉萌可 collection (bit i = mengke_list[i]), NVS key "catch"
void setCaughtMask(uint32_t m);

// Simple horizontal swipe tracker (robust against slow touch polling; LVGL gestures were unreliable here).
struct Swipe {
    lv_point_t start{0, 0};
    int maxd = 0;            // max movement during the current press (px)
    volatile int step = 0;   // +1 = swipe left (next), -1 = swipe right (prev)
};
void attachSwipe(lv_obj_t* obj, Swipe* sw);
inline bool isTap(const Swipe* sw) { return sw->maxd < 24; }

}  // namespace mk
