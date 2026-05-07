/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "gpio.h"
#include "usart.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "elog.h"
#include "shell_port.h"
#include "xst_pack_t.h"
#include "xst_porting.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#define MAIN_TAG "main"
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint8_t xst_user_id_test[2] = {0, 0};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void on_xst_event(xst_handle_t handle, const xst_event_t *e, void *ctx);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{

    /* USER CODE BEGIN 1 */

    /* USER CODE END 1 */

    /* MCU Configuration--------------------------------------------------------*/

    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

    /* USER CODE BEGIN Init */

    /* USER CODE END Init */

    /* Configure the system clock */
    SystemClock_Config();

    /* USER CODE BEGIN SysInit */

    /* USER CODE END SysInit */

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();
    MX_USART3_UART_Init();
    /* USER CODE BEGIN 2 */

    /* 初始化日志系统 (USART3) */
    elog_init();
    elog_start();
    elog_i(MAIN_TAG, "elog initialized successfully");
    /* 初始化命令行 shell (UART2) */
    Port_Shell_RTT_Init();

    /* 初始化掌静脉驱动 */
    xst_port_init();

    /* 注册事件回调 */
    xst_handle_t dev = xst_porting_get_device();
    if (dev != NULL) {
        xst_set_event_callback(dev, on_xst_event, NULL);
    }
    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1) {
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */
        xst_process_all();
        Port_Shell_RTT_Task();
    }
    /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** Initializes the RCC Oscillators according to the specified parameters
     * in the RCC_OscInitTypeDef structure.
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState       = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks
     */
    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
        Error_Handler();
    }

    /** Enables the Clock Security System
     */
    HAL_RCC_EnableCSS();
}

/* USER CODE BEGIN 4 */

