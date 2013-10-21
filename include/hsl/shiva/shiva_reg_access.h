/*
 * Copyright (c) 2012 Qualcomm Atheros, Inc.
 * All rights reserved.
 * Qualcomm Atheros Confidential and Proprietary.
 *
 */

#ifndef _SHIVA_REG_ACCESS_H_
#define _SHIVA_REG_ACCESS_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "common/sw.h"

    sw_error_t
    shiva_phy_get(a_uint32_t dev_id, a_uint32_t phy_addr,
                  a_uint32_t reg, a_uint16_t * value);

    sw_error_t
    shiva_phy_set(a_uint32_t dev_id, a_uint32_t phy_addr,
                  a_uint32_t reg, a_uint16_t value);

    sw_error_t
    shiva_reg_get(a_uint32_t dev_id, a_uint32_t reg_addr, a_uint8_t value[],
                  a_uint32_t value_len);

    sw_error_t
    shiva_reg_set(a_uint32_t dev_id, a_uint32_t reg_addr, a_uint8_t value[],
                  a_uint32_t value_len);

    sw_error_t
    shiva_reg_field_get(a_uint32_t dev_id, a_uint32_t reg_addr,
                        a_uint32_t bit_offset, a_uint32_t field_len,
                        a_uint8_t value[], a_uint32_t value_len);

    sw_error_t
    shiva_reg_field_set(a_uint32_t dev_id, a_uint32_t reg_addr,
                        a_uint32_t bit_offset, a_uint32_t field_len,
                        const a_uint8_t value[], a_uint32_t value_len);

    sw_error_t
    shiva_reg_access_init(a_uint32_t dev_id, hsl_access_mode mode);

    sw_error_t
    shiva_access_mode_set(a_uint32_t dev_id, hsl_access_mode mode);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _SHIVA_REG_ACCESS_H_ */

