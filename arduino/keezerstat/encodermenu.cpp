#include <avr/pgmspace.h>
#include "encodermenu.h"

#define CENTER_LONG_DURATION 500

volatile static int8_t encoderOutput;

static void readRotary()
{
  static uint8_t old_AB = 3;  //lookup table index
  static int8_t encval = 0;   //encoder value  
  static const int8_t enc_states [] =
  {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};  //encoder lookup table

  old_AB <<=2;  // shift up previous state
  old_AB |= ((PIND >> 2) & 0x03); // PD2 and PD3, aka D2/D3
  encval += enc_states[old_AB & 0x0f];
  if( encval > 3 ) {  //four steps forward
    encval = 0;
    ++encoderOutput;
  }
  else if( encval < -3 ) {  //four steps backwards
    encval = 0;
    --encoderOutput;
  }
}

ISR(INT0_vect) { readRotary(); }
ISR(INT1_vect) { readRotary(); }

EncoderMenu::EncoderMenu(uint8_t centerpin, const menu_definition_t *defs, const menu_transition_t *trans) 
  : _definitions(defs), _transitions(trans), _center(centerpin, 20), _encoderScale(1)
{
  // pins are hooked to INT0 and INT1
  pinMode(2, INPUT);
  pinMode(3, INPUT);
  // Interrupt on any change
  EICRA = bit(ISC10) | bit(ISC00);
  // Interrupts go
  EIMSK = bit(INT1) | bit(INT0);
}

unsigned long EncoderMenu::getElapsedDuration(void) const
{
  return millis() - _lastActivity;
}

unsigned long EncoderMenu::getTimeoutDuration(void) const
{
  return (_currMenu) ? (unsigned long)pgm_read_byte(&_currMenu->timeout) * 1000 : 0;
}

void EncoderMenu::update(void)
{
  while (encoderOutput >= _encoderScale)
  {
    encoderOutput -= _encoderScale;
    newButtonEvent(BUTTON_RIGHT);
  }
  while (encoderOutput <= (-_encoderScale))
  {
    encoderOutput += _encoderScale;
    newButtonEvent(BUTTON_LEFT);
  }

  _center.update();
  if (!_lastWasLongPress && _center.read() == LOW && _center.duration() > CENTER_LONG_DURATION)
  {
    _lastWasLongPress = true;
    newButtonEvent(BUTTON_LONG);
  }
  else if (_center.risingEdge())
  {
    if (_lastWasLongPress)
      _lastWasLongPress = false;
    else
      newButtonEvent(BUTTON_CENTER);
  }

  unsigned long dur = getTimeoutDuration();
  if (dur != 0 && getElapsedDuration() > dur)
      newButtonEvent(BUTTON_TIMEOUT);
}

handler_t EncoderMenu::getHandler(void) const
{
  return (_currMenu) ? (handler_t)pgm_read_word(&_currMenu->handler) : 0;
}

state_t EncoderMenu::findTransition(button_t button) const
{
  const menu_transition_t *trans = _transitions;
  state_t lookup;
  while ((lookup = pgm_read_byte(&trans->state)))
  {
    if (lookup == _state)
    {
      button_t transButton = pgm_read_byte(&trans->button);
      if ((button & transButton) == button)
        return pgm_read_byte(&trans->newstate);
    }
    ++trans;
  }
  return _state;
}

void EncoderMenu::setState(state_t state)
{
  //Serial.print(F("setState: ")); Serial.print(state, DEC); 
  //Serial.print(F(" old ")); Serial.println(_state, DEC); 
  
  while (state > ST_VMAX && state != _state)
  {
    handler_t handler = getHandler();
    if (handler)
      handler(BUTTON_LEAVE);

    _state = state;
    _currMenu = _definitions;

    state_t lookup;
    while ((lookup = pgm_read_byte(&_currMenu->state)))
    {
      if (lookup == _state)
        break;
      ++_currMenu;
    }
    
    if (_currMenu)
    {
      handler = getHandler();
      if (handler)
        state = handler(BUTTON_ENTER);
    } else {
      state = ST_NONE;
    }
  }  // while state changing
}

void EncoderMenu::newButtonEvent(button_t e)
{
  //Serial.print(F("Btn: ")); Serial.println(e, HEX);
  _lastActivity = millis();
  _lastButton = e;

  state_t newState = ST_AUTO;
  handler_t handler = getHandler();
  if (handler != NULL)
    newState = handler(e);
  if (newState == ST_AUTO)
    newState = findTransition(e);
  if (newState != ST_AUTO)
    setState(newState);
}
