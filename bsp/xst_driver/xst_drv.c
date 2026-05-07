/**
 * @file      xst_pack_t.h
 * @brief     XST掌静脉协议帧处理
 * @author    3233458843@qq.com
 * @version   3.0
 * @date      2026-04-23
 *
 * @copyright Copyright (c) 2026 All rights reserved.
 *
 * @note      纯C、零HAL依赖、静态内存池。
 *            逐字节状态机：lwrb_read → 状态机解析 → 校验 → 事件回调。
 *            本驱动仅供学习和交流使用，禁止用于商业用途。
 *            如果没有随软件附带 LICENSE 文件，则按 AS-IS 提供。
 *            ！！！ 本驱动未经充分测试，可能存在缺陷和问题，请谨慎使用 ！！！
 *            如果您发现了任何问题或有改进建议，请联系作者。
 *            本驱动的作者不对任何因使用本驱动而导致的损失或损害负责。
 *            本掌静脉驱动需配合lwrb环形缓冲区使用，且仅支持单线程环境。
 *            本驱动的功能和性能可能有限，可能不适用于所有应用场景，请根据实际需求进行评估和测试。
 *            本驱动的接口和实现可能会发生变化，请关注后续更新和维护。
 *            本驱动的使用和修改请遵守相关法律法规和道德规范，不得用于任何非法或不当的目的。
 */

#include "xst_drv.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define xst_container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

/* 内部状态值 */
typedef enum {
    XST_STATE_IDLE  = 0,
    XST_STATE_ERROR = 4,
} xst_state_t;

/* 设备节点 */
typedef struct xst_device_node {
    xst_device_t device;
    /* 帧解析状态机 */
    xst_parse_state_t parse_state;                     // 解析状态
    uint8_t           parsed_msgid;                    // 当前帧的 msgID
    uint16_t          parsed_len;                      // 当前帧的 data 长度
    uint16_t          data_index;                      // 当前帧已接收的 data 字节数
    uint8_t           calc_checksum;                   /* 运行中的 XOR */
    uint8_t           parsed_data[XST_FRAME_BUF_SIZE]; // 解析缓冲区
    uint32_t          last_byte_ticks;                 // 上次接收字节的系统时间（ms）
    /* 事件系统 */
    xst_event_callback_t event_cb;     // 事件回调
    void                *event_cb_ctx; // 事件回调用户上下文
    xst_event_t          last_event;   // 最近一次事件（未被用户 get_event 取走）
    /* 存储 */
    uint8_t                 rx_storage[XST_RX_BUF_MAX_SIZE];
    uint8_t                 tx_storage[XST_TX_BUF_MAX_SIZE];
    struct xst_device_node *next;
    uint8_t                 in_use;
} xst_device_node_t;

static xst_device_node_t  device_pool[XST_MAX_DEVICES];
static xst_device_node_t *xst_device_list  = NULL;
static uint32_t           xst_device_count = 0;

static xst_device_node_t *xst_alloc_node(void);
static xst_device_t      *xst_get_device_internal(xst_handle_t handle);
static xst_device_node_t *xst_get_node(xst_handle_t handle);
static int                xst_validate_config(const xst_config_t *cfg);
static xst_device_node_t *xst_find_device_node(uint8_t device_id);
static void               xst_dispatch_event(xst_device_node_t *node, const xst_event_t *event);
static void               xst_dispatch_frame(xst_device_node_t *node);

/* ===================================================================
 * 设备管理
 * =================================================================== */
/**
 * @brief  创建一个新的设备实例
 * @param  name 设备名称
 * @param  device_id 设备ID
 * @return 设备句柄，失败返回 NULL
 */
xst_handle_t xst_create_device(char *name, uint8_t device_id)
{
    if (name == NULL || xst_device_count >= XST_MAX_DEVICES)
        return NULL;
    if (xst_find_device_node(device_id) != NULL)
        return NULL;

    xst_device_node_t *node = xst_alloc_node();
    if (node == NULL)
        return NULL;

    memset(node, 0, sizeof(xst_device_node_t));
    node->in_use                          = 1;
    node->device.base.name                = name;
    node->device.base.device_id           = device_id;
    node->device.base.device_class        = DEVICE_CLASS_BIOMETRIC;
    node->device.cfg.rx_buf_size          = XST_RX_BUF_DEFAULT_SIZE;
    node->device.cfg.tx_buf_size          = XST_TX_BUF_DEFAULT_SIZE;
    node->device.cfg.normal_frame_max_len = XST_FRAME_MAX_LEN;
    node->device.cfg.bulk_frame_max_len   = XST_FRAME_MAX_LEN * 4;
    node->device.cfg.default_timeout_ms   = 5000;
    node->device.state                    = XST_STATE_IDLE;
    node->device.rx_buf                   = node->rx_storage;
    node->device.tx_buf                   = node->tx_storage;
    node->next                            = xst_device_list;
    xst_device_list                       = node;
    xst_device_count++;

    return (xst_handle_t)&node->device.base;
}
/**
 * @brief  注销一个设备实例 (释放资源，移出列表)
 * @param  handle 设备句柄
 */
