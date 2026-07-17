#!/usr/bin/env python3
"""查询性能 benchmark — 测量 D-Bus 方法的 P50/P95/Max 延迟。

用法:
    python3 bench_queries.py --scale S4 --db /path/to/awareness.db
    python3 bench_queries.py --count 100000

输出: results_queries_<date>.json
"""

import argparse
import dbus
import json
import os
import statistics
import subprocess
import sys
import time
from datetime import datetime

SERVICE = "org.deepin.EnvironmentAwareness"
PATH = "/org/deepin/EnvironmentAwareness"

# 预期阈值 (ms)
THRESHOLDS = {
    "S1": {"p50": 2, "p95": 5, "max": 10},
    "S2": {"p50": 10, "p95": 30, "max": 50},
    "S3": {"p50": 30, "p95": 80, "max": 150},
    "S4": {"p50": 60, "p95": 150, "max": 300},
    "S5": {"p50": 200, "p95": 500, "max": 1000},
}


def get_dbus_iface():
    bus = dbus.SessionBus()
    proxy = bus.get_object(SERVICE, PATH)
    history = dbus.Interface(proxy, "org.deepin.EnvironmentAwareness.History")
    return history


def time_call(fn, *args, **kwargs):
    """调用 D-Bus 方法并返回延迟(ms)。"""
    t0 = time.perf_counter()
    fn(*args, **kwargs)
    t1 = time.perf_counter()
    return (t1 - t0) * 1000


def bench_method(fn, args_list, runs=10):
    """对同一调用重复 runs 次，返回 P50/P95/Max。"""
    latencies = []
    for _ in range(runs):
        lat = time_call(fn, *args_list)
        latencies.append(lat)
    latencies.sort()
    return {
        "p50": round(statistics.median(latencies), 2),
        "p95": round(latencies[int(len(latencies) * 0.95)], 2),
        "max": round(max(latencies), 2),
        "min": round(min(latencies), 2),
        "raw": [round(l, 2) for l in latencies],
    }


def make_filter(d):
    """将 Python dict 转为 dbus 兼容的 a{sv} dict。"""
    result = {}
    for k, v in d.items():
        if isinstance(v, int) and abs(v) > 2**31:
            result[k] = dbus.Int64(v)
        elif isinstance(v, int):
            result[k] = dbus.Int32(v)
        else:
            result[k] = v
    return result

def bench_query_actions(iface, now_ms):
    scenarios = [
        ("全量扫描", {}),
        ("时间范围1h", {"since": now_ms - 3600000, "until": now_ms}),
        ("时间范围1d", {"since": now_ms - 86400000, "until": now_ms}),
        ("类型过滤window", {"type": "window"}),
        ("应用过滤chrome", {"app": "chrome"}),
        ("关键词搜索main", {"keyword": "main"}),
        ("组合过滤", {"type": "file", "app": "code",
                     "since": now_ms - 86400000, "until": now_ms}),
        ("分页limit100", {"limit": 100}),
        ("分页大offset", {"limit": 100, "offset": 500}),
    ]
    results = []
    for name, filt in scenarios:
        r = bench_method(iface.QueryActions, [make_filter(filt)], runs=10)
        results.append({"scenario": name, **r})
    return results


def bench_digest(iface, now_ms):
    scenarios = [
        ("1小时", now_ms - 3600000, now_ms),
        ("1天", now_ms - 86400000, now_ms),
        ("7天", now_ms - 7 * 86400000, now_ms),
    ]
    results = []
    for name, since, until in scenarios:
        r = bench_method(iface.GetActivityDigest,
                         [dbus.Int64(since), dbus.Int64(until)], runs=10)
        results.append({"scenario": name, **r})
    return results


def bench_stats(iface, now_ms):
    scenarios = [
        ("全量统计", {}),
        ("按天统计", {"since": now_ms - 86400000, "until": now_ms}),
        ("按应用统计", {"app": "chrome"}),
    ]
    results = []
    for name, filt in scenarios:
        r = bench_method(iface.GetActionStats, [make_filter(filt)], runs=10)
        results.append({"scenario": name, **r})
    return results


