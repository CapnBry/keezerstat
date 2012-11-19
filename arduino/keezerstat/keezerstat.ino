#include <math.h>
#include <avr/eeprom.h>
#include <arduino.h>
#include <Wire.h>
// Adafruit version of LiquidCrystal
#include "LiquidCrystal.h"
#include "OneWire.h"
#include "Bounce.h"
#include "DallasTemperature.h" 
#include "rgbcontrol.h"
#include "keezerstat.h"
#include "encodermenu.h"
#include "fcmenu.h"
#include "bigchars.h"

#define DPIN_ENCODER_A  2 // Pin 4
#define DPIN_ENCODER_B  3 // Pin 5
#define DPIN_ENCODER_P  4 // Pin 6
#define DPIN_LED_R      5 // Pin 11
#define DPIN_LED_G      6 // Pin 12
#define DPIN_DS18S20    7 // Pin 13
#define DPIN_RELAY_COLD 8 // Pin 14
#define DPIN_LED_B      9 // Pin 15

// Connect via i2c, address #7 (a0-a2 high)
LiquidCrystal lcd(7);
RgbControl rgb(DPIN_LED_R, DPIN_LED_G, DPIN_LED_B);
EncoderMenu menu(DPIN_ENCODER_P, fcmenu_definitions, fcmenu_transitions);

OneWire oneWire(DPIN_DS18S20);
DallasTemperature tempProbes(&oneWire);
DeviceAddress probeAddr;

static uint32_t g_LastStateSwitch;
static uint32_t g_TotalOffTime;
static uint32_t g_TotalOnTime;
static uint32_t g_ThisOnOffTime;
static uint32_t g_LastOnDuration;
static uint32_t g_LastOffDuration;
static uint32_t g_ProbeCheckTime;
static uint8_t g_ProbeCheckCnt;
static float g_LastTemperature;
static float g_TempAverage = NAN;
static int16_t g_CurrPeak;
static int16_t g_LastPeakHigh;
static int16_t g_LastPeakLow;
// Store 8 characters, 5 pixels wide, of temp * 100
// int16 should hold refrigerator temps just fine up/down to 327.68
static int16_t g_TempHistory[8 * 5];  
static TrendDir g_TempTrend;
static char g_SerialBuff[64];

ThermoState g_ThermoState;
int16_t g_SetPoint;
int8_t g_CutinOffset;
int8_t g_CutoutOffset;
uint8_t g_MinOffTime;
uint8_t g_MinRunTime;
char g_Units;
state_t g_LastHome;

#define GRAPH_BAR_PERIOD 60
#define TEMP_FAST_SMOOTH 2
#define ARRAYSIZE(a) (sizeof(a) / sizeof(*(a)))

#define config_store_byte(eeprom_field, src) { eeprom_write_byte((uint8_t *)offsetof(__eeprom_config, eeprom_field), src); }
#define config_store_word(eeprom_field, src) { eeprom_write_word((uint16_t *)offsetof(__eeprom_config, eeprom_field), src); }

#define EEPROM_MAGIC 0xbee0

static struct __eeprom_config {
  uint16_t magic;
  int16_t SetPoint;
  int16_t CutinOffset;
  int16_t CutoutOffset;
  uint8_t MinOffTime;
  uint8_t MinRunTime;
  char Units;
  state_t LastHome;
  uint8_t Brightness;
} DEFAULT_CONFIG PROGMEM = { 
  EEPROM_MAGIC,
  40,  // SetPoint
  0,   // Cut-in offset
  5,   // Cutoff offset
  5,   // Min off time
  3,   // Min run time
  'F', // units
  ST_HOME_GRAPH, // LastHome
  100  // Brightness
};

static void calcExpMovingAverage(const float smooth, float *currAverage, float newValue)
{
  if (isnan(*currAverage))
    *currAverage = newValue;
  else
  {
    newValue = newValue - *currAverage;
    *currAverage = *currAverage + (smooth * newValue);
  }
}

static void printMillis(void)
{
  Serial.write('(');
  Serial.print(millis(), DEC);
  Serial.write(')');
  Serial.write(' ');
}

