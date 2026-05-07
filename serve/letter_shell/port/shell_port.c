#include "shell_port.h"
#include "stm32f103xb.h"
#include "stm32f1xx_hal_def.h"
#include "stm32f1xx_hal_uart.h"
#include <stdint.h>

Shell env_shell;
char shell_buffer[512];

extern int xst_porting_read_shell_byte(uint8_t *byte);

static short _shell_write(char *data, unsigned short len) {
    return HAL_UART_Transmit(&huart2, (uint8_t *)data, len, 100) == HAL_OK ? len : 0;
}

static short _shell_read(char *data, unsigned short len) {
    if (HAL_UART_Receive(&huart2, (uint8_t *)data, len ,0) != HAL_OK) {
        return 0;
    } else {
        return 1;
    }
}

void Port_Shell_RTT_Task(void) {
    shellTask(&env_shell);
}

void Port_Shell_RTT_Init(void) {
    env_shell.write = _shell_write;
    env_shell.read  = _shell_read;
    shellInit(&env_shell, shell_buffer, sizeof(shell_buffer));
    // HAL_UART_Receive_IT(&huart2, (uint8_t *)shell_buffer, 1);
}
