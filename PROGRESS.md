# Crimson Desert Tweaks — 开发进度

## 当前状态

- 当前已发布功能：盾牌隐藏。
- 正在实现血迹残留时间延长；尸体存在时间延长暂不实现，等待找到尸体清理/生命周期字段。

## 已实现

### 盾牌隐藏 (Shield Hide)

- **状态**: ✅ 已实现
- **日期**: 2026-06-28
- **原理**: 拦截 FakeEquip 系统 ShieldFakeEquip 条目处理，将 flags 清零。
- **AOB**: `48 8B 05 0A 3B C3 04 48 8B 08`。
- **已知限制**: 启动时已双持需切换装备触发。
- **v1.13.00 适配**: 2026-07-04 新版 EXE (`SHA256 EA2F5300B30BC890911AABBCB65D4AB9750D051F6D774D1CA364A501E85BC2E2`) 中旧精确 AOB 0 命中；Skinned/面部血迹 publish-store 旧宽松 AOB 仍唯一命中 `0x14071E3BA`。Shield Hide 改为扫描 v1.13.00 ReserveSlot/FakeEquip 固定 key 读取簇：`reserve-slot-key-load-cluster-1.13.00` 在新版 EXE 中命中 9 个地址 (`0x1414E73CF`、`0x1414E7422`、`0x1414E7475`、`0x1414E74C8`、`0x1414E751B`、`0x1414E756E`、`0x1414E75C1`、`0x1414E7614`、`0x1414E7667`)，安装多个同形态 hook；回调仍只在 `entry+0x08 == 15` 时清 `entry+0x10` flags，非 ShieldFakeEquip key 会跳过。
- **v1.13.00 构建验证**: 2026-07-04 Release 构建通过，输出 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`，大小 `491008`，时间 `10:50:17`；没有写入游戏目录。Ghidra/字节签名复查：`reserve-slot-key-load-cluster-1.13.00` 命中 9 个地址，`blood-decals-publish-store-wide-1.12.02` 在新版 EXE 中仍唯一命中 `0x14071E3BA`。

## 当前约束

- 构建产物只留在 `build/Release/`。
- 不自动复制到游戏目录或模组目录。
- 修改 `src/` 下源码时，继续实时更新本文件。

## 进行中

### 血迹残留时间延长 (Blood Decals)

- **状态**: ✅ Skinned/面部血迹已确认有效；地面喷溅血迹仍未完成；terrain object `+0x160` 写入路线已被新版静态反证并从实现中撤掉
- **日期**: 2026-07-01
- **逆向证据**:
  - 2026-07-04 v1.13.00 新版项目 `D:\ghidra project\CrimsonDesertv1.13.00` 打开 `CrimsonDesert.exe`；新版 EXE SHA256 为 `EA2F5300B30BC890911AABBCB65D4AB9750D051F6D774D1CA364A501E85BC2E2`，Ghidra MCP 状态为自动分析中 (`analyzing=true`, `function_count=184796`)，但字节签名、局部反汇编和局部反编译可查。
  - 2026-07-04 v1.13.00 字节签名复查：Skinned/面部血迹 publish-store 旧宽松 AOB 唯一命中 `0x14071E3BA`；particle lifetime post-copy AOB 唯一命中 `0x1406AD92D`，hook 点 `0x1406AD949`；GroundBloodDecal hit-material entry AOB 唯一命中 `0x14055B020`。terrain object `+0x160` refresh 仍可定位到 `0x143D9E8DD` / hook `0x143D9E8F9`，但该路线已被后续结构分析反证，不再安装。
  - 2026-07-04 v1.13.00 反汇编抽查确认：`0x14071E3BA` 仍为 `mov [global], rdi` 发布 manager，并继续发布 `rdi+0x130` / `rdi+0x1B0`；`0x1406AD949` 仍位于 `call FUN_1406A6C70` 之后；`0x14055B020` 仍为 hit-material entry builder 入口，函数内从 `[rsp+0x90]` 取第 7 参数；`0x143D9E8F9` 紧跟 `movss [rbx+0x160], xmm0`，仍是 terrain object 可见结束时间写入后的 hook 点。
  - Ghidra 项目 `D:\ghidra project\CrimsonDesert` 当前打开 `CrimsonDesert.exe`。
  - 血迹/贴花管理器发布路径已定位到 `0x140712600` 附近，关键发布写入位于 `0x1407126DA`。
  - `0x1407126DA` 指令序列会发布 `rdi`、`rdi+0x130`、`rdi+0x1B0` 到 `0x1461E47F0` 附近的全局槽位。
  - 参考 ASI `CrimsonDesertBloodDecals.asi` 的可读字符串包含 `GameSkinnedDecalElement`、`life78`、`fade80`，并显示 `writeFade=disabled`。
  - 2026-07-01 游戏日志确认旧实现已加载、配置读取成功、AOB 命中 `0x1407126DA`、worker 启动，但没有 `patched>0` tick；说明失败点不是安装/配置/AOB，而是对象枚举或字段类型。
  - 参考 ASI 反汇编确认：`+0x78/+0x80` 是 32-bit 毫秒字段，默认血迹值为 `life78=0xBB8` (3000ms)、`fade80=0x5DC` (1500ms)，参考实现把 `+0x78` 写成 `0x493E0` (300000ms)。
  - 参考 ASI 反汇编确认：候选对象来自 effect 容器的 `EffectListA_00A0` / `EffectListB_0100`，路径为 manager slot -> `+0x58` -> `+0x08` -> `+0x38` -> `+0x68` -> `+0x60`，再扫描 `+0xA0/+0xA8` 和 `+0x100/+0x108`。
  - 2026-07-02 新 ASI 游戏日志确认 `life78Ms=600000` 新版本已加载，`publishHits` 持续增长，`globalManager` 与 `observedManager` 一致，但 `effects=0x0 readFailed=1`；失败点进一步缩小为 effect 容器解析/对象识别，不是字段类型、配置读取、hook 安装或 publish 捕获。
  - 2026-07-02 继续逆向参考 ASI：`0x180026FA0` 先通过 `ClientActorManagerGlobal` 解析 `[slot] -> mgr -> mgr+0x58 -> userActor -> userActor+0x08 -> subMgr -> subMgr+0x38` 得到当前 CCOIA，再走 `CCOIA+0x68 -> +0x60` 到 effect 容器。
  - 2026-07-02 二进制字符串确认真实类型名为 `.?AUGameSkinnedDecalElement@pa@@` / `.?AUSkinnedDecalElement@pa@@`，不是最初假设的完整 `.?AV...` 类名；对象识别必须走 MSVC RTTI 类型名，不能把对象首 qword 当作 C 字符串。
  - 2026-07-02 用户游戏内确认：玩家控制角色面部血迹残留时间延长成功；喷洒到地面上的血迹仍按原版快速消失；角色身上没有血迹属于原版行为，不作为本轮 bug。
  - 2026-07-02 日志确认当前实现运行时 `patched` 持续增长且 `totalPatched=145`，说明 `GameSkinnedDecalElement` / `SkinnedDecalElement` 的 `+0x78` 写入有效；地面喷溅不是这条 Skinned 对象路径，或被当前 RTTI 白名单跳过。
  - 2026-07-02 EXE 字符串线索：存在 `GroundBloodDecal`、`TargetGroundDecal`、`TargetBodyDecal`、`BodyBloodDecal`、`BloodDecalInfo`、`MaterialBloodDecalInfo`、`BloodDecalData`、`GPUParticleDecalFlags`、`_isBloodParticle`、`_renderDecalOnTerrain` 等地面/材质/粒子贴花相关字段；这些目前只是地面血迹后续逆向线索，尚未证明生命周期字段。
- **实现决策**:
  - 只写 `GameSkinnedDecalElement` / `Skinned`+`Decal` 对象的 `+0x78` dword 毫秒生命周期字段。
  - `+0x80` 只读作 fade 毫秒诊断，不写入。
  - 尸体相关字符串如 `DeadBody`、`DeadActor`、`DeadCharacter`、`RagdollDynamicDead` 目前只作为后续逆向线索，不进入本次功能。
- **已完成**:
  - 新增 `[BloodDecals]` 配置读取：`Enabled`/`Enable` 通过 `ReadBoolAliasIfPresent`，`LifetimeSeconds` 与 `ScanIntervalMs` 带范围钳制。
  - 新增默认 `crimson-desert-tweaks.ini` 模板。
  - 新增 Blood Decals AOB 扫描函数 `ScanForBloodDecalActorManagerPublish()`，锚定 `0x1407126DA` 的 publish-store 指令序列。
  - 新增并修正 `src/hooks/blood_decals.cpp`：启动 worker 后读取 publish-store manager 全局槽位，沿参考 ASI 的 effect 容器路径扫描 `EffectListA_00A0` / `EffectListB_0100`，用对象名称字符串校验 `GameSkinnedDecalElement` 或 `Skinned`+`Decal`，只写 `object+0x78` 的 dword 毫秒 `life78`，`fade80` 只读不改。
  - 已在 `dllmain.cpp` 接入 Blood Decals 启动/停止，并在 `CMakeLists.txt` 加入新源文件。
  - Release 构建通过，产物位于 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`；本次构建已将 CMake 输出目录覆盖到仓库内 `build/Release`，没有复制到游戏目录或模组目录。