static void uploadCharsGraph(void)
{
  int16_t graphLow = (g_SetPoint - g_CutoutOffset) * 100;
  int16_t graphRange = (g_CutoutOffset + g_CutinOffset) * 100;
  uint8_t customChar[8];
  /* X loop is one per custom character, 8 custom characters */
  for (uint8_t x=0; x<8; ++x)
  {
    /* Y loop is scanning down the character, 8 pixels high */ 
    for (uint8_t y=0; y<8; ++y)
    {
      uint8_t rasterRow = 0;
      /* X2 loop is per pixel in each custom character, 5 pixels wide */
      for (uint8_t x2=0; x2<5; ++x2)
      {
        if (g_TempHistory[x*5 + x2] >= (graphLow + (graphRange * y / 8)))
          bitSet(rasterRow, 4 - x2);
      }
      customChar[7 - y] = rasterRow;
    }
    lcd.createChar(x, customChar);
  }
}

static void uploadCharsBigN(void)
{
  uint8_t buff[8];
  for (uint8_t i=0; i<8; ++i)
  {
    memcpy_P(buff, BIG_CHAR_PARTS + (i * 8), 8);
    lcd.createChar(i, buff);
  }
}

void uploadChars(void)
{
  switch(menu.getState())
  {
    case ST_HOME_GRAPH:
      uploadCharsGraph();
      break;
    case ST_HOME_BIGN:
      uploadCharsBigN();
      break;
  }
}

static void newMinute(void)
{
  Serial.print(F("newMinute\n"));
  // Move all the elements down by one
  for (uint8_t i=0; i<ARRAYSIZE(g_TempHistory)-1; ++i)
    g_TempHistory[i] = g_TempHistory[i+1];
  // Put the new element in the last slot
  g_TempHistory[ARRAYSIZE(g_TempHistory)-1] = g_TempAverage * 100;
 
  if (g_LastHome == ST_HOME_GRAPH)
   uploadCharsGraph();
}

static void setState(ThermoState ts)
{
  printMillis(); Serial.print(F("Thermostate: ")); Serial.println(ts, DEC);
  g_LastStateSwitch = millis();
  g_ThermoState = ts;
  if (menu.getState() >= ST_HOME_SMART && menu.getState() <= ST_HOME_BIGN)
    menu.setState(ST_HOME_SMART);
}

static uint32_t getElapsedStateSecs(void)
{
  return (millis() - g_LastStateSwitch) / 1000;
}

static void lcd_printTime(uint32_t t)
{
  // We only print 8 characters so if greater than 99:59:59 display overflow
  if (t >= 360000)
  {
    lcd.print(F(">100 hrs"));
    return;
  }
  uint8_t h = t / 3600;
  uint8_t m = (t - (h * 3600UL)) / 60;
  uint8_t s = t - (h * 3600UL) - (m * 60);
  char buf[9];
  snprintf_P(buf, sizeof(buf), PSTR("%2u:%2.2u:%2.2u"), h, m, s);
  lcd.print(buf);
  /*
  if (h < 10)
    lcd.print(' ');
  lcd.print(h, DEC);
  lcd.print(':');
  if (m < 10)
    lcd.print('0');
  lcd.print(':');
  lcd.print(m, DEC);
  if (s < 10)
    lcd.print('0');
  lcd.print(m, DEC);
  */
}

static boolean hasTemperature(void)
{
  return !isnan(g_LastTemperature);
}

