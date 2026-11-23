"""Generates the architecture diagrams in docs/images/.

Each diagram is written twice, <name>-light.svg and <name>-dark.svg, and the
Markdown picks one with <picture> and prefers-color-scheme.

Usage: python docs/images/generate_diagrams.py
Only the Python standard library is needed.
"""

from pathlib import Path
from xml.sax.saxutils import escape

OUT = Path(__file__).resolve().parent
WIDTH = 880
MONO = "ui-monospace, SFMono-Regular, 'SF Mono', Menlo, Consolas, 'Liberation Mono', monospace"
FONT = "-apple-system, BlinkMacSystemFont, 'Segoe UI', 'Noto Sans', Helvetica, Arial, sans-serif"

THEMES = {
    "light": {
        "panel_fill": "#f6f8fa", "panel_stroke": "#d0d7de",
        "text": "#1f2328", "muted": "#59636e", "line": "#8c959f",
        "purple": ("#EEEDFE", "#534AB7", "#3C3489", "#534AB7"),
        "teal": ("#E1F5EE", "#0F6E56", "#085041", "#0F6E56"),
        "coral": ("#FAECE7", "#993C1D", "#712B13", "#993C1D"),
        "ghost": ("none", "#8c959f", "#1f2328", "#59636e"),
    },
    "dark": {
        "panel_fill": "#151b23", "panel_stroke": "#3d444d",
        "text": "#e6edf3", "muted": "#9198a1", "line": "#7d8590",
        "purple": ("#26215C", "#AFA9EC", "#EEEDFE", "#CECBF6"),
        "teal": ("#04342C", "#5DCAA5", "#E1F5EE", "#9FE1CB"),
        "coral": ("#4A1B0C", "#F0997B", "#FAECE7", "#F5C4B3"),
        "ghost": ("none", "#7d8590", "#e6edf3", "#9198a1"),
    },
}


class Svg:
    def __init__(self, theme, height, title, desc):
        self.t = THEMES[theme]
        self.height = height
        self.parts = []
        self.title = title
        self.desc = desc

    def text(self, x, y, s, size=13, weight=400, color=None, anchor="start", mono=False):
        color = color or self.t["text"]
        family = f' font-family="{MONO}"' if mono else ""
        self.parts.append(
            f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{weight}" fill="{color}"{family} '
            f'text-anchor="{anchor}" dominant-baseline="central">{escape(s)}</text>')

    def panel(self, x, y, w, h, title, sub=None, title_y=None):
        t = self.t
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="{t["panel_fill"]}" '
            f'stroke="{t["panel_stroke"]}" stroke-width="1"/>')
        ty = title_y if title_y is not None else y + 22
        self.text(x + 16, ty, title, 14, 600)
        if sub:
            self.text(x + 16, ty + 18, sub, 12, 400, t["muted"])

    def chip(self, x, y, w, h, title, sub=None, ramp="purple", dashed=False, sub2=None, mono=False):
        fill, stroke, tcol, scol = self.t[ramp]
        dash = ' stroke-dasharray="5 4"' if dashed else ""
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="{fill}" '
            f'stroke="{stroke}" stroke-width="1"{dash}/>')
        cx = x + w / 2
        lines = [s for s in (sub, sub2) if s]
        if not lines:
            self.text(cx, y + h / 2, title, 13, 600, tcol, "middle", mono)
            return
        total = 18 + 16 * len(lines)
        top = y + (h - total) / 2 + 9
        self.text(cx, top, title, 13, 600, tcol, "middle", mono)
        for i, line in enumerate(lines):
            self.text(cx, top + 18 + 16 * i, line, 12, 400, scol, "middle")

    def arrow(self, d, dashed=False, both=False):
        dash = ' stroke-dasharray="5 4"' if dashed else ""
        start = ' marker-start="url(#arrow)"' if both else ""
        self.parts.append(
            f'<path d="{d}" fill="none" stroke="{self.t["line"]}" stroke-width="1.4"{dash} '
            f'marker-end="url(#arrow)"{start}/>')

    def render(self):
        line = self.t["line"]
        return (
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{self.height}" '
            f'viewBox="0 0 {WIDTH} {self.height}" role="img" font-family="{FONT}">\n'
            f'<title>{escape(self.title)}</title>\n<desc>{escape(self.desc)}</desc>\n'
            '<defs><marker id="arrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" '
            'markerHeight="7" orient="auto-start-reverse"><path d="M2 1L8 5L2 9" fill="none" '
            f'stroke="{line}" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/>'
            '</marker></defs>\n' + "\n".join(self.parts) + "\n</svg>\n")


