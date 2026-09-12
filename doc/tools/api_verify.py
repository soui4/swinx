#!/usr/bin/env python3
"""对 scanner 判定为 stub/partial 的函数，打印其真实定义体供人工核定"""
import re, os, json, sys

ROOT = r"D:/work/soui4/swinx"
d = json.load(open(os.path.join(ROOT, "doc/tools/api_scan.json"), encoding="utf-8"))
f = d["funcs"]

def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", " ", s, flags=re.S)
    s = re.sub(r"//[^\n]*", " ", s)
    return s

def find_def_body(src, name):
    """与主扫描器一致：找 name( ... ) { ... }，调用点(后跟;)会被跳过"""
    pos = 0
    while True:
        i = src.find(name, pos)
        if i == -1:
            return None
        before = src[max(0, i - 40):i]
        if not re.search(r"[;{}\*>\s]\s*$", before) or re.search(r"::\s*$", before):
            pos = i + len(name)
            continue
        j = src.find("(", i)
        depth = 0
        k = j
        while k < len(src):
            if src[k] == "(":
                depth += 1
            elif src[k] == ")":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        k2 = k + 1
        while k2 < len(src) and src[k2] not in "{;":
            k2 += 1
        if k2 < len(src) and src[k2] == "{":
            depth = 0
            m = k2
            while m < len(src):
                if src[m] == "{":
                    depth += 1
                elif src[m] == "}":
                    depth -= 1
                    if depth == 0:
                        return src[k2:m + 1]
                m += 1
        pos = i + len(name)

targets = sorted(n for n, v in f.items() if v["agg"] in ("stub", "partial"))
for n in targets:
    v = f[n]
    for rel, cls, marks in v["site_classes"]:
        path = os.path.join(ROOT, "src", rel)
        src = open(path, encoding="utf-8", errors="replace").read()
        body = find_def_body(src, n)
        if body is None:
            body = "<<未定位到定义体>>"
        txt = re.sub(r"\s+", " ", strip_comments(body)).strip()
        print(f"### {n} [{cls}] @ {rel}  ({marks})")
        print("   ", txt[:360])
    print()
