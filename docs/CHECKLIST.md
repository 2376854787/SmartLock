# 实施进度 Checklist（可直接复制到笔记软件）

> 勾选规则：以“模块 DoD 达成”为准（可在每项后补充链接/commit/测试结果）。

## Phase 1: 基础设施 (Week 1-2)
- [ ] 定义 `compiler.h`, `utils_def.h`, `ret_code_t` (L3)
- [ ] 搭建 CMake 工程结构 (App, Core, Drivers, Tests)
- [ ] 实现日志系统 `log.c` (支持 Hexdump, Level) (L2/L3)
- [ ] 实现并测试 `ring_buffer.c` (PC 端 Unity 测试通过, 零拷贝接口) (L3)

## Phase 2: 系统抽象与容器 (Week 3-4)
- [ ] 定义并实现 `hal_gpio.h`, `hal_uart.h` (L2)
- [ ] 实现侵入式链表 `list.h` (L2)
- [ ] 实现软件定时器 `soft_timer.c` (L2)
- [ ] 实现自动初始化 `auto_init` 机制 (L3)

## Phase 3: 通信与架构 (Week 5-6)
- [ ] 实现串口 DMA + RingBuffer 收发模型 (L3)
- [ ] 实现 EventBus 事件总线 (L2)
- [ ] 实现 CRC16/32 库 (L2)
- [ ] 实现简易 CLI 命令行 (L2)

## Phase 4: 协议与存储 (Week 7-8)
- [ ] 实现 AT 协议解析状态机 (L2)
- [ ] 实现 Config 存储与校验 (CRC + Magic) (L2)
- [ ] 实现二进制协议封包/解包 (TLV + 转义) (L2)

## Phase 5: 可靠性与完善 (Week 9+)
- [ ] 实现 HardFault 异常捕获与寄存器打印 (L2)
- [ ] 完善单元测试，生成覆盖率报告 (L3)
- [ ] 代码静态分析 (Cppcheck/Lint) 清除所有警告 (Commercial)

