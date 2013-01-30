#include <time.h>
#include <stdlib.h>
#include <katcp.h>
#include <plugin.h>

#define N_INPUTS 2

/* These are the constant, user-programmable delays and phases */
double delays[N_INPUTS];
double phases[N_INPUTS];

int set_delay_cmd(struct katcp_dispatch *d, int argc){ 
  int input;
  double delay;
  char * delaystr;

  /* Grab the first argument, input number = (0, N_INPUTS) */
  input = arg_unsigned_long_katcp(d, 1);
  if (input < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse first command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Make sure it's a valid input */
  if (input >= N_INPUTS){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "given input %d is NOT within range (0,%d)", input, N_INPUTS);
    return KATCP_RESULT_FAIL;
  }

  /* Grab the second argument the desired constant, offset delay */
  delaystr = arg_string_katcp(d, 2);
  if (delaystr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse second command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Convert the given string to a double */
  delay = atof(delaystr);
  if (delay == 0.0){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "atof returned 0.0, is that what you wanted?");
  }

  /* Finally, set the global variable */
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "setting input %d to delay of %.6f ns", input, delay);
  delays[input] = delay;

  return KATCP_RESULT_OK;
}

int set_phase_cmd(struct katcp_dispatch *d, int argc){
  int input;
  double phase;
  char * phasestr;

  /* Grab the first argument, input number = (0, N_INPUTS) */
  input = arg_unsigned_long_katcp(d, 1);
  if (input < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse first command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Make sure it's a valid input */
  if (input >= N_INPUTS){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "given input %d is NOT within range (0,%d)", input, N_INPUTS);
    return KATCP_RESULT_FAIL;
  }

  /* Grab the second argument the desired constant, offset phase */
  phasestr = arg_string_katcp(d, 2);
  if (phasestr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse second command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Convert the given string to a double */
  phase = atof(phasestr);
  if (phase == 0.0){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "atof returned 0.0, is that what you wanted?");
  }

  /* Finally, set the global variable */
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "setting input %d to phase of %.6f ns", input, phase);
  phases[input] = phase;

  return KATCP_RESULT_OK;
}

int get_delay_cmd(struct katcp_dispatch *d, int argc){ 
  int input;
  double delay;

  /* Grab the first argument, input number = (0, N_INPUTS) */
  input = arg_unsigned_long_katcp(d, 1);
  if (input < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse first command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Make sure it's a valid input */
  if (input >= N_INPUTS){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "given input %d is NOT within range (0,%d)", input, N_INPUTS);
    return KATCP_RESULT_FAIL;
  }

  /* Finally, grab and requested value */
  delay = delays[input];
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "input %d is set to %.6f ns", input, delay);

  /* And relay it back to the client */
  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
  append_double_katcp(d, KATCP_FLAG_DOUBLE | KATCP_FLAG_LAST, delay);
  return KATCP_RESULT_OWN;
}

int get_phase_cmd(struct katcp_dispatch *d, int argc){
  int input;
  double phase;

  /* Grab the first argument, input number = (0, N_INPUTS) */
  input = arg_unsigned_long_katcp(d, 1);
  if (input < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse first command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Make sure it's a valid input */
  if (input >= N_INPUTS){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "given input %d is NOT within range (0,%d)", input, N_INPUTS);
    return KATCP_RESULT_FAIL;
  }

  /* Finally, grab and requested value */
  phase = phases[input];
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "input %d is set to %.6f ns", input, phase);

  /* And relay it back to the client */
  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
  append_double_katcp(d, KATCP_FLAG_DOUBLE | KATCP_FLAG_LAST, phase);
  return KATCP_RESULT_OWN;
}

struct PLUGIN KATCP_PLUGIN = {
  .n_cmds = 4,
  .name = "sma-astro",
  .version = KATCP_PLUGIN_VERSION,
  .cmd_array = {
    {
      .name = "?sma-astro-delay-set", 
      .desc = "set constant delay offset per input (?sma-astro-delay-set input delay)",
      .cmd = set_delay_cmd
    },
    {
      .name = "?sma-astro-phase-set", 
      .desc = "set constant phase offset per input (?sma-astro-phase-set input phase)",
      .cmd = set_phase_cmd
    },
    {
      .name = "?sma-astro-delay-get", 
      .desc = "get constant delay offset for input (?sma-astro-delay-set input)",
      .cmd = get_delay_cmd
    },
    {
      .name = "?sma-astro-phase-get", 
      .desc = "get constant phase offset per input (?sma-astro-phase-set input)",
      .cmd = get_phase_cmd
    },
  }
};
