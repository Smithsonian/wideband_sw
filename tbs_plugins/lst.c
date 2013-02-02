#include "lst.h"

/*
  Normalize an angle between 0 and 2pi radians
*/
void doubleNormalize0to2pi(double *angle)
{
  if (fabs(*angle) < 2.0e9) {
    int quotient;

    quotient = (int)(*angle / M_2PI);
    *angle = *angle - ((double)quotient)*M_2PI;
    if (*angle < 0.0)
      *angle += M_2PI;
  } else {
    /* Let's hope this path is never taken - it will be expensive! */
    while (*angle > M_2PI)
      *angle -= M_2PI;
    while (*angle < 0.0)
      *angle += M_2PI;
  }
}

void doubleNormalize0to360(double *a)
{
  double temp;

  temp = (*a) * DEGREES_TO_RADIANS;
  doubleNormalize0to2pi(&temp);
  *a = temp / DEGREES_TO_RADIANS;
}

/*
  Return the Greenwich Mean Siderial Time
*/
double gMST(double tJD)
{
  int nDays;
  double tJD0h, s, t, t0;

  tJD0h = (double)((int)tJD) + 0.5;
  s = tJD0h - 2451545.0;
  t = s/36525.0;
  t0 = 6.697374558 +
    (2400.051336 * t) +
    (0.000025862*t*t);
  nDays = (int)(t0/24.0);
  t0 -= 24.0*(double)nDays;
  t0 += (tJD - tJD0h) * 24.0 * 1.002737909;
  while (t0 < 0)
    t0 += 24.0;
  while (t0 >= 24.0)
    t0 -= 24.0;
  return(t0);
}

/*
  Return the Local Siderial Time for the given Julian Date
*/
double lSTAtTJD(double tJD)
{
  double theLST;

  theLST = gMST(tJD);
  theLST *= HOURS_TO_DEGREES;
  theLST += LONGITUDE/DEGREES_TO_RADIANS;
  doubleNormalize0to360(&theLST);
  theLST *= DEGREES_TO_RADIANS;
  return(theLST);
}

double tJDNow(struct timespec at_time)
{
  long nsec;
  int year, month, day, hour, min, sec, a, b, c, dd;
  double y, m, d, tJD;
  struct tm *nowValues;

  nowValues = gmtime(&at_time.tv_sec);
  year = nowValues->tm_year + 1900;
  month = nowValues->tm_mon + 1;
  day  = nowValues->tm_mday;
  hour = nowValues->tm_hour;
  min  = nowValues->tm_min;
  sec  = nowValues->tm_sec;
  nsec = at_time.tv_nsec;
  y = (double)year;
  m = (double)month;
  d = (double)day + hour/24.0 + ((double)min)/1440.0 + ((double)sec)/86400.0 + ((double)nsec)/(86400.0*1e9);
  if (m < 3.0) {
    y = y-1.0;
    m = m+12.0;
  }
  a  = (((int)y)/100);
  b  = 2 - a + a/4;
  c  = (int)(365.25*y);
  dd = (int)(30.6001*(m+1.0));
  tJD = ((double)(b+c+dd)) + d + 1720994.5;
  return(tJD);
}

/* main() */
/* { */
/*   int hh, mm; */
/*   double lST, tJD, lSTHours, ss; */

/*   tJD = tJDNow(); */
/*   lST = lSTAtTJD(tJD); */
/*   lSTHours = lST/HOURS_TO_RADIANS; */
/*   hh = (int)lSTHours; */
/*   mm = (int)((lSTHours - (double)hh)*60); */
/*   ss = (lSTHours - (double)hh - ((double)mm)/60.0)*3600.0; */
/*   printf("LST %f (radians) = %02d:%02d:%05.2f\n", lST, */
/* 	 hh, mm, ss); */
/* } */
