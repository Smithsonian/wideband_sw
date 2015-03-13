#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <math.h>
#include <sys/stat.h>
#include <katcp.h>
#include <sys/mman.h>
#include <tcpborphserver3.h>
#include <dsm.h>
#include <pthread.h>
#include <plugin.h>
#include "sma_adc.h"

#define TRUE 1
#define FALSE 0
#define FAIL 1

#define SCOPE_0_DATA "scope_snap0_bram"
#define SCOPE_0_CTRL "scope_snap0_ctrl"
#define SCOPE_0_STATUS "scope_snap0_status"
#define SCOPE_1_DATA "scope_snap1_bram"
#define SCOPE_1_CTRL "scope_snap1_ctrl"
#define SCOPE_1_STATUS "scope_snap1_status"

#define EXPECT_READBACK_ERRORS 1

/* pointers for controlling and accessing the snapshot */
static int *ctrl_p[2], *stat_p[2];
static signed char *snap_p[2];
pthread_mutex_t snap_mutex;

/* the following are for programming the adc through the spi interface. */
#define ADC5G_CONTROLLER "adc5g_controller"
#define CONTROL_REG_ADDR 0x01
#define CHANSEL_REG_ADDR 0x0f
#define EXTOFFS_REG_ADDR 0x20
#define EXTGAIN_REG_ADDR 0x22
#define EXTPHAS_REG_ADDR 0x24
#define CALCTRL_REG_ADDR 0x10
#define FIRST_EXTINL_REG_ADDR 0x30
#define SPI_WRITE 0x80;

/* Pointer for accessing the spi interfrace */
uint32_t *spi_p;

/* Default file names */
#define SNAP_NAME "/instance/adcTests/snap"
#define OGP_BASE_NAME "/instance/configFiles/ogp_if"

/* The ogp array contains o, g and p of core A followed by the same for cores
 * B, C and D.
 * In this case, since phase can not be determined, each p will be 0.
 * Overlaod_cnt will contain a count of -128 and 127 codes for each core.
 */
typedef struct {
  float offs[4];
  float gains[4];
  float avz;
  float avamp;
  int overload_cnt[4];
} og_rtn;

/* Things for the adc monitor thread */

int run_adc_monitor = FALSE;
int run_cmd_monitor = FALSE;
pthread_t cmd_monitor_thread;
pthread_t adc_monitor_thread;
dsm_structure dsm_adc_cal;
int adc_cal_valid = 0;
int adc_cmd_rtn[3];
#define CMD_STATUS adc_cmd_rtn[0]

struct tbs_raw *get_mode_pointer(struct katcp_dispatch *d){
  struct tbs_raw *tr;

  /* Grab the mode pointer and check that the FPGA is programmed */
  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    CMD_STATUS = RAW_MODE;
    return NULL;
  }

  /* Make sure we're programmed */
  if(tr->r_fpga != TBS_FPGA_MAPPED){
    CMD_STATUS = FPGA_PGM;
    return NULL;
  }
  return(tr);
}

int set_spi_pointer(struct tbs_raw *tr, int zdok) {
  struct tbs_entry *spi_reg;

  spi_reg = find_data_avltree(tr->r_registers, ADC5G_CONTROLLER);
  if(spi_reg == NULL) {
    CMD_STATUS = SPI_REG;
    return 0;
  }
  spi_p = ((uint32_t *)(tr->r_map + spi_reg->e_pos_base)) + 1 + zdok;
  return 1;
}

/* the following spi subroutines assume that spi_p is set up.  It also
 * assumes that it is running on a big-endian (network byte order) machine
 * like the ppc on a roach2*/

/* return the 16 bit value which was read or -1 if an error */
int get_spi_register(int reg_addr) {
  unsigned char spi_val[4];

  /* request the register */
  spi_val[0] = 0;
  spi_val[1] = 0;
  spi_val[2] = (unsigned char)reg_addr;
  spi_val[3] = 1;
  *spi_p = *(uint32_t *)spi_val;
  /* delay so that the request can complete */
  usleep(1000);
  /* read the return */
  *(uint32_t *)spi_val = *spi_p;
  if(spi_val[2] == (unsigned char)reg_addr) {
    return *(uint16_t *)spi_val;
  } else {
    return(-1);
  }
}

