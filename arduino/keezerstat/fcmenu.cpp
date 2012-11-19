#include "fcmenu.h"
#include "keezerstat.h"

#define DEGREE "\xdf"
#define RTARROW "\x7e"
#define LTARROW "\x7f"

static int editInt;
static boolean isEditing;

static state_t menuEditCommon(button_t button)
{
  if (isEditing)
  {
    lcd.print(LTARROW[0]);
    // Return auto if this is a longpress or a timeout, else lock us here
    return (button & (BUTTON_TIMEOUT | BUTTON_LONG)) ? ST_AUTO : ST_NONE;
  }
  else
  {
    lcd.print(' ');
    return ST_AUTO;
  }
}

static state_t menuNumberEdit(button_t button, unsigned char increment,
  int minVal, int maxVal, const prog_char *format)
{
  char buffer[17];
  rgb.set(WHITE);
  
  if (button == BUTTON_ENTER)
    isEditing = false;
  else if (button == BUTTON_CENTER)
    isEditing = !isEditing;

  lcd.setCursor(0, 1);
  if (isEditing)
  {
    if (button == BUTTON_RIGHT)
      editInt += increment;
    else if (button == BUTTON_LEFT)
      editInt -= increment;
    if (editInt < minVal)
      editInt = minVal;
    if (editInt > maxVal)
      editInt = maxVal;

    lcd.print(RTARROW[0]);
  }
  else
    lcd.print(' ');

  snprintf_P(buffer, sizeof(buffer), format, editInt, g_Units);
  lcd.print(buffer);
  return menuEditCommon(button);
}

static state_t menuBooleanEdit(button_t button, const prog_char *preamble,
  const prog_char *zero, const prog_char *one)
{
  char buffer[17];
  if (button == BUTTON_ENTER)
    isEditing = false;
  else if (button == BUTTON_CENTER)
    isEditing = !isEditing;

  lcd.setCursor(0, 1);
  if (preamble != NULL)
  {
    memcpy_P(buffer, preamble, sizeof(buffer));
    lcd.print(buffer);
  }

  if (isEditing)
  {
    if (button == BUTTON_LEFT || button == BUTTON_RIGHT)
      editInt = !editInt;
    lcd.print(RTARROW[0]);
  }
  else
    lcd.print(' ');

  const prog_char *p;
  if (editInt)
    p = one;
  else
    p = zero;
  memcpy_P(buffer, p, sizeof(buffer));
  lcd.print(buffer);

  return menuEditCommon(button);
}

static state_t menuHome(button_t button)
{
  if (button == BUTTON_ENTER)
  {
    g_LastHome = menu.getState();
    //Serial.print(F("Enter home ")); Serial.println(g_LastHome, DEC);
    uploadChars();
  }
  else if (button & (BUTTON_LONG | BUTTON_CENTER))
    return ST_SETPOINT;
  else if (button == BUTTON_LEFT)
  {
    state_t newHome = g_LastHome - 1;
    if (newHome <= ST_HOME_FIRST)
      return ST_HOME_LAST - 1;
    else
      return newHome;
  }
  else if (button == BUTTON_RIGHT)
  {
    state_t newHome = g_LastHome + 1;
    if (newHome >= ST_HOME_LAST)
      return ST_HOME_FIRST + 1;
    else
      return newHome;
  }

  if (button & (BUTTON_ENTER | BUTTON_TIMEOUT))
  {
    displayHome();
  }

  return ST_AUTO;
}

static state_t menuSetpoint(button_t button)
{
  if (button == BUTTON_ENTER)
  {
    lcd.clear();
    lcd.print(F("Cooling setpoint"));
    editInt = g_SetPoint;
  }
  else if (isEditing && button == BUTTON_CENTER)
  {
    setSetPoint(editInt);
  }

  return menuNumberEdit(button, 1, -40, 100, PSTR(" %3d"DEGREE"%c "));
}

