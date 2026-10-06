#!/usr/bin/env python3
"""Scenario selection, preset combinations, and both armies' castle captures."""

from session import Session
from check import body, choose


def capture(map_id, objective, seat, blue_party=0, red_party=0):
    s = Session(
        red=bool(seat),
        map_id=map_id,
        objective=objective,
        blue_party=blue_party,
        red_party=red_party,
    )
    try:
        captures = 0
        waited_on_castle = False
        for step in range(150):
            p = s.stable()[0]
            if p["outcome"]:
                break
            o = s.observe(p["active"])
            actions = o["legal_actions"]
            assert o["rules"]["map"]["id"] == s.catalog["maps"][map_id]["id"]
            assert (
                o["rules"]["parties"][0]["id"] == s.catalog["parties"][blue_party]["id"]
            )
            assert (
                o["rules"]["parties"][1]["id"] == s.catalog["parties"][red_party]["id"]
            )
            assert all(
                (a["x"], a["y"]) == ((13, 7) if o["seat"] == 0 else (1, 7))
                for a in actions
                if a["type"] == "seize"
            )
            if o["seat"] != seat:
                a = next(a for a in actions if a["type"] == "end")
            else:
                seize = [a for a in actions if a["type"] == "seize"]
                if seize:
                    a = seize[0]
                    if (
                        map_id == 0
                        and objective == 2
                        and seat == 0
                        and not waited_on_castle
                    ):
                        a = next(
                            w
                            for w in actions
                            if w["type"] == "wait"
                            and w["actor"] == a["actor"]
                            and w["x"] == a["x"]
                            and w["y"] == a["y"]
                        )
                        waited_on_castle = True
                    else:
                        captures += 1
                else:
                    # Use the sword unit to take a clear route toward the enemy castle.
                    moves = [
                        a
                        for a in actions
                        if a["type"] == "wait" and a["actor"] == (129 if seat else 1)
                    ]
                    tx = 1 if seat else 13
                    a = (
                        min(moves, key=lambda a: abs(a["x"] - tx) + abs(a["y"] - 7))
                        if moves
                        else next(a for a in actions if a["type"] == "end")
                    )
            request = body(s, o, a, str(step))
            r = s.act(o["seat"], request)
            assert r.get("accepted"), r
            if a["type"] == "seize":
                assert s.act(o["seat"], request) == r
        else:
            raise AssertionError("capture did not finish")
        final = s.observe(seat)
        assert (
            captures == 1
            and final["outcome"] == seat + 1
            and final["victory_reason"] == "seizure"
        ), final
        assert all(
            not u["dead"] for u in final["units"]
        ), "capture should win without killing"
        print(
            "PASS capture",
            map_id,
            objective,
            seat,
            blue_party,
            red_party,
            step,
            "actions",
            flush=True,
        )
    finally:
        s.close()


def battle(map_id, opener):
    s = Session(
        red=bool(opener), map_id=map_id, blue_party=map_id, red_party=(map_id + 1) % 3
    )
    try:
        attacks = 0
        for step in range(500):
            p = s.stable()[0]
            if p["outcome"]:
                break
            o = s.observe(p["active"])
            a = choose(o)
            attacks += a["type"] == "attack"
            result = s.act(o["seat"], body(s, o, a, str(step)))
            assert result.get("accepted"), result
        else:
            raise AssertionError("Elimination scenario did not finish")
        assert attacks > 0 and s.stable()[0]["outcome"] in [1, 2, 3]
        print(
            "PASS elimination",
            map_id,
            opener,
            step,
            "actions",
            attacks,
            "attacks",
            flush=True,
        )
    finally:
        s.close()


def mismatch():
    for peer in [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 2]]:
        try:
            Session(peer_choices=peer)
        except RuntimeError as e:
            assert "ROM link failure" in str(e)
        else:
            raise AssertionError("Different selections must reject lobby")
    print(
        "PASS mismatched map, blue party, red party and objective rejected", flush=True
    )


if __name__ == "__main__":
    mismatch()
    for m in range(3):
        for opener in range(2):
            battle(m, opener)
    for m in range(3):
        for objective in [1, 2]:
            for seat in range(2):
                capture(m, objective, seat, blue_party=m, red_party=(m + 1) % 3)
    # All nine independent party pairings can deploy and agree.
    for b in range(3):
        for r in range(3):
            s = Session(blue_party=b, red_party=r)
            try:
                o = s.observe(0)
                assert len(o["units"]) == 10
                a = choose(o)
                assert s.act(0, body(s, o, a, "preset"))["accepted"]
                assert not any(a["type"] == "seize" for a in o["legal_actions"])
                print("PASS parties", b, r, flush=True)
            finally:
                s.close()
