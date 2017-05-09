
/* (c)2017 Hayati Ayguen <h_ayguen@web.de>
 * License: MIT
 *
 * program for microController Teensy 3.1 with IDE Teensyduino:
 * purpose: automatic cw keyer with sidetone output
 *          and notification of all states to host/PC
 *          (no drivers needed by using Raw HID interface)
 * settings for Arduino IDE:
 *   Board:           Teensy 3.1
 *   USB Type:        Raw HID
 *   CPU Speed:       72 MHz
 *   Keyboard Layout: US English
 *
 * you might want to change default Config settings:
 * see constructors initializer list at lines 53ff
 */

#include <EEPROM.h>
#include <WProgram.h>
#include "usb_desc.h"

#define USE_SERIAL    1
#define USE_HID       1
#define LOG_KEYSTATE  1

#ifndef __cplusplus
  #error Expected C++ compiler!
#endif

typedef enum {
  kmSTRAIGHT = 0,
  kmIAMBICA,
  kmIAMBICB
} KeyingMode;

typedef enum {
  uPAUSE = 0,
  uDIT   = 1,
  uDAH   = 2,
  uDIDAH = 3
} CWunit;

// forward declarations - why the hell are these necessary!?!?
struct Config;
void readConfFromEEprom( Config * pConf );
void writeConfToEEprom( const Config * pConf );

#if USE_SERIAL
  char acSerialMsg[256];
#endif

#pragma pack(push, 1) // exact fit - no padding

struct Config
{
  Config()
    : structSize( sizeof(Config) )
    , greenDitInpPin( 14 )
    , redDahInpPin( 15 )
    , sideToneSpkrOutPin( 23 )  // PWM output needs further filtering!

    , keyedOutPin( 0 )
    , ditLEDpin( 9 )
    , dahLEDpin( 8 )
    , outLEDpin( 7 )

    , invertDitInpPin( 0 )
    , invertDahInpPin( 0 )
    , invertOutPin( 0 )
    , swapDitDahPins( 0 )

    // resonant frequencies:
    // see https://cdn-reichelt.de/documents/datenblatt/H100/180010RMP-14PHT.pdf
    // of http://www.reichelt.de/SUMMER-EPM-121/3/index.html?&ARTICLE=35927
    // 650, 850, 1400, 1800, 2550, 4050, 5200, 7800
    , ditSpkrFreq( 1400 )
    , dahSpkrFreq( 850 )

    , keyingMode( kmIAMBICB )   // kmSTRAIGHT  kmIAMBICA  kmIAMBICB
    , autoSpace( 1 )
    , keyingSpeedInWPMx10( 70 )  // 70 == 7 wpm
    , bounceLoadDecay( uint16_t(0.96 * 65536.0) )
    , bounceLoadThresh( uint16_t(0.5 * 65536.0) )
  {
    begID[0] = 'C';
    begID[1] = 'W';
    begID[2] = '0';
    begID[3] = '0';

    endID[0] = '0';
    endID[1] = '0';
    endID[2] = 'W';
    endID[3] = 'C';
  }

  bool isValid() const {
    Config tmp;
    if ( begID[0] != tmp.begID[0] || begID[1] != tmp.begID[1] || begID[2] != tmp.begID[2] || begID[3] != tmp.begID[3]
      || endID[0] != tmp.endID[0] || endID[1] != tmp.endID[1] || endID[2] != tmp.endID[2] || endID[3] != tmp.endID[3] )
      return false;
    return true;
  }

  void Reset() {
    Config tmp;
    *this = tmp;
  }


  // off: 0
  char begID[4];

  // off: 4
  uint8_t structSize;
  uint8_t greenDitInpPin;
  uint8_t redDahInpPin;
  uint8_t sideToneSpkrOutPin;

  // off: 8
  uint8_t keyedOutPin;
  uint8_t ditLEDpin;
  uint8_t dahLEDpin;
  uint8_t outLEDpin;

  // off: 12
  uint8_t invertDitInpPin;
  uint8_t invertDahInpPin;
  uint8_t invertOutPin;
  uint8_t swapDitDahPins;

  // off: 16
  uint16_t ditSpkrFreq;
  uint16_t dahSpkrFreq;

  // off: 20
  uint8_t keyingMode;  // KeyingMode keyingMode;
  uint8_t autoSpace;

  // off: 22
  uint16_t keyingSpeedInWPMx10;

  // off: 24
  uint16_t bounceLoadDecay;
  uint16_t bounceLoadThresh;

  // off: 28
  char endID[4];
  // off: 32
};

#pragma pack(pop) //back to whatever the previous packing mode was

