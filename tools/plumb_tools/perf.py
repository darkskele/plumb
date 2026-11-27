"""Profiles a case: perf record over one run of it in process, its samples kept only in its op,
inlined or called from it, and printed to the terminal."""

from __future__ import annotations

import bisect
import json
import math
import os
import re
import subprocess
import tempfile
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path

from plumb_tools import counts, project
from plumb_tools.results import ResultsError, read_results

# Events recorded unless told others, each generic to every Intel core type.
EVENTS = ("cycles", "instructions", "cache-misses", "branch-misses", "dTLB-load-misses")
# Samples a second for each event.
FREQUENCY = 10_000
# The prefix of every run_batch symbol, a case's own and its clones.
RUN_BATCH = "_ZN5plumb6detail9run_batchI"
# Rows each list prints.
LINES, CALLEES, INSTRUCTIONS = 10, 5, 10


@dataclass
class Frame:
    """A frame of a sample's call chain. `sym` is None where perf could not name it."""

    sym: str | None
    offset: int
    dso: str | None


@dataclass
class Sample:
    event: str
    period: int
    frames: list[Frame]


def event_name(raw: str) -> str:
    """An event as perf prints it, without its core type or modifiers."""
    raw = raw.rstrip(":")
    if "/" in raw:
        raw = raw.split("/")[1]
    return raw.split(":")[0]


_HEAD = re.compile(r"^\s*(\d+)\s+(\S+?):?\s*$")
_FRAME = re.compile(r"^\s*[0-9a-f]+ (\S+?)(?:\+0x([0-9a-f]+))? \((.*)\)$")


def parse_script(text: str) -> list[Sample]:
    """Samples from `perf script -F event,period,ip,sym,symoff,dso`."""
    out = []
    for block in re.split(r"\n\s*\n", text.strip()):
        rows = block.splitlines()
        if not rows or not (head := _HEAD.match(rows[0])):
            continue
        frames = []
        for row in rows[1:]:
            if m := _FRAME.match(row):
                sym, off, dso = m.groups()
                if sym == "[unknown]":
                    frames.append(Frame(None, 0, None))
                else:
                    frames.append(Frame(sym, int(off or "0", 16), dso))
        out.append(Sample(event_name(head.group(2)), int(head.group(1)), frames))
    return out


def parse_addr2line(text: str) -> dict[int, list[tuple[str, int]]]:
    """Each address's inline chain, innermost first, from `addr2line -a -i`."""
    out: dict[int, list[tuple[str, int]]] = {}
    chain: list[tuple[str, int]] = []
    for row in text.splitlines():
        if row.startswith("0x"):
            chain = out.setdefault(int(row, 16), [])
        elif ":" in row:
            path, _, line = row.split(" (")[0].rpartition(":")
            chain.append((path, int(line) if line.isdigit() else 0))
    return out


def op_line(chain: list[tuple[str, int]], source: Path) -> tuple[str, int] | None:
    """The innermost project line in an inline chain, None when it never reaches the project. A
    project file is under `source` and not one of plumb's headers."""
    root = str(source).rstrip("/") + "/"
    for path, line in chain:
        if path.startswith(root) and "/include/plumb/" not in path:
            return path, line
    return None


@dataclass
class Code:
    """A binary's run_batch symbols at their addresses, and each instruction in them: its text and
    the op's line it came from, None when not the op's."""

    symbols: dict[str, int]
    instructions: dict[int, tuple[str, tuple[str, int] | None]]
    _starts: list[int] = field(default_factory=list)

    def at(self, address: int) -> int | None:
        """The instruction holding `address`."""
        if not self._starts:
            self._starts = sorted(self.instructions)
        k = bisect.bisect_right(self._starts, address) - 1
        return self._starts[k] if k >= 0 else None


@dataclass
class Profile:
    """A case's op: each event's count, the cycles samples kept, and cycles by instruction, by op
    line and by function called from the op."""

    events: dict[str, int]
    samples: int
    instructions: dict[int, int]
    lines: dict[tuple[str, int], int]
    callees: dict[str, int]


