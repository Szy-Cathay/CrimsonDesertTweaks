# Crimson Desert Tweaks — Codex 项目规则

## 开工与范围

- 解释、计划和回复使用中文；用户不懂编程，要说明实际影响和验证结果。代码注释沿用现有风格。
- 开工先读 `PROGRESS.md` 的当前状态。编码前说明目标、假设、涉及文件和验证方式；影响范围的歧义先澄清。
- 当前暂停新功能开发和地面血迹延时实验；只有用户明确提出新的开发需求时才恢复。v2.00.02 适配成果已撤销，当前版本兼容性尚未确认。
- 只修改当前请求必需的内容。保留用户已有工作，不顺手重构；已存在的文件先读再用补丁编辑，不使用整文件覆盖命令。
- `AGENTS.md` 和 `PROGRESS.md` 随仓库维护。进度写入 `PROGRESS.md`；README 仅在用户要求或公开说明变化时更新。

## 当前代码与资料入口

项目是 C++23 / MSVC x64 ASI 模组，使用 SafetyHook 和 Zydis；构建以 `CMakeLists.txt` 为准。

| 内容 | 当前事实 | 修改前阅读 |
|------|----------|------------|
| Shield Hide | 已发布；主线保留旧版本适配路径，v2.00.02 未验收 | `src/hooks/shield_hide.cpp`、`docs/reverse-engineering/shield-hide.md` |
| Blood Decals | 主线仍包含血迹代码和早期地面血迹路径；地面效果未完成，开发暂停 | `src/hooks/blood_decals.cpp`、`src/hooks/blood_decals_timing.h`、`docs/reverse-engineering/blood-decals.md` |
| 扫描与初始化 | 扫描在 `src/scanner.cpp/h`，安装/卸载在 `src/dllmain.cpp` | 修改功能时核对这两处注册 |
| 配置与热重载 | 配置在 `src/config.cpp/h`，监视线程在 `src/config_watcher.cpp/h` | 默认键和值以根目录 INI 为准 |
| 日志与测试 | 日志经 `src/logger.cpp/h`；配置/血迹测试在 `tests/config_and_blood_decals_test.cpp` | 按实际修改选择验证 |

旧交接和实验文档属于历史证据，不能作为恢复开发的授权或当前待办；读取时先核对记录的游戏版本和日期。

## 构建与验证

在仓库根目录使用 PowerShell：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

配置或血迹逻辑变化时，显式构建并运行相关测试：

```powershell
cmake --build build --config Release --target crimson-desert-tweaks-tests
ctest --test-dir build -C Release --output-on-failure
```

- ASI 和默认 INI 必须同层输出到 `build/Release/`；保留 CMake 的默认 INI 复制和旧输出目录 cache 清理保护。
- 构建产物留在构建目录，安装由用户通过 DMM 手动完成。禁止自动复制到游戏或模组目录。
- 构建成功、AOB 命中和游戏内功能验收分别记录；前两者不能替代实际游戏效果验证。
- 文档修改只检查事实、链接和差异；已有有效证据可复用，不为文档整理重新构建或生成分析缓存。

## 配置与 Hook 契约

- `ReadBoolAliasIfPresent` 必须通过 `ReadBoolAlias` 读取 `Enabled` / `Enable`，不能仅检测别名后用 `ReadBool` 读取 canonical key。2026-06-28 盾牌隐藏曾因此失效。
- 各功能独立管理 `Install*Hook()` / `Remove*Hook()`；未启用时跳过安装并返回成功，安装失败记录日志后继续其他功能。
- 热重载通过 `GetConfig()` 读取新配置；Hook 安装/卸载不支持热切换，需要重启游戏。
- 新增功能获授权后，核对 Hook、scanner 声明/实现、配置、dllmain 注册及 CMake 源文件列表。
- AOB 提供宽松和精确候选，并用目标 EXE 验证预期命中数及指令上下文；一般至少包含 12–16 字节。已有多命中扫描以该功能的明确契约为准。
- 日志统一经 `Log()` 写入 ASI 同目录；调试使用 `Verbose=1` 检查 DIAG，正常使用 `Verbose=0`。

