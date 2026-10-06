#!/usr/bin/env python3
"""Seat-scoped command-line client for Codex gameplay."""

import argparse, json, time, uuid, urllib.request

p = argparse.ArgumentParser()
p.add_argument("credentials")
p.add_argument("operation", choices=["observe", "act", "wait"])
p.add_argument("--action")
p.add_argument("--sequence", type=int)
p.add_argument("--hash", type=int)
p.add_argument("--request-id")
a = p.parse_args()
c = json.load(open(a.credentials))


def call(path, body):
    req = urllib.request.Request(
        c["url"] + "/" + path,
        data=json.dumps(body).encode(),
        headers={
            "Authorization": "Bearer " + c["token"],
            "Content-Type": "application/json",
        },
    )
    with urllib.request.urlopen(req, timeout=45) as r:
        return json.load(r)


if a.operation == "act":
    result = call(
        "act",
        {
            "match_id": c["match_id"],
            "action_id": a.action,
            "sequence": a.sequence,
            "state_hash": a.hash,
            "request_id": a.request_id or uuid.uuid4().hex,
        },
    )
else:
    end = time.monotonic() + 50
    while True:
        result = call("observe", {})
        if (
            a.operation == "observe"
            or result["your_turn"]
            or result["outcome"]
            or time.monotonic() > end
        ):
            break
        time.sleep(1)
print(json.dumps(result))
