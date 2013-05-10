#include <stdlib.h>
#include <katcp.h>
#include <plugin.h>
#include <time.h>
#include <dsm.h>

int open_cmd(struct katcp_dispatch *d, int argc){
  int s;

  /* Open DSM for access */
  s = dsm_open();
  if(s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_open()");
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dsm_open(): failed with %d", s);
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int close_cmd(struct katcp_dispatch *d, int argc){
  int s;

  /* Close up the DSM shop */
  s = dsm_close();
  if(s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_close()");
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dsm_close(): failed with %d", s);
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int read_long_cmd(struct katcp_dispatch *d, int argc){
  int s;
  long value;
  time_t timestamp;
  char *host, *var;

  /* Grab the first argument, the host from which to read */
  host = arg_string_katcp(d, 1);
  if (host == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse first command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Grab the second argument, the variable name requested */
  var = arg_string_katcp(d, 2);
  if (var == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse second command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Grab the value over DSM from host */
  s = dsm_read(host, var, &value, &timestamp);
  if(s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_read()");
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dsm_read('%s', '%s'): failed with %d", host, var, s);
    return KATCP_RESULT_FAIL;
  }

  /* And relay it back to the client */
  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
  append_signed_long_katcp(d, KATCP_FLAG_SLONG | KATCP_FLAG_LAST, value);
  return KATCP_RESULT_OWN;
}

int write_long_cmd(struct katcp_dispatch *d, int argc){
  int s;
  long value;
  char *host, *var;

  /* Grab the first argument, the host/class to write to */
  host = arg_string_katcp(d, 1);
  if (host == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse first command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Grab the second argument, the variable name requested */
  var = arg_string_katcp(d, 2);
  if (var == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse second command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Grab the third argument, the value to write */
  value = arg_signed_long_katcp(d, 3);
  if (var == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse third command line argument");
    return KATCP_RESULT_FAIL;
  }

  /* Write the value to host over DSM */
  s = dsm_write(host, var, &value);
  if(s != DSM_SUCCESS) {
    dsm_error_message(s, "dsm_write()");
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dsm_write('%s', '%s', %d): failed with %d", host, var, value, s);
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

struct PLUGIN KATCP_PLUGIN = {
  .n_cmds = 4,
  .name = "dsm",
  .version = KATCP_PLUGIN_VERSION,
  .cmd_array = {
    {
      .name = "?dsm-open", 
      .desc = "begin DSM operations",
      .cmd = open_cmd
    },
    {
      .name = "?dsm-close", 
      .desc = "close DSM operations",
      .cmd = close_cmd
    },
    {
      .name = "?dsm-long-read", 
      .desc = "read a long value over DSM from host (?dsm-long-read host name)",
      .cmd = read_long_cmd
    },
    {
      .name = "?dsm-long-write", 
      .desc = "write a long value over DSM to host/class (?dsm-long-read {host|class} name value)",
      .cmd = write_long_cmd
    },
  }
};
