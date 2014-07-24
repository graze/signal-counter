signalCounter
=============

Embedded Raspberry Pi system for counting and recording input signals on the GPIO header, submitting them via HTTP

##Operation##
Each time a signal is detected, the current timestamp is written to a file in CSV format. The application then attempts to POST the contents of the CSV file to an HTTP endpoint. Included in the request is the MAC address of the Raspberry Pi's eth0 interface. This is for identification purposes.

A request might look like:

`macAddress=b8:27:eb:b5:c6:b4&csv=1406212693\n1406212720\n1406212775`

###Network Resilience###

The application will continue recording hits to file, even without a network connection (unitl there's no space left on the Pi's filesystem at least!).
A thread periodically check's for a CSV that has yet to be submitted and attempts to POST it.

##Dependencies##
signalCounter requires the wiringPi library and libcurl

https://github.com/WiringPi/WiringPi

http://curl.haxx.se/libcurl/

##Compiling##
Try this:

`gcc -o signalCounter signalCounter.c -lwiringPi -lcurl`

### License
The content of this library is released under the **MIT License** by
**Nature Delivered Ltd.**.<br/> You can find a copy of this license at http://opensource.org/licenses/mit.
