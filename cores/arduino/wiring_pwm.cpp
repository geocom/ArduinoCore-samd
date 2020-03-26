/**
 * The MIT License (MIT)
 * 
 * Author: Hongtai Liu (lht856@foxmail.com)
 * 
 * Copyright (C) 2020  Seeed Technology Co.,Ltd. 
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "wiring_pwm.h"
#include "wiring_private.h"

extern uint32_t toneMaxFrequency;

#if defined(__SAMD51__)
#define PER_COUNTER 0xFF
#else
#define PER_COUNTER 0xFFFF
#endif

static inline uint32_t calcPrescaler(uint32_t frequency)
{
  //if it's a rest, set to 1Hz (below audio range)
  frequency = (frequency > 0 ? frequency : 1);
  //
  // Calculate best prescaler divider and comparator value for a 16 bit TC peripheral
  uint32_t prescalerConfigBits;
  uint32_t ccValue;

  ccValue = toneMaxFrequency / frequency - 1;
  prescalerConfigBits = TC_CTRLA_PRESCALER_DIV1;

  uint8_t i = 0;

  while (ccValue > PER_COUNTER)
  {
    ccValue = toneMaxFrequency / frequency / (2 << i) - 1;
    i++;
    if (i == 4 || i == 6 || i == 8) //DIV32 DIV128 and DIV512 are not available
      i++;
  }

  switch (i - 1)
  {
  case 0:
    prescalerConfigBits = TC_CTRLA_PRESCALER_DIV2;
    break;

  case 1:
    prescalerConfigBits = TC_CTRLA_PRESCALER_DIV4;
    break;

  case 2:
    prescalerConfigBits = TC_CTRLA_PRESCALER_DIV8;
    break;

  case 3:
    prescalerConfigBits = TC_CTRLA_PRESCALER_DIV16;
    break;

  case 5:
    prescalerConfigBits = TC_CTRLA_PRESCALER_DIV64;
    break;

  case 7:
    prescalerConfigBits = TC_CTRLA_PRESCALER_DIV256;
    break;

  case 9:
    prescalerConfigBits = TC_CTRLA_PRESCALER_DIV1024;
    break;

  default:
    break;
  }

  return TC_CTRLA_PRESCALER_DIV1;
}

void pwm(uint32_t outputPin, uint32_t frequency, uint32_t duty)
{
  PinDescription pinDesc = g_APinDescription[outputPin];
  uint32_t attr = pinDesc.ulPinAttribute;

#if defined(__SAMD51__)
  if (attr & (PIN_ATTR_PWM_E | PIN_ATTR_PWM_F | PIN_ATTR_PWM_G))
  {
    duty = mapResolution(duty, 8, 8);
    uint32_t prescalerConfigBits;

    prescalerConfigBits = calcPrescaler(frequency);

    uint32_t tcNum = GetTCNumber(pinDesc.ulPWMChannel);
    uint8_t tcChannel = GetTCChannelNumber(pinDesc.ulPWMChannel);

    if (attr & PIN_ATTR_PWM_E)
      pinPeripheral(outputPin, PIO_TIMER);
    else if (attr & PIN_ATTR_PWM_F)
      pinPeripheral(outputPin, PIO_TIMER_ALT);
    else if (attr & PIN_ATTR_PWM_G)
      pinPeripheral(outputPin, PIO_TCC_PDEC);

    GCLK->PCHCTRL[GCLK_CLKCTRL_IDs[tcNum]].reg = GCLK_PCHCTRL_GEN_GCLK0_Val | (1 << GCLK_PCHCTRL_CHEN_Pos); //use clock generator 0

    // Set PORT
    if (tcNum >= TCC_INST_NUM)
    {
      // -- Configure TC
      Tc *TCx = (Tc *)GetTC(pinDesc.ulPWMChannel);

      //reset
      TCx->COUNT8.CTRLA.bit.SWRST = 1;
      while (TCx->COUNT8.SYNCBUSY.bit.SWRST)
        ;

      // Disable TCx
      TCx->COUNT8.CTRLA.bit.ENABLE = 0;
      while (TCx->COUNT8.SYNCBUSY.bit.ENABLE)
        ;
      // Set Timer counter Mode to 8 bits, normal PWM,
      TCx->COUNT8.CTRLA.reg = TC_CTRLA_MODE_COUNT8 | prescalerConfigBits;
      TCx->COUNT8.WAVE.reg = TC_WAVE_WAVEGEN_NPWM;

      while (TCx->COUNT8.SYNCBUSY.bit.CC0)
        ;
      // Set the initial value
      TCx->COUNT8.CC[tcChannel].reg = (uint8_t)duty;
      while (TCx->COUNT8.SYNCBUSY.bit.CC0)
        ;
      // Set PER to maximum counter value (resolution : 0xFF)
      TCx->COUNT8.PER.reg = 0xFF;
      while (TCx->COUNT8.SYNCBUSY.bit.PER)
        ;
      // Enable TCx
      TCx->COUNT8.CTRLA.bit.ENABLE = 1;
      while (TCx->COUNT8.SYNCBUSY.bit.ENABLE)
        ;
    }
    else
    {
      // -- Configure TCC
      Tcc *TCCx = (Tcc *)GetTC(pinDesc.ulPWMChannel);

      TCCx->CTRLA.bit.SWRST = 1;
      while (TCCx->SYNCBUSY.bit.SWRST)
        ;

      // Disable TCCx
      TCCx->CTRLA.bit.ENABLE = 0;
      while (TCCx->SYNCBUSY.bit.ENABLE)
        ;
      // Set prescaler
      TCCx->CTRLA.reg = prescalerConfigBits | TCC_CTRLA_PRESCSYNC_GCLK;

      // Set TCx as normal PWM
      TCCx->WAVE.reg = TCC_WAVE_WAVEGEN_NPWM;
      while (TCCx->SYNCBUSY.bit.WAVE)
        ;

      while (TCCx->SYNCBUSY.bit.CC0 || TCCx->SYNCBUSY.bit.CC1)
        ;
      // Set the initial value
      TCCx->CC[tcChannel].reg = (uint32_t)duty;
      while (TCCx->SYNCBUSY.bit.CC0 || TCCx->SYNCBUSY.bit.CC1)
        ;
      // Set PER to maximum counter value (resolution : 0xFF)
      TCCx->PER.reg = 0xFF;
      while (TCCx->SYNCBUSY.bit.PER)
        ;
      // Enable TCCx
      TCCx->CTRLA.bit.ENABLE = 1;
      while (TCCx->SYNCBUSY.bit.ENABLE)
        ;
    }
    return;
  }

#else

  if ((attr & PIN_ATTR_PWM) == PIN_ATTR_PWM)
  {
    duty = mapResolution(duty, 8, 16);
    uint32_t prescalerConfigBits;

    prescalerConfigBits = calcPrescaler(frequency);

    uint32_t tcNum = GetTCNumber(pinDesc.ulPWMChannel);
    uint8_t tcChannel = GetTCChannelNumber(pinDesc.ulPWMChannel);

    if (attr & PIN_ATTR_TIMER)
    {
#if !(ARDUINO_SAMD_VARIANT_COMPLIANCE >= 10603)
      // Compatibility for cores based on SAMD core <=1.6.2
      if (pinDesc.ulPinType == PIO_TIMER_ALT)
      {
        pinPeripheral(pin, PIO_TIMER_ALT);
      }
      else
#endif
      {
        pinPeripheral(pin, PIO_TIMER);
      }
    }
    else if ((attr & PIN_ATTR_TIMER_ALT) == PIN_ATTR_TIMER_ALT)
    {
      //this is on an alt timer
      pinPeripheral(pin, PIO_TIMER_ALT);
    }
    else
    {
      return;
    }

    uint16_t GCLK_CLKCTRL_IDs[] = {
        GCLK_CLKCTRL_ID(GCM_TCC0_TCC1), // TCC0
        GCLK_CLKCTRL_ID(GCM_TCC0_TCC1), // TCC1
        GCLK_CLKCTRL_ID(GCM_TCC2_TC3),  // TCC2
        GCLK_CLKCTRL_ID(GCM_TCC2_TC3),  // TC3
        GCLK_CLKCTRL_ID(GCM_TC4_TC5),   // TC4
        GCLK_CLKCTRL_ID(GCM_TC4_TC5),   // TC5
        GCLK_CLKCTRL_ID(GCM_TC6_TC7),   // TC6
        GCLK_CLKCTRL_ID(GCM_TC6_TC7),   // TC7
    };
    GCLK->CLKCTRL.reg = (uint16_t)(GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_IDs[tcNum]);
    while (GCLK->STATUS.bit.SYNCBUSY == 1)
      ;

    // Set PORT
    if (tcNum >= TCC_INST_NUM)
    {
      // -- Configure TC
      Tc *TCx = (Tc *)GetTC(pinDesc.ulPWMChannel);
      // Disable TCx
      TCx->COUNT16.CTRLA.bit.ENABLE = 0;
      syncTC_16(TCx);
      // Set Timer counter Mode to 16 bits, normal PWM
      TCx->COUNT16.CTRLA.reg |= TC_CTRLA_MODE_COUNT16 | TC_CTRLA_WAVEGEN_NPWM | prescalerConfigBits;
      syncTC_16(TCx);
      // Set the initial value
      TCx->COUNT16.CC[tcChannel].reg = (uint32_t)duty;
      syncTC_16(TCx);
      // Enable TCx
      TCx->COUNT16.CTRLA.bit.ENABLE = 1;
      syncTC_16(TCx);
    }
    else
    {
      // -- Configure TCC
      Tcc *TCCx = (Tcc *)GetTC(pinDesc.ulPWMChannel);
      // Disable TCCx
      TCCx->CTRLA.bit.ENABLE = 0;
      syncTCC(TCCx);
      // Set TCCx as normal PWM
      TCCx->WAVE.reg |= TCC_WAVE_WAVEGEN_NPWM;
      syncTCC(TCCx);
      // Set the initial value
      TCCx->CC[tcChannel].reg = (uint32_t)duty;
      syncTCC(TCCx);
      // Set PER to maximum counter value (resolution : 0xFFFF)
      TCCx->PER.reg = 0xFFFF;
      syncTCC(TCCx);
      // Enable TCCx
      TCCx->CTRLA.bit.ENABLE = 1;
      syncTCC(TCCx);
    }
    return;
  }
#endif
}

void noPwm(uint32_t outputPin)
{
  PinDescription pinDesc = g_APinDescription[outputPin];
  uint32_t attr = pinDesc.ulPinAttribute;

#if defined(__SAMD51__)
  if (attr & (PIN_ATTR_PWM_E | PIN_ATTR_PWM_F | PIN_ATTR_PWM_G))
  {

    uint32_t tcNum = GetTCNumber(pinDesc.ulPWMChannel);
    uint8_t tcChannel = GetTCChannelNumber(pinDesc.ulPWMChannel);

    if (attr & PIN_ATTR_PWM_E)
      pinPeripheral(outputPin, PIO_TIMER);
    else if (attr & PIN_ATTR_PWM_F)
      pinPeripheral(outputPin, PIO_TIMER_ALT);
    else if (attr & PIN_ATTR_PWM_G)
      pinPeripheral(outputPin, PIO_TCC_PDEC);

    GCLK->PCHCTRL[GCLK_CLKCTRL_IDs[tcNum]].reg = GCLK_PCHCTRL_GEN_GCLK0_Val | (1 << GCLK_PCHCTRL_CHEN_Pos); //use clock generator 0

    // Set PORT
    if (tcNum >= TCC_INST_NUM)
    {
      // -- Configure TC
      Tc *TCx = (Tc *)GetTC(pinDesc.ulPWMChannel);

      //reset
      TCx->COUNT8.CTRLA.bit.SWRST = 1;
      while (TCx->COUNT8.SYNCBUSY.bit.SWRST)
        ;

      // Disable TCx
      TCx->COUNT8.CTRLA.bit.ENABLE = 0;
      while (TCx->COUNT8.SYNCBUSY.bit.ENABLE)
        ;
    }
    else
    {
      // -- Configure TCC
      Tcc *TCCx = (Tcc *)GetTC(pinDesc.ulPWMChannel);

      TCCx->CTRLA.bit.SWRST = 1;
      while (TCCx->SYNCBUSY.bit.SWRST)
        ;

      // Disable TCCx
      TCCx->CTRLA.bit.ENABLE = 0;
      while (TCCx->SYNCBUSY.bit.ENABLE)
        ;
    }
    return;
  }

#else
  if ((attr & PIN_ATTR_PWM) == PIN_ATTR_PWM)
  {

    uint32_t tcNum = GetTCNumber(pinDesc.ulPWMChannel);
    uint8_t tcChannel = GetTCChannelNumber(pinDesc.ulPWMChannel);

    if (attr & PIN_ATTR_TIMER)
    {
#if !(ARDUINO_SAMD_VARIANT_COMPLIANCE >= 10603)
      // Compatibility for cores based on SAMD core <=1.6.2
      if (pinDesc.ulPinType == PIO_TIMER_ALT)
      {
        pinPeripheral(pin, PIO_TIMER_ALT);
      }
      else
#endif
      {
        pinPeripheral(pin, PIO_TIMER);
      }
    }
    else if ((attr & PIN_ATTR_TIMER_ALT) == PIN_ATTR_TIMER_ALT)
    {
      //this is on an alt timer
      pinPeripheral(pin, PIO_TIMER_ALT);
    }
    else
    {
      return;
    }

    uint16_t GCLK_CLKCTRL_IDs[] = {
        GCLK_CLKCTRL_ID(GCM_TCC0_TCC1), // TCC0
        GCLK_CLKCTRL_ID(GCM_TCC0_TCC1), // TCC1
        GCLK_CLKCTRL_ID(GCM_TCC2_TC3),  // TCC2
        GCLK_CLKCTRL_ID(GCM_TCC2_TC3),  // TC3
        GCLK_CLKCTRL_ID(GCM_TC4_TC5),   // TC4
        GCLK_CLKCTRL_ID(GCM_TC4_TC5),   // TC5
        GCLK_CLKCTRL_ID(GCM_TC6_TC7),   // TC6
        GCLK_CLKCTRL_ID(GCM_TC6_TC7),   // TC7
    };
    GCLK->CLKCTRL.reg = (uint16_t)(GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_IDs[tcNum]);
    while (GCLK->STATUS.bit.SYNCBUSY == 1)
      ;

    // Set PORT
    if (tcNum >= TCC_INST_NUM)
    {
      // -- Configure TC
      Tc *TCx = (Tc *)GetTC(pinDesc.ulPWMChannel);
      // Disable TCx
      TCx->COUNT16.CTRLA.bit.ENABLE = 0;
      syncTC_16(TCx);
    }
    else
    {
      // -- Configure TCC
      Tcc *TCCx = (Tcc *)GetTC(pinDesc.ulPWMChannel);
      // Disable TCCx
      TCCx->CTRLA.bit.ENABLE = 0;
      syncTCC(TCCx);
    }
    return;
  }
#endif
}