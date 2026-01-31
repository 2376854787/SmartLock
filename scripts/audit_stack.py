#!/usr/bin/env python3
from __future__ import annotations

import argparse
import glob
import os
import sys
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Sequence, Set, Tuple


@dataclass(frozen=True)
class FrameInfo:
    function: str
    frame_bytes: int
    kind: str
    file_path: str
    line: int
    col: int


def _eprint(*args: object) -> None:
    print(*args, file=sys.stderr)


def _collect_files(build_dir: str, patterns: Sequence[str]) -> List[str]:
    files: List[str] = []
    for pat in patterns:
        files.extend(glob.glob(os.path.join(build_dir, pat), recursive=True))
    deduped = sorted(set(os.path.normpath(p) for p in files if os.path.isfile(p)))
    return deduped


def parse_su_files(su_files: Sequence[str], repo_root: str) -> Tuple[Dict[str, FrameInfo], Dict[str, FrameInfo], List[str]]:
    frames: Dict[str, FrameInfo] = {}
    frames_dynamic: Dict[str, FrameInfo] = {}
    parse_warnings: List[str] = []

    repo_root_norm = os.path.normcase(os.path.normpath(repo_root))

    for su_path in su_files:
        try:
            with open(su_path, "r", encoding="utf-8", errors="replace") as f:
                for raw in f:
                    line = raw.strip("\r\n")
                    if not line:
                        continue
                    parts = line.split("\t")
                    if len(parts) < 3:
                        parse_warnings.append(f"unparsed .su line: {su_path}: {line!r}")
                        continue
                    loc = parts[0].strip()
                    size_str = parts[1].strip()
                    kind = parts[2].strip()
                    try:
                        frame_bytes = int(size_str)
                    except ValueError:
                        parse_warnings.append(f"bad frame size in .su: {su_path}: {line!r}")
                        continue

                    # Format: <file>:<line>:<col>:<func>
                    try:
                        file_path, line_no, col_no, func = loc.rsplit(":", 3)
                        file_path = file_path.strip()
                        func = func.strip()
                        file_line = int(line_no)
                        file_col = int(col_no)
                    except Exception:
                        parse_warnings.append(f"bad location in .su: {su_path}: {line!r}")
                        continue

                    # Normalize to a stable-ish path for module grouping.
                    file_path_norm = os.path.normpath(file_path.replace("/", os.sep))
                    try:
                        if os.path.normcase(os.path.normpath(file_path_norm)).startswith(repo_root_norm):
                            file_path_norm = os.path.relpath(file_path_norm, repo_root)
                    except Exception:
                        pass

                    info = FrameInfo(
                        function=func,
                        frame_bytes=frame_bytes,
                        kind=kind,
                        file_path=file_path_norm,
                        line=file_line,
                        col=file_col,
                    )

                    if kind.lower() == "dynamic":
                        existing = frames_dynamic.get(func)
                        if existing is None or info.frame_bytes > existing.frame_bytes:
                            frames_dynamic[func] = info
                        continue

                    existing = frames.get(func)
                    if existing is None or info.frame_bytes > existing.frame_bytes:
                        frames[func] = info
        except OSError as e:
            parse_warnings.append(f"failed to read .su file {su_path}: {e}")
    return frames, frames_dynamic, parse_warnings


