# XST_Demo_F1xx — 掌静脉识别模组驱动示例 (STM32F103)

基于 STM32F103C8T6 的 [湘识科技](https://www.xiangshitech.com) 掌静脉识别模组驱动演示项目。集成 **xst_driver**（核心协议栈）、**Letter Shell**（交互式命令行）、**EasyLogger**（分级日志输出）。

---

## 硬件接线

| MCU Pin | 外设 | 连接对象 |
|---------|------|----------|
| PA9 (USART1_TX) | USART1 | 掌静脉模组 RX |
| PA10 (USART1_RX) | USART1 | 掌静脉模组 TX |
| PA2 (USART2_TX) | USART2 | Shell 串口 (用户终端) |
| PA3 (USART2_RX) | USART2 | Shell 串口 |
| PB10 (USART3_TX) | USART3 | 日志串口 (elog) |
| PB11 (USART3_RX) | USART3 | 日志串口 |

### 串口参数

| 串口 | 波特率 | 用途 |
|------|--------|------|
| USART1 | 115200 | 掌静脉模组通讯 |
| USART2 | 115200 | Letter Shell 命令行 |
| USART3 | 115200 | EasyLogger 日志输出 |

> Shell 和 elog 可共用同一串口，本示例为调试方便独立输出。

---

## 软件架构

```
main.c                    ← 应用入口：初始化 + 主循环
├── elog_init/start       ← EasyLogger (USART3)
├── Port_Shell_RTT_Init   ← Letter Shell (USART2)
├── xst_port_init         ← 掌静脉驱动初始化 (USART1)
│   └── xst_create_device
│   └── xst_registe_cfg_and_ops
│   └── xst_init
├── xst_set_event_callback ← 注册事件回调
└── while(1)
    ├── xst_process_all()   ← 轮询处理模组数据
    └── Port_Shell_RTT_Task() ← Shell 任务
```

### 分层结构

```
┌─────────────────────────────────────────┐
│  Application Layer                      │
│  main.c — 事件回调 / Shell 命令 / 日志   │
├─────────────────────────────────────────┤
│  Services                               │
│  Letter Shell / EasyLogger / xst_driver │
├─────────────────────────────────────────┤
│  BSP (Board Support Package)            │
│  xst_driver/porting — 硬件适配层         │
│  lwrb — 环形缓冲区                       │
├─────────────────────────────────────────┤
│  HAL / LL Driver (STM32CubeMX 生成)     │
└─────────────────────────────────────────┘
```

---

## Shell 命令列表

| 命令 | 功能 |
|------|------|
| `log_test` | 输出测试日志 (info/warn/error) |
| `xst_res` | 复位掌静脉模组 |
| `xst_vfy` | 验证/识别手掌（需靠近传感器） |
| `xst_dell_all` | 删除所有已注册用户 |
| `xst_regeid` | 注册用户 (admin=1, name="water", timeout=10s) |
| `help` | 查看所有命令 |
| `clear` | 清屏 |

---

## 构建与烧录

### CubeMX + CMake (推荐)

```bash
# 配置
cube-cmake -S . -B build/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug

# 构建
cube-cmake --build build/Debug

# 烧录 (需 ST-Link)
cube-cmake --build build/Debug --target flash
```

### 手动烧录

使用 STM32CubeProgrammer 或 J-Flash 直接加载 `build/Debug/xst_demo_f1xx.hex`。

---

## xst_driver 快速集成

### 1. 复制文件

```
你的项目/
├── bsp/
│   ├── lwrb/lwrb.h, lwrb.c
│   └── xst_driver/
│       ├── xst_drv.h, xst_drv.c
│       ├── xst_pack_t.h
│       └── porting/
│           ├── xst_porting.h
│           └── xst_porting.c    ← 按平台改写
```

### 2. 实现 xst_porting.c

参考 `bsp/xst_driver/porting/` 下的模板，需实现 4 个关键函数：

```c
static int  hw_init(void *ctx);      // 开启 UART 中断接收
static int  hw_send(void *ctx, const uint8_t *data, size_t len);  // UART 发送
static uint32_t get_tick_ms(void *ctx);  // 系统毫秒时间戳
// ISR 中调用 xst_write_rx_data(dev, &byte, 1) 喂数据
```

### 3. 初始化与主循环

```c
#include "xst_drv.h"
#include "xst_porting.h"

static void on_event(xst_handle_t h, const xst_event_t *e, void *ctx) {
    switch (e->type) {
    case XST_EVENT_REPLY:
        printf("REPLY mid=0x%02X result=%d\n", e->reply.mid, e->reply.result);
        break;
    case XST_EVENT_NOTE:
        if (e->note.note_id == NID_READY)
            printf("Module ready\n");
        break;
    default:
        break;
    }
}

int main(void) {
    /* 平台初始化 */
    HAL_Init(); SystemClock_Config();
    MX_USART1_UART_Init();

    /* 驱动初始化 */
    xst_port_init();
    xst_set_event_callback(xst_porting_get_device(), on_event, NULL);

    while (1) {
        xst_process_all();  /* 轮询解析帧 */
        /* 用户其他任务 */
    }
}
```

### 4. 发送命令

```c
xst_handle_t dev = xst_porting_get_device();

/* 无参数命令 */
xst_reset_module(dev);                    // MID_RESET
xst_delete_all(dev);                      // MID_DEL_ALL

/* 有参数命令 */
uint8_t uid = 678;
xst_delete_user(dev, uid);                // MID_DEL_USER
xst_enroll(dev, 1, "water", 10);          // MID_ENROLL_SINGLE

/* 识别（需靠近传感器） */
uint16_t len = 64;
uint8_t  user_id[64];
xst_verify(dev, user_id, &len);           // MID_VERIFY
```

---

## 移植到其他平台

详细的移植指南和示例（STM32 HAL、STM32 标准库、GD32、ESP32、通用轮询模式）请见 [`bsp/xst_driver/XST_DRIVER.md`](bsp/xst_driver/XST_DRIVER.md)。

---

## 常见问题

### Q: 命令返回 -1

检查 UART 接线和波特率（默认 115200）。确认模组上电后终端出现 `Device is ready` 日志。

### Q: `xst_res` 返回 -1

模组复位后先回复 REPLY 再发送 NID_READY，驱动已做保护防止 NOTE 覆盖 REPLY。如果仍超时，检查逻辑分析仪确认复位帧 `EF AA 10 00 00 10` 已发出。

### Q: 注册/识别进度不显示百分比

elog 默认以字符串打印 data 字段，本示例已将进度值单独解析为百分比输出。

### Q: 如何查看原始协议帧？

开启 elog 后，终端会输出 `REPLY mid=0x%02X result=%d` 和十六进制数据转储。

---

## 相关文档

| 文档 | 位置 | 说明 |
|------|------|------|
| 驱动参考手册 | `bsp/xst_driver/XST_DRIVER.md` | 完整 API 说明、移植指南、事件处理 |
| 通讯协议 V1.1 | `E:\Desktop\毕业\掌静脉资料\` | 湘识科技模组官方协议文档 |
| Letter Shell | https://github.com/NevermindZZT/letter-shell | 嵌入式 CLI |
| EasyLogger | https://github.com/armink/EasyLogger | 轻量日志库 |
| lwrb | https://github.com/MaJerle/lwrb | 环形缓冲区 |

---

*STM32F103C8T6 xst_demo — 2026-05*
