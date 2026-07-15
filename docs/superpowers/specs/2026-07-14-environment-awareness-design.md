# 环境感知模块设计文档

> 日期：2026-07-14
> 状态：已确认，待实现

## 1. 概述

### 1.1 目标

为 deepin 系统自带智能体提供一个独立运行的**系统环境感知模块**。该模块作为后台 daemon 线程运行，持续感知系统变化和用户操作，通过 D-Bus 对外提供规范化的查询接口和事件信号，使智能体更了解用户上下文，从而提供更好的主动服务。

### 1.2 感知类型

模块区分两种感知类型：

| 类型 | 说明 | 示例 |
|---|---|---|
| **实时记录型** | Sensor 持续监听，事件异步写入 SQLite，智能体查询已记录数据 | 用户操作历史、窗口切换记录、剪贴板历史 |
| **实时获取型** | 智能体调用接口时，模块实时采集当前系统状态返回 | 当前活跃窗口、最近打开的文本文档、系统资源状态 |

### 1.3 设计原则

- **事件驱动 + 外部决策**：模块只采集和记录，不自行决策弹气泡；智能体订阅信号后自行决策
- **隐私优先**：内容脱敏后存储，键鼠只记模式不记字符，用户可控开关
- **规范化接口**：D-Bus 接口设计遵循 freedesktop 规范，供任意智能体调用
- **插件化传感器**：每个感知源独立线程，故障隔离，可独立开关
- **Wayland 一等公民**：deepin 25+ 默认 Wayland，所有 Sensor 以 Wayland 原生路径为主、X11 为 fallback，明确标注各 Sensor 在 Wayland 下的降级策略

## 2. 整体架构

```
EnvironmentAwareness Daemon (C++/Qt, systemd service)
├── Sensor 层 (各独立线程)
│   ├── WindowSensor      — KWin D-Bus (Wayland/X11 双路径)
│   ├── FileSensor        — inotify + XDG recent-files
│   ├── ClipboardSensor   — QClipboard 信号
│   ├── InputSensor       — Wayland: libinput/降级 | X11: XRecord
│   ├── SystemSensor      — /proc + upower 定时采集
│   └── BrowserSensor     — 浏览器历史/书签/标签页
├── EventBus              — Qt 信号槽, 异步事件分发
├── StorageController     — SQLite 读写 + 脱敏 + 生命周期管理 + 降采样
├── D-BusManager          — 对外接口 + 事件信号
└── RuleConfig            — 配置管理 + 热加载
```

### 数据流

```
[实时记录型]
Sensor → EventBus → StorageController → SQLite
                                        ↓
智能体 → D-Bus 查询 → D-BusManager → StorageController → SQLite → 返回结果

[实时获取型]
智能体 → D-Bus 查询 → D-BusManager → Sensor/系统API → 实时返回

[事件通知]
Sensor → EventBus → D-BusManager → D-Bus 信号 → 智能体(订阅)
```

## 3. D-Bus 接口设计

### 3.1 服务命名

```
Service:      org.deepin.EnvironmentAwareness
Object path:  /org/deepin/EnvironmentAwareness
```

### 3.2 接口划分

| 接口 | 用途 |
|---|---|
| `org.deepin.EnvironmentAwareness.Context` | 当前上下文查询（实时获取型） |
| `org.deepin.EnvironmentAwareness.History` | 历史操作查询（实时记录型） |
| `org.deepin.EnvironmentAwareness.System` | 系统状态查询 |
| `org.deepin.EnvironmentAwareness.Config` | 配置管理 |
| `org.deepin.EnvironmentAwareness.Events` | 事件信号（主动通知） |

### 3.3 Context 接口 — 当前上下文（实时获取）

| 方法 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `GetActiveWindow` | — | `a{sv}` | 当前活跃窗口：title, app_name, pid, wm_class, window_id, is_minimized |
| `GetRecentFiles` | `i:limit` | `aa{sv}` | 最近打开的文件列表 |
| `GetClipboardContent` | — | `a{sv}` | 当前剪贴板内容（文本+脱敏） |
| `GetRecentActions` | `i:limit` | `aa{sv}` | 最近N条操作记录（内存缓存快速返回） |
| `GetBrowserTabs` | — | `aa{sv}` | 当前浏览器标签页：url, title, favicon, tab_id |


