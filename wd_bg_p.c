/******************************************************************************
 * Author: Ofek Tal                                                           *
 * Reviewer: Atai                                                             *
 * Date: 30.01.21                                                             *
 * Description: implementation of Watch Dog                                   *
 *                                                                            * 
 * Infinity Labs OL113                                                        *
 ******************************************************************************/
#include <stdio.h> /*printf()*/

#include "wd.h"

extern int g_i_am_app;

int main(int argc, char *argv[])
{
    g_i_am_app = 0;

    WDStart(argv);

    (void)argc;

    return 0;
}
