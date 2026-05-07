# XST 掌静脉驱动 v3.0 — 完整参考手册

基于《湘识科技掌静脉识别模组通讯协议 V1.1》，纯 C11 实现，零平台依赖，支持 STM32 HAL / 标准库 / GD32 / ESP32 / 轮询模式。

> **v3.0 更新 (2026-05)**
> - 重构业务命令 API：`xst_get_status`、`xst_enroll_single`、`xst_delete_user`、`xst_get_user_info`、`xst_snap_image`、`xst_get_saved_image_size` 函数签名改为直接传语义参数（如 `uint16_t user_id` 替代裸字节指针），内部自动打包为协议帧格式
> - `xst_verify` 补充 `MID_VERIFY` 协议要求的 2 字节参数 `{pd_rightaway, timeout}`
> - `xst_enroll` 数据布局修正：direction 和 timeout 写入正确偏移，消除栈垃圾
> - `xst_dispatch_event` 保护机制：已收到 REPLY 事件时不被后续 NOTE 覆盖，解决 `xst_exec_cmd` 轮询丢事件问题
> - 事件回调数据打印改为 HEX 转储，支持多行显示
> - 详见 [CHANGELOG](./CHANGELOG.md)

---

## 目录

1. [代码分层架构](#1-代码分层架构)
2. [数据流](#2-数据流)
3. [帧协议](#3-帧协议)
4. [设备管理 API](#4-设备管理-api)
5. [事件系统 API](#5-事件系统-api)
6. [应用层事件处理](#6-应用层事件处理)
7. [数据处理 API](#7-数据处理-api)
8. [移植层 API](#8-移植层-api)
9. [配置宏说明](#9-配置宏说明)
10. [平台移植实例](#10-平台移植实例)
11. [优缺点分析](#11-优缺点分析)
12. [集成步骤](#12-集成步骤)

---

## 1. 代码分层架构

```
┌─────────────────────────────────────────────────┐
│  Application (main.c)                           │
│  事件回调 on_event() / shell / elog              │
├─────────────────────────────────────────────────┤
│  xst_drv.h          平台无关公共 API              │
│  xst_drv.c          核心引擎 (C11 + lwrb)        │
│  xst_pack_t.h       协议常量定义                  │
├─────────────────────────────────────────────────┤
│  xst_porting.h      移植接口约定                  │
│  xst_porting.c      平台适配层 (#if 条件编译)      │
├─────────────────────────────────────────────────┤
│  lwrb               环形缓冲区                   │
│  Hardware            UART / GPIO / SysTick        │
└─────────────────────────────────────────────────┘
```

### 分层原则

| 层 | 职责 | 禁止 |
|----|------|------|
| `xst_drv.h/c` | 设备生命周期、帧解析、事件分发 | 禁止包含 HAL / 标准库 / RTOS 头文件 |
| `xst_pack_t.h` | MsgID / Result / NoteID 枚举和宏 | 禁止包含平台头文件 |
| `xst_porting.h/c` | 实现 `xst_ops_t`，为中断/DMA/轮询接线 | 禁止将业务逻辑写入此层 |
| Application | 注册回调、发送命令、处理事件 | 禁止直接操作 `xst_device_t` 内部字段 |

### 设计模式

**基类句柄模式**：所有 API 接受 `xst_handle_t`（指向 `xst_base_device_t` 的指针），内部通过 `container_of` 还原完整设备结构体。

```
xst_handle_t  ─→  xst_base_device_t (公开)
                      │
              container_of()
                      │
                      ▼
              xst_device_t (私有，驱动内部可见)
```

**两阶段注册**：`xst_create_device()` 创建壳子 → `xst_registe_cfg_and_ops()` 注入硬件实现 → `xst_init()` 启动。分离"设备存在"和"设备可用"的概念，支持从 Flash/网络延迟加载配置。

**OPS 注入**：所有硬件操作通过函数指针表 `xst_ops_t` 注入，核心驱动与平台完全解耦。

---

## 2. 数据流

### 中断驱动（推荐）

```
UART RX ISR
    │
    ▼
HAL_UART_RxCpltCallback()          ← 平台层 (xst_porting.c)
    │
    ▼
xst_write_rx_data()                ← 公共 API (线程安全，lwrb 保证)
    │
    ▼
lwrb (rx_rb 环形缓冲区)             ← 字节缓冲
    │
    ▼
main loop: xst_process_all()
    │
    ▼
xst_process() 逐字节读取 lwrb       ← 核心引擎
    │  状态机: IDLE → HEADER_H → MSGID → LEN_H → LEN_L → DATA → CHECKSUM
    │  校验: xst_calc_checksum()
    │
    ▼
xst_dispatch_frame()               ← 按 MsgID 分类
    ├── MID_REPLY  → XST_EVENT_REPLY
    ├── MID_NOTE   → XST_EVENT_NOTE
    ├── MID_IMAGE  ┐
    ├── MID_LOG    ├→ XST_EVENT_DATA
    ├── MID_DAT    ┘
    └── 其他       → XST_EVENT_ERROR (-5)
    │
    ▼
xst_dispatch_event()               ← 保存到 last_event + 调用用户回调
    │
    ▼
用户回调 on_event(handle, event, ctx)
```

### 轮询模式（无中断）

```
main loop
    │
    ▼
xst_process() → ops.hw_receive() 轮询读 UART → lwrb → 解析 → 回调
```

---

## 3. 帧协议

```
┌──────────┬───────┬──────────┬──────────┬────────────┐
│ SyncWord │ MsgID │   Size   │   Data   │ ParityCheck│
│  2 bytes │ 1 byte│  2 bytes │  N bytes │   1 byte   │
│  0xEFAA  │       │ 大端序   │ 0≤N≤65535│   XOR      │
└──────────┴───────┴──────────┴──────────┴────────────┘

ParityCheck = XOR(MsgID, Size_H, Size_L, Data[0], ..., Data[N-1])
              SyncWord 不参与校验
```

### 帧类型分发

| MsgID | 方向 | Data 结构 | 驱动事件 |
|-------|------|-----------|----------|
| `0x00` MID_REPLY | M→H | `[mid(1)][result(1)][payload(N-2)]` | `XST_EVENT_REPLY` |
| `0x01` MID_NOTE | M→H | `[nid(1)][payload(N-1)]` | `XST_EVENT_NOTE` |
| `0x02` MID_IMAGE | M→H | `[image_data(N)]` (≤4000B) | `XST_EVENT_DATA` |
| `0x03` MID_LOG | M→H | `[log_data(N)]` (≤4000B) | `XST_EVENT_DATA` |
| `0x04` MID_DAT | M→H | `[dat_data(N)]` (≤4000B) | `XST_EVENT_DATA` |
| 其他 | H→M | 命令参数 | 不触发事件 |

### 校验和函数

```c
static uint8_t xst_calc_checksum(uint8_t msgid, uint16_t len, const uint8_t *data)
{
    uint8_t c = msgid;
    c ^= (uint8_t)(len >> 8);
    c ^= (uint8_t)(len & 0xFF);
    for (uint16_t i = 0; i < len; i++) c ^= data[i];
    return c;
}
```

---

## 4. 设备管理 API

### `xst_create_device()`

```c
xst_handle_t xst_create_device(char *name, uint8_t device_id);
```

创建设备壳子，cfg 设为默认值，ops 全部为 NULL。需后续调用 `xst_registe_cfg_and_ops()`。

| 参数 | 说明 |
|------|------|
| `name` | 设备名称，用于日志。不能为 NULL |
| `device_id` | 设备 ID，同一驱动实例中不可重复 |

返回值：成功返回句柄，失败返回 NULL（name=NULL / 设备数满 / ID 重复）。

---

### `xst_create_with_cfg_ops()`

```c
xst_handle_t xst_create_with_cfg_ops(char *name, uint8_t device_id,
                                     const xst_config_t *cfg, const xst_ops_t *ops);
```

一步到位：创建设备 + 注册配置。失败自动清理。推荐简单应用使用。

---

### `xst_registe_cfg_and_ops()`

```c
int xst_registe_cfg_and_ops(xst_handle_t handle,
                            const xst_config_t *cfg, const xst_ops_t *ops);
```

向已创建但未初始化的设备注入配置和硬件接口。返回 0 成功，-1 失败。

---

### `xst_get_handle()`

```c
xst_handle_t xst_get_handle(uint8_t device_id);
```

按 device_id 查找设备句柄。未找到返回 NULL。O(n) 遍历链表。

---

### `xst_unregister()`

```c
void xst_unregister(xst_handle_t handle);
```

从链表移除设备，释放静态内存池节点。句柄即刻失效。

---

### `xst_init()`

```c
int xst_init(xst_handle_t handle);
```

初始化流程：验证配置 → 初始化 lwrb(rx/tx) → 调用 `ops.hw_init()` → 状态设为 IDLE。返回 0 成功。

---

### `xst_deinit()`

```c
int xst_deinit(xst_handle_t handle);
```

调用 `ops.hw_deinit()` → 释放 lwrb → 重置解析器 → 清除事件回调。

---

### `xst_change_cfg()`

```c
int xst_change_cfg(xst_handle_t handle, const xst_config_t *new_cfg);
```

运行时更换配置。仅在 IDLE 状态下可更换。推荐：`deinit → change_cfg → init`。

---

### `xst_change_ops()`

```c
int xst_change_ops(xst_handle_t handle, const xst_ops_t *new_ops);
```

运行时更换硬件操作接口。

---

## 5. 事件系统 API

### `xst_set_event_callback()`

```c
int xst_set_event_callback(xst_handle_t handle, xst_event_callback_t cb, void *user_ctx);
```

注册事件回调。回调在 `xst_process()` 内同步执行，每设备一个回调。

```c
typedef void (*xst_event_callback_t)(xst_handle_t handle,
                                      const xst_event_t *event, void *user_ctx);
```

---

### `xst_event_t` 结构体

```c
typedef enum { XST_EVENT_NONE, XST_EVENT_REPLY, XST_EVENT_NOTE,
               XST_EVENT_DATA, XST_EVENT_ERROR } xst_event_type_t;

typedef struct {
    xst_event_type_t type;
    union {
        struct { uint8_t msgid, result; uint8_t *data; uint16_t len; } reply;
        struct { uint8_t note_id;        uint8_t *data; uint16_t len; } note;
        struct { uint8_t msgid;          uint8_t *data; uint16_t len; } data;
        struct { int code; } error;
    };
} xst_event_t;
```

> **重要**：`data` 指针指向驱动内部 `parsed_data[]`，仅在回调执行期间有效。如需跨调用使用，请在回调中 `memcpy` 到用户缓冲区。

### 事件错误码

| code | 含义 |
|------|------|
| -2 | 帧长度超过 `XST_PARSE_BUF_SIZE` (4096) |
| -3 | 校验和不匹配 |
| -4 | 帧接收超时 (> default_timeout_ms) |
| -5 | 未知 MsgID |

---

### `xst_get_event()`

```c
int xst_get_event(xst_handle_t handle, xst_event_t *event);
```

轮询获取事件（不依赖回调）。返回 1=有新事件，0=无事件，-1=参数无效。事件取出后内部清空。

> **注意**：轮询模式的 `data` 指针可能指向已失效的缓冲区。消费后立即读取数据。

---

### `xst_reset_parser()`

```c
void xst_reset_parser(xst_handle_t handle);
```

解析器置为 IDLE，丢弃部分帧缓冲。用于错误恢复或手动同步。

---

## 6. 应用层事件处理

驱动只管"收字节→组帧→校验→分类"，不关心帧内容。帧内容的数据结构随 `msgid` 不同而变化，全部由应用层回调处理。

### 核心分发逻辑

REPLY 帧体中 `event.reply.msgid` 标识"这是对哪条命令的应答"，`event.reply.data` 的结构完全由 msgid 决定。两层 switch：外层分事件类型，内层分 msgid/note_id。

### 完整事件处理骨架

```c
/* ================================================================
 * 各命令的应答处理函数（按 PDF 协议解析 data 结构体）
 * ================================================================ */

/* MID_RESET (0x10) — 无 data，只看 result */
static void handle_reset_reply(const xst_event_t *e) {
    if (e->reply.result == MR_SUCCESS)
        log_i("模组已复位");
}

/* MID_GETSTATUS (0x11) — data[0] = status (0/1/2/3) */
static void handle_status_reply(const xst_event_t *e) {
    if (e->reply.result == MR_SUCCESS) {
        uint8_t st = e->reply.data[0];
        log_i("模组状态: %s", st == MS_STANDBY ? "空闲" :
                              st == MS_BUSY    ? "忙碌" :
                              st == MS_ERROR   ? "错误" : "无效");
    }
}

/* MID_VERIFY / MID_VERIFY_BIOTYPE (0x12/0xA5)
   data: user_id(2B大端) + name(32B) + admin(1B) + unlockStatus(1B) */
static void handle_verify_reply(const xst_event_t *e) {
    if (e->reply.result == MR_SUCCESS && e->reply.len >= 36) {
        uint16_t uid = ((uint16_t)e->reply.data[0] << 8) | e->reply.data[1];
        char *name   = (char *)&e->reply.data[2];
        uint8_t adm  = e->reply.data[34];
        log_i("识别成功: ID=%d 名称=%s %s", uid, name, adm ? "[管理员]" : "");
        unlock_door();
    } else switch (e->reply.result) {
        case MR_FAILED4_TIME_OUT:      buzzer_beep(); break;
        case MR_FAILED4_UNKNOWN_USER:  led_red();    break;
        case MR_FAILED4_LIVENESS_CHECK: led_red();   break;
    }
}

/* MID_ENROLL_SINGLE / _ID16 (0x1D/0x1E) — data: user_id(2B) + direction(1B) */
static void handle_enroll_reply(const xst_event_t *e) {
    if (e->reply.result == MR_SUCCESS && e->reply.len >= 3) {
        uint16_t uid = ((uint16_t)e->reply.data[0] << 8) | e->reply.data[1];
        log_i("注册成功: ID=%d", uid);
        flash_save_user(uid);
    }
}

/* MID_DEL_USER / MID_DEL_ALL (0x20/0x21) — 无 data */
static void handle_delete_reply(const xst_event_t *e) {
    if (e->reply.result == MR_SUCCESS)
        log_i("删除成功");
    else if (e->reply.result == MR_FAILED4_UNKNOWN_USER)
        log_e("用户不存在");
}

/* MID_GET_ALL_USER_ID (0x24) — data[0]=count, 之后每2字节一个16位ID */
static void handle_user_list_reply(const xst_event_t *e) {
    if (e->reply.result == MR_SUCCESS && e->reply.len >= 1) {
        uint8_t  cnt = e->reply.data[0];
        uint8_t *p   = &e->reply.data[1];
        log_i("已注册用户数: %d", cnt);
        for (int i = 0; i < cnt; i++) {
            uint16_t id = ((uint16_t)p[0] << 8) | p[1]; p += 2;
            log_i("  [%d] ID=%d", i+1, id);
        }
    }
}

/* MID_GET_USER_INFO (0x22) — data: user_id(2B) + name(32B) + admin(1B) */
static void handle_user_info_reply(const xst_event_t *e) {
    if (e->reply.result == MR_SUCCESS && e->reply.len >= 35) {
        uint16_t uid = ((uint16_t)e->reply.data[0] << 8) | e->reply.data[1];
        char *name   = (char *)&e->reply.data[2];
        uint8_t adm  = e->reply.data[34];
        log_i("用户%d: %s %s", uid, name, adm ? "[管理员]" : "");
    }
}

/* MID_GET_VERSION (0x30) — data 为版本字符串 */
static void handle_version_reply(const xst_event_t *e) {
    if (e->reply.result == MR_SUCCESS)
        log_i("模组版本: %.*s", e->reply.len, e->reply.data);
}

/* ================================================================
 * 批量数据处理（IMAGE/LOG/DAT 多帧拼合）
 * ================================================================ */
static uint8_t  bulk_buf[64 * 1024];   /* 64KB 拼合缓冲区 */
static uint32_t bulk_offset = 0;

static void handle_image_chunk(const xst_event_t *e) {
    memcpy(&bulk_buf[bulk_offset], e->data.data, e->data.len);
    bulk_offset += e->data.len;
}

static void handle_bulk_complete(void) {
    log_i("批量数据接收完成: %d 字节", (int)bulk_offset);
    /* SD卡存储 / 屏幕显示 / 上传云端 */
    bulk_offset = 0;
}

/* ================================================================
 * 错误处理
 * ================================================================ */
static int retry_count = 0;

static void handle_error(int code) {
    switch (code) {
    case -2: log_e("帧长度超限");        break;
    case -3: log_e("校验和不匹配");       break;
    case -4: log_e("帧接收超时");         break;
    case -5: log_e("未知MsgID");          break;
    }
    if (++retry_count > 3) {
        log_e("连续通信失败，请检查硬件连接");
        xst_reset_parser(xst_porting_get_device());
        retry_count = 0;
    }
}

/* ================================================================
 * NOTE 通知处理
 * ================================================================ */
static bool module_ready = false;

static void handle_note(const xst_event_t *e) {
    xst_handle_t dev = xst_porting_get_device();
    switch (e->note.note_id) {
    case NID_READY:
        module_ready = true;
        log_i("模组就绪");
        /* 模组就绪后自动查询状态 */
        xst_send_command(dev, MID_GETSTATUS, NULL, 0);
        break;
    case NID_PALM_STATE:
        break;
    case NID_OTA_DONE:
        log_i("OTA升级完成");
        break;
    case NID_UNKNOWNERROR:
        log_e("模组未知错误");
        break;
    case NID_AUTHORIZATION:
        log_e("License验证失败");
        break;
    }
}

/* ================================================================
 * 主事件回调 — 两层 switch 分发
 * ================================================================ */
static void on_event(xst_handle_t h, const xst_event_t *e, void *ctx) {
    (void)h; (void)ctx;

    switch (e->type) {

    case XST_EVENT_NOTE:
        handle_note(e);
        break;

    case XST_EVENT_REPLY:
        switch (e->reply.msgid) {
        case MID_RESET:               handle_reset_reply(e);      break;
        case MID_GETSTATUS:           handle_status_reply(e);     break;
        case MID_VERIFY:              /* fallthrough */
        case MID_VERIFY_BIOTYPE:      handle_verify_reply(e);     break;
        case MID_ENROLL_SINGLE:       /* fallthrough */
        case MID_ENROLL_SINGLE_ID16:  /* fallthrough */
        case MID_ENROLL_SINGLE_ID32:  handle_enroll_reply(e);     break;
        case MID_DEL_USER:            /* fallthrough */
        case MID_DEL_ALL:             handle_delete_reply(e);     break;
        case MID_GET_ALL_USER_ID:     handle_user_list_reply(e);  break;
        case MID_GET_USER_INFO:       handle_user_info_reply(e);  break;
        case MID_GET_VERSION:         handle_version_reply(e);    break;
        case MID_GET_SAVED_IMAGE:     /* 图片总大小，用于判断拼合完成 */
            if (e->reply.result == MR_SUCCESS)
                handle_bulk_complete();
            break;
        default:
            log_i("REPLY mid=0x%02X result=%d", e->reply.msgid, e->reply.result);
            break;
        }
        break;

    case XST_EVENT_DATA:
        switch (e->data.msgid) {
        case MID_IMAGE: handle_image_chunk(e); break;
        case MID_LOG:   handle_image_chunk(e); break;  /* LOG 也拼合 */
        case MID_DAT:   handle_image_chunk(e); break;  /* DAT 也拼合 */
        }
        break;

    case XST_EVENT_ERROR:
        handle_error(e->error.code);
        break;

    default: break;
    }
}
```

### 数据结构速查

REPLY 帧中 `e->reply.data` 的结构随 msgid 变化，参考《通讯协议 V1.1》PDF 各指令章节：

| msgid | data 结构 | 字节数 |
|-------|-----------|--------|
| `MID_GETSTATUS`(0x11) | `status(1B)` — 0=待机 1=忙碌 2=错误 3=未初始化 | 1 |
| `MID_VERIFY`(0x12) | `user_id(2B大端) + user_name(32B) + admin(1B) + unlockStatus(1B)` | 36 |
| `MID_ENROLL_SINGLE`(0x1D) | `user_id(2B大端) + direction(1B)` | 3 |
| `MID_ENROLL_SINGLE_ID16`(0x1E) | `user_id(2B大端) + direction(1B)` | 3 |
| `MID_ENROLL_SINGLE_ID32`(0x1F) | `user_id(4B大端) + direction(1B)` | 5 |
| `MID_DEL_USER`(0x20) | 无 | 0 |
| `MID_GET_ALL_USER_ID`(0x24) | `user_cnt(2B大端) + user_id[user_cnt×2B大端]` | 2+2×cnt |
| `MID_GET_USER_INFO`(0x22) | `user_id(2B大端) + user_name(32B) + admin(1B)` | 35 |
| `MID_SNAP_IMAGE`(0x16) | 无 | 0 |
| `MID_GET_SAVED_IMAGE`(0x17) | `image_size(4B大端)` | 4 |
| `MID_GET_VERSION`(0x30) | 版本字符串 | 变长 |

### 关键注意事项

1. **`e->reply.data` 指针仅在回调期间有效**——指向驱动内部 `parsed_data[]`，回调返回后下一次 `xst_process()` 会覆盖。如需持久保存数据（如 image），请在回调中 `memcpy` 到用户缓冲区。
2. **批量数据（IMAGE/LOG/DAT）** 以 `XST_EVENT_DATA` 事件到达，而非 REPLY。驱动每帧触发一次回调，应用层负责拼合。
3. **`retry_count` 等全局状态** 在单线程环境下无需加锁，但若引入 RTOS 需自行保护。

---

## 8. 数据处理 API

### `xst_process()`

```c
int xst_process(xst_handle_t handle);
```

处理单个设备的一次轮询周期：
1. 如果 `ops.hw_receive != NULL`，轮询接收数据写入 lwrb
2. 逐字节从 lwrb 读取，驱动帧解析状态机
3. 完整帧校验通过后分发事件（回调或缓存）
4. 检测帧解析超时

---

### `xst_process_all()`

```c
int xst_process_all(void);
```

遍历所有已注册设备，逐个调用 `xst_process()`。应在主循环中高频调用。

---

### `xst_write_rx_data()`

```c
size_t xst_write_rx_data(xst_handle_t handle, const uint8_t *data, size_t len);
```

向设备内部 lwrb 写入原始字节。供 ISR 或平台接收层使用。返回实际写入字节数。lwrb 满时写入量可能少于 len。

---

### `xst_read_rx_data()`

```c
size_t xst_read_rx_data(xst_handle_t handle, uint8_t *data, size_t len);
```

从设备内部 lwrb 读取原始字节。供协议解析或自定义处理使用。

---

### `xst_send_command()`

```c
int xst_send_command(xst_handle_t handle, uint8_t msgid, const uint8_t *data, uint16_t len);
```

自动组装协议帧并发送。帧头(EFAA) + msgID + len(2B大端) + data + checksum(XOR) 全部由函数内部完成，用户只需传命令和数据。

| 参数 | 说明 |
|------|------|
| `handle` | 设备句柄 |
| `msgid` | 消息 ID，如 `MID_GETSTATUS`、`MID_VERIFY` 等 |
| `data` | 数据区指针，`NULL` 表示无参数 |
| `len` | 数据区长度，0 表示无参数命令。最大 256 字节 |

返回值：0 = 成功，-1 = 失败（无效句柄 / ops.hw_send=NULL / 数据超长 / 硬件发送失败）。

---

### `xst_exec_cmd()`

```c
int xst_exec_cmd(xst_handle_t handle, uint8_t msgid, const uint8_t *tx_data, uint16_t tx_len,
                 xst_reply_t *reply, uint32_t timeout_ms);
```

同步发送命令并等待模组回复。内部自动轮询 `xst_process()` 和 `xst_get_event()`，直到收到对应 msgID 的 REPLY 或超时。

| 参数 | 说明 |
|------|------|
| `handle` | 设备句柄 |
| `msgid` | 消息 ID |
| `tx_data/tx_len` | 发送数据（可 NULL/0） |
| `reply` | 回复内容输出（可 NULL，仅需结果时传 NULL） |
| `timeout_ms` | 等待超时（毫秒） |

返回值：`reply.result`（模组返回码，0=成功），-1 = 超时或通信失败。

所有高层业务 API（`xst_verify`、`xst_enroll` 等）内部均通过此函数实现：

```c
// xst_exec_cmd 实现示意
int ret = xst_send_command(handle, msgid, tx_data, tx_len);  // 发送
while (time_not_expired) {
    xst_process(handle);            // 轮询接收 + 解析
    if (xst_get_event(handle, &e) > 0 && e.type == XST_EVENT_REPLY) {
        if (reply) *reply = e.reply;
        return (int)e.reply.result; // 返回模组结果码
    }
}
return -1;  // 超时
```

> **注意**：`reply->pay_data` 指针指向驱动内部缓冲区，在下次 `xst_process()` 调用后失效。如需跨调用保存，请在回调或 `xst_exec_cmd` 返回后立即 `memcpy`。

---

**使用示例 — 底层发送**：

```c
xst_handle_t dev = xst_porting_get_device();

/* 无参数命令 */
xst_send_command(dev, MID_RESET,       NULL, 0);
xst_send_command(dev, MID_GETSTATUS,   NULL, 0);
xst_send_command(dev, MID_DEL_ALL,     NULL, 0);
xst_send_command(dev, MID_GET_VERSION, NULL, 0);

/* 带参数命令 */
uint8_t verify[]  = {0x00, 0x08};              // pd=0, timeout=8s
xst_send_command(dev, MID_VERIFY, verify, 2);

uint8_t del[]     = {0x02, 0xA6};              // user_id = 678
xst_send_command(dev, MID_DEL_USER, del, 2);

uint8_t get_info[] = {0x02, 0xA6};             // 查询 ID=678 的用户
xst_send_command(dev, MID_GET_USER_INFO, get_info, 2);

/* 注册用户 (name="water", admin=1, timeout=10s) */
uint8_t enroll[1 + 32 + 1 + 1] = {0};          // admin + name[32] + direction + timeout
enroll[0]  = 0x01;                              // admin=1
memcpy(&enroll[1], "water", 5);                 // name="water"
enroll[34] = 0x0A;                              // timeout=10s
xst_send_command(dev, MID_ENROLL_SINGLE, enroll, 35);
```

**使用示例 — 高层业务 API（推荐）**：

```c
xst_handle_t dev = xst_porting_get_device();
uint8_t status;

/* 系统管理 */
xst_reset_module(dev);                          // MID_RESET
xst_get_status(dev, &status);                   // MID_GETSTATUS → status=0/1/2/3
xst_get_version(dev, buf, sizeof(buf));          // MID_GET_VERSION

/* 鉴权与注册 */
uint16_t id_len = 64;
uint8_t  user_id[64];
xst_verify(dev, user_id, &id_len);              // MID_VERIFY (自动添加 timeout 参数)

xst_enroll(dev, 1, "water", 10);                // MID_ENROLL_SINGLE admin=1 timeout=10s
xst_enroll_single(dev, 1, 678, "water", 10);    // MID_ENROLL_SINGLE_ID16 指定 ID=678
xst_get_enroll_progress(dev, &progress);         // MID_ENROLL_PROGRESS (0-100)

/* 用户管理 */
xst_delete_user(dev, 678);                      // MID_DEL_USER (ID=678)
xst_delete_all(dev);                             // MID_DEL_ALL
xst_get_all_user_id(dev, buf, &len);            // MID_GET_ALL_USER_ID
xst_get_user_info(dev, 678, buf, &len);         // MID_GET_USER_INFO

/* 图像与配置 */
xst_snap_image(dev, 3, 1);                      // MID_SNAP_IMAGE 拍3张,起始编号1
xst_get_saved_image_size(dev, 1, &img_size);    // MID_GET_SAVED_IMAGE 查图片1大小
xst_set_threshold(dev, 50);                     // MID_SET_THRESHOLD_LEVEL

/* OTA 与电源 */
xst_start_ota(dev);
xst_power_down(dev);
```

> 高层 API 内部调用 `xst_exec_cmd()`，**同步阻塞等待模组回复**，返回 0 表示成功，>0 为模组错误码，-1 为通信超时。适用于裸机主循环或 RTOS 任务中调用。

---

### `xst_get_state()`

```c
int xst_get_state(xst_handle_t handle);
```

| 返回值 | 状态 |
|--------|------|
| 0 | IDLE |
| 1 | RX_WAIT |
| 2 | RX_PARSING |
| 3 | TX_BUSY |
| 4 | ERROR |
| -1 | 无效句柄 |

---

## 9. 移植层 API

### `xst_port_init()`

```c
void xst_port_init(void);
```

平台初始化入口。内部创建默认设备、绑定硬件 OPS、启动接收。不同平台实现不同。

### `xst_porting_get_device()`

```c
xst_handle_t xst_porting_get_device(void);
```

返回 porting 层创建的默认设备句柄。供应用层注册回调或发送命令。

### `xst_port_bind_hw()`

```c
int xst_port_bind_hw(xst_handle_t handle, void *huart_handle);
```

一键绑定：填充默认 `xst_config_t`，构造 `xst_ops_t`（使用平台实现的 hw_init/hw_send/get_tick_ms），调用 `xst_registe_cfg_and_ops()`。

### `xst_porting_fill_default_cfg()`

```c
void xst_porting_fill_default_cfg(xst_config_t *cfg);
```

填充默认配置：rx_buf=256, tx_buf=256, normal_max=256, bulk_max=4000, timeout=5000ms。

### `xst_ops_t` 结构体

```c
typedef struct {
    int      (*hw_init)(void *user_ctx);                             // 启动硬件（必须）
    int      (*hw_deinit)(void *user_ctx);                           // 停止硬件（可选）
    int      (*hw_send)(void *user_ctx, const uint8_t *data, size_t len); // 发送（必须）
    int      (*hw_receive)(void *user_ctx, uint8_t *data, size_t len);    // 轮询接收（可选，设为 NULL 则中断驱动）
    uint32_t (*get_tick_ms)(void *user_ctx);                        // 毫秒时间戳（推荐）
    void     *user_ctx;                                              // 平台资源句柄
} xst_ops_t;
```

两个数据接收模式：

| 模式 | hw_receive | 数据路径 |
|------|-----------|----------|
| 中断驱动 | NULL | ISR → `xst_write_rx_data()` → lwrb → 解析 |
| 轮询驱动 | 非 NULL | `xst_process()` 内调用 `hw_receive()` → lwrb → 解析 |

---

## 10. 配置宏说明

所有宏定义在 `xst_drv.h` 中，可按需修改后重新编译：

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `XST_MAX_DEVICES` | 2 | 最大设备数，每个设备约 6KB RAM |
| `XST_FRAME_MAX_LEN` | 256 | 普通帧最大长度 |
| `XST_PARSE_BUF_SIZE` | 4096 | 解析缓冲区大小（应 ≥ 批量帧最大长度） |
| `XST_TX_BUF_DEFAULT_SIZE` | 256 | 默认发送 lwrb 大小 |
| `XST_RX_BUF_DEFAULT_SIZE` | 256 | 默认接收 lwrb 大小 |
| `XST_TX_BUF_MAX_SIZE` | 256 | 最大发送 lwrb（硬限制） |
| `XST_RX_BUF_MAX_SIZE` | 1024 | 最大接收 lwrb（硬限制） |
| `XST_DEFAULT_TIMEOUT_MS` | 5000 | 帧解析超时（毫秒） |

### 内存估算

单个设备节点内存占用：

```
rx_storage[XST_RX_BUF_MAX_SIZE]  = 1024 B
tx_storage[XST_TX_BUF_MAX_SIZE]  = 256 B
xst_device_t                     = ~120 B (元数据)
parsed_data[XST_PARSE_BUF_SIZE]  = 4096 B
last_event (xst_event_t)         = ~16 B (指针)
─────────────────────────────────────────
合计约 5.5 KB / 设备
```

`XST_MAX_DEVICES=2` 时驱动内存池约 11KB，STM32F411 (128KB SRAM) 下占比约 8.6%。

---

## 11. 平台移植实例

### 9.1 STM32 HAL（默认实现）

```c
#include "xst_porting.h"
#include "main.h"
#include "usart.h"

static xst_handle_t xst_port_dev     = NULL;
static uint8_t      xst_port_rx_byte = 0;

static int xst_hw_send(void *ctx, const uint8_t *data, size_t len) {
    return (HAL_UART_Transmit((UART_HandleTypeDef *)ctx,
            (uint8_t *)data, (uint16_t)len, 100) == HAL_OK) ? 0 : -1;
}
static int xst_hw_init(void *ctx) {
    (void)ctx;
    HAL_UART_Receive_IT(&huart1, &xst_port_rx_byte, 1);
    return 0;
}
static uint32_t xst_get_tick(void *ctx) { (void)ctx; return HAL_GetTick(); }

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1 && xst_port_dev) {
        xst_write_rx_data(xst_port_dev, &xst_port_rx_byte, 1);
        HAL_UART_Receive_IT(&huart1, &xst_port_rx_byte, 1);
    }
}

void xst_port_init(void) {
    xst_port_dev = xst_create_device("palm_vein", 0);
    xst_port_bind_hw(xst_port_dev, &huart1);
    xst_init(xst_port_dev);
}
xst_handle_t xst_porting_get_device(void) { return xst_port_dev; }
```

关键点：
- UART 初始化由 CubeMX 在 `MX_USART1_UART_Init()` 完成，不要在 `hw_init` 重复配置
- `HAL_UART_RxCpltCallback` 是 HAL `__weak` 函数，在此覆盖
- 每次收到 1 字节必须重新调用 `HAL_UART_Receive_IT` 继续接收

---

### 9.2 STM32 标准外设库 (StdPeriph)

```c
#include "stm32f10x.h"   /* 按实际芯片替换 */
#include "xst_porting.h"

static xst_handle_t xst_port_dev     = NULL;
static uint8_t      xst_port_rx_byte = 0;

static int hw_init(void *ctx) {
    USART_TypeDef *usart = (USART_TypeDef *)ctx;
    USART_ITConfig(usart, USART_IT_RXNE, ENABLE);
    return 0;
}
static int hw_send(void *ctx, const uint8_t *data, size_t len) {
    USART_TypeDef *usart = (USART_TypeDef *)ctx;
    for (size_t i = 0; i < len; i++) {
        while (USART_GetFlagStatus(usart, USART_FLAG_TXE) == RESET);
        USART_SendData(usart, data[i]);
    }
    while (USART_GetFlagStatus(usart, USART_FLAG_TC) == RESET);
    return 0;
}
static uint32_t get_tick(void *ctx) { (void)ctx; return SysTick_GetMillis(); }

void USART1_IRQHandler(void) {
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        uint8_t byte = (uint8_t)USART_ReceiveData(USART1);
        if (xst_port_dev) xst_write_rx_data(xst_port_dev, &byte, 1);
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}

void xst_port_init(void) {
    xst_port_dev = xst_create_device("palm_vein", 0);
    xst_ops_t ops = { .hw_init=hw_init, .hw_send=hw_send,
                      .get_tick_ms=get_tick, .user_ctx=(void*)USART1 };
    xst_config_t cfg; xst_porting_fill_default_cfg(&cfg);
    xst_registe_cfg_and_ops(xst_port_dev, &cfg, &ops);
    xst_init(xst_port_dev);
}
```

关键点：
- `user_ctx` 直接传 USART1 基地址指针
- UART GPIO/波特率配置需在 `xst_port_init()` 之前完成
- 中断函数名必须与启动文件向量表一致
- 需自行实现 1ms SysTick 累加器 `SysTick_GetMillis()`

---

### 9.3 GD32 标准库

```c
#include "gd32f30x.h"
#include "xst_porting.h"

static xst_handle_t xst_port_dev = NULL;

static int hw_init(void *ctx) {
    usart_interrupt_enable((uint32_t)(uintptr_t)ctx, USART_INT_RBNE);
    return 0;
}
static int hw_send(void *ctx, const uint8_t *data, size_t len) {
    uint32_t usart = (uint32_t)(uintptr_t)ctx;
    for (size_t i = 0; i < len; i++) {
        while (usart_flag_get(usart, USART_FLAG_TBE) == RESET);
        usart_data_transmit(usart, data[i]);
    }
    while (usart_flag_get(usart, USART_FLAG_TC) == RESET);
    return 0;
}
static uint32_t get_tick(void *ctx) { (void)ctx; return SysTick_GetTick(); }

void USART0_IRQHandler(void) {
    if (usart_interrupt_flag_get(USART0, USART_INT_FLAG_RBNE) != RESET) {
        uint8_t byte = (uint8_t)usart_data_receive(USART0);
        if (xst_port_dev) xst_write_rx_data(xst_port_dev, &byte, 1);
        usart_interrupt_flag_clear(USART0, USART_INT_FLAG_RBNE);
    }
}

void xst_port_init(void) {
    xst_port_dev = xst_create_device("palm_vein", 0);
    xst_ops_t ops = { .hw_init=hw_init, .hw_send=hw_send,
                      .get_tick_ms=get_tick, .user_ctx=(void*)(uintptr_t)USART0 };
    xst_config_t cfg; xst_porting_fill_default_cfg(&cfg);
    xst_registe_cfg_and_ops(xst_port_dev, &cfg, &ops);
    xst_init(xst_port_dev);
}
```

关键点：
- GD32 外设用 `uint32_t` 基地址，通过 `(void*)(uintptr_t)` 转换
- 中断标志位：`RBNE`（GD32）vs `RXNE`（STM32）
- GD32 固件库自带 `SysTick_GetTick()`

---

### 9.4 ESP32 (ESP-IDF, FreeRTOS)

```c
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "xst_porting.h"

#define ESP_UART   UART_NUM_1
#define ESP_TX     17
#define ESP_RX     16
#define ESP_BUF    1024

static xst_handle_t  xst_port_dev = NULL;
static QueueHandle_t uart_rx_queue;

static int hw_init(void *ctx) {
    uart_config_t uc = { .baud_rate=115200, .data_bits=UART_DATA_8_BITS,
                         .parity=UART_PARITY_DISABLE, .stop_bits=UART_STOP_BITS_1,
                         .flow_ctrl=UART_HW_FLOWCTRL_DISABLE };
    uart_param_config(ESP_UART, &uc);
    uart_set_pin(ESP_UART, ESP_TX, ESP_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(ESP_UART, ESP_BUF, ESP_BUF, 10, &uart_rx_queue, 0);
    return 0;
}
static int hw_send(void *ctx, const uint8_t *data, size_t len) {
    (void)ctx; return (uart_write_bytes(ESP_UART, data, len) > 0) ? 0 : -1;
}
static uint32_t get_tick(void *ctx) {
    (void)ctx; return (uint32_t)(esp_timer_get_time() / 1000);
}

static void uart_rx_task(void *arg) {
    uart_event_t event; uint8_t buf[128];
    for (;;) {
        if (xQueueReceive(uart_rx_queue, &event, pdMS_TO_TICKS(100))) {
            if (event.type == UART_DATA) {
                int n = uart_read_bytes(ESP_UART, buf, event.size, pdMS_TO_TICKS(100));
                if (n > 0 && xst_port_dev) xst_write_rx_data(xst_port_dev, buf, (size_t)n);
            }
        }
    }
}

void xst_port_init(void) {
    xst_port_dev = xst_create_device("palm_vein", 0);
    xst_config_t cfg; xst_porting_fill_default_cfg(&cfg);
    cfg.rx_buf_size = 512; cfg.tx_buf_size = 512;
    xst_ops_t ops = { .hw_init=hw_init, .hw_send=hw_send, .get_tick_ms=get_tick };
    xst_registe_cfg_and_ops(xst_port_dev, &cfg, &ops);
    xst_init(xst_port_dev);
    xTaskCreate(uart_rx_task, "uart_rx", 2048, NULL, 5, NULL);
}
```

关键点：
- ESP32 UART 自带硬件 FIFO，不要逐字节接收
- 用 FreeRTOS 任务从事件队列批量取出数据，写入 lwrb
- `esp_timer_get_time()` 返回微秒，÷1000 得毫秒

---

### 9.5 通用轮询模式（无中断，裸机）

```c
#include "xst_porting.h"

/* 用户需实现的 5 个硬件函数 */
extern void     my_uart_init(void);
extern void     my_uart_send_byte(uint8_t b);
extern int      my_uart_rx_ready(void);       /* >0 有数据 */
extern uint8_t  my_uart_read_byte(void);
extern uint32_t my_tick_ms(void);

static xst_handle_t xst_port_dev = NULL;

static int hw_init(void *ctx)   { (void)ctx; my_uart_init(); return 0; }
static int hw_send(void *ctx, const uint8_t *d, size_t n) {
    (void)ctx; for (size_t i=0; i<n; i++) my_uart_send_byte(d[i]); return 0;
}
static int hw_receive(void *ctx, uint8_t *buf, size_t max) {
    (void)ctx; size_t c=0;
    while (c<max && my_uart_rx_ready()>0) buf[c++]=my_uart_read_byte();
    return (int)c;
}
static uint32_t get_tick(void *ctx) { (void)ctx; return my_tick_ms(); }

void xst_port_init(void) {
    xst_port_dev = xst_create_device("palm_vein", 0);
    xst_config_t cfg; xst_porting_fill_default_cfg(&cfg);
    xst_ops_t ops = { .hw_init=hw_init, .hw_send=hw_send, .hw_receive=hw_receive,
                      .get_tick_ms=get_tick, .user_ctx=NULL };
    xst_registe_cfg_and_ops(xst_port_dev, &cfg, &ops);
    xst_init(xst_port_dev);
}
```

关键点：
- `hw_receive` 非 NULL 即启用轮询模式，`xst_process()` 自动调用
- 主循环频率必须 ≥1kHz（115200bps 下每个字节间隔约 87μs）
- 无需中断/DMA/RTOS，移植最简单

---

### 9.6 条件编译骨架（多平台合一）

```c
/* porting/xst_porting.c */
#include "xst_porting.h"

static xst_handle_t xst_port_dev     = NULL;
static uint8_t      xst_port_rx_byte = 0;

/* ============ STM32 HAL ============ */
#if defined(HAL_UART_MODULE_ENABLED)
#include "main.h"
#include "usart.h"
// ... HAL 实现 ...

/* ============ STM32 标准库 ============ */
#elif defined(STM32F10X_HD) || defined(STM32F40_41xxx)
#include "stm32f10x.h"
// ... 标准库实现 ...

/* ============ GD32 ============ */
#elif defined(GD32F30X) || defined(GD32F4XX)
#include "gd32f30x.h"
// ... GD32 实现 ...

/* ============ ESP32 ============ */
#elif defined(ESP_PLATFORM)
#include "driver/uart.h"
#include "esp_timer.h"
// ... ESP32 实现 ...

/* ============ 通用轮询 ============ */
#else
// ... 轮询实现 ...
#endif

/* 平台无关工具函数 */
// xst_porting_fill_default_cfg, xst_porting_clear_ops, xst_port_bind_hw
```

---

## 12. 优缺点分析

### 优点

| 优点 | 说明 |
|------|------|
| **零平台依赖** | `xst_drv.c` 不含任何 HAL/RTOS/厂商 SDK 头文件，可独立编译为静态库 |
| **事件驱动** | 帧解析完成后自动回调，用户无需轮询协议状态。支持回调优先 + 轮询双模式 |
| **静态内存** | `device_pool[XST_MAX_DEVICES]` 编译期确定，无 `malloc`/`free`，适合嵌入式 |
| **两阶段注册** | 创建与配置分离，支持从 Flash/网络延迟加载配置 |
| **OPS 注入** | 硬件差异完全封装在 5 个函数指针中，新增平台只需实现 4~5 个函数 |
| **协议完整性** | 7 状态解析机 + XOR 校验 + 超时检测 + 帧长上限保护 |
| **批量数据支持** | 4096 字节解析缓冲，适配 IMAGE/LOG/DAT 的 4000 字节单帧 |
| **ISR 安全** | `xst_write_rx_data` 通过 lwrb 原子操作实现单生产者单消费者，ISR 可安全调用 |
| **多设备** | 链表管理，`xst_process_all()` 遍历处理所有设备，互不干扰 |
| **轻量运行时** | 单线程、无锁、无 RTOS 依赖 |

### 缺点

| 缺点 | 说明 |
|------|------|
| **单线程限制** | 不支持多线程并发调用，ISR 和主循环之间的 lwrb 依赖原子操作 |
| **静态内存池** | `XST_MAX_DEVICES` 编译期固定，运行时无法扩展。最大 2 个设备默认，多设备需改宏 |
| **无发送队列** | 当前驱动只实现了帧接收解析，`xst_hw_send` 是透传发送，无 TX 状态机和重试机制 |
| **数据指针有效期** | 事件中的 `data` 指针在下次 `xst_process()` 后失效，容易被误用 |
| **轮询解析** | 逐字节从 lwrb 读取，对 4000 字节帧的解析效率不如 DMA + 批量处理 |
| **无 DMA 支持** | 当前仅支持 UART 单字节中断或轮询，未适配 DMA 接收 |
| **依赖 lwrb** | 必须集成 lwrb (v3.2.0) 环形缓冲区库 |
| **无帧缓冲池** | 批量数据帧事件无内部缓冲池，应用层必须在回调中立即处理 |

### 适用场景

| 场景 | 适合度 |
|------|--------|
| 单 MCU 控制单个掌静脉模组 | ★★★★★ |
| 单 MCU 控制多个模组 (≤4) | ★★★★☆ |
| 带 RTOS 的多任务系统 | ★★★☆☆ (需自行加锁) |
| 高速批量图像传输 | ★★★☆☆ (建议加 DMA) |
| 需要发送重试/超时/队列 | ★★☆☆☆ (需自行实现) |

---

## 13. 集成步骤

### 最小集成 (STM32 HAL)

**1. 文件依赖**

```
bsp/
├── lwrb/lwrb.h, lwrb.c            ← 环形缓冲区
└── xst_driver/
    ├── xst_drv.h, xst_drv.c        ← 核心驱动
    ├── xst_pack_t.h                ← 协议定义
    └── porting/
        ├── xst_porting.h           ← 移植接口
        └── xst_porting.c           ← 平台实现
```

**2. main.c 模板**

```c
#include "xst_drv.h"
#include "xst_porting.h"
#define LOG_TAG "app"
#include "elog.h"

static void on_event(xst_handle_t h, const xst_event_t *e, void *ctx) {
    (void)h; (void)ctx;
    switch (e->type) {
    case XST_EVENT_REPLY:
        log_i("REPLY mid=0x%02X result=%d len=%d",
              e->reply.msgid, e->reply.result, e->reply.len);
        break;
    case XST_EVENT_NOTE:
        log_i("NOTE nid=%d len=%d", e->note.note_id, e->note.len);
        if (e->note.note_id == NID_READY) {
            /* 模组就绪，可发送命令 */
        }
        break;
    case XST_EVENT_DATA:
        log_i("DATA type=0x%02X len=%d", e->data.msgid, e->data.len);
        break;
    case XST_EVENT_ERROR:
        log_e("ERROR code=%d", e->error.code);
        break;
    default: break;
    }
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    elog_init(); elog_start();
    xst_port_init();
    xst_set_event_callback(xst_porting_get_device(), on_event, NULL);

    while (1) {
        xst_process_all();
    }
}
```

**3. CMakeLists.txt**

```cmake
target_link_libraries(${CMAKE_PROJECT_NAME} xst_driver lwrb elog)
```

### 编译选项

```
-std=gnu11 -Wall -Wextra -Wpedantic
-DSTM32F411xE -DUSE_HAL_DRIVER
```

---

## API 速查表

### 设备管理

| 函数 | 功能 | 返回值 |
|------|------|--------|
| `xst_create_device(name, id)` | 创建设备壳子 | handle / NULL |
| `xst_create_with_cfg_ops(name, id, cfg, ops)` | 创建+注册一步到位 | handle / NULL |
| `xst_registe_cfg_and_ops(h, cfg, ops)` | 注册配置和硬件接口 | 0 / -1 |
| `xst_get_handle(id)` | 按设备 ID 查找 | handle / NULL |
| `xst_unregister(h)` | 注销设备 | void |
| `xst_init(h)` | 初始化（启动硬件） | 0 / -1 |
| `xst_deinit(h)` | 关闭设备 | 0 / -1 |
| `xst_change_cfg(h, cfg)` | 更换配置 | 0 / -1 |
| `xst_change_ops(h, ops)` | 更换硬件接口 | 0 / -1 |

### 数据处理

| 函数 | 功能 | 返回值 |
|------|------|--------|
| `xst_process(h)` | 处理单个设备（解析帧） | 0 / -1 |
| `xst_process_all()` | 遍历处理所有设备 | 0 / -1 |
| `xst_write_rx_data(h, data, len)` | ISR 写入字节到 lwrb | 实际写入字节数 |
| `xst_read_rx_data(h, buf, len)` | 从 lwrb 读取字节 | 实际读取字节数 |
| `xst_send_command(h, msgid, data, len)` | 组装帧+计算校验+发送 | 0 / -1 |

### 事件系统

| 函数 | 功能 | 返回值 |
|------|------|--------|
| `xst_set_event_callback(h, cb, ctx)` | 注册事件回调 | 0 / -1 |
| `xst_get_event(h, event)` | 轮询获取事件 | 1/0/-1 |
| `xst_reset_parser(h)` | 重置解析状态机 | void |
| `xst_get_state(h)` | 获取设备状态 | 0~4 / -1 |

### 移植层

| 函数 | 功能 |
|------|------|
| `xst_port_init()` | 平台初始化 |
| `xst_porting_get_device()` | 获取默认设备句柄 |
| `xst_port_bind_hw(h, ctx)` | 一键绑定硬件 |
| `xst_porting_fill_default_cfg(cfg)` | 填充默认配置 |
| `xst_porting_clear_ops(ops)` | 清零 ops 表 |

---

## CHANGELOG

### v3.0 (2026-05)

- **业务 API 重构**：
  - `xst_get_status(handle, &status)` — 增加 status 输出参数
  - `xst_enroll_single(handle, admin, user_id, name, timeout)` — 改用 `MID_ENROLL_SINGLE_ID16(0x1E)`，内部组装 37 字节参数
  - `xst_delete_user(handle, user_id)` — 直接传 `uint16_t`，内部打包为协议格式
  - `xst_get_user_info(handle, user_id, info, &info_len)` — 同上
  - `xst_snap_image(handle, image_counts, start_number)` — 补齐缺失的 2 个参数
  - `xst_get_saved_image_size(handle, image_number, &size)` — 补齐缺失的 image_number
- **xst_verify 修复**：补充 `MID_VERIFY` 协议要求的 2 字节参数 `{pd_rightaway=0, timeout=8}`
- **xst_enroll 数据布局修复**：direction 和 timeout 写入正确偏移
- **事件保护**：`xst_dispatch_event` 在已有 REPLY 时不覆盖为 NOTE，解决批量帧处理中丢 REPLY 的问题
- **日志改进**：事件数据改为 hex 转储，支持多行，进度值单独解析为百分比

### v2.1 (2026-04)

- 初始发布：纯 C11 掌静脉驱动，STM32 HAL/StdPeriph/GD32/ESP32 移植示例

---

*版本：v3.0 | 日期：2026-05 | 作者：3233458843@qq.com*