static void calcTempTrend(void)
{
  if (!hasTemperature())
    return;

  calcExpMovingAverage((2.0f/(1.0f+GRAPH_BAR_PERIOD)), &g_TempAverage, g_LastTemperature);
  int16_t curr = g_LastTemperature * 100;
  TrendDir newTrend = TREND_NEUTRAL;
  // Look up to N minutes in the past to determine a new trend
  for (uint8_t i=1; i<10; ++i)
  {
    int16_t diff = curr - g_TempHistory[ARRAYSIZE(g_TempHistory)-i];
    if (diff > 100)
    {
      newTrend = TREND_UP;
      break;
    }
    if (diff < -100)
    {
      newTrend = TREND_DOWN;
      break;
    }
  }

  switch (g_TempTrend)
  {
    case TREND_NEUTRAL:
      g_TempTrend = newTrend;
      if (g_TempTrend != TREND_NEUTRAL)
      {
        g_CurrPeak = curr;
      }
      break;
    case TREND_UP:
      if (curr > g_CurrPeak)
        g_CurrPeak = curr;
      else if (newTrend == TREND_DOWN)
      {
        g_LastPeakHigh = g_CurrPeak;
        Serial.print(F("High Peak ")); Serial.println(g_LastPeakHigh, DEC);
        g_TempTrend = TREND_DOWN;
      }
      break;
    case TREND_DOWN:
      if (curr < g_CurrPeak)
        g_CurrPeak = curr;
      else if (newTrend == TREND_UP)
      {
        g_LastPeakLow = g_CurrPeak;
        Serial.print(F("Low Peak ")); Serial.println(g_LastPeakLow, DEC);
        g_TempTrend = TREND_UP;
      }
      break;
  }
}

static void newTempAvailable(float c)
{
  if (g_Units == 'F')
    c = tempProbes.toFahrenheit(c);

  calcExpMovingAverage(2.0f/(1.0f+TEMP_FAST_SMOOTH), &g_LastTemperature, c);

  printMillis();
  Serial.print(F("Temp: "));
  Serial.print(c, 2);
  Serial.println(g_Units);

  switch (g_ThermoState)
  {
    case THERMO_NOPROBE:
      setState(THERMO_MOT);
      break;
    case THERMO_OFF:
      if (c >= (g_SetPoint + g_CutinOffset))
      {
        Serial.print(F("Turning on\n"));
        g_LastOffDuration = g_ThisOnOffTime;
        g_ThisOnOffTime = 0;
        digitalWrite(DPIN_RELAY_COLD, HIGH);
        setState(THERMO_MRT);
      }
      break;
    case THERMO_MRT:
      if (getElapsedStateSecs() >= (g_MinRunTime * 60U))
      {
        Serial.print(F("MRT reached\n"));
        setState(THERMO_ON);
      }
      break;
    case THERMO_ON:
      if (c <= (g_SetPoint - g_CutoutOffset))
      {
        Serial.print(F("Turning off\n"));
        g_LastOnDuration = g_ThisOnOffTime;
        g_ThisOnOffTime = 0;
        digitalWrite(DPIN_RELAY_COLD, LOW);
        setState(THERMO_MOT);
      }
      break;
    case THERMO_MOT:
      if (getElapsedStateSecs() >= (g_MinOffTime * 60U))
      {
        Serial.print(F("MOT reached\n"));
        setState(THERMO_OFF);
      }
      break;
  }
}

static void lcdPrintBigNum(float val)
{
  // good up to 3276.8
  int16_t ival = val * 10;
  uint16_t uval;
  boolean isNeg;
  if (ival < 0)
  {
    isNeg = true;
    uval = -ival;
  }
  else
  {
    isNeg = false;
    uval = ival;
  }

  int8_t x = 16;
  do
  {
    if (uval != 0 || x >= 9)
    {
      const prog_char *numData = NUMS + ((uval % 10) * 6);

      x -= C_WIDTH;
      lcd.setCursor(x, 0);
      for (uint8_t n=0; n<C_WIDTH*2; ++n)
      {
        if (n == C_WIDTH)
          lcd.setCursor(x, 1);
        lcd.write(pgm_read_byte(numData++));
      }

      uval /= 10;
    }  /* if val */
    --x;
    lcd.setCursor(x, 0);
    lcd.write(C_BLK);
    lcd.setCursor(x, 1);
    if (x == 12)
      lcd.write('.');
    else if (uval == 0 && x < 9 && isNeg)
    {
      lcd.write(C_CT);
      isNeg = false;
    }
    else
      lcd.write(C_BLK);
  } while (x != 0);
}

static void displayHomeBigN(void)
{
  lcdPrintBigNum(g_LastTemperature);
}

