/*
 * shamrock.cpp
 *
 * This is an EPICS driver for Andor Shamrock spectrographs
 *
 * Author: Mark Rivers
 *         Univerity of Chicago
 *
 * Created: April 9, 2014
 *
 * Modified: March 16, 2015 to add Flipper functions
 *
 */

#include <iocsh.h>
#include <stdlib.h>
#include <string.h>

#include <epicsString.h>

#include <asynPortDriver.h>

#ifdef _WIN32
#include "ATMCD32D.h"
#else
#include "atmcdLXd.h"
#endif
#include <atspectrograph.h>

#include <epicsExport.h>

static const char *driverName = "shamrock";


/* Shamrock driver specific parameters */
#define SRWavelengthString            "SR_WAVELENGTH"
#define SRMinWavelengthString         "SR_MIN_WAVELENGTH"
#define SRMaxWavelengthString         "SR_MAX_WAVELENGTH"
#define SRCalibrationString           "SR_CALIBRATION"
#define SRGratingString               "SR_GRATING"
#define SRNumGratingsString           "SR_NUM_GRATINGS"
#define SRGratingExistsString         "SR_GRATING_EXISTS"
#define SRFlipperMirrorExistsString   "SR_FLIPPER_MIRROR_EXISTS"
#define SRFlipperMirrorPortString     "SR_FLIPPER_MIRROR_PORT"
#define SRSlitExistsString            "SR_SLIT_EXISTS"
#define SRSlitSizeString              "SR_SLIT_SIZE"


#define MAX_ERROR_MESSAGE_SIZE 100

#define MAX_SLITS 4

#define MAX_GRATINGS 3

#define MAX_FLIPPER_MIRRORS 2


// Maximum number of address.
#define MAX_ADDR 4

/** Driver for Andor Shamrock spectrographs.
 * One instance of this class will control one spectrograph.
 */
class shamrock : public asynPortDriver
{
public:
    shamrock(const char *portName, int shamrockID, const char *iniPath, int priority, int stacksize);

    /* virtual methods to override from ADDriver */
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    virtual asynStatus readFloat32Array(asynUser *pasynUser, epicsFloat32 *pValue, size_t nElements, size_t *nIn);
    void report(FILE *fp, int details);

protected:
    int SRWavelength_;          /** Wavelength                   (float64 read/write) */
    #define FIRST_SR_PARAM SRWavelength_
    int SRMinWavelength_;       /** Min wavelength               (float64 read/write) */
    int SRMaxWavelength_;       /** Min wavelength               (float64 read/write) */
    int SRCalibration_;         /** Calibration                  (float32 array read) */
    int SRGrating_;             /** Grating                      (int32 read/write) */
    int SRNumGratings_;         /** Number of gratings           (int32 read) */
    int SRGratingExists_;       /** Grating exists               (int32 read) */
    int SRFlipperMirrorExists_; /** Flipper Mirror exists        (int32 read) */
    int SRFlipperMirrorPort_;   /** Flipper Mirror Port          (int32 read/write) */
    int SRSlitExists_;          /** Slit exists                  (int32 read) */
    int SRSlitSize_;            /** Slit width                   (float64 read/write) */
    #define LAST_SR_PARAM SRSlitSize_


private:
    /* Local methods to this class */
    inline asynStatus checkError(eATSpectrographReturnCodes status, const char *functionName, const char *shamrockFunction);
    asynStatus getStatus();

    /* Data */
    int shamrockId_;
    bool slitIsPresent_[MAX_SLITS];
    int numPixels_;
    float *calibration_;
    char lastError_[MAX_ERROR_MESSAGE_SIZE];
    bool flipperMirrorIsPresent_[MAX_FLIPPER_MIRRORS];
};

/** Configuration function to configure one spectrograph.
 *
 * This function need to be called once for each spectrography to be used by the IOC. A call to this
 * function instanciates one object from the shamrock class.
 * \param[in] portName asyn port name to assign to the camera.
 * \param[in] shamrockId The spectrograph index.
 * \param[in] iniPath The path to the camera ini file
 * \param[in] priority The EPICS thread priority for this driver.  0=use asyn default.
 * \param[in] stackSize The size of the stack for the EPICS port thread. 0=use asyn default.
 */
extern "C" int shamrockConfig(const char *portName, int shamrockId, const char *iniPath, 
                               int priority, int stackSize)
{
    new shamrock( portName, shamrockId, iniPath, priority, stackSize);
    return asynSuccess;
}

