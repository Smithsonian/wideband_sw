#include <stdio.h>
#include <stdlib.h>
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

#define OK     (0)
#define ERROR (-1)

#define SERVER_PRIORITY (10)

pthread_t serverTId;

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
  if (!svc_freeargs (transp, (xdrproc_t) _xdr_argument, (caddr_t) &argument)) {
    fprintf (stderr, "%s", "unable to free arguments");
    exit (1);
  }
  return;
}

intgWalshResponse *result;
intgWalshResponse *intgsyncwalsh_1(intgWalshPackage *package, CLIENT *cl)
{
  static int firstCall = TRUE;
  int i, nPatterns;

  if (firstCall) {
    result = malloc(sizeof(intgWalshResponse));
    if (result == NULL) {
      perror("malloc of result structure for intgsyncwalsh_1\n");
      exit(ERROR);
    }
    firstCall = FALSE;
  }
  printf("Received a package from the DDS Server - contents:\n");
  nPatterns = package->pattern.pattern_len;
  printf("Number of Walsh Patterns %d, listed below\n", nPatterns);
  for (i = 0; i < nPatterns; i++) {
    int j;

    printf("%2d:  ", i);
    for (j = 0; j < package->pattern.pattern_val[i].step.step_len; j++)
      printf("%d", package->pattern.pattern_val[i].step.step_val[j]);
    printf("\n");
  }
  printf("Start at day %d of year %d\n", package->startDay, package->startYear);
  printf("at %02d:%02d:%02d.%06d\n", package->startHour, package->startMin,
	 package->startSec, package->startuSec);
  result->status = OK;
  return(result);
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
    fprintf(stderr, "%s", "cannot create udp service.");
    exit(1);
  }
  if (!svc_register(transp, INTGPROG, INTGVERS, intgprog_1, IPPROTO_UDP)) {
    fprintf(stderr, "%s", "unable to register (INTGPROG, INTGVERS, udp).");
    exit(1);
  }
  
  transp = svctcp_create(RPC_ANYSOCK, 0, 0);
  if (transp == NULL) {
    fprintf(stderr, "%s", "cannot create tcp service.");
    exit(1);
  }
  if (!svc_register(transp, INTGPROG, INTGVERS, intgprog_1, IPPROTO_TCP)) {
    fprintf(stderr, "%s", "unable to register (INTGPROG, INTGVERS, tcp).");
    exit(1);
  }
  
  svc_run();
  fprintf(stderr, "%s", "svc_run returned");
  exit(1);
  /* NOTREACHED */
}

main()
{
  int status;
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
  pthread_join(serverTId, NULL);
}
