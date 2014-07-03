/**
 * signalCounter.c:
 * Raspberry Pi GPIO signal counter
 *
 * Counts signals detected on the specified GPIO pin and submits them via HTTP to an end point.
 * Count is persistent and is periodically written to the filesystem.
 * Input is debounced.
 * The HTTP request includes the system MAC address as a unique identifier.
 *
 * The interrupt code is based on isr.c example code from 
 * https://github.com/ngs/wiringPi/blob/master/examples/isr.c
 *
 * @author John Smith
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <wiringPi.h>
#include <time.h>

// what GPIO input pin are we using? (wiringPi pin number)
#define	PIN_INPUT 0

// LED to indicate activity
#define PIN_OUTPUT 2

// number of seconds to debounce input signal for
#define DEBOUNCE_INTERVAL_MS 200

// the amount of time between recording results (main execution loop interval)
#define RECORD_INTERVAL_MS 1000

// where on the file system to read and write the count from
#define SIGNAL_COUNT_PATH "/var/lib/signalCounter/count"


//	Global variable to count interrupts
//	Should be declared volatile to make sure the compiler doesn't cache it
static volatile unsigned long long signalCount = 10;

// Stores the previous interrupt time, used for debouncing
static unsigned long long interruptTimePrevious;

// file pointer to persistent signal count
FILE *filePointerCount;

/**
 * blinks the LED. Defined so it can be run in a separate thread
 */
PI_THREAD (blinkLed)
{
    digitalWrite(PIN_OUTPUT, HIGH);
    delay(DEBOUNCE_INTERVAL_MS);
    digitalWrite(PIN_OUTPUT, LOW);
}

/**
 * the interrupt to fire when the input pin is pulled up to 3v
 */
void signalInterrupt (void)
{
    struct timeval tv;
    
    gettimeofday(&tv, NULL);
    
    // seconds to since epoch to ms + microseconds since epoch to ms = ms since epoch
    unsigned long long interruptTime =
        (unsigned long long)(tv.tv_sec) * 1000 +
        (unsigned long long)(tv.tv_usec) / 1000;
    
    if( (interruptTime - interruptTimePrevious) < DEBOUNCE_INTERVAL_MS) {
        // assume this is just jitter on the signal, ignore the IRQ
        return;
    }
    
    interruptTimePrevious = interruptTime;
    
    signalCount++;
    
    // blink the LED to show we recorded the box
    piThreadCreate(blinkLed);
}

int initPersistentCountFile(void)
{
    // try and create the directory structure
    // init a string - can hold up to 256 chars
    char characterArray[256];
    
    // init the 'character pointer' for the above string
    char *p = NULL;
    size_t len;
    
    // convert the string to a 'character array'
    snprintf(characterArray, sizeof(characterArray), "%s", SIGNAL_COUNT_PATH);
    
    len = strlen(characterArray);
    
    // replace the last char with '0' if it's '/', for some reason
    if(characterArray[len - 1] == '/') {
        characterArray[len - 1] = 0;
    }
    
    // loop over each character, via the character pointer
    for(p = characterArray + 1; *p; p++) {
        // break at each '/' and create the dir
        if(*p == '/') {
            *p = 0;
            // we're modifiying string's pointer, but how does it know what char to read up to??
            mkdir(characterArray);
            *p = '/';
        }
    }
    
    // open / create the file to hold persistent count
    filePointerCount = fopen(SIGNAL_COUNT_PATH, "a+");
    
    if(!filePointerCount) {
        fprintf(stderr, "Failed to open persistent count file: %s\n", strerror (errno));
        return -1;
    }
    
    unsigned long long rich;
    
    fscanf(filePointerCount, "%llu", &signalCount);
    
    printf("this is what we read from the file pointer %llu", signalCount);
    
    return 0;
}

/*
 *********************************************************************************
 * main
 *********************************************************************************
 */

int main (void)
{
    // init the wiringPi library
    if (wiringPiSetup () < 0)
    {
        fprintf(stderr, "Unable to setup wiringPi: %s\n", strerror (errno));
        return 1 ;
    }
    
    // set up an interrupt on our input pin
    if (wiringPiISR (PIN_INPUT, INT_EDGE_FALLING, &signalInterrupt) < 0)
    {
        fprintf(stderr, "Unable to setup ISR: %s\n", strerror (errno));
        return 1 ;
    }
    
    // configure the output pin for output. Output output output
    pinMode(PIN_OUTPUT, OUTPUT);
    
    // configure persistent signal count
    if(initPersistentCountFile() < 0) {
        return 1;
    }
    
    // main execution loop of this application
    //for(;;)
    //{
    //    delay(RECORD_INTERVAL_MS);
    //    printf("signals counted: %llu", signalCount);
    //}

    return 0;
}
