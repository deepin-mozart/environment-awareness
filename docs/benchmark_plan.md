# 环境感知模块 Benchmark 方案

> 日期：2026-07-17
> 状态：方案设计，待实施

## 1. 目标

为 `deepin-environment-awareness` daemon 建立可重复、可回归的性能与质量基准，覆盖三个维度：

1. **查询性能** — D-Bus 方法在不同数据量下的延迟
2. **Sensor 精确率** — 事件捕获覆盖率与准确率
3. **资源开销** — daemon 长期运行的 CPU/内存占用

Benchmark 结果用于：回归检测（性能退化预警）、容量规划（数据保留天数 vs 查询延迟）、Sensor 调参依据。

## 2. 基准环境

| 项 | 值 |
|---|---|
| 硬件 | Intel i7-10700 @ 2.90GHz, 24GB RAM, SSD |
| OS | deepin (Linux 4.19) |
| Qt 版本 | 5.x (X11 会话) |
| 数据库 | SQLite, `~/.local/share/deepin/environment-awareness/awareness.db` |
| 采集工具 | `/usr/bin/time -v`, `sqlite3 .timer`, 自定义 D-Bus 客户端 |

## 3. 维度一：查询性能

### 3.1 测试数据生成

用合成数据填充 `actions` 表，模拟不同时间跨度下的记录量：

```
规模梯度：
  S1  = 1,000 条    (约 1 小时)
  S2  = 10,000 条   (约 1 天)
  S3  = 50,000 条   (约 3 天)
  S4  = 100,000 条  (约 7 天, 默认保留上限)
  S5  = 500,000 条  (压力测试, 超出保留范围)
```

**数据生成脚本要求**：
- `type` 分布：`window` 70%, `file` 15%, `clipboard` 10%, `input` 5%
- `app_name` 分布：模拟 10 个应用，power-law（chrome/terminal/code 占 60%）
- `window_title`：每个 app 3-8 个不同 title，模拟真实切换
- `timestamp`：在 `[now - span, now]` 内均匀分布，模拟用户白天密集、夜间稀疏
- `content_preview`：50% 为空，50% 为 20-200 字符随机文本
- `file_path`：file 类型 80% 有路径，20% 为空（模拟未捕获场景）

脚本位置：`tests/bench/gen_actions.py`，输出 SQL `INSERT` 语句可直接导入。

### 3.2 测试方法

每个方法在 5 个规模下各跑 10 次，取 P50/P95/Max。查询前后执行 `PRAGMA cache_size` 重置，避免缓存偏置。

#### 3.2.1 QueryActions

| 场景 | filter 参数 | 关注点 |
|---|---|---|
| 全量扫描 | `{}` | 无索引命中, 全表扫描基准 |
| 时间范围 | `{since, until}` 跨 1h/1d/7d | `idx_actions_timestamp` 命中 |
| 类型过滤 | `{type: "window"}` | `idx_actions_type_time` 命中 |
| 应用过滤 | `{app: "chrome"}` | `idx_actions_app_time` 命中 |
| 关键词搜索 | `{keyword: "main"}` | LIKE 全表扫描, 无索引 |
| 组合过滤 | `{type: "file", app: "code", since, until}` | 多索引交集 |
| 分页 | `{limit: 100, offset: N}` | 大 offset 性能衰减 |

**预期阈值**：
| 规模 | P50 目标 | P95 目标 | Max 上限 |
|---|---|---|---|
| S1 (1K) | < 2ms | < 5ms | < 10ms |
| S2 (10K) | < 10ms | < 30ms | < 50ms |
| S3 (50K) | < 30ms | < 80ms | < 150ms |
| S4 (100K) | < 60ms | < 150ms | < 300ms |
| S5 (500K) | < 200ms | < 500ms | < 1000ms |

超阈值说明需要加索引或优化查询。

#### 3.2.2 GetActivityDigest

3 个子查询（apps/files/clipboard）串联执行，测整体延迟。

| 场景 | 时间范围 | 关注点 |
|---|---|---|
| 1 小时摘要 | 1h span | 小范围快速返回 |
| 1 天摘要 | 1d span | 典型使用场景 |
| 7 天摘要 | 7d span | 最大保留范围 |
| 全量 | 全表 | 压力测试 |

**预期阈值**：
| 规模 | 1h P50 | 1d P50 | 7d P50 | 7d P95 |
|---|---|---|---|---|
| S2 (10K) | < 5ms | < 10ms | < 20ms | < 50ms |
| S3 (50K) | < 10ms | < 25ms | < 60ms | < 120ms |
| S4 (100K) | < 15ms | < 40ms | < 100ms | < 200ms |

**重点检查**：apps 子查询的 `GROUP BY app_name, window_title` 在大量不同 title 时的聚合开销。

