#include "Arduino.h"
#include "rgbcontrol.h"

void RgbControl::set(uint8_t r, uint8_t g, uint8_t b)
{
  // If we're currently in a ramp, adjust our last color to the current color
  // of the ramp instead of the target
  colorRamp(&_lastColor);
  _targetColor.r = r;
  _targetColor.g = g;
  _targetColor.b = b;
  _transitionPct = 0;
  _transitionBegin = millis();
  //Serial.print(F("RGB begin:")); Serial.println(_transitionBegin, DEC);
}

void RgbControl::set(uint32_t rgb)
{
  set(rgb >> 16, rgb >> 8, rgb);
}

void RgbControl::internalSet(const color_t &c)
{
  analogWrite(_pin_r, c.r * _brightness / 100U);
  analogWrite(_pin_g, c.g * _brightness / 100U);
  analogWrite(_pin_b, c.b * _brightness / 100U);
}

void RgbControl::colorRamp(color_t *r)
{
  for (uint8_t i=0; i<3; ++i)
    r->v[i] = (((int16_t)_targetColor.v[i] - (int16_t)_lastColor.v[i]) * _transitionPct / 100) + _lastColor.v[i];
    //c.v[i] = ((100 - _transitionPct) * (int16_t)_lastColor.v[i] + _transitionPct * (int16_t)_targetColor.v[i]) / 100;
}

void RgbControl::update(void)
{
  if (_transitionPct >= 100)
    return;
  
  _transitionPct = ((millis() - _transitionBegin) * 100U) / _transitionDuration;
  if (_transitionPct >= 100)
  {
    _transitionPct = 100;
    //Serial.print(F("RGB end:")); Serial.print(millis(), DEC); Serial.print('\n');
  }

  color_t c;
  colorRamp(&c);
  internalSet(c);
}

void RgbControl::setBrightness(uint8_t brightness)
{
  _brightness = brightness;
  internalSet(_lastColor);
}
