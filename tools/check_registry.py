import json, os, re, sys
from collections import Counter

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, "..", "data", "ui")                      # registry lives in the app repo (build input)
shortcuts_md = os.path.join(here, "..", "..", "docs", "ui", "shortcuts.md")  # docs live outside the repo; check 2 is skipped when absent
reg = json.load(open(os.path.join(root, "registry.json"), encoding="utf-8"))
md = open(shortcuts_md, encoding="utf-8").read() if os.path.exists(shortcuts_md) else ""
if not md:
    print("shortcuts.md not found next to the repo; skipping the docs cross-check")

# registry ids (templates kept as-is, e.g. routine.chord.select-{n})
reg_ids = [a["id"] for a in reg["actions"]]
# legacy rows: the old id must be gone, the new one must exist (template instances allowed)
for entry in reg.get("legacy", []):
    assert entry["old"] not in reg_ids, f"legacy old id still registered: {entry['old']}"
    target = entry["new"]
    base = target.rsplit(".", 1)[0] + ".{n}" if target.rsplit(".", 1)[-1].isdigit() else target
    assert target in reg_ids or base in reg_ids, f"legacy new id unknown: {target}"
dups = [k for k, v in Counter(reg_ids).items() if v > 1]
print("registry actions:", len(reg_ids), "dups:", dups)

# ids in shortcuts.md: backticked, full ids or '.suffix' shorthands following a full id in the same cell
md_ids = set()
for line in md.splitlines():
    if not line.startswith("|"):
        continue
    cells = line.split("|")
    if len(cells) < 4:
        continue
    cell = cells[2]
    toks = re.findall(r"`([^`]+)`", cell)
    prefix = None
    for t in toks:
        t = t.strip()
        if t.startswith("."):
            if prefix is None:
                print("shorthand without prefix:", t); continue
            t = prefix + t
        else:
            m = re.match(r"^(.*)\.[^.]+$", t)
            prefix = m.group(1) if m else None
        md_ids.add(t)

reg_set = set(reg_ids)
print("md ids:", len(md_ids))
print("in md not registry:", sorted(md_ids - reg_set))
print("in registry not md:", sorted(reg_set - md_ids))

# key validity: Qt portable names sanity (no empty, no duplicate per ctx after expansion)
expanded = Counter()
for a in reg["actions"]:
    rng = a.get("range")
    ns = range(rng[0], rng[1] + 1) if rng else [None]
    for n in ns:
        for k in a["keys"]:
            key = k.replace("{n}", str(n)) if n is not None else k
            expanded[(a["ctx"], key)] += 1
conf = [(c, k) for (c, k), v in expanded.items() if v > 1]
print("same-context key conflicts:", conf)

# settings/panels id uniqueness
for sec in ("panels", "settings"):
    ids = [x["id"] for x in reg[sec]["list"]]
    d = [k for k, v in Counter(ids).items() if v > 1]
    print(sec, len(ids), "dups:", d)

bad = [i for i in reg_ids + [x["id"] for x in reg["panels"]["list"]] + [x["id"] for x in reg["settings"]["list"]]
       if not re.fullmatch(r"[a-z]+(\.[a-z0-9-]+|\.[a-z-]*\{n\})+", i)]
print("ids violating pattern:", bad)