void set_spi_register(int reg_addr, int reg_val) {
  unsigned char spi_val[4];

  *(uint16_t *)spi_val = (uint16_t)reg_val;
  spi_val[2] = (unsigned char)(reg_addr) | 0x80;
  spi_val[3] = 1;
  *spi_p = *(uint32_t *)spi_val;
  /* delay so that this spi command will finish before another can start */
  usleep(1000);
}

float get_offset_register(int chan) {
  int rtn;

  set_spi_register(CHANSEL_REG_ADDR, chan);
  rtn = get_spi_register(EXTOFFS_REG_ADDR);
  return((rtn - 0x80)*(100./255.));
}

int set_offset_register(int chan, float offset) {
  int reg_val;

  reg_val = (int)(((offset < 0)? -0.5: 0.5) + offset*(255./100.)) + 0x80;
  set_spi_register(CHANSEL_REG_ADDR, chan);
  set_spi_register(EXTOFFS_REG_ADDR, reg_val);
  set_spi_register(CALCTRL_REG_ADDR, 2<<2);
  return reg_val;
}

float get_gain_register(int chan) {
  int rtn;

  set_spi_register(CHANSEL_REG_ADDR, chan);
  rtn = get_spi_register(EXTGAIN_REG_ADDR);
  return((rtn - 0x80)*(36./255.));
}

int set_gain_register(int chan, float gain) {
  int reg_val;

  reg_val = (int)(((gain < 0)? -0.5: 0.5) + gain*(255./36.)) + 0x80;
  set_spi_register(CHANSEL_REG_ADDR, chan);
  set_spi_register(EXTGAIN_REG_ADDR, reg_val);
  set_spi_register(CALCTRL_REG_ADDR, 2<<4);
  return reg_val;
}

float get_phase_register(int chan) {
  int rtn;

  set_spi_register(CHANSEL_REG_ADDR, chan);
  rtn = get_spi_register(EXTPHAS_REG_ADDR);
  return((rtn - 0x80)*(28./255.));
}

int set_phase_register(int chan, float phase) {
  int reg_val;

  reg_val = (int)(((phase < 0)? -0.5: 0.5) + phase*(255./28.)) + 0x80;
  set_spi_register(CHANSEL_REG_ADDR, chan);
  set_spi_register(EXTPHAS_REG_ADDR, reg_val);
  set_spi_register(CALCTRL_REG_ADDR, 2<<6);
  return reg_val;
}

void get_ogp_registers(float *offs, float *gains, float *phases) {
  int chan;

  for(chan = 1; chan < 5; chan++) {
    offs[chan - 1] = get_offset_register(chan);
    gains[chan - 1] = get_gain_register(chan);
    phases[chan - 1] = get_phase_register(chan);
  }
}

void set_ogp_registers(float *offs, float *gains, float *phases) {
  int chan;

  for(chan = 1; chan < 5; chan++) {
    set_offset_register(chan, offs[chan - 1]);
    set_gain_register(chan, gains[chan - 1]);
    set_phase_register(chan, phases[chan - 1]);
  }
}

int read_adc_calibrations(void) {
   int i;
  if(!adc_cal_valid) {
    if((i = dsm_read("obscon", "SWARM_SAMPLER_CALIBRATIONS_X",
        &dsm_adc_cal, NULL)) != DSM_SUCCESS){
      CMD_STATUS = DSM_READ;
      adc_cmd_rtn[2] = i;
      return FAIL;
    }
    adc_cal_valid = TRUE;
  }
  return(OK);
}

int write_adc_calibrations(void) {
  if(!adc_cal_valid) {
    CMD_STATUS = DSM_WRITE;
    return FAIL;
  }
  if(dsm_write("SWARM_MONITOR_AND_HAL", "SWARM_SAMPLER_CALIBRATIONS_X",
      &dsm_adc_cal) != DSM_SUCCESS){
    CMD_STATUS = DSM_WRITE;
    return FAIL;
  }
  return OK;
}

