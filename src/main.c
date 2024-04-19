// Copyright 2021, Ryan Wendland, XboxHDMI by Ryzee119
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include "main.h"
#include "stm32_hal.h"
#include "adv7511.h"

I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c2;
UART_HandleTypeDef huart;
adv7511 encoder;

void SystemClock_Config(void);
static void init_gpio(void);
static void init_i2c1(void);
static void init_uart2(void);

//Stdout print on UART for printf
int _write(int file, char *ptr, int len)
{
    HAL_UART_Transmit(&huart, (uint8_t *)ptr, len, 100);
    return len;
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    init_gpio();
    init_i2c1();
    init_uart2();

    HAL_Delay(200);
    printf("\r\nADV7511 Chip Revision %u\r\n", adv7511_read_register(&hi2c1, 0x00));

    uint8_t error = 0;

    //Initialise the encoder object
    adv7511_struct_init(&encoder);

    //Force Hot Plug Detect High
    encoder.hot_plug_detect = 1;
    error |= adv7511_write_register(&hi2c1, 0xD6, 0b11000000);

    //Power up the encoder and set fixed registers
    error |= adv7511_power_up(&hi2c1, &encoder);
    HAL_Delay(200);

    //Set video input mode to YCbCr 444, 12bit databus DDR.
    error |= adv7511_update_register(&hi2c1, 0x15, 0b00001111, 0b000000101); //ID=5

    //Set video style to style 1 (Y[3:0] Cb[7:0] first edge, Cr[7:0] Y[7:4] second edge)
    error |= adv7511_update_register(&hi2c1, 0x16, 0b00001100, 0b00001000); //style 1 01 = style 2    10 = style 1  11 = style 3

    //Set DDR Input Edge  first half of pixel data clocking edge, Bit 1 |= 0 for falling edge, 1 for rising edge CHECK
    error |= adv7511_update_register(&hi2c1, 0x16, 0b00000010, 0b00000010); //Rising

    //Bit order reverse for input signals. 1 |= LSB .... MSB Reverse Bus Order
    //Just how my PCB is layed out.
    error |= adv7511_update_register(&hi2c1, 0x48, 0b01000000, 0b01000000);

    //DDR Alignment . 1 |= DDR input is D[35:18] (left aligned), 0 = right aligned
    error |= adv7511_update_register(&hi2c1, 0x48, 0b00100000, 0b00000000);

    //Clock Delay adjust.
    error |= adv7511_update_register(&hi2c1, 0xD0, 0b10000000, 0b10000000);
    error |= adv7511_update_register(&hi2c1, 0xD0, 0b01110000, 3 << 4); //0 to 6, 3 = no delay
    error |= adv7511_update_register(&hi2c1, 0xBA, 0b11100000, 3 << 5);

    //Must be 11 for ID=5 (No sync pulse)
    error |= adv7511_update_register(&hi2c1, 0xD0, 0b00001100, 0b00001100);

    //Enable DE generation. This is derived from HSYNC,VSYNC for video active framing
    error |= adv7511_update_register(&hi2c1, 0x17, 0b00000001, 0b00000001);

    //Set Output Format to 4:4:4
    error |= adv7511_update_register(&hi2c1, 0x16, 0b10000000, 0b00000000);

    //Start AVI Infoframe Update
    error |= adv7511_update_register(&hi2c1, 0x4A, 0b01000000, 0b01000000);

    //Infoframe output format to YCbCr 4:4:4 in infoframe* 10=YCbCr4:4:4, 00=RGB
    error |= adv7511_update_register(&hi2c1, 0x55, 0b01100000, 0b01000000);

    //Infoframe output aspect ratio default to 4:3
    error |= adv7511_update_register(&hi2c1, 0x56, 0b00110000, 0b00010000);
    error |= adv7511_update_register(&hi2c1, 0x56, 0b00001111, 0b00001000);

    //END AVI Infoframe Update
    error |= adv7511_update_register(&hi2c1, 0x4A, 0b01000000, 0b00000000);

    //Output Color Space Selection 0 = RGB 1 = YCbCr
    error |= adv7511_update_register(&hi2c1, 0x16, 0b00000001, 0b00000001);

    //Set Output to HDMI Mode (Instead of DVI Mode)
    error |= adv7511_update_register(&hi2c1, 0xAF, 0b00000010, 0b00000010);

    //Enable General Control Packet CHECK
    error |= adv7511_update_register(&hi2c1, 0x40, 0b10000000, 0b10000000);

    //Disable CSC
    //error |= adv7511_update_register(&hi2c1, 0x18, 0xFF, 0x00);

    //SETUP AUDIO
    //Set 48kHz Audio clock CHECK (N Value)
    error |= adv7511_update_register(&hi2c1, 0x01, 0xFF, 0x00);
    error |= adv7511_update_register(&hi2c1, 0x02, 0xFF, 0x18);
    error |= adv7511_update_register(&hi2c1, 0x03, 0xFF, 0x00);

    //Set SPDIF audio source
    error |= adv7511_update_register(&hi2c1, 0x0A, 0b01110000, 0b00010000);

    //SPDIF enable
    error |= adv7511_update_register(&hi2c1, 0x0B, 0b10000000, 0b10000000);

    if (error)
    {
        printf("Encountered error when setting up ADV7511\r\n");
    }

    apply_csc(&hi2c1, (uint8_t *)identityMatrix);
    uint8_t vic = 0x80;
    uint8_t pll_lock = 0;

    while (1)
    {
        if (error)
        {
            printf("Encountered error when setting up ADV7511\r\n");
            error = 0;
        }

        //Check PLL status
        pll_lock = (adv7511_read_register(&hi2c1, 0x9E) >> 4) & 0x01;
        if (pll_lock == 0)
        {
            printf("PLL Lock: %u\r\n", pll_lock);
            HAL_GPIO_WritePin(GPIOA, STATUS_LED, GPIO_PIN_RESET);
        }
        else
        {
            HAL_GPIO_WritePin(GPIOA, STATUS_LED, GPIO_PIN_SET);
        }

        if ((adv7511_read_register(&hi2c1, 0x3e) >> 2) != (vic & 0x0F))
        {
            printf("VIC Changed!\r\n");
            //Set MSB to 1. This indicates a recent change.
            vic = ADV7511_VIC_CHANGED | adv7511_read_register(&hi2c1, 0x3e) >> 2;
            printf("Detected VIC#: 0x%02x\r\n", vic & ADV7511_VIC_CHANGED_CLEAR);
        }

        if (encoder.interrupt)
        {
            uint8_t interrupt_register = adv7511_read_register(&hi2c1, 0x96);
            printf("Interrupt occurred!\r\n");
            printf("interrupt_register: 0x%02x\r\n", interrupt_register);

            if (interrupt_register & ADV7511_INT0_HPD)
            {
                printf("HPD interrupt\r\n");
                encoder.hot_plug_detect = (adv7511_read_register(&hi2c1, 0x42) >> 6) & 0x01;
            }

            if (interrupt_register & ADV7511_INT0_MONITOR_SENSE)
            {
                printf("Monitor Sense Interrupt\r\n");
                encoder.monitor_sense = (adv7511_read_register(&hi2c1, 0x42) >> 5) & 0x01;
            }

            (encoder.hot_plug_detect) ? printf("HDMI cable detected\r\n") : printf("HDMI cable not detected\r\n");
            (encoder.monitor_sense) ? printf("Monitor is ready\r\n") : printf("Monitor is not ready\r\n");

            if (encoder.hot_plug_detect && encoder.monitor_sense)
            {
                adv7511_power_up(&hi2c1, &encoder);
            }

            encoder.interrupt = 0;
            //Re-enable interrupts
            adv7511_update_register(&hi2c1, 0x96, 0b11000000, 0xC0);
        }

        if (vic & ADV7511_VIC_CHANGED)
        {
            uint16_t hs_delay = 0, vs_delay = 0, active_w = 0, active_h = 0;
            uint8_t ddr_edge = 1;
            vic &= ADV7511_VIC_CHANGED_CLEAR;

            //Set pixel rep mode to auto
            //error |= adv7511_update_register(&hi2c1, 0x3B, 0b01100000, 0b00000000);

            if (vic == ADV7511_VIC_VGA_640x480_4_3)
            {
                printf("Set timing for VGA 640x480 4:3\r\n");
                //Infoframe output aspect ratio default to 4:3
                error |= adv7511_update_register(&hi2c1, 0x56, 0b00110000, 0b00010000);
                ddr_edge = 1;
                hs_delay = 119; //121
                vs_delay = 36;
                active_w = 720;
                active_h = 480;
            }

            else if (vic == ADV7511_VIC_480p_4_3 || vic == ADV7511_VIC_480p_16_9 || vic == ADV7511_VIC_UNAVAILABLE)
            {
                if (vic == ADV7511_VIC_480p_16_9)
                {
                    //Infoframe output aspect ratio default to 16:9
                    error |= adv7511_update_register(&hi2c1, 0x56, 0b00110000, 0b00100000);
                    printf("Set timing for 480p 16:9\r\n");
                }
                else
                {
                    //Infoframe output aspect ratio default to 4:3
                    error |= adv7511_update_register(&hi2c1, 0x56, 0b00110000, 0b00010000);
                    printf("Set timing for 480p 4:3\r\n");
                }
                ddr_edge = 1;
                hs_delay = 118;
                vs_delay = 36;
                active_w = 720;
                active_h = 480;
            }

            else if (vic == ADV7511_VIC_720p_60_16_9)
            {
                printf("Set timing for 720p 16:9\r\n");
                //Infoframe output aspect ratio default to 16:9
                error |= adv7511_update_register(&hi2c1, 0x56, 0b00110000, 0b00100000);
                ddr_edge = 1;
                hs_delay = 299; //259?
                vs_delay = 25;
                active_w = 1280;
                active_h = 720;
            }

            else if (vic == ADV7511_VIC_1080i_60_16_9)
            {
                printf("Set timing for 1080i 16:9\r\n");
                //Infoframe output aspect ratio default to 16:9
                error |= adv7511_update_register(&hi2c1, 0x56, 0b00110000, 0b00100000);
                //Set interlace offset
                error |= adv7511_update_register(&hi2c1, 0x37, 0b11100000, 0 << 5);
                //Offset for Sync Adjustment Vsync Placement
                error |= adv7511_update_register(&hi2c1, 0xDC, 0b11100000, 0 << 5);
                ddr_edge = 1;
                hs_delay = 233; //232
                vs_delay = 22;
                active_w = 1920;
                active_h = 540;
            }

            error |= adv7511_update_register(&hi2c1, 0x16, 0b00000010, ddr_edge << 1);
            error |= adv7511_update_register(&hi2c1, 0x36, 0b00111111, (uint8_t)vs_delay);
            error |= adv7511_update_register(&hi2c1, 0x35, 0b11111111, (uint8_t)(hs_delay >> 2));
            error |= adv7511_update_register(&hi2c1, 0x36, 0b11000000, (uint8_t)(hs_delay << 6));
            error |= adv7511_update_register(&hi2c1, 0x37, 0b00011111, (uint8_t)(active_w >> 7));
            error |= adv7511_update_register(&hi2c1, 0x38, 0b11111110, (uint8_t)(active_w << 1));
            error |= adv7511_update_register(&hi2c1, 0x39, 0b11111111, (uint8_t)(active_h >> 4));
            error |= adv7511_update_register(&hi2c1, 0x3A, 0b11110000, (uint8_t)(active_h << 4));
            printf("Actual Pixel Repetition : 0x%02x\r\n", (adv7511_read_register(&hi2c1, 0x3D) & 0xC0) >> 6);
            printf("Actual VIC Sent : 0x%02x\r\n", adv7511_read_register(&hi2c1, 0x3D) & 0x1F);
        }
    }
}

