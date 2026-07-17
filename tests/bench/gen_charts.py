#!/usr/bin/env python3
"""从 benchmark 结果 JSON 生成 PNG + HTML 图表。

用法:
    python3 gen_charts.py results_2026-07-17.json
    python3 gen_charts.py results1.json results2.json --output-dir charts/

输出:
    charts/<date>/*.png          — 静态图表
    charts/<date>/dashboard.html — 交互式仪表盘
    charts/<date>/report.md      — Markdown 报告
"""
import argparse
import json
import os
import sys
from datetime import datetime

import matplotlib
matplotlib.use("Agg")  # 无头模式
matplotlib.rcParams["font.sans-serif"] = [
    "Noto Sans CJK JP", "Droid Sans Fallback", "Unifont",
    "DejaVu Sans"
]
matplotlib.rcParams["axes.unicode_minus"] = False
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

# 颜色
COLOR_PASS = "#2ecc71"
COLOR_WARN = "#f39c12"
COLOR_FAIL = "#e74c3c"
COLOR_PRIMARY = "#3498db"
COLOR_SECONDARY = "#9b59b6"
COLOR_ACCENT = "#e67e22"

# 阈值
THRESHOLDS_QUERY = {
    "S1": {"p50": 2, "p95": 5, "max": 10},
    "S2": {"p50": 10, "p95": 30, "max": 50},
    "S3": {"p50": 30, "p95": 80, "max": 150},
    "S4": {"p50": 60, "p95": 150, "max": 300},
    "S5": {"p50": 200, "p95": 500, "max": 1000},
}
THRESHOLDS_SIGNAL = {
    "WindowChanged": {"p50": 20, "p95": 50},
    "ClipboardChanged": {"p50": 20, "p95": 50},
    "FileOpened": {"p50": 30, "p95": 80},
}
THRESHOLDS_SENSOR = {
    "recall": 95,
    "precision": 90,
    "app_name_acc": 98,
}
THRESHOLDS_RSS_INITIAL = 150  # MB (Qt+X11 baseline ~114MB)
THRESHOLDS_RSS_2H = 200  # MB


def status_color(value, threshold, higher_is_worse=True):
    if threshold is None:
        return COLOR_PRIMARY
    if higher_is_worse:
        if value > threshold * 1.5:
            return COLOR_FAIL
        elif value > threshold:
            return COLOR_WARN
    else:
        if value < threshold * 0.5:
            return COLOR_FAIL
        elif value < threshold:
            return COLOR_WARN
    return COLOR_PASS


def save_png(fig, output_dir, name):
    path = os.path.join(output_dir, f"{name}.png")
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    return path


def gen_query_latency_png(queries, output_dir, scale="S4"):
    """查询延迟分组柱状图。"""
    # 按方法分组
    methods = {}
    for q in queries:
        m = q.get("method", "Unknown")
        if m not in methods:
            methods[m] = []
        methods[m].append(q)

    fig, axes = plt.subplots(len(methods), 1, figsize=(12, 5 * len(methods)))
    if len(methods) == 1:
        axes = [axes]

    threshold = THRESHOLDS_QUERY.get(scale, THRESHOLDS_QUERY["S4"])

    for ax_idx, (method, items) in enumerate(methods.items()):
        ax = axes[ax_idx]
        scenarios = [q["scenario"] for q in items]
        p50s = [q.get("p50", 0) for q in items]
        p95s = [q.get("p95", 0) for q in items]

        x = np.arange(len(scenarios))
        width = 0.35

        bars50 = ax.bar(x - width/2, p50s, width, label="P50", color=COLOR_PRIMARY, alpha=0.8)
        bars95 = ax.bar(x + width/2, p95s, width, label="P95", color=COLOR_SECONDARY, alpha=0.8)

        # 阈值线
        ax.axhline(y=threshold["p50"], color=COLOR_PASS, linestyle="--",
                   alpha=0.5, label=f"P50阈值 {threshold['p50']}ms")
        ax.axhline(y=threshold["p95"], color=COLOR_WARN, linestyle="--",
                   alpha=0.5, label=f"P95阈值 {threshold['p95']}ms")

        # 数值标注
        for bar in bars50:
            h = bar.get_height()
            ax.text(bar.get_x() + bar.get_width()/2, h, f"{h:.1f}",
                    ha="center", va="bottom", fontsize=8)
        for bar in bars95:
            h = bar.get_height()
            ax.text(bar.get_x() + bar.get_width()/2, h, f"{h:.1f}",
                    ha="center", va="bottom", fontsize=8)

        ax.set_xlabel("场景")
        ax.set_ylabel("延迟 (ms)")
        ax.set_title(f"{method} 延迟 (scale={scale})")
        ax.set_xticks(x)
        ax.set_xticklabels(scenarios, rotation=30, ha="right")
        ax.legend(loc="upper left", fontsize=8)
        ax.grid(axis="y", alpha=0.3)

    plt.tight_layout()
    return save_png(fig, output_dir, "query_latency")