int read_ogp_file(struct katcp_dispatch *d, char *fname, float *ogp) {
  FILE *fp;
  int i;

  fp = fopen(fname, "r");
  if(fp == NULL) {
    CMD_STATUS = FILE_OPEN;
    return 0;
  }
  for(i = 0; i < 12; i++) {
    fscanf(fp, "%f", &ogp[i]);
  }
  fclose(fp);
  return 1;
}

int write_ogp_file(struct katcp_dispatch *d, char *fname, float *ogp) {
  FILE *fp;
  int i;

  umask(0000);
  fp = fopen(fname, "w");
  if(fp == NULL) {
    CMD_STATUS = FILE_OPEN;
    return 0;
  }
  for(i = 0; i < 12; i++) {
    fprintf(fp, "%f\n", ogp[i]);
  }
  fclose(fp);
  return 1;
}

void print_ogp(struct katcp_dispatch *d, float *ogp) {
  int chan;

  for(chan = 0; chan < 4; chan++) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL,
      "%.4f %.4f %.4f", ogp[3*chan], ogp[3*chan+1], ogp[3*chan+2]);
  }
}

/* Set up pointers to the registers for taking and accesing a snapshot for
 * both zdoks. */
int set_snapshot_pointers(struct tbs_raw *tr) {
  struct tbs_entry *ctrl_reg, *data_reg, *status_reg;

  /* Get the register pointers */
  ctrl_reg = find_data_avltree(tr->r_registers, SCOPE_0_CTRL);
  status_reg = find_data_avltree(tr->r_registers, SCOPE_0_STATUS);
  data_reg = find_data_avltree(tr->r_registers, SCOPE_0_DATA);
  if(ctrl_reg == NULL || status_reg == NULL || data_reg == NULL
      ){
    CMD_STATUS = SNAP_REG;
    return 0;
  }
  snap_p[0] = ((signed char *)(tr->r_map + data_reg->e_pos_base));
  ctrl_p[0] = ((int *)(tr->r_map + ctrl_reg->e_pos_base));
  stat_p[0] = ((int *)(tr->r_map + status_reg->e_pos_base));
    ctrl_reg = find_data_avltree(tr->r_registers, SCOPE_1_CTRL);
    status_reg = find_data_avltree(tr->r_registers, SCOPE_1_STATUS);
    data_reg = find_data_avltree(tr->r_registers, SCOPE_1_DATA);
  if(ctrl_reg == NULL || status_reg == NULL || data_reg == NULL
      ){
    CMD_STATUS = SNAP_REG;
    return 0;
  }
  snap_p[1] = ((signed char *)(tr->r_map + data_reg->e_pos_base));
  ctrl_p[1] = ((int *)(tr->r_map + ctrl_reg->e_pos_base));
  stat_p[1] = ((int *)(tr->r_map + status_reg->e_pos_base));
  return 1;
}

int take_snapshot(int zdok) {
  int cnt;

  /* trigger a snapshot capture */
  *ctrl_p[zdok] = 2;
  *ctrl_p[zdok] = 3;
  for(cnt = 0; *stat_p[zdok] & 0x80000000; cnt++) {
    if(cnt > 100) {
      CMD_STATUS = SNAP_TO;
      return 0;
    }
  }
  return *stat_p[zdok];	/* return length of snapshot */
}

/* Cores are read out in the sequence A, C, B, D , but should be in the natural
 * order in the ogp array. */
