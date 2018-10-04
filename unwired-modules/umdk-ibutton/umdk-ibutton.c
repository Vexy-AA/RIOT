/*
 * Copyright (C) 2016-2018 Unwired Devices LLC <info@unwds.com>

 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software
 * is furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @defgroup
 * @ingroup
 * @brief
 * @{
 * @file        umdk-ibutton.c
 * @brief       umdk-ibutton module implementation
 * @author      Mikhail Perkov
 */

#ifdef __cplusplus
extern "C" {
#endif

/* define is autogenerated, do not change */
#undef _UMDK_MID_
#define _UMDK_MID_ UNWDS_IBUTTON_MODULE_ID

/* define is autogenerated, do not change */
#undef _UMDK_NAME_
#define _UMDK_NAME_ "ibutton"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

#include "periph/gpio.h"
#include "periph/pm.h"

#include "unwds-common.h"

#include "board.h"
#include "onewire.h"
#include "umdk-ibutton.h"

#include "thread.h"
#include "rtctimers.h"
#include "rtctimers-millis.h"
#include "periph/rtc.h"
#include "checksum/crc8.h"

#define ENABLE_DEBUG 0
#include "debug.h"

static uwnds_cb_t *callback;
static kernel_pid_t ibutton_pid;

static rtctimers_millis_t detect_timer;
static uint8_t id_detected[UMDK_IBUTTON_SIZE_ID] = { 0 };

static int led_gpio_enabled = 0;

static uint8_t check_crc(void)
{
    uint8_t crc_rx = id_detected[UMDK_IBUTTON_SIZE_ID - 1];
    uint8_t crc = crc8(id_detected, UMDK_IBUTTON_SIZE_ID - 1);
    
    if(crc_rx != crc) {
        puts("[umdk-" _UMDK_NAME_ "] Error -> Wrong CRC");
        return CRC_WRONG;
    }
    
    return CRC_RIGHT;
}

static int detect_device(void)
{ 
    /* Look for iButton device */    
    onewire_sendbyte(UMDK_IBUTTON_READ_ROM); 
    
    /* Read 64-bit device ID */
    for (int i = 0; i < UMDK_IBUTTON_SIZE_ID; i++) {
        id_detected[i] = onewire_readbyte();    
    }
    
    /* Incorrect ID family */    
    if (id_detected[0] == 0)
        return DEVICE_ERROR;
        
    /* Check CRC */
    if (check_crc() == CRC_RIGHT) {
        return DEVICE_OK;    
    }

    return DEVICE_ERROR;
}

static void *radio_send(void *arg)
{
    (void)arg;
    
    msg_t msg;
    msg_t msg_queue[4];
    msg_init_queue(msg_queue, 4);

    while (1) {
        msg_receive(&msg);
        module_data_t data;

        data.data[0] = _UMDK_MID_;
        data.length = 1;
                                  
        memcpy(&data.data[1], id_detected,  UMDK_IBUTTON_SIZE_ID);
        data.length += UMDK_IBUTTON_SIZE_ID;
        
        gpio_set(UMDK_IBUTTON_LED_GPIO);
        led_gpio_enabled = (1000*UMDK_IBUTTON_GRANTED_PERIOD_SEC)/UMDK_IBUTTON_POLLING_PERIOD_MS;
        
        printf("[" _UMDK_NAME_ "] i-Button detected, ID ");
        for(int i = data.length - 1; i > 0; i--) {
            printf("%02X ", data.data[i]);
        }
        printf("\n");

        data.as_ack = false;
        callback(&data);
    }
    
    return NULL;
} 

static void detect_handler(void *arg) 
{
    (void) arg;
    if (led_gpio_enabled) {
        if (--led_gpio_enabled == 0) {
            gpio_clear(UMDK_IBUTTON_LED_GPIO);
        }
    }
    else {
        if (onewire_detect()) {
            if (detect_device() == DEVICE_OK) {
                msg_t detect_msg;
                msg_try_send(&detect_msg, ibutton_pid);
            }
        }
    }
    rtctimers_millis_set(&detect_timer, UMDK_IBUTTON_POLLING_PERIOD_MS);
}

void umdk_ibutton_init(uint32_t *non_gpio_pin_map, uwnds_cb_t *event_callback)
{
    (void) non_gpio_pin_map;

    callback = event_callback;
    
    /* Initialize 1-Wire bus driver */
    onewire_init(UMDK_IBUTTON_DEV);
    
    gpio_init(UMDK_IBUTTON_LED_GPIO, GPIO_OUT);
    gpio_clear(UMDK_IBUTTON_LED_GPIO);
    
    /* Create handler thread */
    char *stack = (char *) allocate_stack(UMDK_IBUTTON_STACK_SIZE);
    if (!stack) {
        return;
    }
    ibutton_pid = thread_create(stack, UMDK_IBUTTON_STACK_SIZE, THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST,
                                radio_send, NULL, "ibutton thread");
    
    /* Configure periodic wakeup */
    detect_timer.callback = &detect_handler;
    rtctimers_millis_set(&detect_timer, UMDK_IBUTTON_POLLING_PERIOD_MS);
}

static inline void reply_error(module_data_t *reply) 
{
    reply->as_ack = true;
    reply->length = 2;
    reply->data[0] = _UMDK_MID_;
    reply->data[1] = 0;
}

static inline void reply_ok(module_data_t *reply) 
{
    reply->as_ack = true;
    reply->length = 2;
    reply->data[0] = _UMDK_MID_;
    reply->data[1] = 1;;
}

bool umdk_ibutton_cmd(module_data_t *cmd, module_data_t *reply)
{
    if (cmd->length < 1) {
        return false;
    }

    umdk_ibutton_cmd_t c = cmd->data[0];
    
    switch (c) {
        default: {
            reply_error(reply);
            return true;
        }
    }
    /* Don't reply by default */
    return false;
}

#ifdef __cplusplus
}
#endif