/** Constructor for the shamrock class
 * \param[in] portName asyn port name to assign to the camera.
 * \param[in] shamrockID The spectrograph index.
 * \param[in] iniPath The path to the camera ini file
 * \param[in] priority The EPICS thread priority for this driver.  0=use asyn default.
 * \param[in] stackSize The size of the stack for the EPICS port thread. 0=use asyn default.
 */
shamrock::shamrock(const char *portName, int shamrockID, const char *iniPath, int priority, int stackSize)
    : asynPortDriver(portName, MAX_ADDR,
            asynInt32Mask | asynFloat64Mask | asynFloat32ArrayMask | asynDrvUserMask, 
            asynInt32Mask | asynFloat64Mask | asynFloat32ArrayMask,
            ASYN_CANBLOCK | ASYN_MULTIDEVICE, 1, priority, stackSize),
    shamrockId_(shamrockID)
{
    static const char *functionName = "shamrock";
    int status;
    int derror;
    eATSpectrographReturnCodes error;
    float minWavelength, maxWavelength;
    int numDevices;
    int numGratings;
    float pixelWidth;
    int i;
    int numFlipperStatus;
    int width, height;
    float xSize, ySize;

    createParam(SRWavelengthString,       asynParamFloat64,   &SRWavelength_);
    createParam(SRMinWavelengthString,    asynParamFloat64,   &SRMinWavelength_);
    createParam(SRMaxWavelengthString,    asynParamFloat64,   &SRMaxWavelength_);
    createParam(SRCalibrationString,      asynParamFloat32Array,   &SRCalibration_);
    createParam(SRGratingString,          asynParamInt32,     &SRGrating_);
    createParam(SRNumGratingsString,      asynParamInt32,     &SRNumGratings_);
    createParam(SRGratingExistsString,    asynParamInt32,     &SRGratingExists_);
    createParam(SRFlipperMirrorPortString,        asynParamInt32,     &SRFlipperMirrorPort_);
    createParam(SRFlipperMirrorExistsString,    asynParamInt32,     &SRFlipperMirrorExists_);
    createParam(SRSlitExistsString,      asynParamInt32,     &SRSlitExists_);
    createParam(SRSlitSizeString,         asynParamFloat64,   &SRSlitSize_);

    error = ATSpectrographInitialize((char *)iniPath);

    status = checkError(error, functionName, "ATSpectrographInitialize");
    if (status) return;
    error = ATSpectrographGetNumberDevices(&numDevices);
    status = checkError(error, functionName, "ATSpectrographGetNumberDevices");
    if (status) return;
    if (numDevices < 1) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s:  No ATSpectrograph spectrographs found, numDevices=%d\n",
            driverName, functionName, numDevices);
        return;
    }

    //Get Detector dimensions
    derror = GetDetector(&width, &height);
    if (derror != DRV_SUCCESS) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s:  GetDetector() status = %d\n",
            driverName, functionName, derror);
        return;
    }

    //Sets the number of pixels for calibration purposes
    error = ATSpectrographSetNumberPixels(shamrockId_, width);
    status = checkError(error, functionName, "ATSpectrographSetNumberPixels");

    //Get Detector pixel size
    derror = GetPixelSize(&xSize, &ySize);
    if (derror != DRV_SUCCESS) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s:  GetPixelSize() status = %d\n",
            driverName, functionName, derror);
        return;
    }

    //Set the pixel width in microns for calibration purposes.
    error = ATSpectrographSetPixelWidth(shamrockId_, xSize);
    status = checkError(error, functionName, "ATSpectrographSetPixelWidth");
    
    // Determine the number of pixels on the attached CCD and the pixel size
    error = ATSpectrographGetNumberPixels(shamrockId_, &numPixels_);
    status = checkError(error, functionName, "ATSpectrographGetNumberPixels");
    error = ATSpectrographGetPixelWidth(shamrockId_, &pixelWidth);
    status = checkError(error, functionName, "ATSpectrographGetPixelWidth");
    calibration_ = (float *)calloc(numPixels_, sizeof(float));

    // Determine which slits are present
    for (i=0; i<MAX_SLITS; i++) {
        int present;
        error = ATSpectrographSlitIsPresent(shamrockId_, static_cast<eATSpectrographSlitIndex>(i+1), &present);
        status = checkError(error, functionName, "ATSpectrographAutoSlitIsPresent");
        slitIsPresent_[i] = (present == 1);
        setIntegerParam(i, SRSlitExists_, slitIsPresent_[i]);
    }

    // Determine how many gratings are present
    error = ATSpectrographGetNumberGratings(shamrockId_, &numGratings);
    status = checkError(error, functionName, "ATSpectrographGetNumberGratings");
    setIntegerParam(SRNumGratings_, numGratings);

    // Get wavelength range of each grating
    for (i=1; i<=numGratings; i++) {
        setIntegerParam(i, SRGratingExists_, 1);
        error = ATSpectrographGetWavelengthLimits(shamrockId_, i, &minWavelength, &maxWavelength);
        status = checkError(error, functionName, "ATSpectrographGetWavelengthLimits");
        setDoubleParam(i, SRMinWavelength_, minWavelength);
        setDoubleParam(i, SRMaxWavelength_, maxWavelength);
    }
    for (i=numGratings; i<MAX_GRATINGS; i++) {
        setIntegerParam(i, SRGratingExists_, 0);
    }
    
    // Determine which Flipper Mirrors exist
    for (i=0; i<MAX_FLIPPER_MIRRORS; i++) {
        error = ATSpectrographFlipperMirrorIsPresent(shamrockId_, static_cast<eATSpectrographFlipper>(i+1), &numFlipperStatus);
        status = checkError(error, functionName, "ATSpectrographFlipperMirrorIsPresent");
        flipperMirrorIsPresent_[i] = (numFlipperStatus== 1); 
        setIntegerParam(i, SRFlipperMirrorExists_, flipperMirrorIsPresent_[i]);
    }
    
    getStatus();
    
    for (i=0; i<MAX_ADDR; i++) {
        callParamCallbacks(i);
    }
    
    return;
}

