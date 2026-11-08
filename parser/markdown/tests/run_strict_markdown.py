#!/usr/bin/env python3
"""One strict parser-owner CTest for the original immutable CommonMark/GFM gates.

Required test-only modules: cmarkgfm and mistune; missing packages fail fast.
No runtime Python parser, no alternate production Markdown grammar.
"""
from pathlib import Path
import subprocess
import sys

def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: run_strict_markdown.py html-dump ast-dump output-dir")
    html, ast = (Path(x).resolve() for x in sys.argv[1:3])
    output = Path(sys.argv[3]).resolve()
    source = Path(__file__).resolve().parent
    spec = source / "spec.txt"
    output.mkdir(parents=True, exist_ok=True)
    for dep in ("cmarkgfm", "mistune"):
        __import__(dep)
    def check(script: str, *args: str) -> None:
        print("GATE:", script, flush=True)
        subprocess.run(
            [sys.executable, str(source / script), *map(str, args)],
            check=True, timeout=300, cwd=source,
        )
    safe = output / "commonmark-safe.json"
    spec_report = output / "commonmark-spec.json"
    check("survey_commonmark_spec.py", "--renderer", html, "--spec", spec,
          "--render-policy", "safe", "--report", safe)
    check("gate_commonmark_nonregression.py", "--survey", safe,
          "--baseline", source / "commonmark_0_31_2_baseline.json",
          "--out", output / "safe-gate.json")
    check("survey_commonmark_spec.py", "--renderer", html, "--spec", spec,
          "--render-policy", "spec", "--report", spec_report)
    check("gate_commonmark_spec_nonregression.py", "--survey", spec_report,
          "--baseline", source / "commonmark_0_31_2_spec_baseline.json",
          "--out", output / "spec-gate.json")
    check("compare_cmarkgfm_html.py", "--renderer", html,
          "--report", output / "gfm-oracle.json")
    check("compare_python_markdown.py", "--parser", ast,
          "--report", output / "ast-oracle.json")
    check("gate_commonmark_indented.py", "--renderer", html, "--spec", spec,
          "--report", output / "indent-gate.json")
    subprocess.run([sys.executable, "-m", "unittest", "discover",
                    "-s", str(source), "-p", "test_*.py"],
                   check=True, timeout=60, cwd=source)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
