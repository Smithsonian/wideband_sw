#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <katcp.h>
#include <sys/mman.h>
#include <tcpborphserver3.h>
#include <plugin.h>
#include <dsm.h>

#define HB_PERIOD 0.010082461
#define WALSH_LEN 1024
#define WALSH_ARM_REG "sync_ctrl"
#define WALSH_TABLE_BRAM "walsh_table"
#define DDS_HOST "newdds"
#define DSM_ARMAT_VAR "SYNC_UNIX_TIME_D"
#define DSM_WALSH_VAR "PATTERN_V2_V64_B"
#define DSM_WALSH_INP 2
#define DSM_WALSH_SKIP 2
#define DSM_WALSH_LEN 64

double timespec_to_double(struct timespec in) {
  return (double)in.tv_sec + (1e-9) * ((double)in.tv_nsec);
}

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

int test_walsh_cmd(struct katcp_dispatch *d, int argc){
  int iter = 0;
  char *regname;
  uint32_t value;
  double sec, msec, period;
  long long tv_sec, tv_nsec;
  struct tbs_raw *tr;
  struct tbs_entry *te;
  struct timespec start, next, last;

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

  /* Grab the first argument, name of reg to twiddle */
  regname = arg_string_katcp(d, 1);
  if (regname == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse first command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Grab the second argument, period in microseconds */
  period = (double)arg_unsigned_long_katcp(d, 2) * 1e-3;
  if (period == 0.0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse second command line argument");
    return KATCP_RESULT_FAIL;
  }
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "period=%.6f ms", period);

  /* Get the register pointer */
  te = find_data_avltree(tr->r_registers, regname);
  if(te == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register %s not defined", regname);
    return KATCP_RESULT_FAIL;
  }

  /* Get current value of WALSH_ARM_REG */
  value = *((uint32_t *)(tr->r_map + te->e_pos_base));
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "arm ctrl value = %d", (int)value);

  /* Loop for a minute */
  clock_gettime(CLOCK_REALTIME, &start);
  start.tv_sec++;
  start.tv_nsec = 0;
  while (iter < (int)(60000.0/period)) {
    iter++; 
    msec = iter * period;
    sec = msec * 1e-3;
    tv_sec = (long long)sec;
    tv_nsec = ((long long)(msec * 1e6) % 1000000000);
    next.tv_sec = start.tv_sec + tv_sec;
    next.tv_nsec = start.tv_nsec + tv_nsec;
    last = wait_for(next);
    //log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "iter==%lld.%09lld", tv_sec, tv_nsec);

    /* Twiddle the LSB value */
    *((uint32_t *)(tr->r_map + te->e_pos_base)) = value | (iter%2);
    msync(tr->r_map, tr->r_map_size, MS_SYNC);

  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "start=%d.%09d", start.tv_sec, start.tv_nsec);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "stop==%d.%09d", last.tv_sec, last.tv_nsec);
  return KATCP_RESULT_OK;

}

int load_pattern_walsh_cmd(struct katcp_dispatch *d, int argc){
  int i, j, s, input;
  time_t timeStamp;
  char walshBytes[2][64];
  uint32_t *ind, value, patval;
  struct tbs_raw *tr;
  struct tbs_entry *te;

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

  /* Get the pointer to the Walsh table block memory */
  te = find_data_avltree(tr->r_registers, WALSH_TABLE_BRAM);
  if(te == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register %s not defined", WALSH_TABLE_BRAM);
    return KATCP_RESULT_FAIL;
  }

  /* Grab the Walsh pattern using DSM */
  s = dsm_read(DDS_HOST, DSM_WALSH_VAR, &walshBytes, &timeStamp);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_read()");
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dsm_read('%s', '%s'): failed with %d", DDS_HOST, DSM_WALSH_VAR, s);
    return KATCP_RESULT_FAIL;
  }

  /* For each input ... */
  for (input=0; input<DSM_WALSH_INP; input++) {

    /* Repeat the pattern to fill the block */
    for (i=0; i<WALSH_LEN/(DSM_WALSH_LEN/DSM_WALSH_SKIP); i++) {
      for (j=0; j<(DSM_WALSH_LEN/DSM_WALSH_SKIP); j++) {

	/* Get current value of WALSH_TABLE_BRAM[i*DSM_WALSH_LEN + j] */
	ind = tr->r_map + te->e_pos_base + 4*(i*(DSM_WALSH_LEN/DSM_WALSH_SKIP) + j);
	value = *((uint32_t *)ind);

	/* Mask in the requested values */
	patval = ((uint32_t)walshBytes[input][j*2] & 0xf) << (input*4);
	*((uint32_t *)ind) = value & ~(0xf << (input*4)) | patval;

	/* mysnc to update the memory map */
	msync(tr->r_map, tr->r_map_size, MS_SYNC);
      }
    }
  }

  return KATCP_RESULT_OK;
}

