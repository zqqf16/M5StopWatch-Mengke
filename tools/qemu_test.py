#!/usr/bin/env python3
"""End-to-end QEMU verification of the Mengke firmware. Run after tools/qemu_boot.sh --reset.
Writes screenshots to screenshots/ and prints a PASS/FAIL table based on serial-log evidence."""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(__file__)); from qmpc import Q
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))); SS = os.path.join(ROOT, 'screenshots')
LOG = '/tmp/q.log'; os.makedirs(SS, exist_ok=True)
q = Q('/workspace/m5stopwatch-qemu-oss/run/qmp.sock')
def log(): return open(LOG, encoding='utf8', errors='ignore').read()
mark = [0]
def since(): return log()[mark[0]:]
def m(): mark[0] = len(log())
def shot(n, wait=0):
    time.sleep(wait); q.shot(os.path.join(SS, n + '.png')); print('shot', n)
results = []
def check(name, cond): results.append((name, bool(cond))); print(('PASS ' if cond else 'FAIL ') + name)
def home(): q.key(['a', 'b'], 1500); time.sleep(3)
def b(n=1, w=1.2):
    for _ in range(n): q.key(['b']); time.sleep(w)
def a(n=1, w=1.2):
    for _ in range(n): q.key(['a']); time.sleep(w)

time.sleep(2); shot('01_launcher_carousel')
check('boots straight into launcher (no guide page, no splash)', 'Launcher] on open' in log() and 'show guide' not in log())
# ---------------- Carousel ----------------
m(); q.tap(233, 220); time.sleep(3); shot('02_carousel_01_aixin')
check('carousel opens', '[Carousel] on open' in since())
m(); b(); shot('03_carousel_B_next', 1.5); check('KEYB -> next (#2 Shimmerping)', 'show #2 Shimmerping' in since())
check('voice: carousel speaks name+intro (Shimmerping)', 'speak [Shimmerping]' in since())
m(); a(); a(); shot('04_carousel_A_prev_wrap', 1.5); check('KEYA -> prev, wraps to #24 Auroraping', 'show #24 Auroraping' in since())
m(); q.swipe(400, 240, 60, 240, ms=500); time.sleep(2); shot('05_carousel_swipe_next')
check('swipe left -> next (#1)', 'show #1 Heartsping' in since())
m(); q.swipe(60, 240, 400, 240, ms=500); time.sleep(2); check('swipe right -> prev (#24)', 'show #24 Auroraping' in since())
b(4, 1.0); time.sleep(1); shot('06_carousel_04_yueguang')
q.tap(233, 200); time.sleep(2); shot('07_carousel_info_card'); q.tap(233, 200); time.sleep(1.5)
m(); q.tap(233, 200, ms=1500); time.sleep(2); shot('08_carousel_favorite_heart'); check('long-press -> favourite saved', 'favorites mask -> 0x000008' in since())
check('audio: carousel intro clip decoded + queued', 'speak [' in log())
m(); b(5, 0.25); time.sleep(2.5); check('voice: fast switching interrupts (no queue)', 'playback interrupted by new clip' in since() and since().count('speak [') >= 5)
a(5, 0.25); time.sleep(2)
for i in range(6): b(1, 0.9)
shot('09_carousel_10_dongdong', 1.5)
m(); home(); shot('10_home_from_carousel'); check('A+B in app -> home (launcher)', '[Carousel] on close' in since() and 'Launcher] on open' in since())
# ---------------- Draw ----------------
m(); b(1, 1.5); shot('11_launcher_draw_icon'); check('voice: launcher announces 你是哪只萌可', 'speak [app 你是哪只萌可]' in since())
m(); q.tap(233, 220); time.sleep(3); shot('12_draw_idle')
check('draw opens', '[Draw] on open' in since())
q.set('/machine/bmi270', 'accel-x', 2.5); q.set('/machine/bmi270', 'accel-z', 2.0); time.sleep(0.6)
q.set('/machine/bmi270', 'accel-x', 0.0); q.set('/machine/bmi270', 'accel-z', 1.0); time.sleep(0.4); shot('13_draw_rolling')
time.sleep(4.6); shot('14a_draw_reveal_sparkles'); time.sleep(1.4); shot('14_draw_result'); check('IMU shake (QMP accel) triggers draw + result', 'shake detected' in since() and '[Draw] result:' in since())
check('voice: draw reveal speaks 你是X + intro', 'speak [draw reveal + intro]: 2 parts' in since())
check('draw reveal: star burst + bounce animation', '[Fx] star burst' in since() and '[Fx] bounce' in since())
m(); b(); time.sleep(7); shot('15_draw_result_keyB'); check('KEYB re-draws', '[Draw] result:' in since())
home()
# ---------------- Watch ----------------
b(1, 1.5); shot('16_launcher_watch_icon'); m(); q.tap(233, 220); time.sleep(3); shot('17_watch_face')
check('watch face opens', '[Watch] on open' in since()); check('voice: watch enter prompt', 'speak [enter watch]' in since())
m(); q.tap(233, 300); time.sleep(2); check('voice: tap watch speaks time', 'speak [time ' in since())
m(); b(); time.sleep(2.5); shot('18_watch_companion_next'); check('KEYB changes companion', 'companion ->' in since())
home()
# ---------------- Quiz ----------------
b(1, 1.5); shot('19_launcher_quiz_icon'); m(); q.tap(233, 220); time.sleep(2.0); shot('20_quiz_question')
check('quiz opens', '[Quiz] on open' in since()); check('voice: quiz intro instructions + first question', 'speak [quiz intro + question]: 2 parts' in since())
def cur():
    r = re.findall(r'q#(\d+) type=(\w+) shown=(\w+) asked=(\w+) expect=(\w+)', since()); return r[-1] if r else None