inline asynStatus shamrock::checkError(eATSpectrographReturnCodes status, const char *functionName, const char *shamrockFunction)
{
    if (status != ATSPECTROGRAPH_SUCCESS) {
        ATSpectrographGetFunctionReturnDescription(status, lastError_, sizeof(lastError_));
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: ERROR calling %s Description=%s\n",
            driverName, functionName, shamrockFunction, lastError_);
        return asynError;
    }
    return asynSuccess;
}

asynStatus shamrock::getStatus()
{
    eATSpectrographReturnCodes error;
    asynStatus status;
    int grating;
    float wavelength;
    float width;
    int i;
    static const char *functionName = "getStatus";
    eATSpectrographPortPosition port;

    //Get Flipper Status
    for (i=0; i<MAX_FLIPPER_MIRRORS; i++) {
        if (flipperMirrorIsPresent_[i] == 0) continue;
        error = ATSpectrographGetFlipperMirror(shamrockId_, static_cast<eATSpectrographFlipper>(i+1), &port);
        status = checkError(error, functionName, "ATSpectrographGetFlipperMirror");
        if (status) return asynError;
        setIntegerParam(i, SRFlipperMirrorPort_, port);
    }


    error = ATSpectrographGetGrating(shamrockId_, &grating);
    status = checkError(error, functionName, "ATSpectrographGetGrating");
    if (status) return asynError;
    setIntegerParam(SRGrating_, grating);
    
    error = ATSpectrographGetWavelength(shamrockId_, &wavelength);
    status = checkError(error, functionName, "ATSpectrographGetWavelength");
    if (status) return asynError;
    setDoubleParam(SRWavelength_, wavelength);

    for (i=0; i<MAX_SLITS; i++) {
        setDoubleParam(i, SRSlitSize_, 0.);
        if (slitIsPresent_[i] == 0) continue;
        error = ATSpectrographGetSlitWidth(shamrockId_, static_cast<eATSpectrographSlitIndex>(i+1), &width);
        status = checkError(error, functionName, "ATSpectrographGetSlitWidth");
        if (status) return asynError;
        setDoubleParam(i, SRSlitSize_, width);
    }
    
    error = ATSpectrographGetCalibration(shamrockId_, calibration_, numPixels_);
    status = checkError(error, functionName, "ATSpectrographGetCalibration");
    if (status) return asynError;
    setDoubleParam(0, SRMinWavelength_, calibration_[0]);
    setDoubleParam(0, SRMaxWavelength_, calibration_[numPixels_-1]);
    // We need to find a C/C++ library to do 3'rd order polynomial fit
    // For now we do a first order fit!
    //double slope = (calibration_[numPixels_-1] - calibration_[0]) / (numPixels_-1);

    for (i=0; i<MAX_ADDR; i++) {
        callParamCallbacks(i);
    }
    
    doCallbacksFloat32Array(calibration_, numPixels_, SRCalibration_, 0);

    return asynSuccess;
}
  

/** Sets an int32 parameter.
  * \param[in] pasynUser asynUser structure that contains the function code in pasynUser->reason. 
  * \param[in] value The value for this parameter 
  *
  * Takes action if the function code requires it.  ADAcquire, ADSizeX, and many other
  * function codes make calls to the Firewire library from this function. */
