"""Reads a run's results.bin by the schema it carries, so its fields are named as plumb names them."""

from __future__ import annotations

import struct
from pathlib import Path
from types import SimpleNamespace

MAGIC = b"plumbres"
VERSION = 6

_SCALARS = {
    "bool": "?",
    "char": "c",
    "i8": "b",
    "u8": "B",
    "i16": "h",
    "u16": "H",
    "i32": "i",
    "u32": "I",
    "i64": "q",
    "u64": "Q",
    "f64": "d",
}


class ResultsError(ValueError):
    """A results file that cannot be read, and why."""


class Record(SimpleNamespace):
    """A struct read from a results file, its fields as attributes."""


class _In:
    """Bytes read in order by a schema. Reading past the end raises truncated."""

    def __init__(self, data: bytes, where: str):
        self.data = data
        self.at = 0
        self.where = where
        self.structs: dict[str, list[tuple[str, str]]] = {}

    def fail(self, why: str) -> ResultsError:
        return ResultsError(f"{self.where}: {why}")

    def one(self, fmt: str):
        s = struct.Struct("<" + fmt)
        if len(self.data) - self.at < s.size:
            raise self.fail("truncated")
        (v,) = s.unpack_from(self.data, self.at)
        self.at += s.size
        return v

    def length(self) -> int:
        n = self.one("Q")
        if n > len(self.data) - self.at:
            raise self.fail("truncated")
        return n

    def raw(self, n: int) -> bytes:
        if len(self.data) - self.at < n:
            raise self.fail("truncated")
        b = self.data[self.at : self.at + n]
        self.at += n
        return b

    def schema(self) -> str:
        """Reads the schema, a line per struct as `Name: field type, field type`, and returns the
        first struct's name."""
        root = None
        for line in self.raw(self.length()).decode().splitlines():
            name, sep, body = line.partition(":")
            if not sep:
                raise self.fail(f"bad schema line {line!r}")
            fields = [tuple(f.split(" ")) for f in body.strip().split(", ")] if body.strip() else []
            if any(len(f) != 2 for f in fields):
                raise self.fail(f"bad schema line {line!r}")
            self.structs[name] = fields
            root = root or name
        if root is None:
            raise self.fail("empty schema")
        return root

    def value(self, t: str):
        if t in _SCALARS:
            return self.one(_SCALARS[t])
        if t == "str":
            return self.raw(self.length()).decode()
        if t.startswith("list<") and t.endswith(">"):
            return [self.value(t[5:-1]) for _ in range(self.length())]
        if t.endswith("]"):
            inner, _, n = t[:-1].rpartition("[")
            if inner == "char":
                return self.raw(int(n))
            return [self.value(inner) for _ in range(int(n))]
        if t not in self.structs:
            raise self.fail(f"unknown type {t}")
        return Record(**{name: self.value(ft) for name, ft in self.structs[t]})


def parse_results(data: bytes, where: str) -> Record:
    """The results in `data`, read from `where`, refusing data that is damaged or of another
    version."""
    i = _In(data, where)
    if data[:8] != MAGIC:
        raise i.fail("not a plumb results file")
    i.at = 8
    if (v := i.one("I")) != VERSION:
        raise i.fail(f"results version {v}, expected {VERSION}")
    r = i.value(i.schema())
    if i.at != len(data):
        raise i.fail("trailing bytes")
    return r


def read_results(path: Path | str) -> Record:
    """The results at `path`."""
    try:
        data = Path(path).read_bytes()
    except OSError as e:
        raise ResultsError(f"cannot open {path}: {e.strerror}") from e
    return parse_results(data, str(path))
