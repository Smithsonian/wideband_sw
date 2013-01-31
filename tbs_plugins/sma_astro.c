#include <math.h>
#include <time.h>
#include <unistd.h> // only for usleep
#include <stdio.h>
#include <stdlib.h>
#include <katcp.h>
#include <pthread.h>
#include <sys/mman.h>
#include <tcpborphserver3.h>
#include <plugin.h>

#define TRUE 1
#define FALSE 0
#define N_INPUTS 2
#define CDELAY_REG "cdelay_ctrl"
#define PHASE_REG "cgain_phase_%d"
#define PHASE_MAX 179.9945068359375
#define PHASE_MIN -180.0000000000000
#define FDELAY_MAX 0.999969482421875
#define FDELAY_MIN -1.000000000000000
#define CDELAY_MID 16384 // middle of coarse delay FIFO
#define SAMPLE_FREQ 2.288 // sample rate in GHz

/* These are the constant, user-programmable delays and phases */
volatile double delays[N_INPUTS];
volatile double phases[N_INPUTS];

/* Some global variables for fringe-stopping */
volatile int fstop_go = FALSE;
pthread_mutex_t fstop_mutex;
pthread_t fstop_thread;

/* Sets the constant part of the phase register */
int set_phase(int input, double phase, struct tbs_raw *tr){
  char regname[50];
  struct tbs_entry *te;
  int16_t casted_phase;
  uint32_t old_value, new_value;

  /* Check that the phase is within range */
  if ((phase>PHASE_MAX) || (phase<PHASE_MIN)){
    return -1;
  }

  /* Scale and cast phase (which should be -180 <= p < 180) */
  casted_phase = (int16_t)(phase * (pow(2, 16)/360.0));

  /* Get the register name, given input number */
  if(sprintf(regname, PHASE_REG, input) < 0){
    return -2;
  }

  /* Get the phase register pointer */
  te = find_data_avltree(tr->r_registers, regname);
  if(te == NULL){
    return -3;
  }

  /* Get current value of the register */
  old_value = *((uint32_t *)(tr->r_map + te->e_pos_base));

  /* Set the proper phase bits */
  new_value = (old_value & 0x0000ffff) + (((uint32_t)casted_phase)<<16);

  /* Finally, write it to the mmap region */
  *((uint32_t *)(tr->r_map + te->e_pos_base)) = new_value;
  msync(tr->r_map, tr->r_map_size, MS_SYNC);

  return 0;
}  

/* Sets the constant part of the coarse delay register */
int set_cdelay(int input, int delay_samp, struct tbs_raw *tr){
  struct tbs_entry *te;
  uint16_t casted_cdelay;
  uint32_t old_value, new_value;
  int shift_by = input*15;

  /* Check that the delay is within range */
  if ((delay_samp>CDELAY_MID-1) || (delay_samp<-CDELAY_MID)){
    return -1;
  }

  /* Scale and cast coarse delay (which should be in ns) */
  casted_cdelay = (uint16_t)(CDELAY_MID + delay_samp);

  /* Get the delay register pointer */
  te = find_data_avltree(tr->r_registers, CDELAY_REG);
  if(te == NULL){
    return -2;
  }

  /* Get current value of the register */
  old_value = *((uint32_t *)(tr->r_map + te->e_pos_base));

  /* Set the proper delay bits */
  new_value = (old_value & 0xc0000000) + \
    (old_value & (0x3fff8000>>shift_by)) + \
    (((uint32_t)casted_cdelay)<<shift_by);

  /* Finally, write it to the mmap region */
  *((uint32_t *)(tr->r_map + te->e_pos_base)) = new_value;
  msync(tr->r_map, tr->r_map_size, MS_SYNC);

  return 0;
}  

/* Sets the fine-delay part of the phase register */
int set_fdelay(int input, double fdelay_samp, struct tbs_raw *tr){
  char regname[50];
  struct tbs_entry *te;
  int16_t casted_fdelay;
  uint32_t old_value, new_value;

  /* Check that the delay is within range */
  if ((fdelay_samp>FDELAY_MAX) || (fdelay_samp<FDELAY_MIN)){
    return -1;
  }

  /* Scale and cast fine-delay (which should be -1 <= p < 1) */
  casted_fdelay = (int16_t)(fdelay_samp * pow(2, 15));

  /* Get the register name, given input number */
  if(sprintf(regname, PHASE_REG, input) < 0){
    return -2;
  }

  /* Get the phase register pointer */
  te = find_data_avltree(tr->r_registers, regname);
  if(te == NULL){
    return -3;
  }

  /* Get current value of the register */
  old_value = *((uint32_t *)(tr->r_map + te->e_pos_base));

  /* Set the proper phase bits */
  new_value = (old_value & 0xffff0000) + (uint32_t)casted_fdelay;

  /* Finally, write it to the mmap region */
  *((uint32_t *)(tr->r_map + te->e_pos_base)) = new_value;
  msync(tr->r_map, tr->r_map_size, MS_SYNC);

  return 0;
}

