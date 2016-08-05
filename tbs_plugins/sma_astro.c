#include <math.h>
#include <time.h>
#include <unistd.h> // only for usleep
#include <string.h>
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
#define CDELAY_MID 24576 // middle of coarse delay FIFO
#define SAMPLE_FREQ 4.576 // sample rate in GHz
#define DSM_READ_FREQ 100
#define DSM_WRITE_FREQ 10000
#define DSM_GEOM_VAR "SWARM_SOURCE_GEOM_X"
#define DSM_DUT1 "UT1MUTC_D"
#define DSM_GEOM_RA "SOURCE_RA_D"
#define DSM_GEOM_A "GEOM_DELAY_A_V2_D"
#define DSM_GEOM_B "GEOM_DELAY_B_V2_D"
#define DSM_GEOM_C "GEOM_DELAY_C_V2_D"
#define DSM_FOFF_VAR "SWARM_FIXED_OFFSETS_X"
#define DSM_DEL_OFF "DELAY_V2_D"
#define DSM_PHA_OFF "PHASE_V2_D"
#define DSM_FSTATS_VAR "SWARM_FSTOP_STATS_X"
#define DSM_FSTOP_DEL "FSTOP_DELAY_V2_D"
#define DSM_FSTOP_LST "FSTOP_LST_D"
#define DSM_FSTOP_HA  "FSTOP_HA_D"
#define DSM_FSTOP_UT  "FSTOP_UT_D"

/* DSM host names, may be changed by user */
volatile char dds_host[DSM_NAME_LENGTH] = "newdds";
volatile char obs_host[DSM_NAME_LENGTH] = "obscon";

/* These are the constant, user-programmable delays and phases */
volatile double delays[N_INPUTS];
volatile double phases[N_INPUTS];

/* Some global variables for fringe-stopping */
volatile int fstop_go = FALSE;
volatile int fstop_del_en = TRUE;
volatile int fstop_pha_en = TRUE;
volatile double source_rA = 0.0;
volatile double global_dut1 = 0.0;
volatile double longitude = -2.71359;
volatile double fstop_freq[N_INPUTS] = {7.850, -12.150};
volatile double delay_trip[N_INPUTS][3];
volatile double final_delay[N_INPUTS];
volatile double final_phase[N_INPUTS];
volatile double ut, ha, lst, tjd;
pthread_mutex_t fstop_mutex;
pthread_t fstop_thread;

/* Some global monitoring variables */
volatile int late_errors = 0;
volatile double  delay_rate[N_INPUTS] = {0.0, 0.0};
volatile double fringe_rate[N_INPUTS] = {0.0, 0.0};
volatile double late_ms = 0, max_late_ms = 0, avg_late_ms = 0;

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

