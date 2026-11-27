from pathlib import Path

import pytest

from plumb_tools import perf

SRC = Path("/src")
RB = "_ZN5plumb6detail9run_batchIN1F1OEEdRT_m"

SCRIPT = f"""\
    1000 cpu_atom/cycles/:
\t    5555000010 {RB}+0x10 (/src/build/bench)
\t    5555000900 _ZN5plumb6detail9run_entryE+0x40 (/src/build/bench)

    2000 cpu_atom/cycles/:
\t    5555000014 {RB}+0x14 (/src/build/bench)

    500 cpu_atom/cycles/:
\t    5555000004 {RB}+0x4 (/src/build/bench)

    700 cpu_atom/cycles/:
\t    7000000001 memcpy+0x1 (/usr/lib/libc.so.6)
\t    5555000019 {RB}+0x16 (/src/build/bench)

    300 cpu_atom/cycles/:
\tffffffffbb5eede0 [unknown] ([unknown])
\t    5555000019 {RB}+0x16 (/src/build/bench)

    900 cpu_atom/cycles/:
\t    5555000800 _ZN5plumb6detail9run_entryE+0x400 (/src/build/bench)

    40 cpu_atom/instructions/:
\t    5555000010 {RB}+0x10 (/src/build/bench)

    3 cache-misses:
\t    5555000014 {RB}+0x14 (/src/build/bench)
"""


def test_samples_parse_event_period_and_frames():
    s = perf.parse_script(SCRIPT)
    assert len(s) == 8
    assert (s[0].event, s[0].period) == ("cycles", 1000)
    assert s[0].frames[0] == perf.Frame(RB, 0x10, "/src/build/bench")
    assert s[4].frames[0] == perf.Frame(None, 0, None)
    assert s[7].event == "cache-misses"


def test_event_names_lose_their_core_type():
    assert perf.event_name("cpu_core/branch-misses/") == "branch-misses"
    assert perf.event_name("cycles:u") == "cycles"
    assert perf.event_name("cpu_atom/cycles/P") == "cycles"


# Instructions of run_batch at 0x100: a stamp at +0x4, the op's lines at +0x10 and +0x14, the loop
# at +0x18, and a call in the op at +0x16.
ADDR2LINE = """\
0x0000000000000104
/usr/lib/gcc/x86_64-linux-gnu/16/include/ia32intrin.h:124
/src/include/plumb/clock.hpp:27
/src/include/plumb/batch.hpp:46
0x0000000000000110
/src/demo/demo.cpp:73 (discriminator 1)
/src/include/plumb/batch.hpp:47 (discriminator 1)
0x0000000000000114
/usr/include/c++/16/bits/stl_vector.h:1109
/src/demo/demo.cpp:75
/src/include/plumb/batch.hpp:47
0x0000000000000116
/src/demo/demo.cpp:76
/src/include/plumb/batch.hpp:47
0x0000000000000118
/src/include/plumb/batch.hpp:47 (discriminator 2)
"""


def test_an_instruction_is_the_ops_when_its_chain_reaches_a_project_file():
    lines = perf.parse_addr2line(ADDR2LINE)
    got = {a: perf.op_line(chain, SRC) for a, chain in lines.items()}
    assert got == {
        0x104: None,
        0x110: ("/src/demo/demo.cpp", 73),
        0x114: ("/src/demo/demo.cpp", 75),
        0x116: ("/src/demo/demo.cpp", 76),
        0x118: None,
    }


def test_a_file_outside_the_source_is_not_the_projects():
    assert perf.op_line([("/elsewhere/x.cpp", 3)], SRC) is None


@pytest.fixture
def code():
    lines = perf.parse_addr2line(ADDR2LINE)
    text = {0x104: "rdtscp", 0x110: "xor rax,rdx", 0x114: "mov rbx,[rdx]", 0x116: "call memcpy",
            0x118: "add rcx,0x1"}
    return perf.Code({RB: 0x100}, {a: (text[a], perf.op_line(c, SRC)) for a, c in lines.items()})