def row(svg, x0, width, y, h, items, gap=12, ramp="purple"):
    """Places chips of equal width across [x0, x0 + width)."""
    n = len(items)
    w = (width - gap * (n - 1)) / n
    for i, item in enumerate(items):
        title, sub = item[0], item[1] if len(item) > 1 else None
        r = item[2] if len(item) > 2 else ramp
        svg.chip(round(x0 + i * (w + gap), 1), y, round(w, 1), h, title, sub, r)
    return w


# ---------------------------------------------------------------- layers

def architecture(theme):
    """Layers and the modules (static libraries) in each, with the systems they hold."""
    chip_h, gap_v, gap_h, pad = 52, 10, 12, 12
    label_w = 170
    x0, x1 = 20, WIDTH - 20
    cx0 = x0 + label_w
    cw = x1 - 12 - cx0

    def span_row(svg, y, items, columns):
        unit = (cw - gap_h * (columns - 1)) / columns
        x = cx0
        for title, systems, span, mono, ramp in items:
            w = unit * span + gap_h * (span - 1)
            svg.chip(round(x, 1), y, round(w, 1), chip_h, title, systems, ramp, mono=mono)
            x += w + gap_h

    P, T = "purple", "teal"
    # Each chip: (module, systems, span in columns, monospace title, ramp); then the column count.
    layers = [
        ("Tools", "Programs over the engine", [
            ([("apps/cooker", "sources to cooked files", 1, True, P),
              ("apps/editor", "hierarchy, inspector, gizmos", 1, True, P)], 2)]),
        ("Game and world", "Composition and the frame", [
            ([("app", "engine, main loop", 1, True, P), ("world", "ECS, scheduler, extract", 1, True, P),
              ("debug", "overlay, console, stats", 1, True, P), ("Game code", "gameplay systems", 1, False, P)], 4)]),
        ("Servers", "Independent of each other", [
            ([("render", "server, culling, debug draw", 1, True, P),
              ("physics", "bodies, queries, solver", 1, True, P),
              ("animation", "sampling, blending, skinning", 1, True, P)], 3),
            ([("particles", "emitters, SoA simulation", 1, True, P),
              ("audio", "voices, 3D mixing", 1, True, P)], 3)]),
        ("Graphics, assets", "Used by the servers", [
            ([("rhi", "interface, bindless, barriers", 1, True, P),
              ("rhi/d3d12", "DirectX 12 backend", 1, True, P),
              ("assets", "IO thread, RefPool, loaders", 1, True, P)], 3)]),
        ("Base", "Built on core", [
            ([("platform", "window, input, files, time", 1, True, P),
              ("math", "vectors, matrices, quaternions", 1, True, P),
              ("reflect", "manual type registration", 1, True, P)], 3),
            ([("jobs", "thread pool, scheduler", 1, True, P),
              ("asset_formats", "cooked layouts, no logic", 1, True, P)], 3)]),
        ("Core", "Lowest library", [
            ([("Types", "numeric types", 1, False, T), ("Log", "lock-free sinks", 1, False, T),
              ("Assert", "handler, fatal", 1, False, T), ("Handle<T>", "runtime refs", 1, False, T),
              ("StringId", "64-bit name hash", 1, False, T)], 5),
            ([("Memory", "allocators, tags", 1, False, T), ("CVars", "and commands", 1, False, T),
              ("Events", "typed queues", 1, False, T), ("SPSC queue", "lock-free ring", 1, False, T)], 5)]),
    ]

    def height(nrows, extra=0):
        return nrows * chip_h + (nrows - 1) * gap_v + 2 * pad + extra

    heights = [height(len(rows), 22 if name == "Core" else 0) for name, _, rows in layers]
    total = 20 + sum(heights) + 12 * (len(layers) - 1) + 30 + 56 + 46
    svg = Svg(theme, total, "Aurora Engine architecture",
              "Six layers, from tools down to core, and the modules in each. Each module is a static "
              "library that depends only on the layers below it; servers do not depend on each other.")

    y = 20
    memory_x = memory_bottom = None
    for (name, sub, rows), h in zip(layers, heights):
        svg.panel(x0, y, x1 - x0, h, name, sub, title_y=y + h / 2 - 9)
        ry = y + pad
        if name == "Core":
            svg.text(cx0, y + 20, "core", 13, 600, svg.t["text"], mono=True)
            svg.text(cx0 + 44, y + 20, "one library: vocabulary and infrastructure for every module",
                     12, 400, svg.t["muted"])
            ry = y + 34
        for items, columns in rows:
            span_row(svg, ry, items, columns)
            if items[0][0] == "Memory":
                memory_x = cx0 + (cw - gap_h * (columns - 1)) / columns / 2
                memory_bottom = ry + chip_h
            ry += chip_h + gap_v
        y += h + 12

    box_y = y - 12 + 30
    os_w = 260
    os_x = max(x0, memory_x - os_w / 2)
    svg.arrow(f"M{memory_x} {memory_bottom} L{memory_x} {box_y - 2}")
    svg.chip(os_x, box_y, os_w, 56, "Windows (Win32)", "virtual memory only", "ghost", dashed=True)
    tx = os_x + os_w + 24
    svg.chip(tx, box_y, x1 - tx, 56, "tests/  (outside the stack)",
             "one executable per layer, plus test_main", "ghost", dashed=True)
    svg.text(x0, box_y + 56 + 26, "Each module is a static library and can include only the headers "
             "of the layers below it. World connects the servers.", 12, 400, svg.t["muted"])
    return svg