static state_t menuUnits(button_t button)
{
  if (button == BUTTON_ENTER)
  {
    lcd.clear();
    lcd.print(F("Temperature unit"));
    if (g_Units == 'F')
      editInt = 0;
    else
      editInt = !0;
  }
  else if (isEditing && button == BUTTON_CENTER)
  {
    if (editInt)
      g_Units = 'C';
    else
      g_Units = 'F';
    configChanged();
  }

  return menuBooleanEdit(button, NULL, PSTR(" Fahrenheit "), PSTR(" Celcius    "));
}

static state_t menuCutInDifferential(button_t button)
{
  if (button == BUTTON_ENTER)
  {
    lcd.clear();
    lcd.print(F("Cut-in diff"));
    editInt = g_CutinOffset;
  }
  else if (isEditing && button == BUTTON_CENTER)
  {
    g_CutinOffset = editInt;
    configChanged();
  }

  return menuNumberEdit(button, 1, 0, 30, PSTR(" %3d"DEGREE"%c "));
}

static state_t menuCutOutDifferential(button_t button)
{
  if (button == BUTTON_ENTER)
  {
    lcd.clear();
    lcd.print(F("Cutout diff"));
    editInt = g_CutoutOffset;
  }
  else if (isEditing && button == BUTTON_CENTER)
  {
    g_CutoutOffset = editInt;
    configChanged();
  }

  return menuNumberEdit(button, 1, 0, 30, PSTR(" %3d"DEGREE"%c "));
}

static state_t menuMinOffTime(button_t button)
{
  if (button == BUTTON_ENTER)
  {
    lcd.clear();
    lcd.print(F("Min OFF time"));
    editInt = g_MinOffTime;
  }
  else if (isEditing && button == BUTTON_CENTER)
  {
    g_MinOffTime = editInt;
    configChanged();
  }
  
  return menuNumberEdit(button, 1, 0, 30, PSTR(" %3d minutes "));
}

static state_t menuMinRunTime(button_t button)
{
  if (button == BUTTON_ENTER)
  {
    lcd.clear();
    lcd.print(F("Min ON time"));
    editInt = g_MinRunTime;
  }
  else if (isEditing && button == BUTTON_CENTER)
  {
    g_MinRunTime = editInt;
    configChanged();
  }

  return menuNumberEdit(button, 1, 0, 30, PSTR(" %3d minutes "));
}

static state_t menuHomeSmart(button_t button)
{
  if (button == BUTTON_ENTER)
  {
    switch (g_ThermoState)
    {
      case THERMO_NOPROBE:
        rgb.set(RED);
        break;
      case THERMO_OFF:
        rgb.set(GREEN);
        break;
      case THERMO_MOT:
        rgb.set(ORANGE);
        break;
      case THERMO_MRT:
        rgb.set(TEAL);
        break;
      case THERMO_ON:
        rgb.set(LTBLUE);
        break;
    }
  } /* if enter */
  return g_LastHome;
}

static state_t menuVersion(button_t button)
{
  if (button == BUTTON_ENTER)
  {
    lcd.clear();
    lcd.print(F("   KeezerStat"));
    lcd.setCursor(0, 1);
    lcd.print(F(__DATE__));
    rgb.set(YELLOW);
  }
  return ST_AUTO;
}

static state_t menuResetConfig(button_t button)
{
  if (button == BUTTON_ENTER)
  {
    lcd.clear();
    lcd.print(F("Reset Config?"));
    editInt = 0;
  }
  else if (isEditing && button == BUTTON_CENTER)
  {
    if (editInt)
      configReset();
    return ST_HOME_SMART;
  }

  return menuBooleanEdit(button, NULL, PSTR(" No  "), PSTR(" Yes "));
}

state_t menuLcdBrightness(button_t button)
{
  if (button == BUTTON_ENTER)
  {
    lcd.clear();
    lcd.print(F("LCD brightness"));
    editInt = rgb.getBrightness();
  }
  else if (isEditing && button == BUTTON_CENTER)
    configChanged();
  
  state_t r = menuNumberEdit(button, 10, 10, 100, PSTR(" %3d%% "));
  if (isEditing && button & (BUTTON_LEFT | BUTTON_RIGHT))
    rgb.setBrightness(editInt);
  return r;
}