int startpos[] = {0, 2, 1, 3};
og_rtn og_from_noise(int len, signed char *snap) {
  og_rtn rtn;
  float avg, amp;
  int sum, i, cnt, core, code, start_i;

  memset((char *)&rtn, 0, sizeof(rtn));

  for(core = 0; core < 4; core++) {
    start_i = startpos[core];
    cnt = 0;
    sum = 0;
    amp = 0;
    for(i=start_i; i < len; i+= 4) {
      cnt++;
      code = snap[i];
      sum += code;
      if(code == -128 || code == 127) {
        rtn.overload_cnt[core]++;
      }
    }
    avg = (float)sum / cnt;
    for(i=start_i; i < len; i+= 4) {
      amp += fabs((float)snap[i] - avg);
    }
    avg *= (-500.0/256);
    rtn.avz += avg;
    rtn.avamp += amp/cnt;
    rtn.offs[core] = avg;
    rtn.gains[core] = amp/cnt;
  }
  rtn.avz /= 4;
  rtn.avamp /= 4;
  for(core = 0; core < 4; core++) {
    rtn.gains[core] = 100 * (rtn.avamp - rtn.gains[core])/rtn.avamp;
  }
  return(rtn);
}

/* Routine to take an adc snapshot once a second and keep
 * SWARM_LOADING_FACTOR_V2_F and SWARM_SAMPLER_HIST_V256_V2_L up to date */
void *monitor_adc(void *tr) {
  int status, len;
  int hist_cnt, n, val, sum, ssq;
  int zdok;
  int hist[2][256];
  double avg;
  float loading_factor[2];
#define NHIST 60

  bzero(&hist, sizeof(hist));
  hist_cnt = 0;
  while(run_adc_monitor == TRUE) {
    for(zdok = 0; zdok < 2; zdok++) {
      sum = 0;
      ssq = 0;
      pthread_mutex_lock(&snap_mutex);
      len = take_snapshot(zdok);
      pthread_mutex_unlock(&snap_mutex);
      for(n = 0; n < len; n++) {
        val = snap_p[zdok][n];
        sum += val;
        ssq += val*val;
        hist[zdok][val+128]++;
      }
      pthread_mutex_unlock(&snap_mutex);
      avg = (double)sum/len;
      loading_factor[zdok] =
           -20*log10f((float)(128/sqrt((double)ssq/len - avg*avg)));
    }
    status = dsm_write("SWARM_MONITOR", "SWARM_LOADING_FACTOR_V2_F",
        loading_factor);
    if(++hist_cnt >= NHIST) {
      status |= dsm_write("obscon", "SWARM_SAMPLER_HIST_V2_V256_L", hist);
      hist_cnt = 0;
      bzero(&hist, sizeof(hist));
    }
    if (status != DSM_SUCCESS) {
      CMD_STATUS = STRUCT_GET_ELEMENT;
    }
    sleep(1);
  }
  return tr;
}

int get_snapshot_cmd(int zdok){
  int i = 0;
  int len;
  FILE *fp;

  umask(0000);
  if((fp = fopen(SNAP_NAME, "w")) == NULL){
    CMD_STATUS = FILE_OPEN;
    return KATCP_RESULT_FAIL;
  }
  pthread_mutex_lock(&snap_mutex);
  len = take_snapshot(zdok);
  if(len == 0) {
    CMD_STATUS = SNAP_FAIL;
    fclose(fp);
    return KATCP_RESULT_FAIL;
  }
  for(i = 0; i < len; i++) {
    fprintf(fp, "%d\n", snap_p[zdok][i]);
  }
  pthread_mutex_unlock(&snap_mutex);
  fclose(fp);
  return KATCP_RESULT_OK;
}