/* I2C1 init function */
static void init_i2c1(void)
{
    hi2c1.Instance = HI2C_ADV_INSTANCE;
    hi2c1.Init.Timing = HI2C_ADV_INSTANCE_TIMING;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    //Configure Analogue filter
    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    //Configure Digital filter
    if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }
}

static void init_uart2(void)
{
    huart.Instance = HUART_DEBUG_INSTANCE;
    huart.Init.BaudRate = 9600;
    huart.Init.StopBits = UART_STOPBITS_1;
    huart.Init.Parity = UART_PARITY_NONE;
    huart.Init.Mode = UART_MODE_TX;
    huart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart.Init.OverSampling = UART_OVERSAMPLING_16;
    huart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_HalfDuplex_Init(&huart) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }
}

static void init_gpio(void)
{

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    #ifdef GPIOA
    if (ADV_IRQ_PORT==GPIOA || STATUS_LED_PORT==GPIOA || SPARE_GPIO_PORT==GPIOA)
        __HAL_RCC_GPIOA_CLK_ENABLE();
    #endif
    #ifdef GPIOB
    if (ADV_IRQ_PORT==GPIOB || STATUS_LED_PORT==GPIOB || SPARE_GPIO_PORT==GPIOB)
        __HAL_RCC_GPIOB_CLK_ENABLE();
    #endif
    #ifdef GPIOC
    if (ADV_IRQ_PORT==GPIOC || STATUS_LED_PORT==GPIOC || SPARE_GPIO_PORT==GPIOC)
        __HAL_RCC_GPIOC_CLK_ENABLE();
    #endif
    #ifdef GPIOD
    if (ADV_IRQ_PORT==GPIOD || STATUS_LED_PORT==GPIOD || SPARE_GPIO_PORT==GPIOD)
        __HAL_RCC_GPIOD_CLK_ENABLE();
    #endif
    #ifdef GPIOE
    if (ADV_IRQ_PORT==GPIOE || STATUS_LED_PORT==GPIOE || SPARE_GPIO_PORT==GPIOE)
        __HAL_RCC_GPIOE_CLK_ENABLE();
    #endif
    #ifdef GPIOF
    if (ADV_IRQ_PORT==GPIOF || STATUS_LED_PORT==GPIOF || SPARE_GPIO_PORT==GPIOF)
        __HAL_RCC_GPIOF_CLK_ENABLE();
    #endif
    #ifdef GPIOG
    if (ADV_IRQ_PORT==GPIOG || STATUS_LED_PORT==GPIOG || SPARE_GPIO_PORT==GPIOG)
        __HAL_RCC_GPIOG_CLK_ENABLE();
    #endif
    #ifdef GPIOH
    if (ADV_IRQ_PORT==GPIOH || STATUS_LED_PORT==GPIOH || SPARE_GPIO_PORT==GPIOH)
        __HAL_RCC_GPIOH_CLK_ENABLE();
    #endif

    GPIO_InitStruct.Pin = ADV_IRQ_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(ADV_IRQ_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = STATUS_LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(STATUS_LED_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = SPARE_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(SPARE_GPIO_PORT, &GPIO_InitStruct);

    //EXTI interrupt init
    HAL_NVIC_SetPriority(ADV_IRQ_LINE, 0, 0);
    HAL_NVIC_EnableIRQ(ADV_IRQ_LINE);

}

void _Error_Handler(char *file, int line)
{
    while (1)
    {
    }
}