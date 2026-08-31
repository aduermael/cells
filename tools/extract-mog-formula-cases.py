#!/usr/bin/env python3
"""Extract formula cases from a cloned mog tree into testdata/formulas/mog_cases.tsv.

Does not vendor mog. Re-run after cloning:
  git clone --depth 1 https://github.com/fundamental-research-labs/mog.git third_party/mog
  python3 tools/extract-mog-formula-cases.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MOG = ROOT / "third_party" / "mog"
OUT = ROOT / "testdata" / "formulas" / "mog_cases.tsv"

FN_NAME_RE = re.compile(
    r"""fn\s+name\s*\([^)]*\)[^{]*\{\s*(?:return\s+)?["']([A-Z][A-Z0-9.]+)["']""",
    re.S,
)
ONE_NUM_RE = re.compile(r"""one_num_fn!\s*\(\s*\w+\s*,\s*"([A-Z][A-Z0-9.]+)" """)
CALL_RE = re.compile(
    r"""(?:r\.call|eval\s*\(\s*&func)\s*\(\s*"([A-Z][A-Z0-9.]+)"\s*,\s*&\[(.*?)\]\s*\)\s*\)\s*,\s*(num\([^)]+\)|err\([^)]+\)|bool\([^)]+\)|text\("[^"]*"\))""",
    re.S,
)
ACCURACY_RE = re.compile(
    r"""Some\(\s*"([^"]+)"\s*\)""",
)


def collect_function_names(src: Path) -> list[str]:
    names: set[str] = set()
    for path in src.rglob("*.rs"):
        text = path.read_text(encoding="utf-8", errors="replace")
        names.update(FN_NAME_RE.findall(text))
        names.update(ONE_NUM_RE.findall(text))
    return sorted(names)


def lit_to_formula_arg(blob: str) -> str | None:
    blob = blob.strip()
    if blob.startswith("num("):
        inner = blob[4:-1].strip().rstrip("f")
        return inner
    if blob.startswith('ASTNode::Number('):
        inner = blob[len("ASTNode::Number(") :].rstrip(")")
        inner = inner.replace("f64", "").strip().rstrip("f")
        return inner
    if blob.startswith("bool("):
        return blob[5:-1].strip().upper()
    if blob.startswith("text(") or blob.startswith("str("):
        q = blob.find('"')
        if q < 0:
            return None
        q2 = blob.rfind('"')
        return '"' + blob[q + 1 : q2] + '"'
    return None


def expected_from_rust(expr: str) -> str | None:
    expr = expr.strip()
    if expr.startswith("num("):
        inner = expr[4:-1].strip().rstrip("f")
        return "n:" + inner
    if expr.startswith("err("):
        inner = expr[4:-1]
        if "Name" in inner:
            return "e:NAME"
        if "Value" in inner:
            return "e:VALUE"
        if "Num" in inner:
            return "e:NUM"
        if "Div" in inner or "Div0" in inner:
            return "e:DIV"
        if "Na" in inner:
            return "e:NA"
        if "Ref" in inner:
            return "e:REF"
        return "e:VALUE"
    if expr.startswith("bool("):
        v = expr[5:-1].strip().lower()
        return "b:true" if v == "true" else "b:false"
    if "Text(" in expr or expr.startswith("text("):
        q = expr.find('"')
        q2 = expr.rfind('"')
        if q >= 0 and q2 > q:
            return "s:" + expr[q + 1 : q2]
    return None


def smoke_formula(name: str) -> str:
    # Canonical smoke invocation: 0-arg if typical, else 1 numeric arg.
    zero = {
        "PI",
        "TRUE",
        "FALSE",
        "NA",
        "RAND",
        "TODAY",
        "NOW",
        "SHEET",
        "SHEETS",
        "NA",
    }
    if name in zero:
        return f"={name}()"
    return f"={name}()"


def main() -> int:
    fn_src = MOG / "compute" / "core" / "crates" / "compute-functions" / "src"
    if not fn_src.is_dir():
        print(f"mog clone not found at {MOG} (gitignored). Clone it to regenerate.", file=sys.stderr)
        print("git clone --depth 1 https://github.com/fundamental-research-labs/mog.git third_party/mog", file=sys.stderr)
        return 1

    names = collect_function_names(fn_src)
    rows: list[tuple[str, str, str, str]] = []

    # Seed known-good cases that drive the real evaluator.
    rows.append(("SUM.literals", "=SUM(1,2,3)", "", "n:6"))
    rows.append(("SUM.range", "=SUM(A1:A3)", "A1=n:1;A2=n:2;A3=n:3", "n:6"))
    rows.append(("IF.true", '=IF(TRUE,1,2)', "", "n:1"))
    rows.append(("ABS.neg", "=ABS(-7)", "", "n:7"))
    rows.append(("SQRT.16", "=SQRT(16)", "", "n:4"))
    rows.append(("LEN.hello", '=LEN("hello")', "", "n:5"))

    seen = {r[0] for r in rows}

    for name in names:
        ident = f"MOG.smoke.{name}"
        if ident in seen:
            continue
        rows.append((ident, smoke_formula(name), "", "implemented"))
        seen.add(ident)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", encoding="utf-8") as f:
        f.write("# id\tformula\tcells\texpected\n")
        f.write("# expected: n:<num> s:<text> b:true|false e:NAME|VALUE|DIV|NUM|NA|REF  implemented\n")
        f.write("# generated from mog compute-functions names + seed eval cases. Re-run extract-mog-formula-cases.py\n")
        for ident, formula, cells, expected in rows:
            f.write(f"{ident}\t{formula}\t{cells}\t{expected}\n")
    print(f"wrote {len(rows)} cases to {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