int measure_og_cmd(int zdok, int rpt_arg){
  int rpt = 100;
  int i, n;
  int len;
  float offs[2][4], gains[2][4], oflow[2][4];
  float avz[2], avamp[2];
  og_rtn single_og;

  if(rpt_arg != 0) {
    rpt = rpt_arg;
    if(rpt <= 0 || rpt > 2000) {
      CMD_STATUS = BAD_RPT;
      return FAIL;
    }
  }
  i = read_adc_calibrations();
  if(i != OK) return(i);
  dsm_structure_get_element(&dsm_adc_cal, DEL_OFFS, offs); 
  dsm_structure_get_element(&dsm_adc_cal, DEL_GAINS, gains); 
  dsm_structure_get_element(&dsm_adc_cal, DEL_OVL_CNT , oflow); 
  dsm_structure_get_element(&dsm_adc_cal, DEL_AVZ, avz); 
  dsm_structure_get_element(&dsm_adc_cal, DEL_AVAMP, avamp); 

  /* Clear the values for the zdok we will measure */
  for(i = 0; i < 4; i++) {
    offs[zdok][i] = 0;
    gains[zdok][i] = 0;
    oflow[zdok][i] = 0;
  }
  avz[zdok] = 0;
  avamp[zdok] = 0;
  for(n = rpt; n > 0; n--) {
    pthread_mutex_lock(&snap_mutex);
    len = take_snapshot(zdok);
    if(len == 0) {
      CMD_STATUS = SNAP_FAIL;
      return FAIL;
    }
    single_og = og_from_noise(len, snap_p[zdok]);
    pthread_mutex_unlock(&snap_mutex);
    avz[zdok] += single_og.avz;
    avamp[zdok] += single_og.avamp;
    for(i = 0; i < 4; i++) {
      offs[zdok][i] += single_og.offs[i];
      gains[zdok][i] += single_og.gains[i];
      oflow[zdok][i] += single_og.overload_cnt[i];
    }
  }
  for(i = 0; i < 4; i++) {
    offs[zdok][i] /= rpt;
    gains[zdok][i] /= rpt;
  }
  avz[zdok] /= rpt;
  avamp[zdok] /= rpt;
  dsm_structure_set_element(&dsm_adc_cal, DEL_OFFS, offs); 
  dsm_structure_set_element(&dsm_adc_cal, DEL_GAINS, gains); 
  dsm_structure_set_element(&dsm_adc_cal, DEL_OVL_CNT , oflow); 
  dsm_structure_set_element(&dsm_adc_cal, DEL_AVZ, avz); 
  dsm_structure_set_element(&dsm_adc_cal, DEL_AVAMP, avamp); 
  return(write_adc_calibrations());
}

#define DEBUG_CHECK_OGP_REGISTERS 0
int check_ogp_registers(float *expect_offs, float *expect_gains,
        float *expect_phases) {
  float rb_offs[4], rb_gains[4], rb_phases[4];
  int core, diff, errCnt;
#if DEBUG_CHECK_OGP_REGISTERS
FILE *fp;

umask(0000);
fp = fopen(SNAP_NAME, "w");
#endif /* DEBUG_CHECK_OGP_REGISTERS */
  errCnt = 0;
  get_ogp_registers(rb_offs, rb_gains, rb_phases);
  for(core = 0; core < 4; core++) {
    diff = (int)(fabs(rb_offs[core] - expect_offs[core])
       *255./100.1 + 0.5);
#if DEBUG_CHECK_OGP_REGISTERS
if(diff) fprintf(fp, "Off core %d %.4f %.4f %d\n",
core, rb_offs[core], expect_offs[core], diff);
#endif /* DEBUG_CHECK_OGP_REGISTERS */
    errCnt += diff;
    diff = (int)(fabs(rb_gains[core] - expect_gains[core])
       *255./36.03 + 0.5);
#if DEBUG_CHECK_OGP_REGISTERS
if(diff) fprintf(fp, "Gains core %d %.4f %.4f %d\n",
core, rb_gains[core], expect_gains[core], diff);
#endif /* DEBUG_CHECK_OGP_REGISTERS */
    errCnt += diff;
    diff = (int)(fabs(rb_phases[core] - expect_phases[core])
       *255./28.03 + 0.5);
#if DEBUG_CHECK_OGP_REGISTERS
if(diff) fprintf(fp, "Phases core %d %.4f %.4f %d\n",
core, rb_phases[core], expect_phases[core], diff);
#endif /* DEBUG_CHECK_OGP_REGISTERS */
    errCnt += diff;
  }
#if DEBUG_CHECK_OGP_REGISTERS
fclose(fp);
#endif /* DEBUG_CHECK_OGP_REGISTERS */
  return(-errCnt);
}

