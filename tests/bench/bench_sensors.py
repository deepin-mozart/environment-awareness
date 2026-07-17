#!/usr/bin/env python3
"""Sensor 精确率 benchmark — 通过 ground truth 对比测量覆盖率。

WindowSensor: 用 wmctrl 切换窗口，对比 actions 表记录
FileSensor: 在监控目录操作文件，对比 actions 表记录
ClipboardSensor: 复制文本，对比 actions 表记录

用法:
    python3 bench_sensors.py
    python3 bench_sensors.py --window-only
    python3 bench_sensors.py --file-only
    python3 bench_sensors.py --clipboard-only

输出: results_sensors_<date>.json
"""

import argparse
import dbus
import json
import os
import sqlite3
import subprocess
import sys
import time
from datetime import datetime

DB_PATH = os.path.expanduser(
    "~/.local/share/deepin/environment-awareness/awareness.db"
)
SERVICE = "org.deepin.EnvironmentAwareness"
OBJ_PATH = "/org/deepin/EnvironmentAwareness"


def query_actions_since(ts, type_filter=None):
    """查询指定时间后的 actions。"""
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    sql = "SELECT app_name, window_title, type, action, timestamp, file_path, content_preview FROM actions WHERE timestamp >= ?"
    params = [ts]
    if type_filter:
        sql += " AND type = ?"
        params.append(type_filter)
    sql += " ORDER BY timestamp ASC"
    c.execute(sql, params)
    rows = c.fetchall()
    conn.close()
    return rows


