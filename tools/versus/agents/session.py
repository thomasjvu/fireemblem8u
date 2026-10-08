#!/usr/bin/env python3
"""Two isolated linked GBA cores, with serialized agent observations/actions."""

from rules import describe
from pathlib import Path
import os
import sys
import json, subprocess, time, threading, uuid, struct, secrets, argparse, hashlib
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

ROOT = Path(__file__).resolve().parents[3]


class Session:
    def __init__(
        self,
        red=False,
        evidence=None,
        map_id=0,
        blue_party=0,
        red_party=0,
        objective=0,
        peer_map=None,
        peer_choices=None,
        video=False,
        desktop=False,
        human_seats=0,
        reuse=None,
    ):
        self.owns_process = True
        self.desktop = desktop
        self.human_seats = human_seats
        self.lock = threading.RLock()
        self.id = uuid.uuid4().hex
        self.keys = [secrets.token_urlsafe(24) for _ in range(2)]
        self.cache = {}
        self.legal_cache = {}
        self.failed = None
        self.log = Path(evidence or ROOT / "build/versus/agent-matches" / self.id)
        self.log.mkdir(parents=True, exist_ok=False)
        self.emulator_log = reuse.emulator_log if reuse is not None else self.log / "emulator.log"
        manifest = json.loads((ROOT / "build/versus/manifest.json").read_text())
        self.manifest = manifest
        catalog_bytes = (ROOT / "build/versus/catalog.json").read_bytes()
        if hashlib.sha256(catalog_bytes).hexdigest() != manifest["catalog_sha256"]:
            raise RuntimeError("Scenario catalog does not match ROM build")
        self.catalog = json.loads(catalog_bytes)
        if self.catalog != json.loads((ROOT / "versus/catalog.json").read_text()):
            raise RuntimeError(
                "Scenario catalog changed; rebuild the ROM before starting a match"
            )
        counts = [
            len(self.catalog[k]) for k in ["maps", "parties", "parties", "objectives"]
        ]
        choices = [map_id, blue_party, red_party, objective]
        if any(
            type(v) is not int or v not in range(n) for v, n in zip(choices, counts)
        ):
            raise ValueError("Unknown map, party, or objective")
        self.choices = choices
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
        binary = ROOT / "build/versus" / ("agent-desktop" if desktop else "agent-bridge")
        if desktop and sys.platform == "darwin" and os.environ.get("SDL_VIDEODRIVER") != "dummy":
            binary = ROOT / "build/versus/Fire Emblem Versus.app/Contents/MacOS/agent-desktop"
        if not binary.exists():
            raise RuntimeError("Build the native frontend with tools/manage.py desktop first")
        args = [
            str(binary),
            str(ROOT / "build/versus/fire-emblem-versus.gba"),
            symbols["gBmMapTerrain"],
            manifest["symbols"]["VersusOptions"].removeprefix("0x"),
            str(int(red)),
            *map(str, choices),
        ]
        if peer_choices is not None:
            if len(peer_choices) != 4 or any(
                type(v) is not int or v not in range(n)
                for v, n in zip(peer_choices, counts)
            ):
                raise ValueError("Unknown peer setup")
            args.extend(map(str, peer_choices))
        elif peer_map is not None:
            args.append(str(peer_map))
        frames = self.log / "frames"
        if video:
            frames.mkdir()
        if reuse is not None:
            if not desktop or not reuse.desktop or reuse.p.poll() is not None:
                raise RuntimeError("Only a live native frontend can be reused")
            if video or reuse.choices is None:
                raise RuntimeError("Persistent native frontend does not support legacy screenshot export")
            if reuse.manifest['rom_sha256'] != self.manifest['rom_sha256']:
                raise RuntimeError("Cannot reuse a frontend for a different ROM")
            self.p = reuse.p
            reuse.event("frontend_transferred", {"next_match": self.id})
            reuse.owns_process = False
            ready = self.rpc("reset 0 " + " ".join(map(str, [int(red), *choices, human_seats])))
            if not ready.get('ready'):
                self.close()
                raise RuntimeError("Native frontend reset failed")
        else:
            self.p = subprocess.Popen(
                args,
                cwd=ROOT,
                env={
                    **os.environ,
                    "VERSUS_FRAME_DIRECTORY": str(frames.resolve()) if video else "",
                    "VERSUS_HUMAN_SEATS": str(human_seats),
                },
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=self.emulator_log.open("w"),
                text=True,
                bufsize=1,
            )
            startup = []
            reader = threading.Thread(target=lambda: startup.append(self.p.stdout.readline()), daemon=True)
            reader.start()
            reader.join(10)
            if reader.is_alive():
                self.p.kill()
                try:
                    self.p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
                raise RuntimeError("Emulator frontend startup timed out; inspect " + str(self.emulator_log))
            ready = startup[0]
            if not ready:
                self.p.wait(timeout=10)
                raise RuntimeError("Emulator frontend failed to start; inspect " + str(self.emulator_log))
            if not json.loads(ready).get("ready"):
                raise RuntimeError("Emulator bridge did not become ready")
        self.event(
            "start",
            {
                "match": self.id,
                "manifest": manifest,
                "rules": describe(self.catalog, self.choices),
                "red_opener": red,
            },
        )
        try:
            self.stable()
        except Exception:
            self.close()
            raise

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
                "Emulator exited; inspect " + str(self.emulator_log)
            )
        return json.loads(result)

    def stable(self, deadline=45):
        if self.failed:
            raise RuntimeError(self.failed)
        end = time.monotonic() + deadline
        report_at = time.monotonic() + 15
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
                    "options",
                ]
            ):
                return a, b
            if time.monotonic() >= report_at:
                self.event("confirmation_delayed", {"blue": a, "red": b})
                report_at = end + 1
            time.sleep(0.005)
        self.event("unconfirmed_peers", {"blue": a, "red": b})
        raise TimeoutError(
            "Peers did not reach the same confirmed state: "
            + repr(
                [
                    {
                        k: p[k]
                        for k in [
                            "seq",
                            "hash",
                            "state",
                            "active",
                            "error",
                            "outcome",
                            "rng",
                            "options",
                        ]
                    }
                    for p in [a, b]
                ]
            )
        )

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
                            17: "seize",
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
            options = bytes.fromhex(s["options"])
            selected = list(options[4:8])
            if selected != self.choices:
                raise RuntimeError("ROM selection differs from requested rules")
            rules = describe(self.catalog, selected)
            for n in range(10):
                b = raw[n * 72 : (n + 1) * 72]
                state = int.from_bytes(b[12:16], "little")
                units.append(
                    {
                        "id": (0 if n < 5 else 128) + n % 5 + 1,
                        "seat": n // 5,
                        "level": b[8],
                        "role": self.catalog["parties"][selected[1 + n // 5]]["units"][
                            n % 5
                        ]["role"],
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
                "rules": rules,
                "victory_reason": {
                    0: None,
                    1: "elimination",
                    2: "seizure",
                    3: "round_limit",
                    4: "no_defenders",
                    5: "surrender",
                }[options[8]],
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

    def human_action(self, seat, sequence, stop=None):
        """Wait for an ordinary map-menu action, then require linked confirmation.

        While a human moves the cursor or opens menus, transient unit buffers may
        differ. Poll sequence only; do not request legal commands or peer equality
        until the ROM has committed the human action.
        """
        if not self.desktop or not (self.human_seats & (1 << seat)):
            raise RuntimeError("Native human input requires a desktop human seat")
        with self.lock:
            response = self.rpc(f"control {seat} 1")
            if not response.get("accepted"):
                raise RuntimeError("Could not enable human input")
            self.event("human_input_enabled", {"seat": seat, "sequence": sequence})
        try:
            while stop is None or not stop.is_set():
                with self.lock:
                    snapshot = self.rpc(f"observe {seat}")
                if snapshot.get("error"):
                    raise RuntimeError("ROM link failed during human turn")
                if snapshot["seq"] > sequence:
                    with self.lock:
                        self.rpc(f"control {seat} 0")
                        peers = self.stable()
                    if peers[0]["seq"] != sequence + 1:
                        raise RuntimeError("Unexpected human command sequence")
                    self.event("human_action", {"seat": seat, "sequence": peers[0]["seq"],
                                                "state_hash": peers[0]["hash"]})
                    return {"accepted": True, "sequence": peers[0]["seq"]}
                time.sleep(0.025)
            raise RuntimeError("Match stopped during human turn")
        finally:
            if self.p.poll() is None:
                with self.lock:
                    self.rpc(f"control {seat} 0")

    def close(self):
        if not self.owns_process:
            return
        if self.p.poll() is None:
            try:
                self.p.stdin.write("quit\n")
                self.p.stdin.flush()
            except BrokenPipeError:
                pass
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
    p.add_argument("--desktop", action="store_true", help="Show the native emulator window for agent API play")
    p.add_argument("--hotseat", action="store_true", help="Native two-human play without an HTTP server")
    catalog = json.loads((ROOT / "versus/catalog.json").read_text())
    p.add_argument(
        "--map",
        choices=[m["id"] for m in catalog["maps"]],
        default=catalog["maps"][0]["id"],
    )
    p.add_argument(
        "--blue-party",
        choices=[m["id"] for m in catalog["parties"]],
        default="balanced",
    )
    p.add_argument(
        "--red-party", choices=[m["id"] for m in catalog["parties"]], default="balanced"
    )
    p.add_argument("--objective", choices=catalog["objectives"], default="elimination")
    a = p.parse_args()
    maps = [m["id"] for m in catalog["maps"]]
    parties = [m["id"] for m in catalog["parties"]]
    session = Session(
            red=a.red,
            map_id=maps.index(a.map),
            blue_party=parties.index(a.blue_party),
            red_party=parties.index(a.red_party),
            objective=catalog["objectives"].index(a.objective),
            desktop=a.desktop or a.hotseat,
            human_seats=3 if a.hotseat else 0,
        )
    if a.hotseat:
        try:
            while session.p.poll() is None:
                state = session.stable()[0]
                if state['outcome']:
                    time.sleep(.25)
                else:
                    session.human_action(state['active'], state['seq'])
        except KeyboardInterrupt:
            pass
        finally:
            session.close()
    else:
        serve(session, a.port)
