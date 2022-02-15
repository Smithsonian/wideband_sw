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

#define DSM_SCAN_X "SWARM_SCAN_X"
#define DSM_SCAN_X_LENGTH "LENGTH_L"
#define DSM_SCAN_X_PROGRESS "PROGRESS_L"
#define DSM_WRITE_HOST "SWARM_MONITOR"

#define SWARM_TOTAL_WSTEPS 64
#define SWARM_VECTORS_PER_WSTEP 11
#define SWARM_XENG_CTRL "xeng_ctrl"
#define SWARM_XENG_STAT "xeng_status"
#define SWARM_XENG_XNUM "xeng_xn_num"

/* Some global variables */
pthread_mutex_t fpga_mutex;

/* Some global variables for DSM waiting */
volatile int updates = 0;
volatile int dsm_errors = 0;
volatile int hdlr_errors = 0;
volatile int waiting = FALSE;
pthread_t waiting_thread;

/* Some global variables for DSM writing */
volatile int last_write_status = 0;
volatile int writing = FALSE;
pthread_t writing_thread;

/* Scan progress writer */
int write_scan_progress(struct tbs_raw *tr){
  int s;
  long xn_stat_l, xn_xnum_l;
  uint32_t xn_stat, xn_xnum;
  struct tbs_entry *te1, *te2;
  dsm_structure swarm_scan_ds;

  /* Make sure we're programmed */
  if(tr->r_fpga != TBS_FPGA_MAPPED){
    return -1;
  }

  /* Get the xeng_status register pointer */
  te1 = find_data_avltree(tr->r_registers, SWARM_XENG_STAT);
  if(te1 == NULL){
    return -2;
  }

  /* Get the xeng_xn_num register pointer */
  te2 = find_data_avltree(tr->r_registers, SWARM_XENG_XNUM);
  if(te2 == NULL){
    return -3;
  }

  pthread_mutex_lock(&fpga_mutex);

  /* Get current value of the registers */
  xn_stat = *((uint32_t *)(tr->r_map + te1->e_pos_base));
  xn_xnum = *((uint32_t *)(tr->r_map + te2->e_pos_base));

  pthread_mutex_unlock(&fpga_mutex);

  /* Initialize our DSM structure */
  s = dsm_structure_init(&swarm_scan_ds, DSM_SCAN_X);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_structure_init()");
    return -4;
  }

  /* Set the length element */
  xn_xnum_l = (long) xn_xnum; // cast to long
  s = dsm_structure_set_element(&swarm_scan_ds, DSM_SCAN_X_LENGTH, &xn_xnum_l);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_set_element()");
    dsm_structure_destroy(&swarm_scan_ds);
    return -5;
  }

  /* Set the progress element */
  xn_stat_l = (long) xn_stat; // cast to long
  s = dsm_structure_set_element(&swarm_scan_ds, DSM_SCAN_X_PROGRESS, &xn_stat_l);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_set_element()");
    dsm_structure_destroy(&swarm_scan_ds);
    return -6;
  }

  /* Then write it to the DSM host */
  s = dsm_write(DSM_WRITE_HOST, DSM_SCAN_X, &swarm_scan_ds);
  if (s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_write()");
    dsm_structure_destroy(&swarm_scan_ds);
    return -7;
  }

  /* Free the DSM structure */
  dsm_structure_destroy(&swarm_scan_ds);

  return 0;
}


/* SIGINT handler stops waiting */
void sigint_handler(int sig){
  waiting = FALSE;
}

/* Function that handles writing to DSM */
void * dsm_write_dispatch(void * tr){

  /* Start the writing loop */
  while (writing) {

    /* Do our writing tasks in order */
    last_write_status = write_scan_progress(tr);

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
  append_double_katcp(d, KATCP_FLAG_DOUBLE, writing);
  append_double_katcp(d, KATCP_FLAG_DOUBLE | KATCP_FLAG_LAST, last_write_status);
  return KATCP_RESULT_OWN;

}

int start_all_cmd(struct katcp_dispatch *d, int argc){
  int status;

  /* Start DSM writing */
  status = start_writing_cmd(d, argc);
  if (status < 0)
    return KATCP_RESULT_FAIL;

  return KATCP_RESULT_OK;
}

int stop_all_cmd(struct katcp_dispatch *d, int argc){
  int status;

  /* Stop DSM writing */
  status = stop_writing_cmd(d, argc);
  if (status < 0)
    return KATCP_RESULT_FAIL;

  return KATCP_RESULT_OK;
}

struct PLUGIN KATCP_PLUGIN = {
  .n_cmds = 3,
  .name = "sma-dsm",
  .version = KATCP_PLUGIN_VERSION,
  .init = start_all_cmd,
  .uninit = stop_all_cmd,
  .cmd_array = {
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
