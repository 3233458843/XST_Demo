/**
 * @file      xst_pack_t.h
 * @brief     XST掌静脉数据包类型定义
 * @author    3233458843@qq.com
 * @version   3.0
 * @date      2026-04-23
 *
 * @copyright Copyright (c) 2026 All rights reserved.
 *
 * @note      本驱动仅供学习和交流使用，禁止用于商业用途。
 *            如果没有随软件附带 LICENSE 文件，则按 AS-IS 提供。
 *            ！！！ 本驱动未经充分测试，可能存在缺陷和问题，请谨慎使用 ！！！
 *            如果您发现了任何问题或有改进建议，请联系作者。
 *            本驱动的作者不对任何因使用本驱动而导致的损失或损害负责。
 *            本掌静脉驱动需配合lwrb环形缓冲区使用，且仅支持单线程环境。
 *            本驱动的功能和性能可能有限，可能不适用于所有应用场景，请根据实际需求进行评估和测试。
 *            本驱动的接口和实现可能会发生变化，请关注后续更新和维护。
 *            本驱动的使用和修改请遵守相关法律法规和道德规范，不得用于任何非法或不当的目的。
 */

#ifndef __XST_PACK_T_H__
#define __XST_PACK_T_H__

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*  ------------------------------------------------------------*/
#ifdef __cplusplus
extern "C" {
#endif

/* Exported macros -----------------------------------------------------------*/
#define XST_FRAME_H 0xEF
#define XST_FRAME_L 0xAA

/* Exported types ------------------------------------------------------------*/
typedef struct {
    uint8_t *header;   // 包头
    uint8_t *msgID;    // 命令字，表示数据包的类型和功能
    uint8_t *len;      // 数据长度，表示数据字段的字节数
    uint8_t *data;     // 数据字段，存储实际的数据内容，最大255字节
    uint8_t  checksum; // 校验和，计算方法为cmd、len和data字段的异或值
} xst_frame_format_t;

typedef struct {
    uint8_t  mid;
    uint8_t  result;
    uint8_t *paydata;
} xst_reply_farme_t;

typedef struct {
    uint8_t  mid;
    uint8_t *paydata;
} xst_note_farme_t;

typedef enum {
    MID_REPLY                 = 0x00, // Reply 是模组对主控发送出的命令的应答，对于主控的每条命令，模组最终都会进行 reply
    MID_NOTE                  = 0x01, // Note 是模组主动发给主控的信息，根据 note id 判断消息类型和适配的 data 结构
    MID_IMAGE                 = 0x02, // 模组给主控传送图片
    MID_LOG                   = 0x03, // 模组给主控传送日志文件
    MID_DAT                   = 0x04, // 模组给主控传送特征文件
    MID_RESET                 = 0x10, // 停止所有当前在处理的消息，模组进入待机状态
    MID_GETSTATUS             = 0x11, // 立即返回模组当前状态
    MID_VERIFY                = 0x12, // 鉴权解锁
    MID_ENROLL                = 0x13, // 新用户注册：交互式录入
    MID_ENROLL_PROGRESS       = 0x14, // 获取录入进度值
    MID_SNAP_IMAGE            = 0x16, // 抓拍图片并存储到本地
    MID_GET_SAVED_IMAGE       = 0x17, // 获取待上传图片大小
    MID_UPLOAD_IMAGE          = 0x18, // 将本地存储图片上传到主控
    MID_ENROLL_SINGLE         = 0x1D, // 新用户注册：单帧录入
    MID_ENROLL_SINGLE_ID16    = 0x1E, // 新用户注册：单帧录入，支持输入 16 位的用户 ID
    MID_ENROLL_SINGLE_ID32    = 0x1F, // 新用户注册：单帧录入，支持输入 32 位的用户 ID
    MID_DEL_USER              = 0x20, // 删除一个注册用户
    MID_DEL_ALL               = 0x21, // 删除所有注册用户
    MID_GET_USER_INFO         = 0x22, // 获得某个注册用户的信息
    MID_ALGORITHM_RESET       = 0x23, // 重置算法状态，如果正在进行交互式录入，会清空已录入的方向
    MID_GET_ALL_USER_ID       = 0x24, // 获取所有已注册用户的数量和 ID
    MID_GET_ALL_USER_INFO     = 0x25, // 获取所有已注册用户的信息
    MID_GET_VERSION           = 0x30, // 获得软件版本信息
    MID_START_OTA             = 0x40, // 进入 OTA 升级模式
    MID_STOP_OTA              = 0x41, // 退出 OTA 模式，模组重启
    MID_GET_OTA_STATUS        = 0x42, // 获取 OTA 状态以及传输升级包的起始包序号
    MID_OTA_HEADER            = 0x43, // 发送升级包的大小、总包数、分包大小、升级包 md5 值
    MID_OTA_PACKET            = 0x44, // 发送升级包：包序号、包大小、包数据
    MID_INIT_ENCRYPTION       = 0x50, // 设置加密随机数
    MID_CONFIG_BAUDRATE       = 0x51, // OTA 模式下设定通信口波特率
    MID_SET_RELEASE_ENC_KEY   = 0x52, // 设定量产加密秘钥序列，掉电保存
    MID_SET_DEBUG_ENC_KEY     = 0x53, // 设定调试加密秘钥序列，掉电丢失
    MID_GET_LOG_FILE_SIZE     = 0x60, // 获取 log 日志文件的大小
    MID_UPLOAD_LOG_FILE       = 0x61, // 将保存的 log 日志文件上传至主控
    MID_ENROLL_BIOTYPE        = 0xA0, // 用户注册，支持输入生物特征信息类别，用户 ID 自动增加
    MID_ENROLL_BIOTYPE_ID16   = 0xA1, // 用户注册，支持输入生物特征信息类别和 16 位用户 ID
    MID_ENROLL_BIOTYPE_ID32   = 0xA2, // 用户注册，支持输入生物特征信息类别和 32 位用户 ID
    MID_VERIFY_BIOTYPE        = 0xA5, // 识别，支持输入生物特征信息类别
    MID_DEL_USER_BIOTYPE      = 0xB0, // 删除一个指定用户，支持输入生物特征信息类别
    MID_DEL_ALL_BIOTYPE       = 0xB1, // 删除所有注册用户，支持输入生物特征信息类别
    MID_GET_ALL_USER_ID_BIOTYPE = 0xB4, // 获取所有已注册用户的数量和 ID，支持输入生物特征信息类别
    MID_GET_USER_INFO_BIOTYPE = 0xB5, // 获得某个注册用户的信息，支持输入生物特征信息类别
    MID_GET_ALL_INFO_BIOTYPE  = 0xB6, // 获得所有注册用户的信息，支持输入生物特征信息类别
    MID_GET_DAT_FILE_SIZE     = 0xC0, // 获取指定用户特征文件大小，支持输入生物特征信息类别
    MID_UPLOAD_DAT_FILE       = 0xC1, // 上传指定用户特征文件，支持输入生物特征信息类别
    MID_INFORM_DAT_FILE_SIZE  = 0xC2, // 告知待下载的用户特征文件大小，支持输入生物特征信息类别
    MID_DOWNLOAD_DAT_FILE     = 0xC3, // 下载指定用户的特征文件，支持输入生物特征信息类别
    MID_SET_THRESHOLD_LEVEL   = 0xD4, // 设置算法安全等级
    MID_POWER_DOWN            = 0xED, // 模组断电前，保存简单的 log
    MID_DEBUG_MODE            = 0xF0, // 使能 debug 模式，会存储所有图片和较多 log
    MID_GET_DEBUG_INFO        = 0xF1, // 获取 debug 模式下存储的数据包大小
    MID_UPLOAD_DEBUG_INFO     = 0xF2, // 上传 debug 模式下存储的数据包
    MID_GET_LIBRARY_VERSION   = 0xF3, // 获取当前算法库版本信息
    MID_DEMO_MODE             = 0xFE, // 进入演示模式
    MID_QUIT                  = 0xFF  // 系统退出
} xst_return_msgid_t;

typedef enum {
    MR_SUCCESS                = 0,  // 指令执行成功
    MR_REJECTED               = 1,  // 模组拒绝该命令
    MR_ABORTED                = 2,  // 录入/解锁算法已终止
    MR_FAILED4_CAMERA         = 4,  // 摄像头打开失败
    MR_FAILED4_UNKNOWN_REASON = 5,  // 未知错误
    MR_FAILED4_INVALID_PARAM  = 6,  // 无效的参数
    MR_FAILED4_NO_MEMORY      = 7,  // 内存不足
    MR_FAILED4_UNKNOWN_USER   = 8,  // 未录入的用户
    MR_FAILED4_MAX_USER       = 9,  // 录入超过最大数量
    MR_FAILED4_ENROLLED       = 10, // 已录入
    MR_FAILED4_LIVENESS_CHECK = 12, // 活体检测失败
    MR_FAILED4_TIME_OUT       = 13, // 录入或解锁超时
    MR_FAILED4_LICENSE_FAIL   = 14, // 加密芯片授权失败
    MR_FAILED4_READ_FILE      = 19, // 读取文件失败
    MR_FAILED4_WRITE_FILE     = 20, // 写入文件失败
    MR_FAILED4_NO_ENCRYPT     = 21  // 未采用加密通讯
} xst_return_result_t;

#define MR_FAILED4_AUTHORIZATION MR_FAILED4_LICENSE_FAIL

typedef enum {
    NID_READY         = 0, // 模组已准备好
    NID_FACE_STATE    = 1, // 算法执行成功, 并返回人脸信息
    NID_UNKNOWNERROR  = 2, // 未知错误
    NID_OTA_DONE      = 3, // OTA 升级完毕
    NID_PALM_STATE    = 4, // 算法执行成功, 并返回手掌信息
    NID_LICENSE_FAIL  = 8  // License 验证失败 (PDF:NID_AUTHORIZATION)
} xst_return_nid_t;

#define NID_AUTHORIZATION NID_LICENSE_FAIL

typedef enum {
    MS_STANDBY = 0, // 表示模组处于空闲状态，等待主控命令
    MS_BUSY    = 1, // 表示模组处于工作状态
    MS_ERROR   = 2, // 表示模组出错，不能正常工作
    MS_INVALID = 3  // 表示模组模组未进行初始化
} xst_return_state_t;

typedef enum {
    XST_EVENT_NONE = 0, /* 无事件 */
    XST_EVENT_REPLY,    /* 模组应答帧 (MID_REPLY) — 命令执行结果 */
    XST_EVENT_NOTE,     /* 模组主动通知 (MID_NOTE) — 状态/错误上报 */
    XST_EVENT_DATA,     /* 批量数据传输 (MID_IMAGE/MID_LOG/MID_DAT) */
    XST_EVENT_ERROR,    /* 解析错误/校验失败/超时 */
} xst_event_type_t;

typedef enum {
    XST_PARSE_IDLE = 0, /* 空闲，等待帧头 0xEF */
    XST_PARSE_HEADER_H, /* 收到 0xEF，等待 0xAA */
    XST_PARSE_MSGID,    /* 帧头完整，等待 msgID */
    XST_PARSE_LEN_H,    /* 收到 msgID，等待长度高字节 */
    XST_PARSE_LEN_L,    /* 等待长度低字节 */
    XST_PARSE_DATA,     /* 收集中载荷 */
    XST_PARSE_CHECKSUM, /* 校验和检测 */
} xst_parse_state_t;

/* 同步命令执行（内部轮询等待回复，超时返回 -1） */
typedef struct {
    uint8_t  msgid;    /* 回显的命令 ID */
    uint8_t  result;   /* 操作结果 (xst_return_result_t) */
    uint16_t pay_len;  /* 负载长度 */
    uint8_t *pay_data; /* 负载指针（指向内部缓冲，尽快使用） */
} xst_reply_t;

typedef struct {
    xst_event_type_t type;
    union {
        /* MID_REPLY: data 指向 parsed_data[2]，即 payload 起始 */
        struct {
            uint8_t  mid;
            uint8_t  result;
            uint8_t *data;
            uint16_t len;
        } reply;
        /* MID_NOTE: data 指向 parsed_data[1]，即 payload 起始 */
        struct {
            uint8_t  note_id;
            uint8_t *data;
            uint16_t len;
        } note;
        /* MID_IMAGE / MID_LOG / MID_DAT: data 指向 parsed_data[0] */
        struct {
            uint8_t  msgid;
            uint8_t *data;
            uint16_t len;
        } data;
        /* 解析错误 / 超时 / 校验失败 */
        struct {
            int code;
        } error;
    };
} xst_event_t;
/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* __XST_PACK_T_H__ */
