/**
 * @file      xst_porting.c
 * @brief     XST 极简移植层 (STM32F1xx HAL)
 * @note      USART1 = XST 掌静脉模块, USART2 = Shell, USART3 = elog
 */

#include "xst_porting.h"
#include "main.h"
#include "stm32f1xx_hal_def.h"
#include "usart.h"

static int      xst_hw_send(void *user_ctx, const uint8_t *data, size_t len);
static int      xst_hw_init(void *user_ctx);
static uint32_t xst_get_tick(void *user_ctx);

xst_handle_t xst_port_dev     = NULL;
uint8_t      xst_port_rx_byte = 0;

#if defined(HAL_UART_MODULE_ENABLED)

static int xst_hw_send(void *user_ctx, const uint8_t *data, size_t len)
{
    return (HAL_UART_Transmit((UART_HandleTypeDef *)user_ctx, (uint8_t *)data, (uint16_t)len,
                              HAL_MAX_DELAY) == HAL_OK)
               ? 0
               : -1;
}

static int xst_hw_init(void *user_ctx)
{
    (void)user_ctx;
    HAL_UART_Receive_IT(&huart1, &xst_port_rx_byte, 1);
    return 0;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1 && xst_port_dev != NULL) {
        xst_write_rx_data(xst_port_dev, &xst_port_rx_byte, 1);
        HAL_UART_Receive_IT(&huart1, &xst_port_rx_byte, 1);
    }
}

static uint32_t xst_get_tick(void *user_ctx)
{
    (void)user_ctx;
    return HAL_GetTick();
}

void xst_port_init(void)
{
    xst_port_dev = xst_create_device("palm_vein", 0);
    if (xst_port_dev == NULL)
        return;

    static xst_config_t cfg;
    xst_porting_fill_default_cfg(&cfg);

    static xst_ops_t ops = {.hw_init     = xst_hw_init,
                            .hw_deinit   = NULL,
                            .hw_send     = xst_hw_send,
                            .hw_receive  = NULL, // 这里不需要接收函数，因为是HAL库，开启串口中断后会进入回调函数。
                            .get_tick_ms = xst_get_tick,
                            .user_ctx    = &huart1};
    xst_registe_cfg_and_ops(xst_port_dev, &cfg, &ops);
    xst_init(xst_port_dev);
}

xst_handle_t xst_porting_get_device(void) { return xst_port_dev; }


#else /* mock */

static int xst_hw_send(void *user_ctx, const uint8_t *data, size_t len)
{
    (void)user_ctx;
    (void)data;
    (void)len;
    return 0;
}
static int xst_hw_init(void *user_ctx)
{
    (void)user_ctx;
    return 0;
}
static uint32_t xst_get_tick(void *user_ctx)
{
    (void)user_ctx;
    return 0;
}
void         xst_port_init(void) {}
xst_handle_t xst_porting_get_device(void) { return NULL; }
int          xst_porting_read_shell_byte(uint8_t *byte)
{
    (void)byte;
    return 0;
}

#endif

/** @addtogroup XST_PORTING
 * @brief XST 极简移植层接口实现
 *
 * 主要功能包括：
 * - 定义默认配置
 * - 定义硬件相关操作函数
 * - 提供设备句柄获取接口
*/
void xst_porting_fill_default_cfg(xst_config_t *cfg)
{
    if (!cfg)
        return;
    cfg->rx_buf_size          = XST_RX_BUF_DEFAULT_SIZE;
    cfg->tx_buf_size          = XST_TX_BUF_DEFAULT_SIZE;
    cfg->normal_frame_max_len = XST_FRAME_MAX_LEN;
    cfg->bulk_frame_max_len   = 4000;
    cfg->default_timeout_ms   = XST_DEFAULT_TIMEOUT_MS;
}

/** @addtogroup XST_PORTING
 * @brief XST 极简移植层接口实现
 *
 * 主要功能包括：
 * - 定义默认配置
 * - 定义硬件相关操作函数
 * - 提供设备句柄获取接口
*/
void xst_porting_clear_ops(xst_ops_t *ops)
{
    if (ops) {
        ops->hw_init     = NULL;
        ops->hw_deinit   = NULL;
        ops->hw_send     = NULL;
        ops->hw_receive  = NULL;
        ops->get_tick_ms = NULL;
        ops->user_ctx    = NULL;
    }
}