def attribute(samples: list[Sample], code: Code) -> Profile:
    """The samples in the op. A sample counts when its own instruction is the op's, or when the
    nearest run_batch frame above it is a call in the op. Frames above the sample's own are the
    calls' own addresses."""
    p = Profile({}, 0, {}, {}, {})
    for s in samples:
        for depth, f in enumerate(s.frames):
            if f.sym is not None and f.sym.startswith(RUN_BATCH) and f.sym in code.symbols:
                break
        else:
            continue
        address = code.symbols[f.sym] + f.offset
        at = code.at(address)
        if at is None or code.instructions[at][1] is None:
            continue
        p.events[s.event] = p.events.get(s.event, 0) + s.period
        if s.event != "cycles":
            continue
        p.samples += 1
        line = code.instructions[at][1]
        p.instructions[at] = p.instructions.get(at, 0) + s.period
        p.lines[line] = p.lines.get(line, 0) + s.period
        if depth:
            leaf = s.frames[0].sym or "[kernel]"
            p.callees[leaf] = p.callees.get(leaf, 0) + s.period
    return p


def rates(p: Profile, instructions_per_op: float) -> dict[str, tuple[float, float]]:
    """Each event per op and per thousand instructions."""
    instructions = p.events.get("instructions", 0)
    out = {}
    for e, n in p.events.items():
        per_k = 1000 * n / instructions if instructions else math.nan
        out[e] = (per_k * instructions_per_op / 1000, per_k)
    return out


def _run(cmd: list[str]) -> str:
    try:
        done = subprocess.run(cmd, capture_output=True, text=True)
    except FileNotFoundError as e:
        raise ResultsError(f"{cmd[0]} not found") from e
    if done.returncode != 0:
        raise ResultsError(f"failed: {' '.join(cmd[:3])}: {done.stderr.strip()[:500]}")
    return done.stdout


def layout(binary: Path, source: Path) -> Code:
    """The run_batch symbols of `binary` and their instructions."""
    symbols, sizes = {}, {}
    for row in _run(["nm", "-S", "--defined-only", str(binary)]).splitlines():
        parts = row.split()
        if len(parts) == 4 and parts[3].startswith(RUN_BATCH):
            symbols[parts[3]] = int(parts[0], 16)
            sizes[parts[3]] = int(parts[1], 16)
    if not symbols:
        raise ResultsError(f"no run_batch in {binary}")
    text = {}
    for sym, start in symbols.items():
        dump = _run(["objdump", "-d", "-C", "--no-show-raw-insn", "-M", "intel",
                     f"--start-address={start:#x}", f"--stop-address={start + sizes[sym]:#x}",
                     str(binary)])
        for row in dump.splitlines():
            if m := re.match(r"^\s*([0-9a-f]+):\s+(.*)$", row):
                ins = re.sub(r"\s*<.*run_batch.*$", "", m.group(2))
                text[int(m.group(1), 16)] = re.sub(r"\s+", " ", ins).strip()
    chains = parse_addr2line(
        _run(["addr2line", "-a", "-i", "-e", str(binary), *(f"{a:#x}" for a in sorted(text))])
    )
    return Code(symbols, {a: (t, op_line(chains.get(a, []), source)) for a, t in text.items()})


def record(bench: project.Bench, case: str, events: list[str], frequency: int, out: Path,
           wrap: list[str]) -> str:
    """perf record over one run of `case` in process, returning perf script's text."""
    data = out / "perf.data"
    cmd = [*wrap, "perf", "record", "-q", "-F", str(frequency), "-e", ",".join(events),
           "--call-graph", "lbr", "-o", str(data), "--",
           str(bench.path), "--run", case, "--in-process", "--out", str(out / "run")]
    try:
        done = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    except FileNotFoundError as e:
        raise ResultsError(f"{cmd[0]} not found") from e
    if done.returncode != 0 or not data.exists():
        raise ResultsError(f"perf record of {case} failed: {done.stderr.strip()[-500:]}")
    return _run(["perf", "script", "-i", str(data), "-F", "event,period,ip,sym,symoff,dso",
                 "--no-inline", "--no-demangle"])


