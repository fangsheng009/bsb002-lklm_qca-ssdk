/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "sw.h"
#include "fal_port_ctrl.h"
#include "hsl_api.h"
#include "hsl.h"
#include "malibu_phy.h"
#include "aos_timer.h"
#include "hsl_phy.h"
#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>

static a_uint16_t
_phy_reg_read(a_uint32_t dev_id, a_uint32_t phy_addr, a_uint32_t reg)
{
	sw_error_t rv;
	a_uint16_t phy_data;

	HSL_PHY_GET(rv, dev_id, phy_addr, reg, &phy_data);
	if (SW_OK != rv)
		return 0xFFFF;

	return phy_data;

}

static void
_phy_reg_write(a_uint32_t dev_id, a_uint32_t phy_addr, a_uint32_t reg,
	       a_uint16_t phy_data)
{
	sw_error_t rv;

	HSL_PHY_SET(rv, dev_id, phy_addr, reg, phy_data);
}

#define malibu_phy_reg_read _phy_reg_read
#define malibu_phy_reg_write _phy_reg_write

/******************************************************************************
*
*  phy4 medium is fiber 100fx
*
*  get phy4 medium is 100fx
*/
static a_bool_t __medium_is_fiber_100fx(a_uint32_t dev_id, a_uint32_t phy_id)
{
	a_uint16_t phy_data = 0;

	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_SGMII_STATUS);

	if (phy_data & MALIBU_PHY4_AUTO_FX100_SELECT) {
		return A_TRUE;
	}
	/* Link down */
	if ((!(phy_data & MALIBU_PHY4_AUTO_COPPER_SELECT)) &&
	    (!(phy_data & MALIBU_PHY4_AUTO_BX1000_SELECT)) &&
	    (!(phy_data & MALIBU_PHY4_AUTO_SGMII_SELECT))) {

		phy_data =
		    malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CHIP_CONFIG);
		if ((phy_data & MALIBU_PHY4_PREFER_FIBER)
		    && (!(phy_data & MALIBU_PHY4_FIBER_MODE_1000BX))) {
			return A_TRUE;
		}
	}

	return A_FALSE;
}

/******************************************************************************
*
*  phy4 prfer medium
*
*  get phy4 prefer medum, fiber or copper;
*/
static malibu_phy_medium_t __phy_prefer_medium_get(a_uint32_t dev_id,
						   a_uint32_t phy_id)
{
	a_uint16_t phy_medium = 0;

	phy_medium =
	    malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CHIP_CONFIG);

	return ((phy_medium & MALIBU_PHY4_PREFER_FIBER) ?
		MALIBU_PHY_MEDIUM_FIBER : MALIBU_PHY_MEDIUM_COPPER);
}

/******************************************************************************
*
*  phy4 activer medium
*
*  get phy4 current active medium, fiber or copper;
*/
static malibu_phy_medium_t __phy_active_medium_get(a_uint32_t dev_id,
						   a_uint32_t phy_id)
{
	a_uint16_t phy_data = 0;

	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_SGMII_STATUS);

	if ((phy_data & MALIBU_PHY4_AUTO_COPPER_SELECT)) {
		return MALIBU_PHY_MEDIUM_COPPER;
	} else if ((phy_data & MALIBU_PHY4_AUTO_BX1000_SELECT)) {
		return MALIBU_PHY_MEDIUM_FIBER;	/*PHY_MEDIUM_FIBER_BX1000 */
	} else if ((phy_data & MALIBU_PHY4_AUTO_FX100_SELECT)) {
		return MALIBU_PHY_MEDIUM_FIBER;	/*PHY_MEDIUM_FIBER_FX100 */
	}
	/* link down */
	return __phy_prefer_medium_get(dev_id, phy_id);
}

/******************************************************************************
*
*  phy4 copper page or fiber page select
*
*  set phy4 copper or fiber page
*/

static sw_error_t __phy_reg_pages_sel(a_uint32_t dev_id, a_uint32_t phy_id,
				      malibu_phy_reg_pages_t phy_reg_pages)
{
	a_uint16_t reg_pages = 0;
	reg_pages = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CHIP_CONFIG);

	if (phy_reg_pages == MALIBU_PHY_COPPER_PAGES) {
		reg_pages |= 0x8000;
	} else if (phy_reg_pages == MALIBU_PHY_SGBX_PAGES) {
		reg_pages &= ~0x8000;
	} else
		return SW_BAD_PARAM;

	malibu_phy_reg_write(dev_id, phy_id, MALIBU_PHY_CHIP_CONFIG, reg_pages);

	return SW_OK;
}

/******************************************************************************
*
*  phy4 reg pages selection by active medium
*
*  phy4 reg pages selection
*/
static sw_error_t __phy_reg_pages_sel_by_active_medium(a_uint32_t dev_id,
						       a_uint32_t phy_id)
{
	malibu_phy_medium_t phy_medium;
	malibu_phy_reg_pages_t reg_pages;

	phy_medium = __phy_active_medium_get(dev_id, phy_id);
	if (phy_medium == MALIBU_PHY_MEDIUM_FIBER) {
		reg_pages = MALIBU_PHY_SGBX_PAGES;
	} else if (phy_medium == MALIBU_PHY_MEDIUM_COPPER) {

		reg_pages = MALIBU_PHY_COPPER_PAGES;
	} else

		return SW_BAD_VALUE;

	return __phy_reg_pages_sel(dev_id, phy_id, reg_pages);
}