def gen_digest_latency_png(queries, output_dir):
    """GetActivityDigest 延迟折线图。"""
    digest_items = [q for q in queries if q.get("method") == "GetActivityDigest"]
    if not digest_items:
        return None

    fig, ax = plt.subplots(figsize=(10, 6))
    scenarios = [q["scenario"] for q in digest_items]
    p50s = [q.get("p50", 0) for q in digest_items]
    p95s = [q.get("p95", 0) for q in digest_items]
    maxs = [q.get("max", 0) for q in digest_items]

    ax.plot(scenarios, p50s, "o-", label="P50", color=COLOR_PASS, linewidth=2, markersize=8)
    ax.plot(scenarios, p95s, "s-", label="P95", color=COLOR_WARN, linewidth=2, markersize=8)
    ax.plot(scenarios, maxs, "^-", label="Max", color=COLOR_FAIL, linewidth=2, markersize=8)

    for i, (p50, p95, mx) in enumerate(zip(p50s, p95s, maxs)):
        ax.annotate(f"{p50:.1f}", (i, p50), textcoords="offset points",
                    xytext=(0, 10), ha="center", fontsize=9)

    ax.set_xlabel("时间范围")
    ax.set_ylabel("延迟 (ms)")
    ax.set_title("GetActivityDigest 延迟")
    ax.legend()
    ax.grid(True, alpha=0.3)
    plt.tight_layout()
    return save_png(fig, output_dir, "digest_latency")


def gen_signal_latency_png(signals, output_dir):
    """信号延迟条形图。"""
    if not signals:
        return None

    fig, ax = plt.subplots(figsize=(10, 5))
    names = [s["signal"] for s in signals]
    p50s = [s.get("p50", 0) for s in signals]
    p95s = [s.get("p95", 0) for s in signals]
    maxs = [s.get("max", 0) for s in signals]

    y = np.arange(len(names))
    height = 0.25

    ax.barh(y - height, p50s, height, label="P50", color=COLOR_PASS, alpha=0.8)
    ax.barh(y, p95s, height, label="P95", color=COLOR_WARN, alpha=0.8)
    ax.barh(y + height, maxs, height, label="Max", color=COLOR_FAIL, alpha=0.8)

    # 阈值线
    for s_idx, s in enumerate(signals):
        thr = THRESHOLDS_SIGNAL.get(s["signal"], {})
        if "p50" in thr:
            ax.axvline(x=thr["p50"], color=COLOR_PASS, linestyle="--", alpha=0.3)

    for i, (p50, p95, mx) in enumerate(zip(p50s, p95s, maxs)):
        ax.text(p50, i - height, f" {p50:.0f}", va="center", fontsize=8)
        ax.text(p95, i, f" {p95:.0f}", va="center", fontsize=8)
        ax.text(mx, i + height, f" {mx:.0f}", va="center", fontsize=8)

    ax.set_yticks(y)
    ax.set_yticklabels(names)
    ax.set_xlabel("延迟 (ms)")
    ax.set_title("信号投递延迟")
    ax.legend()
    ax.grid(axis="x", alpha=0.3)
    plt.tight_layout()
    return save_png(fig, output_dir, "signal_latency")


def gen_sensor_accuracy_png(sensors, output_dir):
    """Sensor 精确率柱状图。"""
    if not sensors:
        return None

    fig, ax = plt.subplots(figsize=(10, 6))
    names = [s["sensor"] for s in sensors]

    # 各指标
    metrics = ["recall", "precision", "app_name_acc", "title_acc", "action_acc", "dedup_effective"]
    metric_labels = ["Recall", "Precision", "appName准确率", "Title准确率", "Action准确率", "去重有效率"]

    x = np.arange(len(names))
    width = 0.13

    for m_idx, (metric, label) in enumerate(zip(metrics, metric_labels)):
        values = [s.get(metric, 0) for s in sensors]
        offset = (m_idx - len(metrics) / 2 + 0.5) * width
        ax.bar(x + offset, values, width, label=label, alpha=0.8)

    # 阈值线
    ax.axhline(y=95, color=COLOR_PASS, linestyle="--", alpha=0.5, label="Recall阈值 95%")
    ax.axhline(y=90, color=COLOR_WARN, linestyle="--", alpha=0.5, label="Precision阈值 90%")

    ax.set_xlabel("Sensor")
    ax.set_ylabel("百分比 (%)")
    ax.set_title("Sensor 精确率")
    ax.set_xticks(x)
    ax.set_xticklabels(names)
    ax.legend(loc="lower right", fontsize=8)
    ax.grid(axis="y", alpha=0.3)
    ax.set_ylim(0, 105)
    plt.tight_layout()
    return save_png(fig, output_dir, "sensor_accuracy")


