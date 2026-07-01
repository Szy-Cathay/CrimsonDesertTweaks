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

## 当前约束

- 构建产物只留在 `build/Release/`。
- 不自动复制到游戏目录或模组目录。
- 修改 `src/` 下源码时，继续实时更新本文件。

## 进行中

### 血迹残留时间延长 (Blood Decals)

- **状态**: ✅ 已实现，待游戏内确认
- **日期**: 2026-07-01
- **逆向证据**:
  - Ghidra 项目 `D:\ghidra project\CrimsonDesert` 当前打开 `CrimsonDesert.exe`。
  - 血迹/贴花管理器发布路径已定位到 `0x140712600` 附近，关键发布写入位于 `0x1407126DA`。
  - `0x1407126DA` 指令序列会发布 `rdi`、`rdi+0x130`、`rdi+0x1B0` 到 `0x1461E47F0` 附近的全局槽位。
  - 参考 ASI `CrimsonDesertBloodDecals.asi` 的可读字符串包含 `GameSkinnedDecalElement`、`life78`、`fade80`，并显示 `writeFade=disabled`。
- **实现决策**:
  - 只写 `GameSkinnedDecalElement` 对象的 `+0x78` float 生命周期字段。
  - `+0x80` 只读作 fade 诊断，不写入。
  - 尸体相关字符串如 `DeadBody`、`DeadActor`、`DeadCharacter`、`RagdollDynamicDead` 目前只作为后续逆向线索，不进入本次功能。
- **已完成**:
  - 新增 `[BloodDecals]` 配置读取：`Enabled`/`Enable` 通过 `ReadBoolAliasIfPresent`，`LifetimeSeconds` 与 `ScanIntervalMs` 带范围钳制。
  - 新增默认 `crimson-desert-tweaks.ini` 模板。
  - 新增 Blood Decals AOB 扫描函数 `ScanForBloodDecalActorManagerPublish()`，锚定 `0x1407126DA` 的 publish-store 指令序列。
  - 新增 `src/hooks/blood_decals.cpp`：启动 worker 后读取 publish-store 全局槽位，用 MSVC RTTI 校验 `GameSkinnedDecalElement`，只写 `object+0x78` 的 `life78`，`fade80` 只读不改。
  - 已在 `dllmain.cpp` 接入 Blood Decals 启动/停止，并在 `CMakeLists.txt` 加入新源文件。
  - Release 构建通过，产物位于 `D:\crimson-desert-tweaks\build\Release\crimson-desert-tweaks.asi`；本次构建已将 CMake 输出目录覆盖到仓库内 `build/Release`，没有复制到游戏目录或模组目录。
- **下一步**:
  - 需要一次游戏内运行确认日志中 `blood-decals: worker started` 与 patch/tick 统计，并观察血迹是否按 `LifetimeSeconds` 延长。

## 下一步

- 新功能必须先说明目标、假设、涉及文件和验证方式。
