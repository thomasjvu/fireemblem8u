#!/usr/bin/env python3
"""Exercise desktop human input through the native menus and linked ROM.

SDL dummy drivers verify the input/ROM path without a display or physical pad.
Visible rendering and actual hardware need separate checks.
"""
import os
import threading
import time
import uuid
from pathlib import Path
from session import Session

os.environ.setdefault('SDL_VIDEODRIVER', 'dummy')
os.environ.setdefault('SDL_AUDIODRIVER', 'dummy')
ROOT = Path(__file__).resolve().parents[3]
s = Session(desktop=True, human_seats=3, evidence=ROOT / 'build/versus/desktop-checks' / uuid.uuid4().hex)

def key(seat, mask):
    with s.lock:
        assert s.rpc(f'keys {seat} {mask}').get('accepted')
    time.sleep(.08)
    with s.lock:
        s.rpc(f'keys {seat} 0')
    time.sleep(.5)

def commit(seat, keys):
    seq = s.stable()[0]['seq']
    answer, errors = [], []
    def wait():
        try: answer.append(s.human_action(seat, seq))
        except Exception as exc: errors.append(exc)
    thread = threading.Thread(target=wait, daemon=True)
    thread.start()
    time.sleep(.5)
    for mask in keys: key(seat, mask)
    thread.join(20)
    assert not thread.is_alive(), 'Native UI command did not commit'
    assert not errors, errors
    assert answer[0]['sequence'] == seq + 1, answer
    return s.stable()[0]

try:
    time.sleep(1)
    # Inactive Red input cannot submit or alter the Blue command sequence.
    with s.lock: s.rpc('control 1 1')
    key(1, 772)
    assert s.stable()[0]['seq'] == 0
    with s.lock: s.rpc('control 1 0')
    # Cancel movement, then move Neimi north and choose Wait through normal menus.
    state = commit(0, [1, 64, 2, 1, 64, 1, 128, 1])
    assert state['active'] == 0
    unit = next(u for u in s.observe(0)['units'] if u['id'] == 3)
    assert unit['y'] == 11 and unit['spent'], unit
    # Map menu End through A on the spent unit, A to confirm.
    state = commit(0, [1, 1])
    assert state['active'] == 1
    state = commit(1, [1, 128, 1, 128, 1])
    unit = next(u for u in s.observe(1)['units'] if u['id'] == 131)
    assert unit['y'] == 3 and unit['spent'], unit
    # Agent commands continue in the very same emulator after human UI input.
    o = s.observe(1)
    action = next(a for a in o['legal_actions'] if a['type'] == 'end')
    assert s.act(1, {'match_id': s.id, 'sequence': o['sequence'], 'state_hash': o['state_hash'],
                     'action_id': action['id'], 'request_id': uuid.uuid4().hex})['accepted']
    state = commit(0, [772])
    assert state['outcome'] == 2
    assert s.stable()[1]['hash'] == state['hash']
    previous = s
    s = Session(desktop=True, human_seats=1, map_id=1, red=True, reuse=previous,
                evidence=ROOT / 'build/versus/desktop-checks' / uuid.uuid4().hex)
    assert s.p.pid == previous.p.pid and s.id != previous.id
    assert s.observe(1)['active_seat'] == 1
    assert s.rpc('control 1 1').get('error') == 'not_human_seat'
    previous.close()
    assert s.p.poll() is None
    print('PASS persistent native window: new map, opener, human seats and match identity, same process')
    print('PASS desktop native cancel, Blue/Red move+Wait, End, agent handoff, surrender and peer agreement')
finally:
    s.close()
