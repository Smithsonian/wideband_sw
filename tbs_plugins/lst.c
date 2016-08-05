#include "lst.h"

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

double localApparentSiderealTime(double mJD)
/* This routine returns the local apparent siderial time for the array ref-  */
/* erence position, in radians.                                              */
{
  double gMST, lMST, lAST;
  double mJDDay, mJDDayFraction;

  /* Split up JD into integer and fractional bits */
  mJDDayFraction = modf(mJD, &mJDDay);

  gMST = slaGmsta(mJDDay, mJDDayFraction);
  /* Now add in longitude of arrary center                                   */
  lMST = gMST + LONGITUDE;
  /* Now add equation of the equinoxes                                       */
  lAST = lMST + slaEqeqx(mJDDay+mJDDayFraction);
  /* Now put within normal angular range of 0 to 2*pi                        */
  lAST = slaDranrm(lAST);
  return(lAST);
}