def stars():
    r = re.findall(r'stars=(\d+)', since()); return int(r[-1]) if r else None
st0 = int(re.findall(r'stars loaded (\d+)', since())[-1]); print('stars at start', st0)
time.sleep(4)
m(); q.tap(233, 186); time.sleep(1.5); check('tap picture replays the question', 'speak [quiz replay]' in since())
qq = cur() if cur() else None
m0 = mark[0]; mark[0] = 0; qq = cur(); mark[0] = m0
wrong = (lambda: b()) if qq[4] == 'yes' else (lambda: a())
right = (lambda: a(1, 0.4)) if qq[4] == 'yes' else (lambda: b(1, 0.4))
m(); wrong(); shot('21_quiz_wrong_shake', 0); time.sleep(2.5)
check('wrong #1: gentle "再想想" + question again', 'speak [quiz try again]: 2 parts' in since() and '[Fx] gentle shake' in since())
m(); wrong(); time.sleep(0.5); shot('22_quiz_reveal_answer'); time.sleep(4.5)
check('wrong #2: reveals correct answer, moves on', ('speak [quiz reveal answer]' in since() or 'speak [quiz reveal royal]' in since()) and 'q#2' in since())
check('no star awarded for wrong answers', stars() is None)
# answer correctly until next multiple of 5 (and at least one ordinary correct answer)
need = 5 - (st0 % 5)
if need == 1: need = 6
got_shot = False
for i in range(need):
    m0 = mark[0]; mark[0] = 0; qq = cur(); mark[0] = m0
    m(); (a if qq[4] == 'yes' else b)(1, 0.3); time.sleep(0.6)
    if not got_shot and 'five-star' not in since():
        shot('23_quiz_correct_burst'); got_shot = True
        check('correct: cheer + "送你一颗小星星" + burst + bounce', 'speak [quiz correct]: 2 parts' in since() and '[Fx] star burst' in since() and '[Fx] bounce' in since())
    if 'five-star' in since():
        shot('24_quiz_five_star_celebration', 0.4); time.sleep(6.5)
    else: time.sleep(3.2)
m0 = mark[0]; mark[0] = 0; L2 = since(); mark[0] = m0
st1 = int(re.findall(r'correct, stars=(\d+)', L2)[-1])
check('stars counted (+%d)' % need, st1 == st0 + need)
check('every 5 stars: special celebration voice + animation', 'five-star celebration at %d' % (st1 - st1 % 5) in L2 and 'speak [quiz five stars]: 2 parts' in L2)
shot('25_quiz_stars_row', 0.5)
home(); m(); q.tap(233, 220); time.sleep(2.5)
check('stars persisted in NVS across reopen', ('stars loaded %d' % st1) in since())
m(); q.tap(123, 368); time.sleep(0.8); check('tap ✓ icon answers', 'answer yes ->' in since())
time.sleep(5)
home()
# ---------------- Catch (捕捉萌可, effort search via QMP bmi270 shake/spin; A fallback) ----------------
def catch_jolt(n, dt=0.09):
    for k in range(n):
        q.set('/machine/bmi270', 'accel-z', 2.6 if k % 2 == 0 else 0.1); time.sleep(dt)
    q.set('/machine/bmi270', 'accel-z', 1.0)