void readConfFromEEprom( Config * pConf )
{
  const int confSize = sizeof(Config);
  uint8_t aConf[ sizeof(Config) ];
  for ( int a = 0; a < confSize; ++a )
    aConf[a] = EEPROM.read(a);
  memcpy( pConf, aConf, confSize );
#if USE_SERIAL
  Serial.println( "read config from EEPROM:");
  sprintf(acSerialMsg, "config has %s config", (pConf->isValid() ? "valid" : "invalid") );
  Serial.println( acSerialMsg );
#endif
  if ( ! pConf->isValid() )
    pConf->Reset();
}

void writeConfToEEprom( const Config * pConf )
{
  const int confSize = sizeof(Config);
  uint8_t aConf[ sizeof(Config) ];

  if ( pConf->isValid() )
  {
    Config cFromEEprom;
    uint8_t prevConf[ sizeof(Config) ];
    uint8_t verifyConf[ sizeof(Config) ];
    int a;
    bool allEq = true;

    readConfFromEEprom( &cFromEEprom );
    memcpy( &prevConf, &cFromEEprom, confSize );

    memcpy( aConf, pConf, confSize );
    for ( a = 0; a < confSize; ++a )
    {
      if ( true || prevConf[a] != aConf[a] )
      {
        EEPROM.write( a, aConf[a] );
        verifyConf[a] = EEPROM.read(a);
#if USE_SERIAL
        if ( verifyConf[a] != aConf[a] )
        {
          sprintf(acSerialMsg, "EEPROM write verification at byte %d of %d failed!", a, confSize );
          Serial.println( acSerialMsg );
        }
        else
        {
          sprintf(acSerialMsg, "EEPROM write verification at byte %d of %d successful.", a, confSize );
          Serial.println( acSerialMsg );
        }
#endif
        allEq = allEq && ( verifyConf[a] == aConf[a] );
      }
    }

#if USE_SERIAL
    sprintf(acSerialMsg, "EEPROM write verification %s", (allEq ? "successful" : "failed!") );
    Serial.println( acSerialMsg );
#endif
  }
}


// 5ms == 5 * 4 delays a 250 us = 20
// x^20 = 0.5
// 20*log10(x) = log10(0.5)
// log10(x) = log10(0.5) / 20
// x = 10^( log10(0.5)/20 )
// x = 0.96

Config c;

int outOnValue;
int outOffValue;

uint8_t txBufS[64];
uint8_t txBufC[64];
uint8_t rxBuf[64];

class CWstateMachine
{
public:
  virtual void Config() = 0;
  virtual void Reset() = 0;
  virtual uint8_t Do( uint8_t sensorVal ) = 0;
};

class StraightCW
  : public CWstateMachine
{
public:
  StraightCW() { Reset(); }
  void Config() { }
  void Reset();
  uint8_t Do( uint8_t sensorVal );
private:
  uint8_t lastSensorVal;
  uint8_t lastOutputVal;
};

class IambicCW
  : public CWstateMachine
{
public:
  IambicCW() { Reset(); }
  void Config();
  void Reset();
  uint8_t Do( uint8_t sensorVal );
private:
  int fsm_state;
  int ditOutputCycles;
  int dahOutputCycles;
  int outputCounter;
  unsigned timeCounter;

  uint8_t sensorBuf[4];
  uint8_t lastSensorVal;
  uint8_t lastOutputVal;
  uint8_t playOpposite;
  uint8_t releasedKeys;
};




StraightCW stateStraight;
IambicCW   stateIambic;
CWstateMachine * state = &stateStraight;

bool xmitSensor = false;

void setupCW()
{
  pinMode(c.greenDitInpPin, INPUT_PULLUP);
  pinMode(c.redDahInpPin, INPUT_PULLUP);
  pinMode(c.keyedOutPin, OUTPUT);

  pinMode(c.ditLEDpin, OUTPUT);
  pinMode(c.dahLEDpin, OUTPUT);
  pinMode(c.outLEDpin, OUTPUT);

  outOnValue  = ( c.invertOutPin ) ? LOW : HIGH;
  outOffValue = ( c.invertOutPin ) ? HIGH : LOW;
  digitalWrite( c.keyedOutPin, outOffValue );

  xmitSensor = false;

  switch( c.keyingMode )
  {
  default:
  case kmSTRAIGHT:
    state = &stateStraight;
    break;
  case kmIAMBICA:
  case kmIAMBICB:
    state = &stateIambic;
    break;
  }
  state->Config();
  state->Reset();

  delay(100);
}

