#ifndef _RGBCONTROL_H_
#define _RGBCONTROL_H_

#include <inttypes.h>

class RgbControl
{
public:
  RgbControl(const uint8_t pin_r, const uint8_t pin_g, const uint8_t pin_b)
    : _pin_r(pin_r), _pin_g(pin_g), _pin_b(pin_b), 
    _transitionDuration(1500), _transitionPct(100), _brightness(100)
  { 
  };
  
  void set(uint8_t r, uint8_t g, uint8_t b);
  void set(uint32_t rgb);
  void update(void);
  void setBrightness(uint8_t brightness);
  uint8_t getBrightness(void) const { return _brightness; }
    
private:
  union color_t {
    uint8_t v[3];
    struct {
      uint8_t r;
      uint8_t g;
      uint8_t b;
    };
  };
  const uint8_t _pin_r;
  const uint8_t _pin_g;
  const uint8_t _pin_b;
  const uint32_t _transitionDuration;

  color_t _lastColor;
  color_t _targetColor;

  uint32_t _transitionBegin;
  uint8_t _transitionPct;
  uint8_t _brightness;

  void colorRamp(color_t *r);
  void internalSet(const color_t &c);
};

#endif /* _RGBCONTROL_H_ */
