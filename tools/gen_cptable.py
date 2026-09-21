#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate swinx/src/cptable.h: static CP936 / Windows-125x -> Unicode tables.

Why this exists
---------------
swinx's sysapi.cpp implements MultiByteToWideChar / WideCharToMultiByte on top of
iconv.  Android's bionic (and OHOS's musl) only implement a handful of Unicode
encodings (UTF-8, UTF-16LE/BE, UTF-32LE/BE, US-ASCII, wchar_t) and know nothing
about GBK/CP936 or the Windows-125x ANSI code pages, so iconv_open() always fails
there.  These tables are the fallback used when iconv is unavailable.

Data source
-----------
CPython's own codecs -- 'cp936' (i.e. GBK) and 'cp1250'..'cp1258'.  They carry the
standard Unicode/Microsoft code page mapping tables, so the generated header is
self-contained and free of third-party licensing entanglements.

Usage
-----
    python swinx/tools/gen_cptable.py

Writes swinx/src/cptable.h as UTF-8 **with BOM** and **CRLF** line endings, to
match the rest of swinx/src (git core.autocrlf=true, see AGENTS.md).
"""
import os

# ---- CP936 two-byte area geometry -------------------------------------------
LEAD_FIRST = 0x81
LEAD_LAST = 0xFE
TRAILS = list(range(0x40, 0x7F)) + list(range(0x80, 0xFF))  # 190 values, 0x7F excluded
TRAIL_COUNT = len(TRAILS)

PER_LINE = 16


def build_cp936():
    """Return the 23940-entry CP936 table, indexed by (lead, trail) as in the header."""
    table = []
    for lead in range(LEAD_FIRST, LEAD_LAST + 1):
        for trail in TRAILS:
            try:
                table.append(ord(bytes((lead, trail)).decode("cp936")))
            except UnicodeDecodeError:
                table.append(0)
    return table


def build_single_byte(cp):
    """Return the 128-entry CP125x table, indexed by (byte - 0x80)."""
    table = []
    for b in range(0x80, 0x100):
        try:
            table.append(ord(bytes((b,)).decode("cp%d" % cp)))
        except UnicodeDecodeError:
            table.append(0)
    return table


def format_array(decl, table):
    lines = ["static const unsigned short %s = {" % decl]
    for i in range(0, len(table), PER_LINE):
        chunk = table[i:i + PER_LINE]
        lines.append("    " + " ".join("0x%04x," % v for v in chunk))
    lines.append("};")
    return lines


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_path = os.path.normpath(os.path.join(here, "..", "src", "cptable.h"))

    out = []
    out.append("/* cptable.h - 由 swinx/tools/gen_cptable.py 自动生成, 请勿手工修改。")
    out.append(" *")
    out.append(" * 用途: Android(bionic)/OHOS(musl) 的 iconv 只支持 Unicode 系编码")
    out.append(" * (UTF-8/UTF-16/UTF-32/US-ASCII/wchar_t), 完全不认识 GBK/CP936 与")
    out.append(" * Windows-125x 等传统代码页, 故 sysapi.cpp 中 to_unicode/to_mb 的")
    out.append(" * iconv_open 在这些平台上必然失败。本文件内置这些代码页的静态码表,")
    out.append(" * 供 iconv 不可用时兜底, 使 MultiByteToWideChar/WideCharToMultiByte")
    out.append(" * 在各平台上行为一致。")
    out.append(" *")
    out.append(" * 数据来源: CPython 内置编解码器 cp936 与 cp1250..cp1258(即 Unicode 官方")
    out.append(" * 发布的 Microsoft 代码页映射表), 无第三方库授权牵连。")
    out.append(" */")
    out.append("#ifndef SWINX_CPTABLE_H")
    out.append("#define SWINX_CPTABLE_H")
    out.append("")
    out.append("namespace swinx")
    out.append("{")
    out.append("// CP936(GBK) 双字节区: 首字节 0x81..0xFE, 尾字节 0x40..0x7E 与 0x80..0xFE")
    out.append("// (共 190 个, 0x7F 保留), 索引 = (首字节 - kCP936LeadFirst) * kCP936TrailCount")
    out.append("// + 尾字节序号; 表值为 0 表示该编码未定义。")
    out.append("enum")
    out.append("{")
    out.append("    kCP936LeadFirst = 0x%02X," % LEAD_FIRST)
    out.append("    kCP936LeadLast = 0x%02X," % LEAD_LAST)
    out.append("    kCP936LeadCount = kCP936LeadLast - kCP936LeadFirst + 1,")
    out.append("    kCP936TrailCount = %d," % TRAIL_COUNT)
    out.append("    kCP936TableSize = kCP936LeadCount * kCP936TrailCount,")
    out.append("};")
    out.extend(format_array("kCP936ToUnicode[kCP936TableSize]", build_cp936()))
    out.append("")

    for cp in range(1250, 1259):
        out.append("// Windows-%d 单字节页: 索引 = 字节 - 0x80, 表值为 0 表示未定义。" % cp)
        out.extend(format_array("kCP%dToUnicode[128]" % cp, build_single_byte(cp)))
        out.append("")

    out.append("} // namespace swinx")
    out.append("")
    out.append("#endif // SWINX_CPTABLE_H")

    text = "\r\n".join(out) + "\r\n"
    with open(out_path, "wb") as f:
        f.write(b"\xef\xbb\xbf" + text.encode("utf-8"))

    print("wrote %s (%d bytes)" % (out_path, os.path.getsize(out_path)))


if __name__ == "__main__":
    main()
