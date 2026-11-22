// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "driver/i2c_master.h"
/* The one device handle of the AXP2101 (made on first use on the I2C0 bus), shared by bsp_keys.c (IRQ) and bsp_pmu.c (battery reads). NULL without the bus. */
i2c_master_dev_handle_t bsp_pmu_dev(void);