/******************************************************************************
*
* malibu_phy_debug_write - debug port write
*
* debug port write
*/
sw_error_t
malibu_phy_debug_write(a_uint32_t dev_id, a_uint32_t phy_id, a_uint16_t reg_id,
		       a_uint16_t reg_val)
{
	malibu_phy_reg_write(dev_id, phy_id, MALIBU_DEBUG_PORT_ADDRESS, reg_id);
	malibu_phy_reg_write(dev_id, phy_id, MALIBU_DEBUG_PORT_DATA, reg_val);

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_debug_read - debug port read
*
* debug port read
*/
a_uint16_t
malibu_phy_debug_read(a_uint32_t dev_id, a_uint32_t phy_id, a_uint16_t reg_id)
{
	malibu_phy_reg_write(dev_id, phy_id, MALIBU_DEBUG_PORT_ADDRESS, reg_id);
	return malibu_phy_reg_read(dev_id, phy_id, MALIBU_DEBUG_PORT_DATA);
}

/******************************************************************************
*
* malibu_phy_mmd_write - PHY MMD register write
*
* PHY MMD register write
*/
sw_error_t
malibu_phy_mmd_write(a_uint32_t dev_id, a_uint32_t phy_id,
		     a_uint16_t mmd_num, a_uint16_t reg_id, a_uint16_t reg_val)
{
	malibu_phy_reg_write(dev_id, phy_id, MALIBU_MMD_CTRL_REG, mmd_num);
	malibu_phy_reg_write(dev_id, phy_id, MALIBU_MMD_DATA_REG, reg_id);
	malibu_phy_reg_write(dev_id, phy_id, MALIBU_MMD_CTRL_REG,
			     0x4000 | mmd_num);
	malibu_phy_reg_write(dev_id, phy_id, MALIBU_MMD_DATA_REG, reg_val);

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_mmd_read -  PHY MMD register read
*
* PHY MMD register read
*/
a_uint16_t
malibu_phy_mmd_read(a_uint32_t dev_id, a_uint32_t phy_id,
		    a_uint16_t mmd_num, a_uint16_t reg_id)
{
	malibu_phy_reg_write(dev_id, phy_id, MALIBU_MMD_CTRL_REG, mmd_num);
	malibu_phy_reg_write(dev_id, phy_id, MALIBU_MMD_DATA_REG, reg_id);
	malibu_phy_reg_write(dev_id, phy_id, MALIBU_MMD_CTRL_REG,
			     0x4000 | mmd_num);

	return malibu_phy_reg_read(dev_id, phy_id, MALIBU_MMD_DATA_REG);
}

/******************************************************************************
*
* malibu_phy_set combo medium type
*
* set combo medium fiber or copper
*/
sw_error_t
malibu_phy_set_combo_prefer_medium(a_uint32_t dev_id, a_uint32_t phy_id,
				   fal_port_medium_t phy_medium)
{
	a_uint16_t phy_data;
	if (phy_id != COMBO_PHY_ID)
		return SW_NOT_SUPPORTED;

	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CHIP_CONFIG);

	if (phy_medium == PHY_MEDIUM_FIBER) {
		phy_data |= MALIBU_PHY4_PREFER_FIBER;
	} else if (phy_medium == PHY_MEDIUM_COPPER) {
		phy_data &= ~MALIBU_PHY4_PREFER_FIBER;
	} else {
		return SW_BAD_PARAM;
	}
	malibu_phy_reg_write(dev_id, phy_id, MALIBU_PHY_CHIP_CONFIG, phy_data);

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_get combo medium type
*
* get combo medium fiber or copper
*/
sw_error_t
malibu_phy_get_combo_prefer_medium(a_uint32_t dev_id, a_uint32_t phy_id,
				   fal_port_medium_t * phy_medium)
{
	a_uint16_t phy_data;
	if (phy_id != COMBO_PHY_ID)
		return SW_NOT_SUPPORTED;

	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CHIP_CONFIG);

	*phy_medium =
	    (phy_data & MALIBU_PHY4_PREFER_FIBER) ? PHY_MEDIUM_FIBER :
	    PHY_MEDIUM_COPPER;

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_get current combo medium type copper or fiber
*
* get current combo medium type
*/
sw_error_t
malibu_phy_get_combo_current_medium_type(a_uint32_t dev_id, a_uint32_t phy_id,
					 fal_port_medium_t * phy_medium)
{

	if (phy_id != COMBO_PHY_ID)
		return SW_NOT_SUPPORTED;

	*phy_medium = __phy_active_medium_get(dev_id, phy_id);

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_set fiber mode 1000bx or 100fx
*
* set combo fbier mode
*/
sw_error_t
malibu_phy_set_combo_fiber_mode(a_uint32_t dev_id, a_uint32_t phy_id,
				fal_port_fiber_mode_t fiber_mode)
{
	a_uint16_t phy_data;
	if (phy_id != COMBO_PHY_ID)
		return SW_NOT_SUPPORTED;

	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CHIP_CONFIG);

	if (fiber_mode == PHY_FIBER_1000BX) {
		phy_data |= MALIBU_PHY4_FIBER_MODE_1000BX;
	} else if (fiber_mode == PHY_FIBER_100FX) {
		phy_data &= ~MALIBU_PHY4_FIBER_MODE_1000BX;
	} else {
		return SW_BAD_PARAM;
	}

	malibu_phy_reg_write(dev_id, phy_id, MALIBU_PHY_CHIP_CONFIG, phy_data);

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_get fiber mode 1000bx or 100fx
*
* get combo fbier mode
*/
sw_error_t
malibu_phy_get_combo_fiber_mode(a_uint32_t dev_id, a_uint32_t phy_id,
				fal_port_fiber_mode_t * fiber_mode)
{
	a_uint16_t phy_data;
	if (phy_id != COMBO_PHY_ID)
		return SW_NOT_SUPPORTED;

	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CHIP_CONFIG);

	*fiber_mode =
	    (phy_data & MALIBU_PHY4_FIBER_MODE_1000BX) ? PHY_FIBER_1000BX :
	    PHY_FIBER_100FX;

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_set_mdix - 
*
* set phy mdix configuraiton
*/
sw_error_t
malibu_phy_set_mdix(a_uint32_t dev_id, a_uint32_t phy_id,
		    fal_port_mdix_mode_t mode)
{
	a_uint16_t phy_data;

	if ((phy_id == COMBO_PHY_ID)
	    && (MALIBU_PHY_MEDIUM_COPPER !=
		__phy_active_medium_get(dev_id, phy_id)))

		return SW_NOT_SUPPORTED;

	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_SPEC_CONTROL);

	if (mode == PHY_MDIX_AUTO) {
		phy_data |= MALIBU_PHY_MDIX_AUTO;
	} else if (mode == PHY_MDIX_MDIX) {
		phy_data |= MALIBU_PHY_MDIX;
	} else if (mode == PHY_MDIX_MDI) {
		phy_data &= ~MALIBU_PHY_MDIX_AUTO;
	} else {
		return SW_BAD_PARAM;
	}

	malibu_phy_reg_write(dev_id, phy_id, MALIBU_PHY_SPEC_CONTROL, phy_data);

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_get_mdix 
*
* get phy mdix configuration
*/
sw_error_t
malibu_phy_get_mdix(a_uint32_t dev_id, a_uint32_t phy_id,
		    fal_port_mdix_mode_t * mode)
{
	a_uint16_t phy_data;

	if ((phy_id == COMBO_PHY_ID)
	    && (MALIBU_PHY_MEDIUM_COPPER !=
		__phy_active_medium_get(dev_id, phy_id)))

		return SW_NOT_SUPPORTED;

	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_SPEC_CONTROL);

	if ((phy_data & MALIBU_PHY_MDIX_AUTO) == MALIBU_PHY_MDIX_AUTO) {
		*mode = PHY_MDIX_AUTO;
	} else if ((phy_data & MALIBU_PHY_MDIX) == MALIBU_PHY_MDIX) {
		*mode = PHY_MDIX_MDIX;
	} else {
		*mode = PHY_MDIX_MDI;
	}

	return SW_OK;

}

/******************************************************************************
*
* malibu_phy_get_mdix status
*
* get phy mdix status
*/
sw_error_t
malibu_phy_get_mdix_status(a_uint32_t dev_id, a_uint32_t phy_id,
			   fal_port_mdix_status_t * mode)
{
	a_uint16_t phy_data;

	if (phy_id == COMBO_PHY_ID) {

		if (MALIBU_PHY_MEDIUM_COPPER !=
		    __phy_active_medium_get(dev_id, phy_id))
			return SW_NOT_SUPPORTED;

		__phy_reg_pages_sel(dev_id, phy_id, MALIBU_PHY_COPPER_PAGES);
	}

	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_SPEC_STATUS);

	*mode =
	    (phy_data & MALIBU_PHY_MDIX_STATUS) ? PHY_MDIX_STATUS_MDIX :
	    PHY_MDIX_STATUS_MDI;

	return SW_OK;

}
/******************************************************************************
*
* malibu_phy_reset_done - reset the phy
*
* reset the phy
*/
a_bool_t malibu_phy_reset_done(a_uint32_t dev_id, a_uint32_t phy_id)
{
	a_uint16_t phy_data;
	a_uint16_t ii = 200;

	if (phy_id == COMBO_PHY_ID)
		__phy_reg_pages_sel_by_active_medium(dev_id, phy_id);

	do {
		phy_data =
		    malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CONTROL);
		aos_mdelay(10);
	}
	while ((!MALIBU_RESET_DONE(phy_data)) && --ii);

	if (ii == 0)
		return A_FALSE;

	return A_TRUE;
}