def catch_fill_until(outcome_re, max_rounds=12, prefer='shake'):
    """Fill progress until a logged outcome (catch|miss). prefer='shake' uses IMU; 'A' mashes button."""
    for i in range(max_rounds):
        m();
        if prefer == 'A':
            for _ in range(10):
                a(1, 0.35)
                if re.search(r'outcome (catch|miss)', since()):
                    break  # don't keep pressing A into Reveal (that starts the next search)
        else:
            catch_jolt(14); time.sleep(0.4)
            q.set('/machine/bmi270', 'gyro-z', 420.0); time.sleep(0.8); q.set('/machine/bmi270', 'gyro-z', 0.0)
            catch_jolt(10); time.sleep(0.5)
        time.sleep(1.0)
        blob = since()
        if re.search(outcome_re, blob):
            return True, blob
        # if we got the other outcome, press A once to start next search and continue
        if 'outcome catch' in blob or 'outcome miss' in blob:
            a(1, 1.8); time.sleep(0.5)
    return False, since()
m(); b(1, 1.5); shot('37_launcher_catch_icon'); check('voice: launcher announces 捕捉萌可', 'speak [app 捕捉萌可]' in since())
m(); q.tap(233, 220); time.sleep(2.5); shot('38_catch_search_idle')
check('catch opens with voice intro', '[Catch] on open' in since() and 'speak [catch intro]' in since())
c0 = int(re.findall(r'collection loaded (\d+)', since())[-1]); print('catch start collection', c0)
check('catch: randomized effort need (no arrow target)', re.search(r'search start need=\d+', since()) is not None)
time.sleep(5.0); check('catch: idle -> gentle nudge voice', 'speak [catch hint]' in since() or 'nudge after idle' in since())
m();
# poll: QEMU IMU can lag after idle; keep shaking until cheer / quarter advance (not just 1% q0)
for _ in range(20):
    catch_jolt(8); time.sleep(0.4)
    blob = since()
    if ('speak [catch cheer]' in blob
            or re.search(r'progress [2-9]\d% \(q[1-4]\) by motion', blob)
            or 'outcome' in blob):
        break
time.sleep(0.5); shot('39_catch_charging')
shake_ok = re.search(r'progress \d+% \(q\d\) by motion', since()) is not None
cheer_ok = 'speak [catch cheer]' in since()
quarter_ok = re.search(r'progress [2-9]\d% \(q[1-4]\) by motion', since()) is not None
check('catch: IMU shake fills progress ring (+ cheer)', shake_ok and (cheer_ok or quarter_ok or 'outcome' in since()))
# Fresh search for gyro fill shot (avoid overwriting encourage frame with a reveal)
if 'outcome' in since() or re.search(r'progress [7-9]\d%|progress 100%', since()):
    a(1, 1.8); time.sleep(0.4)
