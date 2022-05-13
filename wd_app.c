/******************************************************************************
 * Author: Ofek Tal                                                           *
 * Reviewer: Atai                                                             *
 * Date: 30.01.21                                                             *
 * Description: implementation of Watch Dog                                   *
 *                                                                            * 
 * Infinity Labs OL113                                                        *
 ******************************************************************************/
#include <stdio.h>  /*printf*/
#include <unistd.h> /*sleep*/
#include <limits.h> /*INT_MAX*/

#include "wd.h"

#define RUNINING_APP_TIME (3)

int main(int argc, char *argv[])
{
    size_t left_to_sleep = RUNINING_APP_TIME;

    printf("STATUS IS %d\n", WDStart(argv));

    while (left_to_sleep)
    {
        left_to_sleep = sleep(left_to_sleep);
    }

    WDStop();

    (void)argc;

    return 0;
}
