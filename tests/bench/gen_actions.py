#!/usr/bin/env python3
"""合成数据生成器 — 向 awareness.db 插入指定规模的测试数据。

用法:
    python3 gen_actions.py --scale S4
    python3 gen_actions.py --count 50000 --db /path/to/awareness.db

规模梯度:
    S1=1K, S2=10K, S3=50K, S4=100K, S5=500K
"""

import argparse
import os
import random
import sqlite3
import sys
import time

SCALES = {
    "S1": 1_000,
    "S2": 10_000,
    "S3": 50_000,
    "S4": 100_000,
    "S5": 500_000,
}

# 10 个应用，power-law 分布（chrome/terminal/code 占 60%）
APPS = [
    ("chrome", 25, [
        "Google Gemini - Google Chrome",
        "研发终端信息.xlsx - Google Chrome",
        "统信软件 - Google Chrome",
        "Google 翻译 - Google Chrome",
        "新标签页 - Google Chrome",
    ]),
    ("deepin-terminal", 20, [
        "终端",
        "终端 — bash",
    ]),
    ("code", 15, [
        "main.cpp - pi - Visual Studio Code",
        "StorageController.cpp - pi - Visual Studio Code",
        "ClipboardSensor.cpp - pi - Visual Studio Code",
        "CMakeLists.txt - pi - Visual Studio Code",
    ]),
    ("deepin-editor", 8, [
        "config.yml — 文本编辑器",
        "main.cpp — 文本编辑器",
    ]),
    ("wps", 7, [
        "报告.docx - WPS 文字",
        "预算.xlsx - WPS 表格",
        "演示.pptx - WPS 演示",
    ]),
    ("sublime_merge", 6, [
        "~/project - Sublime Merge",
    ]),
    ("dde-file-manage", 5, [
        "文件管理器",
        "下载 — 文件管理器",
    ]),
    ("obsidian", 5, [
        "设计文档 - Obsidian",
        "笔记 - Obsidian",
    ]),
    ("Typora", 5, [
        "design.md - Typora",
        "README.md - Typora",
    ]),
    ("UIThread", 4, [
        "企业微信",
    ]),
]

FILE_PATHS = [
    "/home/user/project/main.cpp",
    "/home/user/project/StorageController.cpp",
    "/home/user/docs/design.md",
    "/home/user/docs/README.md",
    "/home/user/project/CMakeLists.txt",
    "/home/user/config.yml",
    "/home/user/notes.txt",
]

CONTENT_SAMPLES = [
    "",
    "",
    "",
    "",
    "int main() { return 0; }",
    "#include <iostream>",
    "def hello(): print('world')",
    "这是一个测试文档的内容预览",
    "SELECT * FROM actions WHERE type='window'",
    "https://example.com/page?id=123",
    "会议记录：讨论环境感知模块设计方案",
    "TODO: implement Wayland support",
]

TYPE_DIST = [
    ("window", 0.70, "switch"),
    ("file", 0.15, "modify"),
    ("clipboard", 0.10, "copy"),
    ("input", 0.05, "key_press"),
]


def gen_actions(count, span_ms=None):
    """生成 count 条 actions，返回 list of tuples。"""
    if span_ms is None:
        # 默认 7 天
        span_ms = 7 * 24 * 3600 * 1000
    now = int(time.time() * 1000)
    start = now - span_ms

    # 构建 app 加权列表
    app_pool = []
    for app_name, weight, titles in APPS:
        for _ in range(weight):
            app_pool.append((app_name, titles))

    # 构建 type 加权列表
    type_pool = []
    for t, prob, action in TYPE_DIST:
        n = int(prob * 100)
        type_pool.extend([(t, action)] * n)

    actions = []
    for i in range(count):
        # 时间分布：白天密集(8h-22h)，夜间稀疏
        # 简化：均匀分布 + 随机抖动
        ts = start + int((i / count) * span_ms + random.gauss(0, span_ms * 0.01))
        ts = max(start, min(now, ts))

        t, action = random.choice(type_pool)
        app_name, titles = random.choice(app_pool)
        window_title = random.choice(titles) if random.random() > 0.05 else ""

        file_path = ""
        content_preview = ""
        if t == "file":
            file_path = random.choice(FILE_PATHS) if random.random() > 0.2 else ""
            content_preview = random.choice(CONTENT_SAMPLES)
        elif t == "clipboard":
            content_preview = random.choice(CONTENT_SAMPLES)
        elif t == "window":
            content_preview = window_title
        elif t == "input":
            content_preview = random.choice(CONTENT_SAMPLES)

        # metadata
        metadata = ""
        if t == "input":
            metadata = '{"pattern":"continuous_typing","duration_ms":5000,"key_count":23}'
        elif t == "clipboard":
            metadata = '{"content_type":"short_text","text_length":15}'

        actions.append((
            ts, t, action, app_name, random.randint(1000, 99999),
            window_title, file_path, content_preview, metadata
        ))

    return actions


def insert_actions(db_path, actions):
    conn = sqlite3.connect(db_path)
    c = conn.cursor()
    c.execute("PRAGMA journal_mode=WAL")
    c.execute("PRAGMA synchronous=NORMAL")

    c.executemany(
        "INSERT INTO actions (timestamp, type, action, app_name, app_pid, "
        "window_title, file_path, content_preview, metadata) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
        actions
    )
    conn.commit()
    conn.close()


def main():
    parser = argparse.ArgumentParser(description="生成合成测试数据")
    parser.add_argument("--scale", choices=list(SCALES.keys()), help="规模梯度")
    parser.add_argument("--count", type=int, help="直接指定条数")
    parser.add_argument("--db", default=None, help="数据库路径")
    parser.add_argument("--span-days", type=int, default=7, help="时间跨度(天)")
    args = parser.parse_args()

    count = args.count if args.count else SCALES[args.scale]

    if args.db:
        db_path = args.db
    else:
        db_path = os.path.expanduser(
            "~/.local/share/deepin/environment-awareness/awareness.db"
        )

    if not os.path.exists(db_path):
        print(f"ERROR: 数据库不存在: {db_path}", file=sys.stderr)
        sys.exit(1)

    print(f"生成 {count} 条合成数据 → {db_path}")

    # 先清理旧数据
    conn = sqlite3.connect(db_path)
    c = conn.cursor()
    old_count = c.execute("SELECT COUNT(*) FROM actions").fetchone()[0]
    c.execute("DELETE FROM actions")
    conn.commit()
    conn.close()
    print(f"已清理旧数据: {old_count} 条")

    actions = gen_actions(count, span_ms=args.span_days * 24 * 3600 * 1000)
    insert_actions(db_path, actions)

    # 验证
    conn = sqlite3.connect(db_path)
    c = conn.cursor()
    new_count = c.execute("SELECT COUNT(*) FROM actions").fetchone()[0]
    type_dist = c.execute("SELECT type, COUNT(*) FROM actions GROUP BY type").fetchall()
    conn.close()

    print(f"插入完成: {new_count} 条")
    print("类型分布:")
    for t, n in type_dist:
        print(f"  {t}: {n} ({n/new_count*100:.1f}%)")
    db_size = os.path.getsize(db_path)
    print(f"数据库体积: {db_size/1024:.1f} KB")


if __name__ == "__main__":
    main()