def test_only_samples_in_the_op_or_called_from_it_count(code):
    p = perf.attribute(perf.parse_script(SCRIPT), code)
    assert p.events == {"cycles": 4000, "instructions": 40, "cache-misses": 3}
    assert p.samples == 4
    assert p.instructions == {0x110: 1000, 0x114: 2000, 0x116: 1000}
    assert p.lines == {("/src/demo/demo.cpp", 73): 1000, ("/src/demo/demo.cpp", 75): 2000,
                       ("/src/demo/demo.cpp", 76): 1000}
    assert p.callees == {"memcpy": 700, "[kernel]": 300}


def test_rates_are_per_thousand_instructions_and_per_op():
    p = perf.Profile({"cycles": 3000, "instructions": 2000, "cache-misses": 10}, 3, {}, {}, {})
    r = perf.rates(p, instructions_per_op=200)
    assert r["cycles"] == (pytest.approx(300.0), pytest.approx(1500.0))
    assert r["cache-misses"] == (pytest.approx(1.0), pytest.approx(5.0))


def test_rates_without_instructions_per_op_leave_it_unknown():
    p = perf.Profile({"cycles": 3000, "instructions": 2000}, 3, {}, {}, {})
    per_op, per_k = perf.rates(p, instructions_per_op=float("nan"))["cycles"]
    assert per_op != per_op and per_k == pytest.approx(1500.0)


def shown(code, colour=False, last=None):
    p = perf.attribute(perf.parse_script(SCRIPT), code)
    return perf.show("push", "bench", 25.0, 200.0, p, code, SRC, 1.0, last, colour)


def test_the_profile_without_colour_has_no_escapes(code):
    text, _ = shown(code)
    assert "\x1b[" not in text
    assert text.splitlines()[0] == "━━ push bench"
    assert "EVENTS" in text and "LINES OF THE OP" in text and "CALLED FROM THE OP" in text


def test_the_profile_in_colour_paints_it(code):
    text, _ = shown(code, colour=True)
    assert "\x1b[1;36m━━ push \x1b[0m" in text


def test_each_event_shows_its_change_from_the_previous_profile(code):
    _, kept = shown(code)
    kept.rates["cycles"][0] *= 2
    text, _ = shown(code, last=kept)
    row = next(l for l in text.splitlines() if l.strip().startswith("cycles"))
    assert row.split()[-1] == "-50.0%"


def test_a_share_bar_fills_in_eighths():
    assert perf._bar(1.0) == "█" * perf.BAR
    assert perf._bar(0.5 + 1 / (perf.BAR * 8)) == ("█" * 8 + "▏").ljust(perf.BAR)
    assert perf._bar(0.0) == " " * perf.BAR


def row_of(text, event):
    return next(l for l in text.splitlines() if l.strip().startswith(event))


def test_every_event_states_its_change_however_small(code):
    p = perf.attribute(perf.parse_script(SCRIPT), code)
    p.events["cache-misses"] = 1
    _, kept = perf.show("push", "bench", 25.0, 200.0, p, code, SRC, 1.0, None)
    kept.rates["cache-misses"][0] *= 2
    text, _ = perf.show("push", "bench", 25.0, 200.0, p, code, SRC, 1.0, kept)
    assert row_of(text, "cache-misses").split()[-1] == "-50.0%"


def test_a_change_past_two_percent_is_coloured_and_one_within_is_dim(code):
    _, kept = shown(code)
    kept.rates["cycles"][0] *= 2
    kept.rates["instructions"][0] *= 1.01
    text, _ = shown(code, colour=True, last=kept)
    assert "\x1b[32m" in row_of(text, "cycles")
    assert row_of(text, "instructions").endswith("\x1b[2m    -1.0%\x1b[0m")
