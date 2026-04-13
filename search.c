/**
 * File: search.c
 * Description: 
 *      Implements a multi-threaded file search program using the
 * producer/consumer pattern. Scanners search assigned files for a pattern
 * and write results to a shared circular buffer. A single Reporter thread 
 * reads from the buffer and prints results.
 *
 *       Synchronization is achieved using one mutex and two condition
 * variables: one for signaling available space (notFull) and one
 * for signaling available data (notEmpty).
 */

#include "utils.h"

/**
 * BufferEntry: one slot in the circular buffer.
 * Stores the filename and the "line match" count produced by a Scanner.
 */
typedef struct {
    char filename[256]; // name of the file that was scanned
    int  count;         // number of lines containing the pattern
} BufferEntry;

/**
 * ScannerArg: argument bundle sent to each Scanner.
 * Carries everything the thread needs without relying on globals for input.
 */
typedef struct {
    const char *filename; //path of the file this thread must scan    
    const char *pattern;  //search pattern (case-sensitive substring) 
} ScannerArg;

/* Circular buffer and its variables */
static BufferEntry buffer[MAX_BUFFER_SIZE]; // the shared circular buffer  
static int bufHead    = 0;  // index of the next slot to write into        
static int bufTail    = 0;  // index of the next slot to read from         
static int bufCount   = 0;  // number of items currently in the buffer     

/* Total number of Scanner threads;
 Reporter uses this to know when to stop */
static int totalScanners  = 0;
/* Number of Scanner threads that have finished and written their result    */
static int doneCount      = 0;

/* Synchronization primitives */
static pthread_mutex_t bufMutex   = PTHREAD_MUTEX_INITIALIZER; // Buffer mutex init
static pthread_cond_t  notFull    = PTHREAD_COND_INITIALIZER; // Scanners wait here when buffer is full  
static pthread_cond_t  notEmpty   = PTHREAD_COND_INITIALIZER; // Reporter waits here when buffer is empty 

/* The pattern string,
 set once in runSearch and read by Scanner threads */
static const char *globalPattern = NULL;

/* -------------------------------------------------------------------------
 * runSearch
 * ------------------------------------------------------------------------- */

/**
 * runSearch: validates arguments, spawns threads, and waits for completion.
 * 
 * min 1 file
 * max 5 files
 *
 * Expected argv layout:
 *   argv[0]  program name
 *   argv[1]  pattern  (3-10 characters, validated in tester.c / main)
 *   argv[2]  "-f"     (validated in tester.c / main)
 *   argv[3+] filenames
 *
 * This function:
 *   1. Counts the number of files and validates the range 2-MAX_FILES
 *   2. Stores the pattern in globalPattern
 *   3. Creates one Scanner thread per file and one Reporter thread
 *   4. Joins all threads before returning
 *
 * @param argc  argument count from main
 * @param argv  argument vector from main
 */
void runSearch(int argc, char *argv[]) {
    // argv[3] is the first filename
    int numFiles = argc - 3;

    // Validate file count
    if (numFiles < 2 || numFiles > MAX_FILES) {
        printUsage();
    }

    /* Store the pattern so scanner threads can access it */
    globalPattern  = argv[1];
    totalScanners  = numFiles;

    /* Thread handles */
    pthread_t scannerThreads[MAX_FILES];
    pthread_t reporterThread;

    /* Argument bundles for each Scanner thread (one per file) */
    ScannerArg scanArgs[MAX_FILES];

    /* Create the Reporter thread first so it is ready to consume results */
    if (pthread_create(&reporterThread, NULL, report, NULL) != 0) {
        perror("pthread_create reporter");
        exit(EXIT_FAILURE);
    }

    /* Create one Scanner thread for each file */
    for (int i = 0; i < numFiles; i++) {
        scanArgs[i].filename = argv[3 + i]; // assign the i file
        scanArgs[i].pattern  = globalPattern;

        if (pthread_create(&scannerThreads[i], NULL, scanFile, &scanArgs[i]) != 0) {
            perror("pthread_create scanner");
            exit(EXIT_FAILURE);
        }
    }

    /* Wait for all Scanner threads to finish */
    for (int i = 0; i < numFiles; i++) {
        pthread_join(scannerThreads[i], NULL);
    }

    /* Wait for the Reporter thread to finish printing all results */
    pthread_join(reporterThread, NULL);
}