### 3.4 History 接口 — 历史操作查询

| 方法 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `QueryActions` | `a{sv}:filter` | `aa{sv}` | 按条件查询：type, app, since, until, limit, offset |
| `QueryActionsByApp` | `s:app, i:limit` | `aa{sv}` | 查询特定应用的操作历史 |
| `GetActionStats` | `a{sv}:filter` | `a{sv}` | 统计：各类型操作次数、活跃时长、最常用应用 |
| `GetTimeline` | `i:since, i:until` | `aa{sv}` | 时间线视图：按时间排序的操作序列 |
| `GetRecentFile` | `i:limit` | `aa{sv}` | 实时获取最近编辑的文本文档（XDG recent + inotify） |
| `GetBrowserHistory` | `i:limit, s:keyword?` | `aa{sv}` | 查询浏览器历史记录（标题/URL/访问时间/访问次数） |
| `GetBrowserBookmarks` | `i:limit, s:folder?` | `aa{sv}` | 查询浏览器书签列表 |
| `SearchBrowserHistory` | `s:keyword, i:limit` | `aa{sv}` | 按关键词搜索浏览器历史记录 |

### 3.5 System 接口 — 系统状态


| 方法 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `GetSystemStatus` | — | `a{sv}` | CPU/内存/磁盘/网络/电量/屏幕亮度 |
| `GetNetworkInfo` | — | `a{sv}` | 连接状态、SSID、IP |
| `GetStorageInfo` | — | `a{sv}` | 磁盘使用率、读写速率 |

### 3.6 Config 接口 — 配置管理

| 方法 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `GetConfig` | — | `a{sv}` | 当前配置：保留天数、传感器开关、脱敏级别 |
| `SetConfig` | `a{sv}:config` | `b` | 更新配置（热生效） |
| `ClearHistory` | `i:before_timestamp` | `i` | 清理指定时间之前的记录，返回删除条数 |
| `ClearAll` | — | `i` | 清空所有记录，返回删除条数 |

### 3.7 Events 接口 — 事件信号（主动通知）

| 信号 | 参数 | 说明 |
|---|---|---|---|
| `WindowChanged` | `a{sv}:window_info` | 活跃窗口切换 |
| `FileOpened` | `a{sv}:file_info` | 文件被打开 |
| `FileModified` | `a{sv}:file_info` | 文件内容变化 |
| `ClipboardChanged` | `a{sv}:clipboard_info` | 剪贴板内容变化 |
| `InputPattern` | `a{sv}:pattern_info` | 输入模式识别（如"连续输入""快捷键按下"） |
| `SystemAlert` | `a{sv}:alert_info` | 系统状态异常（如电量低、磁盘满） |
| `BrowserTabChanged` | `a{sv}:tab_info` | 浏览器标签页切换或URL变化 |
| `BrowserNavigation` | `a{sv}:nav_info` | 浏览器导航到新页面（url, title, referrer） |
| `BrowserDownload` | `a{sv}:download_info` | 浏览器下载事件 |

模块不自行决策弹气泡，只发送事件信号。智能体订阅信号后自行决策。

### 3.8 调用示例

```cpp
// 实时获取型：获取最近打开的文本文档
QDBusInterface iface("org.deepin.EnvironmentAwareness",
                     "/org/deepin/EnvironmentAwareness",
                     "org.deepin.EnvironmentAwareness.History");
QDBusReply<QList<QVariantMap>> reply = iface.call("GetRecentFile", 10);
// 返回: [{path, name, app, last_modified, size, is_text}, ...]

// 浏览器历史记录查询
QDBusReply<QList<QVariantMap>> reply3 = iface.call("GetBrowserHistory", 20);

// 实时记录型：获取最近10条操作记录
QDBusReply<QList<QVariantMap>> reply2 = iface.call("GetRecentActions", 10);

// 订阅事件信号
QDBusConnection::sessionBus().connect(
    "org.deepin.EnvironmentAwareness",
    "/org/deepin/EnvironmentAwareness",
    "org.deepin.EnvironmentAwareness.Events",
    "WindowChanged",
    receiver, SLOT(onWindowChanged(QVariantMap)));
```

## 4. 数据模型（SQLite）

数据库路径：`~/.local/share/deepin/environment-awareness/awareness.db`
### 4.1 actions 表 — 操作记录