def gen_rss_growth_png(resource, output_dir):
    """RSS 增长趋势折线图。"""
    rss_data = [r for r in resource if r.get("metric") == "RSS"]
    if not rss_data:
        return None

    fig, ax = plt.subplots(figsize=(10, 6))
    times = [r.get("time_min", 0) for r in rss_data]
    rss_mbs = [r.get("rss_mb", r.get("rss_kb", 0) / 1024) for r in rss_data]

    ax.plot(times, rss_mbs, "o-", color=COLOR_PRIMARY, linewidth=2, markersize=4)

    # 阈值线
    ax.axhline(y=THRESHOLDS_RSS_INITIAL, color=COLOR_PASS, linestyle="--",
               alpha=0.5, label=f"初始阈值 {THRESHOLDS_RSS_INITIAL}MB")
    ax.axhline(y=THRESHOLDS_RSS_2H, color=COLOR_WARN, linestyle="--",
               alpha=0.5, label=f"2h阈值 {THRESHOLDS_RSS_2H}MB")

    ax.set_xlabel("时间 (分钟)")
    ax.set_ylabel("RSS (MB)")
    ax.set_title("Daemon 内存占用趋势")
    ax.legend()
    ax.grid(True, alpha=0.3)

    # 标注最大值
    max_rss = max(rss_mbs)
    max_idx = rss_mbs.index(max_rss)
    ax.annotate(f"峰值: {max_rss:.1f}MB", (times[max_idx], max_rss),
                textcoords="offset points", xytext=(10, 10), fontsize=9,
                arrowprops=dict(arrowstyle="->", color=COLOR_FAIL))

    plt.tight_layout()
    return save_png(fig, output_dir, "rss_growth")


def gen_cpu_usage_png(resource, output_dir):
    """CPU 占用图。"""
    cpu_data = [r for r in resource if r.get("metric") == "CPU"]
    if not cpu_data:
        return None

    fig, ax = plt.subplots(figsize=(10, 6))
    scenarios = [r["scenario"] for r in cpu_data]
    avgs = [r.get("avg_cpu", 0) for r in cpu_data]
    maxs = [r.get("max_cpu", 0) for r in cpu_data]
    baselines = [r.get("baseline_cpu", 0) for r in cpu_data]

    x = np.arange(len(scenarios))
    width = 0.25

    ax.bar(x - width, baselines, width, label="基线", color=COLOR_PRIMARY, alpha=0.8)
    ax.bar(x, avgs, width, label="平均", color=COLOR_WARN, alpha=0.8)
    ax.bar(x + width, maxs, width, label="峰值", color=COLOR_FAIL, alpha=0.8)

    ax.axhline(y=5, color=COLOR_WARN, linestyle="--", alpha=0.5, label="5%阈值")

    for i, (b, a, m) in enumerate(zip(baselines, avgs, maxs)):
        ax.text(i - width, b, f"{b:.1f}", ha="center", va="bottom", fontsize=8)
        ax.text(i, a, f"{a:.1f}", ha="center", va="bottom", fontsize=8)
        ax.text(i + width, m, f"{m:.1f}", ha="center", va="bottom", fontsize=8)

    ax.set_xlabel("场景")
    ax.set_ylabel("CPU 占用率 (%)")
    ax.set_title("高频事件 CPU 占用")
    ax.set_xticks(x)
    ax.set_xticklabels(scenarios, rotation=15)
    ax.legend()
    ax.grid(axis="y", alpha=0.3)
    plt.tight_layout()
    return save_png(fig, output_dir, "cpu_usage")


def gen_db_size_png(resource, output_dir):
    """数据库体积图。"""
    db_data = [r for r in resource if r.get("metric") == "DBSize"]
    if not db_data:
        return None

    fig, ax = plt.subplots(figsize=(8, 5))
    d = db_data[0]
    sizes = [d.get("size_kb", 0), d.get("wal_kb", 0)]
    labels = ["DB", "WAL"]
    colors = [COLOR_PRIMARY, COLOR_ACCENT]

    bars = ax.bar(labels, sizes, color=colors, alpha=0.8, width=0.5)
    for bar in bars:
        h = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2, h, f"{h:.1f}KB",
                ha="center", va="bottom", fontsize=10)

    ax.set_ylabel("大小 (KB)")
    ax.set_title(f"SQLite 数据库体积 (总计 {d.get('total_kb', 0):.1f}KB)")
    ax.grid(axis="y", alpha=0.3)
    plt.tight_layout()
    return save_png(fig, output_dir, "db_size")