void xst_unregister(xst_handle_t handle)
{
    if (handle == NULL)
        return;
    xst_device_t *dev = xst_get_device_internal(handle);
    if (dev == NULL)
        return;

    xst_device_node_t *prev = NULL;
    // 从链表中移除节点
    for (xst_device_node_t *cur = xst_device_list; cur; prev = cur, cur = cur->next) {
        if (&cur->device == dev) {
            if (prev == NULL)
                xst_device_list = cur->next;
            else
                prev->next = cur->next;
            cur->in_use = 0;
            xst_device_count--;
            return;
        }
    }
}
/**
 * @brief  注册设备配置和操作接口
 * @param  handle 设备句柄
 * @param  cfg    新的配置指针
 * @param  ops    新的操作接口指针
 * @return 0=成功, -1=失败
 */
int xst_registe_cfg_and_ops(xst_handle_t handle, const xst_config_t *cfg, const xst_ops_t *ops)
{
    if (handle == NULL || cfg == NULL || ops == NULL || xst_validate_config(cfg) != 0)
        return -1;
    xst_device_t *dev = xst_get_device_internal(handle);
    if (dev == NULL)
        return -1;
    dev->cfg = *cfg;
    dev->ops = *ops;
    return 0;
}
/** @brief  创建并注册一个设备实例（简化接口）
 * @param  name 设备名称
 * @param  device_id 设备ID
 * @param  cfg    配置指针
 * @param  ops    操作接口指针
 * @return 设备句柄，失败返回 NULL
 */
xst_handle_t xst_create_with_cfg_ops(char *name, uint8_t device_id, const xst_config_t *cfg,
                                     const xst_ops_t *ops)
{
    xst_handle_t h = xst_create_device(name, device_id);
    if (h && xst_registe_cfg_and_ops(h, cfg, ops) != 0) {
        xst_unregister(h);
        return NULL;
    }
    return h;
}
/** @brief  获取设备实例句柄
 * @param  device_id 设备ID
 * @return 设备句柄，失败返回 NULL
 */
xst_handle_t xst_get_handle(uint8_t device_id)
{
    xst_device_node_t *node = xst_find_device_node(device_id);
    return node ? (xst_handle_t)&node->device.base : NULL;
}
/** @brief  初始化设备实例
 * @param  handle 设备句柄
 * @return 0=成功, -1=失败
 */
int xst_init(xst_handle_t handle)
{
    xst_device_node_t *node = xst_get_node(handle);
    if (node == NULL)
        return -1;
    xst_device_t *dev = &node->device;

    if (xst_validate_config(&dev->cfg) || dev->rx_buf == NULL || dev->tx_buf == NULL ||
        !lwrb_init(&dev->rx_rb, dev->rx_buf, dev->cfg.rx_buf_size) ||
        !lwrb_init(&dev->tx_rb, dev->tx_buf, dev->cfg.tx_buf_size) || dev->ops.hw_init == NULL ||
        dev->ops.hw_init(dev->ops.user_ctx) != 0) {
        dev->state = XST_STATE_ERROR;
        return -1;
    }
    node->parse_state = XST_PARSE_IDLE;
    dev->state        = XST_STATE_IDLE;
    return 0;
}
/** @brief  反初始化设备实例
 * @param  handle 设备句柄
 * @return 0=成功, -1=失败
 */
int xst_deinit(xst_handle_t handle)
{
    xst_device_node_t *node = xst_get_node(handle);
    if (node == NULL)
        return -1;
    xst_device_t *dev = &node->device;
    if (dev->ops.hw_deinit == NULL || dev->ops.hw_deinit(dev->ops.user_ctx) != 0) {
        dev->state = XST_STATE_ERROR;
        return -1;
    }
    lwrb_free(&dev->rx_rb);
    lwrb_free(&dev->tx_rb);
    node->event_cb     = NULL;
    node->event_cb_ctx = NULL;
    dev->state         = XST_STATE_IDLE;
    return 0;
}
/** @brief  修改设备配置
 * @param  handle 设备句柄
 * @param  new_cfg 新的配置指针
 * @return 0=成功, -1=失败
 * @note   修改配置会重置缓冲区，要求设备处于空闲状态。
 */