/* -------------------------------------------------------------------------
 * scanFile  
 * ------------------------------------------------------------------------- */

/**
 * scanFile: counts lines containing the pattern and pushes the result to
 *            circular buffer.
 *
 * Algorithm (grep pattern filename | wc -l):
 *   - Read the file line by line.
 *   - For each line, use strstr() to check whether the pattern appears at
 *     least once. if so, increment the counter (multiple occurrences on the
 *     same line count as one occurance).
 *   - After scanning the whole file, acquire the mutex, wait while the buffer
 *     is full, write the result, signal the Reporter, and release the mutex.
 *
 * @param arg  pointer to a ScannerArg struct (filename + pattern)
 * @return     NULL
 */
void *scanFile(void *arg) {
    ScannerArg *scanArg  = (ScannerArg *)arg;
    const char *filename = scanArg->filename;
    const char *pattern  = scanArg->pattern;

    int lineCount = 0; /* number of lines that contain the pattern at least once */

    /* Line buffer: at least 5 × MAX_LINE_LENGTH as required by the spec */
    char lineBuffer[MAX_LINE_LENGTH * 5];

    /* Open the file for reading */
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        /* If the file cannot be opened, record 0 matches and continue */
        perror(filename);
    } else {
        /* Read one line at a time */
        while (fgets(lineBuffer, sizeof(lineBuffer), fp) != NULL) {
            /* strstr returns non-NULL if the pattern is found anywhere in the line */
            if (strstr(lineBuffer, pattern) != NULL) {
                lineCount++;
            }
        }
        fclose(fp);
    }

    /* write result to the circular buffer */
    pthread_mutex_lock(&bufMutex);

    /* Wait while the buffer is full (no space to write) */
    while (bufCount == MAX_BUFFER_SIZE) {
        pthread_cond_wait(&notFull, &bufMutex);
    }

    /* Write the result into the next available slot */
    strncpy(buffer[bufHead].filename, filename, sizeof(buffer[bufHead].filename) - 1);
    buffer[bufHead].filename[sizeof(buffer[bufHead].filename) - 1] = '\0';
    buffer[bufHead].count = lineCount;

    /* Advance the write index, wrapping around when the end is reached */
    bufHead = (bufHead + 1) % MAX_BUFFER_SIZE;
    bufCount++;

    /* Track how many Scanners have completed their work */
    doneCount++;

    /* Wake up the Reporter: there is new data in the buffer */
    pthread_cond_signal(&notEmpty);

    pthread_mutex_unlock(&bufMutex);

    return NULL;
}

/* -------------------------------------------------------------------------
 * report  (Reporter thread entry point)
 * ------------------------------------------------------------------------- */

/**
 * report - continuously retrieves results from the shared buffer and prints
 *          them until all Scanner threads have finished.
 *
 * The Reporter loops until it has printed one result for every Scanner thread.
 * Inside each loop:
 *   1. Acquires the mutex.
 *   2. Waits while the buffer is empty & not all Scanners are done.
 *   3. Reads and removes the oldest entry from the buffer.
 *   4. Signals waiting Scanners that a slot has been freed.
 *   5. Releases the mutex and prints the result.
 *
 * @param arg  unused (required by pthread signature)
 * @return     NULL
 */
void *report() {
    int processed = 0; /* number of results printed so far */

    /* Keep running until we have consumed one result per Scanner thread */
    while (processed < totalScanners) {

        pthread_mutex_lock(&bufMutex);

        /*
          Wait while the buffer is empty.
         */
        while (bufCount == 0) {
            pthread_cond_wait(&notEmpty, &bufMutex);
        }

        /* Read the oldest entry from the buffer */
        BufferEntry entry = buffer[bufTail];

        /* Advance the read index, wrapping around at the end */
        bufTail = (bufTail + 1) % MAX_BUFFER_SIZE;
        bufCount--;

        /* Signal waiting Scanner threads that there is now space in the buffer */
        pthread_cond_signal(&notFull);

        pthread_mutex_unlock(&bufMutex);

        /* Print the result outside the critical section */
        printf("Pattern %s found %d times in file %s\n",
               globalPattern, entry.count, entry.filename);

        processed++;
    }

    return NULL;
}