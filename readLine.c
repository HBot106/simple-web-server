#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* readLine(FILE* fileIN)
{
    /* i and currentSize are used for dyanically extending the string when it becomes to long for the allocated space */
    int i, currentSize;
    /* c holds the current character being evaluated/copied */
    char c;
    /* lineOutPtr points to allocated array and is what is returned */
    char* lineOutPtr;

    /* allocate an array with space for 100 chars, print error if it fails */
    lineOutPtr = (char*)malloc(100*sizeof(char));
    if(!lineOutPtr)
    {
        fprintf(stderr, "%s\n", "malloc error");
        exit(1);
    }

    /* initialize the char count */
    i = 0;
    /* keep track of current size of the allocated space (in char sized units) */
    currentSize = 100;
    /* sets the first char to \n for the case where a line only contains \n */
    lineOutPtr[0] = '\0';

    /* while the number of stored chars is less than the allocated size */
    while(i < currentSize)
    {
        c = fgetc(fileIN);

        /* if c is a '\n' or the end of the file is reached break the loop */
        if ((c == '\n') || (c == EOF))
        {
            break;
        }

        /* otherwise set the next char in the allocated array to c */
        lineOutPtr[i] = c;

        /* increments i then writes a null to that spot that will get over written if there is another chararter */
        i++;
        lineOutPtr[i] = '\0';

        /* if we fill up the allocated space, allocate more */
        if (i == currentSize-1)
        {
            currentSize = currentSize + 100;

            /* allocate more space, give error if needed */
            lineOutPtr = (char*)realloc(lineOutPtr, (currentSize*(sizeof(char))));
            if(!lineOutPtr)
            {
                fprintf(stderr, "%s\n", "malloc error");
                exit(1);
            }
        }
    }
    if (c == EOF)
    {
        return lineOutPtr;
    }
    else if (c == '\n')
    {
        lineOutPtr[i] = c;
        return lineOutPtr;
    }
    return lineOutPtr;
    
}