int set_ogp_cmd(int zdok){
  float offs[2][4], gains[2][4], phases[2][4];

  read_adc_calibrations();
  dsm_structure_get_element(&dsm_adc_cal, SV_OFFS, offs); 
  dsm_structure_get_element(&dsm_adc_cal, SV_GAINS, gains); 
  dsm_structure_get_element(&dsm_adc_cal, SV_PHASES, phases); 
  set_ogp_registers(offs[zdok], gains[zdok], phases[zdok]);
  return(check_ogp_registers(offs[zdok],gains[zdok],phases[zdok]));
}

int get_ogp_cmd(int zdok){
  float offs[2][4], gains[2][4], phases[2][4];
  int i;

  i = read_adc_calibrations();
  if(i != OK) return(i);
#if EXPECT_READBACK_ERRORS  
  dsm_structure_get_element(&dsm_adc_cal, A_OFFS, offs); 
  dsm_structure_get_element(&dsm_adc_cal, A_GAINS, gains); 
  dsm_structure_get_element(&dsm_adc_cal, A_PHASES, phases); 
  get_ogp_registers(offs[zdok], gains[zdok], phases[zdok]);
  dsm_structure_set_element(&dsm_adc_cal, A_OFFS, offs); 
  dsm_structure_set_element(&dsm_adc_cal, A_GAINS, gains); 
  dsm_structure_set_element(&dsm_adc_cal, A_PHASES, phases); 
#else
  dsm_structure_get_element(&dsm_adc_cal, SV_OFFS, offs); 
  dsm_structure_get_element(&dsm_adc_cal, SV_GAINS, gains); 
  dsm_structure_get_element(&dsm_adc_cal, SV_PHASES, phases); 
  get_ogp_registers(offs[zdok], gains[zdok], phases[zdok]);
  dsm_structure_set_element(&dsm_adc_cal, SV_OFFS, offs); 
  dsm_structure_set_element(&dsm_adc_cal, SV_GAINS, gains); 
  dsm_structure_set_element(&dsm_adc_cal, SV_PHASES, phases); 
#endif
  return(write_adc_calibrations());
}

int update_ogp_cmd(int zdok){
  int i;
  float del_offs[2][4], del_gains[2][4];
  float cur_offs[2][4], cur_gains[2][4], cur_phases[2][4];

  i = read_adc_calibrations();
  if(i != OK)  return(3);
  dsm_structure_get_element(&dsm_adc_cal, DEL_OFFS, del_offs); 
  dsm_structure_get_element(&dsm_adc_cal, DEL_GAINS, del_gains); 
#if EXPECT_READBACK_ERRORS 
  dsm_structure_get_element(&dsm_adc_cal, SV_OFFS, cur_offs); 
  dsm_structure_get_element(&dsm_adc_cal, SV_GAINS, cur_gains); 
  dsm_structure_get_element(&dsm_adc_cal, SV_GAINS, cur_phases); 
  for(i = 0; i < 4; i++) {
    cur_offs[zdok][i] = (100./255.)*round((255./100.)*cur_offs[zdok][i]);
    cur_gains[zdok][i+1] = (36./255.)*round((255./36.)*cur_gains[zdok][i+1]);
  }
#else
  get_ogp_registers(cur_offs[zdok], cur_gains[zdok], cur_phases[zdok]);
#endif
  for(i = 0; i < 4; i++) {
    cur_offs[zdok][i] += del_offs[zdok][i];
    cur_gains[zdok][i] += del_gains[zdok][i];
  }
  bzero(cur_phases, sizeof(cur_phases));
  set_ogp_registers(cur_offs[zdok], cur_gains[zdok], cur_phases[zdok]);
  dsm_structure_set_element(&dsm_adc_cal, SV_OFFS, cur_offs); 
  dsm_structure_set_element(&dsm_adc_cal, SV_GAINS, cur_gains); 
  dsm_structure_set_element(&dsm_adc_cal, SV_PHASES, cur_phases); 
  i = write_adc_calibrations();
  if(i != OK)  return(5);
  return(check_ogp_registers(cur_offs[zdok],cur_gains[zdok],cur_phases[zdok]));
}

