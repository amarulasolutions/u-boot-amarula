// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2016 Rockchip Electronics Co., Ltd
 */

#include <common.h>
#include <dm.h>
#include <log.h>
#include <spl_gpio.h>
#include <power/regulator.h>

#include <asm/arch-rockchip/gpio.h>

#define GPIO0_BASE		0xff720000

static int led_setup(void)
{
	struct rockchip_gpio_regs * const gpio0 = (void *)GPIO0_BASE;

	/* Turn on red LED, indicating full power mode */
	spl_gpio_output(gpio0, GPIO(BANK_B, 5), 1);

	return 0;
}

static int roc_pc_early_init_f(void)
{
	struct udevice *regulator;
	int ret;

	ret = regulator_get_by_platname("vcc5v0_host", &regulator);
	if (ret) {
		debug("%s vcc5v0_host init fail! ret %d\n", __func__, ret);
		goto out;
	}

	ret = regulator_set_enable(regulator, true);
	if (ret)
		debug("%s vcc5v0-host-en set fail! ret %d\n", __func__, ret);
out:
	return 0;
}

int board_early_init_f(void)
{
	if (IS_ENABLED(CONFIG_SPL_BUILD))
		return led_setup();
	else
		return roc_pc_early_init_f();
}
