#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define MAX_FILES 5 // Maximum number of files provided through command line arguments
#define MAX_LINE_LENGTH 2048 // Maximum number of characters per line when reading from a file
#define MAX_BUFFER_SIZE 2  // Size of the shared circular buffer 

int printUsage();
void runSearch(int argc, char *argv[]);
void *scanFile(void *arg);
void *report();

#endif