#### 3.2.3 GetActionStats

| 场景 | filter | 关注点 |
|---|---|---|
| 全量统计 | `{}` | 全表聚合 |
| 按天统计 | `{since, until}` 1d | 时间分桶 |
| 按应用统计 | `{app: "chrome"}` | 应用维度聚合 |

**预期**：S4 规模下 P50 < 100ms, P95 < 250ms。超过说明需要预聚合表。

#### 3.2.4 GetBrowserHistory

直接读浏览器 SQLite 文件（不走本地 DB），测：
- 文件锁竞争（浏览器运行时 vs 关闭时）
- 大历史记录（10K+ visits）下的查询延迟

**预期**：100 条结果 P50 < 50ms（含文件打开 + 查询 + 关闭）。

### 3.3 D-Bus 序列化开销

`GetActivityDigest` 返回嵌套 `QVariantMap` + `QVariantList`，通过 D-Bus 传输时有序列化/反序列化开销。

**测试方法**：
1. 生成不同大小的返回 payload（10/50/100/500 个 apps 条目）
2. 客户端测量从调用到收到完整回复的端到端延迟
3. 对比纯 SQL 查询延迟，差值即为 D-Bus 开销

**预期**：500 条 apps 的端到端延迟 < SQL 延迟 + 50ms。超过说明序列化成为瓶颈。

### 3.4 信号投递延迟

从 Sensor 发出信号到客户端收到，测量端到端延迟。

**测试方法**：
1. 在 Sensor 内埋点（`emit eventPublished(event)` 前打 `QElapsedTimer`）
2. 客户端收到 D-Bus 信号后立即打时间戳
3. 差值即投递延迟（含 EventBus 队列 + D-Bus marshalling + 传输）

**预期**：
| 信号 | P50 目标 | P95 目标 |
|---|---|---|
| WindowChanged | < 20ms | < 50ms |
| FileOpened | < 30ms | < 80ms |
| ClipboardChanged | < 20ms | < 50ms |

## 4. 维度二：Sensor 精确率

### 4.1 WindowSensor

**目标**：测量窗口切换事件的捕获覆盖率。

**方法**：
1. 准备 ground truth 脚本：按预定义序列切换 20 个窗口（用 `wmctrl` 或 `xdotool`）
2. 每次切换后等待 3 秒，记录期望事件 `(app_name, window_title, timestamp)`
3. 查询 daemon 的 `actions` 表中 `type='window'` 记录
4. 对比：按 timestamp 顺序匹配，窗口内时间差 < 5 秒视为命中

**指标**：
| 指标 | 定义 |
|---|---|
| 召回率 (Recall) | 命中事件数 / 期望事件数 |
| 精确率 (Precision) | 正确匹配数 / 实际记录数 |
| app_name 准确率 | app_name 匹配的记录数 / 命中记录数 |
| window_title 准确率 | title 完全匹配的记录数 / 命中记录数 |

**预期**：Recall > 95%, Precision > 90%, app_name 准确率 > 98%。

**已知退化场景**（需标注）：
- 无窗口标题的应用（title 为空）
- 极短停留（< 2 秒切换，可能被 debounce 丢弃）
- 多显示器跨屏切换

### 4.2 FileSensor

**目标**：测量文件操作事件的捕获覆盖率。

**方法**：
1. 准备 ground truth 脚本：在监控目录下执行 20 个文件操作序列
   - 创建 5 个 .txt 文件
   - 修改 5 个（写入内容）
   - 用 deepin-editor 打开 3 个
   - 删除 2 个
   - 在非监控目录操作 5 个（对照组）
2. 等待 5 秒让 inotify 处理完毕
3. 查询 `actions` 表中 `type='file'` 记录
4. 对比期望事件

**指标**：
| 指标 | 定义 |
|---|---|
| 召回率 | 命中数 / 期望数（监控目录内） |
| 误报率 | 非监控目录事件数 / 实际记录数 |
| action 准确率 | action 字段正确数 / 命中数 |
| content_preview 覆盖率 | 有 preview 数 / 期望有 preview 数 |

**预期**：Recall > 90%, 误报率 < 5%, action 准确率 > 95%。

**已知退化场景**：
- inotify watch 上限（`ENOSPC`）
- 大量文件同时修改（inotify 事件风暴）
- 深层目录文件（watch_depth=2 限制）

### 4.3 ClipboardSensor

**目标**：测量剪贴板事件捕获的完整性和去重效果。

**方法**：
1. ground truth 脚本：模拟 30 次剪贴板操作
   - 20 次不同内容（文本/URL/代码片段）
   - 5 次相同内容（测试去重，间隔 < 5 秒应只记 1 次）
   - 5 次相同内容但间隔 > 10 秒（应各记 1 次）