int xst_change_cfg(xst_handle_t handle, const xst_config_t *new_cfg)
{
    if (handle == NULL || new_cfg == NULL)
        return -1;
    xst_device_t *dev = xst_get_device_internal(handle);
    if (dev == NULL || xst_validate_config(new_cfg) != 0)
        return -1;
    dev->cfg = *new_cfg;
    if ((lwrb_is_ready(&dev->rx_rb) || lwrb_is_ready(&dev->tx_rb)) && dev->state != XST_STATE_IDLE)
        return -1;
    if ((lwrb_is_ready(&dev->rx_rb) || lwrb_is_ready(&dev->tx_rb)) &&
        (!lwrb_init(&dev->rx_rb, dev->rx_buf, dev->cfg.rx_buf_size) ||
         !lwrb_init(&dev->tx_rb, dev->tx_buf, dev->cfg.tx_buf_size))) {
        dev->state = XST_STATE_ERROR;
        return -1;
    }
    return 0;
}
/** @brief  修改设备操作接口
 * @param  handle 设备句柄
 * @param  new_ops 新的操作接口指针
 * @return 0=成功, -1=失败
 */
int xst_change_ops(xst_handle_t handle, const xst_ops_t *new_ops)
{
    if (handle == NULL || new_ops == NULL)
        return -1;
    xst_device_t *dev = xst_get_device_internal(handle);
    if (dev == NULL)
        return -1;
    dev->ops = *new_ops;
    return 0;
}

/* ===================================================================
 * xst_process — 逐字节状态机
 * ===================================================================
 * 帧格式: EF AA | msgID(1) | len(2大端) | data(N) | checksum(XOR)
 *
 * 每字节从 lwrb 读出，由状态机驱动解析。
 * data 存入 parsed_data，同时计算 XOR。
 * 校验通过后通过 event_on 回调分发，用户在此处理业务。
 */

int xst_process(xst_handle_t handle)
{
    xst_device_node_t *node = xst_get_node(handle);
    if (node == NULL)
        return -1;
    xst_device_t *dev = &node->device;

    /* 轮询模式兼容 */
    if (dev->ops.hw_receive != NULL) {
        uint8_t tmp[32];
        int     n = dev->ops.hw_receive(dev->ops.user_ctx, tmp, sizeof(tmp));
        if (n < 0) {
            dev->state = XST_STATE_ERROR;
            return -1;
        }
        if (n > 0 && lwrb_write(&dev->rx_rb, tmp, (lwrb_sz_t)n) != (lwrb_sz_t)n) {
            dev->state = XST_STATE_ERROR;
            return -1;
        }
    }

    /* 逐字节从 lwrb 读取并解析 */
    uint8_t byte;
    while (lwrb_read(&dev->rx_rb, &byte, 1) == 1) {
        if (dev->ops.get_tick_ms)
            node->last_byte_ticks = dev->ops.get_tick_ms(dev->ops.user_ctx);

        switch (node->parse_state) {
            case XST_PARSE_IDLE:
                if (byte == XST_FRAME_H) {
                    node->parse_state = XST_PARSE_HEADER_H;
                }
                break;

            case XST_PARSE_HEADER_H:
                if (byte == XST_FRAME_L) {
                    node->parse_state   = XST_PARSE_MSGID;
                    node->calc_checksum = 0; /* 开始累积 XOR */
                } else if (byte != XST_FRAME_H) {
                    node->parse_state = XST_PARSE_IDLE;
                }
                break;

            case XST_PARSE_MSGID:
                node->parsed_msgid  = byte;
                node->calc_checksum = byte; /* XOR: msgID */
                node->parse_state   = XST_PARSE_LEN_H;
                break;

            case XST_PARSE_LEN_H:
                node->parsed_len = (uint16_t)byte << 8;
                node->calc_checksum ^= byte; /* XOR: len_H */
                node->parse_state = XST_PARSE_LEN_L;
                break;

            case XST_PARSE_LEN_L:
                node->parsed_len |= byte;
                node->calc_checksum ^= byte; /* XOR: len_L */
                if (node->parsed_len > XST_FRAME_BUF_SIZE) {
                    /* 帧超过小缓冲，丢弃 */
                    node->parse_state = XST_PARSE_IDLE;
                    xst_dispatch_event(node,
                                       &(xst_event_t){.type = XST_EVENT_ERROR, .error.code = -2});
                } else if (node->parsed_len == 0) {
                    node->parse_state = XST_PARSE_CHECKSUM;
                } else {
                    node->data_index  = 0;
                    node->parse_state = XST_PARSE_DATA;
                }
                break;

            case XST_PARSE_DATA:
                node->parsed_data[node->data_index++] = byte;
                node->calc_checksum ^= byte; /* XOR: data[N] */
                if (node->data_index >= node->parsed_len) {
                    node->parse_state = XST_PARSE_CHECKSUM;
                }
                break;

            case XST_PARSE_CHECKSUM:
                node->parse_state = XST_PARSE_IDLE;
                if (node->calc_checksum == byte) {
                    xst_dispatch_frame(node);
                } else {
                    xst_dispatch_event(node,
                                       &(xst_event_t){.type = XST_EVENT_ERROR, .error.code = -3});
                }
                break;

            default:
                node->parse_state = XST_PARSE_IDLE;
                break;
        }
    }

    /* 超时检测：解析到一半没新字节 */
    if (node->parse_state != XST_PARSE_IDLE && dev->ops.get_tick_ms) {
        uint32_t now = dev->ops.get_tick_ms(dev->ops.user_ctx);
        if (now - node->last_byte_ticks > dev->cfg.default_timeout_ms) {
            node->parse_state = XST_PARSE_IDLE;
            xst_dispatch_event(node, &(xst_event_t){.type = XST_EVENT_ERROR, .error.code = -4});
        }
    }

    return 0;
}
/** @brief  处理所有设备实例的输入（适用于轮询模式）
 * @return 0=成功, -1=至少一个设备出错
 */
