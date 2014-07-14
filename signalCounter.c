/**
 * signalCounter.c:
 *
 * Raspberry Pi GPIO signal counter
 *
 * Counts signals detected on the specified GPIO pin and submits them via HTTP to an end point.
 * Count is persistent and is periodically written to the filesystem.
 * Input is debounced.
 * The HTTP request includes the system MAC address as a unique identifier.
 *
 * The interrupt code is based on isr.c example code from
 *
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
#include <unistd.h>
#include <curl/curl.h>

// what GPIO input pin are we using? (wiringPi pin number)
#define	PIN_INPUT 0

// LED to indicate activity
#define PIN_OUTPUT 2

// the amount of time between recording results (main execution loop interval)
#define RECORD_INTERVAL_MS 1000

// each time a signal is requested, a timestamp is recorded here
#define PATH_SIGNAL_COUNT "/var/lib/signalCounter/count"

// the count file is moved to here before it is submitted
#define PATH_SIGNAL_COUNT_SWAP "/tmp/signalCounterCount.swp"

#define PATH_MAC_ADDRESS_ETH0 "/sys/class/net/eth0/address"

// number of seconds to debounce input signal for
#define DEBOUNCE_INTERVAL_MS 200

// where the signal count CSV string will be posted to
#define END_POINT_URL "http://dispatch.john.dev.graze.com/signal-count/record-box-formed-csv"


// Stores the previous interrupt time, used for debouncing
static unsigned long long interruptTimePrevious;

// are currently submitting the signal count?
static int isProcessingCountFile = -1;

/**
 * record the signal count to CSV file
 */
int fileRecordSignalCount(void)
{
    // try and create the directory structure
    // init a string - can hold up to 256 chars
    char characterArray[256];
    
    // init the 'character pointer' for the above string
    char * p;
    p = NULL;
    size_t len;
    
    // convert the string to a 'character array'
    snprintf(characterArray, sizeof(characterArray), "%s", PATH_SIGNAL_COUNT);
    
    len = strlen(characterArray);
    
    // replace the last char with '0' if it's '/', for some reason
    if(characterArray[len - 1] == '/') {
        characterArray[len - 1] = 0;
    }
    
    // loop over each character, via the character pointer
    for(p = characterArray + 1; * p; p++) {
        // break at each '/' and create the dir
        if(* p == '/') {
            * p = 0;
            // we're modifiying string's pointer, but how does it know what char to read up to??
            mkdir(characterArray);
            * p = '/';
        }
    }
    
    FILE * filePointerCount;
    
    // create/open existing file for append
    filePointerCount = fopen(PATH_SIGNAL_COUNT, "a");
    
    if(filePointerCount == NULL) {
        fprintf(stderr, "Failed to open count file: %s\n", strerror(errno));
        return -1;
    }
    
    fprintf(filePointerCount, "%lld\n", time(NULL));
    
    fclose(filePointerCount);
    
    printf("signal was recorded to file\n");
    
    return 0;
}

int fileSwapFileExists(void)
{
    return access(PATH_SIGNAL_COUNT_SWAP, F_OK);
}

int fileCountFileExists(void)
{
    return access(PATH_SIGNAL_COUNT, F_OK);
}

int fileMoveCountToSwap(void)
{
    return rename(PATH_SIGNAL_COUNT, PATH_SIGNAL_COUNT_SWAP);
}

/**
 * read the whole swap file into memory. If this proves too memory hungry, split into smaller chunks
 *
 */
char * fileGetFileContents(char * fileName)
{
    char * fileContents;
    long fileSize;
    
    FILE * swapFile = fopen(fileName, "rb");
    
    // figure out how big this file is
    fseek(swapFile, 0, SEEK_END);
    
    fileSize = ftell(swapFile);
    
    rewind(swapFile);
    
    // allocate enough memory for the contents of the file
    fileContents = malloc((fileSize + 1) * (sizeof(char)));
    
    // load the contents of the file into the fileContents var
    fread(fileContents, sizeof(char), fileSize, swapFile);
    
    fclose(swapFile);
    
    // null terminate
    fileContents[fileSize] = 0;
    
    return fileContents;
}

char * fileGetSwapFileContents(void)
{
    return fileGetFileContents(PATH_SIGNAL_COUNT_SWAP);
}