```sql
CREATE TABLE actions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL,          -- Unix时间戳(毫秒)
    type TEXT NOT NULL,                  -- window/file/clipboard/input/system/browser
    action TEXT NOT NULL,                -- open/close/switch/modify/copy/paste/key_press/...
    app_name TEXT,                       -- 相关应用名
    app_pid INTEGER,                     -- 相关进程PID
    window_title TEXT,                   -- 窗口标题
    file_path TEXT,                      -- 文件路径(file类型)
    content_preview TEXT,                -- 脱敏后内容预览(最多200字符)
    metadata TEXT                        -- JSON: 额外字段
);
CREATE INDEX idx_actions_type_time ON actions(type, timestamp);
CREATE INDEX idx_actions_app_time ON actions(app_name, timestamp);
CREATE INDEX idx_actions_timestamp ON actions(timestamp);
```

### 4.2 app_sessions 表 — 应用活跃时长

```sql
CREATE TABLE app_sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    app_name TEXT NOT NULL,
    pid INTEGER,
    window_title TEXT,
    start_time INTEGER NOT NULL,
    end_time INTEGER,
    duration_ms INTEGER
);
CREATE INDEX idx_sessions_app ON app_sessions(app_name);
CREATE INDEX idx_sessions_time ON app_sessions(start_time);
```

### 4.3 system_snapshots 表 — 系统状态快照

```sql
CREATE TABLE system_snapshots (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL,
    cpu_usage REAL,
    memory_usage REAL,
    disk_usage REAL,
    network_status TEXT,
    network_ssid TEXT,
    battery_level INTEGER,               -- 0-100, -1=无电池
    screen_brightness INTEGER,           -- 0-100
    metadata TEXT
);
CREATE INDEX idx_snapshots_time ON system_snapshots(timestamp);
```

### 4.4 browser_visits 表 — 浏览器访问记录

```sql
CREATE TABLE browser_visits (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL,          -- 访问时间(Unix毫秒)
    url TEXT NOT NULL,                   -- 页面URL
    title TEXT,                          -- 页面标题
    browser TEXT,                        -- 浏览器标识(deepin-browser/chrome/firefox)
    tab_id TEXT,                         -- 标签页ID
    visit_type TEXT,                     -- typed/link/reload/form_submit
    referrer TEXT,                       -- 来源页面
    visit_count INTEGER DEFAULT 1,       -- 累计访问次数
    metadata TEXT                        -- JSON: favicon, duration_sec等
);
CREATE INDEX idx_bvisits_time ON browser_visits(timestamp);
CREATE INDEX idx_bvisits_url ON browser_visits(url);
CREATE INDEX idx_bvisits_title ON browser_visits(title);
CREATE INDEX idx_bvisits_browser ON browser_visits(browser);
```

### 4.5 redaction_rules 表 — 脱敏规则

```sql
CREATE TABLE redaction_rules (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT UNIQUE NOT NULL,
    pattern TEXT NOT NULL,               -- 正则表达式
    replacement TEXT NOT NULL,           -- 替换文本
    enabled INTEGER DEFAULT 1
);
```

### 4.6 脱敏机制

StorageController 在写入 `content_preview` 前应用所有 enabled 的脱敏规则。内置规则：

| 规则名 | 匹配 | 替换 |
|---|---|---|
| phone | 手机号正则 | `[PHONE]` |
| id_card | 身份证号正则 | `[ID_CARD]` |
| email | 邮箱地址正则 | `[EMAIL]` |
| bank_card | 银行卡号正则 | `[BANK_CARD]` |
| secret | 密钥/token正则 | `[SECRET]` |

用户可通过 Config 接口增删脱敏规则。

### 4.7 数据生命周期

后台定时器每小时检查一次，删除 `timestamp < (now - retention_days * 86400000)` 的记录。保留天数通过 Config 可配置，默认 7 天。

**降采样策略**：不同表采用不同的生命周期策略，避免数据库膨胀：

| 表 | 保留策略 | 说明 |
|---|---|---|
| `actions` | 按保留天数删除 | 用户操作记录，默认7天 |
| `app_sessions` | 按保留天数删除 | 应用活跃时长，默认7天 |
| `browser_visits` | 按保留天数删除 | 浏览器访问记录，默认7天 |
| `system_snapshots` | 分级降采样 | 0-3天保留全量；3-7天每小时保留1条均值；超过7天删除 |