int xst_process_all(void)
{
    int result = 0;
    for (xst_device_node_t *n = xst_device_list; n; n = n->next)
        if (xst_process((xst_handle_t)&n->device.base) != 0)
            result = -1;
    return result;
}

/* ===================================================================
 * 公共 API
 * =================================================================== */
/** @brief  获取设备当前状态
 * @param  handle 设备句柄
 * @return 状态值，-1=无效句柄
 */
int xst_get_state(xst_handle_t handle)
{
    xst_device_t *dev = xst_get_device_internal(handle);
    return dev ? dev->state : -1;
}
/** @brief  获取当前事件（非阻塞）
 * @param  handle 设备句柄
 * @param  event 输出参数，成功返回事件内容
 * @return 1=有事件, 0=无事件, -1=无效句柄
 */
int xst_get_event(xst_handle_t handle, xst_event_t *event)
{
    xst_device_node_t *node = xst_get_node(handle);
    if (node == NULL || event == NULL)
        return -1;
    if (node->last_event.type == XST_EVENT_NONE)
        return 0;
    *event = node->last_event;
    memset(&node->last_event, 0, sizeof(node->last_event));
    return 1;
}
/** @brief  发送数据（异步接口）
 * @param  handle 设备句柄
 * @param  msgid  消息ID
 * @param  data   数据指针
 * @param  len    数据长度
 * @return 0=成功, -1=失败
 */
size_t xst_write_rx_data(xst_handle_t handle, const uint8_t *data, size_t len)
{
    xst_device_t *dev = xst_get_device_internal(handle);
    if (dev == NULL || data == NULL || len == 0)
        return 0;
    return (size_t)lwrb_write(&dev->rx_rb, data, (lwrb_sz_t)len);
}
/** @brief  读取数据（异步接口）
 * @param  handle 设备句柄
 * @param  data   输出数据缓冲
 * @param  len    输出数据缓冲长度
 * @return 实际读取字节数，0=无数据, -1=失败
 */
size_t xst_read_rx_data(xst_handle_t handle, uint8_t *data, size_t len)
{
    xst_device_t *dev = xst_get_device_internal(handle);
    if (dev == NULL || data == NULL || len == 0)
        return 0;
    return (size_t)lwrb_read(&dev->rx_rb, data, (lwrb_sz_t)len);
}
/**
 * @brief  发送命令（同步接口，封装帧格式）
 * @param  handle 设备句柄
 * @param  msgid  消息ID
 * @param  data   数据指针
 * @param  len    数据长度
 * @return 0=成功, -1=失败
 * @note   该函数会封装帧头、长度、校验等，直接调用底层 hw_send 发送完整帧。
 */
int xst_send_command(xst_handle_t handle, uint8_t msgid, const uint8_t *data, uint16_t len)
{
    xst_device_t *dev = xst_get_device_internal(handle);
    if (dev == NULL || dev->ops.hw_send == NULL || len > XST_TX_BUF_MAX_SIZE)
        return -1;

    uint8_t buf[XST_TX_BUF_MAX_SIZE + 6], *p = buf;
    *p++ = XST_FRAME_H;
    *p++ = XST_FRAME_L;
    *p++ = msgid;
    *p++ = (uint8_t)(len >> 8);
    *p++ = (uint8_t)(len & 0xFF);
    if (len > 0 && data) {
        memcpy(p, data, len);
        p += len;
    }

    uint8_t cksum = msgid;
    cksum ^= (uint8_t)(len >> 8);
    cksum ^= (uint8_t)(len & 0xFF);
    for (uint16_t i = 0; i < len; i++)
        cksum ^= data[i];
    *p++ = cksum;

    return dev->ops.hw_send(dev->ops.user_ctx, buf, (size_t)(p - buf));
}
/** @brief  设置事件回调
 * @param  handle 设备句柄
 * @param  cb     事件回调函数指针
 * @param  user_ctx 用户上下文指针，回调时传回
 * @return 0=成功, -1=失败
 */
int xst_set_event_callback(xst_handle_t handle, xst_event_callback_t cb, void *user_ctx)
{
    xst_device_node_t *node = xst_get_node(handle);
    if (node == NULL)
        return -1;
    node->event_cb     = cb;
    node->event_cb_ctx = user_ctx;
    return 0;
}
/** @brief  重置解析状态机（通常在错误后调用，丢弃当前帧数据）
 * @param  handle 设备句柄
 */