# ---------------------------------------------------------------- core

def core(theme):
    svg = Svg(theme, 698, "Aurora core layer",
              "Foundations, the memory stack and the planned configuration and messaging modules of "
              "the core layer. Arrows mean 'built on'.")
    x0, x1 = 20, WIDTH - 20
    inner0, inner1 = x0 + 16, x1 - 16
    iw = inner1 - inner0

    svg.panel(x0, 20, x1 - x0, 104, "Foundations")
    row(svg, inner0, iw, 56, 54, [("Types", "u8 .. f64, size literals"), ("Log", "lock-free sinks"),
                                  ("Assert", "handler, fatal"), ("Handle<T>", "index + generation"),
                                  ("StringId", "64-bit name hash")])

    svg.panel(x0, 140, x1 - x0, 376, "Memory", "Every allocation has an owner and a tag")
    gap = 14
    cw = (iw - 4 * gap) / 5
    cols = [inner0 + i * (cw + gap) for i in range(5)]
    mid = [c + cw / 2 for c in cols]
    ch = 54

    y_top, y_alloc, y_buf, y_vm, y_rep = 196, 278, 360, 442, 476

    svg.chip(cols[2], y_top, cw, ch, "RefPool<T>", "shared assets only", "teal")
    svg.chip(cols[4], y_top, cw, ch, "Global new/delete", "safety net, General", "teal")
    allocs = [("Arena", "marks, scratch, ring"), ("Pool", "fixed-size blocks"),
              ("HandlePool<T>", "generations"), ("DenseArray<T>", "swap-remove"),
              ("Heap", "AURORA_ALLOC + tag")]
    for c, (t, s) in zip(cols, allocs):
        svg.chip(c, y_alloc, cw, ch, t, s, "teal")
    for i in (2, 4):
        svg.arrow(f"M{mid[i]} {y_top + ch} L{mid[i]} {y_alloc - 3}")
    for i in range(5):
        svg.arrow(f"M{mid[i]} {y_alloc + ch} L{mid[i]} {y_buf - 3}")

    vb_w = cols[3] + cw - cols[0]
    svg.chip(cols[0], y_buf, vb_w, ch, "VirtualBuffer", "reserve once, commit in place, the base address never moves", "teal")
    svg.chip(cols[4], y_buf, cw, ch, "mimalloc", "general-purpose heap", "teal")
    vmid = cols[0] + vb_w / 2
    y_vm = y_buf + ch + 28
    svg.arrow(f"M{vmid} {y_buf + ch} L{vmid} {y_vm - 3}")
    svg.chip(cols[0], y_vm, vb_w, ch, "vm:: (Win32)", "VirtualAlloc and VirtualFree: the only OS code in core", "teal")
    svg.chip(cols[4], y_vm, cw, ch, "Memory report", "per-tag leaks at shutdown", "teal")

    svg.panel(x0, 532, x1 - x0, 112, "Configuration and messaging")
    row(svg, inner0, iw, 568, 58, [("CVars and commands", "applied at frame start"),
                                   ("EventQueue, Delegate", "double-buffered events"),
                                   ("SpscQueue", "IO and audio threads")])
    svg.text(x0, 668, "Arrows mean “built on”. Arena, Pool, HandlePool and DenseArray grow "
             "inside one reservation, so pointers never move.", 12, 400, svg.t["muted"])
    return svg


