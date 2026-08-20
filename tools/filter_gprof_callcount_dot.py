#!/usr/bin/env python3
"""Reduce a zero-time gprof2dot graph to the hottest functions by call count."""

import colorsys
import math
import re
import sys


def usage():
    raise SystemExit(
        "usage: filter_gprof_callcount_dot.py INPUT.dot OUTPUT.dot [TOP=100]"
    )


if len(sys.argv) not in (3, 4):
    usage()

input_path = sys.argv[1]
output_path = sys.argv[2]
top_count = int(sys.argv[3]) if len(sys.argv) == 4 else 100
if top_count < 1:
    usage()

node_re = re.compile(r"^\s*(\d+)\s+\[(.*)\];\s*$")
edge_re = re.compile(r"^\s*(\d+)\s*->\s*(\d+)\s+\[(.*)\];\s*$")
label_re = re.compile(r'label="((?:\\.|[^"])*)"')
calls_re = re.compile(r"\\n(\d+)×")

nodes = {}
edges = []

with open(input_path, "r", encoding="utf-8", errors="replace") as source:
    for line in source:
        edge_match = edge_re.match(line)
        if edge_match:
            label_match = label_re.search(edge_match.group(3))
            call_match = calls_re.search(label_match.group(1)) if label_match else None
            calls = int(call_match.group(1)) if call_match else 0
            edges.append((edge_match.group(1), edge_match.group(2), calls))
            continue

        node_match = node_re.match(line)
        if not node_match:
            continue
        label_match = label_re.search(node_match.group(2))
        if not label_match:
            continue
        label = label_match.group(1)
        call_match = calls_re.search(label)
        calls = int(call_match.group(1)) if call_match else 0
        nodes[node_match.group(1)] = (label, calls)

selected_ids = {
    node_id
    for node_id, _ in sorted(
        nodes.items(), key=lambda item: item[1][1], reverse=True
    )[:top_count]
}

max_calls = max((nodes[node_id][1] for node_id in selected_ids), default=1)
max_log = max(math.log10(max_calls + 1), 1.0)


def clean_label(raw_label, calls):
    parts = raw_label.split(r"\n")
    parts = [
        part
        for part in parts
        if not re.fullmatch(r"\(?\d+(?:\.\d+)?%\)?", part)
        and not part.endswith("×")
    ]
    name = parts[0] if parts else "unknown"
    if len(name) > 88:
        name = name[:85] + "..."
    name = name.replace("\\", "\\\\").replace('"', '\\"')
    return f"{name}\\n{calls:,} calls"


def heat_color(calls):
    strength = math.log10(calls + 1) / max_log
    hue = (1.0 - strength) * 0.60
    red, green, blue = colorsys.hsv_to_rgb(hue, 0.82, 0.92)
    return "#{:02x}{:02x}{:02x}".format(
        int(red * 255), int(green * 255), int(blue * 255)
    )


with open(output_path, "w", encoding="utf-8") as output:
    output.write("digraph unreal_profile_calls {\n")
    output.write(
        '\tgraph [fontname="Arial", rankdir=LR, overlap=false, '
        'label="Unreal Amiga: call-count profile (no time samples)", '
        'labelloc=t];\n'
    )
    output.write(
        '\tnode [fontname="Arial", shape=box, style=filled, '
        'fontcolor=white, fontsize=10];\n'
    )
    output.write('\tedge [fontname="Arial", fontsize=8, color="#707070"];\n')

    for node_id in sorted(
        selected_ids, key=lambda item: nodes[item][1], reverse=True
    ):
        raw_label, calls = nodes[node_id]
        output.write(
            f'\t{node_id} [label="{clean_label(raw_label, calls)}", '
            f'fillcolor="{heat_color(calls)}"];\n'
        )

    for caller, callee, calls in edges:
        if caller not in selected_ids or callee not in selected_ids:
            continue
        width = 1.0 + 3.0 * math.log10(calls + 1) / max_log
        label = f'{calls:,}×' if calls else ""
        output.write(
            f'\t{caller} -> {callee} [label="{label}", penwidth={width:.2f}];\n'
        )

    output.write("}\n")
