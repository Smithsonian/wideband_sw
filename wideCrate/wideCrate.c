#include <time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <rpc/pmap_clnt.h>
#include <rpc/rpc.h>
#include <unistd.h>
#include <string.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include "dDS.h"
#include "integration.h"

/* KATCP libraries */
#include "katcl.h"
#include "katcp.h"

#define OK     (0)
#define ERROR (-1)

#define SERVER_PRIORITY (10)

pthread_t serverTId;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t package_received = 0;

/* Global package shared between threads */
intgWalshPackage *dds_package;

typedef struct {
  char roach2_hostname[64];
  struct timespec sowf_ts;
  int hb_offset;
} send_walsh_sync_args;

typedef struct {
  int errCode;
} send_walsh_sync_resp;

void* send_walsh_sync(void* arg_ptr)
{
  int i, total, katcp_result;
  struct katcl_line *l;
  send_walsh_sync_resp* resp = malloc(sizeof(send_walsh_sync_resp));
  send_walsh_sync_args* args = (send_walsh_sync_args *)arg_ptr;

  /* connect to roach2 */
  l = create_name_rpc_katcl(args->roach2_hostname);
  if(l == NULL){
    fprintf(stderr, "unable to create client connection to %s: %s\n", 
	    args->roach2_hostname, strerror(errno));
    resp->errCode = ERROR;
    return (void *)resp;
  }

  /* send the walsh arm request, with 60s timeout */
  katcp_result = send_rpc_katcl(l, 60000,
  				KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?sma-walsh-arm",
  				KATCP_FLAG_ULONG, args->sowf_ts.tv_sec,
  				KATCP_FLAG_ULONG, args->sowf_ts.tv_nsec,
  				KATCP_FLAG_LAST  | KATCP_FLAG_SLONG, (long)args->hb_offset,
  				NULL);

  /* result is 0 if the reply returns "ok", 1 if it failed and -1 if things went wrong doing IO or otherwise */
  printf("result of ?sma-walsh-arm is %d\n", katcp_result);

  /* you can examine the content of the reply with the following functions */
  total = arg_count_katcl(l);
  printf("have %d arguments in reply\n", total);
  for(i = 0; i < total; i++){
    /* for binary data use the arg_buffer_katcl, string will stop at the first occurrence of a \0 */
    printf("reply[%d] is <%s>\n", i, arg_string_katcl(l, i));
  }

  destroy_rpc_katcl(l);

  resp->errCode = OK;
  return (void *)resp;
}

statusCheck(dDSStatus *status)
{
  if (status->status == DDS_SUCCESS)
    printf("O.K.\n");
  else
    printf("DDS server returned error status, reason = %d\n",
      status->reason);
}

void intgprog_1(struct svc_req *rqstp, register SVCXPRT *transp)
{
  union {
    INTG__test_struct intgconfigure_1_arg;
    intgParameters intgsetparams_1_arg;
    intgCommand intgcommand_1_arg;
    intgDumpFile intgnewfile_1_arg;
    attenuationRequest intgattenadjust_1_arg;
    intgWalshPackage intgsyncwalsh_1_arg;
    intgCommand intgreportconfiguration_1_arg;
    intgC2DCParameters intgc2dcrates_1_arg;
    intgCommand1IP intgcommand1ip_1_arg;
    intgChannelCounts intgsetchannels_1_arg;
  } argument;
  char *result;
  xdrproc_t _xdr_argument, _xdr_result;
  char *(*local)(char *, struct svc_req *);
  
  switch (rqstp->rq_proc) {
  case NULLPROC:
    (void) svc_sendreply (transp, (xdrproc_t) xdr_void, (char *)NULL);
    return;
    
  case INTGSYNCWALSH:
    _xdr_argument = (xdrproc_t) xdr_intgWalshPackage;
    _xdr_result = (xdrproc_t) xdr_intgWalshResponse;
    local = (char *(*)(char *, struct svc_req *)) intgsyncwalsh_1_svc;
    break;
  default:
    svcerr_noproc (transp);
    return;
  }
  memset ((char *)&argument, 0, sizeof (argument));
  if (!svc_getargs (transp, (xdrproc_t) _xdr_argument, (caddr_t) &argument)) {
    svcerr_decode (transp);
    return;
  }
  result = (*local)((char *)&argument, rqstp);
  if (result != NULL && !svc_sendreply(transp, (xdrproc_t) _xdr_result, result)) {
    svcerr_systemerr (transp);
  }
  /* if (!svc_freeargs (transp, (xdrproc_t) _xdr_argument, (caddr_t) &argument)) { */
  /*   fprintf (stderr, "%s", "unable to free arguments"); */
  /*   exit (1); */
  /* } */
  return;
}

intgWalshResponse *result;
intgWalshResponse *intgsyncwalsh_1(intgWalshPackage *package, CLIENT *cl)
{

  result = malloc(sizeof(intgWalshResponse));
  if (result == NULL) {
    perror("malloc of result structure for intgsyncwalsh_1\n");
    exit(ERROR);
  }

  if (!package_received) {
    pthread_mutex_lock(&mutex);
    dds_package = package; // only possible since I disable svc_freeargs
    pthread_mutex_unlock(&mutex);
    package_received = 1;
  }

  result->status = OK;
  return result;
}

