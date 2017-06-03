![banner](https://cloud.githubusercontent.com/assets/1314694/14457766/df8e98bc-00a4-11e6-8936-bba6cd26709f.jpg)

# signal-counter


Embedded Raspberry Pi system for counting and recording input signals on the GPIO header, submitting them via HTTP.

Written for recording the output of a 24v PLC sensors via a custom made PCB, the schematic for which can be found here: https://circuits.io/circuits/275120-24v-sensor-input-to-raspberry-pi-gpio

## Operation
Each time a signal is detected, the current timestamp is written to a file in CSV format. The application then attempts to POST the contents of the CSV file to an HTTP endpoint. In order to identify the Raspberry Pi, the eth0 MAC address is included in the request.

A request might look like:

`macAddress=b8:27:eb:b5:c6:b4&csv=1406212693%0A1406212720%0A1406212775`

### Network Resilience

The application will continue recording hits to file, even without a network connection. A thread periodically checks for a CSV that has yet to be submitted and attempts to POST it.

## Compiling
signal-counter requires the wiringPi library and libcurl

- http://wiringpi.com/download-and-install/
- `sudo apt-get install libcurl4-openssl-dev`

To compile on a Raspberry Pi, run the following:

`gcc -o signalCounter signalCounter.c -lwiringPi -lcurl`

## Usage
`signalCounter [endpoint] (trigger_interval_ms)`

- `endpoint` - the HTTP endpoint that recorded signals are POSTed to
- `trigger_interval_ms` - the number of ms of signal required before a hit is recorded. If no argument is supplied this defaults to 300ms.

## Starting the program on boot
Move the compiled program somewhere sensible, like `/usr/local/bin/signalCounter` (or create a symlink), and add the following line to `/etc/rc.local`:

`/usr/local/bin/signalCounter http://server/end-point > /dev/null  2>&1 &`

This will run signal-counter in the background, redirecting output from stdout and stderr to `/dev/null`. If you want to log output from the application, replace `/dev/null` with a file of your choice.

### License
The content of this library is released under the **MIT License** by
**Nature Delivered Ltd.**.<br/> You can find a copy of this license at http://opensource.org/licenses/mit.
