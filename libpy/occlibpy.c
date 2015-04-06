/* occlibpy.c
 *
 * Python interface to OCC driver
 *
 * Copyright (c) 2014 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 */

#include <Python.h>
#include <occlib_hw.h>

/**
 * Static pointer to OCC exception. Increase refcount where used.
 */
static PyObject *OccError = NULL;

/**
 * Custom object returned from open() which describes single OCC
 * connection.
 */
typedef struct {
    PyObject_HEAD
    struct occ_handle *occ;
} OccObject;

// Forward declaration
static PyTypeObject Occ_Type;

PyDoc_STRVAR(py_occ_open__doc__,
"open(device) -> OccObject\n\n"
"Open connection to OCC driver.\n\n"
"device parameter must point to a valid OCC device file path,\n"
"ie. /dev/snsocb0.\n"
"Raise an occ.error when failed to open connection.");
static PyObject *py_occ_open(PyObject *self, PyObject *args, PyObject *keywds) {
    const char *path;
    int ret;

    static char *kwlist[] = {"device", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "s", kwlist, &path))
        return NULL;

    OccObject *occObj = PyObject_New(OccObject, &Occ_Type);
    if (occObj == NULL)
        return NULL;

    ret = occ_open(path, OCC_INTERFACE_OPTICAL, &occObj->occ);
    if (ret != 0) {
        PyObject_Del(occObj);
        return NULL;
    }

    return (PyObject *)occObj;
}

PyDoc_STRVAR(py_occ_open_debug__doc__,
"open_debug(device) -> OccObject\n\n"
"Open debug connection to OCC driver.\n\n"
"device parameter must point to a valid OCC device file path,\n"
"ie. /dev/snsocb0.\n"
"Raise an occ.error when failed to open connection.");
static PyObject *py_occ_open_debug(PyObject *self, PyObject *args, PyObject *keywds) {
    const char *path;
    int ret;

    static char *kwlist[] = {"device", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "s", kwlist, &path))
        return NULL;

    OccObject *occObj = PyObject_New(OccObject, &Occ_Type);
    if (occObj == NULL)
        return NULL;

    ret = occ_open_debug(path, OCC_INTERFACE_OPTICAL, &occObj->occ);
    if (ret != 0) {
        PyObject_Del(occObj);
        return NULL;
    }

    return (PyObject *)occObj;
}