void setup()
{
#if USE_SERIAL
  Serial.begin(38400);
#endif

  {
    int i;

    for ( i = 0; i < 64; ++i )
      txBufS[i] = 0;
  
    txBufS[0] = 'C';
    txBufS[1] = 'W';

    for ( i = 0; i < 64; ++i )
      txBufC[i] = 0;

    txBufC[0] = 'C';
    txBufC[1] = 'F';
    txBufC[2] = 'G';
    txBufC[3] = 'U';
  }

  readConfFromEEprom( &c );

  setupCW();

#if 0
  //           +     +    ++     +     -   +++    ++   -
  // dB:       75,  78,   83,   79,   77,   92,   89,  86
  //int f[] = { 650, 850, 1400, 1800, 2550, 4050, 5200, 7800 };
  //int f[] = { 650, 850, 1400, 4050 };
  //int f[] = { 50, 100, 150, 200, 250, 300, 350, 400, 450, 500, 550, 600, 650, 700, 750, 800, 850, 900, 950, 1000 };
  //int f[] = { 450, 800, 1000, 2000, 3000, 4000 };
  //int f[] = { 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
  int f[] = { 440, 790, 810, 960, 4000 };
  int nf = sizeof(f)/sizeof(f[0]);
  for ( int fi=0; fi < nf; ++fi )
  {
    int freq = f[fi];
    tone( c.sideToneSpkrOutPin, freq );
    delay(1000);
    noTone(c.sideToneSpkrOutPin);
    delay(500);
  }
  delay(1000);
#endif


#if USE_SERIAL
  delay(3000);
#ifdef USB_RAWHID
  Serial.println( "USB_RAWHID set");
#else
  Serial.println( "USB_RAWHID not set");
#endif
#ifdef VENDOR_ID
  sprintf(acSerialMsg, "VENDOR_ID         0x%0x", VENDOR_ID);
  Serial.println( acSerialMsg );
#endif
#ifdef VENDOR_ID
  sprintf(acSerialMsg, "PRODUCT_ID        0x%0x", PRODUCT_ID);
  Serial.println( acSerialMsg );
#endif
#ifdef VENDOR_ID
  sprintf(acSerialMsg, "RAWHID_USAGE_PAGE 0x%0x", RAWHID_USAGE_PAGE);
  Serial.println( acSerialMsg );
#endif
#ifdef VENDOR_ID
  sprintf(acSerialMsg, "RAWHID_USAGE      0x%0x", RAWHID_USAGE);
  Serial.println( acSerialMsg );
#endif
#endif
}


void loop()
{
#if USE_SERIAL == 0
  while (1)
#endif
  {
    static uint16_t loadA = 0;
    static uint16_t loadB = 0;

    uint8_t sensorValA = ( ( digitalRead(c.greenDitInpPin) ) ? 1 : 0 ) ^ c.invertDitInpPin;
    if ( !sensorValA )
      loadA = 65535;
    else
      loadA = ( ( uint32_t(loadA) * uint32_t(c.bounceLoadDecay) ) >> 16 ) & 0xffff;

    uint8_t sensorValB = ( ( digitalRead(c.redDahInpPin) ) ? 1 : 0 ) ^ c.invertDahInpPin;
    if ( !sensorValB )
      loadB = 65535;
    else
      loadB = ( ( uint32_t(loadB) * uint32_t(c.bounceLoadDecay) ) >> 16 ) & 0xffff;

    static uint8_t sensorVal = 0xff;
    uint8_t prevSensorVal = sensorVal;
    sensorVal = ( ( loadA >= c.bounceLoadThresh ) ? uDIT : 0 ) | ( ( loadB >= c.bounceLoadThresh ) ? uDAH : 0 );
    if ( c.swapDitDahPins )
      sensorVal = ( ( sensorVal & uDIT ) ? uDAH : 0 ) | ( ( sensorVal & uDAH ) ? uDIT : 0 );


    static uint8_t outputVal = 0;
    uint8_t prevOutputVal = outputVal;
    outputVal = state->Do( sensorVal );


    int nRx = RawHID.recv(rxBuf, 0); // 0 timeout = do not wait

    if ( nRx == 64 && rxBuf[0]=='C' && rxBuf[1]=='F' && rxBuf[2]=='G' )
    {
      if ( rxBuf[3]=='U' )  // CFG Update: use / update to attached configuration
      {
        Config nCfg;
        memcpy( &nCfg, &rxBuf[4], sizeof(Config) );
        if ( nCfg.isValid() )
        {
          memcpy( &c, &rxBuf[4], sizeof(Config) );
          setupCW();
        }
      }
      else if ( rxBuf[3]=='W' )  // CFG Write: write current configuration (no configuration attached)
      {
        writeConfToEEprom( &c );
      }
      else if ( rxBuf[3]=='R' )  // CFG Request: transmit current configuration to host/PC
      {
        memcpy( &txBufC[4], &c, sizeof(Config) );
        RawHID.send(txBufC, 0);
      }
      nRx = 0;
    }

    xmitSensor = ( xmitSensor || nRx > 0 || prevSensorVal != sensorVal || prevOutputVal != outputVal );

    if ( xmitSensor )
    {
      txBufS[2] = sensorVal;
      txBufS[3] = outputVal;
      txBufS[4] = (uint8_t)( loadA >> 8 );
      txBufS[5] = (uint8_t)( loadB >> 8 );

      int nTx = RawHID.send(txBufS, 0);
      if (nTx > 0)
      {
        xmitSensor = false;
#if ( USE_SERIAL && LOG_KEYSTATE )
        sprintf(acSerialMsg, "\nDIT=%c DAH=%c Output=%c"
                , ( ( sensorVal & uDIT ) ? '1' : '0' )
                , ( ( sensorVal & uDAH ) ? '1' : '0' )
                , ( ( outputVal ) ? '1' : '0' )
                );
        Serial.println( acSerialMsg );
#endif
      }
    }
    delayMicroseconds( 250 );  // 1/4 ms = 250 us
  }
}




