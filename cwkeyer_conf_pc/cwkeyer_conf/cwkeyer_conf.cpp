#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#if defined(OS_LINUX) || defined(OS_MACOSX)
#include <sys/ioctl.h>
#include <termios.h>
#elif defined(OS_WINDOWS) || defined(WIN32) || defined(WIN64)
#include <conio.h>
#include <windows.h>
#endif

#include "hid.h"

#define DEVICE_TIMEOUT_MS		10
#define RAWHID_PACKET_SIZE		64

bool emulateKeyerHW = false;

static char get_keystroke(void);


typedef enum {
	kmSTRAIGHT = 0,
	kmIAMBICA,
	kmIAMBICB,
	kmLATENCY
} KeyingMode;

typedef enum {
	uPAUSE = 0,
	uDIT = 1,
	uDAH = 2,
	uMASK = 3
} CWunit;


#define EEPROM_SIZE        2048
#define DEFAULT_DIT_PIN    14
#define DEFAULT_DAH_PIN    15
#define DEFAULT_SPKR_PIN   23
#define DEFAULT_OUT_PIN     0
#define DEFAULT_DIT_LED_PIN 9
#define DEFAULT_DAH_LED_PIN 8
#define DEFAULT_OUT_LED_PIN 7


#pragma pack(push, 1) // exact fit - no padding

struct KnobConfig
{
	KnobConfig()
		: addr(0)
		, keyingSpeedInWPMx10(0)
		, inpPin( -1 )
		, repeat()
	{
	}
	uint16_t	addr;					// start of text in EEPROM
	uint16_t	keyingSpeedInWPMx10;	// replay in this speed. default speed with 0
	uint8_t		inpPin;					// pin number of knob: 0xff == -1 == not used
	uint8_t		repeat;					// repeat N times. 0 == endless loop
};

struct CWconfig
{
  CWconfig()
    : structSize( sizeof(CWconfig) )
    , greenDitInpPin( DEFAULT_DIT_PIN )
    , redDahInpPin( DEFAULT_DAH_PIN )
    , sideToneSpkrOutPin( DEFAULT_SPKR_PIN )  // PWM output needs further filtering!