static void on_xst_event(xst_handle_t handle, const xst_event_t *e, void *ctx)
{
    (void)handle;
    (void)ctx;

    switch (e->type) {
        case XST_EVENT_REPLY:
            elog_i(MAIN_TAG, "REPLY  mid=0x%02X result=%d len=%d", e->reply.mid, e->reply.result,
                   e->reply.len);
            if (e->reply.len > 0) {
                char     hex[64] = {0};
                uint16_t i;
                for (i = 0; i < e->reply.len; i++) {
                    sprintf(hex + (i % 16) * 3, "%02X ", e->reply.data[i]);
                    if ((i % 16) == 15 || i == e->reply.len - 1) {
                        elog_i(MAIN_TAG, "       data[%3d-%3d]: %s", i - (i % 16), i, hex);
                        memset(hex, 0, sizeof(hex));
                    }
                }
            }
            switch (e->reply.mid) {
                case MID_RESET:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module reset successfully");
                    else
                        elog_e(MAIN_TAG, "xst module reset failed with code %d", e->reply.result);
                    break;
                case MID_GETSTATUS:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module status retrieved successfully");
                    else
                        elog_e(MAIN_TAG, "xst module status retrieval failed with code %d",
                               e->reply.result);
                    break;
                case MID_VERIFY:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module verification successful");
                    else
                        elog_e(MAIN_TAG, "xst module verification failed with code %d",
                               e->reply.result);
                    break;
                case MID_ENROLL:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module enrollment successful");
                    else
                        elog_e(MAIN_TAG, "xst module enrollment failed with code %d",
                               e->reply.result);
                    break;
                case MID_ENROLL_PROGRESS:
                    if (e->reply.result == MR_SUCCESS && e->reply.len >= 1)
                        elog_i(MAIN_TAG, "xst module enrollment progress: %d%%", e->reply.data[0]);
                    else if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module enrollment progress updated");
                    else
                        elog_e(MAIN_TAG,
                               "xst module enrollment progress update failed with code %d",
                               e->reply.result);
                    break;
                /******************************以下为不常用回复指令，可删除************/
                case MID_SNAP_IMAGE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module snapshot image taken successfully");
                    else
                        elog_e(MAIN_TAG, "xst module snapshot image failed with code %d",
                               e->reply.result);
                    break;
                case MID_GET_SAVED_IMAGE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module saved image retrieved successfully");
                    else
                        elog_e(MAIN_TAG, "xst module saved image retrieval failed with code %d",
                               e->reply.result);
                    break;
                case MID_UPLOAD_IMAGE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module image uploaded successfully");
                    else
                        elog_e(MAIN_TAG, "xst module image upload failed with code %d",
                               e->reply.result);
                    break;
                /*******************************删除至此****************************/
                case MID_ENROLL_SINGLE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module single enrollment successful");
                    else
                        elog_e(MAIN_TAG, "xst module single enrollment failed with code %d",
                               e->reply.result);
                    break;
                case MID_ENROLL_SINGLE_ID16:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module ID16 enrollment successful");
                    else
                        elog_e(MAIN_TAG, "xst module ID16 enrollment failed with code %d",
                               e->reply.result);
                    break;
                case MID_ENROLL_SINGLE_ID32:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module ID32 enrollment successful");
                    else
                        elog_e(MAIN_TAG, "xst module ID32 enrollment failed with code %d",
                               e->reply.result);
                    break;
                case MID_DEL_USER:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module user deleted successfully");
                    else
                        elog_e(MAIN_TAG, "xst module user deletion failed with code %d",
                               e->reply.result);
                    break;
                case MID_DEL_ALL:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module all users deleted successfully");
                    else
                        elog_e(MAIN_TAG, "xst module all users deletion failed with code %d",
                               e->reply.result);
                    break;
                case MID_GET_USER_INFO:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module user info retrieved successfully");
                    else
                        elog_e(MAIN_TAG, "xst module user info retrieval failed with code %d",
                               e->reply.result);
                    break;
                /******************************以下为不常用回复指令，可删除************/
                case MID_ALGORITHM_RESET:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module algorithm reset successful");
                    else
                        elog_e(MAIN_TAG, "xst module algorithm reset failed with code %d",
                               e->reply.result);
                    break;
                /*******************************删除至此*****************************/
                case MID_GET_ALL_USER_ID:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module all user IDs retrieved successfully");
                    else
                        elog_e(MAIN_TAG, "xst module all user IDs retrieval failed with code %d",
                               e->reply.result);
                    break;
                case MID_GET_ALL_USER_INFO:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module all user info retrieved successfully");
                    else
                        elog_e(MAIN_TAG, "xst module all user info retrieval failed with code %d",
                               e->reply.result);
                    break;
                case MID_GET_VERSION:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module version retrieved successfully");
                    else
                        elog_e(MAIN_TAG, "xst module version retrieval failed with code %d",
                               e->reply.result);
                    break;
                /*******************************以下为不常用回复指令，可删除***********/
                case MID_START_OTA:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module OTA started successfully");
                    else
                        elog_e(MAIN_TAG, "xst module OTA start failed with code %d",
                               e->reply.result);
                    break;
                case MID_STOP_OTA:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module OTA stopped successfully");
                    else
                        elog_e(MAIN_TAG, "xst module OTA stop failed with code %d",
                               e->reply.result);
                    break;
                case MID_GET_OTA_STATUS:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module OTA status retrieved successfully");
                    else
                        elog_e(MAIN_TAG, "xst module OTA status retrieval failed with code %d",
                               e->reply.result);
                    break;
                case MID_OTA_HEADER:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module OTA header retrieved successfully");
                    else
                        elog_e(MAIN_TAG, "xst module OTA header retrieval failed with code %d",
                               e->reply.result);
                    break;
                case MID_OTA_PACKET:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module OTA packet retrieved successfully");
                    else
                        elog_e(MAIN_TAG, "xst module OTA packet retrieval failed with code %d",
                               e->reply.result);
                    break;
                case MID_INIT_ENCRYPTION:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module encryption initialized successfully");
                    else
                        elog_e(MAIN_TAG, "xst module encryption initialization failed with code %d",
                               e->reply.result);
                    break;
                case MID_CONFIG_BAUDRATE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module baud rate configured successfully");
                    else
                        elog_e(MAIN_TAG, "xst module baud rate configuration failed with code %d",
                               e->reply.result);
                    break;
                case MID_SET_RELEASE_ENC_KEY:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module release encryption key set successfully");
                    else
                        elog_e(MAIN_TAG,
                               "xst module release encryption key setting failed with code %d",
                               e->reply.result);
                    break;
                case MID_SET_DEBUG_ENC_KEY:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module debug encryption key set successfully");
                    else
                        elog_e(MAIN_TAG,
                               "xst module debug encryption key setting failed with code %d",
                               e->reply.result);
                    break;
                case MID_GET_LOG_FILE_SIZE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module log file size retrieved successfully");
                    else
                        elog_e(MAIN_TAG, "xst module log file size retrieval failed with code %d",
                               e->reply.result);
                    break;
                case MID_UPLOAD_LOG_FILE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module log file uploaded successfully");
                    else
                        elog_e(MAIN_TAG, "xst module log file upload failed with code %d",
                               e->reply.result);
                    break;
                /*******************************删除至此*****************************/
                case MID_ENROLL_BIOTYPE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module enrollment successful");
                    else
                        elog_e(MAIN_TAG, "xst module enrollment failed with code %d",
                               e->reply.result);
                    break;
                case MID_ENROLL_BIOTYPE_ID16:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module ID16 enrollment successful");
                    else
                        elog_e(MAIN_TAG, "xst module ID16 enrollment failed with code %d",
                               e->reply.result);
                    break;
                case MID_ENROLL_BIOTYPE_ID32:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module ID32 enrollment successful");
                    else
                        elog_e(MAIN_TAG, "xst module ID32 enrollment failed with code %d",
                               e->reply.result);
                    break;
                case MID_VERIFY_BIOTYPE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module verification successful");
                    else
                        elog_e(MAIN_TAG, "xst module verification failed with code %d",
                               e->reply.result);
                    break;
                case MID_DEL_USER_BIOTYPE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module user deletion successful");
                    else
                        elog_e(MAIN_TAG, "xst module user deletion failed with code %d",
                               e->reply.result);
                    break;
                case MID_DEL_ALL_BIOTYPE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module all users deleted successfully");
                    else
                        elog_e(MAIN_TAG, "xst module all users deletion failed with code %d",
                               e->reply.result);
                    break;
                case MID_GET_ALL_USER_ID_BIOTYPE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module all user IDs retrieved successfully");
                    else
                        elog_e(MAIN_TAG, "xst module all user IDs retrieval failed with code %d",
                               e->reply.result);
                    break;
                case MID_GET_USER_INFO_BIOTYPE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module user info retrieved successfully");
                    else
                        elog_e(MAIN_TAG, "xst module user info retrieval failed with code %d",
                               e->reply.result);
                    break;
                case MID_GET_ALL_INFO_BIOTYPE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module all info retrieved successfully");
                    else
                        elog_e(MAIN_TAG, "xst module all info retrieval failed with code %d",
                               e->reply.result);
                    break;
                /*******************************以下为不常用回复指令，可删除***********/
                case MID_GET_DAT_FILE_SIZE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module data file size retrieved successfully");
                    else
                        elog_e(MAIN_TAG, "xst module data file size retrieval failed with code %d",
                               e->reply.result);
                    break;
                case MID_UPLOAD_DAT_FILE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module data file uploaded successfully");
                    else
                        elog_e(MAIN_TAG, "xst module data file upload failed with code %d",
                               e->reply.result);
                    break;
                case MID_INFORM_DAT_FILE_SIZE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module data file size informed successfully");
                    else
                        elog_e(MAIN_TAG, "xst module data file size informing failed with code %d",
                               e->reply.result);
                    break;
                case MID_DOWNLOAD_DAT_FILE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module data file downloaded successfully");
                    else
                        elog_e(MAIN_TAG, "xst module data file download failed with code %d",
                               e->reply.result);
                    break;
                case MID_SET_THRESHOLD_LEVEL:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module threshold level set successfully");
                    else
                        elog_e(MAIN_TAG, "xst module threshold level setting failed with code %d",
                               e->reply.result);
                    break;
                case MID_POWER_DOWN:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module powered down successfully");
                    else
                        elog_e(MAIN_TAG, "xst module power down failed with code %d",
                               e->reply.result);
                    break;
                case MID_DEBUG_MODE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module debug mode enabled successfully");
                    else
                        elog_e(MAIN_TAG, "xst module debug mode enabling failed with code %d",
                               e->reply.result);
                    break;
                case MID_GET_DEBUG_INFO:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module debug info retrieved successfully");
                    else
                        elog_e(MAIN_TAG, "xst module debug info retrieval failed with code %d",
                               e->reply.result);
                    break;
                case MID_UPLOAD_DEBUG_INFO:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module debug info uploaded successfully");
                    else
                        elog_e(MAIN_TAG, "xst module debug info upload failed with code %d",
                               e->reply.result);
                    break;
                case MID_GET_LIBRARY_VERSION:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module library version retrieved successfully");
                    else
                        elog_e(MAIN_TAG, "xst module library version retrieval failed with code %d",
                               e->reply.result);
                    break;
                case MID_DEMO_MODE:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "xst module demo mode enabled successfully");
                    else
                        elog_e(MAIN_TAG, "xst module demo mode enabling failed with code %d",
                               e->reply.result);
                    break;
                /*******************************删除至此*****************************/
                case MID_QUIT:
                    if (e->reply.result == MR_SUCCESS)
                        elog_i(MAIN_TAG, "License authorization successful");
                    else
                        elog_e(MAIN_TAG, "License authorization failed with code %d",
                               e->reply.result);
                    break;
                default:
                    elog_w(MAIN_TAG, "License authorization failed");
                    break;
            }
            break;

        case XST_EVENT_NOTE:
            elog_i(MAIN_TAG, "NOTE  nid=0x%02X len=%d", e->note.note_id, e->note.len);
            if (e->note.len > 0) {
                char     hex[64] = {0};
                uint16_t i;
                for (i = 0; i < e->note.len; i++) {
                    sprintf(hex + (i % 16) * 3, "%02X ", e->note.data[i]);
                    if ((i % 16) == 15 || i == e->note.len - 1) {
                        elog_i(MAIN_TAG, "       data[%3d-%3d]: %s", i - (i % 16), i, hex);
                        memset(hex, 0, sizeof(hex));
                    }
                }
            }
            switch (e->note.note_id) {
                case NID_READY:
                    elog_i(MAIN_TAG, "Device is ready");
                    break;
                case NID_FACE_STATE:
                    elog_i(MAIN_TAG, "Face state updated");
                    break;
                case NID_UNKNOWNERROR:
                    elog_i(MAIN_TAG, "An unknown error occurred");
                    break;
                case NID_OTA_DONE:
                    elog_i(MAIN_TAG, "OTA update completed");
                    break;
                case NID_PALM_STATE:
                    elog_i(MAIN_TAG, "Palm state updated");
                    break;
                case NID_LICENSE_FAIL:
                    elog_w(MAIN_TAG, "License authorization failed");
                    break;
                default:
                    elog_w(MAIN_TAG, "Unknown note received");
                    break;
            }
            break;

        case XST_EVENT_DATA:
            elog_i(MAIN_TAG, "DATA  msgid=0x%02X len=%d", e->data.msgid, e->data.len);
            if (e->data.len > 0) {
                char     hex[64] = {0};
                uint16_t i;
                for (i = 0; i < e->data.len; i++) {
                    sprintf(hex + (i % 16) * 3, "%02X ", e->data.data[i]);
                    if ((i % 16) == 15 || i == e->data.len - 1) {
                        elog_i(MAIN_TAG, "       data[%3d-%3d]: %s", i - (i % 16), i, hex);
                        memset(hex, 0, sizeof(hex));
                    }
                }
            }
            break;

        case XST_EVENT_ERROR:
            elog_e(MAIN_TAG, "ERROR code=%d", e->error.code);
            break;

        case XST_EVENT_NONE:
            break;

        default:
            break;
    }
}

void elog_shell_test(void)
{
    elog_i(MAIN_TAG, "This is an info message");
    elog_w(MAIN_TAG, "This is a warning message");
    elog_e(MAIN_TAG, "This is an error message");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0), log_test, elog_shell_test, test function);

void xst_res(void) { xst_reset_module(xst_port_dev); }
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0), xst_res, xst_res, reset xst module);

void xst_vfy(void)
{
    uint16_t len = sizeof(xst_user_id_test);
    xst_verify(xst_port_dev, xst_user_id_test, &len);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0), xst_vfy, xst_vfy, verify xst module);

void xst_dell_all(void) { xst_delete_all(xst_port_dev); }
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0), xst_dell_all, xst_dell_all,
                 delete all users in xst module);

void xst_regeid(void) { xst_enroll(xst_port_dev, 1, (uint8_t *)"water", 10); }
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0), xst_regeid, xst_regeid, register user ID in xst module);
/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1) {
    }
    /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