///////////////////////////////////////////////////////////////////////////////////////////////////////////

void StraightCW::Reset()
{
  lastSensorVal = 0xff;
  lastOutputVal = 0;
}

uint8_t StraightCW::Do( uint8_t sensorVal )
{
  uint8_t outputVal = lastOutputVal;
  if ( lastSensorVal != sensorVal )
  {
#if USE_SERIAL
        Serial.println( "new sensorval" );
#endif
    if ( sensorVal )
    {
      outputVal = 1;
      digitalWrite( c.keyedOutPin, outOnValue );
      digitalWrite(c.ditLEDpin, HIGH);  // LED on
      digitalWrite(c.outLEDpin, HIGH);  // LED on
      tone(c.sideToneSpkrOutPin, c.ditSpkrFreq);
    }
    else
    {
      digitalWrite(c.ditLEDpin, LOW); // LED off
      outputVal = 0;
      digitalWrite( c.keyedOutPin, outOffValue );
      digitalWrite(c.outLEDpin, LOW); // LED off
      noTone(c.sideToneSpkrOutPin);
    }
    lastSensorVal = sensorVal;
    lastOutputVal = outputVal;
  }
  return outputVal;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

void IambicCW::Config()
{
  ditOutputCycles = ( uint32_t( 1200 )     * 40 / uint32_t( c.keyingSpeedInWPMx10 ) );
  dahOutputCycles = ( uint32_t( 1200 ) * 3 * 40 / uint32_t( c.keyingSpeedInWPMx10 ) );
}

void IambicCW::Reset()
{
  fsm_state = 4;
  lastSensorVal = 0xff;
  lastOutputVal = 0;
  outputCounter = 0;
  timeCounter = 0;
  playOpposite = 0;
  releasedKeys = 0;
}

uint8_t IambicCW::Do( uint8_t sensorVal )
{
  int prev_fsm_state = fsm_state;
  uint8_t outputVal = lastOutputVal;
  ++timeCounter;

  // fsm_state:
  // bit 3 == value 8: character pause
  // bit 2 == value 4: pause/mute tone
  // bit 1 == value 2: current/muted tone is DAH
  // bit 0 == value 1: current/muted tone is DIT

  switch(fsm_state)
  {
  case 12:
    if ( c.autoSpace )
    {
      --outputCounter;
      if ( !outputCounter )
      {
        fsm_state = 4;
        releasedKeys = 0;
      }
      break;
    }
    fsm_state = 4;
    // break
  case 4: // init / pause
    if ( sensorVal & uDIT )
    {
      fsm_state = 1;
      outputCounter = ditOutputCycles;
#if USE_SERIAL
      sprintf(acSerialMsg, "%u: switch to state 1 with DIT. outCounter = %d", timeCounter, outputCounter);
      Serial.println( acSerialMsg );
#endif
      break;
    }
    if ( sensorVal & uDAH )
    {
      fsm_state = 2;
      outputCounter = dahOutputCycles;
#if USE_SERIAL
      sprintf(acSerialMsg, "%u: switch to state 2 with DAH. outCounter = %d", timeCounter, outputCounter);
      Serial.println( acSerialMsg );
#endif
      break;
    }
    break;

  case 1:  // currently playing DIT
  case 2:  // currently playing DAH
    --outputCounter;
    if ( !outputCounter )
    {
      fsm_state += 4;
      outputCounter = ditOutputCycles;
#if USE_SERIAL
      sprintf(acSerialMsg, "%u: switch state += 4  =>  %d. outCounter = %d", timeCounter, fsm_state, outputCounter);
      Serial.println( acSerialMsg );
#endif
    }
    break;

  case 5:  // unit pause after DIT
  case 6:  // unit pause after DAH
    if ( (sensorVal & uDIDAH) == 0 )
      releasedKeys = 1;
    --outputCounter;
    if ( !outputCounter )
    {
      fsm_state &= 3;  // stop silence

      if ( (sensorVal & uDIDAH) == uDIDAH )
      {
        fsm_state ^= 3;  // squeeze
        playOpposite = ( c.keyingMode == kmIAMBICB ) ? 1 : 0;
#if USE_SERIAL
        if ( playOpposite )
          Serial.println( "activated IAMBIC-B opposite" );
#endif
      }
      else if ( playOpposite && releasedKeys )
      {
        fsm_state ^= 3;  // squeeze once again in IAMBICB when both keys released
        playOpposite = 0;
#if USE_SERIAL
        Serial.println( "now playing IAMBIC-B opposite" );
#endif
      }
      else if ( 1 && releasedKeys )  // ???
      {
        fsm_state = 0;
        playOpposite = 0;
      }
      else
      {
        fsm_state = (sensorVal & ( uDIT | uDAH ));
        playOpposite = 0;
      }

      if ( fsm_state == 1 )
        outputCounter = ditOutputCycles;
      else if ( fsm_state == 2 )
        outputCounter = dahOutputCycles;
      else
      {
        fsm_state = 12;
        outputCounter = dahOutputCycles - ditOutputCycles;
#if USE_SERIAL
        Serial.println( "character PAUSE" );
#endif
      }

#if USE_SERIAL
      sprintf(acSerialMsg, "%u: switch state from %d to %d. outCounter = %d", timeCounter, prev_fsm_state, fsm_state, outputCounter);
      Serial.println( acSerialMsg );
#endif
      break;
    }
  }

  if ( fsm_state != prev_fsm_state )
  {
    int prev_play = !( prev_fsm_state & 4 );
    int curr_play = !(      fsm_state & 4 );
    if ( !prev_play && curr_play )
    {
      outputVal = 1;
      digitalWrite( c.keyedOutPin, outOnValue );
      digitalWrite(c.outLEDpin, HIGH);  // LED on
      if ( fsm_state == 1 )
      {
        digitalWrite(c.ditLEDpin, HIGH);  // LED on
        digitalWrite(c.dahLEDpin, LOW);   // LED off
        tone(c.sideToneSpkrOutPin, c.ditSpkrFreq);
#if USE_SERIAL
        sprintf(acSerialMsg, "%u: tone Dit ON", timeCounter );
        Serial.println( acSerialMsg );
#endif
      }
      else if ( fsm_state == 2 )
      {
        digitalWrite(c.ditLEDpin, LOW);   // LED off
        digitalWrite(c.dahLEDpin, HIGH);  // LED on
        tone(c.sideToneSpkrOutPin, c.dahSpkrFreq);
#if USE_SERIAL
        sprintf(acSerialMsg, "%u: tone Dah ON", timeCounter );
        Serial.println( acSerialMsg );
#endif
      }
    }
    else if ( prev_play && !curr_play )
    {
      outputVal = 0;
      digitalWrite( c.keyedOutPin, outOffValue );
      digitalWrite(c.outLEDpin, LOW); // LED off
      digitalWrite(c.ditLEDpin, LOW); // LED off
      digitalWrite(c.dahLEDpin, LOW); // LED off
      noTone(c.sideToneSpkrOutPin);
#if USE_SERIAL
      sprintf(acSerialMsg, "%u: tone OFF", timeCounter );
      Serial.println( acSerialMsg );
#endif
    }
  }

  // fsm_state:
  // PAUSE T-DAH T-DIT
  //   4     2     1
  //
  // pressed key; start playing tone
  // dit  0 -> 1
  // dah  0 -> 2

  // tone timeout: +4
  // 1 -> 4+1  unit pause
  // 2 -> 4+2  unit pause

  // pause timeout - single key is pressed: -4
  // 4+1 -> 1  timeout
  // 4+2 -> 2  timeout

  // pause timeout - both keys are pressed: -4 , ^= 0x3
  // 4+1 -> 2  timeout -> squeeze
  // 4+2 -> 1  timeout -> squeeze

  lastSensorVal = sensorVal;
  lastOutputVal = outputVal;
  return outputVal;
}