static void lcd_printTemp(float t)
{
  int8_t intpart = t;
  int8_t floatpart = (10 * t) - (10 * intpart);
  char buf[8];
  snprintf_P(buf, sizeof(buf), PSTR("%3d.%01d\xdf%c"), intpart, floatpart, g_Units);
  lcd.print(buf);
}

static void displayHomeGraph(void)
{  
  lcd_printTemp(g_LastTemperature);
  lcd.print(' ');
  lcd_printTime(g_ThisOnOffTime);
  
  lcd.setCursor(0, 1);
  char buf[9];
  uint32_t tt = g_TotalOnTime + g_TotalOffTime;
  if (tt == 0)
    tt = 1;
  snprintf_P(buf, sizeof(buf), PSTR("Duty%3u%%"), (uint8_t)((g_TotalOnTime * 100) / tt));
  lcd.print(buf);

  // Print the custom graph characters
  for (uint8_t c=0; c<8; ++c)
   lcd.write(c);
}

static void displayHomeNoProbe(void)
{
  lcd.print(F(" No temperature "));
  lcd.setCursor(0, 1);
  lcd.print(F("sensor  detected"));
}

static void displayHomeLastDur(void)
{
  lcd.print(F("Last on "));
  lcd_printTime(g_LastOnDuration);
  lcd.setCursor(0, 1);
  lcd.print(F("    off "));
  lcd_printTime(g_LastOffDuration);
}

static void displayHomeLastPeak(void)
{
  lcd.print(F("Last high"));
  lcd_printTemp(g_LastPeakHigh / 10.0f);
  lcd.setCursor(0, 1);
  lcd.print(F("      low"));  
  lcd_printTemp(g_LastPeakLow / 10.0f);
}

void displayHome(void)
{
  lcd.home();
  if (g_ThermoState == THERMO_NOPROBE)
  {
    displayHomeNoProbe();
    return;
  }

  switch (menu.getState())
  {
    case ST_HOME_GRAPH:
      displayHomeGraph();
      break;
    case ST_HOME_BIGN:
      displayHomeBigN();
      break;
    case ST_HOME_LAST_DUR:
      displayHomeLastDur();
      break;
    case ST_HOME_LAST_PEAK:
      displayHomeLastPeak();
      break;
  }
}

static void scanAndBeginMeasure(void)
{
  tempProbes.begin();
  uint8_t cnt = tempProbes.getDeviceCount();
#if DEBUG_SCAN
  Serial.print(F("Scanning DS18S20 found ")); Serial.print(cnt, DEC); Serial.print('\n');
  for (uint8_t i=0; i<cnt; ++i)
  {
    if (tempProbes.getAddress(probeAddr, i))
    {
      Serial.print(F("Probe "));
      Serial.print(i, DEC); Serial.print(' ');
      printAddress(probeAddr);
      Serial.print(F(" resolution "));
      Serial.println(tempProbes.getResolution(probeAddr), DEC);
    }
  }
#endif

  if (cnt)
  {
    if (g_ThermoState == THERMO_NOPROBE)
      setState(THERMO_MOT);
    // Use the first probe
    tempProbes.getAddress(probeAddr, 0);
    tempProbes.setResolution(probeAddr, 12);
    tempProbes.requestTemperaturesByAddress(probeAddr);
    g_ProbeCheckTime = millis();
    g_ProbeCheckCnt = 0;
  }
  else if (g_ThermoState != THERMO_NOPROBE)
    setState(THERMO_NOPROBE);
}

static void checkTemps(void)
{
  // ms between checks for probes or conversion completions
#define PROBE_CHECK_INTERVAL 200
#define PROBE_CHECK_MAX      5

  if (millis() - g_ProbeCheckTime < PROBE_CHECK_INTERVAL)
    return;

  g_ProbeCheckTime = millis();
  if (g_ThermoState != THERMO_NOPROBE)
  {
    if (tempProbes.isConversionAvailable(probeAddr))
    {
      //printMillis(); Serial.print(F("Temperature ready\n"));
      newTempAvailable(tempProbes.getTempC(probeAddr));
    }
    else
    {
      ++g_ProbeCheckCnt;
      if (g_ProbeCheckCnt <= PROBE_CHECK_MAX)
        return;
    }
  }
  // Either we've gone over probe check max, there's no probe, 
  // or we just finished a measurement, so start a new scan/measure
  scanAndBeginMeasure();
}   