int arm_walsh_cmd(struct katcp_dispatch *d, int argc){
  int s, hb_offset;
  time_t timeStamp;
  uint32_t value, mask;
  struct tbs_raw *tr;
  struct tbs_entry *te;
  double start_db, now_db, arm_at_db;
  struct timespec start, now;

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

  /* Get the register pointer */
  te = find_data_avltree(tr->r_registers, WALSH_ARM_REG);
  if(te == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register %s not defined", WALSH_ARM_REG);
    return KATCP_RESULT_FAIL;
  }

  /* Get current value of WALSH_ARM_REG */
  value = *((uint32_t *)(tr->r_map + te->e_pos_base));
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "arm ctrl value = %d", (int)value);

  /* Set MSB=0 to prepare to arm */
  *((uint32_t *)(tr->r_map + te->e_pos_base)) = value & 0x7fffffff;
  msync(tr->r_map, tr->r_map_size, MS_SYNC);

  /* Grab the first argument, offset in heartbeats */
  hb_offset = arg_signed_long_katcp(d, 1);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "hb_offset=%d", hb_offset);

  /* Grab the sync time over DSM */
  s = dsm_read(DDS_HOST, DSM_ARMAT_VAR, &arm_at_db, &timeStamp);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_read()");
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dsm_read('%s', '%s'): failed with %d", DDS_HOST, DSM_ARMAT_VAR, s);
    return KATCP_RESULT_FAIL;
  }

  /* Make sure arm_at is not in the past */
  clock_gettime(CLOCK_REALTIME, &now);
  if ((int)arm_at_db <= now.tv_sec){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%0.6f is in the past! It's %d.%06d now", 
		      arm_at_db, now.tv_sec, now.tv_nsec);
    return KATCP_RESULT_FAIL;
  }

  /* Print out requested arm time */
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "arm reqst=%.9f", arm_at_db);

  /* Add in the HB offset and print */
  arm_at_db += ((double)hb_offset) * HB_PERIOD;
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "arming at=%.9f", arm_at_db);

  /* ... and now we wait, timeout=60 seconds */
  clock_gettime(CLOCK_REALTIME, &start);
  start_db = timespec_to_double(start);
  now_db = timespec_to_double(now);
  while (now_db < arm_at_db) {
    clock_gettime(CLOCK_REALTIME, &now);
    now_db = timespec_to_double(now);
    if (now.tv_sec > start.tv_sec+60) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "arm timed out after 60 seconds");
      return KATCP_RESULT_FAIL;
    }
  }      

  /* Twiddle bits 31 and 29 finally to arm swof and mcnt */
  mask = 0xa0000000;
  *((uint32_t *)(tr->r_map + te->e_pos_base)) = value & ~mask;
  msync(tr->r_map, tr->r_map_size, MS_SYNC);
  *((uint32_t *)(tr->r_map + te->e_pos_base)) = value | mask;
  msync(tr->r_map, tr->r_map_size, MS_SYNC);
  *((uint32_t *)(tr->r_map + te->e_pos_base)) = value & ~mask;
  msync(tr->r_map, tr->r_map_size, MS_SYNC);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "armed at==%.9f", now_db);

  return KATCP_RESULT_OK;
}

struct PLUGIN KATCP_PLUGIN = {
  .n_cmds = 3,
  .name = "sma-walsh",
  .version = KATCP_PLUGIN_VERSION,
  .cmd_array = {
    {
      .name = "?sma-walsh-arm", 
      .desc = "arm the SOWF generator at the DSM-specified time (?sma-walsh-arm hb_offset)",
      .cmd = arm_walsh_cmd
    },
    {
      .name = "?sma-walsh-pattern-load", 
      .desc = "load the current DSM-specified Walsh pattern (?sma-walsh-load)",
      .cmd = load_pattern_walsh_cmd
    },
    {
      .name = "?sma-walsh-test", 
      .desc = "generates a 50% duty cycle pulse with period of N us (?sma-walsh-test regname period)",
      .cmd = test_walsh_cmd
    },
  }
};
