/**
 * @file      xst_porting.h
 * @brief     XST 驱动平台适配公共接口
 */

#ifndef __XST_PORTING_H__
#define __XST_PORTING_H__

#include "xst_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

extern xst_handle_t xst_port_dev;

void xst_port_init(void);

xst_handle_t xst_porting_get_device(void);

void xst_porting_fill_default_cfg(xst_config_t *cfg);
void xst_porting_clear_ops(xst_ops_t *ops);
int  xst_porting_validate_ops(const xst_ops_t *ops);

#ifdef __cplusplus
}
#endif

#endif /* __XST_PORTING_H__ */
