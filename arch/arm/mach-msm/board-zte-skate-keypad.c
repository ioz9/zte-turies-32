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



#include <linux/platform_device.h>
#include <linux/gpio_event.h>

#include <asm/mach-types.h>

static unsigned int keypad_row_gpios[] = {33, 28}; 
static unsigned int keypad_col_gpios[] = {37, 41, 40}; 

#define KEYMAP_INDEX(row, col) ((row)*ARRAY_SIZE(keypad_col_gpios) + (col))


static const unsigned short keypad_keymap_skate[ARRAY_SIZE(keypad_col_gpios) *
					      ARRAY_SIZE(keypad_row_gpios)] = {
	/*                       row, col   */
	[KEYMAP_INDEX(0, 0)] = KEY_BACK, 
	[KEYMAP_INDEX(0, 1)] = 0, 
	[KEYMAP_INDEX(0, 2)] = KEY_MENU,
	[KEYMAP_INDEX(1, 0)] = KEY_VOLUMEUP,
	[KEYMAP_INDEX(1, 1)] = KEY_VOLUMEDOWN,
	[KEYMAP_INDEX(1, 2)] = KEY_HOME,
};

static const unsigned short skate_keypad_virtual_keys[] = {
	KEY_END,
	KEY_POWER
};

/* skate keypad platform device information */
static struct gpio_event_matrix_info skate_keypad_matrix_info = {
	.info.func	= gpio_event_matrix_func,
	.keymap		= keypad_keymap_skate,
	.output_gpios	= keypad_row_gpios,
	.input_gpios	= keypad_col_gpios,
	.noutputs	= ARRAY_SIZE(keypad_row_gpios),
	.ninputs	= ARRAY_SIZE(keypad_col_gpios),
	.settle_time.tv.nsec = 0,
	.poll_time.tv.nsec = 20 * NSEC_PER_MSEC,
#if 1 // chenjun
	.flags		= GPIOKPF_LEVEL_TRIGGERED_IRQ | GPIOKPF_DRIVE_INACTIVE | GPIOKPF_PRINT_UNMAPPED_KEYS
#else
	.flags		= GPIOKPF_LEVEL_TRIGGERED_IRQ | GPIOKPF_DRIVE_INACTIVE | GPIOKPF_ACTIVE_HIGH | GPIOKPF_PRINT_UNMAPPED_KEYS /*| GPIOKPF_PRINT_MAPPED_KEYS*/
#endif
};

static struct gpio_event_info *skate_keypad_info[] = {
	&skate_keypad_matrix_info.info
};

static struct gpio_event_platform_data skate_keypad_data = {
	.name		= "skate_keypad",  //ZTE_KEYPAD_WLY_0520
	.info		= skate_keypad_info,
	.info_count	= ARRAY_SIZE(skate_keypad_info)
};

struct platform_device keypad_device_skate = {
	.name	= GPIO_EVENT_DEV_NAME,
	.id	= -1,
	.dev	= {
		.platform_data	= &skate_keypad_data,
	},
};
/* ZTE_FTM_MODE_WLY_001, @2009-12-11, START*/
#ifdef CONFIG_ZTE_FTM_FLAG_SUPPORT
extern int zte_get_ftm_flag(void);
#endif
/* ZTE_FTM_MODE_WLY_001, @2009-12-11, END*/
static int __init skate_init_keypad(void)
{
        /* ZTE_FTM_MODE_WLY_001, @2009-12-11, START*/
	#ifdef CONFIG_ZTE_FTM_FLAG_SUPPORT
	int ftm_flag;
	ftm_flag = zte_get_ftm_flag();
	if (1 ==ftm_flag)return 0;
	#endif
        /* ZTE_FTM_MODE_WLY_001, @2009-12-11, START*/
	skate_keypad_matrix_info.keymap = keypad_keymap_skate;
	return platform_device_register(&keypad_device_skate);
}

device_initcall(skate_init_keypad);

