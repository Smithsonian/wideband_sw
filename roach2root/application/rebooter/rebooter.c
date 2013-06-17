#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <dirent.h>

void signalHandler(int signum)
{
  if ((signum == SIGURG) || (signum == SIGSTKFLT)) {
    int errCode;

    fprintf(stderr, "rebooter received signal %d - will try to force a reboot\n", signum);
    errCode = execl("/bin/bash", "bash", "/common/bin/shutdown", (char *)NULL);
    perror("execl");
    fprintf(stderr, "execl returned %d\n", errCode);
  } else
    fprintf(stderr, "rebooter signalhandler received an unexpected signum = %d - will ignore\n",
            signum);
}

main()
{
  struct sigaction action, oldAction;
  DIR *dummy;

  action.sa_flags = 0;
  sigemptyset(&action.sa_mask);
  action.sa_handler = signalHandler;
  sigaction(SIGURG, &action, &oldAction);
  sigaction(SIGSTKFLT, &action, &oldAction);
  while (1) {
    dummy = opendir("/instance/smainit_req/");
    if (dummy != NULL)
      closedir(dummy);
    sleep(1);
  }
}