# ---------------------------------------------------------------- threads

def threads(theme):
    svg = Svg(theme, 486, "Aurora runtime threads",
              "The main thread runs the frame and talks to the render, IO, audio and worker threads "
              "only through frame packets and queues.")
    x0, x1 = 20, WIDTH - 20
    panel_bottom = 160
    svg.panel(x0, 20, x1 - x0, panel_bottom - 20, "Main thread", "Window, input, simulation, world, extract")
    steps = [("Frame start", "apply cvars, drain queues"), ("Platform and input", "window messages"),
             ("Fixed-step update", "systems, 0..N times"), ("Extract", "writes the frame packet")]
    inner0, iw = x0 + 16, x1 - x0 - 32
    gap = 20
    w = (iw - 3 * gap) / 4
    cy = 82
    for i, (t, s) in enumerate(steps):
        x = inner0 + i * (w + gap)
        svg.chip(x, cy, w, 56, t, s)
        if i:
            svg.arrow(f"M{x - gap + 2} {cy + 28} L{x - 2} {cy + 28}")

    by = 236
    boxes = [
        ("IO thread", "blocking file reads", "watches cooked assets"),
        ("Audio thread", "real-time mixing", "no locks, no allocation"),
        ("Workers", "jobs over array ranges", "hardware threads \u2212 2"),
        ("Render thread", "applies packet commands", "records and presents"),
    ]
    xs = [inner0 + i * (w + gap) for i in range(4)]
    for x, (t, s, s2) in zip(xs, boxes):
        svg.chip(x, by, w, 72, t, s, "coral", sub2=s2)

    labels = [("SPSC requests", "and completions"), ("SPSC commands", "and events"),
              ("scheduler", "phases"), ("frame packets", "ring of 2")]
    for i, x in enumerate(xs):
        m = x + w / 2
        if i == 3:
            svg.arrow(f"M{m} {cy + 56} L{m} {by - 3}")
        else:
            svg.arrow(f"M{m} {panel_bottom} L{m} {by - 3}", both=i < 2, dashed=i == 2)
        svg.text(m + 10, 189, labels[i][0], 12, 400, svg.t["muted"])
        svg.text(m + 10, 205, labels[i][1], 12, 400, svg.t["muted"])

    m = xs[3] + w / 2
    svg.arrow(f"M{m} {by + 72} L{m} {by + 72 + 33}")
    svg.chip(xs[3], by + 72 + 36, w, 56, "GPU", "two frames in flight", "ghost", dashed=True)
    svg.text(x0, 448, "Threads never share mutable state: data is copied into packets and queue "
             "entries,", 12, 400, svg.t["muted"])
    svg.text(x0, 466, "or handed over with explicit ownership.", 12, 400, svg.t["muted"])
    return svg


# ---------------------------------------------------------------- frame pipeline