/******************************************************************************
*
* malibu_autoneg_done
*
* malibu_autoneg_done
*/
a_bool_t malibu_autoneg_done(a_uint32_t dev_id, a_uint32_t phy_id)
{
	a_uint16_t phy_data;
	a_uint16_t ii = 200;

	if (phy_id == COMBO_PHY_ID)
		__phy_reg_pages_sel_by_active_medium(dev_id, phy_id);

	do {
		phy_data =
		    malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_STATUS);
		aos_mdelay(10);
	}
	while ((!MALIBU_AUTONEG_DONE(phy_data)) && --ii);

	if (ii == 0)
		return A_FALSE;

	return A_TRUE;
}

/******************************************************************************
*
* malibu_phy_Speed_Duplex_Resolved
 - reset the phy
*
* reset the phy
*/
a_bool_t malibu_phy_speed_duplex_resolved(a_uint32_t dev_id, a_uint32_t phy_id)
{
	a_uint16_t phy_data;
	a_uint16_t ii = 200;

	if (phy_id == COMBO_PHY_ID)
		__phy_reg_pages_sel_by_active_medium(dev_id, phy_id);

	do {
		phy_data =
		    malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_SPEC_STATUS);
		aos_mdelay(10);
	}
	while ((!MALIBU_SPEED_DUPLEX_RESOVLED(phy_data)) && --ii);

	if (ii == 0)
		return A_FALSE;

	return A_TRUE;
}

/******************************************************************************
*
* malibu_phy_reset - reset the phy
*
* reset the phy
*/
sw_error_t malibu_phy_reset(a_uint32_t dev_id, a_uint32_t phy_id)
{
	a_uint16_t phy_data;

	if (phy_id == COMBO_PHY_ID)
		__phy_reg_pages_sel_by_active_medium(dev_id, phy_id);

	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CONTROL);
	malibu_phy_reg_write(dev_id, phy_id, MALIBU_PHY_CONTROL,
			     phy_data | MALIBU_CTRL_SOFTWARE_RESET);

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_off - power off the phy 
*
* Power off the phy
*/
sw_error_t malibu_phy_poweroff(a_uint32_t dev_id, a_uint32_t phy_id)
{
	a_uint16_t phy_data;
	if (phy_id == COMBO_PHY_ID) {
		__phy_reg_pages_sel(dev_id, phy_id, MALIBU_PHY_SGBX_PAGES);
		phy_data =
		    malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CONTROL);
		malibu_phy_reg_write(dev_id, phy_id, MALIBU_PHY_CONTROL,
				     phy_data | MALIBU_CTRL_POWER_DOWN);

		__phy_reg_pages_sel(dev_id, phy_id, MALIBU_PHY_COPPER_PAGES);
		phy_data =
		    malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CONTROL);
		malibu_phy_reg_write(dev_id, phy_id, MALIBU_PHY_CONTROL,
				     phy_data | MALIBU_CTRL_POWER_DOWN);
	} else {
		phy_data =
		    malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CONTROL);
		malibu_phy_reg_write(dev_id, phy_id, MALIBU_PHY_CONTROL,
				     phy_data | MALIBU_CTRL_POWER_DOWN);
	}
	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_on - power on the phy 
