#include <time.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include <katcp.h>
#include <plugin.h>

/* Global atomic values */
volatile sig_atomic_t keep_going = 0;
volatile sig_atomic_t iteration = 0;

/* Global non-atomic values (i.e. not used by interrupt handler) */
long int tv_usec, tv_sec, repeat = 0;
unsigned long period = 0;

/* Function prototypes */
void catch_alarm(int signal);
unsigned int set_alarm(long int sec, long int usec, long int repeat);

/* KATCP command to start a repeating alarm */
int start_alarm_cmd(struct katcp_dispatch *d, int argc) {
  int wait; 

  /* Check if alarm has been set */
  if (period == 0) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "alarm has not been set");
    return KATCP_RESULT_FAIL;
  }    

  /* Set the timer */
  signal(SIGALRM, catch_alarm);
  if (set_alarm(tv_sec, tv_usec, repeat) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "problem setting itimer alarm");
    return KATCP_RESULT_FAIL;
  }

  /* Grab the first argument, the optional wait for alarm to expire */
  wait = arg_unsigned_long_katcp(d, 1);
  if (wait == 1){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "waiting for alarm to finish");
    
    /* Wait for timer to stop */
    while (keep_going == 1) {
      usleep(100);
    }
  }

  return KATCP_RESULT_OK;
}

/* KATCP command to stop a repeating alarm */
int stop_alarm_cmd(struct katcp_dispatch *d, int argc) {
  keep_going = 0;
  return KATCP_RESULT_OK;
}

/* KATCP command to set alarm */
int set_alarm_cmd(struct katcp_dispatch *d, int argc) {
  /* Grab the first argument, period N */
  period = arg_unsigned_long_katcp(d, 1);
  if (period == 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Grab the second argument M, default is 1 */
  repeat = arg_signed_long_katcp(d, 2);
  if (repeat == 0){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "no repeat given, defaulting to once");
    repeat = 1;
  }

  /* Convert period to sec and usecs */
  tv_usec = (long int) (period % 1000000);
  tv_sec = (long int) (period / 1000000);
  if (repeat > 0) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "setting alarm period to %d.%06d seconds, repeating %d time(s)", tv_sec, tv_usec, repeat);
  } else {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "setting alarm period to %d.%06d seconds, repeating forever", tv_sec, tv_usec);
  }

  return KATCP_RESULT_OK;
}

/* Use setitimer to cause an alarm */
unsigned int set_alarm(long int sec, long int usec, long int repeat) {
  struct itimerval new;

  new.it_interval.tv_usec = usec;
  new.it_interval.tv_sec = sec;
  new.it_value.tv_usec = usec;
  new.it_value.tv_sec = sec;

  iteration = repeat;
  keep_going = 1;

  if (setitimer(ITIMER_REAL, &new, NULL) < 0) {
    return -1;
  }
  return 0;
}

/* Here is the exception handler */
void catch_alarm(int signal) {
  struct timespec now;

  if (!keep_going) {
    /* Clear the itimer when done */
    setitimer(ITIMER_REAL, NULL, NULL);
    return;
  }
  iteration--;
  if (iteration == 0) {
    keep_going = 0;
  }

  clock_gettime(CLOCK_REALTIME, &now);
  printf("(%ld.%09ld) ", (long int)now.tv_sec, now.tv_nsec);
  if (iteration == 0) {
    printf("Done repeating\n");
  } else if (iteration > 0) {
    printf("Iteration: %d\n", iteration);
  } else {
    printf("Repeating forever...\n");
  }

}


/*
 * KATCP_PLUGIN definition
 */
struct PLUGIN KATCP_PLUGIN = {
  .n_cmds = 3,
  .name = "alarm",
  .version = "0.0.1",
  .cmd_array = {
    {
      .name = "?alarm-set", 
      .desc = "set the alarm period to N, and repeat M times or set M=-1 to repeat forever (default once) (?alarm-set N [M=1])",
      .cmd = set_alarm_cmd
    },
    {
      .name = "?alarm-start", 
      .desc = "if alarm-set was called with M>0, start the repeat alarm, optionally wait for it to finish (?alarm-start [wait=0])",
      .cmd = start_alarm_cmd
    },
    {
      .name = "?alarm-stop", 
      .desc = "if repeating alarm was started, stop it (?alarm-stop)",
      .cmd = stop_alarm_cmd
    },
  }
};