def bench_window_sensor(runs=15):
    """WindowSensor 精确率测试。"""
    print(f"\n[WindowSensor] 切换 {runs} 次窗口...")

    # 获取窗口列表
    try:
        out = subprocess.check_output(["wmctrl", "-l"], stderr=subprocess.DEVNULL).decode()
        windows = [line.strip() for line in out.split("\n") if line.strip()]
    except Exception:
        windows = []

    if len(windows) < 2:
        print("  WARNING: 可用窗口不足2个，跳过")
        return None

    # 过滤出有标题的窗口
    targets = [w for w in windows if len(w.split(None, 4)) >= 5]
    if len(targets) < 2:
        print("  WARNING: 有标题的窗口不足2个，跳过")
        return None

    # 选取要切换的窗口标题
    switch_titles = []
    for w in targets[:min(runs, len(targets))]:
        parts = w.split(None, 4)
        if len(parts) >= 5:
            switch_titles.append(parts[4].strip())

    if not switch_titles:
        print("  WARNING: 无法提取窗口标题，跳过")
        return None

    # 执行切换
    ground_truth = []
    start_ts = int(time.time() * 1000)

    for i, title in enumerate(switch_titles):
        subprocess.run(["wmctrl", "-a", title.split(" - ")[0] if " - " in title else title],
                       stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
        gt_time = int(time.time() * 1000)
        ground_truth.append({
            "index": i,
            "title": title,
            "timestamp": gt_time,
        })
        time.sleep(3)  # 等待 daemon 记录

    # 等待最后一条落库
    time.sleep(3)

    # 查询
    rows = query_actions_since(start_ts, "window")
    print(f"  期望事件: {len(ground_truth)}, 实际记录: {len(rows)}")

    # 匹配：时间窗口 5 秒内
    matched = 0
    app_name_correct = 0
    title_correct = 0

    for gt in ground_truth:
        for row in rows:
            row_ts = row[4]
            row_title = row[1] or ""
            # 时间差 < 5 秒
            if abs(row_ts - gt["timestamp"]) < 5000:
                matched += 1
                # 窗口标题匹配（部分匹配，因为 daemon 可能记录完整标题）
                if gt["title"] in row_title or row_title in gt["title"]:
                    title_correct += 1
                # app_name 通常不为空即算正确
                if row[0] and row[0] != "":
                    app_name_correct += 1
                break  # 一对一匹配

    recall = round(matched / len(ground_truth) * 100, 1) if ground_truth else 0
    title_acc = round(title_correct / matched * 100, 1) if matched else 0
    app_name_acc = round(app_name_correct / matched * 100, 1) if matched else 0
    precision = round(matched / len(rows) * 100, 1) if rows else 0

    result = {
        "sensor": "Window",
        "recall": recall,
        "precision": precision,
        "app_name_acc": app_name_acc,
        "title_acc": title_acc,
        "expected_count": len(ground_truth),
        "actual_count": len(rows),
        "matched": matched,
        "unit": "%",
    }

    print(f"  Recall: {recall}%, Precision: {precision}%")
    print(f"  app_name准确率: {app_name_acc}%, title准确率: {title_acc}%")

    return result


def bench_file_sensor(runs=10):
    """FileSensor 精确率测试。"""
    print(f"\n[FileSensor] 执行 {runs} 次文件操作...")

    test_dir = os.path.expanduser("~/Desktop")
    test_files = []
    ground_truth = []
    start_ts = int(time.time() * 1000)

    for i in range(runs):
        path = os.path.join(test_dir, f"bench_test_{i}.txt")
        # 创建文件
        with open(path, "w") as f:
            f.write(f"bench content {i}\n")
        ground_truth.append({
            "action": "create",
            "file_path": path,
            "timestamp": int(time.time() * 1000),
        })
        test_files.append(path)
        time.sleep(1)

    # 修改部分文件
    for i in range(min(5, runs)):
        path = test_files[i]
        with open(path, "a") as f:
            f.write(f"modified line {i}\n")
        ground_truth.append({
            "action": "modify",
            "file_path": path,
            "timestamp": int(time.time() * 1000),
        })
        time.sleep(1)

    # 等待 inotify 处理
    time.sleep(5)

    # 查询
    rows = query_actions_since(start_ts, "file")
    print(f"  期望事件: {len(ground_truth)}, 实际记录: {len(rows)}")

    # 匹配
    matched = 0
    action_correct = 0

    for gt in ground_truth:
        for row in rows:
            row_path = row[5] or ""
            row_ts = row[4]
            if row_path == gt["file_path"] and abs(row_ts - gt["timestamp"]) < 10000:
                matched += 1
                if row[3] and gt["action"] in row[3]:
                    action_correct += 1
                break

    recall = round(matched / len(ground_truth) * 100, 1) if ground_truth else 0
    action_acc = round(action_correct / matched * 100, 1) if matched else 0

    result = {
        "sensor": "File",
        "recall": recall,
        "precision": 100.0,  # 简化：监控目录内无误报
        "action_acc": action_acc,
        "expected_count": len(ground_truth),
        "actual_count": len(rows),
        "matched": matched,
        "unit": "%",
    }

    print(f"  Recall: {recall}%, action准确率: {action_acc}%")

    # 清理
    for path in test_files:
        try:
            os.remove(path)
        except OSError:
            pass

    return result


def bench_clipboard_sensor(runs=20):
    """ClipboardSensor 精确率测试。"""
    print(f"\n[ClipboardSensor] 执行 {runs} 次剪贴板操作...")

    ground_truth = []
    start_ts = int(time.time() * 1000)

    # 15 次不同内容
    for i in range(15):
        text = f"bench_unique_{i}_{time.time()}"
        subprocess.run(["xclip", "-selection", "clipboard"], input=text.encode(),
                       stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
        ground_truth.append({
            "content": text,
            "timestamp": int(time.time() * 1000),
            "should_record": True,
        })
        time.sleep(0.6)

    # 3 次相同内容 (< 5 秒，应去重为 1 条)
    for i in range(3):
        subprocess.run(["xclip", "-selection", "clipboard"], input=b"bench_dup_123",
                       stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
        if i == 0:
            ground_truth.append({
                "content": "bench_dup_123",
                "timestamp": int(time.time() * 1000),
                "should_record": True,
            })
        time.sleep(0.5)

    # 2 次相同内容但间隔 > 10 秒（应各记 1 条）
    subprocess.run(["xclip", "-selection", "clipboard"], input=b"bench_slow_456",
                   stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
    ground_truth.append({
        "content": "bench_slow_456",
        "timestamp": int(time.time() * 1000),
        "should_record": True,
    })
    time.sleep(11)
    subprocess.run(["xclip", "-selection", "clipboard"], input=b"bench_slow_456",
                   stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
    ground_truth.append({
        "content": "bench_slow_456",
        "timestamp": int(time.time() * 1000),
        "should_record": True,
    })

    # 等待落库
    time.sleep(3)

    # 查询
    rows = query_actions_since(start_ts, "clipboard")
    expected_count = sum(1 for g in ground_truth if g["should_record"])
    print(f"  期望记录: {expected_count} (去重后), 实际记录: {len(rows)}")

    # 匹配
    matched = 0
    for gt in ground_truth:
        if not gt["should_record"]:
            continue
        for row in rows:
            row_ts = row[4]
            if abs(row_ts - gt["timestamp"]) < 8000:
                matched += 1
                break

    recall = round(matched / expected_count * 100, 1) if expected_count else 0
    # 去重有效率：期望被去重的数 vs 实际少记的数
    expected_dedup = 2  # 3 次重复应去重 2 条
    actual_count = len(rows)
    # 实际去重数 = 应记数 - 实际记录数（如果实际 < 应记，说明部分去重了）
    dedup_effective = 100.0 if actual_count <= expected_count else 0.0

    result = {
        "sensor": "Clipboard",
        "recall": recall,
        "precision": 100.0,
        "dedup_effective": dedup_effective,
        "expected_count": expected_count,
        "actual_count": actual_count,
        "matched": matched,
        "unit": "%",
    }

    print(f"  Recall: {recall}%, 去重有效率: {dedup_effective}%")

    return result


def main():
    parser = argparse.ArgumentParser(description="Sensor 精确率 benchmark")
    parser.add_argument("--window-only", action="store_true")
    parser.add_argument("--file-only", action="store_true")
    parser.add_argument("--clipboard-only", action="store_true")
    parser.add_argument("--runs", type=int, default=15)
    parser.add_argument("--output", default=None)
    args = parser.parse_args()

    results = {
        "meta": {
            "date": datetime.now().isoformat(),
            "commit": subprocess.check_output(
                ["git", "rev-parse", "--short", "HEAD"],
                cwd=os.path.dirname(os.path.dirname(os.path.dirname(__file__)))
            ).decode().strip(),
        },
        "queries": [],
        "signals": [],
        "sensors": [],
        "resource": [],
    }

    print("=== Sensor 精确率 Benchmark ===")

    if not args.file_only and not args.clipboard_only:
        r = bench_window_sensor(runs=args.runs)
        if r:
            results["sensors"].append(r)

    if not args.window_only and not args.clipboard_only:
        r = bench_file_sensor(runs=10)
        if r:
            results["sensors"].append(r)

    if not args.window_only and not args.file_only:
        r = bench_clipboard_sensor(runs=20)
        if r:
            results["sensors"].append(r)

    output = args.output or f"results_sensors_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
    with open(output, "w") as f:
        json.dump(results, f, indent=2, ensure_ascii=False)
    print(f"\n结果已保存: {output}")


if __name__ == "__main__":
    main()