int clear_ogp_cmd(int zdok) {
  int i;
  float offs[2][4], gains[2][4], phases[2][4];

  i = read_adc_calibrations();
  if(i != OK)  return(3);
  dsm_structure_get_element(&dsm_adc_cal, SV_OFFS, offs); 
  bzero(offs[zdok], 4*sizeof(float));
  dsm_structure_set_element(&dsm_adc_cal, SV_OFFS, offs); 
  dsm_structure_get_element(&dsm_adc_cal, SV_GAINS, gains); 
  bzero(gains[zdok], 4*sizeof(float));
  dsm_structure_set_element(&dsm_adc_cal, SV_GAINS, gains); 
  dsm_structure_get_element(&dsm_adc_cal, SV_PHASES, phases); 
  bzero(phases[zdok], 4*sizeof(float));
  dsm_structure_set_element(&dsm_adc_cal, SV_PHASES, phases); 
  set_ogp_registers(offs[zdok], gains[zdok], phases[zdok]);
  return(get_ogp_cmd(zdok));
}

void start_adc_monitor_cmd(struct tbs_raw *tr){
  int status;
  /* Set flag run adc monitoring of input level and histogram */
  run_adc_monitor = TRUE;

  /* Start the thread */
  status = pthread_create(&adc_monitor_thread,NULL, monitor_adc, (void *)tr);
  if (status < 0){
    CMD_STATUS = MONITOR_THREAD;
    run_adc_monitor = FALSE;
  }
}

/* int stop_adc_monitor_cmd(struct katcp_dispatch *d, int argc){ */
void stop_adc_monitor_cmd(void){

  /* Set flag to stop adc_monitor measuring and reporting */
  run_adc_monitor = FALSE;

  /* Wait until the thread stops */
  if(pthread_join(adc_monitor_thread, NULL)) {
    CMD_STATUS = MONITOR_NOT_JOINED;
  } else {
    CMD_STATUS = OK;
  }
}

/* SIGINT handler stops waiting */
void sigint_handler(int sig){
  run_cmd_monitor = FALSE;
}

/* cmd_thread routine to monitor SWARM_ADC_CMD_L */
void *cmd_monitor(void *tr) {
  char hostName[DSM_NAME_LENGTH];
  char varName[DSM_NAME_LENGTH];
  int cmd[3];
  int zdok, zdok_cmd;
  int rtn, errCnt;
  static int zdokStart[3] = {0, 1, 0};
  static int zdokLimit[3] = {1, 2, 2};

  /* Set our INT signal handler so dsm_read_wait can be interrupted */
  signal(SIGINT, sigint_handler);

  while(run_cmd_monitor == TRUE) {
    bzero(adc_cmd_rtn, sizeof(adc_cmd_rtn));
    errCnt = 0;
    rtn = dsm_read_wait(hostName, varName, cmd);
    if(rtn == DSM_INTERRUPTED)
      return (void *)0;
    if(cmd[0] < START_ADC_MONITOR) {
      zdok_cmd = cmd[1];
      if(zdok_cmd < 0 || zdok_cmd > 2){
        CMD_STATUS = BAD_ZDOK;
        return (void *)0;
      }
      for(zdok = zdokStart[zdok_cmd]; zdok < zdokLimit[zdok_cmd]; zdok++) {
        set_spi_pointer((struct tbs_raw *)tr, zdok);
        switch(cmd[0]) {
	case TAKE_SNAPSHOT:
	  get_snapshot_cmd(zdok);
	  break;
	case SET_OGP:
	  rtn = set_ogp_cmd(zdok);
	  break;
	case GET_OGP:
	  rtn = get_ogp_cmd(zdok);
	  break;
	case MEASURE_OG:
          rtn = measure_og_cmd(zdok, 0);
	  break;
	case UPDATE_OGP:
	  rtn = update_ogp_cmd(zdok);
	  break;
	case CLEAR_OGP:
	  rtn = clear_ogp_cmd(zdok);
	  break;
        default:
          CMD_STATUS = UNK;
	}
	if(rtn != OK) {
	  if(rtn > 0) {
	    break;
	  } else {
	    CMD_STATUS = OGP_READBACK;
	    adc_cmd_rtn[2] = -rtn;
	  }
	}
      }
    } else {
      switch(cmd[0]) {
      case START_ADC_MONITOR:
        start_adc_monitor_cmd((struct tbs_raw *)tr);
        break;
      case STOP_ADC_MONITOR:
        stop_adc_monitor_cmd();
        break;
      default:
        CMD_STATUS = UNK;
      }
    }
    usleep(40000);
    dsm_write_notify("hal9000", "SWARM_ADC_CMD_RETURN_V3_L", &adc_cmd_rtn);
  }
  return (void *)0;
}

