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
#include <dsm.h>
#include "lst.h"

#define TRUE 1
#define FALSE 0
#define N_INPUTS 2
#define CDELAY_REG "cdelay_ctrl"
#define PHASE_REG "cgain_phase_%d"
//#define PHASE_MAX 179.9945068359375
#define PHASE_MAX 180.0000000000000
#define PHASE_MIN -180.0000000000000
//#define FDELAY_MAX 0.999969482421875
#define FDELAY_MAX 1.000000000000000
#define FDELAY_MIN -1.000000000000000
#define CDELAY_MID 16384 // middle of coarse delay FIFO
#define SAMPLE_FREQ 2.288 // sample rate in GHz
#define FSTOP_UPDATE 100
#define PI 3.14159265
#define DDS_HOST "newdds"
#define DSM_GEOM_VAR "SWARM_SOURCE_GEOM_X"
#define DSM_GEOM_RA "SOURCE_RA_D"
#define DSM_GEOM_A "GEOM_DELAY_A_V2_D"
#define DSM_GEOM_B "GEOM_DELAY_B_V2_D"
#define DSM_GEOM_C "GEOM_DELAY_C_V2_D"

/* These are the constant, user-programmable delays and phases */
volatile double delays[N_INPUTS];
volatile double phases[N_INPUTS];

/* Some global variables for fringe-stopping */
volatile int fstop_go = FALSE;
volatile int fstop_del_en = TRUE;
volatile int fstop_pha_en = TRUE;
volatile double longitude = 0.0;
volatile double source_rA = 0.0;
volatile double fstop_freq = 0.0;
volatile double delay_trip[N_INPUTS][3];
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
  new_value = 0xc0000000 + (old_value & (0x3fff8000>>shift_by)) + \
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

  /* Finally, grab the requested value */
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

/* Update DSM-derived variables */
int update_vars() {
  int s;
  time_t timeStamp;
  dsm_structure structure;
  double rA, a[2], b[2], c[2];

  /* Initialize the DSM geometry structure */
  s = dsm_structure_init(&structure, DSM_GEOM_VAR);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_init()");
    return -1;
  }

  /* Read the structure over DSM */
  s = dsm_read(DDS_HOST, DSM_GEOM_VAR, &structure, &timeStamp);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_read()");
    return -2;
  }

  /* Get the source rA element */
  s = dsm_structure_get_element(&structure, DSM_GEOM_RA, &rA);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_get_element(rA)");
    return -3;
  }

  /* Get part A of delay triplet */
  s = dsm_structure_get_element(&structure, DSM_GEOM_A, &a[0]);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_get_element(A)");
    return -4;
  }

  /* Get part B of delay triplet */
  s = dsm_structure_get_element(&structure, DSM_GEOM_B, &b[0]);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_get_element(B)");
    return -5;
  }

  /* Get part C of delay triplet */
  s = dsm_structure_get_element(&structure, DSM_GEOM_C, &c[0]);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_get_element(C)");
    return -6;
  }

  /* Finally, set the global variables */
  pthread_mutex_lock(&fstop_mutex);
  source_rA = rA;
  delay_trip[0][0] = a[0] * 1e9;
  delay_trip[0][1] = b[0] * 1e9;
  delay_trip[0][2] = c[0] * 1e9;
  delay_trip[1][0] = a[1] * 1e9;
  delay_trip[1][1] = b[1] * 1e9;
  delay_trip[1][2] = c[1] * 1e9;
  pthread_mutex_unlock(&fstop_mutex);

  return 0;
}

/* Return -1 if negative, 1 if positive or zero */
double sign(double n) {
  return (n<0) ? -1 : 1;
}

/* Modulo returning a float */
double mod(double a, int n) {
  return  a - (double)(n*((int)a / n));
}

/* Convert a timespec struct to a double */
double timespec_to_double(struct timespec in) {
  return (double)in.tv_sec + (1e-9) * ((double)in.tv_nsec);
}

/* Wait for a specific time, then return quickly */
struct timespec wait_for(struct timespec time_in) {
  struct timespec now;
  double now_db, time_db;

  time_db = timespec_to_double(time_in);
  clock_gettime(CLOCK_REALTIME, &now);
  now_db = timespec_to_double(now);
  while (now_db < time_db) {
    clock_gettime(CLOCK_REALTIME, &now);
    now_db = timespec_to_double(now);
  }

  return now;
}

/* Add a certain amount of time to a timespec */
void add_nsec(struct timespec *in, struct timespec *out, long long nsec) {
  long delsec, delnsec;

  delsec = (long)(((double)nsec) * 1e-9);
  delnsec = (long)(nsec - delsec * 1e9);

  out->tv_sec = in->tv_sec + delsec;
  out->tv_nsec = in->tv_nsec + delnsec;
}