def parse_cgraph_files(cgraph_files: Sequence[str]) -> Tuple[Dict[str, Set[str]], List[str]]:
    graph: Dict[str, Set[str]] = {}
    warnings: List[str] = []

    # GCC dump format (as seen in *.cgraph):
    #   FuncName/242 (FuncName)
    #     Type: function definition analyzed
    #     ...
    #     Calls: foo/1 bar/2
    #
    # We'll key nodes by the token before '/' (FuncName).
    def _sym_from_token(tok: str) -> Optional[str]:
        if "/" not in tok:
            return None
        name, maybe_id = tok.rsplit("/", 1)
        if not maybe_id.isdigit():
            return None
        if not name:
            return None
        return name

    for cg_path in cgraph_files:
        current: Optional[str] = None
        try:
            with open(cg_path, "r", encoding="utf-8", errors="replace") as f:
                for raw in f:
                    line = raw.rstrip("\r\n")
                    if not line:
                        continue
                    if not line.startswith(" "):
                        # Symbol header line.
                        # Example: StartLightSensorTask/242 (StartLightSensorTask)
                        head = line.split()
                        if not head:
                            continue
                        sym = _sym_from_token(head[0])
                        if sym is not None:
                            current = sym
                            graph.setdefault(current, set())
                        else:
                            current = None
                        continue

                    if current is None:
                        continue

                    stripped = line.strip()
                    if stripped.startswith("Calls:"):
                        rest = stripped[len("Calls:") :].strip()
                        if not rest:
                            continue
                        for tok in rest.split():
                            callee = _sym_from_token(tok)
                            if callee is None:
                                continue
                            if callee == current:
                                graph.setdefault(current, set()).add(callee)
                            else:
                                graph.setdefault(current, set()).add(callee)
                                graph.setdefault(callee, set())
        except OSError as e:
            warnings.append(f"failed to read .cgraph file {cg_path}: {e}")
    return graph, warnings


def find_cycles(graph: Dict[str, Set[str]]) -> List[List[str]]:
    color: Dict[str, int] = {}  # 0=unseen, 1=visiting, 2=done
    stack: List[str] = []
    cycles: List[List[str]] = []

    def dfs(start: str) -> None:
        color[start] = 1
        stack.append(start)
        for nxt in graph.get(start, ()):
            c = color.get(nxt, 0)
            if c == 0:
                dfs(nxt)
                continue
            if c == 1:
                try:
                    idx = stack.index(nxt)
                except ValueError:
                    idx = 0
                cycles.append(stack[idx:] + [nxt])
        stack.pop()
        color[start] = 2

    for node in sorted(graph.keys()):
        if color.get(node, 0) == 0:
            dfs(node)
    return cycles


def module_from_file_path(file_path: str) -> str:
    norm = file_path.replace("\\", "/").lstrip("./")
    if not norm or ":" in norm.split("/", 1)[0]:
        return "<external>"
    return norm.split("/", 1)[0]


