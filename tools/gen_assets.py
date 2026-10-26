#!/usr/bin/env python3
"""Generate Mengke firmware assets: round-cropped JPEG avatars, app icons,
IMA-ADPCM TTS clips (edge-tts), and the C data table. Fonts: see gen_fonts.sh"""
import json, os, subprocess, struct, asyncio, io
from PIL import Image, ImageDraw, ImageFilter
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
IMG = os.path.join(ROOT, 'research/img'); OUT = os.path.join(ROOT, 'main/assets/mengke'); CACHE = os.path.join(HERE, 'cache')
os.makedirs(OUT, exist_ok=True); os.makedirs(CACHE, exist_ok=True)
D = json.load(open(os.path.join(HERE, 'mengke.json'), encoding='utf8'))
AV = 360; MID = 220; Q = 80; VOICE = 'zh-CN-XiaoyiNeural'; SR = 16000

def hexc(c): return tuple(int(c[i:i+2], 16) for i in (0, 2, 4))

def round_card(src, size, color, pad=0.90, ring=True):
    S = size * 3
    bg = Image.new('RGB', (S, S), (0, 0, 0))
    c = hexc(color)
    # radial gradient disc: light center -> theme colour edge
    g = Image.new('RGB', (S, S), c); gd = ImageDraw.Draw(g)
    for i in range(60):
        r = S / 2 * (1 - i / 60); t = i / 59
        col = tuple(int(c[k] * (1 - t * 0.75) + 255 * t * 0.75) for k in range(3))
        gd.ellipse((S/2 - r, S/2 - r, S/2 + r, S/2 + r), fill=col)
    g = g.filter(ImageFilter.GaussianBlur(S / 60))
    mask = Image.new('L', (S, S), 0); ImageDraw.Draw(mask).ellipse((2, 2, S - 3, S - 3), fill=255)
    bg.paste(g, (0, 0), mask)
    if ring:
        ImageDraw.Draw(bg).ellipse((2, 2, S - 3, S - 3), outline=(255, 255, 255), width=S // 90)
    im = Image.open(src).convert('RGBA'); im = im.crop(im.getbbox())
    box = int(S * pad); im.thumbnail((box, box), Image.LANCZOS)
    x = (S - im.width) // 2; y = int(S * 0.97) - im.height if im.height > S * 0.7 else (S - im.height) // 2
    y = min(y, (S - im.height) // 2 + int(S * 0.06))
    # soft shadow
    sh = Image.new('RGBA', (S, S), (0, 0, 0, 0)); a = im.split()[3].point(lambda v: v * 0.35)
    sh.paste((0, 0, 0, 255), (x + S // 80, y + S // 60), a); sh = sh.filter(ImageFilter.GaussianBlur(S / 120))
    comp = bg.convert('RGBA'); comp.alpha_composite(sh)
    clip = Image.new('RGBA', (S, S), (0, 0, 0, 0)); clip.paste(im, (x, y), im)
    m2 = Image.new('L', (S, S), 0); ImageDraw.Draw(m2).ellipse((S//90, S//90, S - S//90, S - S//90), fill=255)
    from PIL import ImageChops
    clip.putalpha(ImageChops.multiply(clip.split()[3], m2))
    comp.alpha_composite(clip)
    return comp.convert('RGB').resize((size, size), Image.LANCZOS)

def jpeg(im, q=Q):
    b = io.BytesIO(); im.save(b, 'JPEG', quality=q, optimize=True, subsampling=2, progressive=False); return b.getvalue()

# ---------------- IMA ADPCM ----------------
STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]
IDX = [-1,-1,-1,-1,2,4,6,8]
def adpcm_enc(pcm):
    pred = 0; idx = 0; out = bytearray(); nib = []
    for s in pcm:
        step = STEP[idx]; d = s - pred; code = 0
        if d < 0: code = 8; d = -d
        diff = step >> 3
        if d >= step: code |= 4; d -= step; diff += step
        step >>= 1
        if d >= step: code |= 2; d -= step; diff += step
        step >>= 1
        if d >= step: code |= 1; diff += step
        pred = pred - diff if code & 8 else pred + diff
        pred = max(-32768, min(32767, pred)); idx = max(0, min(88, idx + IDX[code & 7])); nib.append(code)
    if len(nib) & 1: nib.append(0)
    for i in range(0, len(nib), 2): out.append(nib[i] | (nib[i+1] << 4))
    return bytes(out)

async def tts(text, path):
    import edge_tts
    await edge_tts.Communicate(text, VOICE, rate='-5%', pitch='+15Hz').save(path)

def clip(key, text):
    mp3 = os.path.join(CACHE, key + '.mp3')
    if not os.path.exists(mp3): asyncio.run(tts(text, mp3))
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', mp3, '-af', 'silenceremove=start_periods=1:start_threshold=-45dB,areverse,silenceremove=start_periods=1:start_threshold=-45dB,areverse,loudnorm=I=-14:TP=-1.5,volume=1.0',
                          '-ar', str(SR), '-ac', '1', '-f', 's16le', '-'], capture_output=True, check=True).stdout
    pcm = struct.unpack('<%dh' % (len(raw) // 2), raw)
    return adpcm_enc(pcm), len(pcm)

def chimes():
    # soft rising bell tones (sine + 2nd harmonic, exp decay), synthesized: chime1..3 rising, sparkle = arpeggio
    import math
    def tone(freqs, dur=0.22, gap=0.0):
        out = []
        for f in freqs:
            N = int(SR * dur)
            for i in range(N):
                t = i / SR; e = math.exp(-t * 9) * min(1, i / 80)
                out.append(int(9000 * e * (math.sin(2 * math.pi * f * t) + 0.3 * math.sin(4 * math.pi * f * t))))
        return out
    res = []
    for k, fr in [('ch1', [784]), ('ch2', [988]), ('ch3', [1175]), ('ch4', [1319, 1568]), ('sparkle', [1047, 1319, 1568, 2093])]:
        pcm = tone(fr, 0.16 if len(fr) > 1 else 0.3); res.append((k, len(pcm), adpcm_enc(pcm)))
    return res

def carr(name, data):
    lines = [', '.join('0x%02x' % b for b in data[i:i+24]) for i in range(0, len(data), 24)]
    return 'static const uint8_t %s[%d] = {\n%s\n};\n' % (name, len(data), ',\n'.join(lines))


def badge(im, kind):
    S = im.width; d = ImageDraw.Draw(im); r = S * 0.17; cx, cy = S * 0.80, S * 0.80
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 255, 255), outline=(60, 60, 90), width=max(2, S // 70))
    col = (80, 70, 140)
    if kind == 'gear':
        import math
        for k in range(8):
            a = k * math.pi / 4; x, y = cx + math.cos(a) * r * 0.62, cy + math.sin(a) * r * 0.62
            d.ellipse((x - r * .2, y - r * .2, x + r * .2, y + r * .2), fill=col)
        d.ellipse((cx - r * .55, cy - r * .55, cx + r * .55, cy + r * .55), fill=col)
        d.ellipse((cx - r * .22, cy - r * .22, cx + r * .22, cy + r * .22), fill=(255, 255, 255))
    elif kind == 'arrow':
        w = r * 0.28
        d.line((cx - r * .5, cy + r * .5, cx + r * .3, cy - r * .3), fill=(255, 111, 168), width=int(w))
        d.polygon([(cx + r * .62, cy - r * .62), (cx + r * .62, cy + r * .02), (cx - r * .02, cy - r * .62)], fill=(255, 111, 168))
    elif kind == 'clock':
        d.ellipse((cx - r * .7, cy - r * .7, cx + r * .7, cy + r * .7), outline=col, width=max(3, S // 45))
        d.line((cx, cy, cx, cy - r * .5), fill=col, width=max(3, S // 45)); d.line((cx, cy, cx + r * .35, cy), fill=col, width=max(3, S // 45))
    elif kind == 'star':
        import math
        pts = [(cx + math.cos(-math.pi/2 + k*math.pi/5) * r * (0.72 if k % 2 == 0 else 0.3), cy + math.sin(-math.pi/2 + k*math.pi/5) * r * (0.72 if k % 2 == 0 else 0.3)) for k in range(10)]
        d.polygon(pts, fill=(245, 190, 40))
    elif kind == 'cards':
        w = r * 0.42
        d.rounded_rectangle((cx - r*.62, cy - r*.5, cx - r*.62 + w*1.3, cy + r*.5), radius=r*.1, fill=(127, 178, 255))
        d.rounded_rectangle((cx + r*.62 - w*1.3, cy - r*.5, cx + r*.62, cy + r*.5), radius=r*.1, fill=(255, 111, 168))
    elif kind == 'heart':
        d.ellipse((cx - r*.6, cy - r*.5, cx, cy + r*.1), fill=(255, 80, 130)); d.ellipse((cx, cy - r*.5, cx + r*.6, cy + r*.1), fill=(255, 80, 130))
        d.polygon([(cx - r*.58, cy - r*.12), (cx + r*.58, cy - r*.12), (cx, cy + r*.6)], fill=(255, 80, 130))
    else:
        from PIL import ImageFont
        f = ImageFont.truetype(os.path.join(HERE, 'fonts/ZCOOLKuaiLe-Regular.ttf'), int(r * 1.3))
        d.text((cx, cy + r * 0.05), kind, font=f, fill=col, anchor='mm')
    return im

ICONS = [('carousel', 'aixin', '图'), ('draw', 'yueguang', '?'), ('watch', 'yinguang', 'clock'), ('quiz', 'xingguang', '答'), ('settings', 'guangming', 'gear'), ('catch', 'qiaoqiao', 'arrow'), ('transform', 'aixin', 'star'), ('memory', 'tutu', 'cards'), ('pet', 'xiongxiong', 'heart')]
def gen_icons(c):
    tot = 0; byid = {m['id']: m for m in D}
    for name, mid, kind in ICONS:
        m = byid[mid]; im = badge(round_card(os.path.join(IMG, m['img'] + '.png'), 200, m['color'], pad=0.86), kind)
        b = jpeg(im, 85); tot += len(b); im.save(os.path.join(ROOT, 'research/preview/icon_%s.jpg' % name))
        c.append(carr('icon_%s_jpg' % name, b))
        c.append('const lv_image_dsc_t mengke_icon_%s = {{LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_RAW, 0, 200, 200, 0, 0}, sizeof(icon_%s_jpg), icon_%s_jpg, NULL, NULL};\n' % (name, name, name))
    return tot

def main():
    c = ['// AUTO-GENERATED by tools/gen_assets.py - do not edit\n#include "mengke_data.h"\n']
    rows = []; tot_img = tot_aud = 0
    prev = os.path.join(ROOT, 'research/preview'); os.makedirs(prev, exist_ok=True)
    for i, m in enumerate(D):
        card = round_card(os.path.join(IMG, m['img'] + '.png'), AV, m['color'])
        jb = jpeg(card); tot_img += len(jb); card.save(os.path.join(prev, m['id'] + '.jpg'), quality=90)
        thumb = jpeg(round_card(os.path.join(IMG, m['img'] + '.png'), MID, m['color']), 82); tot_img += len(thumb)
        a, n = clip('name_' + m['id'], m['cn']); tot_aud += len(a)
        b, n2 = clip('you_' + m['id'], '你是' + m['cn'] + '！'); tot_aud += len(b)
        e, n3 = clip('intro_' + m['id'], m['cn'] + '，' + m['intro']); tot_aud += len(e)
        c += [carr('img_%s' % m['id'], jb), carr('thm_%s' % m['id'], thumb), carr('snd_%s' % m['id'], a), carr('you_%s' % m['id'], b), carr('itr_%s' % m['id'], e)]
        rows.append(m | dict(jb=len(jb), th=len(thumb), n=n, n2=n2, n3=n3))
    extra = {'quiz_ok': '答对啦，真棒！', 'quiz_bad': '不对哦，再想想！', 'draw_hint': '摇一摇，看看你是哪只萌可！', 'hello': '欢迎来到奇妙萌可之闪耀流星！'}
    for k, t in extra.items():
        a, n = clip('x_' + k, t); tot_aud += len(a); c.append(carr('sfx_' + k, a))
        c.append('const MengkeClip mengke_sfx_%s = {sfx_%s, sizeof(sfx_%s), %d};\n' % (k, k, k, n))
    extra2 = {'app_carousel': '萌可图鉴', 'app_draw': '你是哪只萌可', 'app_watch': '萌可表盘', 'app_quiz': '萌可问答', 'app_settings': '设置',
              'enter_watch': '点一点萌可，听听现在几点。', 'enter_settings': '这里可以调音量和亮度。',
              'w_now': '现在是', 'w_dian': '点', 'w_fen': '分', 'w_zheng': '整', 'w_vol': '音量', 'w_bri': '亮度',
              'w_who': '这是谁呀？', 'w_ans': '正确答案是'}
    for k, t in extra2.items():
        a, n = clip('x_' + k, t); tot_aud += len(a); c.append(carr('sfx_' + k, a))
        c.append('const MengkeClip mengke_sfx_%s = {sfx_%s, sizeof(sfx_%s), %d};\n' % (k, k, k, n))
    # v3: voice yes/no quiz + reveal celebration
    extra3 = {'q_intro': '萌可问答开始啦！对的按A，不对按B。点一点图片，可以再听一遍。',
              'q_royal': '这是皇室萌可吗？',
              'q_ok1': '答对啦，真棒！', 'q_ok2': '太厉害啦，答对啦！', 'q_ok3': '真聪明，你答对啦！',
              'q_star': '送你一颗小星星！',
              'q_bad1': '不对哦，再想想！', 'q_bad2': '没关系，再试一次吧！',
              'q_last': '没关系，正确答案是', 'q_royal_yes': '没关系，她是皇室萌可哦！', 'q_royal_no': '没关系，她不是皇室萌可哦！',
              'q_five': '哇！你又集齐了五颗星星，你是闪耀的萌可小达人！',
              'q_next': '下一题！'}
    for k, t in extra3.items():
        a, n = clip('x_' + k, t); tot_aud += len(a); c.append(carr('sfx_' + k, a))
        c.append('const MengkeClip mengke_sfx_%s = {sfx_%s, sizeof(sfx_%s), %d};\n' % (k, k, k, n))
    # v4: 捕捉萌可 (effort search: shake/spin/A, no directional arrow)
    extra4 = {'app_catch': '捕捉萌可',
              'c_intro': '萌可藏起来啦！摇一摇、转一转手表，去找找她吧！',
              'c_go1': '加油！', 'c_go2': '快找到啦！', 'c_go3': '再摇一摇！', 'c_go4': '好棒，继续！',
              'c_hint1': '再摇一摇手表吧！', 'c_hint2': '转一转，或者按按A键也可以哦！',
              'c_miss': '哎呀，她跑掉啦！再试一次吧！',
              'c_got': '抓到啦！是', 'c_new': '是新朋友哦，太棒啦！', 'c_again': '又见面啦！',
              'c_have': '你已经抓到', 'c_zhi': '只萌可啦！', 'c_all': '哇！你把所有萌可都抓到啦，你是萌可小队长！',
              'c_more': '按A再找一只，按B看看你的萌可收藏。',
              'c_gallery': '这是你的萌可收藏！点一点，听听她们是谁。', 'c_none': '还没有抓到萌可哦，快去找找吧！',
              'c_secret': '这只还没抓到哦！'}
    for k, t in extra4.items():
        a, n = clip('x_' + k, t); tot_aud += len(a); c.append(carr('sfx_' + k, a))
        c.append('const MengkeClip mengke_sfx_%s = {sfx_%s, sizeof(sfx_%s), %d};\n' % (k, k, k, n))
    # v5: 萌可变身 / 萌可躲猫猫 / 养萌可 (original generic lines, not official catchphrases)
    extra5 = {'app_transform': '萌可变身', 'app_memory': '萌可躲猫猫', 'app_pet': '养萌可',
              't_intro': '萌可变身！快摇一摇手表，给魔法能量充满电吧！',
              't_hint': '用力摇一摇，或者转一转手表！按A键也可以哦！',
              't_half': '能量越来越多啦！加油！', 't_almost': '马上就满啦！', 't_full': '能量满啦！',
              't_cast1': '闪耀流星，变身！', 't_cast2': '星星的光芒，变身！', 't_cast3': '流星魔法，闪亮变身！',
              't_become': '哇！你变成了', 't_again': '摇一摇，再变身一次吧！',
              'm_intro': '萌可躲猫猫！萌可藏在卡片后面，点一点卡片，找出两个一样的萌可！',
              'm_hint': '点一点卡片，翻开看看是谁！',
              'm_match1': '找到一对啦，真棒！', 'm_match2': '一模一样，太厉害啦！',
              'm_again1': '不一样哦，再找找看！', 'm_again2': '没关系，记住她们藏在哪里哦！',
              'm_win': '全部找到啦！你的记性真好！送你一颗小星星！',
              'm_more': '下一次，会有更多萌可来躲猫猫哦！', 'm_next': '再玩一次！萌可又藏起来啦！',
              'p_choose': '选一个萌可做你的好朋友吧！按A换一个，按B或者点一点她，就选她！',
              'p_chosen': '太好啦！我们是好朋友啦！',
              'p_hello': '你来啦！我好开心！', 'p_miss': '我好想你呀！快来陪我玩吧！',
              'p_menu': '喂一喂，摸摸我，或者一起跳舞吧！',
              'p_feed_name': '喂一喂', 'p_pet_name': '摸摸', 'p_play_name': '跳舞',
              'p_eat1': '啊呜！好好吃！谢谢你！', 'p_eat2': '真香呀，我吃饱饱啦！',
              'p_pet1': '嘻嘻，好舒服呀！', 'p_pet2': '好痒呀，我好喜欢你！',
              'p_dance': '摇一摇手表，我们一起跳舞吧！', 'p_dance_ok': '跳得真棒！好开心呀！',
              'p_full': '我最最喜欢你啦！',
              'p_streak_a': '你已经连续', 'p_streak_b': '天来看我啦！',
              'p_gift_bow': '送你一个漂亮的蝴蝶结！', 'p_gift_crown': '哇！送你一顶闪亮的皇冠！', 'p_gift_wand': '送你一根星星魔法棒！'}
    for k, t in extra5.items():
        a, n = clip('x_' + k, t); tot_aud += len(a); c.append(carr('sfx_' + k, a))
        c.append('const MengkeClip mengke_sfx_%s = {sfx_%s, sizeof(sfx_%s), %d};\n' % (k, k, k, n))
    hdr = os.path.join(OUT, 'mengke_sfx5.h')
    open(hdr, 'w', encoding='utf8').write('// AUTO-GENERATED by tools/gen_assets.py\n#pragma once\n#include "mengke_data.h"\n#ifdef __cplusplus\nextern "C" {\n#endif\n'
        + ''.join('extern const MengkeClip mengke_sfx_%s;\n' % k for k in extra5) + 'extern const lv_image_dsc_t mengke_icon_transform, mengke_icon_memory, mengke_icon_pet;\n#ifdef __cplusplus\n}\n#endif\n')
    for k, n, a in chimes(): tot_aud += len(a); c.append(carr('sfx_' + k, a)); c.append('const MengkeClip mengke_sfx_%s = {sfx_%s, sizeof(sfx_%s), %d};\n' % (k, k, k, n))
    asks = []
    for m in D:
        a, n = clip('ask_' + m['id'], '这是' + m['cn'] + '吗？'); tot_aud += len(a); c.append(carr('ask_' + m['id'], a)); asks.append((m['id'], n))
    c.append('const MengkeClip mengke_ask[MENGKE_COUNT] = {\n' + ''.join('  {ask_%s, sizeof(ask_%s), %d},\n' % (i, i, n) for i, n in asks) + '};\n')
    nums = []
    for i in range(101):
        if i > 59 and i % 10: continue
        a, n = clip('num_%d' % i, str(i)); tot_aud += len(a); c.append(carr('num_%d' % i, a)); nums.append((i, n))
    c.append('const MengkeClip mengke_num[101] = {\n' + ''.join('  [%d] = {num_%d, sizeof(num_%d), %d},\n' % (i, i, i, n) for i, n in nums) + '};\n')
    tot_img += gen_icons(c)
    c.append('const Mengke mengke_list[MENGKE_COUNT] = {\n')
    for r in rows:
        c.append('  {"%s", "%s", "%s", "%s", "%s", "%s", 0x%s,\n   {{LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_RAW, 0, %d, %d, 0, 0}, sizeof(img_%s), img_%s, NULL, NULL},\n   {{LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_RAW, 0, %d, %d, 0, 0}, sizeof(thm_%s), thm_%s, NULL, NULL},\n   {snd_%s, sizeof(snd_%s), %d}, {you_%s, sizeof(you_%s), %d}, "%s", {itr_%s, sizeof(itr_%s), %d}},\n'
                 % (r['cn'], r['en'], r['attr'], r['group'], r['ep'], r['desc'], r['color'], AV, AV, r['id'], r['id'], MID, MID, r['id'], r['id'], r['id'], r['id'], r['n'], r['id'], r['id'], r['n2'], r['intro'].replace('"','\\"'), r['id'], r['id'], r['n3']))
    c.append('};\n')
    open(os.path.join(OUT, 'mengke_data.c'), 'w', encoding='utf8').write(''.join(c))
    print('avatars+thumbs JPEG total: %d bytes (%.1f KB), audio ADPCM total: %d bytes (%.1f KB)' % (tot_img, tot_img/1024, tot_aud, tot_aud/1024))
    for r in rows: print(r['cn'], r['jb'], r['th'], '%.2fs' % (r['n']/SR))
main()