*
* Power on the phy
*/
sw_error_t malibu_phy_poweron(a_uint32_t dev_id, a_uint32_t phy_id)
{
	a_uint16_t phy_data;
	if (phy_id == COMBO_PHY_ID) {
		__phy_reg_pages_sel(dev_id, phy_id, MALIBU_PHY_SGBX_PAGES);
		phy_data =
		    malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CONTROL);
		malibu_phy_reg_write(dev_id, phy_id, MALIBU_PHY_CONTROL,
				     phy_data & ~MALIBU_CTRL_POWER_DOWN);

		__phy_reg_pages_sel(dev_id, phy_id, MALIBU_PHY_COPPER_PAGES);
		phy_data =
		    malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CONTROL);
		malibu_phy_reg_write(dev_id, phy_id, MALIBU_PHY_CONTROL,
				     phy_data & ~MALIBU_CTRL_POWER_DOWN);

	} else {
		phy_data =
		    malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CONTROL);
		malibu_phy_reg_write(dev_id, phy_id, MALIBU_PHY_CONTROL,
				     phy_data & ~MALIBU_CTRL_POWER_DOWN);
	}

	aos_mdelay(200);

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_get_ability - get the phy ability
*
*
*/
sw_error_t
malibu_phy_get_ability(a_uint32_t dev_id, a_uint32_t phy_id,
		       a_uint32_t * ability)
{
	a_uint16_t phy_data;

	*ability = 0;
	if (phy_id == COMBO_PHY_ID)
		__phy_reg_pages_sel_by_active_medium(dev_id, phy_id);

	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_STATUS);

	if (phy_data & MALIBU_STATUS_AUTONEG_CAPS)
		*ability |= FAL_PHY_AUTONEG_CAPS;

	if (phy_data & MALIBU_STATUS_100T2_HD_CAPS)
		*ability |= FAL_PHY_100T2_HD_CAPS;

	if (phy_data & MALIBU_STATUS_100T2_FD_CAPS)
		*ability |= FAL_PHY_100T2_FD_CAPS;

	if (phy_data & MALIBU_STATUS_10T_HD_CAPS)
		*ability |= FAL_PHY_10T_HD_CAPS;

	if (phy_data & MALIBU_STATUS_10T_FD_CAPS)
		*ability |= FAL_PHY_10T_FD_CAPS;

	if (phy_data & MALIBU_STATUS_100X_HD_CAPS)
		*ability |= FAL_PHY_100X_HD_CAPS;

	if (phy_data & MALIBU_STATUS_100X_FD_CAPS)
		*ability |= FAL_PHY_100X_FD_CAPS;

	if (phy_data & MALIBU_STATUS_100T4_CAPS)
		*ability |= FAL_PHY_100T4_CAPS;

	if (phy_data & MALIBU_STATUS_EXTENDED_STATUS) {
		phy_data =
		    malibu_phy_reg_read(dev_id, phy_id, MALIBU_EXTENDED_STATUS);

		if (phy_data & MALIBU_STATUS_1000T_FD_CAPS) {
			*ability |= FAL_PHY_1000T_FD_CAPS;
		}

		if (phy_data & MALIBU_STATUS_1000X_FD_CAPS) {
			*ability |= FAL_PHY_1000X_FD_CAPS;
		}
	}

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_get_ability - get the phy ability
*
*
*/
sw_error_t
malibu_phy_get_partner_ability(a_uint32_t dev_id, a_uint32_t phy_id,
			       a_uint32_t * ability)
{
	a_uint16_t phy_data;

	*ability = 0;

	if (phy_id == COMBO_PHY_ID)
		__phy_reg_pages_sel_by_active_medium(dev_id, phy_id);

	phy_data =
	    malibu_phy_reg_read(dev_id, phy_id, MALIBU_LINK_PARTNER_ABILITY);

	if (phy_data & MALIBU_LINK_10BASETX_HALF_DUPLEX)
		*ability |= FAL_PHY_PART_10T_HD;

	if (phy_data & MALIBU_LINK_10BASETX_FULL_DUPLEX)
		*ability |= FAL_PHY_PART_10T_FD;

	if (phy_data & MALIBU_LINK_100BASETX_HALF_DUPLEX)
		*ability |= FAL_PHY_PART_100TX_HD;

	if (phy_data & MALIBU_LINK_100BASETX_FULL_DUPLEX)
		*ability |= FAL_PHY_PART_100TX_FD;

	if (phy_data & MALIBU_LINK_NPAGE) {
		phy_data =
		    malibu_phy_reg_read(dev_id, phy_id,
					MALIBU_1000BASET_STATUS);

		if (phy_data & MALIBU_LINK_1000BASETX_FULL_DUPLEX)
			*ability |= FAL_PHY_PART_1000T_FD;
	}

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_status - test to see if the specified phy link is alive
*
* RETURNS:
*    A_TRUE  --> link is alive
*    A_FALSE --> link is down
*/
a_bool_t malibu_phy_get_link_status(a_uint32_t dev_id, a_uint32_t phy_id)
{
	a_uint16_t phy_data;
	if (phy_id == COMBO_PHY_ID)
		__phy_reg_pages_sel_by_active_medium(dev_id, phy_id);
	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_SPEC_STATUS);

	if (phy_data & MALIBU_STATUS_LINK_PASS)
		return A_TRUE;

	return A_FALSE;
}

