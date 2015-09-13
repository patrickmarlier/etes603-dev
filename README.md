EgisTec ES603 driver development and debugging
==============================================

This repository is for debugging and development purpose of the EgisTec (aka Lightuning) ES603 fingerprint device driver.
All dated from 2012 and it is just a reincarnation of the google repository with some adjustments.

The asynchronous ES603 driver has been integrated into the mainline of libfprint.
Please use latest libfprint library and report bugs in [libfprint bugzilla](https://bugs.freedesktop.org/enter_bug.cgi?product=libfprint).

Outline
-------

* [Repository content](#repo)
* [Story](#story)
* [Analysis](#analysis)
* [Troubleshooting](#troubleshooting)
* [Acknowledgments](#acknowledgments)


<a name="repo">Repository content</a>
-------------------------------------

* `etes603.c`: Synchronous driver
* `fake_fp.c`: Fake functions of libfprint for debug purpose
* `contact.c`: Program to test the contact detection of the device
* `dumpregs.c`: Program to dump all registers of the device
* `gui.c`: GUI program to debug calibration parameters
  * `MODE_FRAME`
  * `MODE_IMAGE`
  * `MODE_IMAGE_FULL`
  * `MODE_LOGGING`
* `assemble.c`: Program to merge frames to compose a fingerprint image
* `leds.c`: Program to test LEDs of the device


<a name="story">Story</a>
---------------------------------------

All started in 2012 when I bought a laptop to replace my desktop computer.
My choice was the Lenovo Essential B570.
It comes with Windows 7 Home edition so the first thing I did was to change the HDD to the SSD from my old desktop PC and to install Ubuntu.
After fixing the different issues (GPT partition, 3Gb SATA link, wifi, ...), I focused on the embedded fingerprint device.

`lsusb` command gives this:
<pre><code>Bus 002 Device 003: ID 1c7a:0603 LighTuning Technology Inc.</code></pre>
LighTuning was bought by EgisTec few years ago but both does not provide linux support of the device.
On linux, libfprint seems the most advanced framework for fingerprint.
My search about EgisTec ended to the driver from Alexey for the SS801U device.
However, the device embedded in my laptop is a ES603 and after testing, I concluded my device was not compatible.

I could wait someone to implement the driver but since I had no experience about this, I decided to put hands under the hood.
Here the plan:

 1.  Use the Windows support to monitor USB traffic between the fingerprint device and the host
 2.  Create a simple linux application to communicate with a USB device
 3.  Understand part of the traces and protocol to implement it into the testing application
 4.  Iterate previous step until the protocol is reversed to capture a fingerprint
 5.  Add support to libfprint

Once booted on Windows, let's try to gather informations!
A quick search on EgisTec website to find the ES603 characteristics (it could help to understand the protocol):

 *  ES603 (AF or WB?)
 *  192 x 4 pixels @ 508 dpi
 *  auto-calibration technology
 *  finger detection: based on E-Field (AC-Capacitance)
 *  Imaging/navigating mode: Typical 15 mA
 *  Finger detection mode: Typical &lt;500uA
 *  Fly-Estimation&reg; technology delivers complete fingerprint without image reconstruction.

Two files compose the Windows driver:

 *  `fpsensor.sys`: Low level driver
 *  `nbmats1sdk.dll`: High level driver

Egistec BioExcess software permits to exploit this driver on Windows.

In order to understand the protocol, we need to capture the USB communication on Windows 7.
I used different softwares but two are quite good.
USBlyzer has a trial version for 33 days however data frame size is also limited.
USBSnoop is free but it is not easy to install and to make it work properly.
All the captured traces will be used to reverse engineer how to communicate with the device.
To understand properly the protocol, we need a lot of traces from device activation and deactivation, from no finger, from one finger, from many fingers, ...

The first thing you figure out is the fixed header of usb frames (see Analysis section).
The rest is much more difficult to understand and even with extensive testing I did not figure out all the purpose of all registers (EgisTec, please release the specifications).
Since I have no experience about fingerprint device, I read briefly sources of other drivers in the libfprint package.

I developped a small C application with GUI and synchronous usb communcation to figure out how the sensor works.
The second step was to implement the protocol for libfprint (asynchronous usb communication) and to test it with fprint\_demo.
The code was available at <a href="http://code.google.com/p/etes603/">code.google.com/p/etes603/</a> (Now dead) but it now integrated into libfprint library.

I sold my Lenovo computer in 2014 and so I do not have access to the device anymore.
However, if you want to experience the driver, the USB dongle is really cheap and can be found at [DealExtreme](http://www.dx.com/s/109073): SKU 109073 Mini USB 2.0 Biometric Fingerprint Reader Password Security Lock for PC about USD 14.


<a name="analysis">Analysis</a>
-------------------------------

The device uses endpoint 0x02 to send a request (from host to device) and endpoint 0x81 to receive (from device to host).

The fixed header of usb frames (5 bytes):

* Host to device: `'E' 'G' 'I' 'S' 0x09`
* Device to host: `'S' 'I' 'G' 'E' 0x0A`


### Sensor's registers

In this following tables, this is all registers used in the driver.
However I was not able to determine purposes of all registers.

<table style='font-family:"Courier New", Courier, monospace;'>
<tr><th>Register number</th><th>Register purpose</th><th>Register values</th></tr>
<tr><td>0x02</td><td>Mode control</td><td>Sleep:0x30 Contact:0x31 Sensor:0x33 FlyEstimation:0x34</td></tr>
<tr><td>0x03</td><td>Contact register/capacitance?</td><td>(value &gt;&gt; 4) &amp; 0x1 = 1 if finger contact otherwise 0</td></tr>
<tr><td>0x04</td><td>?</td><td></td></tr>
<tr><td>0x10</td><td>MVS FRMBUF control</td><td></td></tr>
<tr><td>0x1A</td><td>?</td><td></td></tr>
<tr><td>0x20</td><td>?</td><td>def: 0x00</td></tr>
<tr><td>0x21</td><td>Small gain</td><td>def: 0x23</td></tr>
<tr><td>0x22</td><td>Normal gain</td><td>def: 0x21</td></tr>
<tr><td>0x23</td><td>Large gain</td><td>def: 0x20</td></tr>
<tr><td>0x24</td><td>?</td><td>def: 0x14</td></tr>
<tr><td>0x25</td><td>?</td><td>def: 0x6A</td></tr>
<tr><td>0x26</td><td>VRB again?</td><td>def: 0x00</td></tr>
<tr><td>0x27</td><td>VRT again?</td><td>def: 0x00</td></tr>
<tr><td>0x28</td><td>?</td><td>def: 0x00</td></tr>
<tr><td>0x29</td><td>?</td><td>def: 0xC0</td></tr>
<tr><td>0x2A</td><td>?</td><td>def: 0x50</td></tr>
<tr><td>0x2B</td><td>?</td><td>def: 0x50</td></tr>
<tr><td>0x2C</td><td>?</td><td>def: 0x4D</td></tr>
<tr><td>0x2D</td><td>?</td><td>def: 0x03</td></tr>
<tr><td>0x2E</td><td>?</td><td>def: 0x06</td></tr>
<tr><td>0x2F</td><td>?</td><td>def: 0x06</td></tr>
<tr><td>0x30</td><td>?</td><td>def: 0x10</td></tr>
<tr><td>0x31</td><td>?</td><td>def: 0x02</td></tr>
<tr><td>0x32</td><td>?</td><td>def: 0x14</td></tr>
<tr><td>0x33</td><td>?</td><td>def: 0x34</td></tr>
<tr><td>0x34</td><td>?</td><td>def: 0x01</td></tr>
<tr><td>0x35</td><td>?</td><td>def: 0x08</td></tr>
<tr><td>0x36</td><td>?</td><td>def: 0x03</td></tr>
<tr><td>0x37</td><td>?</td><td>def: 0x21</td></tr>
<tr><td>0x41</td><td>Encryption byte1</td><td>def: 0x12</td></tr>
<tr><td>0x42</td><td>Encryption byte2</td><td>def: 0x34</td></tr>
<tr><td>0x43</td><td>Encryption byte3</td><td>def: 0x56</td></tr>
<tr><td>0x44</td><td>Encryption byte4</td><td>def: 0x78</td></tr>
<tr><td>0x45</td><td>Encryption byte5</td><td>def: 0x90</td></tr>
<tr><td>0x46</td><td>Encryption byte6</td><td>def: 0xAB</td></tr>
<tr><td>0x47</td><td>Encryption byte7</td><td>def: 0xCD</td></tr>
<tr><td>0x48</td><td>Encryption byte8</td><td>def: 0xEF</td></tr>

<tr><td>0x50</td><td>required for contact detection?</td><td>init: 0x0F valid: value | 0x80 / 0x8F</td></tr>
<tr><td>0x51</td><td>?</td><td>valid: value &amp; 0xF7 / 0x30</td></tr>
<tr><td>0x59</td><td>?</td><td>valid: 0x18</td></tr>
<tr><td>0x5A</td><td>?</td><td>valid: 0x08</td></tr>
<tr><td>0x5B</td><td>?</td><td>valid: 0x00/0x10</td></tr>

<tr><td>0x70</td><td>Sensor model byte0 (version?, firmware?)</td><td>def: 0x4A</td></tr>
<tr><td>0x71</td><td>Sensor model byte1</td><td>def: 0x44</td></tr>
<tr><td>0x72</td><td>Sensor model byte2</td><td>def: 0x49</td></tr>
<tr><td>0x73</td><td>Sensor model byte3</td><td>def: 0x31</td></tr>

<tr><td>0x93</td><td>?</td><td></td></tr>
<tr><td>0x94</td><td>?</td><td></td></tr>

<tr><td>0xE0</td><td>Sensor Gain</td><td>init: 0x04, GAIN_SMALL_INIT: 0x23 (default gain)</td></tr>
<tr><td>0xE1</td><td>For brightness and contrast</td><td>Maximum value for VRT: 0x3F</td></tr>
<tr><td>0xE2</td><td>For brightness and contrast</td><td>Maximum value for VRB: 0x3A</td></tr>
<tr><td>0xE3</td><td>Used for contact detection</td><td>Maximum value for DTVRT: 0x3A</td></tr>
<tr><td>0xE5</td><td>VCO Control</td><td>0x13 (IDLE?), 0x14 (REALTIME)</td></tr>
<tr><td>0xE6</td><td>DC Offset</td><td>Minimum value for DCoffset: 0x00  Maximum value for DCoffset: 0x35</td></tr>

<tr><td>0xF0</td><td>?</td><td>init:0x00 close:0x01</td></tr>
<tr><td>0xF2</td><td>?</td><td>init:0x00 close:0x4E</td></tr>

</table>


### Reading sensor registers

Request:
<pre><code>CMD_READ_REG NN XX ...</code></pre>
`CMD_READ_REG` is 0x01.
NN is the number of registers to read.
XX are the registers number.

Answer:
<pre><code>CMD_OK XX ...</code></pre>
`CMD_OK` is 0x01.
XX are register values.


### Writing sensor registers

Request:
<pre><code>CMD_WRITE_REG NN XX YY ...</code></pre>
`CMD_WRITE_REG` is 0x02.
NN is the number of registers to write.
XX are the registers number.
YY are the new registers values.

Answer:
<pre><code>CMD_OK</code></pre>


### Capturing an image (frame)

Request:
<pre><code>WIDTH, 0x01, GAIN, VRT, VRB</code></pre>
<pre><code>WIDTH, 0x00, 0x00, 0x00, 0x00</code></pre>
it seems that the only working WIDTH is 0xC0 (192) and it the only value used in the windows driver.
If the second byte is 0, registers for GAIN/VRT/VRB will be used.

Answer:
<pre><code>RAWDATA</code></pre>
Frame is 384 bytes long (depending on WIDTH).
The image encoding is 4 bits per pixel raw image.
So the resulting image is 192 * 4 pixels.


### Capturing an image using Fly-Estimation

Image capture (aka Fly-Estimation%reg; technology) completes fingerprint without software reconstruction.

Request:
<pre><code>HEIGHT0, HEIGHT1, UNK1, UNK2, UNK3</code></pre>
<pre><code>0xF4, 0x02, 0x01, 0x64, 0x00</code></pre>
In the windows driver, values are fixed to the above values.
1st 2nd bytes is unsigned short for height, but only on value range:
`0x01 0xF4 (500), 0x02 0x00 (512), 0x02 0xF4 (756) are ok.`
3rd byte : ?? but changes frame size.
4th byte : ??.
5th byte : motion sensibility?.

Answer:
<pre><code>RAWDATA</code></pre>
The sensor buffered 64000 bytes of data. The encoding used is a 4 bits raw.
So the resulting image is `256*500` pixels.
The command is synchronous (blocking).



### Controlling LEDs on selected models (CMD 0x60)

Some models have 2 LEDs (red and blue) that can be controlled with CMD 0x60.

Request CMD 0x60: ask status of LEDs:
<pre><code>0x60 0x01</code></pre>

Answer CMD 0x60: returns OK and status of LEDs
<pre><code>CMD_OK 0xXX</code></pre>

Request CMD 0x60 write: set status of LEDs
<pre><code>0x60 0x02 0xXX</code></pre>

None: 0x00
Red: 0x10
Blue: 0x20
Red+Blue: 0x10|0x20 = 0x30
(Unknown behaviour with value 0x11, 0x21, 0x31)

Answer CMD 0x60 write:
<pre><code>CMD_OK 0xXX</code></pre>


### Unknown CMD 0x20 / CMD 0x25

I was not able to determine the exact purpose of those commands.

Request CMD 0x20:
<pre><code>0x20</code></pre>

Answer CMD 0x20:
<pre><code>0x05 0x00 0x00</code></pre>

Request CMD 0x25:
<pre><code>0x25</code></pre>

Answer CMD 0x25:
<pre><code>CMD_OK 0x00</code></pre>


### Tuning DCoffset register

 1.  Set initial values: `gain=0x23 min_dcoffset=0x00 max_dcoffset=0x35`
 2.  Get a frame with the current gain and use fixed `vrt=0x15 vrb=0x10`
 3.  Use dichotomy to find at what dcoffset value the frame start to be completely black
 4.  Reduce gain if cannot get a completely black frame


### Tuning DTVRT register

DTVRT tuning permit to detect finger contact with the sensor.

 1.  Read registers `0x50, 0x51, 0x59, 0x5A, 0x5B` to reset the initial values at the end of tuning
 2.  Change to sleep mode
 3.  Set `VCO_CONTROL` to `VCO_IDLE`
 4.  Set register `0x50` to `value | 0x80`
 5.  Set register `0x51` to `value & 0xF7`
 6.  Set registers `0x59` to `0x18, 0x5A` to `0x08` and `0x5B` to `0x00`
 7.  Change to contact mode
 8.  Set `DTVRT` to MAX
 9.  Read register `0x03` to value
 10. If `(value >> 4) & 1 == 1`, then we found the tuned `DTVRT` value (current + 5)
 11. Else Set `DTVRT` register to current - 5 and read register `0x03` until contact is found
 12. If `DTVRT` reaches 0, reduce DCoffset of 1 and restart tuning


### Tuning VRT and VRB registers

 1.  Initial values are `VRT = 0x0A`, `VRB = 0x10`
 2.  Reduce DCoffset of 1 (reset it at the end of this tuning)
 3.  Get a frame with this specific vrt/vrb and gain found in tune DCoffset
 4.  Calculate the histogram of the image
 5.  The image should not have more than 95% of black or white pixel (otherwise need to decrease/increase DCoffset)
 6.  increase VRT/VRB until you found a balanced image


### Detecting a finger

The detection of a finger can be done in different ways.
The first way is to capture contiguously frames and that if the frame is black or not.
But the sensor has a specific way to detect the finger contact with the E-Field (as advertised on the website).
Following how to use the E-Field:

 1.  Change to mode contact
 2.  Set registers `0x59` to `0x18`, `0x5A` to `0x08`, `0x5B` to `0x10`
 3.  Read register `0x50` and set it to `(value & 0x7F) | 0x80`
 4.  Read register `0x03` until value is `(value >> 4) & 0x1 == 1` which indicates contact
 5.  Add sleep of 5ms between each read and add a timeout
 6.  When done, change to sleep mode



<a name="troubleshooting">Troubleshooting</a>
-------------------------------------------

### Problem of rights on the device / Unable to open the device

You may have to change rights to access the fingerprint device for non-root:

* `lsusb -d 1c7a:0603` to figure out the bus device id
* `sudo chmod a+rw /dev/bus/usb/00X/00Y`

but the better way to set rights at each reboot is to add a udev rules.
Create the file <code>/etc/udev/rules.d/90-egis.rules</code> with the following content:
<pre><code>SUBSYSTEM=="usb", ACTION=="add", ATTR{idVendor}=="1c7a", ATTR{idProduct}=="0603", MODE="0666"</code></pre>


### After an error, the device is not found

You can experience some problem with the driver when debugging, in this case the device can disconnect and reconnect but then the number on the USB bus changed.
So you need to change again the permission.
The device could be also in a weird state, unplug it and replug it.


<a name="acknowledgments">Acknowledgments</a>
-------------------------------------------

Thanks to all people who helps to test the driver with special thanks to:
 *  Chase Montgomery (BLuFeNiX)
 *  Matthias Macha
 *  Vasily Khoruzhick