/* Only this loop should access registers */
void * fringe_stop(void * tr){
  int j;
  long i = 0;
  int result;

  int cdelay;
  double delay_samp, fdelay;
  double total_delay[N_INPUTS];
  double total_phase[N_INPUTS];
  double final_delay, final_phase;

  int hh, mm;
  double ha, lst, lst_24h, tjd, ss;
  struct timespec start, next;

  clock_gettime(CLOCK_REALTIME, &start);
  while (fstop_go) {

    /* Find the local sidereal time 1 msec from now */
    add_nsec(&start, &next, i * 1e6);
    tjd = tJDNow(next);
    lst = lSTAtTJD(tjd);

    /* Next find the hour angle of the source */
    ha = lst - source_rA;

    /* Update our DSM variables every FSTOP_UPDATE steps 
       also, print out some useful info */
    if ((i % FSTOP_UPDATE) == 0){

      /* Do the DSM update */
      result = update_vars();
      if (result < 0)
	printf("update_vars returned error code %d\r\n", result);

      /* Convert LST into H:M:S */
      lst_24h = lst/HOURS_TO_RADIANS;
      hh = (int)lst_24h;
      mm = (int)((lst_24h - (double)hh)*60);
      ss = (lst_24h - (double)hh - ((double)mm)/60.0)*3600.0;

      /* Print out some useful information every FSTOP_UPDATE steps */
      printf("HA=%25.15f, ", ha * (12.0/PI));
      printf("LST=%d:%02d:%05.2f, ", hh, mm, ss);
      printf("Delay[0]=%25.15f, ", total_delay[0]);
      printf("Delay[1]=%25.15f\n", total_delay[1]);

    }

    for (j=0; j<N_INPUTS; j++) {

      pthread_mutex_lock(&fstop_mutex);

      /* Solve for the geometric delay */
      total_delay[j] = delays[j] + delay_trip[j][0] + \
	cos(ha)*delay_trip[j][1] + sin(ha)*delay_trip[j][2];

      /* And the fringe phase */
      total_phase[j] = phases[j] + sign(fstop_freq) * \
	mod(360 * total_delay[j] * fabs(fstop_freq), 360);

      pthread_mutex_unlock(&fstop_mutex);

      /* Bring phase within range */
      if (total_phase[j] > 180.0) {
	total_phase[j] -= 360.0;
      } else if (total_phase[j] < -180.0) {
	total_phase[j] += 360.0;
      }

    }

    wait_for(next);

    for (j=0; j<N_INPUTS; j++) {

      pthread_mutex_lock(&fstop_mutex);

      /* If fringe phase stopping is enabled use that, 
	 otherwise use the constant phase */
      final_phase = fstop_pha_en ? total_phase[j] : phases[j];

      /* Set the phase values */
      result = set_phase(j, final_phase, tr);
      if (result < 0) {
	printf("set_phase returned error code %d\r\n", result);
      }

      /* If fringe delay tracking is enabled use that, 
	 otherwise use the constant delay */
      final_delay = fstop_del_en ? total_delay[j] : delays[j];

      /* Convert ns to samples */
      delay_samp = final_delay * SAMPLE_FREQ;

      /* Set the coarse delay values */
      cdelay = (int)delay_samp;
      result = set_cdelay(j, cdelay, tr);
      if (result < 0) {
	printf("set_cdelay returned error code %d\r\n", result);
      }

      /* Set the fine delay values */
      fdelay = delay_samp - cdelay;
      result = set_fdelay(j, fdelay, tr);
      if (result < 0) {
	printf("set_fdelay returned error code %d\r\n", result);
      }

      pthread_mutex_unlock(&fstop_mutex);

    }

    i++;
  }

  return tr;
}

int set_fstop_cmd(struct katcp_dispatch *d, int argc){
  int del_en, pha_en;
  char *fstopstr, *longitudestr;

  /* Grab the first argument, the fringe-stopping frequency */
  fstopstr = arg_string_katcp(d, 1);
  if (fstopstr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse first command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Convert the given string to a double */
  pthread_mutex_lock(&fstop_mutex);
  fstop_freq = atof(fstopstr);
  if (fstop_freq == 0.0){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "atof returned 0.0, is that what you wanted?");
  }
  pthread_mutex_unlock(&fstop_mutex);

  /* Grab the second argument, the longitude */
  longitudestr = arg_string_katcp(d, 2);
  if (longitudestr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse third command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Convert the given string to a double */
  pthread_mutex_lock(&fstop_mutex);
  longitude = atof(longitudestr);
  if (longitude == 0.0){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "atof returned 0.0, is that what you wanted?");
  }

  /* Grab the third argument, enable delay flag */
  del_en = arg_unsigned_long_katcp(d, 3);
  fstop_del_en = del_en > 0 ? TRUE : FALSE;

  /* Grab the fourth argument, enable phase flag */
  pha_en = arg_unsigned_long_katcp(d, 4);
  fstop_pha_en = pha_en > 0 ? TRUE : FALSE;

  pthread_mutex_unlock(&fstop_mutex);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "fstop=%.6f, long=%.6f", fstop_freq, longitude);
  return KATCP_RESULT_OK;
}

