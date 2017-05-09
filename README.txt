
This project is "CW keyer" utilizing a Teensy microController.

You connect a Straight or Iambic A/B paddle to the microController.

A (piezo) buzzer produces the latency-free side tone.
  Adding a low-pass filter you can enhance the audio quality
  and connect some speakers.

Different from other CW controllers:
The controller is also connected to the (Windows) PC;
in this case all keys are reported to the PC via HID USB connection.
You don't need a driver :-)
Intention is to use that information for a CW training program
or an SDR transceiver software.

Besides that info, you can configure CW speed, tone frequencies, etc. from PC.


HARDWARE PARTLIST:

* 1 x Teensy 3.1 with pins (you should be able to other another Teensy model with enough pins)
https://www.pjrc.com/store/teensy31.html
http://www.watterott.com/de/Teensy-USB-Board-v31-MK20DX256VLH7-mit-Pins
for overview, see https://www.pjrc.com/teensy/index.html

* 1 x small Breadboard
http://www.pollin.de/shop/dt/MDM2OTg0OTk-
http://www.watterott.com/de/Breadboard-klein-selbstklebend

* 1 x Iambic Dual Paddle, e.g.
http://www.mfjenterprises.com/Product.php?productid=MFJ-564B

* 1 x Piezo buzzer, e.g.
http://www.reichelt.de/SUMMER-EPM-121/3/index.html?&ARTICLE=35927

* some Jumper Wirers M/M
http://www.watterott.com/de/Jumper-Wires-MM-200mm



SOFTWARE PREREQUISITES for Development/Microcontroller Upload:
* Arduino 1.8.2 / Genuino with Teensyduino 1.36
https://www.pjrc.com/teensy/td_download.html


SOFTWARE PREREQUISITES for Development/PC Control:
* Visual Studio Express 2013 for Windows Desktop - or higher version.
With minor modification you should also be able to use other compiler.
https://www.microsoft.com/en-us/download/details.aspx?id=44914



LICENSE:
MIT, (c)2017 Hayati Ayguen <h_ayguen@web.de>

exception:
- Teensy RawHID source files
  see http://www.pjrc.com/teensy/rawhid.html

