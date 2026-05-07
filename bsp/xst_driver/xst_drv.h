/**
 * @file      xst_pack_t.h
 * @brief     XST掌静脉函数声明
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

#ifndef __XST_DRV_H__
#define __XST_DRV_H__

/* Includes ------------------------------------------------------------------*/
#include "lwrb.h"
#include "xst_pack_t.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/*  Driver Configuration Macros -----------------------------------------------*/
#define XST_MAX_DEVICES         1    /*< 最大设备数（F103C8 仅 20KB RAM，设为 1） */
#define XST_FRAME_MAX_LEN       168  /*< 普通帧最大包长 */
#define XST_FRAME_BUF_SIZE      128  /*< 帧组装缓冲区（覆盖 reply/note 等普通帧） */
#define XST_TX_BUF_DEFAULT_SIZE 128  /*< 默认发送缓冲 */
#define XST_RX_BUF_DEFAULT_SIZE 128  /*< 默认接收缓冲 */
#define XST_TX_BUF_MAX_SIZE     256  /*< 最大发送缓冲 */
#define XST_RX_BUF_MAX_SIZE     256  /*< 最大接收缓冲 */
#define XST_DEFAULT_TIMEOUT_MS  5000 /*< 默认超时 */

/*  C++ Support ----------------------------------------------------------------*/
#ifdef __cplusplus
extern "C" {
#endif

/* Exported types  -----------------------------------------------------------*/

typedef enum {
    DEVICE_CLASS_BIOMETRIC = 1, /* 生物识别设备 */
} device_class_t;

/* 配置应用结构体（应用提供） */
typedef struct {
    device_class_t device_class;
    char          *name;      /* 设备名称，用于日志/查找 */
    uint8_t        device_id; /* 设备 ID（支持多路设备） */
} xst_base_device_t;

typedef xst_base_device_t *xst_handle_t;

typedef void (*xst_event_callback_t)(xst_handle_t handle, const xst_event_t *event, void *user_ctx);
/* 设备实例结构体（驱动内部使用） */
typedef struct {
    uint16_t rx_buf_size;          // 接收缓冲区大小，单位字节
    uint16_t tx_buf_size;          // 发送缓冲区大小，单位字节
    uint16_t normal_frame_max_len; // 普通帧最大长度，单位字节
    uint16_t bulk_frame_max_len;   // 大数据帧最大长度，单位字节
    uint32_t default_timeout_ms;   // 默认超时时间，单位毫秒
} xst_config_t;

/* 硬件操作接口（由板层实现） */
typedef struct {
    int (*hw_init)(void *user_ctx);
    int (*hw_deinit)(void *user_ctx);
    int (*hw_send)(void *user_ctx, const uint8_t *data, size_t len);
    int (*hw_receive)(void *user_ctx, uint8_t *data,
                      size_t len); /* 返回读取字节数，0 表示暂无数据，<0 表示错误 */
    uint32_t (*get_tick_ms)(void *user_ctx);
    void *user_ctx;
} xst_ops_t;

typedef struct {
    xst_base_device_t base;   /* 设备配置 */
    xst_config_t      cfg;    /* 设备运行时配置 */
    xst_ops_t         ops;    /* 硬件操作接口 */
    int               state;  /* 内部状态机状态 */
    lwrb_t            rx_rb;  /* 接收环形缓冲区 */
    lwrb_t            tx_rb;  /* 发送环形缓冲区 */
    uint8_t          *rx_buf; /* 接收缓冲区指针 */
    uint8_t          *tx_buf; /* 发送缓冲区指针 */
} xst_device_t;

/* Exported functions --------------------------------------------------------*/
xst_handle_t xst_create_device(char *name, uint8_t device_id);
xst_handle_t xst_create_with_cfg_ops(char *name, uint8_t device_id, const xst_config_t *cfg,
                                     const xst_ops_t *ops);
int xst_registe_cfg_and_ops(xst_handle_t handle, const xst_config_t *cfg, const xst_ops_t *ops);
/* 设备管理接口 */
xst_handle_t xst_get_handle(uint8_t device_id);
void         xst_unregister(xst_handle_t xst_handele);
int          xst_init(xst_handle_t xst_handele);
int          xst_deinit(xst_handle_t xst_handele);
int          xst_change_cfg(xst_handle_t handle, const xst_config_t *new_cfg);
int          xst_change_ops(xst_handle_t handle, const xst_ops_t *new_ops);
int          xst_process(xst_handle_t xst_handele);
int          xst_process_all(void);
int          xst_get_state(xst_handle_t xst_handele);
int          xst_get_event(xst_handle_t xst_handele, xst_event_t *event);
int          xst_set_event_callback(xst_handle_t handle, xst_event_callback_t cb, void *user_ctx);
void         xst_reset_parser(xst_handle_t handle);
size_t       xst_write_rx_data(xst_handle_t handle, const uint8_t *data, size_t len);
size_t       xst_read_rx_data(xst_handle_t handle, uint8_t *data, size_t len);
int xst_send_command(xst_handle_t handle, uint8_t msgid, const uint8_t *data, uint16_t len);
int xst_exec_cmd(xst_handle_t handle, uint8_t msgid, const uint8_t *tx_data, uint16_t tx_len,
                 xst_reply_t *reply, uint32_t timeout_ms);

/* 返回值: 0=成功, >0=模组错误码, -1=超时/通信错误 */

/* 系统管理 */
int xst_get_status(xst_handle_t handle, uint8_t *status);
int xst_get_version(xst_handle_t handle, char *ver, uint16_t size);
int xst_reset_module(xst_handle_t handle);

/* 鉴权与注册 */
int xst_verify(xst_handle_t handle, uint8_t *user_id, uint16_t *user_id_len);
int xst_enroll(xst_handle_t handle, uint8_t admin, const uint8_t *name, uint8_t time_out);
int xst_get_enroll_progress(xst_handle_t handle, uint8_t *progress);
int xst_enroll_single(xst_handle_t handle, uint8_t admin, uint16_t user_id,
                      const uint8_t *name, uint8_t time_out);

/* 用户管理 */
int xst_delete_user(xst_handle_t handle, uint16_t user_id);
int xst_delete_all(xst_handle_t handle);
int xst_get_all_user_id(xst_handle_t handle, uint8_t *buf, uint16_t *buf_len);
int xst_get_user_info(xst_handle_t handle, uint16_t user_id,
                      uint8_t *info, uint16_t *info_len);

/* 图像与配置 */
int xst_snap_image(xst_handle_t handle, uint8_t image_counts, uint8_t start_number);
int xst_get_saved_image_size(xst_handle_t handle, uint8_t image_number, uint32_t *size);
int xst_set_threshold(xst_handle_t handle, uint8_t level);

/* OTA 与电源 */
int xst_start_ota(xst_handle_t handle);
int xst_power_down(xst_handle_t handle);

/* 辅助 */
const char *xst_result_str(uint8_t result);
const char *xst_note_str(uint8_t note_id);

/* C++ Support ----------------------------------------------------------------*/
#ifdef __cplusplus
}
#endif

#endif /* __XST_DRV_H__ */