    , keyedOutPin( DEFAULT_OUT_PIN )
    , ditLEDpin( DEFAULT_DIT_LED_PIN )
    , dahLEDpin( DEFAULT_DAH_LED_PIN )
    , outLEDpin( DEFAULT_OUT_LED_PIN )

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
    , keyingSpeedInWPMx10( 70 )
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
    CWconfig tmp;
    if ( begID[0] != tmp.begID[0] || begID[1] != tmp.begID[1] || begID[2] != tmp.begID[2] || begID[3] != tmp.begID[3]
      || endID[0] != tmp.endID[0] || endID[1] != tmp.endID[1] || endID[2] != tmp.endID[2] || endID[3] != tmp.endID[3] )
      return false;
    return true;
  }

  void Reset() {
    CWconfig tmp;
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

CWconfig c;


bool readConfFromDev()
{
	if (emulateKeyerHW)
		return true;
	for (int tryNo = 0; tryNo < 5;  ++tryNo)
	{
		char buf[RAWHID_PACKET_SIZE];
		int ret;

		for (int i = 0; i < RAWHID_PACKET_SIZE; ++i)	buf[i] = 0;
		buf[0] = 'C';
		buf[1] = 'F';
		buf[2] = 'G';
		buf[3] = 'R';

		ret = rawhid_send(0, buf, RAWHID_PACKET_SIZE, DEVICE_TIMEOUT_MS);
		printf("sent CFG-Request with 4 bytes. rawhid_send returned %d bytes sent\n", ret);
		if (ret < 0)
			continue;

		ret = rawhid_recv(0, buf, RAWHID_PACKET_SIZE, DEVICE_TIMEOUT_MS);
		if (ret < 0)
			continue;

		if (buf[0] != 'C' || buf[1] != 'F' || buf[2] != 'G' || buf[3] != 'U')
			continue;

		CWconfig localC;
		memcpy(&localC, &buf[4], sizeof(CWconfig));
		if (!localC.isValid())
			continue;

		memcpy(&c, &buf[4], sizeof(CWconfig));
		return true;
	}
	return false;
}

bool writeConfToDev(bool writeToEEPROM = false)
{
	char buf[RAWHID_PACKET_SIZE];
	int ret;

	if (!c.isValid())
		return false;

	if (emulateKeyerHW)
		return true;

	if (writeToEEPROM)
	{
		for (int i = 0; i < RAWHID_PACKET_SIZE; ++i)	buf[i] = 0;
		buf[0] = 'C';
		buf[1] = 'F';
		buf[2] = 'G';
		buf[3] = 'W';

		ret = rawhid_send(0, buf, RAWHID_PACKET_SIZE, DEVICE_TIMEOUT_MS);
		printf("sent CFG-Write. rawhid_send returned %d bytes sent\n", ret);
		if (ret < 0)
			return false;
	}
	else
	{
		for (int i = 0; i < RAWHID_PACKET_SIZE; ++i)	buf[i] = 0;
		buf[0] = 'C';
		buf[1] = 'F';
		buf[2] = 'G';
		buf[3] = 'U';
		memcpy(&buf[4], &c, sizeof(CWconfig));

		ret = rawhid_send(0, buf, RAWHID_PACKET_SIZE, DEVICE_TIMEOUT_MS);
		printf("sent CFG-Update. rawhid_send returned %d bytes sent\n", ret);
		if (ret < 0)
			return false;
	}

	return true;
}

void printMenuAndConf()
{
	unsigned tu;
	double unitMs = 1200.0 / (c.keyingSpeedInWPMx10 / 10.0);
	double speedCPM = 6000.0 / unitMs;
									printf("\n\n\n\nMENU:\n");
									printf("1)  read config from device!\n");
									printf("2)  write config (on device) to EEPROM!\n");
									printf("3)  perform latency test!\n");
									printf("4)  print pressed Dits/Dahs\n");
									printf("5)  quit!\n");
									printf("\n");
	tu = c.greenDitInpPin;			printf("10) DIT input pin #         %u\n", tu);
	tu = c.redDahInpPin;			printf("11) DAH input pin #         %u\n", tu);
	tu = c.sideToneSpkrOutPin;		printf("12) Sidetone output pin #   %u\n", tu);
	tu = c.keyedOutPin;				printf("13) Keyed output pin #      %u\n", tu);
	tu = c.ditLEDpin;				printf("14) DIT LED output pin #    %u\n", tu);
	tu = c.dahLEDpin;				printf("15) DAH LED output pin #    %u\n", tu);
	tu = c.outLEDpin;				printf("16) Keyed output LED pin #  %u\n", tu);
	tu = c.invertDitInpPin;			printf("17) Invert DIT input?       %u\n", tu);
	tu = c.invertDahInpPin;			printf("18) Invert DAH input?       %u\n", tu);
	tu = c.invertOutPin;			printf("19) Invert keyed output?    %u\n", tu);
	tu = c.swapDitDahPins;			printf("20) Swap DIT/DAH pins?      %u\n", tu);
	tu = c.ditSpkrFreq;				printf("21) DIT sidetone frequency  %u Hz\n", tu);
	tu = c.dahSpkrFreq;				printf("22) DAH sidetone frequency  %u Hz\n", tu);
	tu = c.keyingMode;				printf("23) CW mode                 %u (0 = Straight, 1 = Mode A, 2 = Mode B)\n", tu);
	tu = c.autoSpace;				printf("24) AutoSpace?              %u\n", tu);
	tu = c.keyingSpeedInWPMx10;		printf("25) Keying speed            %u.%u wpm (=%.1f cpm, 1 unit = %.1f ms)\n", tu/10, tu%10, speedCPM, unitMs );
	tu = c.bounceLoadDecay;			printf("26) Bouncing load decay     %u (max 65535) == %.3f\n", tu, tu/65536.0);
	tu = c.bounceLoadThresh;		printf("27) Bouncing load threshold %u (max 65535)\n", tu);
	printf("\n");
	printf("Enter command value: ");
	fflush(stdout);
}

bool latencyTest()
{
	char txBuf[RAWHID_PACKET_SIZE];
	char rxBuf[RAWHID_PACKET_SIZE];
	int ret;
	uint8_t savedKeyingMode = c.keyingMode;

	if (emulateKeyerHW)
		return true;

	c.keyingMode = kmLATENCY;
	if (!writeConfToDev(false))
		return false;
	::_sleep(1000);

	for (int i = 0; i < RAWHID_PACKET_SIZE; ++i)	txBuf[i] = 0;
	txBuf[0] = 'C';
	txBuf[1] = 'F';
	txBuf[2] = 'G';
	txBuf[3] = 'L';

	LARGE_INTEGER frequency;
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	double dur = 0.0;
	double mint = 1000.0;
	double maxt = -1.0;
	double meant = 0.0;
	double resot;
	int n;

	n = -1;
	do
	{
		++n;
		ret = rawhid_recv(0, rxBuf, RAWHID_PACKET_SIZE, DEVICE_TIMEOUT_MS);
	} while (ret > 0);
	printf("read %d queued packets\n", n);

	do
	{
		if (::QueryPerformanceFrequency(&frequency) == FALSE)
			break;

		for (n = 0; n < 1000; ++n)
		{
			if (::QueryPerformanceCounter(&start) == FALSE)
				break;

			ret = rawhid_send(0, txBuf, RAWHID_PACKET_SIZE, DEVICE_TIMEOUT_MS);
			if (ret < 0)
				break;

			ret = rawhid_recv(0, rxBuf, RAWHID_PACKET_SIZE, DEVICE_TIMEOUT_MS);
			if (ret < 0)
				break;

			if (::QueryPerformanceCounter(&end) == FALSE)
				break;

			dur = static_cast<double>(end.QuadPart - start.QuadPart) / frequency.QuadPart;
			if (mint > dur)	mint = dur;
			if (maxt < dur)	maxt = dur;
			meant += dur;
		}

		if (n != 1000)
		{
			printf("error executing test after %d iterations!\n", n);
		}
		else
		{
			resot = 1.0 / frequency.QuadPart;
			meant /= n;
			printf("transmitted %d requests and received responses, each %d bytes\n", n, RAWHID_PACKET_SIZE);
			printf("min  latency: %.2f ms\n", mint*1000.0);
			printf("max  latency: %.2f ms\n", maxt*1000.0);
			printf("mean latency: %.2f ms\n", meant*1000.0);
			printf("resolution:   %.4f ms\n", resot*1000.0);

			/*
			TEENSY 2.0:
			transmitted 1000 request packets and received same amount of responses, each 64 bytes
				min  latency : 1.82
				max  latency : 2.37
				mean latency : 2.00
			
			TEENSY 3.1 without connected POWER at Notebook:
			transmitted 1000 request packets and received same amount of responses, each 64 bytes
				min  latency: 1.86
				max  latency: 2.16
				mean latency: 2.00				
			*/
		}
	} while (0);

	c.keyingMode = savedKeyingMode;
	if (!writeConfToDev(false))
		return false;

	return true;
}

bool printDitDahs()
{
	char txBuf[RAWHID_PACKET_SIZE];
	char rxBuf[RAWHID_PACKET_SIZE];
	int ret = 0;
	int millisSinceLastPress = 0;
	int n = 100;

	if (emulateKeyerHW)
		return true;

	do
	{
		ret = rawhid_recv(0, rxBuf, RAWHID_PACKET_SIZE, DEVICE_TIMEOUT_MS);
		if (ret < 0)
		{
			// error
			printf("error reading from device!\n");
			break;
		}
		else if (ret > 0 && rxBuf[0] == 'C' && rxBuf[1] == 'W')
		{
			printf("%d: %c%c -> %c\n", n
				, (rxBuf[2] & uDIT) ? '.' : ' '
				, (rxBuf[2] & uDAH) ? '-' : ' '
				, (rxBuf[3]) ? 'X' : ' '
				);
			++n;
			millisSinceLastPress = 0;
		}
		else
		{
			millisSinceLastPress += DEVICE_TIMEOUT_MS;
		}

	} while ( millisSinceLastPress < 5000 && ret >= 0 );

	if (ret >= 0 && millisSinceLastPress >= 5000)
	{
		printf("timeout waiting for key presses.\n");
		return true;
	}
	else
		return false;
}


int main(int argc, char * argv[])
{
	for (int k = 1; k < argc; ++k)
	{
		if (!strcmp(argv[k], "-e") || !strncmp(argv[k], "em", 2))
			emulateKeyerHW = true;
	}

	bool bReOpen = false;
	bool bQuit = false;

	while (!bQuit)
	{
		int r;

		if (!emulateKeyerHW)
		{
			::_sleep(1000);

			// C-based example is 16C0:0480:FFAB:0200
			r = rawhid_open(1, 0x16C0, 0x0480, 0xFFAB, 0x0200);
			if (r <= 0) {
				// Arduino-based example is 16C0:0486:FFAB:0200
				r = rawhid_open(1, 0x16C0, 0x0486, 0xFFAB, 0x0200);
				if (r <= 0) {
					printf("no rawhid device found - connect to USB port!\n");
					//return -1;
					::_sleep(1000);
					continue;
				}
			}
			printf("found rawhid device - requesting current config ..\n\n");

			if (!readConfFromDev())
			{
				printf("error reading config, possibly device went offline\n");
				rawhid_close(0);
				::_sleep(1000);
				continue;
			}
		}

		bReOpen = false;
		bQuit = false;
		while (!bReOpen && !bQuit)
		{
			unsigned menuVal;
			unsigned confVal;
			printMenuAndConf();
			scanf("%u", &menuVal);
			switch (menuVal)
			{
				default:	break;
				case 1:		if (!readConfFromDev())	bReOpen = true;		break;
				case 2:		if (!writeConfToDev(true))	bReOpen = true;	break;
				case 3:     if (!latencyTest())	bReOpen = true;	break;
				case 4:     if (!printDitDahs()) bReOpen = true; break;
				case 5:		bQuit = true;	break;
				case 25:	printf("multiply wpm value with 10, e.g. enter 130 for 13 wpm\n");	break;
			}
			if (menuVal >= 10)
			{
				printf("Enter new config value: ");
				fflush(stdout);
				int tmp;
				scanf("%d", &tmp);
				confVal = (tmp < 0) ? 0xffff : (tmp & 0xffff);
				switch (menuVal)
				{
					default:	break;
					case 10:	c.greenDitInpPin = confVal;		break;
					case 11:	c.redDahInpPin = confVal;		break;
					case 12:	c.sideToneSpkrOutPin = confVal;	break;
					case 13:	c.keyedOutPin = confVal;		break;
					case 14:	c.ditLEDpin = confVal;			break;
					case 15:	c.dahLEDpin = confVal;			break;
					case 16:	c.outLEDpin = confVal;			break;
					case 17:	c.invertDitInpPin = confVal;	break;
					case 18:	c.invertDahInpPin = confVal;	break;
					case 19:	c.invertOutPin = confVal;		break;
					case 20:	c.swapDitDahPins = confVal;		break;
					case 21:	c.ditSpkrFreq = confVal;		break;
					case 22:	c.dahSpkrFreq = confVal;		break;
					case 23:	c.keyingMode = confVal;			break;
					case 24:	c.autoSpace = confVal;			break;
					case 25:	c.keyingSpeedInWPMx10 = confVal;	break;
					case 26:	c.bounceLoadDecay = confVal;	break;
					case 27:	c.bounceLoadThresh = confVal;	break;
					case 30:;
				}
				if (!writeConfToDev(false))
					bReOpen = true;;
			}
		} // end while (!bReOpen && !bQuit)

		if (bReOpen)
			printf("error reading config, possibly device went offline\n");
		if (!emulateKeyerHW && (bReOpen || bQuit))
			rawhid_close(0);
	} // end while (!bQuit)
	return 0;
}

#if defined(OS_LINUX) || defined(OS_MACOSX)
// Linux (POSIX) implementation of _kbhit().
// Morgan McGuire, morgan@cs.brown.edu
static int _kbhit() {
	static const int STDIN = 0;
	static int initialized = 0;
	int bytesWaiting;

	if (!initialized) {
		// Use termios to turn off line buffering
		struct termios term;
		tcgetattr(STDIN, &term);
		term.c_lflag &= ~ICANON;
		tcsetattr(STDIN, TCSANOW, &term);
		setbuf(stdin, NULL);
		initialized = 1;
	}
	ioctl(STDIN, FIONREAD, &bytesWaiting);
	return bytesWaiting;
}
static char _getch(void) {
	char c;
	if (fread(&c, 1, 1, stdin) < 1) return 0;
	return c;
}
#endif


static char get_keystroke(void)
{
	if (_kbhit()) {
		char c = _getch();
		if (c >= 32) return c;
	}
	return 0;
}


