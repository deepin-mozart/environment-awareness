#!/usr/bin/env python3
"""信号投递延迟 benchmark — 从 Sensor 发出信号到客户端收到。

方法：用 dbus-monitor --profile 捕获信号时间戳，与触发时间对比。
用 select + 非阻塞读取避免 readline 阻塞。

用法:
    python3 bench_signals.py
    python3 bench_signals.py --runs 10

输出: results_signals_<date>.json
"""

import argparse
import json
import os
import select
import subprocess
import sys
import time
from datetime import datetime

SERVICE = "org.deepin.EnvironmentAwareness"
IFACE_EVENTS = "org.deepin.EnvironmentAwareness.Events"


def bench_signal(signal_name, trigger_fn, runs=10, timeout_sec=3):
    """用 dbus-monitor --profile 捕获信号时间戳。"""
    latencies = []

    for run_idx in range(runs):
        # 启动 dbus-monitor
        monitor = subprocess.Popen(
            ["dbus-monitor", "--session", "--profile"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL
        )

        # 设置非阻塞读取
        import fcntl
        fd = monitor.stdout.fileno()
        fl = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)

        # 等待 monitor 就绪
        time.sleep(0.2)

        # 清空缓冲
        try:
            while True:
                r, _, _ = select.select([monitor.stdout], [], [], 0.01)
                if not r:
                    break
                monitor.stdout.readline()
        except Exception:
            pass

        # 触发操作并记录时间
        t0 = time.time()
        try:
            trigger_fn()
        except Exception:
            monitor.terminate()
            monitor.wait()
            continue

        # 非阻塞轮询等待信号
        signal_received = False
        signal_time = None
        deadline = t0 + timeout_sec

        while time.time() < deadline:
            r, _, _ = select.select([monitor.stdout], [], [], 0.01)
            if r:
                try:
                    line = monitor.stdout.readline().decode(errors="replace").strip()
                    if line and signal_name in line and SERVICE in line and line.startswith("sig"):
                        parts = line.split("\t")
                        if len(parts) >= 2:
                            try:
                                signal_time = float(parts[1])
                                signal_received = True
                                break
                            except ValueError:
                                pass
                except Exception:
                    pass
            time.sleep(0.001)

        t1 = time.time()
        monitor.terminate()
        monitor.wait()

        if signal_received and signal_time:
            latency = (signal_time - t0) * 1000
            latencies.append(round(latency, 2))
            print(f"  [{signal_name} #{run_idx+1}] {latency:.1f}ms")
        else:
            print(f"  [{signal_name} #{run_idx+1}] TIMEOUT")

        time.sleep(0.3)

    return latencies


def trigger_window_switch():
    """触发窗口切换 — 在终端和文件管理器之间切换。"""
    # 获取当前窗口列表
    try:
        out = subprocess.check_output(["wmctrl", "-l"], stderr=subprocess.DEVNULL).decode()
        windows = [l.split(None, 4)[4] if len(l.split(None, 4)) >= 5 else l for l in out.strip().split("\n") if l]
    except Exception:
        windows = []

    # 选一个不同于当前的窗口来切换
    for w in windows:
        subprocess.run(["wmctrl", "-a", w], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
        time.sleep(0.1)
        break


def trigger_window_switch():
    """触发窗口切换 — 在终端和文件管理器之间交替。"""
    targets = ["终端", "文件管理器"]
    # 获取当前焦点
    try:
        current = subprocess.check_output(
            ["xdotool", "getwindowfocus", "getwindowname"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
    except Exception:
        current = ""

    # 切到另一个
    for t in targets:
        if t not in current:
            subprocess.run(["wmctrl", "-a", t], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
            return
    # fallback: 切到第一个
    subprocess.run(["wmctrl", "-a", targets[0]], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
def trigger_clipboard():
    """触发剪贴板变化。"""
    subprocess.run(
        f"echo bench_clip_{time.time()} | xclip -selection clipboard",
        shell=True, stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL
    )


def main():
    parser = argparse.ArgumentParser(description="信号投递延迟 benchmark")
    parser.add_argument("--runs", type=int, default=10)
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

    print("=== 信号投递延迟 Benchmark ===\n")

    # 1. WindowChanged
    print("[1/2] WindowChanged 信号延迟...")
    window_latencies = bench_signal("WindowChanged", trigger_window_switch, runs=args.runs)

    # 2. ClipboardChanged
    print("\n[2/2] ClipboardChanged 信号延迟...")
    clipboard_latencies = bench_signal("ClipboardChanged", trigger_clipboard, runs=args.runs)

    import statistics

    if window_latencies:
        window_result = {
            "signal": "WindowChanged",
            "p50": round(statistics.median(window_latencies), 2),
            "p95": round(sorted(window_latencies)[int(len(window_latencies) * 0.95)], 2),
            "max": round(max(window_latencies), 2),
            "min": round(min(window_latencies), 2),
            "raw": window_latencies,
            "unit": "ms",
        }
        results["signals"].append(window_result)
        print(f"\n  WindowChanged: P50={window_result['p50']}ms "
              f"P95={window_result['p95']}ms Max={window_result['max']}ms\n")

    if clipboard_latencies:
        clipboard_result = {
            "signal": "ClipboardChanged",
            "p50": round(statistics.median(clipboard_latencies), 2),
            "p95": round(sorted(clipboard_latencies)[int(len(clipboard_latencies) * 0.95)], 2),
            "max": round(max(clipboard_latencies), 2),
            "min": round(min(clipboard_latencies), 2),
            "raw": clipboard_latencies,
            "unit": "ms",
        }
        results["signals"].append(clipboard_result)
        print(f"\n  ClipboardChanged: P50={clipboard_result['p50']}ms "
              f"P95={clipboard_result['p95']}ms Max={clipboard_result['max']}ms\n")

    output = args.output or f"results_signals_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
    with open(output, "w") as f:
        json.dump(results, f, indent=2, ensure_ascii=False)
    print(f"结果已保存: {output}")


if __name__ == "__main__":
    main()
