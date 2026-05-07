/* elog 输出到 USART3（中断方式），由 xst_porting 层提供发送 */

#include "main.h"
#include "stm32f103xb.h"
#include "stm32f1xx_hal_def.h"
#include "stm32f1xx_hal_uart.h"
#include "usart.h"
#include <elog.h>
#include <stdio.h>

ElogErrCode elog_port_init(void) { return ELOG_NO_ERR; }

void elog_port_deinit(void) {}

void elog_port_output(const char *log, size_t size)
{
    HAL_UART_Transmit(&huart3, (uint8_t *)log, size, HAL_MAX_DELAY);
}

void elog_port_output_lock(void) { __disable_irq(); }
void elog_port_output_unlock(void) { __enable_irq(); }

const char *elog_port_get_time(void)
{
    static char cur_system_time[16] = {0};
    snprintf(cur_system_time, 16, "%lu", HAL_GetTick());
    return cur_system_time;
}

const char *elog_port_get_p_info(void) { return ""; }
const char *elog_port_get_t_info(void) { return ""; }