int set_dtrip_cmd(struct katcp_dispatch *d, int argc){
  int input;
  char *Astr, *Bstr, *Cstr;
  double A, B, C;

  /* Grab the first argument, input number = (0, N_INPUTS) */
  input = arg_unsigned_long_katcp(d, 1);
  if (input < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse first command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Grab the second argument the first delay triplet */
  Astr = arg_string_katcp(d, 2);
  if (Astr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse second command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Convert the given string to a double */
  A = atof(Astr);
  if (A == 0.0){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "atof returned 0.0, is that what you wanted?");
  }

  /* Grab the third argument the second delay triplet */
  Bstr = arg_string_katcp(d, 3);
  if (Bstr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse second command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Convert the given string to a double */
  B = atof(Bstr);
  if (B == 0.0){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "atof returned 0.0, is that what you wanted?");
  }

  /* Grab the fourth argument the third delay triplet */
  Cstr = arg_string_katcp(d, 4);
  if (Cstr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse second command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Convert the given string to a double */
  C = atof(Cstr);
  if (C == 0.0){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "atof returned 0.0, is that what you wanted?");
  }

  /* Finally, set the global variables */
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "setting input %d delay triplet to (%.6f, %.6f, %.6f)", input, A, B, C);
  pthread_mutex_lock(&fstop_mutex);
  delay_trip[input][0] = A;
  delay_trip[input][1] = B;
  delay_trip[input][2] = C;
  pthread_mutex_unlock(&fstop_mutex);

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

int info_fstop_cmd(struct katcp_dispatch *d, int argc){

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "Longitude: %f", longitude);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "Freq:      %f", fstop_freq);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "Source rA: %f", source_rA);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "A[0]: %f", delay_trip[0][0]);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "B[0]: %f", delay_trip[0][1]);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "C[0]: %f", delay_trip[0][2]);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "A[1]: %f", delay_trip[1][0]);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "B[1]: %f", delay_trip[1][1]);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "C[1]: %f", delay_trip[1][2]);

  return KATCP_RESULT_OK;
}

struct PLUGIN KATCP_PLUGIN = {
  .n_cmds = 9,
  .name = "sma-astro",
  .version = KATCP_PLUGIN_VERSION,
  .cmd_array = {
    { // 1
      .name = "?sma-astro-delay-set", 
      .desc = "set constant delay offset per input (?sma-astro-delay-set input delay)",
      .cmd = set_delay_cmd
    },
    { // 2
      .name = "?sma-astro-phase-set", 
      .desc = "set constant phase offset per input (?sma-astro-phase-set input phase)",
      .cmd = set_phase_cmd
    },
    { // 3
      .name = "?sma-astro-delay-get", 
      .desc = "get constant delay offset for input (?sma-astro-delay-set input)",
      .cmd = get_delay_cmd
    },
    { // 4
      .name = "?sma-astro-phase-get", 
      .desc = "get constant phase offset per input (?sma-astro-phase-set input)",
      .cmd = get_phase_cmd
    },
    { // 5
      .name = "?sma-astro-fstop-set", 
      .desc = "set various fringe-stopping parameters (?sma-astro-fstop-set fstop longitude del_en pha_en)",
      .cmd = set_fstop_cmd
    },
    { // 6
      .name = "?sma-astro-dtrip-set", 
      .desc = "set the delay triplet for an input (?sma-astro-fstop-set input A B C)",
      .cmd = set_dtrip_cmd
    },
    { // 7
      .name = "?sma-astro-fstop-start", 
      .desc = "start fringe-stopping loop (?sma-astro-fstop-start)",
      .cmd = start_fstop_cmd
    },
    { // 8
      .name = "?sma-astro-fstop-stop", 
      .desc = "stop fringe-stopping loop (?sma-astro-fstop-stop)",
      .cmd = stop_fstop_cmd
    },
    { // 9
      .name = "?sma-astro-fstop-info", 
      .desc = "print some fstop information (?sma-astro-fstop-info)",
      .cmd = info_fstop_cmd
    },
  }
};