int set_delay_cmd(struct katcp_dispatch *d, int argc){ 
  int input;
  double delay;
  char * delaystr;
  struct tbs_raw *tr;

  /* Grab the mode pointer */
  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire raw mode state");
    return KATCP_RESULT_FAIL;
  }

  /* Make sure we're programmed */
  if(tr->r_fpga != TBS_FPGA_MAPPED){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "fpga not programmed");
    return KATCP_RESULT_FAIL;
  }

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
  pthread_mutex_lock(&fstop_mutex);
  delays[input] = delay;
  pthread_mutex_unlock(&fstop_mutex);

  return KATCP_RESULT_OK;
}

int set_phase_cmd(struct katcp_dispatch *d, int argc){
  int input;
  double phase;
  char * phasestr;
  struct tbs_raw *tr;

  /* Grab the mode pointer */
  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire raw mode state");
    return KATCP_RESULT_FAIL;
  }

  /* Make sure we're programmed */
  if(tr->r_fpga != TBS_FPGA_MAPPED){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "fpga not programmed");
    return KATCP_RESULT_FAIL;
  }

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
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "setting input %d to phase of %.6f deg", input, phase);
  pthread_mutex_lock(&fstop_mutex);
  phases[input] = phase;
  pthread_mutex_unlock(&fstop_mutex);

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

/* Only this loop should access registers */
void * fringe_stop(void * tr){
  int j;
  int result;
  double delay_samp;
  long i = 0;

  while (fstop_go) {
    if ((i%100) == 0){
      pthread_mutex_lock(&fstop_mutex);

      for (j=0; j<N_INPUTS; j++) {

	/* Set the phase values */
	set_phase(j, phases[j], tr);

	delay_samp = delays[j] * SAMPLE_FREQ;
	/* Set the coarse delay values */
	result = set_cdelay(j, delay_samp, tr);
	if (result < 0) {
	  printf("set_cdelay returned error code %d\r\n", result);
	}

	/* Set the fine delay values */
	result = set_fdelay(j, delay_samp - (int)delay_samp, tr);
	if (result < 0) {
	  printf("set_fdelay returned error code %d\r\n", result);
	}

      }

      printf("(%8ld) delays= 0:%12.6f 1:%12.6f, phases= 0:%12.6f 1:%12.6f\r\n", i, delays[0], delays[1], phases[0], phases[1]);
      pthread_mutex_unlock(&fstop_mutex);
    }

    usleep(10000); // 10 ms
    i++;
  }

  return tr;
}

int set_fstop_cmd(struct katcp_dispatch *d, int argc){
  return KATCP_RESULT_OK;
}

int start_fstop_cmd(struct katcp_dispatch *d, int argc){
  int status;
  struct tbs_raw *tr;

  /* Grab the mode pointer */
  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire raw mode state");
    return KATCP_RESULT_FAIL;
  }

  /* Make sure we're programmed */
  if(tr->r_fpga != TBS_FPGA_MAPPED){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "fpga not programmed");
    return KATCP_RESULT_FAIL;
  }

  /* Set flag to start fringe-stopping */
  fstop_go = TRUE;

  /* Start the thread */
  status = pthread_create(&fstop_thread, NULL, fringe_stop, (void *)tr);
  if (status < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not create fringe-stopping thread");
    return KATCP_RESULT_FAIL;
  }
  
  return KATCP_RESULT_OK;
}

int stop_fstop_cmd(struct katcp_dispatch *d, int argc){

  /* Set flag to stop fringe-stopping */
  fstop_go = FALSE;

  /* Wait until the thread stops */
  pthread_join(fstop_thread, NULL);

  return KATCP_RESULT_OK;
}

struct PLUGIN KATCP_PLUGIN = {
  .n_cmds = 7,
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
    {
      .name = "?sma-astro-fstop-set", 
      .desc = "set various fringe-stopping parameters for an input (?sma-astro-fstop-set input A B C fstop)",
      .cmd = set_fstop_cmd
    },
    {
      .name = "?sma-astro-fstop-start", 
      .desc = "start fringe-stopping loop (?sma-astro-fstop-start)",
      .cmd = start_fstop_cmd
    },
    {
      .name = "?sma-astro-fstop-stop", 
      .desc = "stop fringe-stopping loop (?sma-astro-fstop-stop)",
      .cmd = stop_fstop_cmd
    },
  }
};
