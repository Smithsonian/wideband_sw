#include <stdlib.h>
#include <katcp.h>
#include <plugin.h>

int example_info_cmd(struct katcp_dispatch *d, int argc){
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "hello world!");
  return KATCP_RESULT_OK;
}

int example_error_cmd(struct katcp_dispatch *d, int argc){
  log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "hello world!");
  return KATCP_RESULT_OK;
}

struct PLUGIN KATCP_PLUGIN = {
  .n_cmds = 2,
  .name = "example",
  .version = KATCP_PLUGIN_VERSION,
  .cmd_array = {
    {
      .name = "?example-info",
      .desc = "say hello over log info",
      .cmd = example_info_cmd
    },
    {
      .name = "?example-error",
      .desc = "say hello over log error",
      .cmd = example_error_cmd
    },
  }
};
