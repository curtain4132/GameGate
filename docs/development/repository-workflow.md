# GameGate 开发仓库说明

更新日期：2026-11-02。

## 1. 目录与仓库职责

- `Code_Source/RetroArch`：保留为上游参考，原有 master 分支和工作区不作产品开发改动。
- `Code_Source/GameGate`：正式二开仓库，拥有独立 `.git` 和 Git 对象，不依赖共享 worktree 或 alternates。
- GameGate 根目录即 RetroArch 派生工程根目录，沿用上游源码布局，不能把它误认为仅有 UI 的独立壳工程。
- [产品需求](../requirements/gamegate-product-requirements-v0.1.md)和[文档导航](../README.md)是 GameGate 的产品入口；根目录上游 README、许可和贡献记录暂时保留原样。

## 2. 基线与分支

| 名称 | 用途 |
|---|---|
| `gamegate-base/retroarch-11e8931021eb` | 标记引入的原始上游提交，长期保留，不移动 |
| `main` | GameGate 已整合的稳定开发基线；当前仅为仓库准备基线，不表示产品已发布 |
| `develop` | 日常开发集成分支，当前工作分支 |
| `feat/<topic>` | 后续单一功能的开发分支，需要时从 develop 创建 |
| `upstream/master` | 最近获取的上游 master 引用，不用于直接开发产品功能 |

上游基线完整提交：`11e8931021eb6b7fa69c04f52dd5d385841fdbbe`。

本地从已存在的 RetroArch 仓库复制该提交及可达祖先历史；不是浅克隆，没有复制上游所有标签或其他分支。基线程序显示 1.22.2，但这不等于检出了 v1.22.2 发布标签。

## 3. 远端

- `upstream`：`https://github.com/libretro/RetroArch.git`，用于获取官方更新。
- `origin`：`https://github.com/curtain4132/GameGate.git`，用户已创建的 GitHub Fork，用于保存 GameGate 开发成果。
- 2026-11-02 已获取 origin/master，其提交为 `11e8931021eb6b7fa69c04f52dd5d385841fdbbe`，与本地上游基线一致，没有历史分叉。
- 远端当前默认分支为 master；本地 main、develop 和基线标签尚未推送，未修改远端默认分支。
- Git 默认会为 upstream 显示相同 fetch／push 地址；没有设置阻止推送的技术屏障，操作时明确不要向 upstream 推送 GameGate 改动。

连接 origin 和获取远端引用不等于推送。首次推送范围建议为 main、develop 及 `gamegate-base/retroarch-11e8931021eb` 标签，确认发布范围后再执行；其后可将产品默认分支设为 main，保留远端 master。

## 4. 开发与提交规则

1. 从 develop 创建具体功能分支，先确定该批次设计和验收范围。
2. 保持上游目录结构，按现有模式扩展，不以品牌改名为由全仓替换 RetroArch 字符串。
3. 保留版权、许可证及必要署名。主程序遵循 GPL-3.0，第三方核心单独核查。
4. 遵循 [AGENTS.md](../../AGENTS.md) 与 [编码规范](../../CODING-GUIDELINES)，相关子目录规则同样适用。
5. 每次只按明确文件路径暂存，禁止把整个目录、构建产物或运行数据一起提交。
6. 新增和修改代码完成相应回归；不追加与变更无关的上游全面验证。
7. 日常提交不等于自动推送，合并与发布按任务授权执行。

[.gitignore](../../.gitignore) 保留上游全部规则，增加 `/artifacts/`。该目录保存本地编译结果、测试核心、截图、日志和仓库准备时的文档哈希备份，均不提交。

## 5. 同步上游

建议在独立集成分支更新，而不是边开发功能边直接合并上游：

```bash
git fetch upstream
git switch develop
git switch -c integration/upstream-YYYYMMDD
git merge upstream/master
```

执行前确认工作区状态，解决冲突后运行受影响检查，再合回 develop；可发布的集成结果再进入 main。

不要对包含 GameGate 改动的分支执行 `reset --hard upstream/master`，也不要对已共享的分支任意改写历史。

## 6. 本轮准备与保留证据

- 在引入源码前记录现有四份文档的 SHA-256，并备份原忽略文件。
- 通过上游树核对现有文档和 artifacts 没有同名跟踪路径，只有 `.gitignore` 需要合并。
- 引入源码后验证四份文档哈希全部未变，再有针对性地更新仓库状态和文档导航。
- 保留此前构建产物及运行数据。
- 首次尝试因当前 bash 缺少 sha256sum，在 Git 初始化前中止；改用 Windows Get-FileHash 后完成。
- 首次初始化提交只包含 GameGate 文档和忽略规则，没有业务源码改动。

原始基线的构建证据见 [Windows 验证记录](../research/2026-11-02-retroarch-windows-baseline.md)。该历史记录描述仓库准备前状态，不要求重复原版通用验证。

## 7. 仓库就绪后的首批任务建议

先完成“本地游戏库 → 选择游戏与核心 → 应用内运行 → 返回”的详细设计和改动包，复用现有任务加载、菜单及运行能力。在线资源源、资料服务和核心下载在稳定的游戏身份及接口边界上逐步接入。

UI 接入方式、首版平台及具体功能范围仍按需求基线确认，不因仓库准备完成就宣称这些实现选择已经获批。本轮尚未开始业务代码实现。

## 8. 本地工作记录与正式成果（2026-11-06）

- 临时计划、过程日志和调查记录放在项目根目录的 `.work-notes/`，由 `.gitignore` 忽略。文件名仍为 `task_plan.md`、`progress.md`、`findings.md`，不在仓库根目录重复创建。
- 本次迁移保留三份记录的完整本地内容，并从当前版本的跟踪文件中移除旧路径；已提交过的历史仍可通过 Git 查看。新克隆不会包含本地记录，需要时自行创建目录。
- 正式设计、交付说明继续保存在 `docs/`，可复用检查工具继续保存在 `tools/`。重要结论需整理为正式成果，不能只存在于本地过程日志。
- 不因每次更新过程记录而自动提交。按用户授权，在有意义且验证通过的阶段提交；推送仍单独按授权执行。
- 待确认的设计稿保持草稿状态，不随无关工具或功能提交。构建包、SDK、缓存与测试产物继续保存在忽略的 `artifacts/`。