/******************************************************************************
*
* malibu_set_autoneg_adv - set the phy autoneg Advertisement
*
*/
sw_error_t
malibu_phy_set_autoneg_adv(a_uint32_t dev_id, a_uint32_t phy_id,
			   a_uint32_t autoneg)
{
	a_uint16_t phy_data = 0;
	if (phy_id == COMBO_PHY_ID) {
		if (__medium_is_fiber_100fx(dev_id, phy_id))
			return SW_NOT_SUPPORTED;

		if (MALIBU_PHY_MEDIUM_COPPER ==
		    __phy_active_medium_get(dev_id, phy_id)) {

			__phy_reg_pages_sel(dev_id, phy_id,
					    MALIBU_PHY_COPPER_PAGES);
			phy_data =
			    malibu_phy_reg_read(dev_id, phy_id,
						MALIBU_AUTONEG_ADVERT);
			phy_data &= ~MALIBU_ADVERTISE_MEGA_ALL;
			phy_data &=
			    ~(MALIBU_ADVERTISE_PAUSE |
			      MALIBU_ADVERTISE_ASYM_PAUSE);

			if (autoneg & FAL_PHY_ADV_100TX_FD)
				phy_data |= MALIBU_ADVERTISE_100FULL;

			if (autoneg & FAL_PHY_ADV_100TX_HD)
				phy_data |= MALIBU_ADVERTISE_100HALF;

			if (autoneg & FAL_PHY_ADV_10T_FD)
				phy_data |= MALIBU_ADVERTISE_10FULL;

			if (autoneg & FAL_PHY_ADV_10T_HD)
				phy_data |= MALIBU_ADVERTISE_10HALF;

			if (autoneg & FAL_PHY_ADV_PAUSE)
				phy_data |= MALIBU_ADVERTISE_PAUSE;

			if (autoneg & FAL_PHY_ADV_ASY_PAUSE)
				phy_data |= MALIBU_ADVERTISE_ASYM_PAUSE;
			__phy_reg_pages_sel(dev_id, phy_id,
					    MALIBU_PHY_COPPER_PAGES);
			malibu_phy_reg_write(dev_id, phy_id,
					     MALIBU_AUTONEG_ADVERT, phy_data);

			phy_data =
			    malibu_phy_reg_read(dev_id, phy_id,
						MALIBU_1000BASET_CONTROL);
			phy_data &= ~MALIBU_ADVERTISE_1000FULL;
			phy_data &= ~MALIBU_ADVERTISE_1000HALF;

			if (autoneg & FAL_PHY_ADV_1000T_FD)
				phy_data |= MALIBU_ADVERTISE_1000FULL;

			malibu_phy_reg_write(dev_id, phy_id,
					     MALIBU_1000BASET_CONTROL,
					     phy_data);
		} else {

			__phy_reg_pages_sel(dev_id, phy_id,
					    MALIBU_PHY_SGBX_PAGES);
			phy_data =
			    malibu_phy_reg_read(dev_id, phy_id,
						MALIBU_AUTONEG_ADVERT);
			phy_data &= ~MALIBU_BX_ADVERTISE_ALL;

			if (autoneg & FAL_PHY_ADV_1000BX_FD)
				phy_data |= MALIBU_BX_ADVERTISE_1000FULL;

			if (autoneg & FAL_PHY_ADV_PAUSE)
				phy_data |= MALIBU_BX_ADVERTISE_PAUSE;

			if (autoneg & FAL_PHY_ADV_ASY_PAUSE)
				phy_data |= MALIBU_BX_ADVERTISE_ASYM_PAUSE;

			__phy_reg_pages_sel(dev_id, phy_id,
					    MALIBU_PHY_SGBX_PAGES);

			malibu_phy_reg_write(dev_id, phy_id,
					     MALIBU_AUTONEG_ADVERT, phy_data);
		}
	} else {

		phy_data =
		    malibu_phy_reg_read(dev_id, phy_id, MALIBU_AUTONEG_ADVERT);
		phy_data &= ~MALIBU_ADVERTISE_MEGA_ALL;
		phy_data &=
		    ~(MALIBU_ADVERTISE_PAUSE | MALIBU_ADVERTISE_ASYM_PAUSE);

		if (autoneg & FAL_PHY_ADV_100TX_FD)
			phy_data |= MALIBU_ADVERTISE_100FULL;

		if (autoneg & FAL_PHY_ADV_100TX_HD)
			phy_data |= MALIBU_ADVERTISE_100HALF;

		if (autoneg & FAL_PHY_ADV_10T_FD)
			phy_data |= MALIBU_ADVERTISE_10FULL;

		if (autoneg & FAL_PHY_ADV_10T_HD)
			phy_data |= MALIBU_ADVERTISE_10HALF;

		if (autoneg & FAL_PHY_ADV_PAUSE)
			phy_data |= MALIBU_ADVERTISE_PAUSE;

		if (autoneg & FAL_PHY_ADV_ASY_PAUSE)
			phy_data |= MALIBU_ADVERTISE_ASYM_PAUSE;
		malibu_phy_reg_write(dev_id, phy_id, MALIBU_AUTONEG_ADVERT,
				     phy_data);

		phy_data =
		    malibu_phy_reg_read(dev_id, phy_id,
					MALIBU_1000BASET_CONTROL);
		phy_data &= ~MALIBU_ADVERTISE_1000FULL;
		phy_data &= ~MALIBU_ADVERTISE_1000HALF;

		if (autoneg & FAL_PHY_ADV_1000T_FD)
			phy_data |= MALIBU_ADVERTISE_1000FULL;

		malibu_phy_reg_write(dev_id, phy_id, MALIBU_1000BASET_CONTROL,
				     phy_data);

	}

	return SW_OK;
}

