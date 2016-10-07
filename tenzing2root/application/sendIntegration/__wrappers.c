#include <stdarg.h>
#include <Python.h>

/***********************************************************/
/* define logging function and logtypes for python.logging */
/* by H.Dickten 2014                                       */
/***********************************************************/
enum logtypes {info, warning, error, debug};

static void log_msg(int type, char *msg)
{
  static PyObject *logging = NULL;
  static PyObject *logger = NULL;
  static PyObject *name = NULL;
  static PyObject *message = NULL;

  // import logging module on demand
  if (logging == NULL){
    logging = PyImport_ImportModuleNoBlock("logging");
    if (logging == NULL)
      PyErr_SetString(PyExc_ImportError,
		      "Could not import module 'logging'");
  }

  // build logger message
  name = Py_BuildValue("s", "pysendint");

  // get the logger
  logger = PyObject_CallMethod(logging, "getLogger", "O", name);

  // build msg-message
  message = Py_BuildValue("s", msg);

  // call function depending on loglevel
  switch (type)
    {
    case info:
      PyObject_CallMethod(logger, "info", "O", message);
      break;

    case warning:
      PyObject_CallMethod(logger, "warn", "O", message);
      break;

    case error:
      PyObject_CallMethod(logger, "error", "O", message);
      break;

    case debug:
      PyObject_CallMethod(logger, "debug", "O", message);
      break;
    }
  Py_DECREF(message);
  Py_DECREF(name);
}

int __wrap_printf(const char *format, ...)
{
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, 256, format, args);
  buffer[strcspn(buffer, "\r\n")] = 0; // strip newlines
  log_msg(debug, buffer);
  va_end(args);
  return 0;
}

int __wrap_fprintf(FILE *fd, const char *format, ...)
{
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, 256, format, args);
  buffer[strcspn(buffer, "\r\n")] = 0; // strip newlines
  log_msg(error, buffer);
  va_end(args);
  return 0;
}

void __wrap_perror(const char *msg)
{
  log_msg(error, strerror(errno));
  return;
}
