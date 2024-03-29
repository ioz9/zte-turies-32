/*
 * Copyright (C) 2009 ZTE,  Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/* ========================================================================================
when             who             what, where, why                                                                            comment tag
---------     -------       -------------------------------------------------------     --------------------------
2010-05-20   weilanying   �޸�blade_keypad����          ZTE_KEYPAD_WLY_0520
2010-04-02   zhangtao     modified the keypad map			  ZTE_KEYPAD_ZT_20100402_001
2009-12-11     wly         support ftm mode                          ZTE_FTM_MODE_WLY_001
2009-10-30   qhhuang       Copy from board-raise-keypad and modify "raise" to "mooncake" to support mooncake keypad      
=========================================================================================*/

#include <linux/platform_device.h>
#include <linux/gpio_event.h>

#include <asm/mach-types.h>

/* chenjun, 2009-07-01, begin */
// chenjun: ��output_gpios
static unsigned int keypad_row_gpios[] = {31, 28}; // {31, 32, 33, 34, 35, 41}

// chenjun: ��input_gpios
static unsigned int keypad_col_gpios[] = {37, 41, 40, 36}; // { 36, 37, 38, 39, 40 }
/* chenjun, 2009-07-01, end */

#define KEYMAP_INDEX(row, col) ((row)*ARRAY_SIZE(keypad_col_gpios) + (col))


static const unsigned short keypad_keymap_mooncake[ARRAY_SIZE(keypad_col_gpios) *
					      ARRAY_SIZE(keypad_row_gpios)] = {
	/*                       row, col   */
	[KEYMAP_INDEX(0, 0)] = KEY_HOME, //upper
	[KEYMAP_INDEX(0, 1)] = KEY_END, //lower
	[KEYMAP_INDEX(0, 2)] = KEY_END,
	[KEYMAP_INDEX(0, 3)] = KEY_SEND,
	[KEYMAP_INDEX(1, 0)] = KEY_VOLUMEUP, //middle key
	[KEYMAP_INDEX(1, 1)] = KEY_VOLUMEDOWN,   //left key
	[KEYMAP_INDEX(1, 2)] = KEY_MENU, //middle key
	[KEYMAP_INDEX(1, 3)] = KEY_HOME,   //left key
};
//ZTE_KEYPAD_ZT_20100402_001 end

static const unsigned short mooncake_keypad_virtual_keys[] = {
	KEY_END,
	KEY_POWER
};

/* mooncake keypad platform device information */
static struct gpio_event_matrix_info mooncake_keypad_matrix_info = {
	.info.func	= gpio_event_matrix_func,
	.keymap		= keypad_keymap_mooncake,
	.output_gpios	= keypad_row_gpios,
	.input_gpios	= keypad_col_gpios,
	.noutputs	= ARRAY_SIZE(keypad_row_gpios),
	.ninputs	= ARRAY_SIZE(keypad_col_gpios),
	.settle_time.tv.nsec = 0,
	.poll_time.tv.nsec = 20 * NSEC_PER_MSEC,
#if 1 // chenjun
	.flags		= GPIOKPF_LEVEL_TRIGGERED_IRQ | GPIOKPF_DRIVE_INACTIVE |
			  GPIOKPF_PRINT_UNMAPPED_KEYS
#else
	.flags		= GPIOKPF_LEVEL_TRIGGERED_IRQ | GPIOKPF_DRIVE_INACTIVE | GPIOKPF_ACTIVE_HIGH | GPIOKPF_PRINT_UNMAPPED_KEYS /*| GPIOKPF_PRINT_MAPPED_KEYS*/
#endif
};

static struct gpio_event_info *mooncake_keypad_info[] = {
	&mooncake_keypad_matrix_info.info
};

static struct gpio_event_platform_data mooncake_keypad_data = {
	.name		= "mooncake_keypad",
	.info		= mooncake_keypad_info,
	.info_count	= ARRAY_SIZE(mooncake_keypad_info)
};

struct platform_device keypad_device_mooncake = {
	.name	= GPIO_EVENT_DEV_NAME,
	.id	= -1,
	.dev	= {
		.platform_data	= &mooncake_keypad_data,
	},
};
/* ZTE_FTM_MODE_WLY_001, @2009-12-11, START*/
#ifdef CONFIG_ZTE_FTM_FLAG_SUPPORT
extern int zte_get_ftm_flag(void);
#endif
/* ZTE_FTM_MODE_WLY_001, @2009-12-11, END*/
static int __init mooncake_init_keypad(void)
{
        /* ZTE_FTM_MODE_WLY_001, @2009-12-11, START*/
	#ifdef CONFIG_ZTE_FTM_FLAG_SUPPORT
	int ftm_flag;
	ftm_flag = zte_get_ftm_flag();
	if (1 ==ftm_flag)return 0;
	#endif
        /* ZTE_FTM_MODE_WLY_001, @2009-12-11, START*/
	mooncake_keypad_matrix_info.keymap = keypad_keymap_mooncake;
	return platform_device_register(&keypad_device_mooncake);
}

device_initcall(mooncake_init_keypad);

