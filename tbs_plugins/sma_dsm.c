#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <katcp.h>
#include <sys/mman.h>
#include <tcpborphserver3.h>
#include <pthread.h>
#include <signal.h>
#include <plugin.h>
#include <unistd.h>
#include <dsm.h>

#define TRUE 1
#define FALSE 0

#define DSM_HOST_NAME "hal9000"
#define DSM_SCAN_LENGTH "SWARM_SCAN_LENGTH_L"

#define DSM_WRITE_HOST "OBS"
#define DSM_SCAN_PROGRESS "SWARM_SCAN_PROGRESS_F"

#define SWARM_TOTAL_WSTEPS 64
#define SWARM_VECTORS_PER_WSTEP 11
#define SWARM_XENG_CTRL "xeng_ctrl"
#define SWARM_XENG_STAT "xeng_status"

/* Some global variables */
pthread_mutex_t fpga_mutex;

/* Some global variables for DSM waiting */
volatile int updates = 0;
volatile int dsm_errors = 0;
volatile int hdlr_errors = 0;
volatile int waiting = FALSE;
pthread_t waiting_thread;

/* Some global variables for DSM writing */
volatile int writing = FALSE;
pthread_t writing_thread;

/* Scan progress writer */
int write_scan_progress(struct tbs_raw *tr){
  int s;
  float progress;
  uint32_t xn_stat;
  struct tbs_entry *te;

  /* Make sure we're programmed */
  if(tr->r_fpga != TBS_FPGA_MAPPED){
    return -1;
  }

  /* Get the xeng_status register pointer */
  te = find_data_avltree(tr->r_registers, SWARM_XENG_STAT);
  if(te == NULL){
    return -2;
  }

  pthread_mutex_lock(&fpga_mutex);

  /* Get current value of the register */
  xn_stat = *((uint32_t *)(tr->r_map + te->e_pos_base));

  pthread_mutex_unlock(&fpga_mutex);

  /* Convert to scan progress in seconds */
  progress = xn_stat * 128.0 * 2048.0 / (26.0 * SWARM_VECTORS_PER_WSTEP * 1e6);

  /* Then write it to the DSM host */
  s = dsm_write(DSM_WRITE_HOST, DSM_SCAN_PROGRESS, &progress);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_write()");
    return -3;
  }

  return 0;
}

/* Scan length handler */
int handle_scan_length(struct tbs_raw *tr, int scan_length){
  uint32_t xn_num;
  struct tbs_entry *te;
  uint32_t old_value, new_value;

  /* Derive xn_num from scan_length in Walsh cycles */
  xn_num = SWARM_VECTORS_PER_WSTEP * SWARM_TOTAL_WSTEPS * scan_length;

  /* Make sure we're programmed */
  if(tr->r_fpga != TBS_FPGA_MAPPED){
    return -1;
  }

  /* Get the xeng_ctrl register pointer */
  te = find_data_avltree(tr->r_registers, SWARM_XENG_CTRL);
  if(te == NULL){
    return -2;
  }

  pthread_mutex_lock(&fpga_mutex);

  /* Get current value of the register */
  old_value = *((uint32_t *)(tr->r_map + te->e_pos_base));

  /* Set the proper xn_num bits */
  new_value = (old_value & 0xe0000000) + (xn_num & 0x1fffffff);

  /* Finally, write it to the mmap region */
  *((uint32_t *)(tr->r_map + te->e_pos_base)) = new_value;
  msync(tr->r_map, tr->r_map_size, MS_SYNC);

  pthread_mutex_unlock(&fpga_mutex);

  return 0;
}

/* SIGINT handler stops waiting */
void sigint_handler(int sig){
  waiting = FALSE;
}

/* Function that handles the waiting */
void * dsm_wait_dispatch(void * tr){
  int status;
  int new_data;
  char host_name[DSM_FULL_NAME_LENGTH], var_name[DSM_FULL_NAME_LENGTH];

  /* Set our INT signal handler */
  signal(SIGINT, sigint_handler);

  /* Add the scan length to the monitor list */
  status = dsm_monitor(DSM_HOST_NAME, DSM_SCAN_LENGTH);
  if (status != DSM_SUCCESS) {
    dsm_error_message(status, "dsm_monitor call for SWARM_SCAN_LENGTH_D");
  }

  /* Start the waiting loop */
  while (waiting) {

    /* Wait for an update */
    status = dsm_read_wait(host_name, var_name, (char *)&new_data);
    if (status != DSM_SUCCESS) {
      if (status != DSM_INTERRUPTED) {
	dsm_error_message(status, "dsm_read_wait");
	dsm_errors++;
      }
      continue;
    }

    /* Dispatch to the right function */
    if (!strcmp(DSM_SCAN_LENGTH, var_name)) {
      status = handle_scan_length(tr, new_data);
      if (status < 0) {
	fprintf(stderr, "Error from handle_scan_length: %d\n", status);
	hdlr_errors++;
      }
    }
    else {
      fprintf(stderr, "Unexpected dsm_read_wait, host %s variable %s\n", host_name, var_name);
    }

    /* Increment the updates counter */
    updates++;
  }

  return tr;
}

