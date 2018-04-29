/******************************************************************************
 * Copyright 2013-2015 Espressif Systems
 *
 * FileName: user_main.c
 *
 * Description: Driver for a 23LC1024 or similar chip connected to the SPI port.
 * The chip is connected to the same pins as the main flash chip except for the
 * /CS pin: that needs to be connected to IO0. The chip is driven in 1-bit SPI
 * mode: theoretically, we can move data faster by using double- or quad-SPI
 * mode but that is not implemented here. The chip also is used like a generic
 * SPI device, nothing memory-mapped like the main flash. Also: these routines
 * are not thread-safe; use mutexes around them if you access the SPI RAM from
 * different threads.
 *
 * Modification history:
 *     2015/06/01, v1.0 File created.
 *     2015/06/12, modifications for SPI in QSPI mode
*******************************************************************************/
#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "gpio.h"
#include "key.h"

#include "../socket_server/socket_server.h"

/******************************************************************************
 * FunctionName : KEY 12/13/16 init
 * Description  :
 * Parameters   : none
 * Returns      :
 *******************************************************************************/

void ICACHE_FLASH_ATTR
gpio_intr_handler(void)
{
    _xt_isr_mask(1<<ETS_GPIO_INUM);    //disable interrupt
    os_delay_us(20*1000);//delay 20ms
    if(!GPIO_INPUT_GET(GPIO_ID_PIN(12))){
        os_printf("\r\n receive 12 button press!");
    }
    if(!GPIO_INPUT_GET(GPIO_ID_PIN(13))){
        os_printf("\r\n receive 13 button press2!");
		if (mp3_volume < 9) {
			mp3_volume++;
		}
    }
    GPIO_REG_WRITE( GPIO_STATUS_W1TC_ADDRESS, GPIO_Pin_12|GPIO_Pin_13 ); //clear
    _xt_isr_unmask(1 << ETS_GPIO_INUM); //Enable the GPIO interrupt
}

os_timer_t gpio16_timer;

void ICACHE_FLASH_ATTR
gpio16_timer_cb(void *arg) {
    static int value = 0;
    value = gpio16_input_get();
    if (value == 0) {
        os_printf("\r\n receive 16 button press2! ");
		if (mp3_volume > 0) {
			mp3_volume--;
		}
    }
}

void ICACHE_FLASH_ATTR
gpio16_init() {
    
    gpio16_input_conf();
    os_timer_disarm(&gpio16_timer);
    os_timer_setfn(&gpio16_timer, gpio16_timer_cb, NULL); /* Set callback for timer */
    os_timer_arm(&gpio16_timer, 50 /* call every 50ms */, 1 /* repeat */);
}

void ICACHE_FLASH_ATTR
key_all_init(void)
{
    GPIO_ConfigTypeDef gpio_in_cfg;                                    //Define GPIO Init Structure
    gpio_in_cfg.GPIO_IntrType = GPIO_PIN_INTR_NEGEDGE;                 //Falling edge trigger
    gpio_in_cfg.GPIO_Mode = GPIO_Mode_Input;                           //Input mode
    gpio_in_cfg.GPIO_Pin  = GPIO_Pin_12|GPIO_Pin_13;                   // Enable GPIO
    gpio_in_cfg.GPIO_Pullup = GPIO_PullUp_EN;
    gpio_config(&gpio_in_cfg);                                         //Initialization function
    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, GPIO_Pin_12|GPIO_Pin_13);//Forbidden ouput register
    gpio_intr_handler_register(gpio_intr_handler);           // Register the interrupt function
    _xt_isr_unmask(1 << ETS_GPIO_INUM);                                //Enable the GPIO interrupt
    
    gpio16_init();
    
}

void ICACHE_FLASH_ATTR
led_init(void)
{
    GPIO_ConfigTypeDef gpio_out_cfg;                                    //Define GPIO Init Structure
    //gpio_out_cfg.GPIO_IntrType = GPIO_PIN_INTR_DISABLE;                 //
    gpio_out_cfg.GPIO_Mode     = GPIO_Mode_Output;                      //Output mode
    gpio_out_cfg.GPIO_Pin      = GPIO_Pin_1|GPIO_Pin_14;                // Enable GPIO
    //gpio_out_cfg.GPIO_Pullup   = GPIO_PullUp_DIS;
    gpio_config(&gpio_out_cfg);                                         //Initialization function
    GPIO_OUTPUT_SET(GPIO_ID_PIN(1),  1);      //set 1
    GPIO_OUTPUT_SET(GPIO_ID_PIN(14), 1);     //set 1
}

void ICACHE_FLASH_ATTR
led_1(int enable)
{
	if (enable) {
        GPIO_OUTPUT_SET(GPIO_ID_PIN(1), 0);      //set 0
	} else {
        GPIO_OUTPUT_SET(GPIO_ID_PIN(1), 1);      //set 1
	}
}

void ICACHE_FLASH_ATTR
led_14(int enable)
{
	if (enable) {
        GPIO_OUTPUT_SET(GPIO_ID_PIN(14), 0);      //set 0
	} else {
        GPIO_OUTPUT_SET(GPIO_ID_PIN(14), 1);      //set 1
	}
}

void ICACHE_FLASH_ATTR
pa_init(void)
{
    GPIO_ConfigTypeDef gpio_out_cfg;
    gpio_out_cfg.GPIO_Mode = GPIO_Mode_Output;
    gpio_out_cfg.GPIO_Pin  = GPIO_Pin_0;
    gpio_config(&gpio_out_cfg);
    GPIO_OUTPUT_SET(GPIO_ID_PIN(0), 0);
}
