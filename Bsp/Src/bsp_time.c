/**
 * @file bsp_time.c
 * @brief 板级计时与启动熵，提供 HAL tick、设备 UID 及 STM32H743 RNG 接口。
 */

#include "bsp_time.h"
#include "stm32h7xx_hal.h"

/* 模块数据 */

#if defined(STM32H743xx)
static uint8_t s_started_hsi48;
#endif

/* 公共接口 */

uint32_t BspTime_GetTickMs(void)
{
    return HAL_GetTick();
}

uint8_t BspTime_ReadBootRandom(uint32_t *word)
{
#if defined(STM32H743xx)
    if (!(RCC->CR & RCC_CR_HSI48RDY))
    {
        if (!(RCC->CR & RCC_CR_HSI48ON))
        {
            __HAL_RCC_HSI48_ENABLE();
            s_started_hsi48 = 1U;
        }
        return 0U;
    }
    if (!(RCC->AHB2ENR & RCC_AHB2ENR_RNGEN))
    {
        __HAL_RCC_RNG_CONFIG(RCC_RNGCLKSOURCE_HSI48);
        __HAL_RCC_RNG_CLK_ENABLE();
        RNG->CR = RNG_CR_RNGEN;
        return 0U;
    }
    if (RNG->SR & (RNG_SR_CECS | RNG_SR_SECS | RNG_SR_CEIS | RNG_SR_SEIS))
    {
        RNG->CR = 0U;
        RNG->SR = 0U;
        RNG->CR = RNG_CR_RNGEN;
        return 0U;
    }
    if (!(RNG->SR & RNG_SR_DRDY))
    {
        return 0U;
    }
    *word = RNG->DR;
#else
    /* 主机测试使用可复现的序列模拟 RNG。 */
    static uint32_t test_word = 0x417C38D3U;
    test_word = test_word * 1664525U + 1013904223U;
    *word = test_word;
#endif
    return 1U;
}

void BspTime_StopBootRandom(void)
{
#if defined(STM32H743xx)
    RNG->CR = 0U;
    __HAL_RCC_RNG_CLK_DISABLE();
    if (s_started_hsi48)
    {
        __HAL_RCC_HSI48_DISABLE();
        s_started_hsi48 = 0U;
    }
#endif
}

void BspTime_GetUniqueId(uint32_t words[3])
{
#if defined(STM32H743xx)
    words[0] = HAL_GetUIDw0();
    words[1] = HAL_GetUIDw1();
    words[2] = HAL_GetUIDw2();
#else
    words[0] = 0x30291807U;
    words[1] = 0x51423324U;
    words[2] = 0x65768798U;
#endif
}
