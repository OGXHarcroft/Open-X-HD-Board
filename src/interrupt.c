// Copyright 2021, Ryan Wendland, XboxHDMI by Ryzee119
// SPDX-License-Identifier: MIT

#include "stm32_hal.h"
#include "adv7511.h"
#include "interrupt.h"

extern adv7511 encoder;

void SysTick_Handler(void)
{

    HAL_IncTick();
    HAL_SYSTICK_IRQHandler();
}

void ADV_IRQ_HANDLER(void)
{
    encoder.interrupt = 1;
    HAL_GPIO_EXTI_IRQHandler(ADV_IRQ_PIN);
}

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    while (1)
        ;
}

void SVC_Handler(void)
{
}

void PendSV_Handler(void)
{
}