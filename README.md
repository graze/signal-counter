signal-counter
=============

Embedded Raspberry Pi system for counting and recording input signals on the GPIO header, submitting them via HTTP.

Written for recording the output of a 24v PLC light gate sensor, the interface for which can be found here: http://123d.circuits.io/circuits/275120-24v-sensor-input-to-raspberry-pi-gpio

##Operation##
Each time a signal is detected, the current timestamp is written to a file in CSV format. The application then attempts to POST the contents of the CSV file to an HTTP endpoint. Included in the request is the MAC address of the Raspberry Pi's eth0 interface. This is for identification purposes.

A request might look like:

`macAddress=b8:27:eb:b5:c6:b4&csv=1406212693%0A1406212720%0A1406212775`

###Network Resilience###

The application will continue recording hits to file, even without a network connection. A thread periodically check's for a CSV that has yet to be submitted and attempts to POST it.

##Dependencies##
signal-counter requires the wiringPi library and libcurl

https://github.com/WiringPi/WiringPi

http://curl.haxx.se/libcurl/

##Compiling##
Try this:

`gcc -o signalCounter signalCounter.c -lwiringPi -lcurl`

##Starting the program on boot##
Move the compiled program somewhere sensible, like `/usr/local/bin/signalCounter` (or create a symlink), and add the following line to `/etc/rc.local`:

`/usr/local/bin/signalCounter > /dev/null  2>&1 &`

This will run signal-counter in the background, redirecting output from stdout and stderr to `/dev/null`. If you want to log output from the application, replace `/dev/null` with a file of your choice.

### License
The content of this library is released under the **MIT License** by
**Nature Delivered Ltd.**.<br/> You can find a copy of this license at http://opensource.org/licenses/mit.
