#include <Python.h>

/* Format string for PyArg_ParseTuple */
#define SI_ARG_FORMAT "dfiiiiiOOi"

/* Function prototype for sendIntegration */
int sendIntegration(int nPoints, double uT, float duration, int chunk,
		    int ant1, int pol1, int ant2, int pol2,
		    float *lsbCross, float *usbCross, int forceTransfer);

static PyObject *
send_integration(PyObject *self, PyObject *args)
{
  /* Parameters for sendIntegration */
  int nPoints;
  double uT;
  float duration;
  int chunk;
  int ant1, pol1;
  int ant2, pol2;
  float *lsbCross, *usbCross;
  int forceTransfer;
  int sts; // return value

  /* Variables for parsing the arrays */
  int i, lsbCrossLen, usbCrossLen;
  PyObject *lsbCrossObj, *usbCrossObj;
  PyObject *lsbCrossItem, *usbCrossItem;

  /* Parse the arguments from Python */
  if (!PyArg_ParseTuple(args, SI_ARG_FORMAT, 
			&uT, &duration, &chunk,
			&ant1, &pol1, &ant2, &pol2,
			&lsbCrossObj, &usbCrossObj, &forceTransfer))
    return NULL;

  /* Make sure lsbCrossObj is a sequence */
  if (!PySequence_Check(lsbCrossObj)) {
    PyErr_SetString(PyExc_TypeError, "lsbCross argument must be a sequence!");
    return NULL;
  }

  /* Make sure usbCrossObj is a sequence */
  if (!PySequence_Check(usbCrossObj)) {
    PyErr_SetString(PyExc_TypeError, "usbCross argument must be a sequence!");
    return NULL;
  }

  /* Get lengths of lsb and usb sequences 
   and make sure they're the same length */
  lsbCrossLen = PySequence_Size(lsbCrossObj);
  usbCrossLen = PySequence_Size(usbCrossObj);
  if (lsbCrossLen != usbCrossLen) {
    PyErr_SetString(PyExc_TypeError, "lsbCross and usbCross must be the same length!");
    return NULL;
  } 

  /* Make sure sequence lengths are divisible by 2 */
  if (lsbCrossLen % 2 != 0) {
    PyErr_SetString(PyExc_TypeError, "lsbCross/usbCross length must be divisible by 2!");
    return NULL;
  }
   
  /* Finally set nPoints */
  nPoints = lsbCrossLen/2;

  /* Malloc the arrays we need to send */
  lsbCross = (float *) malloc(sizeof(float) * nPoints * 2);
  usbCross = (float *) malloc(sizeof(float) * nPoints * 2);
  if ((lsbCross == NULL) || (usbCross == NULL)) {
    PyErr_SetString(PyExc_TypeError, "Problem malloc array data!");
    return NULL;
  }

  /* Now populate the lsbCross data */
  for (i=0; i<nPoints*2; i++) {

    /* Get the item from the array */
    lsbCrossItem = PySequence_GetItem(lsbCrossObj, i);
    if (!PyFloat_Check(lsbCrossItem)) {
      PyErr_SetString(PyExc_TypeError, "Array items must be floats!");
      Py_DECREF(lsbCrossItem); // clean-up
      return NULL;
    }

    /* Convert to a C float */
    lsbCross[i] = (float) PyFloat_AsDouble(lsbCrossItem);
    if (PyErr_Occurred()) {
      PyErr_SetString(PyExc_TypeError, "Error occurred converting to C float!");
      Py_DECREF(lsbCrossItem); // clean-up
      return NULL;
    }

    Py_DECREF(lsbCrossItem); // final clean-up

  }
 
  /* Now populate the usbCross data */
  for (i=0; i<nPoints*2; i++) {

    /* Get the item from the array */
    usbCrossItem = PySequence_GetItem(usbCrossObj, i);
    if (!PyFloat_Check(usbCrossItem)) {
      PyErr_SetString(PyExc_TypeError, "Array items must be floats!");
      Py_DECREF(usbCrossItem); // clean-up
      return NULL;
    }

    /* Convert to a C float */
    usbCross[i] = (float) PyFloat_AsDouble(usbCrossItem);
    if (PyErr_Occurred()) {
      PyErr_SetString(PyExc_TypeError, "Error occurred converting to C float!");
      Py_DECREF(usbCrossItem); // clean-up
      return NULL;
    }

    Py_DECREF(usbCrossItem); // final clean-up

  }

  sts = sendIntegration(nPoints, uT, duration, chunk,
			ant1, pol1, ant2, pol2,
			lsbCross, usbCross, forceTransfer);

  /* Free the malloced memory */
  free(lsbCross);
  free(usbCross);

  return Py_BuildValue("i", sts);
}


static PyMethodDef SendIntMethods[] = {

  {"send_integration", send_integration, METH_VARARGS,
   "Send an integration."},
  {NULL, NULL, 0, NULL}
};


PyMODINIT_FUNC
initpysendint(void)
{
  (void) Py_InitModule("pysendint", SendIntMethods);
}
