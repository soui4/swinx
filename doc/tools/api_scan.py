#!/usr/bin/env python3
"""扫描 swinx 头文件声明的 API，在 src 中定位定义并按函数体特征分类：
implemented(完整实现) / partial(部分实现) / stub(空实现)"""
import re, os, json, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INC = os.path.join(ROOT, "include")
SRC = os.path.join(ROOT, "src")

# ---------- 1. 从头文件收集声明 ----------
decls = {}  # name -> set(header)
for h in sorted(os.listdir(INC)):
    if not h.endswith(".h"):
        continue
    text = open(os.path.join(INC, h), encoding="utf-8", errors="replace").read()
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    # 去掉 typedef 行与预处理行
    lines = [l for l in text.split("\n") if not l.strip().startswith("#")]
    text = "\n".join(lines)
    # 函数声明：类型 名称(参数);  —— 非多行括号内的情况用贪心匹配
    for m in re.finditer(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(((?:[^()]|\([^()]*\))*)\)\s*(?:CONST|const)?\s*;", text):
        name = m.group(1)
        before = text[max(0, m.start() - 60):m.start()]
        if re.search(r"typedef\s*$", before):
            continue
        if name in ("WINAPI", "CALLBACK", "CDECL", "APIENTRY", "if", "for", "while", "switch", "return", "sizeof"):
            continue
        decls.setdefault(name, set()).add(h)

# ---------- 2. 在源码中定位定义并抽取函数体 ----------
def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", " ", s, flags=re.S)
    s = re.sub(r"//[^\n]*", " ", s)
    # 去字符串字面量内容（单引号仅匹配标准 char 字面量，避免吞掉代码）
    s = re.sub(r'"(?:\\.|[^"\\])*"', '""', s)
    s = re.sub(r"(?<![\w'])'(?:\\.|[^'\\\n])'(?![\w'])", "''", s)
    return s

def find_body(src, name, start):
    """从 start 位置向后找 name( ... ) { ... }，返回函数体或 None"""
    i = src.find(name, start)
    while i != -1:
        j = src.find("(", i)
        if j == -1:
            return None, -1
        # 匹配参数括号
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
        # 向后跳过空白与可能的 const/noexcept 找 {
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
                        return src[k2:m + 1], m
                m += 1
            return src[k2:], len(src)
        i = src.find(name, i + 1)
    return None, -1

defs = {}  # name -> list of (file, classification, notes)
srcfiles = []
for dirpath, _, files in os.walk(SRC):
    for f in files:
        if f.endswith((".cpp", ".mm", ".c")):
            srcfiles.append(os.path.join(dirpath, f))

CALL_IGNORE = re.compile(
    r"^(SLOG|SLOGW|SLOGE|SLOGFMT|FIXME|TRACE|memset|memcpy|memmove|strcpy|strlen|"
    r"swinx_log|assert|ASSERT|static_assert|_ASSERT|OutputDebug)\w*$")

def classify(body):
    b = strip_comments(body)
    markers = []
    if re.search(r"stub", b, re.I):
        markers.append("stub注释")
    if re.search(r"not implemented|not_impl|unimplemented", b, re.I):
        markers.append("not-implemented")
    if re.search(r"\bFIXME\b", b):
        markers.append("FIXME")
    if re.search(r"\bTODO\b", b):
        markers.append("TODO")
    # 去掉 SLOG 语句行，统计剩余
    lines = [l for l in b.split("\n") if not re.search(r"\bSLOG\w*\s*\(|\bFIXME\s*\(|\bTRACE\s*\(", l)]
    rest = "\n".join(lines)
    # 统计“有效动作”：return/赋值之外，调用的函数
    calls = set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", rest))
    calls = {c for c in calls if not CALL_IGNORE.match(c) and c not in
             ("if", "while", "for", "switch", "return", "sizeof", "defined")}
    nlines = len([l for l in rest.split("\n") if l.strip()])
    # 有实际数据操作的简单实现（getter/setter/拷贝/比较/构造析构）
    simple_real = bool(
        re.search(r"->\s*\w+|\.\s*\w+\s*=", rest)
        or re.search(r"\b(memcpy|memset|memmove|delete|new)\b", rest)
        or re.search(r"==|!=", rest)
        or re.search(r"\berrno\b", rest)
    )
    if calls:
        if markers:
            return "partial", markers
        return "impl", []
    if simple_real:
        if markers:
            return "partial", markers
        return "impl", ["简单getter/setter/拷贝"]
    if markers and nlines <= 6:
        return "stub", markers
    if nlines <= 4:
        # 无任何外部调用的极短函数体 → 空实现
        return "stub", ["仅返回常量/无操作"]
    if markers:
        return "partial", markers
    return "impl", []

for path in srcfiles:
    rel = os.path.relpath(path, SRC).replace("\\", "/")
    try:
        src = open(path, encoding="utf-8", errors="replace").read()
    except Exception:
        continue
    stripped = strip_comments(src)
    for name in sorted(decls):
        # 快速过滤（在原始文本上执行，避免 strip 误杀）
        if not re.search(r"\b" + re.escape(name) + r"\s*\(", src):
            continue
        pos = 0
        while True:
            body, end = find_body(src, name, pos)
            if body is None:
                break
            # 确认是定义而非函数内调用：检查名字前是否是其他标识符(成员/::)
            pre = src[:src.rfind(name, pos, src.find(name, pos) + len(name))]
            # find_body 用 find 重查了，重新定位
            i = src.find(name, pos)
            before = src[max(0, i - 40):i]
            if not re.search(r"[;{}\*>\s]\s*$", before) or re.search(r"::\s*$", before):
                pos = i + len(name)
                continue
            cls, marks = classify(body)
            defs.setdefault(name, []).append((rel, cls, ";".join(marks)))
            pos = i + len(name)

# ---------- 3. 汇总输出 ----------
result = {}
for name, headers in decls.items():
    if name not in defs:
        continue  # 头文件声明但源码无定义（可能是宏实现或未提供）
    sites = defs[name]
    classes = [c for _, c, _ in sites]
    if "impl" in classes:
        agg = "impl"
    elif "partial" in classes:
        agg = "partial"
    else:
        agg = "stub"
    result[name] = {
        "headers": sorted(headers),
        "files": sorted({f for f, _, _ in sites}),
        "agg": agg,
        "site_classes": [(f, c, m) for f, c, m in sites],
    }

out = {
    "_meta": {
        "decl_total": len(decls),
        "defined_total": len(result),
        "no_def": sorted(n for n in decls if n not in defs),
    },
    "funcs": result,
}
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "api_scan.json"), "w", encoding="utf-8") as fp:
    json.dump(out, fp, ensure_ascii=False, indent=1)

# 摘要
from collections import Counter
agg_c = Counter(v["agg"] for v in result.values())
print("声明函数总数:", len(decls), " 有定义:", len(result))
print("分类统计:", dict(agg_c))
print("声明但无定义(前40):", out["_meta"]["no_def"][:40])