def bench_dbus_serialization(iface, now_ms):
    """测量 D-Bus 端到端延迟 vs 纯 SQL 延迟。"""
    db_path = os.path.expanduser(
        "~/.local/share/deepin/environment-awareness/awareness.db"
    )

    # D-Bus 调用
    dbus_latencies = []
    for _ in range(10):
        t0 = time.perf_counter()
        iface.GetActivityDigest(dbus.Int64(now_ms - 7 * 86400000), dbus.Int64(now_ms))
        t1 = time.perf_counter()
        dbus_latencies.append((t1 - t0) * 1000)

    # 纯 SQL 查询（apps 子查询）
    import sqlite3
    sql_latencies = []
    since = now_ms - 7 * 86400000
    for _ in range(10):
        conn = sqlite3.connect(db_path)
        c = conn.cursor()
        t0 = time.perf_counter()
        c.execute(
            "SELECT app_name, window_title, MIN(timestamp), MAX(timestamp), COUNT(*) "
            "FROM actions WHERE app_name IS NOT NULL AND app_name != '' "
            "AND window_title IS NOT NULL AND window_title != '' "
            "AND timestamp >= ? AND timestamp <= ? "
            "GROUP BY app_name, window_title ORDER BY MIN(timestamp)",
            (since, now_ms)
        )
        c.fetchall()
        t1 = time.perf_counter()
        sql_latencies.append((t1 - t0) * 1000)
        conn.close()

    dbus_median = statistics.median(dbus_latencies)
    sql_median = statistics.median(sql_latencies)
    overhead = dbus_median - sql_median

    return [{
        "scenario": "7天 GetActivityDigest",
        "dbus_p50": round(dbus_median, 2),
        "sql_p50": round(sql_median, 2),
        "overhead_p50": round(overhead, 2),
        "raw_dbus": [round(l, 2) for l in dbus_latencies],
        "raw_sql": [round(l, 2) for l in sql_latencies],
    }]


def main():
    parser = argparse.ArgumentParser(description="查询性能 benchmark")
    parser.add_argument("--scale", default="S4", help="规模标签 (用于阈值对比)")
    parser.add_argument("--runs", type=int, default=10, help="每场景重复次数")
    parser.add_argument("--output", default=None, help="输出文件路径")
    args = parser.parse_args()

    iface = get_dbus_iface()
    now_ms = int(time.time() * 1000)
    scale = args.scale

    print(f"=== 查询性能 Benchmark (scale={scale}) ===")
    results = {
        "meta": {
            "scale": scale,
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

    # 1. QueryActions
    print("\n[1/4] QueryActions...")
    qa_results = bench_query_actions(iface, now_ms)
    for r in qa_results:
        r["method"] = "QueryActions"
        r["scale"] = scale
        r["unit"] = "ms"
        results["queries"].append(r)
        print(f"  {r['scenario']}: P50={r['p50']}ms P95={r['p95']}ms Max={r['max']}ms")

    # 2. GetActivityDigest
    print("\n[2/4] GetActivityDigest...")
    digest_results = bench_digest(iface, now_ms)
    for r in digest_results:
        r["method"] = "GetActivityDigest"
        r["scale"] = scale
        r["unit"] = "ms"
        results["queries"].append(r)
        print(f"  {r['scenario']}: P50={r['p50']}ms P95={r['p95']}ms Max={r['max']}ms")

    # 3. GetActionStats
    print("\n[3/4] GetActionStats...")
    stats_results = bench_stats(iface, now_ms)
    for r in stats_results:
        r["method"] = "GetActionStats"
        r["scale"] = scale
        r["unit"] = "ms"
        results["queries"].append(r)
        print(f"  {r['scenario']}: P50={r['p50']}ms P95={r['p95']}ms Max={r['max']}ms")

    # 4. D-Bus 序列化开销
    print("\n[4/4] D-Bus 序列化开销...")
    serial_results = bench_dbus_serialization(iface, now_ms)
    for r in serial_results:
        r["method"] = "DBusSerialization"
        r["scale"] = scale
        r["unit"] = "ms"
        results["queries"].append(r)
        print(f"  {r['scenario']}: D-Bus P50={r['dbus_p50']}ms SQL P50={r['sql_p50']}ms "
              f"开销={r['overhead_p50']}ms")

    # 保存结果
    output = args.output or f"results_queries_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
    with open(output, "w") as f:
        json.dump(results, f, indent=2, ensure_ascii=False)
    print(f"\n结果已保存: {output}")


if __name__ == "__main__":
    main()