/******************************************************************************
*
* malibu_get_autoneg_adv - get the phy autoneg Advertisement
*
*/
sw_error_t
malibu_phy_get_autoneg_adv(a_uint32_t dev_id, a_uint32_t phy_id,
			   a_uint32_t * autoneg)
{
	a_uint16_t phy_data = 0;

	*autoneg = 0;

	if (phy_id == COMBO_PHY_ID) {
		if (__medium_is_fiber_100fx(dev_id, phy_id))
			return SW_NOT_SUPPORTED;

		if (MALIBU_PHY_MEDIUM_COPPER ==
		    __phy_active_medium_get(dev_id, phy_id)) {

			__phy_reg_pages_sel(dev_id, phy_id,
					    MALIBU_PHY_COPPER_PAGES);

			phy_data =
			    malibu_phy_reg_read(dev_id, phy_id,
						MALIBU_AUTONEG_ADVERT);

			if (phy_data & MALIBU_ADVERTISE_100FULL)
				*autoneg |= FAL_PHY_ADV_100TX_FD;

			if (phy_data & MALIBU_ADVERTISE_100HALF)
				*autoneg |= FAL_PHY_ADV_100TX_HD;

			if (phy_data & MALIBU_ADVERTISE_10FULL)
				*autoneg |= FAL_PHY_ADV_10T_FD;

			if (phy_data & MALIBU_ADVERTISE_10HALF)
				*autoneg |= FAL_PHY_ADV_10T_HD;

			if (phy_data & MALIBU_ADVERTISE_PAUSE)
				*autoneg |= FAL_PHY_ADV_PAUSE;

			if (phy_data & MALIBU_ADVERTISE_ASYM_PAUSE)
				*autoneg |= FAL_PHY_ADV_ASY_PAUSE;

			phy_data =
			    malibu_phy_reg_read(dev_id, phy_id,
						MALIBU_1000BASET_CONTROL);

			if (phy_data & MALIBU_ADVERTISE_1000FULL)
				*autoneg |= FAL_PHY_ADV_1000T_FD;

		} else {

			__phy_reg_pages_sel(dev_id, phy_id,
					    MALIBU_PHY_SGBX_PAGES);
			phy_data =
			    malibu_phy_reg_read(dev_id, phy_id,
						MALIBU_AUTONEG_ADVERT);

			if (phy_data & MALIBU_BX_ADVERTISE_PAUSE)
				*autoneg |= FAL_PHY_ADV_PAUSE;

			if (phy_data & MALIBU_BX_ADVERTISE_ASYM_PAUSE)
				*autoneg |= FAL_PHY_ADV_ASY_PAUSE;

			if (phy_data & MALIBU_BX_ADVERTISE_1000FULL)
				*autoneg |= FAL_PHY_ADV_1000BX_FD;
		}
	} else {

		phy_data =
		    malibu_phy_reg_read(dev_id, phy_id, MALIBU_AUTONEG_ADVERT);

		if (phy_data & MALIBU_ADVERTISE_100FULL)
			*autoneg |= FAL_PHY_ADV_100TX_FD;

		if (phy_data & MALIBU_ADVERTISE_100HALF)
			*autoneg |= FAL_PHY_ADV_100TX_HD;

		if (phy_data & MALIBU_ADVERTISE_10FULL)
			*autoneg |= FAL_PHY_ADV_10T_FD;

		if (phy_data & MALIBU_ADVERTISE_10HALF)
			*autoneg |= FAL_PHY_ADV_10T_HD;

		if (phy_data & MALIBU_ADVERTISE_PAUSE)
			*autoneg |= FAL_PHY_ADV_PAUSE;

		if (phy_data & MALIBU_ADVERTISE_ASYM_PAUSE)
			*autoneg |= FAL_PHY_ADV_ASY_PAUSE;

		phy_data =
		    malibu_phy_reg_read(dev_id, phy_id,
					MALIBU_1000BASET_CONTROL);
		if (phy_data & MALIBU_ADVERTISE_1000FULL)
			*autoneg |= FAL_PHY_ADV_1000T_FD;
	}

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_enable_autonego 
*
* Power off the phy
*/
a_bool_t malibu_phy_autoneg_status(a_uint32_t dev_id, a_uint32_t phy_id)
{
	a_uint16_t phy_data;

	if (phy_id == COMBO_PHY_ID) {

		if (__medium_is_fiber_100fx(dev_id, phy_id))
			return SW_NOT_SUPPORTED;
		__phy_reg_pages_sel_by_active_medium(dev_id, phy_id);
	}

	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CONTROL);

	if (phy_data & MALIBU_CTRL_AUTONEGOTIATION_ENABLE)
		return A_TRUE;

	return A_FALSE;
}

/******************************************************************************
*
* malibu_restart_autoneg - restart the phy autoneg
*
*/
sw_error_t malibu_phy_restart_autoneg(a_uint32_t dev_id, a_uint32_t phy_id)
{
	a_uint16_t phy_data = 0;

	if (phy_id == COMBO_PHY_ID) {
		if (__medium_is_fiber_100fx(dev_id, phy_id))
			return SW_NOT_SUPPORTED;
		__phy_reg_pages_sel_by_active_medium(dev_id, phy_id);
	}
	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CONTROL);

	phy_data |= MALIBU_CTRL_AUTONEGOTIATION_ENABLE;
	malibu_phy_reg_write(dev_id, phy_id, MALIBU_PHY_CONTROL,
			     phy_data | MALIBU_CTRL_RESTART_AUTONEGOTIATION);

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_enable_autonego 
*
*/
sw_error_t malibu_phy_enable_autoneg(a_uint32_t dev_id, a_uint32_t phy_id)
{
	a_uint16_t phy_data = 0;

	if (phy_id == COMBO_PHY_ID) {
		if (__medium_is_fiber_100fx(dev_id, phy_id))
			return SW_NOT_SUPPORTED;
		__phy_reg_pages_sel_by_active_medium(dev_id, phy_id);
	}
	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_CONTROL);

	malibu_phy_reg_write(dev_id, phy_id, MALIBU_PHY_CONTROL,
			     phy_data | MALIBU_CTRL_AUTONEGOTIATION_ENABLE);

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_get_speed - Determines the speed of phy ports associated with the
* specified device.
*/

