#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_system.h"
#include "stm32f4xx_ll_cortex.h"
#include "stm32f4xx_ll_rng.h"

#include "stm32f4xx_hal.h"
#include "stm32f4xx_nucleo_144.h"

#include "RNGWrapper.h"
#include "cmsis_os.h"

#include <string.h>

#define SYSTEM_CLOCK_FREQUENCY  (168000000)

static RNG_HandleTypeDef RngHandle;

void RNG_Init(void)
{
    RngHandle.Instance = RNG;
    /* 初期化済みの場合は初期化をやり直す */
    if (HAL_RNG_DeInit(&RngHandle) != HAL_OK)
    {
        goto ERR;
    }

    /* RNG初期化 */
    if (HAL_RNG_Init(&RngHandle) != HAL_OK)
    {
        goto ERR;
    }

    return;
ERR:
    BSP_LED_Init(LED2);
    while (1)
    {
        BSP_LED_Toggle(LED2);
        osDelay(100);
    }
}

int mbedtls_hardware_poll(void *Data, unsigned char *Output, size_t Len, size_t *oLen)
{
    uint32_t index;
    uint32_t randomValue;

    for (index = 0; index < Len/4; index++)
    {
        if (HAL_RNG_GenerateRandomNumber(&RngHandle, &randomValue) == HAL_OK)
        {
            *oLen += 4;
            memset(&(Output[index * 4]), (int)randomValue, 4);
        }
        else
        {
            BSP_LED_Init(LED2);
            while (1)
            {
                BSP_LED_Toggle(LED2);
                osDelay(100);
            }
        }
    }

    return 0;
}

static void EnableHSI(void)
{
    /* すでに有効化されている場合は何もしない */
    if (LL_RCC_HSI_IsReady() != 1)
    {
        LL_RCC_HSI_Enable();
        while (LL_RCC_HSI_IsReady() != 1);
    }
}

static void DisablePLL(void)
{
    if (LL_RCC_PLL_IsReady() != 0)
    {
        LL_RCC_PLL_Disable();
        while(LL_RCC_PLL_IsReady() != 0);
    }
}

static void EnablePLL(void)
{
    if (LL_RCC_PLL_IsReady() != 1)
    {
        LL_RCC_PLL_Enable();
        while(LL_RCC_PLL_IsReady() != 1);
    }
}

void ConfigureClockForRNG(void)
{
    /* (内蔵オシレータ) --> (HSI) --> (RCC AHB2) の順にクロック供給させる  */
    /* RCC AHB2クロックを有効化 */
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_RNG);
    /* RCC AHB2クロックに供給しているHSIクロックを有効化 */
    EnableHSI();

    /* 内蔵16MHzオシレータからHSIクロックへ供給 */
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSI);
    SystemCoreClock = HSI_VALUE;

    /* PLL の無効化 */
    DisablePLL();

    /* RNGへの供給はPLLから行う */
    /* RNGを動作させるためのクロック周波数は<=48MHzなのでPLLから供給されるクロックを48MHzに設定する。クロックソースはHSE(8MHz)を使用する */
    /* (RNG供給クロック) = (((HSE Freq / PLLM) * PLLN) / PLLQ)            */
    /*                  = (((8000000  /  8  ) * 336)  /  7  ) = 48000000 */
    LL_RCC_PLL_ConfigDomain_48M(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_8, 336, LL_RCC_PLLQ_DIV_7);

    LL_FLASH_SetLatency(LL_FLASH_LATENCY_5);

    /* PLL の有効化 */
    EnablePLL();

    /* システムクロックのソースをPLLからに設定 */
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
    while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL);

    /* 1ms 単位でシステムクロックの割り込みが入るように設定 */
    SysTick_Config(SYSTEM_CLOCK_FREQUENCY / 1000);

    /* CMSIS側のクロックの周波数の変数値も修正しておく */
    SystemCoreClock = SYSTEM_CLOCK_FREQUENCY;
}