def demangle(names: list[str]) -> dict[str, str]:
    if not names:
        return {}
    shown = _run(["c++filt", *names]).splitlines()
    return dict(zip(names, shown))


@dataclass
class Shown:
    """What one profile printed, kept to show as the previous beside the next of the same case."""

    ns: float
    rates: dict[str, list[float]]
    lines: dict[str, float]
    callees: dict[str, float]


def _num(v: float) -> str:
    if not math.isfinite(v):
        return "n/a"
    if abs(v) >= 100:
        return f"{v:,.0f}"
    return f"{v:.1f}" if abs(v) >= 10 else f"{v:.2f}" if abs(v) >= 0.1 else f"{v:.3f}"


def _src(path: str, line: int, cache: dict[str, list[str]]) -> str:
    if path not in cache:
        try:
            cache[path] = Path(path).read_text().splitlines()
        except OSError:
            cache[path] = []
    rows = cache[path]
    return rows[line - 1].strip() if 0 < line <= len(rows) else ""


class _Paint:
    """ANSI styles, or none when `on` is false."""

    BOLD, DIM, CYAN, RED, GREEN, YELLOW = "1", "2", "36", "31", "32", "33"

    def __init__(self, on: bool):
        self.on = on

    def __call__(self, text: str, *styles: str) -> str:
        if not self.on or not styles or not text:
            return text
        return f"\x1b[{';'.join(styles)}m{text}\x1b[0m"


def wants_colour(stream) -> bool:
    """Whether `stream` is a terminal and NO_COLOR is unset."""
    return stream.isatty() and not os.environ.get("NO_COLOR")


# A change in an event per op past this share of the previous profile's is coloured.
CHANGE = 0.02
# Characters in a full share bar.
BAR = 16


def _bar(share: float) -> str:
    """A bar `share` of `BAR` long, in eighths."""
    eighths = round(max(0.0, min(1.0, share)) * BAR * 8)
    full, part = divmod(eighths, 8)
    return ("█" * full + (" ▏▎▍▌▋▊▉"[part] if part else "")).ljust(BAR)