static void handleCommandUrl(char *URL)
{
  if (strncmp_P(URL, PSTR("set?rgb="), 8) == 0) 
  {
    rgb.set(strtoul(URL+8, NULL, 16));
  }
}

static void serial_update(void)
{
  unsigned char len = strlen(g_SerialBuff);
  while (Serial.available())
  {
    char c = Serial.read();
    // support CR, LF, or CRLF line endings
    if (c == '\n' || c == '\r')  
    {
      if (len != 0 && g_SerialBuff[0] == '/')
        handleCommandUrl(&g_SerialBuff[1]);
      len = 0;
    }
    else {
      g_SerialBuff[len++] = c;
      // if the buffer fills without getting a newline, just reset
      if (len >= sizeof(g_SerialBuff))
        len = 0;
    }
    g_SerialBuff[len] = '\0';
  }  /* while Serial */
}

static void newSecond(void)
{
  static uint8_t secondCnt;

  switch (g_ThermoState)
  {
    case THERMO_NOPROBE:
      break;
    case THERMO_OFF:
    case THERMO_MOT:
      ++g_TotalOffTime;
      break;
    case THERMO_MRT:
    case THERMO_ON:
      ++g_TotalOnTime;
      break;
  }
  ++g_ThisOnOffTime;
    
  ++secondCnt;
  if (secondCnt >= GRAPH_BAR_PERIOD)
  {
    newMinute();
    secondCnt = 0;
  }

  calcTempTrend();
}

static void eepromStoreConfig(void)
{
  config_store_word(SetPoint, g_SetPoint);
  config_store_word(CutinOffset, g_CutinOffset);
  config_store_word(CutoutOffset, g_CutoutOffset);
  config_store_byte(MinOffTime, g_MinOffTime);
  config_store_byte(MinRunTime, g_MinRunTime);
  config_store_byte(Units, g_Units);
  config_store_byte(LastHome, g_LastHome);

  uint8_t v;
  v = rgb.getBrightness();
  config_store_byte(Brightness, v);
}

static void eepromLoadConfig(boolean forceDefault)
{
  struct __eeprom_config config;
  eeprom_read_block(&config, 0, sizeof(config));
  forceDefault = forceDefault || config.magic != EEPROM_MAGIC;
  if (forceDefault)
  {
    memcpy_P(&config, &DEFAULT_CONFIG, sizeof(__eeprom_config));
    eeprom_write_block(&config, 0, sizeof(__eeprom_config));  
  }

  g_SetPoint = config.SetPoint;
  g_CutinOffset = config.CutinOffset;
  g_CutoutOffset = config.CutoutOffset;
  g_MinOffTime = config.MinOffTime;
  g_MinRunTime = config.MinRunTime;
  g_Units = config.Units;
  g_LastHome = ST_HOME_GRAPH; //config.LastHome;
  rgb.setBrightness(config.Brightness);
}

void setSetPoint(int16_t sp)
{
  g_SetPoint = sp;
  g_TotalOffTime = g_TotalOnTime = g_ThisOnOffTime = 0;
  configChanged();
}

void configChanged(void)
{
  eepromStoreConfig();
  uploadChars();
}

void configReset(void)
{
  eepromLoadConfig(true);
}

void setup()
{
#if VM_DEBUG  
  Serial.begin(115200);
#else
  Serial.begin(38400);
#endif
  Serial.print(F("$UCID,KeezerStat,"__DATE__" "__TIME__"\n"));

  eepromLoadConfig(false);
  lcd.begin(16, 2);

  g_ThermoState = THERMO_NOPROBE;
  tempProbes.setWaitForConversion(false);
  scanAndBeginMeasure();
  menu.setState(ST_VERSION);
}

void loop()
{
  static uint32_t lastSecond;
  uint32_t thisSecond = millis()/1000;
  if (thisSecond != lastSecond)
  {
    lastSecond = thisSecond;
    newSecond();
  }

  checkTemps();
  serial_update();
  menu.update();
  rgb.update();
}