int start_cmd_monitor_cmd(struct katcp_dispatch *d, int argc){
  int status;
  static struct tbs_raw *tr;
  /* Grab the mode pointer */
  if((tr = get_mode_pointer(d)) == NULL)
    return(KATCP_RESULT_FAIL);

  set_snapshot_pointers(tr);
  /* Set flag run adc monitoring of input level and histogram */
  run_cmd_monitor = TRUE;
  status = dsm_structure_init(&dsm_adc_cal,"SWARM_SAMPLER_CALIBRATIONS_X" );
  if (status != DSM_SUCCESS) {
    CMD_STATUS = STRUCT_INIT;
    return(KATCP_RESULT_FAIL);
  }
  if(dsm_monitor("hal9000", "SWARM_ADC_CMD_V3_L") != DSM_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL,
                "Error setting up monitoring of dsm command.");
    return KATCP_RESULT_FAIL;
  }
  /* Start the thread */
  status = pthread_create(&cmd_monitor_thread,NULL, cmd_monitor, (void *)tr);
  if (status < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL,
                "could not create adc command monitor thread");
    run_cmd_monitor = FALSE;
    return KATCP_RESULT_FAIL;
  }
  return KATCP_RESULT_OK;
}

int stop_cmd_monitor_cmd(struct katcp_dispatch *d, int argc){

  if(run_adc_monitor == TRUE) {
    stop_adc_monitor_cmd();
  }
  if(run_cmd_monitor == FALSE) {
    return KATCP_RESULT_FAIL;
  }
  /* Set flag to stop cmd_monitor thread */
  pthread_kill(cmd_monitor_thread, SIGINT);

  /* Wait until the thread stops */
  pthread_join(cmd_monitor_thread, NULL);
  dsm_clear_monitor();
  dsm_structure_destroy(&dsm_adc_cal);

  return KATCP_RESULT_OK;
}


struct PLUGIN KATCP_PLUGIN = {
  .n_cmds = 2,
  .name = "sma_adc_dsm",
  .version = KATCP_PLUGIN_VERSION,
  .init = start_cmd_monitor_cmd,
  .uninit = stop_cmd_monitor_cmd,
  .cmd_array = {
#if 0
    {
      .name = "?get-snapshot", 
      .desc = "get a snapshot and store it in /instance/snap.  Optional argument zdok",
      .cmd = get_snapshot_cmd
    },
    {
      .name = "?measure-og", 
      .desc = "Take repeated snapshots and mesasure offset and gain assuming noise input.  Write result in /instance/ogp_meas.  Optional arguments zdoc (0) and rpt(30)",
      .cmd = measure_og_cmd
    },
    {
      .name = "?get-ogp", 
      .desc = "read ogp from the adc and write to /instance/ogp",
      .cmd = get_ogp_cmd
    },
    {
      .name = "?set-ogp", 
      .desc = "read ogp from a file and set the adc ogp registers.  Optional arguments zdok [0] and fname [/instance/ogp]",
      .cmd = set_ogp_cmd
    },
    {
      .name = "?update-ogp", 
      .desc = "read /instance/ogp_meas and add to the ogp registers in the adc.  Also store results in /instance/ogp.  Optional argument zdok",
      .cmd = update_ogp_cmd
    },
#endif
    {
      .name = "?start-cmd-thread", 
      .desc = "start the adc monitor thread",
      .cmd = start_cmd_monitor_cmd
    },
    {
      .name = "?stop-cmd-thread", 
      .desc = "stop the adc monitor thread",
      .cmd = stop_cmd_monitor_cmd
    },
  }
};
