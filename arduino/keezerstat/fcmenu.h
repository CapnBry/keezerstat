#ifndef __FCMENU_H__
#define __FCMENU_H__

#include "encodermenu.h"

enum FcMenuStates {
  ST_VERSION = (ST_VMAX+1),
  ST_HOME_SMART,
  ST_HOME_FIRST,
  ST_HOME_GRAPH,
  ST_HOME_BIGN,
  ST_HOME_LAST_DUR,
  ST_HOME_LAST_PEAK,
  ST_HOME_LAST,
  ST_SETPOINT,
  ST_UNITS,
  ST_CUTIN,
  ST_CUTOUT,
  ST_MOT,
  ST_MRT,
  ST_LCD_BRIGHTNESS,
  ST_RESET_CONFIG,
};

extern const menu_definition_t fcmenu_definitions[];
extern const menu_transition_t fcmenu_transitions[];

#endif /* __FCMENU_H__ */