asynStatus shamrock::writeInt32( asynUser *pasynUser, epicsInt32 value)
{
    asynStatus status = asynSuccess;
    eATSpectrographReturnCodes error;
    int function = pasynUser->reason;
    int addr;
    static const char *functionName = "writeInt32";

    pasynManager->getAddr(pasynUser, &addr);
    if (addr < 0) addr=0;

    /* Set the value in the parameter library.  This may change later but that's OK */
    status = setIntegerParam(addr, function, value);

    if (function == SRGrating_) {
        error = ATSpectrographSetGrating(shamrockId_, value);
        status = checkError(error, functionName, "ATSpectrographSetGrating");
    }
    
    // Port Information
    else if (function == SRFlipperMirrorPort_) {
        if (flipperMirrorIsPresent_[addr]) {
            error = ATSpectrographSetFlipperMirror(shamrockId_, static_cast<eATSpectrographFlipper>(addr+1), static_cast<eATSpectrographPortPosition>(value));
            status = checkError(error, functionName, "ATSpectrographSetFlipperMirror");
        }
    }

    
    getStatus();

    asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, 
        "%s::%s function=%d, value=%d, status=%d\n",
        driverName, functionName, function, value, status);
            
    callParamCallbacks(addr);
    return status;
}

/** Sets an float64 parameter.
  * \param[in] pasynUser asynUser structure that contains the function code in pasynUser->reason. 
  * \param[in] value The value for this parameter 
  *
  * Takes action if the function code requires it. */
asynStatus shamrock::writeFloat64( asynUser *pasynUser, epicsFloat64 value)
{
    asynStatus status = asynSuccess;
    eATSpectrographReturnCodes error;
    int function = pasynUser->reason;
    int addr;
    static const char *functionName = "writeFloat64";
    
    pasynManager->getAddr(pasynUser, &addr);
    if (addr < 0) addr=0;

    /* Set the value in the parameter library.  This may change later but that's OK */
    status = setDoubleParam(addr, function, value);

    if (function == SRWavelength_) {
        error = ATSpectrographSetWavelength(shamrockId_, (float) value);
        status = checkError(error, functionName, "ATSpectrographSetWavelength");
    
    } 
    else if (function == SRSlitSize_) {
        if (slitIsPresent_[addr]) {
          error = ATSpectrographSetSlitWidth(shamrockId_, static_cast<eATSpectrographSlitIndex>(addr+1), (float) value);
          status = checkError(error, functionName, "ATSpectrographSetSlit");
        }
    }
    
    getStatus();

    asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
        "%s::%s function=%d, value=%f, status=%d\n",
        driverName, functionName, function, value, status);
    callParamCallbacks(addr);
    return status;
}

/** Reads float32 array.
  * \param[in] pasynUser asynUser structure that contains the function code in pasynUser->reason. 
  * \param[in] pValue The array
  * \param[in] nElements The size of the array
  * \param[out] nIn The number of elements copied to the array
  * Takes action if the function code requires it. */
asynStatus shamrock::readFloat32Array(asynUser *pasynUser, epicsFloat32 *pValue, size_t nElements, size_t *nIn)
{
    asynStatus status = asynSuccess;
    int function = pasynUser->reason;
    //static const char *functionName = "readFloat32Array";
    
    if (function == SRCalibration_) {
        *nIn = numPixels_;
        if (nElements < *nIn) *nIn = nElements;
        memcpy(pValue, calibration_, *nIn * sizeof(float));
    } 
    return status;
}

/** Print out a report; calls asynPortDriver::report to get base class report as well.
  * \param[in] fp File pointer to write output to
  * \param[in] details Level of detail desired.
 */
void shamrock::report(FILE *fp, int details)
{
    //static const char *functionName = "report";
    
    asynPortDriver::report(fp, details);
    return;
}

static const iocshArg configArg0 = {"Port name", iocshArgString};
static const iocshArg configArg1 = {"shamrockId", iocshArgInt};
static const iocshArg configArg2 = {"iniPath", iocshArgString};
static const iocshArg configArg3 = {"priority", iocshArgInt};
static const iocshArg configArg4 = {"stackSize", iocshArgInt};
static const iocshArg * const configArgs[] = {&configArg0,
                                              &configArg1,
                                              &configArg2,
                                              &configArg3,
                                              &configArg4};
static const iocshFuncDef configATSpectrograph = {"shamrockConfig", 5, configArgs};
static void configCallFunc(const iocshArgBuf *args)
{
    shamrockConfig(args[0].sval, args[1].ival, args[2].sval, 
                    args[3].ival, args[4].ival);
}


static void shamrockRegister(void)
{
    iocshRegister(&configATSpectrograph, configCallFunc);
}

extern "C" {
epicsExportRegistrar(shamrockRegister);
}

