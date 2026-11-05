#include "led.h"

void LED0_ON(void)
{
    gpio_bits_reset(LED0_GPIO_PORT, LED0_PIN);
}

void LED0_OFF(void)
{
    gpio_bits_set(LED0_GPIO_PORT, LED0_PIN);
}

void LED0_TOGGLE(void)
{
    gpio_bits_toggle(LED0_GPIO_PORT, LED0_PIN);
}

void LED1_ON(void)
{
    gpio_bits_reset(LED1_GPIO_PORT, LED1_PIN);
}

void LED1_OFF(void)
{
    gpio_bits_set(LED1_GPIO_PORT, LED1_PIN);
}

void LED1_TOGGLE(void)
{
    gpio_bits_toggle(LED1_GPIO_PORT, LED1_PIN);
}

void LED2_ON(void)
{
    gpio_bits_reset(LED2_GPIO_PORT, LED2_PIN);
}

void LED2_OFF(void)
{
    gpio_bits_set(LED2_GPIO_PORT, LED2_PIN);
}

void LED2_TOGGLE(void)
{
    gpio_bits_toggle(LED2_GPIO_PORT, LED2_PIN);
}