def top_k_paths(
    graph: Dict[str, Set[str]],
    frame_bytes: Dict[str, int],
    roots: Sequence[str],
    k: int,
) -> List[Tuple[int, List[str]]]:
    sys.setrecursionlimit(max(10000, sys.getrecursionlimit()))
    memo: Dict[str, List[Tuple[int, List[str]]]] = {}
    visiting: Set[str] = set()

    def best_from(node: str) -> List[Tuple[int, List[str]]]:
        if node in memo:
            return memo[node]
        if node in visiting:
            # Cycles should be handled earlier; keep safe.
            return [(frame_bytes.get(node, 0), [node])]
        visiting.add(node)
        base = frame_bytes.get(node, 0)
        candidates: List[Tuple[int, List[str]]] = [(base, [node])]
        for callee in sorted(graph.get(node, ())):
            for sub_sum, sub_path in best_from(callee):
                candidates.append((base + sub_sum, [node] + sub_path))
        candidates.sort(key=lambda x: x[0], reverse=True)
        memo[node] = candidates[:k]
        visiting.remove(node)
        return memo[node]

    all_paths: List[Tuple[int, List[str]]] = []
    for r in roots:
        all_paths.extend(best_from(r))
    all_paths.sort(key=lambda x: x[0], reverse=True)

    # Deduplicate by rendered chain.
    seen: Set[str] = set()
    out: List[Tuple[int, List[str]]] = []
    for total, path in all_paths:
        key = "->".join(path)
        if key in seen:
            continue
        seen.add(key)
        out.append((total, path))
        if len(out) >= k:
            break
    return out


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Audit recursion and stack usage from GCC .cgraph and .su outputs.")
    parser.add_argument("--build-dir", default="build", help="Build directory root (default: build)")
    parser.add_argument(
        "--entries",
        nargs="+",
        default=["main", "Reset_Handler"],
        help="Entry function names to expand call tree from (default: main Reset_Handler)",
    )
    parser.add_argument("--top", type=int, default=10, help="Top N deepest call chains (default: 10)")
    parser.add_argument(
        "--require-entry",
        action="store_true",
        help="Fail if none of the entry functions exist in the call graph",
    )
    args = parser.parse_args(list(argv) if argv is not None else None)

    repo_root = os.path.abspath(os.getcwd())
    build_dir = os.path.abspath(args.build_dir)

    su_files = _collect_files(build_dir, patterns=["**/*.su"])
    cgraph_files = _collect_files(build_dir, patterns=["**/*.cgraph", "**/*.ipa-cgraph"])

    if not su_files:
        _eprint(f"no .su files found under: {build_dir}")
    if not cgraph_files:
        _eprint(f"no .cgraph files found under: {build_dir}")

    frames, frames_dynamic, su_warnings = parse_su_files(su_files, repo_root=repo_root)
    graph, cg_warnings = parse_cgraph_files(cgraph_files)

    if su_warnings:
        for w in su_warnings[:20]:
            _eprint(f"[warn] {w}")
        if len(su_warnings) > 20:
            _eprint(f"[warn] ... {len(su_warnings) - 20} more .su warnings omitted")

    if cg_warnings:
        for w in cg_warnings[:20]:
            _eprint(f"[warn] {w}")
        if len(cg_warnings) > 20:
            _eprint(f"[warn] ... {len(cg_warnings) - 20} more .cgraph warnings omitted")

    cycles = find_cycles(graph)
    if cycles:
        print("recursion_found: yes")
        for i, cyc in enumerate(cycles[:10], 1):
            print(f"cycle_{i}: " + " -> ".join(cyc))
        print(f"cycle_count: {len(cycles)}")
        return 2

    print("recursion_found: no")

    if frames_dynamic:
        print(f"dynamic_stack_functions: {len(frames_dynamic)}")
        # Keep output short but visible.
        for func, info in sorted(frames_dynamic.items(), key=lambda kv: kv[1].frame_bytes, reverse=True)[:10]:
            print(f"dynamic_stack_example: {func} {info.frame_bytes}B @ {info.file_path}:{info.line}:{info.col}")

    frame_bytes = {fn: info.frame_bytes for fn, info in frames.items()}
    entries = [e for e in args.entries if e in graph]

    if not entries:
        msg = f"none of entries found in call graph: {', '.join(args.entries)}"
        if args.require_entry:
            _eprint(msg)
            return 3
        _eprint("[warn] " + msg)
    else:
        print("entries_used: " + " ".join(entries))

    top_paths = top_k_paths(graph=graph, frame_bytes=frame_bytes, roots=entries, k=max(1, args.top)) if entries else []
    print(f"top_paths: {len(top_paths)}")
    for idx, (total, path) in enumerate(top_paths, 1):
        # Render with per-frame if known.
        rendered: List[str] = []
        for fn in path:
            fb = frame_bytes.get(fn)
            if fb is None:
                rendered.append(fn)
            else:
                rendered.append(f"{fn}({fb}B)")
        print(f"top_{idx}: {total}B: " + " -> ".join(rendered))

    # Module hotspots (largest single-frame function per module).
    hotspots: Dict[str, FrameInfo] = {}
    for fn, info in frames.items():
        mod = module_from_file_path(info.file_path)
        cur = hotspots.get(mod)
        if cur is None or info.frame_bytes > cur.frame_bytes:
            hotspots[mod] = info

    print(f"module_hotspots: {len(hotspots)}")
    for mod, info in sorted(hotspots.items(), key=lambda kv: kv[1].frame_bytes, reverse=True):
        print(f"module_{mod}: {info.function} {info.frame_bytes}B @ {info.file_path}:{info.line}:{info.col}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