/* Update DSM-derived variables */
int fstop_dsm_read() {
  int s;
  time_t timeStamp;
  dsm_structure structure;
  double rA, a[2], b[2], c[2];
  double del_off[2], pha_off[2];
  double dut1;

  /* Initialize the DSM geometry structure */
  s = dsm_structure_init(&structure, DSM_GEOM_VAR);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_init()");
    return -1;
  }

  /* Read the structure over DSM */
  s = dsm_read((char *)dds_host, DSM_GEOM_VAR, &structure, &timeStamp);
  if (s != DSM_SUCCESS) {
    dsm_structure_destroy(&structure);
    dsm_error_message(s, "dsm_read()");
    return -2;
  }

  /* Get the UT1-UTC (DUT1) element */
  s = dsm_structure_get_element(&structure, DSM_DUT1, &dut1);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_get_element(dut1)");
    dsm_structure_destroy(&structure);
    return -3;
  }

  /* Get the source rA element */
  s = dsm_structure_get_element(&structure, DSM_GEOM_RA, &rA);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_get_element(rA)");
    dsm_structure_destroy(&structure);
    return -3;
  }

  /* Get part A of delay triplet */
  s = dsm_structure_get_element(&structure, DSM_GEOM_A, &a[0]);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_get_element(A)");
    dsm_structure_destroy(&structure);
    return -4;
  }

  /* Get part B of delay triplet */
  s = dsm_structure_get_element(&structure, DSM_GEOM_B, &b[0]);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_get_element(B)");
    dsm_structure_destroy(&structure);
    return -5;
  }

  /* Get part C of delay triplet */
  s = dsm_structure_get_element(&structure, DSM_GEOM_C, &c[0]);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_get_element(C)");
    dsm_structure_destroy(&structure);
    return -6;
  }

  /* Destroy structure before re-creating */
  dsm_structure_destroy(&structure);

  /* Initialize the fixed offset structure */
  s = dsm_structure_init(&structure, DSM_FOFF_VAR);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_init()");
    return -7;
  }

  /* Read the structure over DSM */
  s = dsm_read((char *)obs_host, DSM_FOFF_VAR, &structure, &timeStamp);
  if (s != DSM_SUCCESS) {
    dsm_structure_destroy(&structure);
    dsm_error_message(s, "dsm_read()");
    return -8;
  }

  /* Get the fixed delay offset */
  s = dsm_structure_get_element(&structure, DSM_DEL_OFF, &del_off[0]);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_get_element(del_off)");
    dsm_structure_destroy(&structure);
    return -9;
  }

  /* Get the fixed phase offset */
  s = dsm_structure_get_element(&structure, DSM_PHA_OFF, &pha_off[0]);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_get_element(pha_off)");
    dsm_structure_destroy(&structure);
    return -10;
  }

  /* Set the global variables */
  pthread_mutex_lock(&fstop_mutex);
  source_rA = rA;
  global_dut1 = dut1;
  delays[0] = del_off[0];
  phases[0] = pha_off[0];
  delays[1] = del_off[1];
  phases[1] = pha_off[1];
  delay_trip[0][0] = -1.0 * a[0] * 1e9;
  delay_trip[0][1] = -1.0 * b[0] * 1e9;
  delay_trip[0][2] = -1.0 * c[0] * 1e9;
  delay_trip[1][0] = -1.0 * a[1] * 1e9;
  delay_trip[1][1] = -1.0 * b[1] * 1e9;
  delay_trip[1][2] = -1.0 * c[1] * 1e9;
  pthread_mutex_unlock(&fstop_mutex);

  /* Destroy structure before re-creating */
  dsm_structure_destroy(&structure);

  return 0;
}