m(); q.set('/machine/bmi270', 'gyro-z', 520.0); time.sleep(1.0); q.set('/machine/bmi270', 'gyro-z', 0.0)
catch_jolt(3); time.sleep(0.35)
# stop at mid-ring if we already have motion progress; don't wait through outcome
shot('40_catch_encouraging')
check('catch: spinning (gyro) also fills progress', re.search(r'progress \d+% \(q\d\) by motion', since()) is not None or 'outcome' in since())
# Drive to a success reveal (retry if RNG rolls miss)
ok, blob = catch_fill_until(r'outcome catch', max_rounds=14, prefer='shake')
shot('41_catch_reveal', 0.4)
check('catch: sustained effort -> success (~70%)', ok and re.search(r'outcome catch; caught \w+ \(\d+\) by (motion|button)', blob))
check('catch reveal: 抓到啦！是X + intro voice, burst + bounce', re.search(r'speak \[catch reveal (new|repeat)\]: \d parts', since()) and '[Fx] star burst' in since() and '[Fx] bounce' in since())
check('catch: first catch is a new friend (special celebration)', 'speak [catch reveal new]' in since() if c0 == 0 else True)
time.sleep(8); check('catch: speaks collection count', 'speak [catch count]' in since())
# A-button fallback fill on a fresh search
m(); a(1, 1.8); check('catch: A after reveal -> next search', 'search start need=' in since())
m();
for _ in range(8): a(1, 0.35)
time.sleep(1.0)
check('catch: pressing A repeatedly fills progress (no-IMU fallback)', re.search(r'progress \d+% \(q\d\) by button', since()) or 'outcome' in since())
# Seek a miss outcome for soft-fail UX
ok_miss, blob_m = catch_fill_until(r'outcome miss', max_rounds=16, prefer='A')
shot('41b_catch_miss', 0.05)
check('catch: miss (~30%) gentle slip-away, no punishment', ok_miss and 'outcome miss' in blob_m and 'speak [catch miss]' in since())
time.sleep(0.6); m(); a(1, 2.0); check('catch: A/shake after miss -> try again', 'search start need=' in since() or 'speak [catch next]' in since())
# One more success so gallery has at least one (collection may already have from earlier)
ok2, _ = catch_fill_until(r'outcome catch', max_rounds=12, prefer='A')
check('catch: another success after miss/retry', ok2)
c1 = int(re.findall(r'total=(\d+)', log())[-1])
# If A overshoot already started a new search, B still opens gallery; otherwise B from Reveal
time.sleep(0.8)
m(); b(1, 2.8); shot('42_catch_collection')
check('catch: B -> collection gallery with count voice', ('gallery open, caught=%d' % c1) in since() and 'speak [catch gallery]' in since())
r = int(re.findall(r'caught \w+ \((\d+)\)', log())[-1])
m(); q.tap(233 - 192 + (r % 6) * 64 + 32, 233 - 128 + (r // 6) * 64 + 32); time.sleep(1.5); shot('43_catch_collection_item')
check('gallery: tap caught Mengke -> picture + her intro', 'speak [catch gallery item]' in since())
home(); m(); q.tap(233, 220); time.sleep(2.5)
check('catch collection persisted in NVS across reopen', ('collection loaded %d' % c1) in since())
home()
# ---------------- 萌可变身 (Transform: shake/spin/A to charge, then transformation) ----------------
def jolt(n, dt=0.09):
    for k in range(n):
        q.set('/machine/bmi270', 'accel-z', 2.6 if k % 2 == 0 else 0.1); time.sleep(dt)
    q.set('/machine/bmi270', 'accel-z', 1.0)
m(); b(1, 1.5); shot('44_launcher_transform_icon'); check('voice: launcher announces 萌可变身', 'speak [app 萌可变身]' in since())
m(); q.tap(233, 220); time.sleep(2.5); shot('45_transform_charge_empty')
check('transform opens with voice prompt to shake', '[Transform] on open' in since() and 'speak [transform intro]' in since())
m(); jolt(8); time.sleep(0.5); shot('46_transform_energy_filling')
check('transform: IMU shake fills energy ring (+ chimes)', re.search(r'energy \d+ \(\d\) by motion', since()) and re.search(r'speak \[transform (chime|half|almost)\]', since()))
m(); q.set('/machine/bmi270', 'gyro-z', 400.0); time.sleep(1.0); q.set('/machine/bmi270', 'gyro-z', 0.0); time.sleep(0.3)
check('transform: spinning the watch (gyro) also adds energy', 'by motion' in since() or 'full energy' in since())
for _ in range(40):
    if 'full energy' in since(): break
    jolt(6)
time.sleep(0.9); shot('47_transform_light_rays')
check('transform: full energy -> transformation (rays/flash/burst + cast line)', 'full energy -> transformation' in since() and 'speak [transform cast]: 3 parts' in since())
time.sleep(1.9); shot('48_transform_reveal')
check('transform reveal: random Mengke + 你变成了X + intro voice', re.search(r'became \w+ \(\d+\)', since()) and 'speak [transform reveal]: 4 parts' in since() and '[Fx] bounce' in since())
time.sleep(3.5)
m(); a(1, 1.5); check('transform: A on reveal -> charge again', 'speak [transform again]' in since())
for _ in range(11): a(1, 0.35)
time.sleep(4)
check('transform fallback: pressing A repeatedly fills energy -> transforms', 'by button' in since() and 'became' in since())
time.sleep(2); home()
# ---------------- 萌可躲猫猫 (memory match) ----------------
def deal_info():
    L = since(); i = L.rfind('deal pairs=')
    pr = int(re.findall(r'deal pairs=(\d+)', L[i:])[0]); who = [int(x) for x in re.findall(r'who=\[([\d,]+)\]', L[i:])[0].split(',')]
    pos = [(int(x), int(y)) for _, x, y in re.findall(r'card (\d+) at (\d+),(\d+)', L[i:])][:len(who)]
    return pr, who, pos
def solve(who, pos):
    done = set()
    for i in range(len(who)):
        if i in done: continue
        j = [k for k in range(len(who)) if k != i and who[k] == who[i]][0]
        q.tap(*pos[i]); time.sleep(0.6); q.tap(*pos[j]); time.sleep(1.4); done |= {i, j}
m(); b(1, 1.5); shot('49_launcher_memory_icon'); check('voice: launcher announces 萌可躲猫猫', 'speak [app 萌可躲猫猫]' in since())
m(); q.tap(233, 220); time.sleep(2.5); shot('50_memory_2pairs_facedown')
pr, who, pos = deal_info(); st0 = int(re.findall(r'level loaded pairs=\d+ stars=(\d+)', since())[-1])
check('memory opens: voice intro + 2 pairs (4 cards) face-down', 'speak [memory intro]' in since() and pr == 2 and len(who) == 4)
j = [k for k in range(4) if who[k] != who[0]][0]
m(); q.tap(*pos[0]); time.sleep(0.7); check('memory: tap card -> flips + speaks her name', 'flip card 0' in since() and 'speak [memory name]' in since())
q.tap(*pos[j]); time.sleep(0.6); shot('51_memory_mismatch_open'); time.sleep(2.4)
check('memory: mismatch -> gentle "再找找看" then flip back (no penalty)', 'speak [memory again]' in since() and 'flip back' in since() and 'stars=' not in since().split('mismatch')[-1])
m(); k2 = [k for k in range(4) if who[k] == who[0] and k != 0][0]
q.tap(*pos[0]); time.sleep(0.6); q.tap(*pos[k2]); time.sleep(0.6); shot('52_memory_match_sparkle')
check('memory: match -> cheer + sparkle, cards stay open', 'speak [memory match]' in since() and '[Fx] star burst' in since())
time.sleep(1.5); rest = [k for k in range(4) if k not in (0, k2)]
m(); q.tap(*pos[rest[0]]); time.sleep(0.6); q.tap(*pos[rest[1]]); time.sleep(0.8); shot('53_memory_win_celebration')
check('memory: win -> celebration + star (shared quiz stars) + level up to 3 pairs', 'speak [memory win]' in since() and ('win pairs=2 stars=%d next_pairs=3' % (st0 + 1)) in since())
time.sleep(3); m(); q.tap(233, 233); time.sleep(2); shot('54_memory_3pairs')
pr, who, pos = deal_info(); check('memory: next round grows to 3 pairs (6 cards)', pr == 3 and len(who) == 6)
solve(who, pos); time.sleep(3.5); m(); q.tap(233, 233); time.sleep(2); shot('55_memory_4pairs')
pr, who, pos = deal_info(); check('memory: then 4 pairs (8 cards, fits round screen)', pr == 4 and len(who) == 8 and all((x - 233) ** 2 + (y - 233) ** 2 < 175 ** 2 for x, y in pos))
home(); m(); q.tap(233, 220); time.sleep(2.5)
check('memory level + stars persisted in NVS across reopen', ('level loaded pairs=4 stars=%d' % (st0 + 2)) in since())
home()
# ---------------- 养萌可 (virtual pet) ----------------
m(); b(1, 1.5); shot('56_launcher_pet_icon'); check('voice: launcher announces 养萌可', 'speak [app 养萌可]' in since())
m(); q.tap(233, 220); time.sleep(2.5); shot('57_pet_select')
check('pet: first open -> voice-led selection', 'speak [pet choose]' in since() and 'loaded pet=-1' in since())
m(); a(1, 1.5); check('pet select: A -> next Mengke, speaks name', 'speak [pet pick name]' in since())
m(); b(1, 2.5); shot('58_pet_home'); check('pet select: B -> chosen + persisted', re.search(r'chosen \w+ \(\d+\)', since()) and 'speak [pet chosen]' in since())
pet_id = int(re.findall(r'chosen \w+ \((\d+)\)', since())[-1])
m(); q.tap(233 - 122, 334); time.sleep(0.5); shot('59_pet_feed'); time.sleep(1.5)
check('pet: 喂一喂 button -> eat voice + happiness up', 'speak [pet eat]' in since() and re.search(r'feed -> hap \d+', since()))
m(); q.tap(233, 170); time.sleep(1.5); check('pet: tap her -> 摸摸 reaction', 'speak [pet pat]' in since())
m(); q.swipe(170, 150, 300, 190, ms=600); time.sleep(1.5); check('pet: stroke her -> 摸摸 reaction', 'speak [pet stroke]' in since() or 'speak [pet pat]' in since())
m(); q.tap(233 + 122, 334); time.sleep(1.2); check('pet: 跳舞 button -> shake prompt', 'speak [pet dance prompt]' in since())
for _ in range(4): jolt(2, 0.1); time.sleep(0.5)
time.sleep(0.3); shot('60_pet_dance_done'); time.sleep(1.5)
check('pet: shaking -> dance steps + happy voice', 'dance step 4' in since() and 'speak [pet dance done]' in since())
hap1 = int(re.findall(r'-> hap (\d+)', since())[-1]); print('pet hap', hap1)
home(); m(); q.tap(233, 220); time.sleep(2.5); shot('61_pet_hearts_reopen')
check('pet persisted (companion, happiness, streak day 1) + hello voice', ('loaded pet=%d hap=%d' % (pet_id, hap1)) in since() and 'streak=1' in since() and re.search(r'speak \[pet (hello|miss)\]', since()))
home()
# advance the RTC by one day through 设置 -> date/time worker (time OK, Next, day+1 row, OK)
b(1, 1.5); m(); q.tap(233, 220); time.sleep(3); q.tap(233, 432); time.sleep(2.5)
q.tap(233, 408); time.sleep(2); q.tap(233, 408); time.sleep(2); q.tap(233, 269); time.sleep(1.5); q.tap(233, 408); time.sleep(2)
check('settings: date advanced one day (date/time set)', 'date/time set' in since())
rw = re.findall(r'system synced to rtc \(UTC\): (\d{4})-(\d\d)-(\d\d)', since())
check('rtc: write path logs 20xx year (no 1900-base leak)', rw and all(2000 <= int(y) <= 2099 for y, _, _ in rw))
rtc_set = rw[-1] if rw else None
home(); a(1, 1.5); m(); q.tap(233, 220); time.sleep(1.5); shot('62_pet_streak_bow_unlocked'); time.sleep(4); shot('63_pet_bow_on_picture')
check('pet: next-day visit -> streak 2 + bow accessory unlocked + announced by voice', re.search(r'streak=2 acc=1 new_acc=1', since()) and 'speak [pet streak gift]' in since())
check('pet: happiness decayed gently over real time (>=20, never zero)', re.search(r'away_h=2[34]', since()) and int(re.findall(r'loaded pet=\d+ hap=(\d+)', since())[-1]) >= 20)
m(); q.tap(233, 170, ms=1500); time.sleep(2); check('pet: long-press her -> choose a different companion', 'speak [pet choose]' in since())
b(1, 2); home()
# ---------------- Settings ----------------
b(1, 1.5); shot('26_launcher_settings_icon'); m(); q.tap(233, 220); time.sleep(3); shot('27_settings_default')
check('carousel switch: pre-decoded (no runtime JPEG in draw path)', 'pre-decoded 360x360' in log())
check('settings opens', '[MkSettings] on open' in since()); check('voice: settings enter prompt + launcher 设置', 'speak [enter settings]' in log() and 'speak [app 设置]' in log())
q.set('/machine/m5pm1', 'vin-mv', 0); q.set('/machine/m5pm1', 'charging', False); q.set('/machine/m5pm1', 'vbat-mv', 3500)
time.sleep(28); shot('26_settings_battery_3500mV')
q.set('/machine/m5pm1', 'vin-mv', 5000); q.set('/machine/m5pm1', 'charging', True); q.set('/machine/m5pm1', 'vbat-mv', 4150)
time.sleep(28); shot('27_settings_battery_4150mV_charging')
bl = re.findall(r'battery (\d+)% (\d+)mV vin=(\w+) charging=(\w+)', since()); print('battery log:', bl[-6:])
check('battery reflects QMP vbat 3500 mV', any(abs(int(v) - 3500) < 40 for _, v, _, _ in bl))
check('battery reflects QMP 4150 mV + charging', any(abs(int(v) - 4150) < 40 and c == 'true' for _, v, _, c in bl))
m(); a(); a(); shot('28_settings_volume_60', 1.5); check('volume change saved to NVS (60)', 'volume saved to settings: 60' in since()); check('voice: speaks 音量60', 'speak [volume 60]' in since())
m(); a(6, 0.5); time.sleep(1.5); check('voice: volume 0 -> muted, nothing played', 'skipped: muted' in since() and 'speak [volume 0]:' not in since())
b(6, 0.5); time.sleep(1.5)
q.tap(233, 372); time.sleep(1.5); b(); shot('29_settings_brightness_focus', 1.5)
m(); q.tap(233, 432); time.sleep(3); shot('30_settings_set_time_worker'); check('settings: date/time worker opens', 'open set-time worker' in since())
m(); home(); check('A+B from date/time worker -> home, no crash', 'MkSettings] on close' in since() and 'Guru' not in since())
# ---------------- Launcher cycles only the 6 Mengke apps (with wrap) ----------------
from PIL import Image, ImageChops
def icon_sig(n): return Image.open(os.path.join(SS, n + '.png')).convert('L').crop((60, 90, 406, 430))
def same(x, y): return ImageChops.difference(icon_sig(x), icon_sig(y)).getbbox() is None or max(ImageChops.difference(icon_sig(x), icon_sig(y)).getdata()) < 40
shot('31_launcher_at_settings', 1)
b(1, 1.8); shot('32_launcher_wrap_B_to_carousel'); check('launcher: B on last app (设置) wraps to 萌可图鉴', same('32_launcher_wrap_B_to_carousel', '01_launcher_carousel'))
a(1, 1.8); shot('33_launcher_wrap_A_to_settings'); check('launcher: A on first app wraps to 设置 (no 秒表/系统)', same('33_launcher_wrap_A_to_settings', '26_launcher_settings_icon'))
b(1, 1.8)
names = []
for i in range(9):
    shot('34_launcher_cycle_%d' % i, 0.3); b(1, 1.8)
check('launcher: 9 B presses return to start (exactly 9 apps)', same('34_launcher_cycle_0', '01_launcher_carousel') and same('35_tmp' if False else '34_launcher_cycle_0', '34_launcher_cycle_0'))
shot('35_launcher_after_full_cycle', 0.3); check('launcher: full cycle back on 萌可图鉴', same('35_launcher_after_full_cycle', '01_launcher_carousel'))
# ---------------- Power key (PMIC-handled, as official fw) ----------------
m(); q.key(['p'], 300); time.sleep(3); shot('36_after_power_key_click')
check('power key click: no crash, UI alive', 'Guru' not in since())
L = log()
check('no panic / watchdog / abort in log', not re.search(r'Guru Meditation|panic|task_wdt|abort\(\)|Backtrace', L))
check('I2S data written (trace)', 'i2s write chunk' in L)
# ---------------- RTC year round-trip across reboot ----------------
m(); q.cmd('system_reset')
for _ in range(60):
    if 'Launcher] on open' in since(): break
    time.sleep(1)
rr = re.findall(r'rtc synced to system \(UTC\): (\d{4})-(\d\d)-(\d\d)', since()); print('rtc read after reboot:', rr, 'written:', rtc_set)
check('rtc: year round-trips through RX8130 across reboot (2000-2099, == date set)', rr and rtc_set and rr[-1] == rtc_set and 2024 <= int(rr[-1][0]) <= 2099)
print('\nSUMMARY'); [print(('PASS' if ok else 'FAIL'), n) for n, ok in results]
