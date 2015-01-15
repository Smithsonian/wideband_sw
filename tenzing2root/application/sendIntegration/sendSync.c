#include <stdio.h>
#include <stdlib.h>

#define ERROR (-1)
#define OK    ( 0)

#include "dataCatcher.h"   /* RPC server/client structures */

int sendSync(void)
{
  dStatusStructure *result, argument;
  CLIENT *cl;

  cl = clnt_create("hcn", DATACATCHERPROG, DATACATCHERVERS, "tcp");
  if (!cl) {
    fprintf(stderr, "Could not get a client handle to dataCatcher on HCN\n");
    perror("clnt_create");
    return ERROR;
  }
  argument.rt_code = 1;
  if (!(result = sync_asic_1(&argument, cl))) {
    fprintf(stderr, "NULL returned by sync_asic_1()\n");
    perror("sync_asic_1()");
    clnt_destroy(cl);
    return ERROR;
  }
  clnt_destroy(cl);
  return OK;
}
