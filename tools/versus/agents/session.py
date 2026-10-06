#!/usr/bin/env python3
"""Two isolated linked GBA cores, with serialized agent observations/actions."""

from rules import RULES
from pathlib import Path
import json, subprocess, time, threading, uuid, struct, secrets, argparse, hashlib
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

ROOT = Path(__file__).resolve().parents[3]


class Session:
    def __init__(self, red=False, evidence=None):
        self.lock = threading.RLock()
        self.id = uuid.uuid4().hex
        self.keys = [secrets.token_urlsafe(24) for _ in range(2)]
        self.cache = {}
        self.legal_cache = {}
        self.failed = None
        self.log = Path(evidence or ROOT / "build/versus/agent-matches" / self.id)
        self.log.mkdir(parents=True, exist_ok=False)
        manifest = json.loads((ROOT / "build/versus/manifest.json").read_text())
        self.manifest = manifest
        if (
            hashlib.sha256(
                (ROOT / "build/versus/fire-emblem-versus.gba").read_bytes()
            ).hexdigest()
            != manifest["rom_sha256"]
        ):
            raise RuntimeError("ROM does not match build manifest")
        symbols = {}
        for line in (ROOT / "build/versus/engine.ld").read_text().splitlines():
            if " = " in line:
                key, value = line.rstrip(";").split(" = ")
                symbols[key] = value.replace("0x", "")
        args = [
            str(ROOT / "build/versus/agent-bridge"),
            str(ROOT / "build/versus/fire-emblem-versus.gba"),
            symbols["gBmMapTerrain"],
        ]
        if red:
            args.append("--red")
        self.p = subprocess.Popen(
            args,
            cwd=ROOT,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=(self.log / "emulator.log").open("w"),
            text=True,
            bufsize=1,
        )
        assert json.loads(self.p.stdout.readline())["ready"]
        self.event(
            "start",
            {"match": self.id, "manifest": manifest, "rules": RULES, "red_opener": red},
        )
        self.stable()

    def event(self, kind, data):
        with (self.log / "events.jsonl").open("a") as f:
            f.write(
                json.dumps({"time": time.time(), "kind": kind, "data": data}) + "\n"
            )

    def rpc(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        result = self.p.stdout.readline()
        if not result:
            raise RuntimeError(
                "Emulator exited; inspect " + str(self.log / "emulator.log")
            )
        return json.loads(result)

    def stable(self, deadline=15):
        if self.failed:
            raise RuntimeError(self.failed)
        end = time.monotonic() + deadline
        while time.monotonic() < end:
            a = self.rpc("observe 0")
            b = self.rpc("observe 1")
            if a["error"] or b["error"]:
                raise RuntimeError(f"ROM link failure {a['error']}/{b['error']}")
            if a["state"] == b["state"] == 1 and all(
                a[k] == b[k]
                for k in [
                    "seq",
                    "hash",
                    "active",
                    "round",
                    "outcome",
                    "units",
                    "terrain",
                    "rng",
                ]
            ):
                return a, b
            time.sleep(0.005)
        raise TimeoutError("Peers did not reach the same confirmed state")

    def legal(self, seat, snapshot):
        key = (snapshot["seq"], snapshot["hash"], seat)
        if key in self.legal_cache:
            return self.legal_cache[key]
        result = []
        first = 0
        while True:
            page = self.rpc(f"legal {seat} {first}")
            if "error" in page:
                raise RuntimeError(page["error"])
            raw = bytes.fromhex(page["commands"])
            for off in range(0, len(raw), 24):
                command = raw[off : off + 24]
                seq, h, r0, r1, r2, actor, target, x, y, kind, item, cost = (
                    struct.unpack("<II3H7B3x", command)
                )
                result.append(
                    {
                        "id": command.hex(),
                        "type": {
                            1: "wait",
                            2: "attack",
                            3: "heal",
                            26: "vulnerary",
                            240: "end",
                            241: "surrender",
                        }[kind],
                        "actor": actor,
                        "target": target,
                        "x": x,
                        "y": y,
                        "item": item,
                        "cost": cost,
                    }
                )
            first += page["count"]
            if first >= page["total"]:
                break
            if not page["count"]:
                raise RuntimeError("Legal action pagination stalled")
        self.legal_cache = {key: result}
        return result

    def observe(self, seat):
        with self.lock:
            peers = self.stable()
            s = peers[seat]
            units = []
            raw = bytes.fromhex(s["units"])
            for n in range(10):
                b = raw[n * 72 : (n + 1) * 72]
                state = int.from_bytes(b[12:16], "little")
                units.append(
                    {
                        "id": (0 if n < 5 else 128) + n % 5 + 1,
                        "seat": n // 5,
                        "role": ["sword", "axe", "bow", "mage", "healer"][n % 5],
                        "x": b[16],
                        "y": b[17],
                        "hp": b[19],
                        "max_hp": b[18],
                        "power": b[20],
                        "skill": b[21],
                        "speed": b[22],
                        "defense": b[23],
                        "resistance": b[24],
                        "luck": b[25],
                        "dead": b[19] == 0 or bool(state & 4),
                        "spent": bool(state & 2),
                        "items": [
                            int.from_bytes(b[30 + 2 * j : 32 + 2 * j], "little")
                            for j in range(5)
                        ],
                    }
                )
            actions = (
                self.legal(seat, s) if s["active"] == seat and not s["outcome"] else []
            )
            obs = {
                "match_id": self.id,
                "rules": RULES,
                "seat": seat,
                "active_seat": s["active"],
                "sequence": s["seq"],
                "state_hash": s["hash"],
                "round": s["round"],
                "outcome": s["outcome"],
                "your_turn": s["active"] == seat and not s["outcome"],
                "units": units,
                "terrain": [
                    list(bytes.fromhex(s["terrain"])[i * 15 : (i + 1) * 15])
                    for i in range(15)
                ],
                "legal_actions": actions,
            }
            self.event("observation", obs)
            return obs

    def act(self, seat, body):
        with self.lock:
            rid = body.get("request_id")
            if not isinstance(rid, str) or not rid or len(rid) > 128:
                return {"error": "request_id_required"}
            key = (seat, rid)
            encoded = json.dumps(body, sort_keys=True)
            if key in self.cache:
                old, result = self.cache[key]
                return (
                    result
                    if old == encoded
                    else {"error": "request_id_reused_with_different_action"}
                )
            if body.get("match_id") != self.id:
                return {"error": "wrong_match"}
            peers = self.stable()
            s = peers[seat]
            if s["outcome"]:
                return {"error": "match_finished", "outcome": s["outcome"]}
            if s["active"] != seat:
                return {"error": "not_your_turn"}
            if body.get("sequence") != s["seq"] or body.get("state_hash") != s["hash"]:
                return {"error": "stale_observation"}
            action = body.get("action_id")
            if action not in {a["id"] for a in self.legal(seat, s)}:
                return {"error": "illegal_action"}
            self.cache[key] = (encoded, {"error": "action_pending"})
            self.event("submitted", {"seat": seat, "request": body})
            try:
                reply = self.rpc(f"act {seat} {s['seq']+1} {s['hash']} {action}")
                if "error" in reply:
                    self.cache[key] = (encoded, reply)
                    return reply
                a, b = self.stable()
                if a["seq"] != s["seq"] + 1:
                    raise RuntimeError("Unexpected confirmed command sequence")
            except Exception as e:
                self.failed = "Match stopped after unconfirmed action: " + str(e)
                self.cache[key] = (
                    encoded,
                    {"error": "unconfirmed_action", "reason": self.failed},
                )
                self.event("failure", {"reason": self.failed})
                raise
            result = {
                "accepted": True,
                "sequence": a["seq"],
                "state_hash": a["hash"],
                "active_seat": a["active"],
                "outcome": a["outcome"],
            }
            self.cache[key] = (encoded, result)
            self.event("action", {"seat": seat, "request": body, "result": result})
            return result

    def close(self):
        if self.p.poll() is None:
            self.p.stdin.write("quit\n")
            self.p.stdin.flush()
            try:
                self.p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.p.kill()
                self.p.wait()
        self.event("closed", {"exit_code": self.p.returncode})


def serve(session, port):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            try:
                seat = session.keys.index(
                    self.headers.get("Authorization", "").removeprefix("Bearer ")
                )
            except ValueError:
                self.send_error(401)
                return
            try:
                size = int(self.headers.get("Content-Length", "0"))
                if size < 0 or size > 8192:
                    raise ValueError("request too large")
                body = json.loads(self.rfile.read(size) or b"{}")
                if self.path == "/observe":
                    result = session.observe(seat)
                elif self.path == "/act":
                    result = session.act(seat, body)
                else:
                    self.send_error(404)
                    return
                payload = json.dumps(result).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
            except Exception as e:
                session.event("failure", {"reason": str(e)})
                self.send_error(503, str(e))

    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    credentials = {
        "url": f"http://127.0.0.1:{server.server_port}",
        "match_id": session.id,
        "seats": [{"seat": i, "token": k} for i, k in enumerate(session.keys)],
    }

    for seat in credentials["seats"]:
        q = session.log / f"seat-{seat['seat']}.json"
        q.write_text(
            json.dumps({"url": credentials["url"], "match_id": session.id, **seat})
        )
        q.chmod(0o600)
    p = session.log / "credentials.json"
    p.write_text(json.dumps(credentials, indent=2))
    p.chmod(0o600)
    print(json.dumps({"credentials": str(p), "url": credentials["url"]}), flush=True)
    try:
        server.serve_forever()
    finally:
        server.server_close()
        session.close()


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=8768)
    p.add_argument("--red", action="store_true")
    a = p.parse_args()
    serve(Session(red=a.red), a.port)