2. 等待 2 秒
3. 查询 `actions` 表中 `type='clipboard'` 记录

**指标**：
| 指标 | 定义 |
|---|---|
| 召回率 | 去重后命中数 / 期望去重后数 |
| 去重有效率 | 被正确去重的重复数 / 期望去重数 |
| 误去重率 | 被错误丢弃的不同内容数 / 期望记录数 |

**预期**：Recall > 95%, 去重有效率 > 90%, 误去重率 = 0%。

## 5. 维度三：资源开销

### 5.1 长期运行内存占用

**方法**：启动 daemon 后持续运行 2 小时，期间正常使用系统，每 10 分钟采样一次 RSS。

```
采样命令: ps -o rss= -p $(pidof deepin-environment-awareness)
```

**指标**：
| 指标 | 目标 |
|---|---|
| 初始 RSS | < 150 MB (Qt+X11 baseline ~114MB) |
| 2h 后 RSS | < 200 MB |
| 增长率 | < 10 MB/h（无内存泄漏） |

**退化判定**：若 RSS 持续线性增长且不收敛，判定为内存泄漏，需用 valgrind/heaptrack 定位。

### 5.2 高频事件下 CPU 占有率

**方法**：用脚本高频触发事件，测量 daemon CPU 占用。

```
场景 1: 每秒切换 1 次窗口，持续 60 秒 (wmctrl)
场景 2: 每秒修改 5 个文件，持续 60 秒 (inotify 风暴)
场景 3: 每秒复制 2 次剪贴板，持续 60 秒
```

**指标**：
| 场景 | CPU 平均占用目标 | 峰值上限 |
|---|---|---|
| 窗口切换 1Hz | < 2% | < 5% |
| 文件修改 5Hz | < 5% | < 10% |
| 剪贴板 2Hz | < 1% | < 3% |

### 5.3 SQLite 数据库体积

**方法**：在不同记录量下测量 `.db` 文件大小。

```
S1 (1K):   预期 < 200 KB
S2 (10K):  预期 < 2 MB
S3 (50K):  预期 < 10 MB
S4 (100K): 预期 < 20 MB
```

若实际体积显著超标，检查是否未启用 WAL 或 page_size 过小。

## 6. 实施计划

### 6.1 脚本与工具

| 文件 | 用途 |
|---|---|
| `tests/bench/gen_actions.py` | 合成数据生成 |
| `tests/bench/bench_queries.py` | 查询性能测试（D-Bus 客户端） |
| `tests/bench/bench_signals.py` | 信号投递延迟测试 |
| `tests/bench/bench_sensors.py` | Sensor 精确率测试（ground truth 对比） |
| `tests/bench/bench_resource.py` | 资源开销采样 |
| `tests/bench/run_all.sh` | 一键运行 + 生成报告 |
| `tests/bench/gen_charts.py` | 从结果 JSON 生成 PNG + HTML 图表 |

### 6.2 报告格式

每次运行输出 Markdown 报告 `tests/bench/report_<date>.md`：

```markdown
# Benchmark Report <date>

## Environment
- commit: <hash>
- data size: S4 (100K actions)
- ...

## Query Performance
| Method | Scenario | P50 | P95 | Max | Threshold | Status |
|---|---|---|---|---|---|---|
| QueryActions | 全量扫描 | 55ms | 130ms | 280ms | <150ms | PASS |
| ... | ... | ... | ... | ... | ... | ... |

## Sensor Accuracy
| Sensor | Recall | Precision | Status |
|---|---|---|---|
| Window | 97.5% | 93.2% | PASS |
| ... | ... | ... | ... |

## Resource
| Metric | Value | Threshold | Status |
|---|---|---|---|
| Initial RSS | 18 MB | <20MB | PASS |
| ... | ... | ... | ... |
```

### 6.3 CI 集成（可选）

将 `run_all.sh` 接入 CI，每次 PR 自动运行 S2 规模（10K 条），超阈值标红。完整 S4 规模每周定时运行一次。

### 6.4 优先级

| 优先级 | 维度 | 理由 |
|---|---|---|
| P0 | 查询性能 (S2/S4) | 直接影响智能体调用体验，最易回归 |
| P1 | 资源开销 | daemon 长期运行，内存泄漏是致命问题 |
| P2 | Sensor 精确率 | 需要 ground truth 采集，实施成本较高 |
| P3 | D-Bus 序列化 | 仅在 payload 较大时才可能成为瓶颈 |
## 7. 图表生成

Benchmark 运行结束后，`gen_charts.py` 从结果 JSON 自动生成两套图表，输出到 `tests/bench/charts/<date>/`。

### 7.1 数据中间格式

所有 benchmark 脚本将结果写入统一的 `results_<date>.json`：

