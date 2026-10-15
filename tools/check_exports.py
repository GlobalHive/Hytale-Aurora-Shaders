import ctypes
from pathlib import Path
import re
import sys


def check(path):
    source = (Path(__file__).resolve().parent.parent / "hook/gl_stubs.c").read_text()
    table = source.split("stub_names[STUB_COUNT] = {", 1)[1].split("};", 1)[0]
    names = re.findall(r'"([A-Za-z0-9_]+)"', table)
    names += ["wglGetProcAddress", "wglSwapBuffers", "glBindTexture", "glDrawArrays", "glDrawElements"]
    library = ctypes.WinDLL(str(path.resolve()))
    missing = [name for name in names if not getattr(library, name, None)]
    if missing:
        raise RuntimeError("Missing OpenGL proxy exports: " + ", ".join(missing))
    print(f"PASS: all {len(names)} Windows OpenGL proxy exports are present")


if __name__ == "__main__":
    check(Path(sys.argv[1]))