降采样在每小时清理定时器中执行：先聚合 3-7 天前的数据为每小时均值，再删除原始记录。采样间隔和降采样阈值通过 Config 可配置。

## 5. Sensor 实现方案

### 5.1 WindowSensor（窗口/应用切换）

**数据源（Wayland 优先，X11 fallback）**：
- **Wayland 主路径**：通过 `org.kde.KWin` D-Bus 接口监听窗口信号（deepin 25+ kwin_wayland 原生支持）
- **Wayland 备选**：`wlr-foreign-toplevel-management` 协议（如 deepin 切换到 wlroots 合成器）
- **X11 fallback**：`XGetInputFocus` 轮询 + `XSelectInput` 事件通知（仅当运行在 X11 会话时）

**采集事件**：
- `window_created` — 新窗口出现
- `window_closed` — 窗口关闭
- `window_activated` — 焦点切换（驱动 app_sessions 表）
- `window_moved` / `window_resized` — 位置/大小变化

**实现要点**：
- 启动时通过 `QGuiApplication::platformName()` 检测当前会话类型，自动选择路径
- Wayland：通过 `QDBusConnection::connect` 订阅 KWin 信号，窗口信息从 D-Bus 参数直接获取
- X11：窗口信息通过 `_NET_WM_NAME`、`WM_CLASS`、`_NET_WM_PID` 获取
- 焦点切换时自动结束上一个 app_session，开始新的
- X11 轮询 fallback 频率：2秒/次（仅当 D-Bus 信号不可用时）
- 返回的窗口信息统一包含 `wm_class`、`window_id`、`is_minimized` 字段

### 5.2 FileSensor（文件操作）

**数据源**：
- inotify：监控用户目录下文件变化（`IN_OPEN`, `IN_MODIFY`, `IN_CLOSE_WRITE`）
- XDG recent-files：读取 `~/.local/share/recently-used.xbel` 获取应用级"最近打开"记录
- 智能体调用 `GetRecentFiles`/`GetRecentFile` 时，结合 inotify 缓存 + XDG 数据返回最准确结果

**监控范围**：
- 默认 `~/Documents`, `~/Desktop`, `~/Downloads`
- 仅文本类文件（`.txt`, `.md`, `.cpp`, `.py`, `.json`, `.xml` 等）
- 通过 Config 可配置监控目录和文件类型

**采集事件**：
- `file_opened` — `IN_OPEN`
- `file_modified` — `IN_MODIFY`（同文件1秒内合并）
- `file_closed` — `IN_CLOSE_WRITE`
- `file_created` / `file_deleted`

**实现要点**：
- **XDG recent-files 为主数据源**，inotify 为补充
- inotify 仅监控根目录本身 + 一级子目录（深度限制 2 层），避免 `node_modules`/`.git` 等深层目录爆炸
- 排除目录黑名单：`.git`, `node_modules`, `__pycache__`, `.venv`, `build`, `.cache`（可配置）
- `max_watches` 提升至 1024，配合黑名单和深度限制
- `inotify_add_watch` + epoll 监听
- 对文本文档读取首 200 字符做脱敏后存入 `content_preview`

### 5.3 ClipboardSensor（剪贴板）

**数据源**：`QClipboard::dataChanged()` 信号

**采集事件**：
- `clipboard_changed` — 剪贴板内容变化
- `copy` — 检测到新内容
- `paste` — 通过 InputSensor 的 Ctrl+V 联动

**实现要点**：
- 监听 `QGuiApplication::clipboard()->dataChanged()`
- 区分 text / image / html / urls
- 文本脱敏后存储，超长截断（200字符预览）
- 图片只记录尺寸，不存储内容
- 去重：相同内容预览前缀 + < 5秒时间窗口不重复记录（不存储内容哈希，避免彩虹表风险）

### 5.4 InputSensor（键鼠事件）

**数据源（Wayland 受限，X11 fallback）**：
- **X11**：`XRecord` 扩展，可全局捕获键鼠事件
- **Wayland**：安全协议禁止跨客户端输入捕获，以下降级策略可选：
  - 优先：通过 `org.kde.KWin` D-Bus 接口获取键盘修饰键状态和快捷键事件（合成器层面）
  - 次选：`libinput` + `udev` 后端（需 root 或 logind seat 权限，非用户态可行）
  - 降级：仅当智能体窗口本身获得焦点时，通过 Qt 事件捕获（非全局）
  - 最差：InputSensor 禁用，仅依赖 WindowSensor + ClipboardSensor 推断用户行为