/* Update our own DSM variables */
int fstop_dsm_write() {
  int s;
  dsm_structure structure;

  /* Initialize the fstop stats structure */
  s = dsm_structure_init(&structure, DSM_FSTATS_VAR);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_init()");
    return -1;
  }

  /* Set the fstop delay part */
  pthread_mutex_lock(&fstop_mutex);
  s = dsm_structure_set_element(&structure, DSM_FSTOP_DEL, (double *)&final_delay);
  pthread_mutex_unlock(&fstop_mutex);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_set_element(fstop_del)");
    dsm_structure_destroy(&structure);
    return -2;
  }

  /* Set the UT part */
  pthread_mutex_lock(&fstop_mutex);
  s = dsm_structure_set_element(&structure, DSM_FSTOP_UT, (double *)&ut);
  pthread_mutex_unlock(&fstop_mutex);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_set_element(fstop_ut)");
    dsm_structure_destroy(&structure);
    return -2;
  }

  /* Set the LST part */
  pthread_mutex_lock(&fstop_mutex);
  s = dsm_structure_set_element(&structure, DSM_FSTOP_LST, (double *)&lst);
  pthread_mutex_unlock(&fstop_mutex);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_set_element(fstop_lst)");
    dsm_structure_destroy(&structure);
    return -2;
  }

  /* Set the HA part */
  pthread_mutex_lock(&fstop_mutex);
  s = dsm_structure_set_element(&structure, DSM_FSTOP_HA, (double *)&ha);
  pthread_mutex_unlock(&fstop_mutex);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_set_element(fstop_ha)");
    dsm_structure_destroy(&structure);
    return -2;
  }

  /* Then write it to the monitor class */
  s = dsm_write("SWARM_MONITOR", DSM_FSTATS_VAR, &structure);
  if (s != DSM_SUCCESS) {
    dsm_structure_destroy(&structure);
    dsm_error_message(s, "dsm_write()");
    return -3;
  }

  /* Clear up the allocated memory */
  dsm_structure_destroy(&structure);

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
  double last_delay [N_INPUTS];
  double last_phase [N_INPUTS];
  double total_delay[N_INPUTS];
  double total_phase[N_INPUTS];

  struct timespec start, next, now;
  struct timespec next_ut1;
  double tot_late_ms = 0;

  clock_gettime(CLOCK_REALTIME, &start);
  while (fstop_go) {

    /* Find the UTC and UT1 at 1 msec from now */
    add_nsec(&start, &next, i * 1e6);
    add_nsec(&next, &next_ut1, global_dut1 * 1e9);

    /* Next find the hour angle of the source */
    pthread_mutex_lock(&fstop_mutex);
    tjd = tJDNow(next_ut1);
    lst = lSTAtTJD(tjd);
    ha = lst - source_rA;
    ut = timespec_to_double(next_ut1);
    pthread_mutex_unlock(&fstop_mutex);

    /* Read our DSM variables every DSM_READ_FREQ */
    if ((i % DSM_READ_FREQ) == 0){

      /* Do the DSM update */
      result = fstop_dsm_read();
      if (result < 0)
	printf("fstop_dsm_read returned error code %d\r\n", result);

    }

    /* Write our DSM variables every DSM_WRITE_FREQ */
    if ((i % DSM_WRITE_FREQ) == 0){

      /* Do the DSM update */
      result = fstop_dsm_write();
      if (result < 0)
	printf("fstop_dsm_write returned error code %d\r\n", result);

    }

    for (j=0; j<N_INPUTS; j++) {

      pthread_mutex_lock(&fstop_mutex);

      /* Grab the current delay/phase */
      last_delay[j] = final_delay[j];
      last_phase[j] = final_phase[j];

      /* Solve for the geometric delay */
      total_delay[j] = delays[j] + delay_trip[j][0] + \
	cos(ha)*delay_trip[j][1] + sin(ha)*delay_trip[j][2];

      /* If fringe delay tracking is enabled use that, 
	 otherwise use the constant delay */
      final_delay[j] = fstop_del_en ? total_delay[j] : delays[j];

      /* And the fringe phase */
      total_phase[j] = phases[j] + sign(fstop_freq[j]) * \
	mod(360 * total_delay[j] * fabs(fstop_freq[j]), 360);

      /* If fringe phase stopping is enabled use that, 
	 otherwise use the constant phase */
      final_phase[j] = fstop_pha_en ? total_phase[j] : phases[j];

      /* Bring phase within range */
      if (final_phase[j] > 180.0) {
	final_phase[j] -= 360.0;
      } else if (final_phase[j] < -180.0) {
	final_phase[j] += 360.0;
      }

      /* Update the monitoring variables */
      delay_rate[j]  = (final_delay[j] - last_delay[j]) * 1e3;
      fringe_rate[j] = (final_phase[j] - last_phase[j]) * (1e3 / 360.0);

      /* Print some useful information */
      printf("%25.15f:  ", ut);
      printf("HA=%25.15f  ", ha * (12.0/M_PI));
      printf("Phase[%d] = %25.15f    ", j, final_phase[j]);
      printf("Rate [%d] = %25.15f    ", j, fringe_rate[j]);
      if (j==1)
        printf("\n");

      pthread_mutex_unlock(&fstop_mutex);

    }

    wait_for(next);

    for (j=0; j<N_INPUTS; j++) {

      pthread_mutex_lock(&fstop_mutex);

      /* Set the phase values */
      result = set_phase(j, final_phase[j], tr);
      if (result < 0) {
	printf("set_phase returned error code %d\r\n", result);
      }

      /* Convert ns to samples */
      delay_samp = final_delay[j] * SAMPLE_FREQ;

      /* Find the coarse and fine delays */
      cdelay = (int)delay_samp;
      fdelay = delay_samp - cdelay;

      /* Set the coarse delay values */
      result = set_cdelay(j, cdelay, tr);
      if (result < 0) {
	printf("set_cdelay returned error code %d\r\n", result);
      }

      /* Set the fine delay values */
      result = set_fdelay(j, fdelay, tr);
      if (result < 0) {
	printf("set_fdelay returned error code %d\r\n", result);
      }

      pthread_mutex_unlock(&fstop_mutex);

    }

    i++;

    pthread_mutex_lock(&fstop_mutex);

    /* Take note of the time we updated, and how late we were */
    clock_gettime(CLOCK_REALTIME, &now);
    late_ms = (timespec_to_double(now) - ut) * 1e3;

    /* Generate some lateness statistics */
    if (late_ms > max_late_ms)
      max_late_ms = late_ms;
    if (late_ms > 1.0) {
      late_errors++;
      tot_late_ms += late_ms;
      avg_late_ms = tot_late_ms/late_errors;
    }

    pthread_mutex_unlock(&fstop_mutex);

  }

  return tr;
}