- **当前修正**:
  - 已把 `src/hooks/blood_decals.cpp` 从旧的 RTTI + float 秒数字段修正为参考 ASI 证实的名称字符串校验 + dword 毫秒写入。
  - 已把 worker 候选枚举从 publish-store 附近的 `rdi+0x130/0x1B0` 改为 effect 容器的 `+0xA0/+0x100` 两组列表。
  - 已新增首个 tick 摘要日志，避免下次只看到 worker started 却不知道路径/列表/候选是否为空。
  - 已同步 `scanner.cpp` 注释：AOB 仍保留后续 store 指令保证唯一性，但 worker 只使用 manager slot。
  - 2026-07-01 Release 构建通过，输出 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`，没有复制到游戏目录或模组目录。
  - 已新增 publish-store mid-hook：在 `0x1407126DA` 执行时捕获 `ctx.rdi` 作为 observed manager，worker 在 manager 全局槽为 0 时使用该值，并记录 `publishHits/globalManager/observedManager` 诊断。
  - 2026-07-01 第二轮 Release 构建通过，输出 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`，没有复制到游戏目录或模组目录。
  - 2026-07-02 第三轮修正：`src/hooks/blood_decals.cpp` 改为标准 MSVC RTTI COL 读取类型名，识别 `GameSkinnedDecalElement@pa` / `SkinnedDecalElement@pa`；不再把对象首 qword 当名字指针。
  - 2026-07-02 第三轮修正：保留参考 ASI 的当前 CCOIA 主链路，同时新增 `manager+0x130` actor-array 备用扫描；从每个 CCOIA 解析 `+0x68 -> +0x60` effect 容器，降低 `mgr+0x58` 或当前角色链路漂移导致完全扫不到的风险。
  - 2026-07-02 第三轮修正：增加 `mgr->ClientUserActor` RTTI 自愈，在 `0x58 ± 0x200` 内寻找 `ClientUserActor@pa`，找到后缓存偏移并记录日志。
  - 2026-07-02 第三轮 Release 构建通过，输出 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`，没有复制到游戏目录或模组目录。
  - 2026-07-02 静态验证通过：`src/hooks/blood_decals.cpp` 中只存在 `SafeWriteDword(candidate + 0x78, ...)`，没有旧 `SafeWriteFloat` 或 `+0x80` 写入；`dumpbin` 确认 `0x1407126DA` 发布 `0x1461E47F0` manager slot、`0x1461E47F8` actor-array descriptor slot、`0x1461E4800` 第二 descriptor slot。
  - 2026-07-02 第四轮诊断：新增 RTTI 采样。当前仍只写 Skinned 血迹 `+0x78`；对当前 effect 列表中被跳过但 RTTI 名含 `Decal` 或 `Blood` 的候选，最多记录 32 种 `name/reason/life78/fade80`，用于一次游戏运行确认地面喷溅是否已经在当前扫描容器中但被白名单过滤。
  - 2026-07-02 第四轮诊断：新增 `docs/reverse-engineering/blood-decals.md` 技术适配笔记，记录已确认的 publish-store、RTTI、字段布局、成功日志特征和版本更新后排查顺序。
  - 2026-07-02 第四轮静态补查：`GroundBloodDecal` 属于 hit-material 字段块，附近有 `WeaponHitDecal` / `TargetGroundDecal` / `BodyBloodDecal`；`MaterialBloodDecalInfo` 字段只有 `_isBlocked`、`_stringKey`、`_bloodDecalVariationData`、`_skillKey` 等，未发现可确认生命周期字段；粒子字段包含 `_renderDecalOnTerrain`、`_isBloodParticle`、`_gpuParticleDecalFlags`，说明地面喷溅可能走 GPU particle decal 而非 Skinned 对象。
  - 2026-07-02 用户测试日志确认当前 ASI 已加载并继续 patch Skinned 血迹，`totalPatched=71`；但游戏目录 INI 为 `Verbose=0`，上一版 Verbose-only RTTI 采样没有输出 `DIAG skipped-rtti`，因此这次日志不能判断地面血迹是否在当前容器里。已改为默认限流记录 `Decal/Blood` RTTI 样本，不再依赖 `Verbose=1`。
  - 2026-07-02 第四轮诊断 Release 构建通过，输出 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`；`git diff --check` 只有 CRLF 提示；静态检查确认仍无 `SafeWriteFloat`、无 `candidate+0x80` 写入，唯一写入仍是 `candidate+0x78` 的 dword 毫秒值。游戏目录 ASI 时间仍为 03:59，未由 Codex 写入。
  - 2026-07-02 用户新测试日志确认 04:15 ASI 已加载，`DIAG skipped-rtti` 正常输出；当前 effect 容器只采到 `SkinnedDecalDynamicProperty`（`life78/fade80` 为明显非生命周期值）和一个 `GameSkinnedDecalElement life78=1000 fade80=0`，没有出现 `GroundBloodDecal`、`Blood` 或 `Decal` 且 `3000/1500` 的地面候选。因此地面喷溅基本可排除“当前 Skinned effect 列表里被白名单跳过”这一假设。
  - 2026-07-02 Ghidra 小范围汇编确认粒子发射器数据字段：`_gpuParticleDecalFlags` 对应 setter 写 `byte ptr [RCX+0x119]`，`_renderDecalOnTerrain` 写 `byte ptr [RCX+0x12B]`，`_isBloodParticle` 写 `byte ptr [RCX+0x135]`；这属于另一套粒子/地形贴花结构，不是 `GameSkinnedDecalElement +0x78`。
  - 2026-07-02 字符串块确认 `_particleLifeTimeMin` / `_particleLifeTimeMax` 位于 `SplineEffect` 相关字段块，`_effectLifeTime` 位于 `ActionChartFrameEvent_EffectVariationParameter` 字段块；尚未证明这些字段就是地面血迹粒子运行时对象的生命周期字段，不能直接写。
  - 2026-07-02 第五轮只读诊断：`src/hooks/blood_decals.cpp` 新增 `DIAG particle-candidate` 采样，只读候选对象的 `+0x119/+0x12B/+0x135` 和 `+0x118..+0x137` 原始字节；不写粒子字段、不写疑似生命周期字段，用于确认当前扫描候选里是否存在“血液粒子 + 地形贴花”运行时数据。
  - 2026-07-02 第五轮 Release 构建通过，输出 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`；`git diff --check` 只有 CRLF 提示；静态检查确认粒子诊断只使用 `SafeRead/SafeReadBytes`，唯一写入仍是 Skinned 血迹 `candidate+0x78`。
  - 2026-07-02 用户第五轮日志确认新版诊断已加载：`DIAG particle-candidate` 出现 23 个样本，但没有干净的 `blood135=1` 或 `terrain12B=1`；大量样本是低地址、主模块地址或 `terrain12B/blood135` 为 64/128/255 等非布尔值，说明当前 effect-list 扫描把普通数据误当成粒子对象，不能把这些样本作为地面血迹生命周期证据。
  - 2026-07-02 Ghidra 精确 xref 确认：`_isBloodParticle` (`0x1450E3248`) 与 `_renderDecalOnTerrain` (`0x1450E2F20`) 的直接字符串引用都在 `FUN_1429bdec0` 反射/字段注册函数；`GroundBloodDecal`、`_particleLifeTimeMin/Max`、`_effectLifeTime` 没有直接 xref，不能直接从字段名字符串定位运行时对象。
  - 2026-07-02 本地机器码 + Ghidra 小范围反汇编确认 `+0x135` 确实有运行时代码读取：`0x14044A8B9` / `0x1407E4FBC` 处比较 `byte ptr [RCX+0x135]`；`+0x12B` 在 `0x1435773FA` 等渲染路径检查，`+0x119` 在 `0x140BDA26C` setter 更新。这些是真正的后续逆向入口，比当前 effect-list 粒子诊断更可靠。
  - 2026-07-02 第六轮只读诊断收窄：`DIAG particle-candidate` 改为 `DIAG particle-strict-candidate`，候选必须像运行时堆对象、`+0x12B/+0x135` 必须是 0/1，且至少 `terrain12B=1` 或 `blood135=1` 才记录；仍不写任何粒子字段。
  - 2026-07-02 新一轮用户日志核对：游戏目录日志仍停在 13:10:42，ASI 时间为 09:42:51，仓库最新构建产物为 13:26:25；日志里仍是旧 `DIAG particle-candidate`，没有 `DIAG particle-strict-candidate`，因此这次不能作为严格诊断结果。
  - 2026-07-02 当前 EXE 运行时地址以 scanner/dumpbin 输出为准：`_isBloodParticle` 其中一处检查签名唯一命中 `0x1407E4FB4`，hook 点 `0x1407E4FBC`；`_renderDecalOnTerrain` 检查签名唯一命中 `0x1435773F6`，hook 点 `0x1435773FA`。此前脚本把 raw file offset 当成 RVA，记录的 `0x1407E43B4` / `0x1435767F6` 属于文件偏移误算地址，不能作为运行时地址使用。
  - 2026-07-02 第七轮只读诊断：新增 `ScanForBloodParticleRuntimeCheck()` / `ScanForTerrainParticleRuntimeCheck()`，分别钩到 `cmp byte ptr [RCX+0x135],0` 和 `cmp byte ptr [RCX+0x12B],0` 指令本身；`src/hooks/blood_decals.cpp` 新增可选 runtime 诊断 hook，直接从真实游戏运行时代码的 `RCX` 记录 `DIAG particle-runtime source=... ptr=... q68/q70/q78/d78/d80/+0x119/+0x12B/+0x135/bytes118`。该诊断仍只读，不写任何粒子字段；若 AOB 或 hook 创建失败，仅记录日志，不影响已确认有效的 Skinned 血迹延长。
  - 2026-07-02 第七轮 Release 构建通过，输出 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`，时间 13:44:20；`git diff --check` 只有 CRLF 提示；当前 EXE 中两个 runtime hook AOB 均唯一命中；静态检查确认无 `SafeWriteFloat`，无 `+0x80/+0x119/+0x12B/+0x135` 写入，唯一写入仍是 `SafeWriteDword(candidate + 0x78, target_life)`；游戏目录 ASI 时间仍为 09:42:51，未由 Codex 写入。
  - 2026-07-02 用户第七轮日志确认新 ASI 已加载到游戏目录，`blood135-check` 安装于 `0x1407E4FBC`，`terrain12B-check` 安装于 `0x1435773FA`；13:48 左右真实 runtime terrain 路径抓到 2 个对象，均为 `terrain12B=1 blood135=0`，且 `d78/d80` 分别为 `16777217/1341456385`、`16777217/1`，不像已知 `3000/1500` 生命周期字段，因此不能写 `+0x78/+0x80`。
  - 2026-07-02 同轮日志中没有 `source=blood135-check` 的 `DIAG particle-runtime` 样本，说明已 hook 的 `0x1407E4FBC` 入口没有捕获到可确认的血液粒子对象；dumpbin 小范围反汇编确认另一处 `0x14044A8B9` 也读取 `RCX+0x135`，且签名 `49 8B 0E C5 C8 57 F6 80 B9 35 01 00 00 00 75 ...` 在当前 PE 中按 RVA 唯一命中 `0x14044A8B2`，hook 点 `0x14044A8B9`。
  - 2026-07-02 第八轮只读诊断：新增 `ScanForBloodParticleSimulationCheck()`，安装第三个 runtime 探针 `blood135-sim-check` 到 `0x14044A8B9`；同时把 `IsLikelyRuntimeHeapPointer()` 加上 `uintptr_t` 对齐要求，减少 effect-list 严格诊断里的非对象假阳性。仍不写任何粒子字段。
  - 2026-07-02 第八轮 Release 构建通过，输出 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`，时间 14:02:03；`git diff --check` 只有 CRLF 提示；按 PE RVA 校验三个 runtime AOB 均唯一命中：`0x14044A8B2`、`0x1407E4FB4`、`0x1435773F6`；静态检查确认唯一写入仍是 `SafeWriteDword(candidate + 0x78, target_life)`；游戏目录 ASI 时间仍为 13:44:20，未由本次构建写入。
  - 2026-07-02 Ghidra 自动分析状态核对：`/analysis_status?program=CrimsonDesert.exe` 返回 `analyzing=false analyzed=false should_ask_to_analyze=true function_count=207361`，说明项目此前没有全量分析完成。
  - 2026-07-02 已通过 `/reanalyze?program=CrimsonDesert.exe` 启动 Ghidra 全量自动分析；启动后状态变为 `analyzing=true analyzed=false should_ask_to_analyze=false function_count=207366`。分析持续运行超过半小时，Java 进程 CPU 从约 76 秒增长到 2476 秒，说明没有卡死。为避免 Codex 一直阻塞在 HTTP 响应等待上，已停止本地 PowerShell 等待客户端，但复查 `/analysis_status` 仍为 `analyzing=true`，Ghidra Java 进程仍在运行，自动分析未被停止。
  - 2026-07-03 Ghidra 全量分析完成：`analyzing=false analyzed=true function_count=220524`。全局 `search_instructions` 对 `0x20B/0x2B0/0xD0` 操作数搜索仍非常慢，已改用本地 PE 字节扫描 + 小范围函数边界解析。
  - 2026-07-03 本地 PE 扫描确认粒子 runtime 更新函数 `FUN_14069D5F0`：`runtime+0xD0 -> config`，`runtime+0x20B` 为真时调用 `FUN_140696FD0(runtime+0x2B0, ...)`；`FUN_140696FD0` 将 `param+0x28/+0x2C` 发布为 `_particleLifeTimeMin/_particleLifeTimeMax`，对应 runtime 字段 `+0x2D8/+0x2DC`。
  - 2026-07-03 本地 PE 扫描确认 `FUN_140698F80` 在 `runtime+0x20B` 为真时读取 `runtime+0x2DC` 并参与时间计算；`FUN_14069AED0` 在同一分支读取 `runtime+0x34C/+0x350/+0x354` 等粒子生成参数，说明 `+0x2B0` 参数块进入运行时路径，不只是反射/编辑器数据。
  - 2026-07-03 新增实现中：准备增加 `ScanForBloodParticleLifetimeUpdate()`，锚定 `FUN_14069D5F0` 早期 `mov rax,[rdi+0xD0]` 唯一 AOB；hook 中从 `runtime+0xD0 -> config` 读取 `config+0x135` (`_isBloodParticle`) 和 `config+0x12B` (`_renderDecalOnTerrain`)，满足任一标志时才考虑写 `runtime+0x2D8/+0x2DC` 为配置寿命秒数；不写 `+0x119/+0x12B/+0x135` 标志位。
  - 2026-07-03 源码已新增 `ScanForBloodParticleLifetimeUpdate()` 和 particle lifetime mid-hook：hook 条件为 `runtime+0x20B != 0` 且 `config+0x135 == 1` 或 `config+0x12B == 1`，并且当前 `runtime+0x2D8/+0x2DC` 至少一个为正且尚未达到目标寿命；写入 `float LifetimeSeconds` 到 `runtime+0x2D8/+0x2DC`。选择 OR 条件的原因是上一轮真实 runtime 日志已捕获 `terrain12B=1 blood135=0` 的地面渲染对象，地面血迹很可能不设置 `_isBloodParticle`。
  - 2026-07-03 Release 构建通过，输出 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`，大小 474112，时间 16:26:19；游戏目录 `D:\steam\steamapps\common\Crimson Desert\bin64\crimson-desert-tweaks.asi` 仍为 2026-07-02 13:44:20，模组目录 `D:\红色沙漠工具\模组\Crimson Desert Tweaks\crimson-desert-tweaks.asi` 仍为 2026-07-01 09:47:36，确认本次没有自动安装。
  - 2026-07-03 复查发现早期 hook 点 `0x14069D6EE` 位于 `FUN_140696FD0(runtime+0x2B0, ...)` 调用之前，写入 `runtime+0x2D8/+0x2DC` 可能会被游戏在同一次更新中覆盖。已把 `ScanForBloodParticleLifetimeUpdate()` 改为锚定 `runtime+0x20B` 分支内的 post-copy 位置：AOB 从 `80 BF 0B 02 00 00 00 ... E8 ... 48 8B 85 80 01 00 00` 开始，hook offset `0x1C`，当前 EXE 窗口扫描唯一命中 `0x14069DCAD`，hook 点 `0x14069DCC9`。
  - 2026-07-03 已同步 `docs/reverse-engineering/blood-decals.md`，记录粒子 runtime 生命周期路径、post-copy hook 原因、AOB、写入字段和版本更新复查步骤。
  - 2026-07-03 post-copy 修正后 Release 构建通过，输出 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`，大小 474112，时间 16:53:03；游戏目录 ASI 仍为 2026-07-02 13:44:20、模组目录 ASI 仍为 2026-07-01 09:47:36，确认没有自动安装。
  - 2026-07-03 最终静态检查：`git diff --check` 仅有 CRLF 换行提示；post-copy wide/exact AOB 在 `0x14069D000-0x14069E200` 窗口内各唯一命中 `0x14069DCAD`、hook `0x14069DCC9`；`SafeWrite` 调用点仅有 `runtime+0x2D8`、`runtime+0x2DC` 和 Skinned `candidate+0x78`；未发现写 `+0x80/+0x119/+0x12B/+0x135` 或粒子布尔标志位。
  - 2026-07-04 用户日志确认新版已安装到游戏目录：`blood-particle-lifetime-post-copy-wide-1.12.02` 唯一命中并安装 particle lifetime hook 于 `0x14069DCC9`；Skinned patch 继续有效（`totalPatched=22`），真实 terrain runtime 诊断仍出现 `terrain12B=1 blood135=0`；但整份日志没有 `particle lifetime patch`，说明当前问题不再是安装/AOB，而是 particle lifetime hook 未写入（可能未执行或被过滤条件返回）。
  - 2026-07-04 已给 `ParticleLifetimeUpdateCallback` 增加限流诊断 `DIAG particle-lifetime reason=...`，记录 hook 命中、runtime/config 指针、`uses20B`、`terrain12B/blood135`、`lifeMin/lifeMax/target` 和未写原因；停止日志新增 `particleLifetimeHits`。写入字段保持不变。
  - 2026-07-04 静态逆向继续确认地面血迹不应继续沿旧 particle lifetime hook 盲试：`FUN_141FB1950` 的 hit-material 查询由 `param_2` 选择列表，`param_2=8` 对应 `GroundBloodDecal/TargetGroundDecal`；调用点 `0x140548C2B` 从 `material+0x11A` 读取类别字节后调用 `FUN_141FB1950`。
  - 2026-07-04 定位 `FUN_1405497F0` 为 hit-material effect entry 构建函数：入口 `0x1405497F0`，`R9=material config`，第 7 参数 `[entry RSP+0x38]` 是正在构建的 0x1C0 entry；`material+0x11A == 8` 是 GroundBloodDecal 类别。
  - 2026-07-04 0x1C0 entry 生命周期链路证据：`FUN_1403A86A0` 把全局秒数 `DAT_14605AA38 * 1000` 写入 0x70 记录的 `+0x40`；`FUN_14039AC30` 后续把 0x70 记录 `+0x40` 复制到新对象相对 `rbx+0x38` 的 `+0x40`，即最终对象 `+0x78`，与已验证 Skinned 血迹寿命字段一致。因此 0x1C0 entry 的 `+0x40` 是当前最强 GroundBloodDecal 寿命候选。
  - 2026-07-04 已新增 `ScanForGroundBloodHitMaterialEffectEntryBuild()`，AOB 锚定 `FUN_1405497F0` 函数入口；`src/hooks/blood_decals.cpp` 新增独立 `ground blood hit-material entry hook`，只在 `material+0x11A == 8` 时把第 7 参数 entry 的 `+0x40` dword 毫秒写为 `[BloodDecals] LifetimeSeconds`，不写 `+0x80`、不写 `+0x119/+0x12B/+0x135`，也不删除旧 particle lifetime 诊断路径。
  - 2026-07-04 GroundBloodDecal entry hook 版本 Release 构建通过，输出 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`，大小 478720，时间 05:19:27；核对游戏目录 ASI 仍为 2026-07-04 00:13:41，模组目录 ASI 仍为 2026-07-01 09:47:36，确认本次没有自动安装。
  - 2026-07-04 静态检查：`git diff --check` 仅有 CRLF 换行提示；`SafeWrite` 调用点为旧 particle runtime `+0x2D8/+0x2DC`、已验证 Skinned `candidate+0x78`、新增 GroundBloodDecal entry `entry+kHitMaterialEffectEntryLifeMsOffset(0x40)`；未新增对 `+0x80`、`+0x119/+0x12B/+0x135` 的写入。
  - 2026-07-04 用户测试确认 `entry+0x40` 路线仍未延长地面喷溅血迹；重新静态追踪 terrain node 路径，确认 `FUN_143DFA040` 清理/淡出计算直接读取 final/terrain object 的 `+0x14` 与 `+0x160`，并把结果写入 node `+0x198/+0x19C/+0x1A8`。
  - 2026-07-04 继续逆向确认 `FUN_143D83710` 会刷新 terrain object 的可见结束时间：`0x143D83A31 movss [rbx+0x160], xmm0`，其中 `xmm0 = object+0x14 + 计算出的可见时长`；`FUN_143DFA040` 后续使用 `object+0x160`，因此这是比 `entry+0x40` 更接近地面血迹实际消失的字段。
  - 2026-07-04 v1.13.00 静态反证 terrain `+0x160` 写入路线：`FUN_143D9F150` 分配/初始化 terrain object，并在 `0x170` 分支初始化 `+0x158/+0x15C/+0x160/+0x164/+0x168/+0x16C`；`FUN_143E05840` 分配 `0x210` helper 写入 `object+0x08`，并把 `helper+0x200` 回指 object，反编译中出现 `_Malloc_MemoryMgr_AK` / `_Free_MemoryMgr_AK`，说明 `object+0x08` 不是此前假设的 particle emitter/config；`FUN_143D9EC00` 用 `candidate+0x70/+0x78/+0x80` 和 `param_1[0..2]` 比较查重，说明这三段是资源句柄链路。旧实现依赖 `object+0x08 -> config+0x12B` 和 `object+0xA0` Blood key 过滤，结构依据不成立。
  - 2026-07-04 已从源码撤掉 terrain `+0x160` 安装/写入路径：删除 scanner 声明/实现、terrain callback、hook 成员、计数器、安装/卸载和日志字段；当前保留写入点只有 Skinned `candidate+0x78`、particle runtime `+0x2D8/+0x2DC`、GroundBloodDecal entry `entry+0x40`。
  - 2026-07-04 撤掉 terrain hook 后 Release 构建通过，输出 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`，大小 `486400`，时间 `11:26:07`；游戏目录未检测到 `crimson-desert-tweaks.asi`，模组目录 ASI 仍为 `2026/7/1 9:47:36`，确认没有自动安装。
  - 2026-07-04 v1.13.00 继续静态追 GroundBloodDecal entry 参数链：`FUN_14055B020` 在 category=8 entry builder 内调用 `FUN_1408E8B70(entry+0x78, material+0x08, ...)`；`FUN_1408E8B70` 明确把 `material+0x5C` 同时写入参数图的 `"ParticleLifeTime"` 与 `"ParticleLifeTimeMax"`。这解释了为什么只写 `entry+0x40` 可能无法影响地面喷溅粒子寿命。
  - 2026-07-04 已在现有 GroundBloodDecal category=8 hook 内新增 `material+0x5C` float 秒数写入：命中 `material+0x11A == 8` 时先把 `material+0x5C` 写为 `[BloodDecals] LifetimeSeconds`，再保留原来的 `entry+0x40` 毫秒写入；新增日志 `ground blood particle-life param patch`、启动字段 `groundParticleLifeSec`、停止计数 `groundParticleParamPatches/groundParticleParamWriteFailed`。该实现不写 `+0x80`、不写粒子标志 `+0x119/+0x12B/+0x135`、不恢复 terrain `+0x160`。
  - 2026-07-04 `material+0x5C` 版本 Release 构建通过，输出 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`，大小 `487424`，时间 `11:49:15`；游戏目录未检测到 `crimson-desert-tweaks.asi`，模组目录 ASI 仍为 `2026/7/1 9:47:36`，确认没有自动安装。
  - 2026-07-04 v1.13.00 交接修正：恢复根目录 `crimson-desert-tweaks.ini` 默认模板为 `[BloodDecals]`；用户已删除旧 `[Blood]` INI，因此本轮不保留旧 section 兼容，继续只读取 `[BloodDecals] Enabled/LifetimeSeconds/ScanIntervalMs`。
  - 2026-07-04 v1.13.00 Skinned/面部血迹修正：日志中的 `.?AUGameSkinnedDecalElement@pa@@ life78Ms=1000 fade80Ms=0` 已确认为新版 timing；`IsExpectedBloodDecalTiming()` 只新增接受 `1000/0` 和已扩展后的 `target_life/0`，仍保持 RTTI 白名单，不写 `+0x80`，不放宽到所有 Decal。
  - 2026-07-04 构建规则修正：移除 CMake post-build 自动复制到模组目录的旧逻辑，构建产物继续只留在 `build/Release/`，安装由用户手动处理。
  - 2026-07-04 交接修正验证：新增 `crimson-desert-tweaks-tests` 覆盖 `[BloodDecals]` 配置读取和 Skinned timing；`ctest --test-dir build -C Release --output-on-failure` 通过。Release 构建通过，输出 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`，大小 `487424`，时间 `13:55:06`；游戏目录 ASI/INI 时间仍为 `11:49:15` / `2026/7/1 21:55:49`，DMM 模组目录未返回文件，确认本轮没有自动安装。
  - 2026-07-04 用户复测日志显示游戏已加载 `13:55:06` 新 ASI，`BloodDecals Enabled=1`，且 Skinned 路径已有 `patched/totalPatched` 增长；问题不再是 timing 拒绝，而是 v1.13.00 面部血迹原始 `life78=1000ms`，当前 `ScanIntervalMs=1000` 可能在对象消失后才扫描。已把 BloodDecals worker 的有效扫描间隔上限收紧到 `100ms`，默认模板同步为 `ScanIntervalMs=100`，启动日志新增 `effectiveInterval`；不新增字段写入，不恢复 terrain `+0x160`。
  - 2026-07-04 100ms 扫描修正验证：`ctest --test-dir build -C Release --output-on-failure` 通过；Release 构建通过，输出 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`，大小 `487424`，时间 `14:37:55`。游戏目录 ASI/INI 仍为 `13:55:06` / `2026/7/1 21:55:49`，确认本轮没有自动安装。
  - 2026-07-04 收尾文档/构建规则：README/README.zh-CN 已补充 Shield Hide 与 Blood Decals 当前状态、`[BloodDecals]` 配置、Release 输出说明、DMM 安装地址、参考 MOD/源码/本地逆向资料路径；`AGENTS.md` 新增规则，要求每次编译生成 ASI 后把默认 `crimson-desert-tweaks.ini` 放到 ASI 同层；`CMakeLists.txt` 增加 post-build `copy_if_different`，只复制到 `$<TARGET_FILE_DIR:crimson-desert-tweaks>`，不写游戏目录或模组目录。
  - 2026-07-04 Rumi's Blood mod 许可检查：本地 `D:\AI Agent Resources\GitHub\rumi_blood_mod\LICENSE` 为 MIT，但 Nexus 页面权限表同时限制文件/资产使用；README 已按保守口径改为“行为/兼容性参考”，明确本项目不包含、不再分发、不复制该模组文件或资产。
  - 2026-07-04 收尾验证：`ctest --test-dir build -C Release --output-on-failure` 通过；`cmake --build build --config Release --target crimson-desert-tweaks` 通过并执行 `Copying default INI next to ASI in build output...`；`build/Release/crimson-desert-tweaks.asi` 与 `build/Release/crimson-desert-tweaks.ini` 同层存在，输出 INI 与根目录模板 SHA256 均为 `0F67F08175CDC154BB59F2C34B7B97765E475AFE7059D948083BC5C75C34945E`。
- **下一步**:
  - 用户手动安装新的 ASI 和 `[BloodDecals] ScanIntervalMs=100` INI 后，后续日志优先看 Skinned `patched/totalPatched` 是否在伤害发生后立即增长，以及实际面部血迹是否延长。

## 下一步

- 新功能必须先说明目标、假设、涉及文件和验证方式。