```json
{
  "meta": { "commit": "abc123", "date": "2026-07-17", "qt_version": "5.15" },
  "queries": [
    { "method": "QueryActions", "scenario": "全量扫描", "scale": "S1", "p50": 1.2, "p95": 3.5, "max": 8.0, "threshold_p50": 2.0, "unit": "ms" },
    ...
  ],
  "signals": [
    { "signal": "WindowChanged", "p50": 15, "p95": 40, "max": 65, "unit": "ms" }
  ],
  "sensors": [
    { "sensor": "Window", "recall": 97.5, "precision": 93.2, "app_name_acc": 98.1, "title_acc": 95.0, "unit": "%" }
  ],
  "resource": [
    { "metric": "RSS", "time_min": 0, "value": 18, "unit": "MB" },
    { "metric": "RSS", "time_min": 10, "value": 22, "unit": "MB" },
    ...
  ]
}
```

### 7.2 静态 PNG 图表（matplotlib）

生成以下图表，每张独立 PNG 文件：

| 图表 | 类型 | 内容 |
|---|---|---|
| `query_latency.png` | 分组柱状图 | X=规模 S1-S5, Y=延迟(ms), 按 method+scenario 分组, P50/P95 双柱, 阈值线标注 |
| `digest_latency.png` | 折线图 | X=时间范围(1h/1d/7d), Y=延迟, 4 条规模曲线 |
| `signal_latency.png` | 水平条形图 | 各信号的 P50/P95/Max, 阈值竖线 |
| `sensor_accuracy.png` | 分组柱状图 | 各 Sensor 的 Recall/Precision/app_name_acc/title_acc, 阈值线 95%/90% |
| `rss_growth.png` | 折线图 | X=时间(min), Y=RSS(MB), 阈值线 40MB, 增长趋势标注 |
| `cpu_usage.png` | 堆叠柱状图 | X=场景, Y=CPU%, 平均/峰值分层 |
| `db_size.png` | 对数柱状图 | X=规模, Y=DB体积(KB), 对数轴 |
| `summary_dashboard.png` | 2x3 仪表盘 | 上述核心图表缩略排列, 一页总览 |

**样式约定**：
- PASS 绿色 `#2ecc71`, WARN 黄色 `#f39c12`, FAIL 红色 `#e74c3c`
- 超阈值数据点用红色标注
- 每张图含标题、轴标签、图例、网格线

### 7.3 交互式 HTML 图表（ECharts）

生成单页 `dashboard.html`，内嵌 ECharts CDN（或离线 `echarts.min.js`），包含：

| 面板 | 交互能力 |
|---|---|
| 查询性能 | 切换 P50/P95/Max, 按 method 筛选, 悬停显示精确数值 |
| 信号延迟 | 条形图, 可切换信号类型 |
| Sensor 精确率 | 雷达图(Recall/Precision/app_name_acc/title_acc) |
| RSS 趋势 | 折线图, 可缩放时间轴, 阈值区域高亮 |
| CPU 占用 | 堆叠柱状图, 可切换场景 |
| DB 体积 | 对数柱状图, 可切换线性/对数轴 |

**特性**：
- 所有图表数据内嵌在 HTML 中（不依赖外部 JSON 文件）
- 支持 Tab 切换不同维度
- 顶部显示 commit/date/环境信息
- 超阈值指标自动红色高亮

### 7.4 依赖

| 依赖 | 用途 | 安装 |
|---|---|---|
| matplotlib >= 3.5 | PNG 生成 | `pip install matplotlib` |
| jinja2 >= 3.0 | HTML 模板 | `pip install jinja2` |
| ECharts 5.x | 交互图表 | 离线放 `tests/bench/vendor/echarts.min.js` |

### 7.5 触发方式

```bash
# 运行全部 benchmark + 生成图表
./tests/bench/run_all.sh --scale S4

# 仅生成图表（已有 results.json）
python3 tests/bench/gen_charts.py tests/bench/results_2026-07-17.json

# 输出
#   tests/bench/charts/2026-07-17/*.png
#   tests/bench/charts/2026-07-17/dashboard.html
#   tests/bench/report_2026-07-17.md
```


## 8. 已知限制

- **Wayland 适配**：当前所有 Sensor 为 X11 路径，benchmark 在 X11 会话下运行。Wayland 适配后需补充 Wayland benchmark。
- **浏览器历史**：`GetBrowserHistory` 直接读浏览器 SQLite，性能依赖浏览器自身数据库大小，不在合成数据覆盖范围内。
- **多用户**：benchmark 仅测试单用户会话，多用户并发场景未覆盖。
- **ground truth 主观性**：Sensor 精确率测试中，"期望事件"由人工定义，可能遗漏真实使用中的边缘场景。
