signalCounter
=============

Embedded Raspberry Pi system for counting and recording input signals on the GPIO header

##Dependencies##
signalCounter requires the wiringPi library and libcurl

https://github.com/WiringPi/WiringPi
http://curl.haxx.se/libcurl/

##Compiling##
Try this:

`gcc -o signalCounter signalCounter.c -lwiringPi -lcurl`
