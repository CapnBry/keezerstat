#ifndef __ENCODERMENU_H__
#define __ENCODERMENU_H__

#include "Arduino.h"
#include "Bounce.h"

#define BUTTON_NONE    bit(0)
#define BUTTON_ENTER   bit(1)
#define BUTTON_LEAVE   bit(2)
#define BUTTON_TIMEOUT bit(3)
#define BUTTON_LEFT    bit(4)
#define BUTTON_RIGHT   bit(5)
#define BUTTON_CENTER  bit(6)
#define BUTTON_LONG    bit(7)

#define ST_NONE     0
#define ST_AUTO     1
#define ST_VMAX     1

typedef unsigned char state_t;
typedef unsigned char button_t;

typedef state_t (*handler_t)(button_t button);
typedef button_t (*buttonread_t)(void);

typedef struct tagMenuDefinition
{
  state_t state;
  handler_t handler;
  unsigned char timeout;
} menu_definition_t;

typedef struct tagMenuTransition
{
  state_t state;
  button_t button;
  state_t newstate;
} menu_transition_t;

class EncoderMenu
{
public:
  EncoderMenu(uint8_t centerpin, 
    const menu_definition_t *defs, const menu_transition_t *trans);

  void update(void);
  void setState(state_t state);
  state_t getState(void) const { return _state; }
  button_t getButton(void) const { return _lastButton; }
  void setEncoderScale(uint8_t value) { _encoderScale = value; }
  uint8_t getEncoderScale(void) const { return _encoderScale; }
private:
  const menu_definition_t *_definitions;
  const menu_transition_t *_transitions;
  const menu_definition_t *_currMenu;
  state_t _state;
  button_t _lastButton;
  unsigned long _lastActivity;
  Bounce _center;
  bool _lastWasLongPress;
  uint8_t _encoderScale;

  unsigned long getTimeoutDuration(void) const;
  unsigned long getElapsedDuration(void) const;
  void newButtonEvent(button_t e);
  handler_t getHandler(void) const;
  state_t findTransition(button_t button) const;
};
#endif /* __ENCODERMENU_H__ */