def pipeline(theme):
    svg = Svg(theme, 236, "Aurora frame pipeline",
              "While the main thread simulates frame k+1, the render thread records frame k and the "
              "GPU executes frame k-1.")
    lanes = [("Main", "simulate + extract", 0), ("Render", "record", 1), ("GPU", "execute", 2)]
    x_lane, x_start = 20, 140
    slot = 170
    for li, (name, verb, offset) in enumerate(lanes):
        y = 24 + li * 58
        svg.text(x_lane, y + 22, name, 14, 600)
        ramp = ["purple", "coral", "ghost"][li]
        for k in range(4):
            frame = k + 3 - offset
            x = x_start + (k + offset * 0.5) * slot
            if x + slot - 10 > WIDTH - 20:
                continue
            svg.chip(x, y, slot - 10, 44, f"{verb} {frame}", None, ramp, dashed=li == 2)
    svg.arrow(f"M{x_start} 206 L{WIDTH - 24} 206")
    svg.text(x_start, 222, "time", 12, 400, svg.t["muted"])
    return svg


# ---------------------------------------------------------------- assets

def assets(theme):
    svg = Svg(theme, 470, "Aurora asset pipeline",
              "Offline, the cooker turns source files into GPU-ready cooked files. At runtime the IO "
              "thread reads them, the asset server finalizes them and the render server uploads them.")
    x0, x1 = 20, WIDTH - 20
    inner0, iw = x0 + 16, x1 - x0 - 32
    gap = 90
    w = (iw - 2 * gap) / 3
    xs = [inner0 + i * (w + gap) for i in range(3)]
    mids = [x + w / 2 for x in xs]
    h = 66

    svg.panel(x0, 20, x1 - x0, 140, "Offline", "The cooker converts sources; the runtime never parses them")
    oy = 76
    svg.chip(xs[0], oy, w, h, "Source assets", "glTF, PNG, HLSL", "ghost", dashed=True)
    svg.chip(xs[1], oy, w, h, "Asset cooker", "mips, BC compression, DXC", sub2="incremental by hash")
    svg.chip(xs[2], oy, w, h, "Cooked files", "GPU-ready, loaded in place", "ghost", dashed=True,
             sub2="offsets, not pointers")
    for i in (0, 1):
        svg.arrow(f"M{xs[i] + w + 3} {oy + h / 2} L{xs[i + 1] - 3} {oy + h / 2}")

    fy = 184
    svg.chip(xs[1], fy, w, 50, "Asset formats", "shared layouts, no logic", "teal")
    svg.arrow(f"M{mids[1]} {fy - 2} L{mids[1]} {oy + h + 3}")

    svg.panel(x0, 258, x1 - x0, 140, "Runtime", "Loading never stalls the frame")
    ry = 314
    svg.chip(xs[2], ry, w, h, "IO thread", "blocking reads", sub2="folder watch for hot reload", ramp="coral")
    svg.chip(xs[1], ry, w, h, "assets", "RefPool, loader registry", sub2="finalized at frame start", mono=True)
    svg.chip(xs[0], ry, w, h, "Render server", "mesh and texture loaders", sub2="create GPU resources")
    svg.arrow(f"M{mids[1]} {fy + 50} L{mids[1]} {ry - 3}")
    svg.arrow(f"M{mids[2]} {oy + h} L{mids[2]} {ry - 3}")
    for i in (2, 1):
        svg.arrow(f"M{xs[i] - 3} {ry + h / 2} L{xs[i - 1] + w + 3} {ry + h / 2}")
    svg.text(mids[2] + 10, 246, "read", 12, 400, svg.t["muted"])
    svg.text((xs[1] + w + xs[2]) / 2, ry + h / 2 - 12, "completions", 12, 400, svg.t["muted"], "middle")
    svg.text((xs[0] + w + xs[1]) / 2, ry + h / 2 - 12, "calls loader", 12, 400, svg.t["muted"], "middle")

    svg.text(x0, 428, "The cooker and the runtime share only the asset formats module, so the cooker "
             "never links runtime code.", 12, 400, svg.t["muted"])
    svg.text(x0, 446, "Assets are the only reference-counted objects; the render thread sees GPU "
             "handles, never asset objects.", 12, 400, svg.t["muted"])
    return svg


DIAGRAMS = {
    "architecture": architecture,
    "core": core,
    "threads": threads,
    "frame-pipeline": pipeline,
    "asset-pipeline": assets,
}

if __name__ == "__main__":
    for name, build in DIAGRAMS.items():
        for theme in THEMES:
            path = OUT / f"{name}-{theme}.svg"
            path.write_text(build(theme).render(), encoding="utf-8")
            print(path.relative_to(OUT.parent.parent))