void xst_reset_parser(xst_handle_t handle)
{
    xst_device_node_t *node = xst_get_node(handle);
    if (node == NULL)
        return;
    node->parse_state = XST_PARSE_IDLE;
    node->parsed_len  = 0;
    node->data_index  = 0;
}

/* ===================================================================
 * 同步命令封装
 * =================================================================== */
/**
 * @brief  执行命令（同步接口）
 * @param  handle 设备句柄
 * @param  msgid  消息ID
 * @param  tx_data 发送数据指针
 * @param  tx_len 发送数据长度
 * @param  reply 输出参数，成功返回回复内容
 * @param  timeout_ms 超时时间（毫秒）
 * @return 命令结果，-1=失败或超时
 * @note   该函数会发送命令并等待回复，期间调用 xst_process 处理输入，直到收到对应 msgID
 * 的回复或超时。
 */
int xst_exec_cmd(xst_handle_t handle, uint8_t msgid, const uint8_t *tx_data, uint16_t tx_len,
                 xst_reply_t *reply, uint32_t timeout_ms)
{
    xst_device_t *dev = xst_get_device_internal(handle);
    xst_event_t   event;
    if (dev == NULL || dev->ops.get_tick_ms == NULL)
        return -1;

    uint32_t (*get_tick)(void *) = dev->ops.get_tick_ms;
    void *ctx                    = dev->ops.user_ctx;

    if (xst_send_command(handle, msgid, tx_data, tx_len) != 0)
        return -1;

    uint32_t start = get_tick(ctx);
    while (get_tick(ctx) - start < timeout_ms) {
        xst_process(handle);
        int r = xst_get_event(handle, &event);
        if (r > 0 && event.type == XST_EVENT_REPLY) {
            if (reply) {
                reply->msgid    = event.reply.mid;
                reply->result   = event.reply.result;
                reply->pay_len  = event.reply.len;
                reply->pay_data = event.reply.data;
            }
            return (int)event.reply.result;
        }
    }
    return -1;
}
/* ===================================================================
 * 业务命令封装
 * =================================================================== */
/** @brief  获取设备状态
 * @param  handle 设备句柄
 * @return 状态值，-1=失败
 */
int xst_get_status(xst_handle_t handle, uint8_t *status)
{
    xst_reply_t reply;
    if (status == NULL)
        return -1;
    int ret = xst_exec_cmd(handle, MID_GETSTATUS, NULL, 0, &reply, 3000);
    if (ret != 0)
        return ret;
    if (reply.pay_len >= 1)
        *status = reply.pay_data[0];
    return 0;
}
/** @brief  重置设备
 * @param  handle 设备句柄
 * @return 0=成功, -1=失败
 */
int xst_reset_module(xst_handle_t handle)
{
    return xst_exec_cmd(handle, MID_RESET, NULL, 0, NULL, 5000);
}
/** @brief  注册用户（如果设备支持快速注册）
 * @param  handle 设备句柄
 * @return 0=成功, -1=失败
 */
int xst_enroll(xst_handle_t handle, uint8_t admin, const uint8_t *name, uint8_t time_out)
{
    uint8_t data[35];
    if (name == NULL || time_out == 0 || time_out > 60)
        return -1;
    memset(data, 0, sizeof(data));
    data[0]          = admin;
    uint8_t name_len = (uint8_t)strlen((const char *)name);
    if (name_len > 32)
        return -1;
    memcpy(&data[1], name, name_len);
    /* data[1..32] user_name, 尾部已由 memset 补零 */
    data[33] = 0; /* direction, 废弃参数填 0 */
    data[34] = time_out;
    return xst_exec_cmd(handle, MID_ENROLL_SINGLE, data, sizeof(data), NULL,
                        (uint32_t)time_out * 1000 + 5000);
}
/** @brief  删除所有用户数据
 * @param  handle 设备句柄
 * @return 0=成功, -1=失败
 */
int xst_delete_all(xst_handle_t handle)
{
    return xst_exec_cmd(handle, MID_DEL_ALL, NULL, 0, NULL, 5000);
}
/** @brief  拍照并保存图像（如果设备支持）
 * @param  handle 设备句柄
 * @return 0=成功, -1=失败
 */
int xst_snap_image(xst_handle_t handle, uint8_t image_counts, uint8_t start_number)
{
    uint8_t data[2] = {image_counts, start_number};
    return xst_exec_cmd(handle, MID_SNAP_IMAGE, data, sizeof(data), NULL, 15000);
}
/** @brief  开始OTA升级流程（如果设备支持）
 * @param  handle 设备句柄
 * @return 0=成功, -1=失败
 */
int xst_start_ota(xst_handle_t handle)
{
    return xst_exec_cmd(handle, MID_START_OTA, NULL, 0, NULL, 5000);
}
/** @brief  关机（如果设备支持）
 * @param  handle 设备句柄
 * @return 0=成功, -1=失败
 */