int set_hosts_cmd(struct katcp_dispatch *d, int argc){
  char *dds_host_arg, *obs_host_arg;

  /* Grab the first argument, the hostname of the DDS server */
  dds_host_arg = arg_string_katcp(d, 1);
  if (dds_host_arg == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse first command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Grab the second argument, the hostname of the OBSCON/MONITOR server */
  obs_host_arg = arg_string_katcp(d, 2);
  if (obs_host_arg == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse first command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Copy the host names over to our volatile variables */
  pthread_mutex_lock(&fstop_mutex);
  strcpy((char *)dds_host, dds_host_arg);
  strcpy((char *)obs_host, obs_host_arg);
  pthread_mutex_unlock(&fstop_mutex);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "dds_host=%s, obs_host=%s", dds_host, obs_host);
  return KATCP_RESULT_OK;
}

int set_fstop_cmd(struct katcp_dispatch *d, int argc){
  int del_en, pha_en;
  char *fstopstr_0, *fstopstr_1, *longitudestr;

  /* Grab the first argument, the fringe-stopping frequency for input 0 */
  fstopstr_0 = arg_string_katcp(d, 1);
  if (fstopstr_0 == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse first command line argument");
    return KATCP_RESULT_FAIL;
  }
 
  /* Grab the second argument, the fringe-stopping frequency for input 1 */
  fstopstr_1 = arg_string_katcp(d, 2);
  if (fstopstr_1 == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse first command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Convert the given string to a double for input 0 */
  pthread_mutex_lock(&fstop_mutex);
  fstop_freq[0] = atof(fstopstr_0);
  if (fstop_freq[0] == 0.0){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "atof returned 0.0, is that what you wanted?");
  }
  pthread_mutex_unlock(&fstop_mutex);

  /* Convert the given string to a double for input 1 */
  pthread_mutex_lock(&fstop_mutex);
  fstop_freq[1] = atof(fstopstr_1);
  if (fstop_freq[1] == 0.0){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "atof returned 0.0, is that what you wanted?");
  }
  pthread_mutex_unlock(&fstop_mutex);

  /* Grab the second argument, the longitude */
  longitudestr = arg_string_katcp(d, 3);
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
  del_en = arg_unsigned_long_katcp(d, 4);
  fstop_del_en = del_en > 0 ? TRUE : FALSE;

  /* Grab the fourth argument, enable phase flag */
  pha_en = arg_unsigned_long_katcp(d, 5);
  fstop_pha_en = pha_en > 0 ? TRUE : FALSE;

  pthread_mutex_unlock(&fstop_mutex);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "fstop[0]=%.6f, fstop[1]=%.6f, long=%.6f", fstop_freq[0], fstop_freq[1], longitude);
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

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "DDS:%s",    dds_host);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "OBSCON:%s", obs_host);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "Freq[0]:%f", fstop_freq[0]);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "Freq[1]:%f", fstop_freq[1]);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "Longitude:%f", longitude);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "Source rA:%f", source_rA);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "DUT1:%f", global_dut1);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "LST:%f", lst);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "HA:%f", ha);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "UT:%f", ut);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "FringeRate[0]:%.6f~Hz", fringe_rate[0]);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "FringeRate[1]:%.6f~Hz", fringe_rate[1]);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "DelayRate[0]:%.6f~ns/s", delay_rate[0]);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "DelayRate[1]:%.6f~ns/s", delay_rate[1]);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "Max.Lateness:%.6f~ms", max_late_ms);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "Avg.Lateness:%.6f~ms", avg_late_ms);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "LateErrors(>1ms):%d", late_errors);

  return KATCP_RESULT_OK;
}

struct PLUGIN KATCP_PLUGIN = {
  .n_cmds = 5,
  .name = "sma-astro",
  .version = KATCP_PLUGIN_VERSION,
  .uninit = stop_fstop_cmd,
  .cmd_array = {
    { // 0
      .name = "?sma-astro-hosts-set",
      .desc = "set the DSM hostnames from which variables are read (?sma-astro-hosts-set dds_host obs_host)",
      .cmd = set_hosts_cmd
    },
    { // 1
      .name = "?sma-astro-fstop-set", 
      .desc = "set various fringe-stopping parameters (?sma-astro-fstop-set fstop_0 fstop_1 longitude del_en pha_en)",
      .cmd = set_fstop_cmd
    },
    { // 2
      .name = "?sma-astro-fstop-start", 
      .desc = "start fringe-stopping loop (?sma-astro-fstop-start)",
      .cmd = start_fstop_cmd
    },
    { // 3
      .name = "?sma-astro-fstop-stop", 
      .desc = "stop fringe-stopping loop (?sma-astro-fstop-stop)",
      .cmd = stop_fstop_cmd
    },
    { // 4
      .name = "?sma-astro-fstop-info", 
      .desc = "print some fstop information (?sma-astro-fstop-info)",
      .cmd = info_fstop_cmd
    },
  }
};
