#!/usr/bin/env python3
"""
batchstart instancestress test / diagnosticscript.

example:
    # concurrently start 8 instances (random interval 100~600ms), observe 60s, end after cleanup
    python3 test-batch-instance-start.py --count 8

    # specified template and max observation duration
    python3 test-batch-instance-start.py --count 10 --template endpoint-control-1 --observe 90

    # keep instance, do not clean up (debug use)
    python3 test-batch-instance-start.py --count 5 --no-cleanup

output:
    - each instance's status timeline (Unknown → Init → Standby → Running...)
    - exceed observe duration not yet reached Running instancewill be marked [STUCK]
    - final statistics: running / stuck / total
"""

from __future__ import annotations

import argparse
import random
import sys
import time
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set

import requests

DEFAULT_FRAMEWORK = "http://localhost:8080"
TERMINAL_STATES = {"Terminated", "Error"}


@dataclass
class InstanceTrack:
    instance_id: str
    template: str
    submit_ts: float
    first_seen_ts: Optional[float] = None
    standby_ts: Optional[float] = None
    running_ts: Optional[float] = None
    timeline: List[tuple[float, str]] = field(default_factory=list)
    last_state: str = ""

    def record(self, now: float, state: str) -> None:
        if state == self.last_state:
            return
        if self.first_seen_ts is None:
            self.first_seen_ts = now
        if state == "Standby" and self.standby_ts is None:
            self.standby_ts = now
        if state == "Running" and self.running_ts is None:
            self.running_ts = now
        self.timeline.append((now - self.submit_ts, state))
        self.last_state = state


def post_instance(base: str, template: str) -> Optional[str]:
    """to /api/instance sendstartrequest, return task_id.frameworkside willsynca taskId,
    but the real instance_id we rely on /api/instance listto fetch the latestnewthata."""
    try:
        resp = requests.post(
            f"{base}/api/instance",
            json={"template": template, "start": True, "connect": True},
            timeout=5,
        )
    except Exception as e:
        print(f"[ERR ] POST /api/instance failed: {e}", file=sys.stderr)
        return None
    if resp.status_code != 200:
        print(f"[ERR ] POST /api/instance -> {resp.status_code} {resp.text}", file=sys.stderr)
        return None
    data = resp.json()
    return data.get("taskId")


def list_instances(base: str) -> List[dict]:
    try:
        resp = requests.get(f"{base}/api/instance", timeout=5)
    except Exception as e:
        print(f"[WARN] GET /api/instance failed: {e}", file=sys.stderr)
        return []
    if resp.status_code != 200:
        return []
    return resp.json()


def delete_instance(base: str, instance_id: str) -> None:
    try:
        requests.delete(f"{base}/api/instance/{instance_id}", timeout=5)
    except Exception:
        pass


def fmt_duration(a: Optional[float], b: Optional[float]) -> str:
    if a is None or b is None:
        return "   -   "
    return f"{b - a:6.2f}s"


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Batch instance start stress test",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    ap.add_argument("--framework", default=DEFAULT_FRAMEWORK, help="Framework base URL")
    ap.add_argument("--template", default="endpoint-control-1", help="CR template name")
    ap.add_argument("--count", type=int, default=8, help="How many instances to start")
    ap.add_argument("--jitter-ms", nargs=2, type=int, default=[100, 600],
                    metavar=("MIN", "MAX"), help="Random delay between starts in ms")
    ap.add_argument("--observe", type=int, default=60, help="Observation window in seconds")
    ap.add_argument("--poll-ms", type=int, default=500, help="Poll interval in ms")
    ap.add_argument("--no-cleanup", action="store_true", help="Keep instances after the run")
    ap.add_argument("--seed", type=int, default=None, help="Optional random seed")
    args = ap.parse_args()

    if args.seed is not None:
        random.seed(args.seed)

    base = args.framework.rstrip("/")
    print(f"framework={base} template={args.template} count={args.count}")
    print(f"jitter={args.jitter_ms[0]}~{args.jitter_ms[1]}ms observe={args.observe}s")
    print("=" * 70)

    # 1. first record existing instance_id; dispatch start, then identify new instances via diff
    pre_ids: Set[str] = {it["instance_id"] for it in list_instances(base)}

    tracks: Dict[str, InstanceTrack] = {}
    t0 = time.time()

    # 2. start at random intervals; for each request, spawn once, then list instances and locate the new one via diff id
    for i in range(args.count):
        task_id = post_instance(base, args.template)
        ts = time.time() - t0
        time.sleep(0.15) # etc. framework writes AbilityInstance row, otherwise the diff cannot be obtained
        now_ids = {it["instance_id"] for it in list_instances(base)}
        new_ids = now_ids - pre_ids - tracks.keys()
        if len(new_ids) != 1:
            print(f"[{ts:6.2f}] #{i+1:02d} started task={task_id} "
                  f"(instance mapping ambiguous, new_ids={list(new_ids)})")
        for nid in new_ids:
            tracks[nid] = InstanceTrack(
                instance_id=nid, template=args.template, submit_ts=time.time()
            )
            print(f"[{ts:6.2f}] #{i+1:02d} started task={task_id} instance={nid[:8]}")
        if i < args.count - 1:
            delay = random.uniform(args.jitter_ms[0], args.jitter_ms[1]) / 1000.0
            time.sleep(delay)

    # 3. poll within observation windowstatus
    print("-" * 70)
    print("observephase:")
    end_ts = time.time() + args.observe
    while time.time() < end_ts:
        for info in list_instances(base):
            iid = info["instance_id"]
            if iid not in tracks:
                continue
            tracks[iid].record(time.time(), info.get("state", "Unknown"))
        # all tracked instances enter Running or terminate state, then end early
        done = all(
            t.running_ts is not None or t.last_state in TERMINAL_STATES for t in tracks.values()
        )
        if done:
            print(f"all {len(tracks)} instances settled, stop early")
            break
        time.sleep(args.poll_ms / 1000.0)

    # 4. summary report
    print("=" * 70)
    print("each instance's timeline (relative to submission time):")
    stuck = []
    running_count = 0
    for iid, t in tracks.items():
        label = f"[STUCK]" if t.running_ts is None else "[ OK  ]"
        if t.running_ts is None:
            stuck.append(iid)
        else:
            running_count += 1
        timeline_str = "  ".join(f"+{dt:5.2f}s {s}" for dt, s in t.timeline)
        submit_to_standby = fmt_duration(t.submit_ts, t.standby_ts)
        submit_to_running = fmt_duration(t.submit_ts, t.running_ts)
        print(f"{label} {iid[:8]}  →Standby {submit_to_standby}  →Running {submit_to_running}")
        print(f"          timeline: {timeline_str}")

    print("-" * 70)
    print(f"total: running={running_count}/{len(tracks)} stuck={len(stuck)}")
    if stuck:
        print("  stuck instances (can be manually terminated via DELETE /api/instance/<id>):")
        for sid in stuck:
            print("   -", sid)

    # 5. statusratio statistics
    counts: Dict[str, int] = defaultdict(int)
    for t in tracks.values():
        counts[t.last_state or "NoHeartbeat"] += 1
    print("  state breakdown:", dict(counts))

    # 6. cleanup
    if not args.no_cleanup:
        print("-" * 70)
        print("cleanupinstance...")
        for iid in tracks.keys():
            delete_instance(base, iid)
        print(f"deleted {len(tracks)} instances")

    return 0 if not stuck else 1


if __name__ == "__main__":
    sys.exit(main())