int xst_power_down(xst_handle_t handle)
{
    return xst_exec_cmd(handle, MID_POWER_DOWN, NULL, 0, NULL, 5000);
}
/** @brief  获取设备版本信息
 * @param  handle 设备句柄
 * @param  ver 输出缓冲，成功返回版本字符串（UTF-8）
 * @param  size 输出缓冲大小（字节）
 * @return 0=成功, -1=失败
 * @note   版本字符串长度不超过 size-1，包含终止符。
 */
int xst_get_version(xst_handle_t handle, char *ver, uint16_t size)
{
    xst_reply_t reply;
    if (ver == NULL || size == 0)
        return -1;
    int ret = xst_exec_cmd(handle, MID_GET_VERSION, NULL, 0, &reply, 5000);
    if (ret != 0)
        return ret;
    uint16_t n = (reply.pay_len < size - 1) ? reply.pay_len : (size - 1);
    memcpy(ver, reply.pay_data, n);
    ver[n] = '\0';
    return 0;
}
/** @brief  验证用户（识别），获取用户ID
 * @param  handle 设备句柄
 * @param  user_id 输出缓冲，成功返回用户ID字符串（UTF-8）
 * @param  user_id_len 输入输出参数，输入缓冲大小，成功返回实际长度
 * @return 0=成功, -1=失败
 * @note   用户ID长度不超过 user_id_len，包含终止符。
 */
int xst_verify(xst_handle_t handle, uint8_t *user_id, uint16_t *user_id_len)
{
    xst_reply_t reply;
    if (user_id == NULL || user_id_len == NULL)
        return -1;
    uint8_t params[2] = {0, 8}; /* pd_rightaway=0, timeout=8s */
    int     ret       = xst_exec_cmd(handle, MID_VERIFY, params, sizeof(params), &reply, 15000);
    if (ret != 0)
        return ret;
    uint16_t n = reply.pay_len < *user_id_len ? reply.pay_len : *user_id_len;
    memcpy(user_id, reply.pay_data, n);
    *user_id_len = n;
    return 0;
}
/** @brief  获取注册进度（如果正在注册中）
 * @param  handle 设备句柄
 * @param  progress 输出参数，成功返回注册进度（0-100%）
 * @return 0=成功, -1=失败
 */
int xst_get_enroll_progress(xst_handle_t handle, uint8_t *progress)
{
    xst_reply_t reply;
    if (progress == NULL)
        return -1;
    int ret = xst_exec_cmd(handle, MID_ENROLL_PROGRESS, NULL, 0, &reply, 5000);
    if (ret != 0)
        return ret;
    if (reply.pay_len >= 1)
        *progress = reply.pay_data[0];
    return 0;
}
/** @brief  注册单个用户（如果设备支持快速注册）
 * @param  handle 设备句柄
 * @param  user_id 用户ID字符串（UTF-8），长度不超过32字节
 * @param  user_id_len 用户ID长度（字节）
 * @return 0=成功, -1=失败
 */
int xst_enroll_single(xst_handle_t handle, uint8_t admin, uint16_t user_id, const uint8_t *name,
                      uint8_t time_out)
{
    uint8_t data[37];
    if (name == NULL || time_out == 0 || time_out > 60)
        return -1;
    memset(data, 0, sizeof(data));
    data[0]          = admin;
    data[1]          = (uint8_t)(user_id >> 8);   /* user_id 高8位 */
    data[2]          = (uint8_t)(user_id & 0xFF); /* user_id 低8位 */
    uint8_t name_len = (uint8_t)strlen((const char *)name);
    if (name_len > 32)
        return -1;
    memcpy(&data[3], name, name_len);
    data[35] = 0; /* direction, 废弃 */
    data[36] = time_out;
    return xst_exec_cmd(handle, MID_ENROLL_SINGLE_ID16, data, sizeof(data), NULL,
                        (uint32_t)time_out * 1000 + 5000);
}
/** @brief  删除单个用户
 * @param  handle 设备句柄
 * @param  user_id 用户ID字符串（UTF-8），长度不超过32字节
 * @param  user_id_len 用户ID长度（字节）
 * @return 0=成功, -1=失败
 */
int xst_delete_user(xst_handle_t handle, uint16_t user_id)
{
    uint8_t data[2];
    data[0] = (uint8_t)(user_id >> 8);   /* user_id 高8位 */
    data[1] = (uint8_t)(user_id & 0xFF); /* user_id 低8位 */
    return xst_exec_cmd(handle, MID_DEL_USER, data, sizeof(data), NULL, 5000);
}
/** @brief  获取所有注册用户ID列表
 * @param  handle 设备句柄
 * @param  buf 输出缓冲，成功返回用户ID列表（UTF-8），格式为 "id1,id2,id3"
 * @param  buf_len 输入输出参数，输入缓冲大小，成功返回实际长度
 * @return 0=成功, -1=失败
 * @note   用户ID列表长度不超过 buf_len，包含终止符。
 */
