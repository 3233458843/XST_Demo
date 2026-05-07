#ifndef _SHELL_PORT_H
#define _SHELL_PORT_H

#include "shell.h"
#include "usart.h"

#include "stddef.h"
#include <stdint.h>

extern Shell env_shell;
extern char shell_buffer[512];

    void
     Port_Shell_RTT_Task(void);
void Port_Shell_RTT_Init(void);

#endif