def gen_summary_dashboard_png(output_dir, chart_paths):
    """2x3 仪表盘总览。"""
    valid = [p for p in chart_paths if p]
    if not valid:
        return None

    # 用子图排列
    fig, axes = plt.subplots(2, 3, figsize=(18, 10))
    fig.suptitle("Environment Awareness Benchmark Dashboard", fontsize=16, fontweight="bold")

    for idx, (ax, chart_path) in enumerate(zip(axes.flat, valid[:6])):
        try:
            img = plt.imread(chart_path)
            ax.imshow(img)
            ax.axis("off")
        except Exception:
            ax.text(0.5, 0.5, "N/A", ha="center", va="center")
            ax.axis("off")

    # 空白子图
    for ax in axes.flat[len(valid):]:
        ax.axis("off")

    plt.tight_layout()
    return save_png(fig, output_dir, "summary_dashboard")


def gen_html_dashboard(data, output_dir):
    """生成交互式 HTML 仪表盘。"""
    html = """<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<title>Environment Awareness Benchmark Dashboard</title>
<script src="https://cdn.jsdelivr.net/npm/echarts@5/dist/echarts.min.js"></script>
<style>
body { font-family: -apple-system, sans-serif; margin: 0; padding: 20px; background: #f5f5f5; }
.header { background: #2c3e50; color: white; padding: 20px; border-radius: 8px; margin-bottom: 20px; }
.header h1 { margin: 0; }
.header .meta { font-size: 13px; opacity: 0.8; margin-top: 8px; }
.tabs { display: flex; gap: 10px; margin-bottom: 20px; }
.tab { padding: 10px 20px; background: white; border-radius: 6px; cursor: pointer;
       border: 1px solid #ddd; transition: all 0.2s; }
.tab.active { background: #3498db; color: white; border-color: #3498db; }
.panel { display: none; }
.panel.active { display: block; }
.chart-container { background: white; border-radius: 8px; padding: 20px; margin-bottom: 20px;
                   box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
.chart { width: 100%; height: 400px; }
.grid { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
.threshold-table { width: 100%; border-collapse: collapse; font-size: 13px; }
.threshold-table th, .threshold-table td { padding: 8px 12px; border: 1px solid #ddd; text-align: left; }
.threshold-table th { background: #f0f0f0; }
.pass { color: #27ae60; font-weight: bold; }
.warn { color: #e67e22; font-weight: bold; }
.fail { color: #e74c3c; font-weight: bold; }
</style>
</head>
<body>
<div class="header">
  <h1>Environment Awareness Benchmark Dashboard</h1>
  <div class="meta">"""

    meta = data.get("meta", {})
    html += f"Date: {meta.get('date', 'N/A')} | Commit: {meta.get('commit', 'N/A')}"
    if "scale" in meta:
        html += f" | Scale: {meta['scale']}"

    html += """</div>
</div>

<div class="tabs">
  <div class="tab active" onclick="showTab('queries', this)">查询性能</div>
  <div class="tab" onclick="showTab('signals', this)">信号延迟</div>
  <div class="tab" onclick="showTab('sensors', this)">Sensor 精确率</div>
  <div class="tab" onclick="showTab('resource', this)">资源开销</div>
</div>

<div id="queries" class="panel active">
  <div class="chart-container"><div id="chart-query" class="chart"></div></div>
  <div class="chart-container"><div id="chart-digest" class="chart"></div></div>
</div>

<div id="signals" class="panel">
  <div class="chart-container"><div id="chart-signal" class="chart"></div></div>
</div>

<div id="sensors" class="panel">
  <div class="chart-container"><div id="chart-sensor" class="chart"></div></div>
</div>

<div id="resource" class="panel">
  <div class="chart-container"><div id="chart-rss" class="chart"></div></div>
  <div class="chart-container"><div id="chart-cpu" class="chart"></div></div>
  <div class="chart-container"><div id="chart-dbsize" class="chart"></div></div>
</div>

<script>
// 全局图表注册表
var charts = {};
function showTab(id, btn) {
  document.querySelectorAll('.panel').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  btn.classList.add('active');
  // 等待 panel 显示后再 resize 图表
  setTimeout(function() {
    var panel = document.getElementById(id);
    panel.querySelectorAll('.chart').forEach(function(el) {
      var ch = charts[el.id];
      if (ch) ch.resize();
    });
  }, 50);
}
</script>
"""

    # 查询性能图表数据
    queries = data.get("queries", [])
    query_methods = {}
    for q in queries:
        m = q.get("method", "Unknown")
        if m not in query_methods:
            query_methods[m] = []
        query_methods[m].append(q)

    # 查询性能 ECharts
    html += "<script>\n"
    charts_to_init = []

    if query_methods:
        methods = list(query_methods.keys())
        scenarios_all = set()
        for items in query_methods.values():
            for q in items:
                scenarios_all.add(q["scenario"])
        scenarios_all = sorted(scenarios_all)

        series = []
        for m in methods:
            for pct in ["p50", "p95"]:
                values = []
                for sc in scenarios_all:
                    found = None
                    for q in query_methods[m]:
                        if q["scenario"] == sc:
                            found = q
                            break
                    values.append(found.get(pct, 0) if found else 0)
                series.append({
                    "name": f"{m} {pct.upper()}",
                    "type": "bar",
                    "data": values,
                })

        html += "charts['chart-query'] = echarts.init(document.getElementById('chart-query'));\n"
        html += f"charts['chart-query'].setOption({{\n"
        html += f"  title: {{ text: '查询延迟对比' }},\n"
        html += f"  tooltip: {{ trigger: 'axis' }},\n"
        html += f"  legend: {{ top: 30 }},\n"
        html += f"  xAxis: {{ type: 'category', data: {json.dumps(scenarios_all, ensure_ascii=False)} }},\n"
        html += f"  yAxis: {{ type: 'value', name: 'ms' }},\n"
        html += f"  series: {json.dumps(series, ensure_ascii=False)}\n"
        html += f"}});\n"

    html += "charts['chart-digest'] = echarts.init(document.getElementById('chart-digest'));\n"
    digest_items = [q for q in queries if q.get("method") == "GetActivityDigest"]
    if digest_items:
        scenarios = [q["scenario"] for q in digest_items]
        html += f"charts['chart-digest'].setOption({{\n"
        html += f"  title: {{ text: 'GetActivityDigest 延迟' }},\n"
        html += f"  tooltip: {{ trigger: 'axis' }},\n"
        html += f"  legend: {{ data: ['P50','P95','Max'] }},\n"
        html += f"  xAxis: {{ type: 'category', data: {json.dumps(scenarios, ensure_ascii=False)} }},\n"
        html += f"  yAxis: {{ type: 'value', name: 'ms' }},\n"
        html += f"  series: [\n"
        html += f"    {{ name:'P50', type:'line', data:{json.dumps([q.get('p50',0) for q in digest_items])} }},\n"
        html += f"    {{ name:'P95', type:'line', data:{json.dumps([q.get('p95',0) for q in digest_items])} }},\n"
        html += f"    {{ name:'Max', type:'line', data:{json.dumps([q.get('max',0) for q in digest_items])} }}\n"
        html += f"  ]\n"
        html += f"}});\n"

    # 信号延迟
    html += "charts['chart-signal'] = echarts.init(document.getElementById('chart-signal'));\n"
    signals = data.get("signals", [])
    if signals:
        signal_names = [s["signal"] for s in signals]
        html += f"charts['chart-signal'].setOption({{\n"
        html += f"  title: {{ text: '信号投递延迟' }},\n"
        html += f"  legend: {{ data: ['P50','P95','Max'] }},\n"
        html += f"  xAxis: {{ type: 'category', data: {json.dumps(signal_names, ensure_ascii=False)} }},\n"
        html += f"  yAxis: {{ type: 'value', name: 'ms' }},\n"
        html += f"  series: [\n"
        html += f"    {{ name:'P50', type:'bar', data:{json.dumps([s.get('p50',0) for s in signals])} }},\n"
        html += f"    {{ name:'P95', type:'bar', data:{json.dumps([s.get('p95',0) for s in signals])} }},\n"
        html += f"    {{ name:'Max', type:'bar', data:{json.dumps([s.get('max',0) for s in signals])} }}\n"
        html += f"  ]\n"
        html += f"}});\n"

    # Sensor 精确率
    html += "charts['chart-sensor'] = echarts.init(document.getElementById('chart-sensor'));\n"
    sensors = data.get("sensors", [])
    if sensors:
        sensor_names = [s["sensor"] for s in sensors]
        html += f"charts['chart-sensor'].setOption({{\n"
        html += f"  title: {{ text: 'Sensor 精确率' }},\n"
        html += f"  tooltip: {{ trigger: 'axis' }},\n"
        html += f"  legend: {{ data: ['Recall','Precision','appName准确率','Title准确率'] }},\n"
        html += f"  xAxis: {{ type: 'category', data: {json.dumps(sensor_names, ensure_ascii=False)} }},\n"
        html += f"  yAxis: {{ type: 'value', name: '%', max: 100 }},\n"
        html += f"  series: [\n"
        html += f"    {{ name:'Recall', type:'bar', data:{json.dumps([s.get('recall',0) for s in sensors])} }},\n"
        html += f"    {{ name:'Precision', type:'bar', data:{json.dumps([s.get('precision',0) for s in sensors])} }},\n"
        html += f"    {{ name:'appName准确率', type:'bar', data:{json.dumps([s.get('app_name_acc',0) for s in sensors])} }},\n"
        html += f"    {{ name:'Title准确率', type:'bar', data:{json.dumps([s.get('title_acc',0) for s in sensors])} }}\n"
        html += f"  ]\n"
        html += f"}});\n"

    # RSS 趋势
    html += "charts['chart-rss'] = echarts.init(document.getElementById('chart-rss'));\n"
    rss_data = [r for r in data.get("resource", []) if r.get("metric") == "RSS"]
    if rss_data:
        times = [r.get("time_min", 0) for r in rss_data]
        rss_mbs = [round(r.get("rss_mb", r.get("rss_kb", 0) / 1024), 1) for r in rss_data]
        html += f"charts['chart-rss'].setOption({{\n"
        html += f"  title: {{ text: 'Daemon 内存占用' }},\n"
        html += f"  tooltip: {{ trigger: 'axis' }},\n"
        html += f"  xAxis: {{ type: 'category', data: {json.dumps(times)} }},\n"
        html += f"  yAxis: {{ type: 'value', name: 'MB' }},\n"
        html += f"  series: [{{ type:'line', name:'RSS', data:{json.dumps(rss_mbs)}, "
        html += f"markLine: {{ data: [{{ yAxis: {THRESHOLDS_RSS_2H} }}] }} }}]\n"
        html += f"}});\n"

    # CPU
    html += "charts['chart-cpu'] = echarts.init(document.getElementById('chart-cpu'));\n"
    cpu_data = [r for r in data.get("resource", []) if r.get("metric") == "CPU"]
    if cpu_data:
        cpu_scenarios = [r["scenario"] for r in cpu_data]
        html += f"charts['chart-cpu'].setOption({{\n"
        html += f"  title: {{ text: '高频事件 CPU 占用' }},\n"
        html += f"  tooltip: {{ trigger: 'axis' }},\n"
        html += f"  legend: {{ data: ['基线','平均','峰值'] }},\n"
        html += f"  xAxis: {{ type: 'category', data: {json.dumps(cpu_scenarios, ensure_ascii=False)} }},\n"
        html += f"  yAxis: {{ type: 'value', name: '%' }},\n"
        html += f"  series: [\n"
        html += f"    {{ name:'基线', type:'bar', data:{json.dumps([r.get('baseline_cpu',0) for r in cpu_data])} }},\n"
        html += f"    {{ name:'平均', type:'bar', data:{json.dumps([r.get('avg_cpu',0) for r in cpu_data])} }},\n"
        html += f"    {{ name:'峰值', type:'bar', data:{json.dumps([r.get('max_cpu',0) for r in cpu_data])} }}\n"
        html += f"  ]\n"
        html += f"}});\n"

    # DB size
    html += "charts['chart-dbsize'] = echarts.init(document.getElementById('chart-dbsize'));\n"
    db_data = [r for r in data.get("resource", []) if r.get("metric") == "DBSize"]
    if db_data:
        d = db_data[0]
        html += f"charts['chart-dbsize'].setOption({{\n"
        html += f"  title: {{ text: '数据库体积' }},\n"
        html += f"  tooltip: {{ trigger: 'axis' }},\n"
        html += f"  xAxis: {{ type: 'category', data: ['DB','WAL'] }},\n"
        html += f"  yAxis: {{ type: 'value', name: 'KB' }},\n"
        html += f"  series: [{{ type:'bar', data:[{d.get('size_kb',0)},{d.get('wal_kb',0)}] }}]\n"
        html += f"}});\n"

    html += "\n// 初始化后对活动面板resize\n"
    html += "window.addEventListener('load', function() {\n"
    html += "  document.querySelectorAll('.panel.active .chart').forEach(function(el) {\n"
    html += "    var ch = charts[el.id];\n"
    html += "    if (ch) ch.resize();\n"
    html += "  });\n"
    html += "});\n"
    html += "window.addEventListener('resize', function() {\n"
    html += "  for (var id in charts) { if (charts[id]) charts[id].resize(); }\n"
    html += "});\n"
    html += "</script>\n</body>\n</html>"

    path = os.path.join(output_dir, "dashboard.html")
    with open(path, "w", encoding="utf-8") as f:
        f.write(html)
    return path