int xst_get_all_user_id(xst_handle_t handle, uint8_t *buf, uint16_t *buf_len)
{
    xst_reply_t reply;
    if (buf == NULL || buf_len == NULL)
        return -1;
    int ret = xst_exec_cmd(handle, MID_GET_ALL_USER_ID, NULL, 0, &reply, 5000);
    if (ret != 0)
        return ret;
    uint16_t n = reply.pay_len < *buf_len ? reply.pay_len : *buf_len;
    memcpy(buf, reply.pay_data, n);
    *buf_len = n;
    return 0;
}
/** @brief  获取用户信息（如注册时间、特征版本等）
 * @param  handle 设备句柄
 * @param  user_id 用户ID字符串（UTF-8），长度不超过32字节
 * @param  user_id_len 用户ID长度（字节）
 * @param  info 输出缓冲，成功返回用户信息（UTF-8），格式由设备定义
 * @param  info_len 输入输出参数，输入缓冲大小，成功返回实际长度
 * @return 0=成功, -1=失败
 * @note   用户信息长度不超过 info_len，包含终止符。
 */
int xst_get_user_info(xst_handle_t handle, uint16_t user_id, uint8_t *info, uint16_t *info_len)
{
    xst_reply_t reply;
    uint8_t     data[2];
    if (info == NULL || info_len == NULL)
        return -1;
    data[0] = (uint8_t)(user_id >> 8);   /* user_id 高8位 */
    data[1] = (uint8_t)(user_id & 0xFF); /* user_id 低8位 */
    int ret = xst_exec_cmd(handle, MID_GET_USER_INFO, data, sizeof(data), &reply, 5000);
    if (ret != 0)
        return ret;
    uint16_t n = reply.pay_len < *info_len ? reply.pay_len : *info_len;
    memcpy(info, reply.pay_data, n);
    *info_len = n;
    return 0;
}
/** @brief  获取保存的图像大小（如果设备支持拍照功能）
 * @param  handle 设备句柄
 * @param  size 输出参数，成功返回图像大小（字节）
 * @return 0=成功, -1=失败
 * @note   图像数据可通过事件回调获取，或设备另行提供下载接口。
 */
int xst_get_saved_image_size(xst_handle_t handle, uint8_t image_number, uint32_t *size)
{
    xst_reply_t reply;
    if (size == NULL)
        return -1;
    int ret = xst_exec_cmd(handle, MID_GET_SAVED_IMAGE, &image_number, 1, &reply, 5000);
    if (ret != 0)
        return ret;
    if (reply.pay_len >= 4)
        *size = ((uint32_t)reply.pay_data[0] << 24) | ((uint32_t)reply.pay_data[1] << 16) |
                ((uint32_t)reply.pay_data[2] << 8) | reply.pay_data[3];
    return 0;
}
/** @brief  设置安全阈值（如人脸识别的相似度要求）
 * @param  handle 设备句柄
 * @param  level 阈值级别，范围由设备定义（如0-100）
 * @return 0=成功, -1=失败
 */
int xst_set_threshold(xst_handle_t handle, uint8_t level)
{
    return xst_exec_cmd(handle, MID_SET_THRESHOLD_LEVEL, &level, 1, NULL, 5000);
}
/* ===================================================================
 * 辅助函数
 * =================================================================== */
/** @brief  将命令结果代码转换为字符串（用于调试或日志）
 * @param  result 结果代码
 * @return 对应的字符串描述，未知代码返回 "Unknown code"
 */
const char *xst_result_str(uint8_t result)
{
    switch (result) {
        case MR_SUCCESS:
            return "Success";
        case MR_REJECTED:
            return "Rejected";
        case MR_ABORTED:
            return "Aborted";
        case MR_FAILED4_CAMERA:
            return "Camera failed";
        case MR_FAILED4_UNKNOWN_REASON:
            return "Unknown error";
        case MR_FAILED4_INVALID_PARAM:
            return "Invalid param";
        case MR_FAILED4_NO_MEMORY:
            return "No memory";
        case MR_FAILED4_UNKNOWN_USER:
            return "Unknown user";
        case MR_FAILED4_MAX_USER:
            return "Max users";
        case MR_FAILED4_ENROLLED:
            return "Already enrolled";
        case MR_FAILED4_LIVENESS_CHECK:
            return "Liveness check failed";
        case MR_FAILED4_TIME_OUT:
            return "Timeout";
        case MR_FAILED4_LICENSE_FAIL:
            return "License/auth failed";
        case MR_FAILED4_READ_FILE:
            return "Read file failed";
        case MR_FAILED4_WRITE_FILE:
            return "Write file failed";
        case MR_FAILED4_NO_ENCRYPT:
            return "No encryption";
        default:
            return "Unknown code";
    }
}
/** @brief  将事件类型转换为字符串（用于调试或日志）
 * @param  event_type 事件类型
 * @return 对应的字符串描述，未知类型返回 "Unknown event"
 */