sw_error_t
malibu_phy_get_speed(a_uint32_t dev_id, a_uint32_t phy_id,
		     fal_port_speed_t * speed)
{
	a_uint16_t phy_data;

	if (phy_id == COMBO_PHY_ID) {
		__phy_reg_pages_sel_by_active_medium(dev_id, phy_id);
	}
	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_SPEC_STATUS);

	switch (phy_data & MALIBU_STATUS_SPEED_MASK) {
	case MALIBU_STATUS_SPEED_1000MBS:
		*speed = FAL_SPEED_1000;
		break;
	case MALIBU_STATUS_SPEED_100MBS:
		*speed = FAL_SPEED_100;
		break;
	case MALIBU_STATUS_SPEED_10MBS:
		*speed = FAL_SPEED_10;
		break;
	default:
		return SW_READ_ERROR;
	}

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_set_speed - Determines the speed of phy ports associated with the
* specified device.
*/
sw_error_t
malibu_phy_set_speed(a_uint32_t dev_id, a_uint32_t phy_id,
		     fal_port_speed_t speed)
{
	a_uint16_t phy_data = 0;
	a_uint16_t phy_status = 0;

	a_uint32_t autoneg, oldneg;
	fal_port_duplex_t old_duplex;

	if (phy_id == COMBO_PHY_ID) {
		if (MALIBU_PHY_MEDIUM_COPPER !=
		    __phy_active_medium_get(dev_id, phy_id))

			return SW_NOT_SUPPORTED;

		__phy_reg_pages_sel(dev_id, phy_id, MALIBU_PHY_COPPER_PAGES);
	}

	if (FAL_SPEED_1000 == speed) {
		phy_data |= MALIBU_CTRL_SPEED_1000;
	} else if (FAL_SPEED_100 == speed) {
		phy_data |= MALIBU_CTRL_SPEED_100;
	} else if (FAL_SPEED_10 == speed) {
		phy_data |= MALIBU_CTRL_SPEED_10;
	} else {
		return SW_BAD_PARAM;
	}

	phy_data &= ~MALIBU_CTRL_AUTONEGOTIATION_ENABLE;

	(void)malibu_phy_get_autoneg_adv(dev_id, phy_id, &autoneg);
	oldneg = autoneg;
	autoneg &= ~FAL_PHY_ADV_GE_SPEED_ALL;

	(void)malibu_phy_get_duplex(dev_id, phy_id, &old_duplex);

	if (old_duplex == FAL_FULL_DUPLEX) {
		phy_data |= MALIBU_CTRL_FULL_DUPLEX;

		if (FAL_SPEED_1000 == speed) {
			autoneg |= FAL_PHY_ADV_1000T_FD;
		} else if (FAL_SPEED_100 == speed) {
			autoneg |= FAL_PHY_ADV_100TX_FD;
		} else {
			autoneg |= FAL_PHY_ADV_10T_FD;
		}
	} else if (old_duplex == FAL_HALF_DUPLEX) {
		phy_data &= ~MALIBU_CTRL_FULL_DUPLEX;

		if (FAL_SPEED_100 == speed) {
			autoneg |= FAL_PHY_ADV_100TX_HD;
		} else {
			autoneg |= FAL_PHY_ADV_10T_HD;
		}
	} else {
		return SW_FAIL;
	}

	(void)malibu_phy_set_autoneg_adv(dev_id, phy_id, autoneg);
	(void)malibu_phy_restart_autoneg(dev_id, phy_id);
	if (malibu_phy_get_link_status(dev_id, phy_id)) {
		do {
			phy_status =
			    malibu_phy_reg_read(dev_id, phy_id,
						MALIBU_PHY_STATUS);
		}
		while (!MALIBU_AUTONEG_DONE(phy_status));
	}

	malibu_phy_reg_write(dev_id, phy_id, MALIBU_PHY_CONTROL, phy_data);
	(void)malibu_phy_set_autoneg_adv(dev_id, phy_id, oldneg);

	return SW_OK;

}