def gen_markdown_report(data, output_dir, chart_paths):
    """生成 Markdown 报告。"""
    meta = data.get("meta", {})
    queries = data.get("queries", [])
    signals = data.get("signals", [])
    sensors = data.get("sensors", [])
    resource = data.get("resource", [])

    scale = meta.get("scale", "S4")
    threshold = THRESHOLDS_QUERY.get(scale, THRESHOLDS_QUERY["S4"])

    report = f"# Benchmark Report\n\n"
    report += f"> 日期：{meta.get('date', 'N/A')}\n"
    report += f"> Commit：{meta.get('commit', 'N/A')}\n"
    report += f"> 规模：{scale}\n\n"

    # 查询性能
    report += "## 查询性能\n\n"
    report += f"阈值 (scale={scale}): P50<{threshold['p50']}ms, P95<{threshold['p95']}ms, Max<{threshold['max']}ms\n\n"
    report += "| Method | Scenario | P50 (ms) | P95 (ms) | Max (ms) | Status |\n"
    report += "|---|---|---|---|---|---|\n"
    for q in queries:
        method = q.get("method", "?")
        scenario = q.get("scenario", "?")
        p50 = q.get("p50", 0)
        p95 = q.get("p95", 0)
        mx = q.get("max", 0)
        status = "PASS"
        if p95 > threshold["p95"]:
            status = "FAIL"
        elif p50 > threshold["p50"]:
            status = "WARN"
        report += f"| {method} | {scenario} | {p50} | {p95} | {mx} | {status} |\n"

    # 图表引用
    if chart_paths.get("query_latency"):
        report += f"\n![查询延迟](query_latency.png)\n"
    if chart_paths.get("digest_latency"):
        report += f"![Digest延迟](digest_latency.png)\n"

    # 信号延迟
    if signals:
        report += "\n## 信号延迟\n\n"
        report += "| Signal | P50 (ms) | P95 (ms) | Max (ms) | Status |\n"
        report += "|---|---|---|---|---|\n"
        for s in signals:
            name = s.get("signal", "?")
            p50 = s.get("p50", 0)
            p95 = s.get("p95", 0)
            mx = s.get("max", 0)
            thr = THRESHOLDS_SIGNAL.get(name, {"p50": 50, "p95": 100})
            status = "PASS"
            if p95 > thr["p95"]:
                status = "FAIL"
            elif p50 > thr["p50"]:
                status = "WARN"
            report += f"| {name} | {p50} | {p95} | {mx} | {status} |\n"
        if chart_paths.get("signal_latency"):
            report += f"\n![信号延迟](signal_latency.png)\n"

    # Sensor 精确率
    if sensors:
        report += "\n## Sensor 精确率\n\n"
        report += "| Sensor | Recall (%) | Precision (%) | appName准确率 (%) | Title准确率 (%) | Status |\n"
        report += "|---|---|---|---|---|---|\n"
        for s in sensors:
            name = s.get("sensor", "?")
            recall = s.get("recall", 0)
            precision = s.get("precision", 0)
            app_acc = s.get("app_name_acc", 0)
            title_acc = s.get("title_acc", 0)
            status = "PASS"
            if recall < THRESHOLDS_SENSOR["recall"]:
                status = "FAIL"
            elif precision < THRESHOLDS_SENSOR["precision"]:
                status = "WARN"
            report += f"| {name} | {recall} | {precision} | {app_acc} | {title_acc} | {status} |\n"
        if chart_paths.get("sensor_accuracy"):
            report += f"\n![Sensor精确率](sensor_accuracy.png)\n"

    # 资源开销
    rss_data = [r for r in resource if r.get("metric") == "RSS"]
    cpu_data = [r for r in resource if r.get("metric") == "CPU"]
    db_data = [r for r in resource if r.get("metric") == "DBSize"]

    if rss_data or cpu_data or db_data:
        report += "\n## 资源开销\n\n"
        if rss_data:
            initial_rss = rss_data[0].get("rss_mb", 0)
            max_rss = max(r.get("rss_mb", 0) for r in rss_data)
            report += f"| Metric | Value | Threshold | Status |\n"
            report += f"|---|---|---|---|\n"
            report += f"| 初始 RSS | {initial_rss:.1f}MB | <{THRESHOLDS_RSS_INITIAL}MB | "
            report += f"{'PASS' if initial_rss < THRESHOLDS_RSS_INITIAL else 'FAIL'} |\n"
            report += f"| 峰值 RSS | {max_rss:.1f}MB | <{THRESHOLDS_RSS_2H}MB | "
            report += f"{'PASS' if max_rss < THRESHOLDS_RSS_2H else 'FAIL'} |\n"
            if chart_paths.get("rss_growth"):
                report += f"\n![RSS趋势](rss_growth.png)\n"

        if cpu_data:
            report += "\n| Scenario | 基线 CPU | 平均 CPU | 峰值 CPU | Status |\n"
            report += "|---|---|---|---|---|\n"
            for r in cpu_data:
                name = r.get("scenario", "?")
                baseline = r.get("baseline_cpu", 0)
                avg = r.get("avg_cpu", 0)
                mx = r.get("max_cpu", 0)
                status = "PASS"
                if avg > 10:
                    status = "FAIL"
                elif avg > 5:
                    status = "WARN"
                report += f"| {name} | {baseline:.1f}% | {avg:.1f}% | {mx:.1f}% | {status} |\n"
            if chart_paths.get("cpu_usage"):
                report += f"\n![CPU占用](cpu_usage.png)\n"

        if db_data:
            d = db_data[0]
            report += f"\n| Metric | Value |\n"
            report += f"|---|---|\n"
            report += f"| DB 大小 | {d.get('size_kb', 0):.1f}KB |\n"
            report += f"| WAL 大小 | {d.get('wal_kb', 0):.1f}KB |\n"
            report += f"| 总计 | {d.get('total_kb', 0):.1f}KB |\n"
            if chart_paths.get("db_size"):
                report += f"\n![数据库体积](db_size.png)\n"

    if chart_paths.get("summary_dashboard"):
        report += f"\n## 仪表盘总览\n\n![Dashboard](summary_dashboard.png)\n"

    report += f"\n## 交互式仪表盘\n\n打开 `dashboard.html` 查看交互式图表。\n"

    path = os.path.join(output_dir, "report.md")
    with open(path, "w", encoding="utf-8") as f:
        f.write(report)
    return path


