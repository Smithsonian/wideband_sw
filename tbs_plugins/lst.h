#include <stdio.h>
#include <time.h>
#include <math.h>

/* Some useful constants */
#define DEGREES_TO_RADIANS (M_PI/180.0)
#define HOURS_TO_RADIANS   (M_PI/12.0)
#define HOURS_TO_DEGREES   (15.0)
#define M_2PI              (2.0*M_PI)
#define LONGITUDE (-2.713594675620429)

/* Prototypes for lst.c */
double tJDNow(struct timespec at_time);
double lSTAtTJD(double tJD);