int start_waiting_cmd(struct katcp_dispatch *d, int argc){
  int status;
  struct tbs_raw *tr;

  /* Grab the mode pointer */
  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire raw mode state");
    return KATCP_RESULT_FAIL;
  }

  /* Set flag to start fringe-stopping */
  waiting = TRUE;

  /* Start the thread */
  status = pthread_create(&waiting_thread, NULL, dsm_wait_dispatch, (void *)tr);
  if (status < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not create waiting thread");
    return KATCP_RESULT_FAIL;
  }
  
  return KATCP_RESULT_OK;
}

int stop_waiting_cmd(struct katcp_dispatch *d, int argc){

  /* Set flag to stop waiting */
  waiting = FALSE;

  /* Clear the DSM monitor list */
  dsm_clear_monitor();

  /* Send SIGINT to thread
     this should end dsm_read_wait */
  pthread_kill(waiting_thread, SIGINT);

  /* And join the thread */
  pthread_join(waiting_thread, NULL);

  return KATCP_RESULT_OK;
}

int status_waiting_cmd(struct katcp_dispatch *d, int argc){

  /* Send the status in a log */
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "DSM Waiting Status:%d", waiting);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "DSM Waiting Updates:%d", updates);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "DSM Waiting Errors:%d", dsm_errors);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "DSM Handler Errors:%d", hdlr_errors);

  /* Relay status back to the client */
  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
  append_double_katcp(d, KATCP_FLAG_DOUBLE, waiting);
  append_double_katcp(d, KATCP_FLAG_DOUBLE | KATCP_FLAG_LAST, updates);
  return KATCP_RESULT_OWN;

}

/* Function that handles writing to DSM */
void * dsm_write_dispatch(void * tr){
  int status;

  /* Start the writing loop */
  while (writing) {
    status = 0;

    /* Do our writing tasks in order */
    status += write_scan_progress(tr);

    /* Sleep one second */
    sleep(1.0);

  }

  return tr;
}

int start_writing_cmd(struct katcp_dispatch *d, int argc){
  int status;
  struct tbs_raw *tr;

  /* Grab the mode pointer */
  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire raw mode state");
    return KATCP_RESULT_FAIL;
  }

  /* Set flag to start writing to DSM */
  writing = TRUE;

  /* Start the thread */
  status = pthread_create(&writing_thread, NULL, dsm_write_dispatch, (void *)tr);
  if (status < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not create writing thread");
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int stop_writing_cmd(struct katcp_dispatch *d, int argc){

  /* Set flag to stop writing */
  writing = FALSE;

  /* Send SIGINT to thread
     this should end dsm_write */
  pthread_kill(writing_thread, SIGINT);

  /* And join the thread */
  pthread_join(writing_thread, NULL);

  return KATCP_RESULT_OK;
}

int status_writing_cmd(struct katcp_dispatch *d, int argc){

  /* Send the status in a log */
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "DSM Writing Status:%d", writing);

  /* Relay status back to the client */
  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
  append_double_katcp(d, KATCP_FLAG_DOUBLE | KATCP_FLAG_LAST, writing);
  return KATCP_RESULT_OWN;

}

int start_all_cmd(struct katcp_dispatch *d, int argc){
  int status;

  /* Start DSM waiting */
  status = start_waiting_cmd(d, argc);
  if (status < 0)
    return KATCP_RESULT_FAIL;

  /* Start DSM writing */
  status = start_writing_cmd(d, argc);
  if (status < 0)
    return KATCP_RESULT_FAIL;

  return KATCP_RESULT_OK;
}

int stop_all_cmd(struct katcp_dispatch *d, int argc){
  int status;

  /* Stop DSM waiting */
  status = stop_waiting_cmd(d, argc);
  if (status < 0)
    return KATCP_RESULT_FAIL;

  /* Stop DSM writing */
  status = stop_writing_cmd(d, argc);
  if (status < 0)
    return KATCP_RESULT_FAIL;

  return KATCP_RESULT_OK;
}

struct PLUGIN KATCP_PLUGIN = {
  .n_cmds = 6,
  .name = "sma-dsm",
  .version = KATCP_PLUGIN_VERSION,
  .init = start_all_cmd,
  .uninit = stop_all_cmd,
  .cmd_array = {
    { // 1
      .name = "?sma-dsm-wait-start",
      .desc = "start the DSM waiting loop (?sma-dsm-wait-start)",
      .cmd = start_waiting_cmd
    },
    { // 2
      .name = "?sma-dsm-wait-stop",
      .desc = "stop the DSM waiting loop (?sma-dsm-wait-stop)",
      .cmd = stop_waiting_cmd
    },
    { // 3
      .name = "?sma-dsm-wait-status",
      .desc = "returns status of the DSM waiting loop (?sma-dsm-wait-status)",
      .cmd = status_waiting_cmd
    },
    { // 4
      .name = "?sma-dsm-write-start",
      .desc = "start the DSM writing loop (?sma-dsm-write-start)",
      .cmd = start_writing_cmd
    },
    { // 5
      .name = "?sma-dsm-write-stop",
      .desc = "stop the DSM writing loop (?sma-dsm-write-stop)",
      .cmd = stop_writing_cmd
    },
    { // 6
      .name = "?sma-dsm-write-status",
      .desc = "returns status of the DSM writing loop (?sma-dsm-write-status)",
      .cmd = status_writing_cmd
    },
  }
};