def main():
    parser = argparse.ArgumentParser(description="生成 benchmark 图表")
    parser.add_argument("inputs", nargs="+", help="results JSON 文件路径")
    parser.add_argument("--output-dir", default=None, help="输出目录")
    args = parser.parse_args()

    # 合并多个结果文件
    merged = {
        "meta": {"date": datetime.now().isoformat(), "commit": ""},
        "queries": [],
        "signals": [],
        "sensors": [],
        "resource": [],
    }

    for fpath in args.inputs:
        with open(fpath) as f:
            data = json.load(f)
        if "meta" in data and not merged["meta"]["commit"]:
            merged["meta"]["commit"] = data["meta"].get("commit", "")
            merged["meta"]["scale"] = data["meta"].get("scale", "S4")
            merged["meta"]["date"] = data["meta"].get("date", merged["meta"]["date"])
        merged["queries"].extend(data.get("queries", []))
        merged["signals"].extend(data.get("signals", []))
        merged["sensors"].extend(data.get("sensors", []))
        merged["resource"].extend(data.get("resource", []))

    # 输出目录
    date_str = datetime.now().strftime("%Y-%m-%d")
    output_dir = args.output_dir or os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(__file__))),
        "tests", "bench", "charts", date_str
    )
    os.makedirs(output_dir, exist_ok=True)

    print(f"生成图表 → {output_dir}")

    scale = merged["meta"].get("scale", "S4")
    chart_paths = {}

    # PNG 图表
    print("  [1/8] query_latency.png...")
    chart_paths["query_latency"] = gen_query_latency_png(merged["queries"], output_dir, scale)

    print("  [2/8] digest_latency.png...")
    chart_paths["digest_latency"] = gen_digest_latency_png(merged["queries"], output_dir)

    print("  [3/8] signal_latency.png...")
    chart_paths["signal_latency"] = gen_signal_latency_png(merged["signals"], output_dir)

    print("  [4/8] sensor_accuracy.png...")
    chart_paths["sensor_accuracy"] = gen_sensor_accuracy_png(merged["sensors"], output_dir)

    print("  [5/8] rss_growth.png...")
    chart_paths["rss_growth"] = gen_rss_growth_png(merged["resource"], output_dir)

    print("  [6/8] cpu_usage.png...")
    chart_paths["cpu_usage"] = gen_cpu_usage_png(merged["resource"], output_dir)

    print("  [7/8] db_size.png...")
    chart_paths["db_size"] = gen_db_size_png(merged["resource"], output_dir)

    print("  [8/8] summary_dashboard.png...")
    chart_paths["summary_dashboard"] = gen_summary_dashboard_png(output_dir, [
        chart_paths.get("query_latency"),
        chart_paths.get("digest_latency"),
        chart_paths.get("signal_latency"),
        chart_paths.get("sensor_accuracy"),
        chart_paths.get("rss_growth"),
        chart_paths.get("cpu_usage"),
    ])

    # HTML 仪表盘
    print("  HTML dashboard.html...")
    html_path = gen_html_dashboard(merged, output_dir)

    # Markdown 报告
    print("  report.md...")
    report_path = gen_markdown_report(merged, output_dir, chart_paths)

    print(f"\n完成！输出:")
    print(f"  PNG 图表: {output_dir}/*.png")
    print(f"  HTML 仪表盘: {html_path}")
    print(f"  Markdown 报告: {report_path}")


if __name__ == "__main__":
    main()
