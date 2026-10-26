#!/usr/bin/env python3
"""QMP helper for testing the Mengke firmware in m5stopwatch-qemu (keys, touch, long-press, swipe, qom-set)."""
import json, socket, sys, time
class Q:
    def __init__(s, path):
        s.s = socket.socket(socket.AF_UNIX); s.s.connect(path); s.f = s.s.makefile('rw'); s.f.readline(); s.cmd('qmp_capabilities')
    def cmd(s, n, **a):
        s.f.write(json.dumps({'execute': n, 'arguments': a}) + '\n'); s.f.flush()
        while True:
            r = json.loads(s.f.readline())
            if 'return' in r or 'error' in r: return r
    def abs(s, x, y, b=None):
        ev = [{'type': 'abs', 'data': {'axis': 'x', 'value': int(x * 32767 / 465)}}, {'type': 'abs', 'data': {'axis': 'y', 'value': int(y * 32767 / 465)}}]
        if b is not None: ev.append({'type': 'btn', 'data': {'down': b, 'button': 'left'}})
        s.cmd('input-send-event', events=ev)
    def key(s, names, ms=150):
        ev = lambda d: [{'type': 'key', 'data': {'down': d, 'key': {'type': 'qcode', 'data': n}}} for n in names]
        s.cmd('input-send-event', events=ev(True)); time.sleep(ms / 1000); s.cmd('input-send-event', events=ev(False))
    def tap(s, x, y, ms=150): s.abs(x, y, True); time.sleep(ms / 1000); s.abs(x, y, False)
    def swipe(s, x0, y0, x1, y1, ms=600, n=20):
        s.abs(x0, y0, True)
        for k in range(1, n + 1): time.sleep(ms / 1000 / n); s.abs(x0 + (x1 - x0) * k / n, y0 + (y1 - y0) * k / n)
        time.sleep(0.05); s.abs(x1, y1, False)
    def shot(s, f): s.cmd('screendump', filename=f, format='png')
    def set(s, path, prop, val): return s.cmd('qom-set', path=path, property=prop, value=val)
    def get(s, path, prop): return s.cmd('qom-get', path=path, property=prop).get('return')
if __name__ == '__main__':
    q = Q(sys.argv[1]); exec(sys.argv[2])
