# Fastpipe 实现计划

## 目标与假设

实现一个 Windows 命令行工具 `fp`，用大缓冲匿名管道替代命令提示符中的普通 `|`，让多个进程通过二进制流传输数据时减少阻塞和吞吐下降。

当前仓库只有 `README.md`，没有既有源码、构建系统或测试，因此按全新 C++/Win32 项目设计。首版以 Windows 10+、64 位构建为主，同时保持 Win32 API 的实现方式；`FP_BUFFERSIZE` 未在 README 中规定默认值，计划先使用 1 MiB，并集中放在配置模块中方便调整。

## 推荐架构

1. **CLI 与命令解析层**
   - 读取 Windows 原始命令行并解析参数，识别值严格等于 `|` 的参数；这对应 README 中必须写成 `"|"` 的用法，因为未加引号的 `|` 会先被 `cmd.exe` 当作 shell 操作符处理。
   - 支持两个或多个命令组成的链式管道，例如 `fp producer "|" filter "|" consumer`。
   - 在启动前校验空命令、连续管道符、首尾管道符和无法重建的参数，并输出明确的错误信息。
   - 为每个阶段保留可传给 `CreateProcessW` 的完整命令行，正确处理空格、引号、反斜杠和空参数。

2. **配置层**
   - 读取 `FP_BUFFERSIZE`，按无符号字节数解析并校验溢出、零值、非法字符和过大的值。
   - 统一提供默认值和允许范围，避免管道创建与 stdio 缓冲各自解析环境变量。
   - 将最终缓冲区大小传递给管道创建模块及可选 stdio shim。
   - 默认值为 16MB。为了防止用户输入脏数据，不接受小于4096的值和大于64MB的值。

3. **管道与进程编排层**
   - 为相邻命令创建匿名管道，并使用 `CreatePipe` 的缓冲区大小参数。
   - 第一阶段继承父进程 stdin，最后阶段继承父进程 stdout；中间阶段分别绑定前一条管道的读端和后一条管道的写端；stderr 默认继承父进程，避免改变诊断输出。
   - 使用 `STARTUPINFOEXW` 和显式句柄列表控制继承范围，只让目标子进程获得自己的 stdin/stdout 句柄。
   - 每个子进程成功创建后立即关闭父进程持有的无用端点，确保消费者在生产者结束后能收到 EOF，避免因父进程仍持有写端而永久等待。
   - 处理创建失败、部分启动成功和清理路径；为进程和管道句柄建立 RAII 封装。
   - 等待所有阶段结束，并以最后一个阶段的退出码作为 `fp` 的默认退出码；若启动阶段失败则返回独立的启动错误码。
   - 使用 Job Object 或等价的清理策略，确保 `fp` 被异常终止时不会遗留子进程。

4. **stdio 大缓冲组件**
   - 将 README 描述的 `_setmode`/`setvbuf` 逻辑做成独立的 `fp_stdio_hook` 组件，注入到创建的子进程中。
   - 子进程以挂起状态创建后，按配置把 shim 加载到子进程，再恢复执行；shim 监听 C runtime 将 stdin/stdout（不含stderr） 切换到二进制模式的 `_setmode` 调用，并在成功切换后为对应流设置更大的全缓冲区。
   - 使用detours库进行Hook。
   - 自己程序本身静态链接到C运行库；目标进程需要支持以下DLL；最好能监控DLL的载入事件，如果有新出现的VC运行库那么也进行hook。不需要考虑目标程序是静态链接到C语言运行库的情况。
     - msvcrt.dll
     - msvcr70.dll
     - msvcr71.dll
     - msvcr80.dll
     - msvcr90.dll
     - msvcr100.dll
     - msvcr110.dll
     - msvcr120.dll
     - vcruntime140.dll
   - 使用如下方式对比 `_setmode` 的第一个参数是不是stdio。注意：这些函数都要从对应的dll中获得指针来调用，不能使用自己DLL链接的。
     - _fileno(__acrt_iob_func(0)) // stdin
     - _fileno(__acrt_iob_func(1)) // stdout
   - 将缓冲区分配、释放、重复调用保护和 hook 失败降级处理封装在 shim 内；不能 hook 的程序仍应正常运行，只是不获得额外 stdio 缓冲优化。
   - 明确记录位数和 CRT 限制：shim 必须与目标进程位数匹配，优先支持动态 UCRT；静态链接或非 MSVC CRT 的进程走无 hook 降级路径。
   - 核心管道功能不依赖 shim，便于先验证吞吐与正确性，再逐步加入运行时优化。
   - 需要通过 LdrRegisterDllNotification 监听，如果加载了新的C运行库，那么也要进行Hook。

