#ifndef __KEEZERSTAT_H__
#define __KEEZERSTAT_H__

#include "LiquidCrystal.h"
#include "rgbcontrol.h"
#include "lcdcolors.h"
#include "fcmenu.h"

enum ThermoState { THERMO_NOPROBE, THERMO_OFF, THERMO_MRT, THERMO_ON, THERMO_MOT };
enum TrendDir { TREND_NEUTRAL, TREND_UP, TREND_DOWN };

void uploadChars(void);
void displayHome(void);

void setSetPoint(int16_t sp);
void configChanged(void);
void configReset(void);

extern ThermoState g_ThermoState;
extern int16_t g_SetPoint;
extern int8_t g_CutinOffset;
extern int8_t g_CutoutOffset;
extern char g_Units;
extern uint8_t g_MinOffTime;
extern uint8_t g_MinRunTime;
extern state_t g_LastHome;

extern LiquidCrystal lcd;
extern RgbControl rgb;
extern EncoderMenu menu;

#endif  /* __FRIDGECONTROL_H__ */
