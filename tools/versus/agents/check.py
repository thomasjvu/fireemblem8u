#!/usr/bin/env python3
"""Exercise agent ownership, retries and complete native tactical matches."""

from session import Session
from concurrent.futures import ThreadPoolExecutor


def choose(o):
    actions = o["legal_actions"]
    units = {u["id"]: u for u in o["units"]}
    enemies = [u for u in units.values() if u["seat"] != o["seat"] and not u["dead"]]
    attacks = [a for a in actions if a["type"] == "attack"]
    if attacks:
        return min(attacks, key=lambda a: units[a["target"]]["hp"])
    heals = [a for a in actions if a["type"] == "heal"]
    if heals:
        return min(
            heals, key=lambda a: units[a["target"]]["hp"] / units[a["target"]]["max_hp"]
        )
    potions = [
        a
        for a in actions
        if a["type"] == "vulnerary"
        and units[a["actor"]]["hp"] < units[a["actor"]]["max_hp"] / 2
    ]
    if potions:
        return potions[0]
    moves = [
        a
        for a in actions
        if a["type"] == "wait" and units[a["actor"]]["role"] != "healer"
    ]
    if moves and enemies:
        return min(
            moves,
            key=lambda a: min(
                abs(a["x"] - e["x"]) + abs(a["y"] - e["y"]) for e in enemies
            ),
        )
    return next(a for a in actions if a["type"] == "end")


def body(s, o, a, rid):
    return dict(
        match_id=s.id,
        sequence=o["sequence"],
        state_hash=o["state_hash"],
        action_id=a["id"],
        request_id=rid,
    )


def check(red=False):
    s = Session(red=red)
    try:
        seat = int(red)
        o = s.observe(seat)
        assert o["rules"]["objective"]["id"] == "elimination"
        assert o["rules"]["party"]["size"] == 5
        assert o["rules"]["map"]["castles"] == []
        a = choose(o)
        b = body(s, o, a, "first")
        assert s.act(1 - seat, b)["error"] == "not_your_turn"
        assert s.act(seat, {**b, "match_id": "wrong"})["error"] == "wrong_match"
        assert s.act(seat, {**b, "action_id": "00"})["error"] == "illegal_action"
        assert s.act(seat, {**b, "sequence": 99})["error"] == "stale_observation"
        before = s.stable()
        s.observe(seat)
        after = s.stable()
        assert before == after, "query mutated confirmed state"
        with ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(lambda _: s.act(seat, b), range(2)))
        r = results[0]
        assert r["accepted"]
        assert results[1] == r
        assert s.act(seat, b) == r
        assert (
            s.act(seat, {**b, "action_id": "changed"})["error"]
            == "request_id_reused_with_different_action"
        )
        attacks = 0
        for i in range(500):
            p = s.stable()[0]
            if p["outcome"]:
                break
            o = s.observe(p["active"])
            assert all(
                a["actor"] != a["target"]
                for a in o["legal_actions"]
                if a["type"] == "heal"
            )
            a = choose(o)
            attacks += a["type"] == "attack"
            r = s.act(o["seat"], body(s, o, a, str(i)))
            assert r.get("accepted"), r
        else:
            raise AssertionError("match did not finish")
        assert attacks > 0
        assert s.stable()[0]["outcome"] in (1, 2, 3)
        print(
            "PASS",
            "red" if red else "blue",
            "opener",
            i + 1,
            "actions",
            attacks,
            "attacks",
            "outcome",
            r["outcome"],
            "evidence",
            s.log,
            flush=True,
        )
    finally:
        s.close()


if __name__ == "__main__":
    check()
    check(True)
