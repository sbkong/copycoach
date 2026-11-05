"""ctest check 1 (docs/ui/naming.md section 6): no registry id or token name appears as a string literal in hand-written
source. Ids are referenced through the generated headers only. Exact-id matching, so file names like project.json do not
trigger. Exit code 1 lists every offending line.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
SCAN_DIRS = ["core", "ui", "app"]
# Practice event names are stored data, not UI ids (stats.md section 4, user decision 2026-11-02): one may
# legitimately spell the same as an action (copy.predict.accept).
EXEMPT_FILES = {os.path.join("core", "stats", "event.h")}
EXTENSIONS = {".h", ".cpp", ".hpp", ".cc"}


def ids():
    reg = json.load(open(os.path.join(ROOT, "data", "ui", "registry.json"), encoding="utf-8"))
    names = {a["id"] for a in reg["actions"]}
    names |= {p["id"] for p in reg["panels"]["list"]}
    names |= {s["id"] for s in reg["settings"]["list"]}
    theme = json.load(open(os.path.join(ROOT, "data", "ui", "theme-dark.json"), encoding="utf-8"))
    for section in ("semantic", "component", "shape", "spacing", "typography", "font"):
        # plain words such as "unit" or "body" are legitimate JSON keys elsewhere; only hyphenated token names are checked
        names |= {k for k in theme.get(section, {}) if not k.startswith("$") and "-" in k}
    return names


def main():
    names = ids()
    pattern = re.compile('"(' + "|".join(re.escape(n) for n in sorted(names, key=len, reverse=True)) + ')"')
    offenders = []
    for d in SCAN_DIRS:
        for dirpath, _, files in os.walk(os.path.join(ROOT, d)):
            if "spike" in dirpath.replace("\\", "/").split("/"):
                continue
            for fn in files:
                if os.path.splitext(fn)[1] not in EXTENSIONS:
                    continue
                path = os.path.join(dirpath, fn)
                if os.path.relpath(path, ROOT) in EXEMPT_FILES:
                    continue
                for no, line in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
                    m = pattern.search(line)
                    if m:
                        offenders.append(f"{os.path.relpath(path, ROOT)}:{no}: literal id {m.group(1)}")
    if offenders:
        print("\n".join(offenders))
        print(f"check_literals: {len(offenders)} literal id(s); use the generated headers (generated/*.h)")
        sys.exit(1)
    print(f"check_literals: ok ({len(names)} names, dirs {SCAN_DIRS})")


if __name__ == "__main__":
    main()
