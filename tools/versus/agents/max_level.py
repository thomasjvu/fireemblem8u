#!/usr/bin/env python3
"""Verify native maximum-level deployment for every preset on both seats."""
from scenarios import Session, PARTIES, verify_deployment
from check import body

for party in range(PARTIES):
    s = Session(blue_party=party, red_party=party)
    try:
        o = s.observe(0)
        verify_deployment(o, 0, party, party)
        assert all(u["level"] == 20 for u in o["units"])
        a = next(a for a in o["legal_actions"] if a["type"] == "end")
        assert s.act(0, body(s, o, a, "phase"))["accepted"]
        assert all(u["level"] == 20 for u in s.observe(1)["units"])
        print("PASS max-level preset", party, flush=True)
    finally:
        s.close()