int handle_package(intgWalshPackage *package, int hb_offset) {
  send_walsh_sync_args *args1 = malloc(sizeof(send_walsh_sync_args));
  send_walsh_sync_args *args2 = malloc(sizeof(send_walsh_sync_args));
  send_walsh_sync_resp *resp1, *resp2;
  void *respVoid1, *respVoid2;
  pthread_t tid1, tid2;

  int i, nPatterns, step_len;
  struct timespec sowf_ts, armed_at;
  struct tm curr;
  struct tm sowf;
  time_t raw;


  /* Process the Walsh table */
  printf("Received a package from the DDS Server - contents:\n");
  nPatterns = package->pattern.pattern_len;
  printf("Number of Walsh Patterns %d, listed below\n", nPatterns);
  for (i = 1; i < nPatterns; i++) {
    int j;

    step_len = package->pattern.pattern_val[i].step.step_len;
    printf("%2d: %d ", i, step_len);
    for (j = 0; j < step_len; j++)
      printf("%d", package->pattern.pattern_val[i].step.step_val[j]);
    printf("\n");
  }

  /* Print a human-readable version of the agreed upon start of Walsh hearbeat */
  printf("Start at day %d of year %d\n", package->startDay, package->startYear);
  printf("at %02d:%02d:%02d.%06d\n", package->startHour, package->startMin,
  	 package->startSec, package->startuSec);
  
  /* Get a struct tm of current time and replace with SOWF info */
  time(&raw);
  curr = *gmtime(&raw);
  sowf = curr;
  sowf.tm_year = package->startYear - 1900;
  sowf.tm_yday = package->startDay;
  sowf.tm_hour = package->startHour;
  sowf.tm_min  = package->startMin;
  sowf.tm_sec  = package->startSec;
  printf("Current time: %d\n", (int)mktime(&curr));
  printf("SOWF time:    %d\n", (int)mktime(&sowf));
  
  /* Convert it to a timespec */
  sowf_ts.tv_sec = mktime(&sowf);
  sowf_ts.tv_nsec = (package->startuSec) * 1e3;

  /* Send SOWF into to first ROACH */
  args1->sowf_ts = sowf_ts;
  args1->hb_offset = hb_offset;
  strcpy(args1->roach2_hostname, "roach2-01");
  pthread_create(&tid1, NULL, send_walsh_sync, (void *)args1);
  printf("Sent SOWF to roach2-01\n");

  /* Send SOWF into to second ROACH */
  args2->sowf_ts = sowf_ts;
  args2->hb_offset = hb_offset;
  strcpy(args2->roach2_hostname, "roach2-02");
  pthread_create(&tid2, NULL, send_walsh_sync, (void *)args2);
  printf("Sent SOWF to roach2-02\n");

  /* Join all threads, wait for return */
  pthread_join(tid1, respVoid1);
  pthread_join(tid2, respVoid2);
  resp1 = (send_walsh_sync_resp *)respVoid1;
  resp2 = (send_walsh_sync_resp *)respVoid2;

  /* After functions return */
  clock_gettime(CLOCK_REALTIME, &armed_at);
  printf("Presumably armed at: %d.%06d\n", (int)armed_at.tv_sec, (int)armed_at.tv_nsec);

  return resp1->errCode;
}

intgWalshResponse *intgsyncwalsh_1_svc(intgWalshPackage *package,
				       struct svc_req *dummy)
{
  CLIENT *cl = NULL;
  
  result = intgsyncwalsh_1(package, cl);
  return result;
}

void *server(void *arg)
{
  printf("The server thread has started\n");
  register SVCXPRT *transp;
  
  pmap_unset(INTGPROG, INTGVERS);
  
  transp = svcudp_create(RPC_ANYSOCK);
  if (transp == NULL) {
    fprintf(stderr, "%s", "cannot create udp service.\n");
    exit(1);
  }
  if (!svc_register(transp, INTGPROG, INTGVERS, intgprog_1, IPPROTO_UDP)) {
    fprintf(stderr, "%s", "unable to register (INTGPROG, INTGVERS, udp).\n");
    exit(1);
  }
  
  transp = svctcp_create(RPC_ANYSOCK, 0, 0);
  if (transp == NULL) {
    fprintf(stderr, "%s", "cannot create tcp service.\n");
    exit(1);
  }
  if (!svc_register(transp, INTGPROG, INTGVERS, intgprog_1, IPPROTO_TCP)) {
    fprintf(stderr, "%s", "unable to register (INTGPROG, INTGVERS, tcp).\n");
    exit(1);
  }
  
  svc_run();
  fprintf(stderr, "%s", "svc_run returned\n");
  exit(1);
  /* NOTREACHED */
}

int main(int argc, char **argv)
{
  int status;
  int hb_offset = 0;
  char hostName[20];
  dDSCommand command;
  pthread_attr_t attr;
  CLIENT *cl;
  
  /* Create the RPC server thread */
  if (pthread_attr_init(&attr) != OK) {
    perror("pthread_attr_init attr");
    exit(ERROR);
  }
  if ((status = pthread_create(&serverTId, &attr, server,
			       (void *) 12)) != OK) {
    perror("pthread_create for the server thread");
    fprintf(stderr, "pthread_create returned status of %d\n", status);
    exit(ERROR);
  }

  /* Make the client call to the DDS server */
  if (!(cl = clnt_create("mrg", DDSPROG, DDSVERS, "tcp"))) {
    clnt_pcreateerror("Creating handle to server on mrg");
    exit(ERROR);
  }
  command.antenna = DDS_ALL_ANTENNAS;
  command.receiver = DDS_ALL_RECEIVERS;
  command.refFrequency = 9.0e6;
  command.command = DDS_START_WALSH;
  gethostname(command.client, 20);
  command.client[19] = (char)0;
  printf("This client's name is \"%s\".\n", command.client);
  statusCheck(ddsrequest_1(&command, cl));

  while (!package_received) {
    usleep(1);
  }

  if (argc == 2) {
    hb_offset = atoi(argv[1]);
  }

  handle_package(dds_package, hb_offset);

  //pthread_join(serverTId, NULL);
}