const char *xst_note_str(uint8_t note_id)
{
    switch (note_id) {
        case NID_READY:
            return "Module ready";
        case NID_FACE_STATE:
            return "Face state";
        case NID_UNKNOWNERROR:
            return "Unknown error";
        case NID_OTA_DONE:
            return "OTA done";
        case NID_PALM_STATE:
            return "Palm state";
        case NID_LICENSE_FAIL:
            return "License failed";
        default:
            return "Unknown note";
    }
}

/* ===================================================================
 * Private 函数
 * =================================================================== */
/**
 * @brief  分配一个设备节点
 * @return 分配的设备节点指针，失败返回 NULL
 */
static xst_device_node_t *xst_alloc_node(void)
{
    for (int i = 0; i < XST_MAX_DEVICES; i++)
        if (!device_pool[i].in_use) {
            device_pool[i].in_use = 1;
            return &device_pool[i];
        }
    return NULL;
}
/**
 * @brief  根据句柄获取设备指针
 * @param  handle 设备句柄
 * @return 设备指针，失败返回 NULL
 */
static xst_device_t *xst_get_device_internal(xst_handle_t handle)
{
    if (handle == NULL)
        return NULL;
    xst_device_t *dev = xst_container_of(handle, xst_device_t, base);
    for (xst_device_node_t *n = xst_device_list; n; n = n->next)
        if (&n->device == dev)
            return dev;
    return NULL;
}
/**
 * @brief  根据句柄获取设备节点指针
 * @param  handle 设备句柄
 * @return 设备节点指针，失败返回 NULL
 */
static xst_device_node_t *xst_get_node(xst_handle_t handle)
{
    xst_device_t *dev = xst_get_device_internal(handle);
    return dev ? xst_container_of(dev, xst_device_node_t, device) : NULL;
}
/**
 * @brief  验证配置参数合法性
 * @param  cfg 配置参数指针
 * @return 0 合法，-1 不合法
 */
static int xst_validate_config(const xst_config_t *cfg)
{
    return (!cfg || cfg->rx_buf_size == 0 || cfg->tx_buf_size == 0 ||
            cfg->rx_buf_size > XST_RX_BUF_MAX_SIZE || cfg->tx_buf_size > XST_TX_BUF_MAX_SIZE)
               ? -1
               : 0;
}
/** @brief  根据设备ID查找设备节点
 * @param  device_id 设备ID
 * @return 设备节点指针，未找到返回 NULL
 */
static xst_device_node_t *xst_find_device_node(uint8_t device_id)
{
    for (xst_device_node_t *n = xst_device_list; n; n = n->next)
        if (n->device.base.device_id == device_id)
            return n;
    return NULL;
}
/**
 * @brief  分发事件给用户回调
 * @param  node 设备节点指针
 * @param  event 事件数据指针
 */
static void xst_dispatch_event(xst_device_node_t *node, const xst_event_t *event)
{
    if (!node || !event)
        return;
    /* 回调始终调用（日志输出等） */
    if (node->event_cb) {
        xst_handle_t h = (xst_handle_t)&node->device.base;
        node->event_cb(h, event, node->event_cb_ctx);
    }
    /* xst_exec_cmd 轮询等待 REPLY，不能让它被后续 NOTE 覆盖 */
    if (node->last_event.type == XST_EVENT_REPLY && event->type != XST_EVENT_REPLY)
        return;
    node->last_event = *event;
}
/**
 * @brief  分发帧数据给用户回调
 * @param  node 设备节点指针
 */
static void xst_dispatch_frame(xst_device_node_t *node)
{
    xst_event_t event;
    memset(&event, 0, sizeof(event));

    switch (node->parsed_msgid) {
        case MID_REPLY:
            if (node->parsed_len >= 2) {
                event.type         = XST_EVENT_REPLY;
                event.reply.mid    = node->parsed_data[0];
                event.reply.result = node->parsed_data[1];
                event.reply.len    = node->parsed_len - 2;
                event.reply.data   = &node->parsed_data[2];
            }
            break;
        case MID_NOTE:
            if (node->parsed_len >= 1) {
                event.type         = XST_EVENT_NOTE;
                event.note.note_id = node->parsed_data[0];
                event.note.len     = node->parsed_len - 1;
                event.note.data    = &node->parsed_data[1];
            }
            break;
        case MID_IMAGE:
        case MID_LOG:
        case MID_DAT:
            event.type       = XST_EVENT_DATA;
            event.data.msgid = node->parsed_msgid;
            event.data.len   = node->parsed_len;
            event.data.data  = node->parsed_data;
            break;
        default:
            event.type       = XST_EVENT_ERROR;
            event.error.code = -5;
            break;
    }

    xst_dispatch_event(node, &event);
}
