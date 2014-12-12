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
#include <stdbool.h>
#include <wiringPi.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>

// what GPIO input pin are we using? (wiringPi pin number)
#define	PIN_INPUT 0

// LED to indicate activity
#define PIN_OUTPUT 2

// each time a signal is requested, a timestamp is recorded here
#define PATH_SIGNAL_COUNT "/var/lib/signalCounter/count"

// the count file is moved to here before it is submitted
#define PATH_SIGNAL_COUNT_SWAP "/tmp/signalCounterCount.swp"

#define PATH_MAC_ADDRESS_ETH0 "/sys/class/net/eth0/address"

// number of seconds we want signal for before counting as an actual hit
#define TRIGGER_INTERVAL_MS 300

// where the signal count CSV string will be posted to
#define END_POINT_URL "http://dispatch/uk/box-form/record-signal-counter-csv"

static unsigned long long interruptTimeMsRising = 0;

// are currently submitting the signal count?
static bool isProcessingCountFile = false;

/**
 * record the signal count to CSV file
 */
int fileRecordSignalCount(unsigned long long interruptTimeMs)
{
    // try and create the directory structure
    char characterArray[256];
    char * p;
    p = NULL;
    size_t len;
    FILE * filePointerCount;
    
    // convert the string to a 'character array'
    snprintf(characterArray, sizeof(characterArray), "%s", PATH_SIGNAL_COUNT);
    
    len = strlen(characterArray);
    
    // remove last \n char and null terminate
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
    
    // create/open existing file for append
    filePointerCount = fopen(PATH_SIGNAL_COUNT, "a");
    
    if(filePointerCount == NULL) {
        fprintf(stderr, "Failed to open count file: %s\n", strerror(errno));
        return -1;
    }
    
    // convert ms to s
    fprintf(filePointerCount, "%llu\n", (interruptTimeMs / 1000));
    
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
char * fileGetFileContents(char * filename)
{
    char * fileContents;
    unsigned long fileSize;
    unsigned int bytesRead;
    FILE * fileHandle;
    
    fileHandle = fopen(filename, "rb");

    // move file pointer to the end of the file
    fseek(fileHandle, 0, SEEK_END);

    // the position of the file pointer now
    fileSize = ftell(fileHandle);

    // move the pointer back to the start of the file
    rewind(fileHandle);

    // allocate memory, include an extra byte for null terminate char
    fileContents = malloc(fileSize + 1);

    // load the contents of the file into the fileContents var
    bytesRead = fread(fileContents, 1, fileSize, fileHandle);

    fclose(fileHandle);

    // null terminate
    // @gotcha - null terminate at bytes read, not fileSize bytes. For non-real (OS) files,
    // a block (4096bytes) is returned
    fileContents[bytesRead] = 0;

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
int requestPostCsv(char * macAddress, char * csv)
{    
    CURL * curl;
    char * postString;
    FILE * devNull;
    char * csvUrlEncoded;
    int returnValue = 0;

    // init
    curl_global_init(CURL_GLOBAL_ALL);

    //get a curl handle
    curl = curl_easy_init();

    if(curl) {
        // set the end point
        curl_easy_setopt(curl, CURLOPT_URL, END_POINT_URL);

        // url encode, to keep newline chars
        csvUrlEncoded = (char*) curl_easy_escape(curl, csv, 0);

        asprintf(&postString, "macAddress=%s&csv=%s", macAddress, csvUrlEncoded);

        printf("combined string is this: %s\n", postString);

        // specify post data
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postString);

        // send response body to /dev/null
        devNull = fopen("/dev/null", "w+");

        curl_easy_setopt(curl, CURLOPT_WRITEDATA, devNull);

        // make the request
        CURLcode res;

        res = curl_easy_perform(curl);

        fclose(devNull);

        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            returnValue = -1;
        }

        // clean up
        free(postString);

        curl_free(csvUrlEncoded);

        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();

    return returnValue;
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
 * blink LED for use with wiringPi threading interface
 */
PI_THREAD(ledSignalCounted)
{
    ledBlink(300);
}

void processCountFile(void)
{
    // are we already processing something?
    if(isProcessingCountFile) {
        printf("something is already being processed\n");
        return;
    }
    
    // prevent the thread starting multiple times
    isProcessingCountFile = true;
    
    // does a swap already exist?
    // this will normally be false, unless something crashed / lost power
    // before it was submitted in a previous run
    if(fileSwapFileExists() < 0) {
        // no swap file. Is there anything to process?
        if(fileCountFileExists() < 0) {
            printf("nothing to process\n");
            // nothing to process
            isProcessingCountFile = false;
            return;
        }
        else {
            printf("count file exists\n");
        }
        
        // move the count file to the swap file
        if(fileMoveCountToSwap() < 0) {
            // something went wrong
            printf("could not move count to swap\n");
            isProcessingCountFile = false;
            return;
        }
        else {
            printf("moved count file to swap\n");
        }
    }
    else {
        printf("swap file already exists\n");
    }
    
    char * macAddress = fileGetMacAddress();
    char * csv = fileGetSwapFileContents();
    
    int requestPostCsvSuccess = requestPostCsv(macAddress, csv);
    
    free(macAddress);
    free(csv);
    
    // submit the contents of the file
    if(requestPostCsvSuccess < 0) {
        //submit failed, @todo log
        isProcessingCountFile = false;
        return;
    }
    
    // successfully recorded, delete the swap file
    if(remove(PATH_SIGNAL_COUNT_SWAP) < 0) {
        printf("failed to delete swap\n");
        //@todo record this properly
    }
    
    isProcessingCountFile = false;
    printf("processCountFile ended\n");
    return;
}

/**
 * the interrupt to fire when the input pin is pulled up to 3v
 */
void signalIsr(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);

    unsigned long long interruptTimeMs =
        (unsigned long long)(tv.tv_sec) * 1000 +
        (unsigned long long)(tv.tv_usec) / 1000;
    
    // determine whether this is rising edge or falling edge
    if(digitalRead(PIN_INPUT) == 1) {
        // rising edge
        interruptTimeMsRising = interruptTimeMs;
        return;
    }
    
    // Was there a preceding rising edge detected?
    if(interruptTimeMsRising == 0) {
        // No rising value, ignore
        return;
    }
    
    // else, falling edge
    unsigned long long intervalTimeMs = interruptTimeMs - interruptTimeMsRising;
    
    // reset, ready for next event
    interruptTimeMsRising = 0;

    printf("\n\nnew signal - interval was %llu\n", intervalTimeMs);

    if( intervalTimeMs < TRIGGER_INTERVAL_MS) {
        printf("ignoring, signal time was not long enough\n");
        return;
    }
    
    // record the signal count to file
    fileRecordSignalCount(interruptTimeMs);
    
    // blink the LED to show we recorded the signal
    //piThreadCreate(ledSignalCounted);
    ledBlink(200);
    
    // attempt to submit the count file
    // disable - threads become unresponsive after a while. Rely on main loop call to processCountFile();
    //processCountFile();
}

/**
 * init and run the application
 */
int main(void)
{
    // init the wiringPi library
    if (wiringPiSetup () < 0)
    {
        fprintf(stderr, "Unable to setup wiringPi: %s\n", strerror (errno));
        return 1 ;
    }
    
    // set up an interrupt on our input pin
    if (wiringPiISR(PIN_INPUT, INT_EDGE_BOTH, &signalIsr) < 0)
    {
        fprintf(stderr, "Unable to setup ISR: %s\n", strerror (errno));
        return 1 ;
    }
    
    // configure the output pin for output. Output output output
    pinMode(PIN_OUTPUT, OUTPUT);
    
    pinMode(PIN_INPUT, INPUT);
    
    // pull the internal logic gate down to 0v - we don't want it floating around
    pullUpDnControl(PIN_INPUT, PUD_DOWN);
    
    // blink 3 times - we're ready to go
    ledBlink(300);
    delay(300);
    ledBlink(300);
    delay(300);
    ledBlink(300);
    
    printf("signalCount started\n");
    
    for(;;) {
        delay(1000);
        
        // this thread will submit any count files that have not been sent
        printf("about to run cleanup thread\n");
        processCountFile();
    }

    return 0;
}
