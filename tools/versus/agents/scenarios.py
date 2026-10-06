#!/usr/bin/env python3
"""Scenario selection, preset combinations, and both armies' castle captures."""

from session import Session, ROOT
import json

CATALOG = json.loads((ROOT / "versus/catalog.json").read_text())
MAPS = len(CATALOG["maps"])
PARTIES = len(CATALOG["parties"])
from check import body, choose


def verify_deployment(o, map_id, blue_party, red_party):
    # Assert against the selected catalog, rather than just peer agreement.
    expected = [
        [(1, 12, 10)[tile] for tile in row] for row in CATALOG["maps"][map_id]["tiles"]
    ]
    if o["rules"]["objective"]["seize_enabled"]:
        expected[7][1] = expected[7][13] = 11
    assert o["terrain"] == expected, "Native terrain differs from catalog"
    for u in o["units"]:
        seat = u["seat"]
        index = (u["id"] & 127) - 1
        preset = CATALOG["parties"][[blue_party, red_party][seat]]["units"][index]
        assert u["max_hp"] == preset["hp"]
        for key in ["power", "speed", "defense", "resistance", "role"]:
            assert u[key] == preset[key], (key, u, preset)
        assert [u["x"], u["y"]] == CATALOG["maps"][map_id]["deployment"][seat][index]


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
            if step == 0:
                verify_deployment(o, map_id, blue_party, red_party)
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


def battle(map_id, opener, blue_party=None, red_party=None):
    s = Session(
        red=bool(opener),
        map_id=map_id,
        blue_party=map_id % PARTIES if blue_party is None else blue_party,
        red_party=(map_id + 1) % PARTIES if red_party is None else red_party,
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
    for m in range(MAPS):
        for opener in range(2):
            battle(m, opener)
    for m in range(MAPS):
        for objective in [1, 2]:
            for seat in range(2):
                capture(
                    m,
                    objective,
                    seat,
                    blue_party=m % PARTIES,
                    red_party=(m + 1) % PARTIES,
                )
    for party in range(MAPS, PARTIES):
        for opener in range(2):
            battle(party % MAPS, opener, party, (party + 1) % PARTIES)
        for seat in range(2):
            capture(party % MAPS, 2, seat, party, party)
    # All independent party pairings can deploy and agree.
    for b in range(PARTIES):
        for r in range(PARTIES):
            s = Session(blue_party=b, red_party=r)
            try:
                o = s.observe(0)
                assert len(o["units"]) == 10
                verify_deployment(o, 0, b, r)
                a = choose(o)
                assert s.act(0, body(s, o, a, "preset"))["accepted"]
                assert not any(a["type"] == "seize" for a in o["legal_actions"])
                print("PASS parties", b, r, flush=True)
            finally:
                s.close()
