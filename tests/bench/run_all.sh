#!/bin/bash
# 一键运行 benchmark + 生成图表和报告
#
# 用法:
#   ./run_all.sh                    # 默认 S4 (100K)
#   ./run_all.sh --scale S2         # 指定规模
#   ./run_all.sh --skip-sensors     # 跳过 sensor 精确率测试
#   ./run_all.sh --skip-cpu         # 跳过 CPU 测试(省时间)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

SCALE="S4"
SKIP_SENSORS=false
SKIP_CPU=false
SKIP_SIGNALS=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --scale) SCALE="$2"; shift 2 ;;
        --skip-sensors) SKIP_SENSORS=true; shift ;;
        --skip-cpu) SKIP_CPU=true; shift ;;
        --skip-signals) SKIP_SIGNALS=true; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

DATE=$(date +%Y%m%d_%H%M%S)
DATE_SHORT=$(date +%Y-%m-%d)
OUTPUT_DIR="$SCRIPT_DIR/charts/$DATE_SHORT"
RESULT_DIR="$SCRIPT_DIR/results"
mkdir -p "$RESULT_DIR" "$OUTPUT_DIR"

echo "============================================"
echo "  Environment Awareness Benchmark"
echo "  Scale: $SCALE"
echo "  Date: $DATE"
echo "============================================"

# Step 1: 生成合成数据
echo ""
echo "[Step 1/5] 生成合成数据 ($SCALE)..."
python3 gen_actions.py --scale "$SCALE"

# Step 2: 查询性能
echo ""
echo "[Step 2/5] 查询性能 benchmark..."
python3 bench_queries.py --scale "$SCALE" --output "$RESULT_DIR/queries_$DATE.json"

# Step 3: 信号延迟
if [ "$SKIP_SIGNALS" = false ]; then
    echo ""
    echo "[Step 3/5] 信号延迟 benchmark..."
    python3 bench_signals.py --runs 15 --output "$RESULT_DIR/signals_$DATE.json" || true
else
    echo ""
    echo "[Step 3/5] 跳过信号延迟 benchmark"
fi

# Step 4: Sensor 精确率
if [ "$SKIP_SENSORS" = false ]; then
    echo ""
    echo "[Step 4/5] Sensor 精确率 benchmark..."
    python3 bench_sensors.py --output "$RESULT_DIR/sensors_$DATE.json" || true
else
    echo ""
    echo "[Step 4/5] 跳过 Sensor 精确率 benchmark"
fi

# Step 5: 资源开销
echo ""
echo "[Step 5/5] 资源开销 benchmark..."
if [ "$SKIP_CPU" = true ]; then
    python3 bench_resource.py --rss-only --rss-duration 2 --output "$RESULT_DIR/resource_$DATE.json"
else
    python3 bench_resource.py --rss-duration 2 --output "$RESULT_DIR/resource_$DATE.json" || true
fi

# 生成图表
echo ""
echo "============================================"
echo "  生成图表和报告"
echo "============================================"

python3 gen_charts.py \
    "$RESULT_DIR/queries_$DATE.json" \
    "$RESULT_DIR/resource_$DATE.json" \
    --output-dir "$OUTPUT_DIR"

# 如果有 signals 和 sensors 结果，也合并
if [ -f "$RESULT_DIR/signals_$DATE.json" ]; then
    python3 gen_charts.py \
        "$RESULT_DIR/queries_$DATE.json" \
        "$RESULT_DIR/signals_$DATE.json" \
        "$RESULT_DIR/sensors_$DATE.json" \
        "$RESULT_DIR/resource_$DATE.json" \
        --output-dir "$OUTPUT_DIR" || true
fi

echo ""
echo "============================================"
echo "  Benchmark 完成！"
echo "============================================"
echo ""
echo "结果文件:"
echo "  $RESULT_DIR/queries_$DATE.json"
echo "  $RESULT_DIR/signals_$DATE.json"
echo "  $RESULT_DIR/sensors_$DATE.json"
echo "  $RESULT_DIR/resource_$DATE.json"
echo ""
echo "图表和报告:"
echo "  $OUTPUT_DIR/*.png"
echo "  $OUTPUT_DIR/dashboard.html"
echo "  $OUTPUT_DIR/report.md"
