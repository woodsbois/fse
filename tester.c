/**
 * Program: Multi-Threaded File Search
 * Description: This program searches for a specified pattern in multiple text files concurrently using multithreading.
 *              Each thread is assigned to one file, searches for the given pattern, and updates a shared total count
 *              of occurrences. The program outputs the total occurrences of the pattern across all files.
 * 
 * Usage:
 *   ./fse pattern -f file1 file2 file3 ...
 * 
 * Options:
 *   pattern        Specify the pattern (3 to 10 characters inclusive) to search for in the files.
 *   -f file1 file2... List of files to search through (2 to 5 files).
 * 
 */


#include "utils.h"

int printUsage() {
    printf("Usage:\n");
    printf("  ./fse pattern -f file1 file2 file3 ...\n\n");
    printf("Options:\n");
    printf("  pattern        Specify the pattern (3 to 10 characters) to search for in the files.\n");
    printf("  -f file1 file2... List of files to search through (2 to 5 files).\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc < 5 || strlen(argv[1]) < 3 || strlen(argv[1]) > 10 || strcmp(argv[2], "-f") != 0) {
        printUsage();
    }
    
    runSearch(argc, argv);

    return 0;
}