/******************************************************************************
*
* malibu_phy_get_duplex - Determines the speed of phy ports associated with the
* specified device.
*/
sw_error_t
malibu_phy_get_duplex(a_uint32_t dev_id, a_uint32_t phy_id,
		      fal_port_duplex_t * duplex)
{
	a_uint16_t phy_data;

	if (phy_id == COMBO_PHY_ID) {
		__phy_reg_pages_sel_by_active_medium(dev_id, phy_id);
	}

	phy_data = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_SPEC_STATUS);

	//read duplex
	if (phy_data & MALIBU_STATUS_FULL_DUPLEX)
		*duplex = FAL_FULL_DUPLEX;
	else
		*duplex = FAL_HALF_DUPLEX;

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_set_duplex - Determines the speed of phy ports associated with the
* specified device.
*/
sw_error_t
malibu_phy_set_duplex(a_uint32_t dev_id, a_uint32_t phy_id,
		      fal_port_duplex_t duplex)
{
	a_uint16_t phy_data = 0;
	a_uint16_t phy_status = 0;
	fal_port_speed_t old_speed;
	a_uint32_t oldneg, autoneg;

	if (phy_id == COMBO_PHY_ID) {

		if (MALIBU_PHY_MEDIUM_COPPER !=
		    __phy_active_medium_get(dev_id, phy_id)) {

			__phy_reg_pages_sel(dev_id, phy_id,
					    MALIBU_PHY_SGBX_PAGES);

			phy_data =
			    malibu_phy_reg_read(dev_id, phy_id,
						MALIBU_PHY_CONTROL);

			if (__medium_is_fiber_100fx(dev_id, phy_id)) {

				if (duplex == FAL_FULL_DUPLEX) {
					phy_data |= MALIBU_CTRL_FULL_DUPLEX;
				} else if (duplex == FAL_HALF_DUPLEX) {
					phy_data &= ~MALIBU_CTRL_FULL_DUPLEX;
				} else {
					return SW_BAD_PARAM;
				}
				malibu_phy_reg_write(dev_id, phy_id,
						     MALIBU_PHY_CONTROL,
						     phy_data);

				return SW_OK;
			}

			if (A_TRUE == malibu_phy_autoneg_status(dev_id, phy_id))
				phy_data &= ~MALIBU_CTRL_AUTONEGOTIATION_ENABLE;

			(void)malibu_phy_get_autoneg_adv(dev_id, phy_id,
							 &autoneg);
			oldneg = autoneg;
			autoneg &= ~FAL_PHY_ADV_BX_SPEED_ALL;
			(void)malibu_phy_get_speed(dev_id, phy_id, &old_speed);

			if (FAL_SPEED_1000 == old_speed) {
				phy_data |= MALIBU_CTRL_SPEED_1000;
			} else {
				return SW_FAIL;
			}

			if (duplex == FAL_FULL_DUPLEX) {
				phy_data |= MALIBU_CTRL_FULL_DUPLEX;

				if (FAL_SPEED_1000 == old_speed) {
					autoneg = FAL_PHY_ADV_1000BX_FD;
				}
			} else if (duplex == FAL_HALF_DUPLEX) {
				phy_data &= ~MALIBU_CTRL_FULL_DUPLEX;

				if (FAL_SPEED_1000 == old_speed) {
					autoneg = FAL_PHY_ADV_1000BX_HD;
				}
			} else {
				return SW_BAD_PARAM;
			}

			(void)malibu_phy_set_autoneg_adv(dev_id, phy_id,
							 autoneg);
			(void)malibu_phy_restart_autoneg(dev_id, phy_id);
			if (malibu_phy_get_link_status(dev_id, phy_id)) {
				do {
					phy_status =
					    malibu_phy_reg_read(dev_id, phy_id,
								MALIBU_PHY_STATUS);
				}
				while (!MALIBU_AUTONEG_DONE(phy_status));
			}

			malibu_phy_reg_write(dev_id, phy_id, MALIBU_PHY_CONTROL,
					     phy_data);
			(void)malibu_phy_set_autoneg_adv(dev_id, phy_id,
							 oldneg);

			return SW_OK;

		} else {
			__phy_reg_pages_sel(dev_id, phy_id,
					    MALIBU_PHY_COPPER_PAGES);
		}
	}

	if (A_TRUE == malibu_phy_autoneg_status(dev_id, phy_id))
		phy_data &= ~MALIBU_CTRL_AUTONEGOTIATION_ENABLE;

	(void)malibu_phy_get_autoneg_adv(dev_id, phy_id, &autoneg);
	oldneg = autoneg;
	autoneg &= ~FAL_PHY_ADV_GE_SPEED_ALL;
	(void)malibu_phy_get_speed(dev_id, phy_id, &old_speed);

	if (FAL_SPEED_1000 == old_speed) {
		phy_data |= MALIBU_CTRL_SPEED_1000;
	} else if (FAL_SPEED_100 == old_speed) {
		phy_data |= MALIBU_CTRL_SPEED_100;
	} else if (FAL_SPEED_10 == old_speed) {
		phy_data |= MALIBU_CTRL_SPEED_10;
	} else {
		return SW_FAIL;
	}

	if (duplex == FAL_FULL_DUPLEX) {
		phy_data |= MALIBU_CTRL_FULL_DUPLEX;

		if (FAL_SPEED_1000 == old_speed) {
			autoneg = FAL_PHY_ADV_1000T_FD;
		} else if (FAL_SPEED_100 == old_speed) {
			autoneg = FAL_PHY_ADV_100TX_FD;
		} else {
			autoneg = FAL_PHY_ADV_10T_FD;
		}
	} else if (duplex == FAL_HALF_DUPLEX) {
		phy_data &= ~MALIBU_CTRL_FULL_DUPLEX;

		if (FAL_SPEED_100 == old_speed) {
			autoneg = FAL_PHY_ADV_100TX_HD;
		} else {
			autoneg = FAL_PHY_ADV_10T_HD;
		}
	} else {
		return SW_BAD_PARAM;
	}

	(void)malibu_phy_set_autoneg_adv(dev_id, phy_id, autoneg);
	(void)malibu_phy_restart_autoneg(dev_id, phy_id);
	if (malibu_phy_get_link_status(dev_id, phy_id)) {
		do {
			phy_status =
			    malibu_phy_reg_read(dev_id, phy_id,
						MALIBU_PHY_STATUS);
		}
		while (!MALIBU_AUTONEG_DONE(phy_status));
	}

	malibu_phy_reg_write(dev_id, phy_id, MALIBU_PHY_CONTROL, phy_data);
	(void)malibu_phy_set_autoneg_adv(dev_id, phy_id, oldneg);

	return SW_OK;
}

/******************************************************************************
*
* malibu_phy_get_phy_id - get the phy id
*
*/
sw_error_t
malibu_phy_get_phy_id(a_uint32_t dev_id, a_uint32_t phy_id,
		      a_uint16_t * org_id, a_uint16_t * rev_id)
{
	*org_id = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_ID1);
	*rev_id = malibu_phy_reg_read(dev_id, phy_id, MALIBU_PHY_ID2);

	return SW_OK;
}
static int malibu_phy_probe(struct phy_device *pdev)
{
	int ret;
	hsl_phy_ops_t malibu_phy_api_ops = { 0 };

#if 0
	malibu_phy_api_ops.phy_powersave_set = malibu_phy_set_powersave;
	malibu_phy_api_ops.phy_powersave_get = malibu_phy_get_powersave;
	malibu_phy_api_ops.phy_hibernation_set = malibu_phy_set_hibernate;
	malibu_phy_api_ops.phy_hibernation_get = malibu_phy_get_hibernate;
	malibu_phy_api_ops.phy_cdt = malibu_phy_cdt;
#endif

	ret = hsl_phy_api_ops_register(&malibu_phy_api_ops);
	return ret;
}

static void malibu_phy_remove(struct phy_device *pdev)
{

	int ret;
	hsl_phy_ops_t malibu_phy_api_ops;

	ret = hsl_phy_api_ops_unregister(&malibu_phy_api_ops);
	return ret;
}

/******************************************************************************
*
* malibu_phy_init -
*
*/
a_bool_t malibu_phy_init(void)
{

	static struct phy_driver qca_malibu_phy_driver = {
		.name = "QCA Malibu",
		.phy_id = 0x004DD0B0,
		.phy_id_mask = 0xffffffff,
		.probe = malibu_phy_probe,
		.remove = malibu_phy_remove,
		.features = PHY_BASIC_FEATURES,
		.driver = {.owner = THIS_MODULE},
	};

	return phy_driver_register(&qca_malibu_phy_driver);
}

int malibu_phy_test(void)
{

	phy_api_ops_init(0);
	return malibu_phy_probe((struct phy_device *)NULL);
}