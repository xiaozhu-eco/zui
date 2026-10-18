# tools/trace

两件东西：把 Perfetto 的读取端取到本地，以及一份固定的查询集。通道说明见
[`docs/PERF.md`](../../docs/PERF.md) §5，这里只讲怎么按。

## 取工具

```powershell
pwsh tools/trace/fetch_trace_processor.ps1
```

```bash
tools/trace/fetch_trace_processor.sh
```

下到 `build/tools/trace_processor_shell[.exe]`，版本和 sha256 都钉在脚本里，校验不过就丢弃重下。
`-Url` / `-f` 分别用来指镜像和强制重取。

这是诊断工具，不是依赖：`cmake/deps.cmake` 不认识它，正常构建不需要网络。产物在 `build/` 下，
不进仓库。


## 跑示例程序并出 trace

```powershell
pwsh tools/trace/run_app.ps1 studio
pwsh tools/trace/run_app.ps1 hello -Perf
pwsh tools/trace/run_app.ps1 dump build/perf/grid60.html --size 1100x720 --repeat 6
pwsh tools/trace/run_app.ps1 reader -Config windows-x64-release -Perf
pwsh tools/trace/run_app.ps1 -List
```

关掉窗口（或等 `dump` 自己退出）后写入 `build/trace/<程序名>.pftrace`。默认
`ZUI_TRACE_CATEGORIES=app,engine`；要每盒细节加 `-AllCategories`。

## 产 trace 并查

```powershell
# 无窗口，最能反映布局本身的成本
build/windows-x64-release/zui_dump.exe build/perf/grid60.html `
    --size 1100x720 --repeat 6 --trace build/trace/grid60.pftrace

# 只记阶段区间，不记每盒的三个类目：开销落回噪声级，体积降两个数量级
$env:ZUI_TRACE_CATEGORIES='app,engine'
```

```powershell
# 先看热点排行
build/tools/trace_processor_shell.exe query -f tools/trace/queries/03_hotspots.sql build/trace/grid60.pftrace

# 连续追问时把解析做一次
build/tools/trace_processor_shell.exe server unix --name s1 --daemonize build/trace/grid60.pftrace
build/tools/trace_processor_shell.exe query --remote s1 "SELECT name, dur/1e6 AS ms FROM slice ORDER BY dur DESC LIMIT 10"
build/tools/trace_processor_shell.exe server kill s1
```

`queries/` 里每条查询回答一个问题。通道约定见 [`docs/PERF.md`](../../docs/PERF.md) §5。

## 写查询时的两个坑

TrackEvent 的 debug annotation 读回来键带 **`debug.`** 前缀：写入器发的 `kind`，查询里要用
`debug.kind`。拿不准就先 `SELECT * FROM args LIMIT 0` 看列名。

计数器的名字在 `counter_track` 上，不在 `counter` 行上——`counter` 只有 `(ts, track_id, value)`。
`07_counters.sql`、`08_last_frame_counters.sql` 里有现成的 join 写法。