**采集事件**：
- `key_press` — 按键按下（记录 key_code，不记录完整输入序列）
- `mouse_click` — 鼠标点击（记录 x, y 坐标和按钮）
- `input_pattern` — 输入模式识别（聚合事件）

**隐私处理（关键）**：
- 不记录连续字符输入，只记录模式统计
- 记录格式：`{pattern: "continuous_typing", duration_ms: 12000, key_count: 47}`
- 快捷键：`{type: "shortcut", keys: "Ctrl+S"}`（只记组合键不记内容）
- 鼠标只记录点击次数和区域，不记录精确轨迹
- 提供配置项可完全关闭键鼠感知

**实现要点**：
- 启动时检测会话类型，Wayland 下优先尝试 KWin D-Bus 接口
- X11 下使用 `XRecord` 范围：`XRecordAllClients`
- 1秒窗口内聚合按键 → 生成 `input_pattern` 事件
- 修饰键组合识别 Ctrl/Alt/Shift/Super
- Wayland 降级时在日志中明确记录降级原因和当前策略

### 5.5 SystemSensor（系统状态定时采集）

**数据源**：
- CPU：`/proc/stat` 解析
- 内存：`/proc/meminfo`
- 磁盘：`statfs()`
- 网络：`/proc/net/dev` + NetworkManager D-Bus
- 电池/亮度：`upower` D-Bus + `/sys/class/backlight/`

**采集策略**：
- 默认 30 秒采样一次，存入 `system_snapshots` 表
- 异常触发 `SystemAlert` 信号：
  - 电量 < 20%（可配置）
  - 磁盘使用率 > 90%（可配置）
  - CPU 持续 > 90% 持续 60 秒（可配置）
- 采样间隔通过 Config 可配置

### 5.6 BrowserSensor（浏览器活动）

**数据源**：
- 主：**deepin-browser** 原生 D-Bus 接口（如有），订阅标签页变化、导航、下载信号
- 通用：解析浏览器 SQLite 历史数据库（兼容 Chromium 内核和 Firefox）
  - Chromium 内核（deepin-browser/Chrome/Edge）：`~/.config/<browser>/Default/History`
  - Firefox：`~/.mozilla/firefox/<profile>/places.sqlite`
- 书签：Chromium `Bookmarks` JSON 文件 / Firefox `places.sqlite` 中 `moz_bookmarks` 表
- 当前标签页：通过浏览器 D-Bus 接口或窗口属性获取当前活跃标签页 URL

**采集事件**：
- `browser_navigation` — 导航到新页面（url, title, referrer）
- `browser_tab_changed` — 标签页切换（tab_id, url, title）
- `browser_download` — 下载事件（url, filename, size, state）
- `browser_bookmark_added` — 添加书签

**实现要点**：
- 历史数据库为浏览器独占锁，**只读 + WAL 模式**打开，设置 `sqlite3_busy_timeout(2000)` 捕获 `SQLITE_BUSY` 静默跳过
- **inotify 监控 `History` 文件本身的 mtime 变化**（单文件，不占 watch 配额），文件变化时才触发读取，避免定时轮询的无效 I/O
- 轮询 fallback 间隔放宽至 **30 秒**（当 inotify 不可用时），增量读取自上次以来的新记录（通过 `last_visit_time` 字段）
- URL 域名提取 + 分类（搜索/社交/开发/购物等），存入 metadata 便于后续分析
- 隐私：URL 中的查询参数脱敏（移除 token/session/key 等敏感参数），URL 本身不脱敏
- 当 deepin-browser 提供 D-Bus 接口时，优先使用实时信号，文件监听作为 fallback

**历史记录去重**：
- 同一 URL 在短时间内（5分钟）多次访问只记录一次，`visit_count` 递增

## 6. 配置管理

配置文件：`~/.config/deepin/environment-awareness/config.json`