## 逆向工具与证据

当前工具链（2026-09-05 核验）：

- Ghidra 12.1.3：`D:\ghidra_12.1.3_PUBLIC\ghidraRun.bat`。
- Ghidra MCP 6.0.0：`C:\Users\12494\Documents\Codex\MCP\ghidra-mcp-6.0.0\.venv\Scripts\bridge-mcp-ghidra.exe`；维护工具时读取 [运行环境规则](C:/Users/12494/Documents/Codex/MCP/ghidra-mcp-6.0.0/AGENTS.md)。
- Ghidra 扩展位于 `C:\Users\12494\AppData\Roaming\ghidra\ghidra_12.1.3_PUBLIC\Extensions\GhidraMCP\`。
- Codex 的实际 MCP 启动配置以 `C:\Users\12494\.codex\config.toml` 中的 `mcp_servers.ghidra-mcp` 为准。连接前核对实例、打开的程序和分析状态，不假定旧连接已经切换。
- 旧分析工程 `D:\ghidra project\CrimsonDesertv1.13.00.gpr` 与同名 `.rep` 目录成套保留，作为旧版对照。升级工具不等于升级工程内的游戏二进制。

逆向记录必须注明游戏版本、EXE SHA256、分析工程/程序、地址采用 VA 还是 RVA、相关指令/AOB，以及证据来自静态分析、运行日志还是用户游戏验收。推测和已确认事实分开，已否定路线保留原因。

分析新版本时使用独立项目或独立程序名。清理缓存前区分临时导出和正式工程数据库；全量索引、下载包与测试产物放在明确的临时目录，验证后清理。

## 项目路径

- 游戏目录：`D:\steam\steamapps\common\Crimson Desert\bin64\`。
- DMM 模组目录：`D:\红色沙漠工具\模组\Crimson Desert Tweaks\`。
- 逆向参考工具：`D:\红色沙漠工具\修改器\`。
- 同类源码参考：`D:\AI Agent Resources\GitHub\` 下的 `CrimsonDesertTools-main`、`CrimsonDesert-player-status-modifier-master`、`CrimsonDesert-parry-evade-master`、`CrimsonDesertMods-main`。

## 联网

默认使用系统 TUN / 虚拟网卡模式，不为 git、curl、cmake 默认设置固定代理端口。

- 联网失败时先说明现象并检查 TUN/代理。
- 仅在确认命令未走 TUN 或用户明确要求时临时设置代理；未经要求不改全局 Git proxy。
- CMake 下载依赖失败且排查后仍无法连接 GitHub，再说明情况并询问是否使用镜像。

## 保护规则与拦截说明

当前仓库不存在 `CLAUDE.md`、`.claude/settings.json` 或 `.claude/hooks/`。以下是 Codex 必须遵守的项目约束，不代表已部署 PreToolUse Hook：

- 禁止修改或删除 `deps/safetyhook/`，它是构建依赖的完整源码。
- 禁止向游戏目录复制、移动、删除或写入文件；构建输出只留在 `build/Release/`。
- 禁止 `git reset --hard`、`git checkout -- .`、`git clean -fd` 等危险还原。
- 工具或实际 Hook 拦截操作时，停止该操作并说明返回的原因，不换写法或工具绕过。未收到具体 Hook 信息时，不把 `blocked by policy` 归因于不存在的项目 Hook。
- 需要修改实际 Hook 或放宽保护规则时，先说明具体规则；仅在用户已明确授权该修改后执行。

## 进度记录

改变源码、构建行为或项目状态后，及时更新 `PROGRESS.md` 中的当前事实、原因、有效验证和待确认事项。暂停项目不自动安排后续实验。

正文保留简明状态；地址、结构、签名和已否定路线放入相应逆向技术文档并从进度文件链接。旧命令流水和逐次产物时间由 Git 历史承载，不重复追加。
