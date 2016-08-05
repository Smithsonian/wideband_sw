#include <stdio.h>
#include <time.h>
#include <math.h>
#include "slalib.h"

/* Some useful constants */
#define LONGITUDE (-2.713594675620429)

/* Prototypes for lst.c */
double tJDNow(struct timespec at_time);
double localApparentSiderealTime(double mJD);