```json
{
    "retention_days": 7,
    "sampling_interval_sec": 30,
    "redaction_enabled": true,
    "sensors": {
        "window": { "enabled": true, "poll_interval_ms": 2000 },
        "file": {
            "enabled": true,
            "watch_dirs": ["~/Documents", "~/Desktop", "~/Downloads"],
            "max_watches": 1024,
            "watch_depth": 2,
            "ignore_dirs": [".git", "node_modules", "__pycache__", ".venv", "build", ".cache"],
            "file_types": ["txt", "md", "cpp", "py", "json", "xml"]
        },
        "clipboard": { "enabled": true, "max_preview_len": 200 },
        "input": { "enabled": true, "record_patterns": true, "record_keys": false },
        "system": {
            "enabled": true,
            "alert_battery": 20,
            "alert_disk": 90,
            "alert_cpu": 90,
            "downsample_after_days": 3,
            "downsample_interval_sec": 3600
        },
        "browser": {
            "enabled": true,
            "poll_interval_ms": 30000,
            "supported_browsers": ["deepin-browser", "chrome", "firefox"],
            "dedup_window_sec": 300,
            "redact_url_params": true
        }
    }
}
```

配置通过 D-Bus Config 接口热加载，修改后立即生效，无需重启。

## 7. 部署

- 以 systemd user service 运行：`~/.config/systemd/user/deepin-environment-awareness.service`
- 随用户登录自启动
- 单进程，多线程，各 Sensor 独立线程

## 8. 项目结构

```
deepin-environment-awareness/
├── CMakeLists.txt
├── src/
│   ├── main.cpp                      # daemon 入口
│   ├── core/
│   │   ├── EventBus.h/cpp           # 事件总线
│   │   ├── StorageController.h/cpp  # SQLite + 脱敏 + 清理
│   │   ├── Config.h/cpp             # 配置管理 + 热加载
│   │   └── Event.h                  # 事件数据结构定义
│   ├── sensors/
│   │   ├── ISensor.h                # Sensor 接口
│   │   ├── WindowSensor.h/cpp
│   │   ├── FileSensor.h/cpp
│   │   ├── ClipboardSensor.h/cpp
│   │   ├── InputSensor.h/cpp
│   │   ├── SystemSensor.h/cpp
│   │   └── BrowserSensor.h/cpp
│   ├── dbus/
│   │   ├── DBusManager.h/cpp        # D-Bus 服务管理
│   │   ├── ContextAdaptor.h/cpp     # Context 接口
│   │   ├── HistoryAdaptor.h/cpp     # History 接口
│   │   ├── SystemAdaptor.h/cpp      # System 接口
│   │   ├── ConfigAdaptor.h/cpp      # Config 接口
│   │   └── EventsAdaptor.h/cpp      # Events 信号
│   └── utils/
│       ├── Redactor.h/cpp           # 脱敏引擎
│       └── Logger.h/cpp             # 日志
├── data/
│   └── deepin-environment-awareness.service  # systemd unit
└── tests/
    ├── test_storage.cpp
    ├── test_redactor.cpp
    └── test_sensors.cpp
```

## 9. 错误处理

- Sensor 初始化失败：记录日志，该 Sensor 禁用，其他 Sensor 正常运行
- SQLite 写入失败：重试 3 次，失败后事件放入内存队列待恢复
- D-Bus 注册失败：daemon 启动失败，systemd 自动重启
- X11/XRecord 不可用：InputSensor 降级或禁用
- Wayland 全局输入捕获受限：InputSensor 降级为 KWin D-Bus 快捷键事件或禁用，日志记录降级原因
- inotify watch 达到上限（`ENOSPC`）：记录警告日志，停止新增 watch，保留已有；可通过 `fs.inotify.max_user_watches` 提升系统上限
- 浏览器历史数据库锁定或不可读：记录警告日志，BrowserSensor 降级为仅依赖 D-Bus 信号
- 浏览器未安装或未检测到支持的浏览器：BrowserSensor 自动禁用

## 10. 测试策略

- **StorageController**：测试写入、查询、脱敏、清理生命周期
- **Redactor**：测试各脱敏规则匹配和替换
- **各 Sensor**：使用 mock 数据源测试事件采集逻辑
- **D-Bus 接口**：使用 `dbus-send` 或 `gdbus` 工具测试方法调用和信号
- **集成测试**：启动完整 daemon，模拟用户操作，验证端到端数据流