def show(case: str, target: str, ns: float, ipo: float, p: Profile, code: Code, source: Path,
         seconds: float, last: Shown | None, colour: bool = False) -> tuple[str, Shown]:
    """The profile as terminal text, and what to keep of it."""
    paint = _Paint(colour)
    B, D, C = paint.BOLD, paint.DIM, paint.CYAN
    total = sum(p.instructions.values())
    share = (lambda n: n / total) if total else (lambda n: 0.0)
    r = rates(p, ipo)
    cache: dict[str, list[str]] = {}

    def rel(path: str) -> str:
        try:
            return str(Path(path).relative_to(source))
        except ValueError:
            return path

    def section(title: str) -> str:
        return "  " + paint(title.upper(), B)

    def hot(v: float, text: str) -> str:
        return paint(text, B, paint.YELLOW) if v >= 0.2 else paint(text, B) if v >= 0.05 else text

    facts = [
        (_num(ns), "ns per op at p50"),
        (_num(ipo), "instructions per op"),
        (f"{p.samples:,}", "samples in the op"),
        (f"{seconds:.1f}", "s"),
    ]
    out = [paint(f"━━ {case} ", B, C) + paint(target, D),
           "  " + paint(" · ", D).join(f"{paint(v, B)} {paint(label, D)}" for v, label in facts)]
    if last:
        out.append("  " + paint(f"previous profile {_num(last.ns)} ns per op at p50", D))
    out.append("")
    if not total:
        out += ["  " + paint("no sample landed in the op", paint.YELLOW), ""]
        return "\n".join(out), Shown(ns, {}, {}, {})

    out.append(section("events"))
    out.append(paint(f"  {'':<20}{'per op':>12}{'per 1k instr':>14}{'previous':>12}{'change':>9}", D))
    for e in [e for e in EVENTS if e in r] + sorted(e for e in r if e not in EVENTS):
        per_op, per_k = r[e]
        before = last.rates[e][0] if last and e in last.rates else math.nan
        change, tone = "", ()
        if math.isfinite(before) and before and math.isfinite(per_op):
            d = per_op / before - 1
            change = f"{d:+.1%}"
            tone = (paint.RED,) if d > CHANGE else (paint.GREEN,) if d < -CHANGE else (D,)
        out.append(
            f"  {e:<20}{paint(f'{_num(per_op):>12}', B)}{_num(per_k):>14}"
            f"{paint(f'{_num(before) if math.isfinite(before) else '':>12}', D)}"
            f"{paint(f'{change:>9}', *tone)}"
        )
    out.append("")

    def ranked(d: dict, n: int) -> list:
        return sorted(d.items(), key=lambda kv: -kv[1])[:n]

    def row(v: float, before: str | None, label: str, rest: str = "") -> str:
        kept = "" if before is None else paint(f"{before:>10}", D)
        return (f"  {paint(_bar(v), C)} {hot(v, f'{v:>6.1%}')}{kept}  "
                f"{label}{(' ' + rest) if rest else ''}")

    lines = {f"{rel(path)}:{line}": c for (path, line), c in p.lines.items()}
    width = max(len(k) for k in lines)
    def header(label: str, previous: bool = True) -> str:
        kept = f"{'previous':>10}" if previous else ""
        return paint(f"  {'share of cycles':<{BAR + 7}}{kept}  {label}", D)

    out.append(section("lines of the op"))
    out.append(header("line"))
    for (path, line), c in ranked(p.lines, LINES):
        key = f"{rel(path)}:{line}"
        before = f"{last.lines[key]:.1%}" if last and key in last.lines else ""
        out.append(row(share(c), before, paint(f"{key:<{width}}", C), _src(path, line, cache)))
    out.append("")

    names = demangle([n for n in p.callees if n.startswith("_Z")])
    if p.callees:
        out.append(section("called from the op"))
        out.append(header("function"))
        for name, c in ranked(p.callees, CALLEES):
            shown = names.get(name, name)
            before = f"{last.callees[shown]:.1%}" if last and shown in last.callees else ""
            out.append(row(share(c), before, shown[:100]))
        out.append("")

    out.append(section("hottest instructions"))
    out.append(header("address", previous=False))
    for at, c in ranked(p.instructions, INSTRUCTIONS):
        text, line = code.instructions[at]
        where = f"{Path(line[0]).name}:{line[1]}" if line else ""
        op, _, args = text.partition(" ")
        shown = paint(f"{op:<7}", B) + f" {args[:48]:<48}"
        out.append(row(share(c), None, paint(f"{at:>6x}", D), f"{shown} {paint(where, C)}"))
    out.append("")

    kept = Shown(
        ns,
        {e: list(v) for e, v in r.items()},
        {k: share(c) for k, c in lines.items()},
        {names.get(n, n): share(c) for n, c in p.callees.items()},
    )
    return "\n".join(out), kept


def profile(bench: project.Bench, case: str, events: list[str], frequency: int, source: Path,
            keep: Path, wrap: list[str], colour: bool = False) -> str:
    """Profiles `case` and returns what to print. Its profile is kept in `keep/<case>.json`."""
    start = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="plumb_perf_") as tmp:
        script = record(bench, case, events, frequency, Path(tmp), wrap)
        results = read_results(Path(tmp) / "run" / "results.bin")
    code = layout(bench.path, source)
    p = attribute(parse_script(script), code)
    ns, ipo = math.nan, math.nan
    if results.cases and results.cases[0].reps:
        c = results.cases[0]
        ns = c.reps[0].figures.p50
        if s := counts.summarise(c.reps, False):
            ipo = s.rates.instructions
    saved = keep / f"{case}.json"
    last = None
    if saved.exists():
        try:
            last = Shown(**json.loads(saved.read_text()))
        except (json.JSONDecodeError, TypeError):
            last = None
    text, kept = show(case, bench.target, ns, ipo, p, code, source, time.monotonic() - start, last,
                      colour)
    keep.mkdir(parents=True, exist_ok=True)
    saved.write_text(json.dumps(asdict(kept)))
    return text