## 计划新增的文件

- `CMakeLists.txt`：配置 C++ 标准、Unicode、Win32 链接库、主程序、shim 和测试目标。
- `src/main.cpp`：程序入口、错误码和整体生命周期编排。
- `src/cli_parser.h/.cpp`：原始命令行解析、管道阶段拆分和 Windows 参数重建。
- `src/config.h/.cpp`：`FP_BUFFERSIZE` 解析、默认值、范围校验及诊断信息。
- `src/win_handle.h`：进程、线程、管道和 Job Object 的 RAII 句柄封装。
- `src/pipe_chain.h/.cpp`：匿名管道创建、句柄继承属性和端点生命周期管理。
- `src/process_launcher.h/.cpp`：`CreateProcessW`、`STARTUPINFOEXW`、显式句柄列表、Job Object、等待和退出码处理。
- `src/stdio_hook/`：独立的 shim DLL、`_setmode` hook、`setvbuf` 配置和兼容性降级逻辑。
- `tests/`：解析器单元测试、配置测试、测试生产者/消费者程序和 Windows 集成测试脚本。
- `README.md`：补充构建方式、默认缓冲区大小、环境变量格式、管道限制、退出码和 shim 兼容性说明。

## 实施顺序

1. 建立 CMake 工程和基础 RAII/错误处理代码，先让空命令和错误输入可诊断。
2. 实现参数拆分及 Windows 命令行重建，并用单元测试覆盖引号、空格、反斜杠、空参数和多个 `|`。
3. 实现 `FP_BUFFERSIZE` 配置解析及边界测试。
4. 实现单条大缓冲管道和两个子进程的启动、句柄关闭、EOF 传递、等待及退出码传播。
5. 扩展到任意数量的管道阶段，加入启动失败回滚和 Job Object 清理。
6. 实现独立 stdio shim；先验证 shim 不可用时核心链路仍可运行，再验证二进制模式切换后缓冲区设置生效。
7. 完善文档、错误信息和构建/测试脚本，并在 Windows 环境执行完整验证。

## 验证方案

- **解析与配置单测**：正常命令、多个阶段、缺少命令、非法管道位置、引号参数、非法/溢出/零值 `FP_BUFFERSIZE`。
- **数据完整性集成测试**：生成超过默认管道容量的随机二进制数据，经一至多级 `fp` 管道传输后比较字节数和 SHA-256，确认没有文本模式转换或截断。
- **生命周期测试**：生产者提前退出、消费者提前退出、消费者持续等待 EOF、子进程返回非零码、某一阶段无法启动、stderr 不被重定向。
- **配置测试**：比较不同 `FP_BUFFERSIZE` 下的运行结果和可观测缓冲配置；验证过大或非法值能在启动子进程前失败。
- **shim 兼容性测试**：动态 UCRT 程序、静态 CRT 程序、不同位数程序及不调用 `_setmode` 的程序，均不得破坏正常执行。
- **构建验证**：使用 `cmake -S . -B build`、`cmake --build build --config Release` 和 CTest/PowerShell 集成脚本验证 Debug/Release 构建。

## 验收标准

- README 示例命令可直接运行，并能传输大于默认管道缓冲区的二进制流。
- `FP_BUFFERSIZE` 能可靠控制管道大小，非法配置不会静默产生错误行为。
- 所有父子句柄在正确时机关闭，无死锁、无错误 EOF、无遗留子进程。
- 多阶段管道、错误退出和异常清理行为有自动化测试覆盖。
- stdio shim 作为增强能力可选启用，兼容性失败时不影响核心大缓冲管道功能。
