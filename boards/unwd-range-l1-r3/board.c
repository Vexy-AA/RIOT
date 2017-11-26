/*
 * Copyright (C) 2016 Unwired Devices
 *
 * This file is subject to the terms and conditions of the GNU Lesser General
 * Public License v2.1. See the file LICENSE in the top level directory for more
 * details.
 */

/**
 * @ingroup     boards_unwd-range-l1
 * @{
 *
 * @file
 * @brief       Board specific implementations for the unwd-range-l1 R160829 board
 *
 * @author      Mihail Churikov
 *
 * @}
 */

#include "board.h"
#include "periph/gpio.h"
#include "lpm.h"

void board_init(void)
{
    /* initialize the CPU */
    cpu_init();

    /* initialize the boards LEDs */
    if (LED_GREEN != GPIO_UNDEF) {
        gpio_init(LED_GREEN, GPIO_OUT);
        lpm_add_gpio_exclusion(LED_GREEN);
    }
    
    if (LED_RED != GPIO_UNDEF) {
        gpio_init(LED_RED, GPIO_OUT);
        lpm_add_gpio_exclusion(LED_RED);
    }
}