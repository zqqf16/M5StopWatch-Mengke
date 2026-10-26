// Mengke data table (generated content in mengke_data.c)
#pragma once
#include <lvgl.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MENGKE_COUNT 24
#define MENGKE_CLIP_RATE 16000
typedef struct { const uint8_t* adpcm; uint32_t bytes; uint32_t samples; } MengkeClip;
typedef struct {
    const char* name;   // 中文名
    const char* en;     // English name
    const char* attr;   // 代表元素/属性
    const char* group;  // 皇室 / 星星 / 传奇
    const char* ep;     // 收服/登场集数
    const char* desc;   // 一句话简介
    uint32_t color;     // theme colour
    lv_image_dsc_t avatar;  // 360x360 JPEG (round, black outside)
    lv_image_dsc_t mid;     // 220x220 JPEG (pre-sized: TJPGD output can't be scaled by LVGL)
    MengkeClip name_clip;   // "X萌可"
    MengkeClip you_clip;    // "你是X萌可！"
    const char* intro;      // kid-friendly one-liner
    MengkeClip intro_clip;  // "X萌可，<intro>"
} Mengke;
extern const Mengke mengke_list[MENGKE_COUNT];
extern const MengkeClip mengke_sfx_quiz_ok, mengke_sfx_quiz_bad, mengke_sfx_draw_hint, mengke_sfx_hello;
extern const MengkeClip mengke_sfx_app_carousel, mengke_sfx_app_draw, mengke_sfx_app_watch, mengke_sfx_app_quiz, mengke_sfx_app_settings,
    mengke_sfx_enter_watch, mengke_sfx_enter_settings, mengke_sfx_w_now, mengke_sfx_w_dian, mengke_sfx_w_fen, mengke_sfx_w_zheng,
    mengke_sfx_w_vol, mengke_sfx_w_bri, mengke_sfx_w_who, mengke_sfx_w_ans;
extern const MengkeClip mengke_num[101];
extern const MengkeClip mengke_sfx_q_intro, mengke_sfx_q_royal, mengke_sfx_q_ok1, mengke_sfx_q_ok2, mengke_sfx_q_ok3, mengke_sfx_q_star,
    mengke_sfx_q_bad1, mengke_sfx_q_bad2, mengke_sfx_q_last, mengke_sfx_q_royal_yes, mengke_sfx_q_royal_no, mengke_sfx_q_five, mengke_sfx_q_next;
extern const MengkeClip mengke_sfx_app_catch, mengke_sfx_c_intro, mengke_sfx_c_go1, mengke_sfx_c_go2, mengke_sfx_c_go3, mengke_sfx_c_go4,
    mengke_sfx_c_hint1, mengke_sfx_c_hint2, mengke_sfx_c_miss, mengke_sfx_c_got, mengke_sfx_c_new, mengke_sfx_c_again, mengke_sfx_c_have,
    mengke_sfx_c_zhi, mengke_sfx_c_all, mengke_sfx_c_more, mengke_sfx_c_gallery, mengke_sfx_c_none, mengke_sfx_c_secret, mengke_sfx_ch1,
    mengke_sfx_ch2, mengke_sfx_ch3, mengke_sfx_ch4, mengke_sfx_sparkle;
extern const MengkeClip mengke_ask[MENGKE_COUNT];  // "这是X萌可吗？"  // 0..59 and tens up to 100
LV_FONT_DECLARE(mengke_font_40);
LV_FONT_DECLARE(mengke_font_26);
LV_FONT_DECLARE(mengke_font_digits);
#ifdef __cplusplus
}
#endif
#ifdef __cplusplus
extern "C" {
#endif
extern const lv_image_dsc_t mengke_icon_carousel, mengke_icon_draw, mengke_icon_watch, mengke_icon_quiz, mengke_icon_settings, mengke_icon_catch;
#ifdef __cplusplus
}
#endif