char * fileGetMacAddress(void)
{
    return fileGetFileContents(PATH_MAC_ADDRESS_ETH0);
}

/**
 * Submit (via HTTP POST) the CSV to an endpoint
 * Based on the example from here: http://curl.haxx.se/libcurl/c/http-post.html
 */
int requestPostCsv(csv)
{
    // init
    curl_global_init(CURL_GLOBAL_ALL);
    
    CURL * curl;
    
    //get a curl handle
    curl = curl_easy_init();

    if(curl) {
        // set the end point
        curl_easy_setopt(curl, CURLOPT_URL, END_POINT_URL);
        
        char * postString = NULL;
        asprintf(&postString, "csv=%s&macAddress=%s", fileGetSwapFileContents(), fileGetMacAddress());
        
        // specify post data
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postString);
        
        // send response body to /dev/null
        FILE *devNull = fopen("/dev/null", "w+");
        
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, devNull);

        // make the request
        CURLcode res;
        
        res = curl_easy_perform(curl);
        
        fclose(devNull);
        
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }
        else {
            printf("request made\n");
        }
        
        // clean up
        curl_easy_cleanup(curl);
    }
    
    curl_global_cleanup();
    return 0;
}

/**
 * blink the LED
 */
void ledBlink(int durationMs)
{
    digitalWrite(PIN_OUTPUT, HIGH);
    delay(durationMs);
    digitalWrite(PIN_OUTPUT, LOW);
}

/**
 * blink LED for use with wiringPi threading interface.
 * LED will be on for the duration of debouncing interval
 */
PI_THREAD(ledSignalCounted)
{
    ledBlink(DEBOUNCE_INTERVAL_MS);
}

PI_THREAD(processCountFile)
{
    // are we already processing something
    if(isProcessingCountFile > -1) {
        printf("something is already being processed\n");
        return;
    }
    
    isProcessingCountFile = 0;
    
    // does a swap already exist?
    // this will normally be false, unless something crashed / lost power
    // before it was submitted in a previous run
    if(fileSwapFileExists() < 0) {
        // no swap file. Is there anything to process?
        if(fileCountFileExists() < 0) {
            printf("nothing to process\n");
            // nothing to process
            isProcessingCountFile = -1;
            return;
        }
        else {
            printf("count file exists\n");
        }
        
        // move the count file to the swap file
        if(fileMoveCountToSwap() < 0) {
            // something went wrong
            // @todo log the error
            isProcessingCountFile = -1;
            return;
        }
    }
    else {
        printf("swap file already exists\n");
    }
    
    // submit the contents of the file
    requestPostCsv();
    
    isProcessingCountFile = -1;
}

/**
 * the interrupt to fire when the input pin is pulled up to 3v
 */
void signalIsr (void)
{
    struct timeval tv;
    
    gettimeofday(&tv, NULL);
    
    // seconds to since epoch to ms + microseconds since epoch to ms = ms since epoch
    unsigned long long interruptTime =
        (unsigned long long)(tv.tv_sec) * 1000 +
        (unsigned long long)(tv.tv_usec) / 1000;
    
    if( (interruptTime - interruptTimePrevious) < DEBOUNCE_INTERVAL_MS) {
        // assume this is just jitter, ignore the IRQ
        return;
    }
    
    interruptTimePrevious = interruptTime;
    
    // record the signal count to file
    fileRecordSignalCount();
    
    // blink the LED to show we recorded the signal
    piThreadCreate(ledSignalCounted);
    
    // attempt to submit the count file
    piThreadCreate(processCountFile);
}

/**
 * init and run the application
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
    if (wiringPiISR (PIN_INPUT, INT_EDGE_FALLING, &signalIsr) < 0)
    {
        fprintf(stderr, "Unable to setup ISR: %s\n", strerror (errno));
        return 1 ;
    }
    
    // configure the output pin for output. Output output output
    pinMode(PIN_OUTPUT, OUTPUT);
    
    // create a thread to watch for new signal count files
    //piThreadCreate(recordSignalCount);
    
    // blink 3 times - we're ready to go
    ledBlink(300);
    delay(300);
    ledBlink(300);
    delay(300);
    ledBlink(300);
    
    printf("signalCount started\n");
    
    for(;;) {
        delay(2000);
    }

    return 0;
}