#define SETTING_TIMEOUT 4
const menu_definition_t fcmenu_definitions[] PROGMEM = {
  { ST_VERSION, menuVersion, 2 },
  { ST_HOME_SMART, menuHomeSmart, 0 },
  { ST_HOME_GRAPH, menuHome, 1 },
  { ST_HOME_BIGN, menuHome, 1 },
  { ST_HOME_LAST_DUR, menuHome, 1 },
  { ST_HOME_LAST_PEAK, menuHome, 1 },
  { ST_SETPOINT, menuSetpoint, SETTING_TIMEOUT },
  { ST_UNITS, menuUnits, SETTING_TIMEOUT },
  { ST_CUTIN, menuCutInDifferential, SETTING_TIMEOUT },
  { ST_CUTOUT, menuCutOutDifferential, SETTING_TIMEOUT },
  { ST_MOT, menuMinOffTime, SETTING_TIMEOUT },
  { ST_MRT, menuMinRunTime, SETTING_TIMEOUT },
  { ST_LCD_BRIGHTNESS, menuLcdBrightness, SETTING_TIMEOUT },
  { ST_RESET_CONFIG, menuResetConfig, SETTING_TIMEOUT }, 
  { 0, 0 }
};

const menu_transition_t fcmenu_transitions[] PROGMEM = {
  { ST_VERSION, BUTTON_LEFT | BUTTON_RIGHT | BUTTON_CENTER | BUTTON_TIMEOUT, ST_HOME_SMART },

  { ST_SETPOINT, BUTTON_LONG | BUTTON_TIMEOUT, ST_HOME_SMART },
  { ST_SETPOINT, BUTTON_LEFT, ST_RESET_CONFIG },
  { ST_SETPOINT, BUTTON_RIGHT, ST_UNITS },

  { ST_UNITS, BUTTON_LONG | BUTTON_TIMEOUT, ST_HOME_SMART },
  { ST_UNITS, BUTTON_LEFT, ST_SETPOINT },
  { ST_UNITS, BUTTON_RIGHT, ST_CUTIN },

  { ST_CUTIN, BUTTON_LONG | BUTTON_TIMEOUT, ST_HOME_SMART },
  { ST_CUTIN, BUTTON_LEFT, ST_UNITS },
  { ST_CUTIN, BUTTON_RIGHT, ST_CUTOUT },

  { ST_CUTOUT, BUTTON_LONG | BUTTON_TIMEOUT, ST_HOME_SMART },
  { ST_CUTOUT, BUTTON_LEFT, ST_CUTIN },
  { ST_CUTOUT, BUTTON_RIGHT, ST_MOT },

  { ST_MOT, BUTTON_LONG | BUTTON_TIMEOUT, ST_HOME_SMART },
  { ST_MOT, BUTTON_LEFT, ST_CUTOUT },
  { ST_MOT, BUTTON_RIGHT, ST_MRT },

  { ST_MRT, BUTTON_LONG | BUTTON_TIMEOUT, ST_HOME_SMART },
  { ST_MRT, BUTTON_LEFT, ST_MOT },
  { ST_MRT, BUTTON_RIGHT, ST_LCD_BRIGHTNESS },

  { ST_LCD_BRIGHTNESS, BUTTON_LONG | BUTTON_TIMEOUT, ST_HOME_SMART },
  { ST_LCD_BRIGHTNESS, BUTTON_LEFT, ST_MRT },
  { ST_LCD_BRIGHTNESS, BUTTON_RIGHT, ST_RESET_CONFIG },

  { ST_RESET_CONFIG, BUTTON_LONG | BUTTON_TIMEOUT, ST_HOME_SMART },
  { ST_RESET_CONFIG, BUTTON_LEFT, ST_LCD_BRIGHTNESS },
  { ST_RESET_CONFIG, BUTTON_RIGHT, ST_SETPOINT },

  { 0, 0, 0 }
};

