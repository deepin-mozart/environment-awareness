#!/usr/bin/env python3
"""资源开销 benchmark — 测量 daemon 长期运行内存占用 + 高频事件 CPU。

用法:
    python3 bench_resource.py           # 完整测试(2h RSS采样 + 60s CPU)
    python3 bench_resource.py --rss-only
    python3 bench_resource.py --cpu-only

输出: results_resource_<date>.json
"""

import argparse
import json
import os
import subprocess
import sys
import time
from datetime import datetime


def get_daemon_pid():
    try:
        out = subprocess.check_output(
            ["pgrep", "-f", "deepin-environment-awareness"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
        pids = [int(p) for p in out.split("\n") if p]
        return pids[0] if pids else None
    except subprocess.CalledProcessError:
        return None


def get_rss_kb(pid):
    try:
        out = subprocess.check_output(
            ["ps", "-o", "rss=", "-p", str(pid)]
        ).decode().strip()
        return int(out)
    except (subprocess.CalledProcessError, ValueError):
        return 0


def get_cpu_percent(pid):
    try:
        out = subprocess.check_output(
            ["ps", "-o", "%cpu=", "-p", str(pid)]
        ).decode().strip()
        return float(out)
    except (subprocess.CalledProcessError, ValueError):
        return 0.0


def bench_rss(duration_min=10, interval_sec=10):
    """采样 daemon RSS（KB），持续 duration_min 分钟。"""
    pid = get_daemon_pid()
    if not pid:
        print("ERROR: daemon 未运行", file=sys.stderr)
        return []

    print(f"RSS 采样: PID={pid}, 持续 {duration_min}min, 间隔 {interval_sec}s")
    samples = []
    total = duration_min * 60
    elapsed = 0
    while elapsed < total:
        rss = get_rss_kb(pid)
        samples.append({
            "time_sec": elapsed,
            "time_min": round(elapsed / 60, 1),
            "rss_kb": rss,
            "rss_mb": round(rss / 1024, 1),
        })
        if elapsed % 60 == 0:
            print(f"  [{elapsed//60}min] RSS={rss/1024:.1f}MB")
        time.sleep(interval_sec)
        elapsed += interval_sec

    return samples


def bench_cpu_high_freq(scenarios):
    """高频事件下测量 CPU 占用。"""
    pid = get_daemon_pid()
    if not pid:
        print("ERROR: daemon 未运行", file=sys.stderr)
        return []

    results = []
    for name, cmd, duration_sec, freq_hz in scenarios:
        print(f"\nCPU 测试: {name} ({freq_hz}Hz, {duration_sec}s)")
        # 基线 CPU
        baseline = get_cpu_percent(pid)
        print(f"  基线 CPU: {baseline:.1f}%")

        # 启动压力脚本
        proc = subprocess.Popen(cmd, shell=True, stderr=subprocess.DEVNULL)

        # 采样 CPU
        cpu_samples = []
        for i in range(duration_sec):
            time.sleep(1)
            cpu = get_cpu_percent(pid)
            cpu_samples.append(cpu)
            if (i + 1) % 10 == 0:
                print(f"  [{i+1}s] CPU={cpu:.1f}%")

        # 等待结束
        proc.terminate()
        proc.wait()

        avg_cpu = sum(cpu_samples) / len(cpu_samples) if cpu_samples else 0
        max_cpu = max(cpu_samples) if cpu_samples else 0

        results.append({
            "scenario": name,
            "freq_hz": freq_hz,
            "duration_sec": duration_sec,
            "baseline_cpu": round(baseline, 2),
            "avg_cpu": round(avg_cpu, 2),
            "max_cpu": round(max_cpu, 2),
            "raw_cpu": [round(c, 2) for c in cpu_samples],
        })
        print(f"  结果: avg={avg_cpu:.1f}% max={max_cpu:.1f}%")

    return results


def bench_db_size():
    """测量数据库文件体积。"""
    db_path = os.path.expanduser(
        "~/.local/share/deepin/environment-awareness/awareness.db"
    )
    if not os.path.exists(db_path):
        return {"size_kb": 0, "size_mb": 0, "path": db_path}

    size = os.path.getsize(db_path)
    # WAL 文件
    wal_path = db_path + "-wal"
    wal_size = os.path.getsize(wal_path) if os.path.exists(wal_path) else 0

    return {
        "size_kb": round(size / 1024, 1),
        "size_mb": round(size / 1024 / 1024, 2),
        "wal_kb": round(wal_size / 1024, 1),
        "total_kb": round((size + wal_size) / 1024, 1),
        "path": db_path,
    }


def main():
    parser = argparse.ArgumentParser(description="资源开销 benchmark")
    parser.add_argument("--rss-only", action="store_true")
    parser.add_argument("--cpu-only", action="store_true")
    parser.add_argument("--rss-duration", type=int, default=10, help="RSS采样分钟数")
    parser.add_argument("--rss-interval", type=int, default=10, help="RSS采样间隔秒")
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

    # RSS 采样
    if not args.cpu_only:
        rss_samples = bench_rss(args.rss_duration, args.rss_interval)
        for s in rss_samples:
            s["metric"] = "RSS"
            s["unit"] = "KB"
            results["resource"].append(s)

    # DB 体积
    db_info = bench_db_size()
    results["resource"].append({
        "metric": "DBSize",
        "size_kb": db_info["size_kb"],
        "size_mb": db_info["size_mb"],
        "wal_kb": db_info["wal_kb"],
        "total_kb": db_info["total_kb"],
        "unit": "KB",
    })
    print(f"\n数据库体积: {db_info['size_kb']}KB (WAL: {db_info['wal_kb']}KB)")

    # CPU 高频事件
    if not args.rss_only:
        scenarios = [
            # (name, command, duration_sec, freq_hz)
            ("窗口切换1Hz",
             "while true; do wmctrl -a '终端' 2>/dev/null; wmctrl -a '文件管理器' 2>/dev/null; sleep 1; done",
             60, 1),
            ("文件修改5Hz",
             "for i in $(seq 1 300); do echo $i > /tmp/bench_test_$((i%5)).txt 2>/dev/null; sleep 0.2; done",
             60, 5),
            ("剪贴板2Hz",
             "for i in $(seq 1 120); do xclip -selection clipboard <<< \"text_$i\" 2>/dev/null; sleep 0.5; done",
             60, 2),
        ]
        cpu_results = bench_cpu_high_freq(scenarios)
        for r in cpu_results:
            r["metric"] = "CPU"
            r["unit"] = "%"
            results["resource"].append(r)

    output = args.output or f"results_resource_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
    with open(output, "w") as f:
        json.dump(results, f, indent=2, ensure_ascii=False)
    print(f"\n结果已保存: {output}")


if __name__ == "__main__":
    main()
