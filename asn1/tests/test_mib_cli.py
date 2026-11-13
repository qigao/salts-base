"""Formal command-line behavior checks for the module's built compiler."""

import pathlib
import subprocess
import sys
import tempfile


def main():
    compiler = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="asn1 mib ") as temporary:
        root = pathlib.Path(temporary)
        source = root / "input.mib"
        source.write_text(
            "Demo DEFINITIONS ::= BEGIN root OBJECT IDENTIFIER ::= { 1 3 6 1 } END",
            encoding="ascii",
        )

        def run(arguments, success=True):
            result = subprocess.run([str(compiler), *map(str, arguments)], capture_output=True, text=True)
            if (result.returncode == 0) != success:
                raise AssertionError(f"Unexpected exit {result.returncode}: {arguments}\n{result.stderr}")

        run(["--help"])
        run([], False)
        run(["-o"], False)
        run(["--unknown", source], False)
        run([source, source], False)
        run([root / "missing.mib"], False)
        run(["-o", source, source], False)
        for index, (flags, expected) in enumerate([
            ([], {"Demo.h", "Demo.c", "Demo_runtime.c"}),
            (["--no-c"], {"Demo_runtime.c"}),
            (["--no-runtime"], {"Demo.h", "Demo.c"}),
            (["--no-c", "--no-runtime"], set()),
        ]):
            output = root / f"output space & {index}"
            run([*flags, "-o", output, source])
            if {path.name for path in output.iterdir()} != expected:
                raise AssertionError(f"Incorrect outputs for {flags}")

        source.write_text("Bad DEFINITIONS ::= BEGIN x OBJECT IDENTIFIER ::= { 3 1 } END", encoding="ascii")
        output = root / "invalid"
        run(["-o", output, source], False)
        if list(output.iterdir()):
            raise AssertionError("Semantic errors must not write generated files")
        run(["--no-c", "--no-runtime", "-o", output, source], False)


if __name__ == "__main__":
    main()
