// SPDX-License-Identifier: GPL-2.0+
/*
 * RTC driver for Rockchip RK808 PMIC.
 *
 * Copyright (C) 2020 Amarula Solutions(India).
 * Suniel Mahesh <sunil@amarulasolutions.com>
 *
 * Based on code from Linux kernel:
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Chris Zhong <zyw@rock-chips.com>
 * Author: Zhang Qing <zhangqing@rock-chips.com>
 *
 * Date & Time support (no alarms and interrupts)
 */

#include <command.h>
#include <common.h>
#include <dm.h>
#include <i2c.h>
#include <rtc.h>
#include <power/rk8xx_pmic.h>
#include <power/pmic.h>
#include <linux/bitops.h>
#include <linux/compat.h>

/* RTC_CTRL_REG bitfields */
#define BIT_RTC_CTRL_REG_STOP_RTC_M		BIT(0)

/* RK808 has a shadowed register for saving a "frozen" RTC time.
 * When user setting "GET_TIME" to 1, the time will save in this shadowed
 * register. If set "READSEL" to 1, user read rtc time register, actually
 * get the time of that moment. If we need the real time, clr this bit.
 */

#define BIT_RTC_CTRL_REG_RTC_GET_TIME		BIT(6)
#define BIT_RTC_CTRL_REG_RTC_READSEL_M		BIT(7)
#define RTC_STATUS_MASK				0xFE

#define SECONDS_REG_MSK				0x7F
#define MINUTES_REG_MAK				0x7F
#define HOURS_REG_MSK				0x3F
#define DAYS_REG_MSK				0x3F
#define MONTHS_REG_MSK				0x1F
#define YEARS_REG_MSK				0xFF
#define WEEKS_REG_MSK				0x7

/* REG_SECONDS_REG through REG_YEARS_REG is how many registers? */

#define NUM_TIME_REGS	(REG_WEEKS - REG_SECONDS + 1)

static int rk808_rtc_set(struct udevice *dev, const struct rtc_time *tm)
{
	u8 rtc_data[NUM_TIME_REGS];

	debug("RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
			tm->tm_year, tm->tm_mon, tm->tm_mday,
			tm->tm_wday, tm->tm_hour, tm->tm_min, tm->tm_sec);

	rtc_data[0] = bin2bcd(tm->tm_sec);
	rtc_data[1] = bin2bcd(tm->tm_min);
	rtc_data[2] = bin2bcd(tm->tm_hour);
	rtc_data[3] = bin2bcd(tm->tm_mday);
	rtc_data[4] = bin2bcd(tm->tm_mon);
	rtc_data[5] = bin2bcd(tm->tm_year - 2000);
	rtc_data[6] = bin2bcd(tm->tm_wday);

/* Stop RTC while updating the RTC registers */
	pmic_clrsetbits(dev->parent, REG_RTC_CTRL, 0,
						BIT_RTC_CTRL_REG_STOP_RTC_M);
	pmic_write(dev->parent, REG_SECONDS, rtc_data, NUM_TIME_REGS);

/* Start RTC again */
	pmic_clrsetbits(dev->parent, REG_RTC_CTRL,
						BIT_RTC_CTRL_REG_STOP_RTC_M, 0);
	return 0;
}

static int rk808_rtc_get(struct udevice *dev, struct rtc_time *tm)
{
	u8 rtc_data[NUM_TIME_REGS];

/* Force an update of the shadowed registers right now */
	pmic_clrsetbits(dev->parent, REG_RTC_CTRL, 0,
						BIT_RTC_CTRL_REG_RTC_GET_TIME);

/*
 * After we set the GET_TIME bit, the rtc time can't be read
 * immediately. So we should wait up to 31.25 us, about one cycle of
 * 32khz. If we clear the GET_TIME bit here, the time of i2c transfer
 * certainly more than 31.25us: 16 * 2.5us at 400kHz bus frequency.
 */
	pmic_clrsetbits(dev->parent, REG_RTC_CTRL,
					BIT_RTC_CTRL_REG_RTC_GET_TIME, 0);
	pmic_read(dev->parent, REG_SECONDS, rtc_data, NUM_TIME_REGS);

	tm->tm_sec = bcd2bin(rtc_data[0] & SECONDS_REG_MSK);
	tm->tm_min = bcd2bin(rtc_data[1] & MINUTES_REG_MAK);
	tm->tm_hour = bcd2bin(rtc_data[2] & HOURS_REG_MSK);
	tm->tm_mday = bcd2bin(rtc_data[3] & DAYS_REG_MSK);
	tm->tm_mon = (bcd2bin(rtc_data[4] & MONTHS_REG_MSK));
	tm->tm_year = (bcd2bin(rtc_data[5] & YEARS_REG_MSK)) + 2000;
	tm->tm_wday = bcd2bin(rtc_data[6] & WEEKS_REG_MSK);
/*
 * RK808 PMIC RTC h/w counts/has 31 days in november. This is corrected
 * when date cmd is invoked on prompt. checks for the current day and
 * if it is 31 November, then adjusts it to 1 December.
 *
 * h/w also has weeks register which counts from 0 to 7(0(sun)-6(sat)).
 * 7 is an unknown state, reset it back to 0(sun).
 */
	if (tm->tm_mon == 11 && tm->tm_mday == 31) {
		debug("correcting Nov 31st to Dec 1st (HW bug)\n");
		tm->tm_mon += 1;
		tm->tm_mday = 1;
		if (tm->tm_wday == 7)
			tm->tm_wday = 0;
		rk808_rtc_set(dev, tm);
	}

	if (tm->tm_wday == 7) {
		tm->tm_wday = 0;
		rk808_rtc_set(dev, tm);
	}

	debug("RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
			tm->tm_year, tm->tm_mon, tm->tm_mday,
			tm->tm_wday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	return 0;
}

static int rk808_rtc_reset(struct udevice *dev)
{
/* Not needed */
	return 0;
}

static int rk808_rtc_init(struct udevice *dev)
{
	struct rtc_time tm;

/* start rtc running by default, and use shadowed timer. */
	pmic_clrsetbits(dev->parent, REG_RTC_CTRL, 0,
					BIT_RTC_CTRL_REG_RTC_READSEL_M);
	pmic_reg_write(dev->parent, REG_RTC_STATUS, RTC_STATUS_MASK);
/* set init time */
	rk808_rtc_get(dev, &tm);
	return 0;
}

static int rk808_rtc_probe(struct udevice *dev)
{
	rk808_rtc_init(dev);
	return 0;
}

static const struct rtc_ops rk808_rtc_ops = {
	.get = rk808_rtc_get,
	.set = rk808_rtc_set,
	.reset = rk808_rtc_reset,
};

U_BOOT_DRIVER(rk808_rtc) = {
	.name     = "rk808_rtc",
	.id       = UCLASS_RTC,
	.ops      = &rk808_rtc_ops,
	.probe    = rk808_rtc_probe,
};