PyDoc_STRVAR(py_occ_close__doc__,
"close() -> None\n\n"
"Close the connection to OCC driver.\n\n"
"After this function returns the OCC object is no longer valid and can no\n"
"longer be used for communication with the driver.");
static PyObject *py_occ_close(OccObject *self, PyObject *args) {
    if (self->occ != NULL) {
        occ_close(self->occ);
        self->occ = NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}


PyDoc_STRVAR(py_occ_reset__doc__,
"reset() -> None\n\n"
"Reset OCC device and internal variables to initial state.\n\n"
"Reset will establish initial state of the board when the driver was loaded.\n"
"This includes setting registers to initial values, establishing DMA for\n"
"receive queue(s), clearing DMA buffer and sending reset command to the FPGA\n"
"for internal cleanup.\n\n"
"Resetting OCC card might be required sometimes. For instance, OCC card\n"
"may get stalled to prevent buffer overflow. When that happens no data\n"
"can be received or sent and driver state as well as board need to be recycled.\n");
static PyObject *py_occ_reset(OccObject *self, PyObject *args) {
    int ret;

    if (self->occ == NULL) {
        PyErr_SetString(OccError, "OCC connection closed");
        return NULL;
    }

    ret = occ_reset(self->occ);
    if (ret < 0) {
        PyErr_SetString(OccError, strerror(-1 * ret));
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(py_occ_enable_rx__doc__,
"enable_rx([enable=True]) -> None\n\n"
"Enable or disable receiving of data.\n\n"
"When powered up or reset, the board will not be receiving any data until\n"
"instructed to do so. This function allows the application to enable as well\n"
"as disable receiving of data at any point in time.\n"
"When high data-rate is expected, the application may want to setup the\n"
"receive data handler first and only then enable data reception to prevent\n"
"exhausting the limited DMA buffer before the thread could be even started.\n");
static PyObject *py_occ_enable_rx(OccObject *self, PyObject *args, PyObject *keywds) {
    int ret;
    int enable = 1;

    static char *kwlist[] = {"enable", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "|i", kwlist, &enable))
        return NULL;

    if (self->occ == NULL) {
        PyErr_SetString(OccError, "OCC connection closed");
        return NULL;
    }

    ret = occ_enable_rx(self->occ, enable != 0);
    if (ret < 0) {
        PyErr_SetString(OccError, strerror(-1 * ret));
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(py_occ_enable_error_packets__doc__,
"enable_error_packets([enable=True]) -> None\n\n"
"Enable or disable outputing error packets.\n\n"
"OCC FPGA detects communication errors. It recognizes three groups of\n"
"errors:\n"
"- CRC errors are detected when the data integrity for a packet fails\n"
"- Frame length errors are the ones when the packet length doesn't match\n"
"  actual data\n"
"- Frame errors are other out-of-sync data errors\n\n"
"FPGA provides counter for each group separately. But it can also\n"
"transform corrupted packet into error packet which application can\n"
"detect. Error packets can be enabled with this function.\n");
static PyObject *py_occ_enable_error_packets(OccObject *self, PyObject *args, PyObject *keywds) {
    int ret;
    int enable = 1;

    static char *kwlist[] = {"enable", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "|i", kwlist, &enable))
        return NULL;

    if (self->occ == NULL) {
        PyErr_SetString(OccError, "OCC connection closed");
        return NULL;
    }

    ret = occ_enable_error_packets(self->occ, enable != 0);
    if (ret < 0) {
        PyErr_SetString(OccError, strerror(-1 * ret));
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(py_occ_status__doc__,
"status([fast]) -> dictionary with status fields\n\n"
"Return OCC board and driver status.\n\n"
"Reading SFP status can take up to 500ms and can be disabled with fast\n"
"boolean flag. In that case sfp_* field are missing from the response.");
static PyObject *py_occ_status(OccObject *self, PyObject *args, PyObject *keywds) {
    int ret;
    int fast = 0;
    occ_status_t status;
    PyObject *sdict;

    static char *kwlist[] = {"fast", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "|i", kwlist, &fast))
        return NULL;

    if (self->occ == NULL) {
        PyErr_SetString(OccError, "OCC connection closed");
        return NULL;
    }

    ret = occ_status(self->occ, &status, fast != 0);
    if (ret < 0) {
        PyErr_SetString(OccError, strerror(-1 * ret));
        return NULL;
    }

    sdict = PyDict_New();

    if (status.board == OCC_BOARD_PCIE)
        PyDict_SetItem(sdict, PyString_FromString("board_type"), PyString_FromString("PCIe"));
    else if (status.board == OCC_BOARD_PCIX)
        PyDict_SetItem(sdict, PyString_FromString("board_type"), PyString_FromString("PCI-X"));
    else if (status.board == OCC_BOARD_SIMULATOR)
        PyDict_SetItem(sdict, PyString_FromString("board_type"), PyString_FromString("simulator"));
    else
        PyDict_SetItem(sdict, PyString_FromString("board_type"), PyString_FromString("unknown"));

    if (status.interface == OCC_INTERFACE_OPTICAL)
        PyDict_SetItem(sdict, PyString_FromString("interface"), PyString_FromString("optical"));
    else if (status.interface == OCC_INTERFACE_LVDS)
        PyDict_SetItem(sdict, PyString_FromString("interface"), PyString_FromString("LVDS"));
    else
        PyDict_SetItem(sdict, PyString_FromString("interface"), PyString_FromString("optical"));

    PyDict_SetItem(sdict, PyString_FromString("hardware_ver"), PyInt_FromLong(status.hardware_ver));
    PyDict_SetItem(sdict, PyString_FromString("firmware_ver"), PyInt_FromLong(status.firmware_ver));
    PyDict_SetItem(sdict, PyString_FromString("firmware_date"), PyInt_FromLong(status.firmware_date));
    PyDict_SetItem(sdict, PyString_FromString("dma_size"), PyInt_FromLong(status.dma_size));
    PyDict_SetItem(sdict, PyString_FromString("dma_used"), PyInt_FromLong(status.dma_used));
    PyDict_SetItem(sdict, PyString_FromString("rx_rate"), PyInt_FromLong(status.rx_rate));
    PyDict_SetItem(sdict, PyString_FromString("stalled"), PyBool_FromLong(status.stalled));
    PyDict_SetItem(sdict, PyString_FromString("overflowed"), PyBool_FromLong(status.overflowed));

    if (status.optical_signal == OCC_OPT_CONNECTED)
        PyDict_SetItem(sdict, PyString_FromString("optical_signal"), PyString_FromString("connected"));
    else if (status.optical_signal == OCC_OPT_NO_SFP)
        PyDict_SetItem(sdict, PyString_FromString("optical_signal"), PyString_FromString("no SFP"));
    else if (status.optical_signal == OCC_OPT_NO_CABLE)
        PyDict_SetItem(sdict, PyString_FromString("optical_signal"), PyString_FromString("RX signal lost"));
    else if (status.optical_signal == OCC_OPT_LASER_FAULT)
        PyDict_SetItem(sdict, PyString_FromString("optical_signal"), PyString_FromString("laser fault"));
    else
        PyDict_SetItem(sdict, PyString_FromString("optical_signal"), PyString_FromString("unknown"));

    PyDict_SetItem(sdict, PyString_FromString("rx_enabled"), PyBool_FromLong(status.rx_enabled));
    PyDict_SetItem(sdict, PyString_FromString("err_packets_enabled"), PyBool_FromLong(status.err_packets_enabled));
    PyDict_SetItem(sdict, PyString_FromString("fpga_serial_number"), PyLong_FromLongLong(status.fpga_serial_number));
    PyDict_SetItem(sdict, PyString_FromString("fpga_temp"), PyFloat_FromDouble(status.fpga_temp));
    PyDict_SetItem(sdict, PyString_FromString("fpga_core_volt"), PyFloat_FromDouble(status.fpga_core_volt));
    PyDict_SetItem(sdict, PyString_FromString("fpga_aux_volt"), PyFloat_FromDouble(status.fpga_aux_volt));

    if (!fast) {
        if (status.sfp_type == OCC_SFP_MODE_SINGLE)
            PyDict_SetItem(sdict, PyString_FromString("sfp_type"), PyString_FromString("single mode"));
        else if (status.sfp_type == OCC_SFP_MODE_MULTI)
            PyDict_SetItem(sdict, PyString_FromString("sfp_type"), PyString_FromString("multi mode"));
        else
            PyDict_SetItem(sdict, PyString_FromString("sfp_type"), PyString_FromString("unknown"));

        PyDict_SetItem(sdict, PyString_FromString("sfp_part_number"), PyString_FromString(status.sfp_part_number));
        PyDict_SetItem(sdict, PyString_FromString("sfp_serial_number"), PyString_FromString(status.sfp_serial_number));
        PyDict_SetItem(sdict, PyString_FromString("sfp_temp"), PyFloat_FromDouble(status.sfp_temp));
        PyDict_SetItem(sdict, PyString_FromString("sfp_rx_power"), PyFloat_FromDouble(status.sfp_rx_power));
        PyDict_SetItem(sdict, PyString_FromString("sfp_tx_power"), PyFloat_FromDouble(status.sfp_tx_power));
        PyDict_SetItem(sdict, PyString_FromString("sfp_vcc_power"), PyFloat_FromDouble(status.sfp_vcc_power));
        PyDict_SetItem(sdict, PyString_FromString("sfp_tx_bias_cur"), PyFloat_FromDouble(status.sfp_tx_bias_cur));
    }

    PyDict_SetItem(sdict, PyString_FromString("err_crc"), PyInt_FromLong(status.err_crc));
    PyDict_SetItem(sdict, PyString_FromString("err_length"), PyInt_FromLong(status.err_length));
    PyDict_SetItem(sdict, PyString_FromString("err_frame"), PyInt_FromLong(status.err_frame));

    return sdict;
}

PyDoc_STRVAR(py_occ_send__doc__,
"send(data) -> None\n\n"
"Write data to oCC.\n\n"
"Data must be converted into string.");
static PyObject *py_occ_send(OccObject *self, PyObject *args, PyObject *keywds) {
    int ret;
    const char *data;
    int count;

    static char *kwlist[] = {"data", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "s#", kwlist, &data, &count))
        return NULL;

    if (self->occ == NULL) {
        PyErr_SetString(OccError, "OCC connection closed");
        return NULL;
    }

    ret = occ_send(self->occ, (void *)data, count);
    if (ret != 0) {
        PyErr_SetString(OccError, strerror(-1 * ret));
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(py_occ_read__doc__,
"read([size], [timeout]) -> data\n\n"
"Read at most size bytes from OCC.\n\n");
static PyObject *py_occ_read(OccObject *self, PyObject *args, PyObject *keywds) {
    int ret;
    void *data;
    size_t count;
    size_t size = 4096;
    float timeout = 0.0;

    static char *kwlist[] = {"size", "timeout", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "|if", kwlist, &size, &timeout))
        return NULL;

    if (self->occ == NULL) {
        PyErr_SetString(OccError, "OCC connection closed");
        return NULL;
    }

    ret = occ_data_wait(self->occ, &data, &count, (uint32_t)(timeout * 1000));
    if (ret < 0) {
        PyErr_SetString(OccError, strerror(-1 * ret));
        return NULL;
    }

    if (count > size)
        count = size;

    ret = occ_data_ack(self->occ, count);
    if (ret < 0) {
        PyErr_SetString(OccError, strerror(-1 * ret));
        return NULL;
    }

    return PyString_FromStringAndSize(data, count);
}

PyDoc_STRVAR(py_occ_io_read__doc__,
"io_read(bar, offset) -> 32 bit raw value\n\n"
"Read 32 bit register from PCI device and return its value.\n\n"
"Raise exception when channel is not connected or on driver error.");
static PyObject *py_occ_io_read(OccObject *self, PyObject *args, PyObject *keywds) {
    int ret;
    int bar = 0;
    int offset = 0;
    uint32_t value = 0;

    static char *kwlist[] = {"bar", "offset", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "ii", kwlist, &bar, &offset))
        return NULL;

    if (self->occ == NULL) {
        PyErr_SetString(OccError, "OCC connection closed");
        return NULL;
    }

    ret = occ_io_read(self->occ, bar, offset, &value, 1);
    if (ret < 0) {
        PyErr_SetString(OccError, strerror(-1 * ret));
        return NULL;
    }

    return PyInt_FromLong(value);
}

PyDoc_STRVAR(py_occ_io_write__doc__,
"io_write(bar, offset, value) -> None\n\n"
"Write 32 bit register to PCI device.\n\n"
"Raise exception when channel is not connected or on driver error.");
static PyObject *py_occ_io_write(OccObject *self, PyObject *args, PyObject *keywds) {
    int ret;
    int bar;
    int offset;
    uint32_t value;

    static char *kwlist[] = {"bar", "offset", "value", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "iiI", kwlist,
                                     &bar, &offset, &value))
        return NULL;

    if (self->occ == NULL) {
        PyErr_SetString(OccError, "OCC connection closed");
        return NULL;
    }

    ret = occ_io_read(self->occ, bar, offset, &value, 1);
    if (ret < 0) {
        PyErr_SetString(OccError, strerror(-1 * ret));
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef Occ_Methods[] = {
    { "close",      (PyCFunction)py_occ_close,      METH_VARARGS, py_occ_close__doc__ },
    { "reset",      (PyCFunction)py_occ_reset,      METH_VARARGS, py_occ_reset__doc__ },
    { "enable_rx",  (PyCFunction)py_occ_enable_rx,  METH_VARARGS | METH_KEYWORDS, py_occ_enable_rx__doc__ },
    { "enable_error_packets", (PyCFunction)py_occ_enable_error_packets,  METH_VARARGS | METH_KEYWORDS, py_occ_enable_error_packets__doc__ },
    { "status",     (PyCFunction)py_occ_status,     METH_VARARGS | METH_KEYWORDS, py_occ_status__doc__ },
    { "send",       (PyCFunction)py_occ_send,       METH_VARARGS | METH_KEYWORDS, py_occ_send__doc__ },
    { "read",       (PyCFunction)py_occ_read,       METH_VARARGS | METH_KEYWORDS, py_occ_read__doc__ },
    { "io_read",    (PyCFunction)py_occ_io_read,    METH_VARARGS | METH_KEYWORDS, py_occ_io_read__doc__ },
    { "io_write",   (PyCFunction)py_occ_io_write,   METH_VARARGS | METH_KEYWORDS, py_occ_io_write__doc__ },
    {NULL, NULL, 0, NULL}
};

/**
 * Used to find OccObject methods.
 */
static PyObject *py_occ_getattr(OccObject *self, char *name) {
    return Py_FindMethod(Occ_Methods, (PyObject *)self, name);
}

/**
 * OccObject destructor.
 */
static void py_occ_dealloc(OccObject *self) {
    if (self->occ != NULL) {
        occ_close(self->occ);
        self->occ = NULL;
    }
    PyObject_Del(self);
}

static PyTypeObject Occ_Type = {
 	/* The ob_type field must be initialized in the module init function
 	* to be portable to Windows without using C++. */
 	PyVarObject_HEAD_INIT(NULL, 0)
 	"occlib.occ", /*tp_name*/
 	sizeof(OccObject), /*tp_basicsize*/
 	0, /*tp_itemsize*/
 	/* methods */
 	(destructor)py_occ_dealloc, /*tp_dealloc*/
 	0, /*tp_print*/
 	(getattrfunc)py_occ_getattr, /*tp_getattr*/
 	0, /*tp_setattr*/
 	0, /*tp_compare*/
 	0, /*tp_repr*/
 	0, /*tp_as_number*/
 	0, /*tp_as_sequence*/
 	0, /*tp_as_mapping*/
 	0, /*tp_hash*/
 	0, /*tp_call*/
 	0, /*tp_str*/
 	0, /*tp_getattro*/
 	0, /*tp_setattro*/
 	0, /*tp_as_buffer*/
 	Py_TPFLAGS_DEFAULT, /*tp_flags*/
 	0, /*tp_doc*/
 	0, /*tp_traverse*/
 	0, /*tp_clear*/
 	0, /*tp_richcompare*/
 	0, /*tp_weaklistoffset*/
 	0, /*tp_iter*/
 	0, /*tp_iternext*/
 	0, /*tp_methods*/
 	0, /*tp_members*/
 	0, /*tp_getset*/
 	0, /*tp_base*/
 	0, /*tp_dict*/
 	0, /*tp_descr_get*/
 	0, /*tp_descr_set*/
 	0, /*tp_dictoffset*/
 	0, /*tp_init*/
 	0, /*tp_alloc*/
 	0, /*tp_new*/
 	0, /*tp_free*/
 	0, /*tp_is_gc*/
};

static PyMethodDef Occlib_Methods[] = {
    { "open",  (PyCFunction)py_occ_open, METH_VARARGS | METH_KEYWORDS, py_occ_open__doc__},
    { "open_debug",  (PyCFunction)py_occ_open_debug, METH_VARARGS | METH_KEYWORDS, py_occ_open_debug__doc__},
    {NULL, NULL, 0, NULL}
};

PyDoc_STRVAR(module_doc, "OCC interface library");

PyMODINIT_FUNC initocc(void)
{
    if (PyType_Ready(&Occ_Type) < 0)
        return;

    PyObject *m = Py_InitModule3("occ", Occlib_Methods, module_doc);
    if (m == NULL)
        return;

    if (OccError == NULL) {
        OccError = PyErr_NewException("occ.error", NULL, NULL);
        if (OccError == NULL)
            return;
    }
    Py_INCREF(OccError);
    PyModule_AddObject(m, "error", OccError);
}
