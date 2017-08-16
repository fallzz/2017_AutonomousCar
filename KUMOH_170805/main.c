#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <nvcommon.h>
#include <nvmedia.h>
#include <testutil_board.h>
#include <testutil_capture_input.h>
#include "nvthread.h"
#include "car_lib.h"
#include <highgui.h>
#include <cv.h>
#include <ResTable_720To320.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>

#define VIP_BUFFER_SIZE 6
#define VIP_FRAME_TIMEOUT_MS 100
#define VIP_NAME "vip"
#define MESSAGE_PRINTF printf
#define CRC32_POLYNOMIAL 0xEDB88320L
#define RESIZE_WIDTH  320
#define RESIZE_HEIGHT 240

#define WINDOW_COUNT 15
#define MARGIN 25
#define MIN_PIXEL 50

#define YM_PER_PIX 30/240
#define XM_PER_PIX 3.7/320
#define PI 3.1415926

#define DRIVE 0
#define STOP_WAIT_START_SIGNAL1 1 
#define STOP_WAIT_START_SIGNAL2 2 
#define STOP_FRONT_SENSOR 3

#define SEARCH_AREA_SPEED   10
#define RUN_SPEED   50
//asd
int verticalFlag = 0;
int parallelFlag = 0;
int obstacleFlag = 0;
int spaceFlag = 0;

//OVERALL VARIABLES
static char drive_status;
static unsigned int img2bird[180][240][2];
static NvMediaVideoSurface *capSurf = NULL;
static NvBool stop = NVMEDIA_FALSE;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

//DATA STRUCTURE
typedef struct {
	I2cId i2cDevice;
	CaptureInputDeviceId vipDeviceInUse;
	NvMediaVideoCaptureInterfaceFormat vipInputtVideoStd;
	unsigned int vipInputWidth, vipInputHeight;
	float vipAspectRatio;
	unsigned int vipMixerWidth, vipMixerHeight;
	NvBool vipDisplayEnabled;
	NvMediaVideoOutputType vipOutputType;
	NvMediaVideoOutputDevice vipOutputDevice[2];
	NvBool vipFileDumpEnabled;
	char * vipOutputFileName;
	unsigned int vipCaptureTime, vipCaptureCount;
} TestArgs;

typedef struct {
	NvMediaVideoSurface *surf;
	NvBool last;
} QueueElem;

typedef struct {
	char *name;
	NvSemaphore *semStart, *semDone;
	NvMediaVideoCapture *capture;
	NvMediaVideoMixer *mixer;
	FILE *fout;
	unsigned int inputWidth, inputHeight, timeout, last;
	NvBool displayEnabled, fileDumpEnabled, timeNotCount;
} CaptureContext;

//Driving Function
static void start_setting(int velocity);
static void Stop_wait_start_signal1(IplImage* imgYCrCb, IplImage* imgGray);
static void Stop_wait_start_signal2();
static void StopSignChecking(IplImage* imgHSV, IplImage* imgGray);

static void InversePerspectiveMapping(IplImage* imgOrigin, IplImage* imgBird);
static void MatrixMultiply(float *a, float *b, float *result, int col1, int col2, int row2);
static void lineDetection(IplImage* imgBird, IplImage* imgOrigin, IplImage* imgYUV);
static int birdEyeView(IplImage* imgBird, IplImage* imgOrigin, IplImage* imgYUV, int *base);
static int slidingWindow(IplImage* imgGray, int base, int *lineX, int *lineY);
static int nonZero(IplImage* imgGray, int top, int bottom, int left, int right, int *tempX, int *tempY);

//Function(from captureOpenCV.c)
static void SignalHandler(int signal);
static void GetTime(NvMediaTime *time);
static void AddTime(NvMediaTime *time, NvU64 uSec, NvMediaTime *res);
static NvBool SubTime(NvMediaTime *time1, NvMediaTime *time2);
static int ParseOptions(int argc, char *argv[], TestArgs *args);
static int DumpFrame(FILE *fout, NvMediaVideoSurface *surf);
static int Frame2Ipl(IplImage* img);
static unsigned int CaptureThread(void *params);
static void CheckDisplayDevice(NvMediaVideoOutputDevice deviceType, NvMediaBool *enabled, unsigned int *displayId);
void *ControlThread(void *unused);

int main(int argc, char *argv[]){
	int err = -1;
	TestArgs testArgs;

	CaptureInputHandle handle;

	NvMediaVideoCapture *vipCapture = NULL;
	NvMediaDevice *device = NULL;
	NvMediaVideoMixer *vipMixer = NULL;
	NvMediaVideoOutput *vipOutput[2] = { NULL, NULL };
	NvMediaVideoOutput *nullOutputList[1] = { NULL };
	FILE *vipFile = NULL;

	NvSemaphore *vipStartSem = NULL, *vipDoneSem = NULL;
	NvThread *vipThread = NULL;

	CaptureContext vipCtx;
	NvMediaBool deviceEnabled = NVMEDIA_FALSE;
	unsigned int displayId;

	pthread_t cntThread;

	signal(SIGINT, SignalHandler);

	memset(&testArgs, 0, sizeof(TestArgs));
	if (!ParseOptions(argc, argv, &testArgs))
		return -1;
	// Init Option
	start_setting(0);
	printf("1. Create NvMedia capture \n");
	// Create NvMedia capture(s)
	switch (testArgs.vipDeviceInUse)
	{
	case AnalogDevices_ADV7180:
		break;
	case AnalogDevices_ADV7182:
	{
		CaptureInputConfigParams params;

		params.width = testArgs.vipInputWidth;
		params.height = testArgs.vipInputHeight;
		params.vip.std = testArgs.vipInputtVideoStd;

		if (testutil_capture_input_open(testArgs.i2cDevice, testArgs.vipDeviceInUse, NVMEDIA_TRUE, &handle) < 0)
		{
			MESSAGE_PRINTF("Failed to open VIP device\n");
			goto fail;
		}

		if (testutil_capture_input_configure(handle, &params) < 0)
		{
			MESSAGE_PRINTF("Failed to configure VIP device\n");
			goto fail;
		}

		break;
	}
	default:
		MESSAGE_PRINTF("Bad VIP device\n");
		goto fail;
	}


	if (!(vipCapture = NvMediaVideoCaptureCreate(testArgs.vipInputtVideoStd, // interfaceFormat
		NULL, // settings
		VIP_BUFFER_SIZE)))// numBuffers
	{
		MESSAGE_PRINTF("NvMediaVideoCaptureCreate() failed for vipCapture\n");
		goto fail;
	}


	printf("2. Create NvMedia device \n");
	// Create NvMedia device
	if (!(device = NvMediaDeviceCreate()))
	{
		MESSAGE_PRINTF("NvMediaDeviceCreate() failed\n");
		goto fail;
	}

	printf("3. Create NvMedia mixer(s) and output(s) and bind them \n");
	// Create NvMedia mixer(s) and output(s) and bind them
	unsigned int features = 0;


	features |= NVMEDIA_VIDEO_MIXER_FEATURE_VIDEO_SURFACE_TYPE_YV16X2;
	features |= NVMEDIA_VIDEO_MIXER_FEATURE_PRIMARY_VIDEO_DEINTERLACING; // Bob the 16x2 format by default
	if (testArgs.vipOutputType != NvMediaVideoOutputType_OverlayYUV)
		features |= NVMEDIA_VIDEO_MIXER_FEATURE_DVD_MIXING_MODE;

	if (!(vipMixer = NvMediaVideoMixerCreate(device, // device
		testArgs.vipMixerWidth, // mixerWidth
		testArgs.vipMixerHeight, // mixerHeight
		testArgs.vipAspectRatio, //sourceAspectRatio
		testArgs.vipInputWidth, // primaryVideoWidth
		testArgs.vipInputHeight, // primaryVideoHeight
		0, // secondaryVideoWidth
		0, // secondaryVideoHeight
		0, // graphics0Width
		0, // graphics0Height
		0, // graphics1Width
		0, // graphics1Height
		features, // features
		nullOutputList))) // outputList
	{
		MESSAGE_PRINTF("NvMediaVideoMixerCreate() failed for vipMixer\n");
		goto fail;
	}

	printf("4. Check that the device is enabled (initialized) \n");
	// Check that the device is enabled (initialized)
	CheckDisplayDevice(
		testArgs.vipOutputDevice[0],
		&deviceEnabled,
		&displayId);

	if ((vipOutput[0] = NvMediaVideoOutputCreate(testArgs.vipOutputType, // outputType
		testArgs.vipOutputDevice[0], // outputDevice
		NULL, // outputPreference
		deviceEnabled, // alreadyCreated
		displayId, // displayId
		NULL))) // displayHandle
	{
		if (NvMediaVideoMixerBindOutput(vipMixer, vipOutput[0], NVMEDIA_OUTPUT_DEVICE_0) != NVMEDIA_STATUS_OK)
		{
			MESSAGE_PRINTF("Failed to bind VIP output to mixer\n");
			goto fail;
		}
	}
	else
	{
		MESSAGE_PRINTF("NvMediaVideoOutputCreate() failed for vipOutput\n");
		goto fail;
	}



	printf("5. Open output file(s) \n");
	// Open output file(s)
	if (testArgs.vipFileDumpEnabled)
	{
		vipFile = fopen(testArgs.vipOutputFileName, "w");
		if (!vipFile || ferror(vipFile))
		{
			MESSAGE_PRINTF("Error opening output file for VIP\n");
			goto fail;
		}
	}

	printf("6. Create vip pool(s), queue(s), fetch threads and stream start/done semaphores \n");
	// Create vip pool(s), queue(s), fetch threads and stream start/done semaphores
	if (NvSemaphoreCreate(&vipStartSem, 0, 1) != RESULT_OK)
	{
		MESSAGE_PRINTF("NvSemaphoreCreate() failed for vipStartSem\n");
		goto fail;
	}

	if (NvSemaphoreCreate(&vipDoneSem, 0, 1) != RESULT_OK)
	{
		MESSAGE_PRINTF("NvSemaphoreCreate() failed for vipDoneSem\n");
		goto fail;
	}

	vipCtx.name = VIP_NAME;

	vipCtx.semStart = vipStartSem;
	vipCtx.semDone = vipDoneSem;

	vipCtx.capture = vipCapture;
	vipCtx.mixer = vipMixer;
	vipCtx.fout = vipFile;

	vipCtx.inputWidth = testArgs.vipInputWidth;
	vipCtx.inputHeight = testArgs.vipInputHeight;

	vipCtx.timeout = VIP_FRAME_TIMEOUT_MS;

	vipCtx.displayEnabled = testArgs.vipDisplayEnabled;
	vipCtx.fileDumpEnabled = testArgs.vipFileDumpEnabled;

	if (testArgs.vipCaptureTime)
	{
		vipCtx.timeNotCount = NVMEDIA_TRUE;
		vipCtx.last = testArgs.vipCaptureTime;
	}
	else
	{
		vipCtx.timeNotCount = NVMEDIA_FALSE;
		vipCtx.last = testArgs.vipCaptureCount;
	}


	if (NvThreadCreate(&vipThread, CaptureThread, &vipCtx, NV_THREAD_PRIORITY_NORMAL) != RESULT_OK)
	{
		MESSAGE_PRINTF("NvThreadCreate() failed for vipThread\n");
		goto fail;
	}

	printf("wait for ADV7182 ... one second\n");
	sleep(1);

	printf("7. Kickoff \n");
	// Kickoff
	NvMediaVideoCaptureStart(vipCapture);
	NvSemaphoreIncrement(vipStartSem);

	printf("8. Control Thread\n");
	pthread_create(&cntThread, NULL, &ControlThread, NULL);

	printf("9. Wait for completion \n");
	// Wait for completion
	NvSemaphoreDecrement(vipDoneSem, NV_TIMEOUT_INFINITE);
	err = 0;

fail: // Run down sequence
	// Destroy vip threads and stream start/done semaphores
	if (vipThread)
		NvThreadDestroy(vipThread);
	if (vipDoneSem)
		NvSemaphoreDestroy(vipDoneSem);
	if (vipStartSem)
		NvSemaphoreDestroy(vipStartSem);

	printf("10. Close output file(s) \n");
	DesireSpeed_Write(0);
	CameraYServoControl_Write(1500);
	SteeringServoControl_Write(1500);
	// Close output file(s)
	if (vipFile)
		fclose(vipFile);

	// Unbind NvMedia mixer(s) and output(s) and destroy them
	if (vipOutput[0])
	{
		NvMediaVideoMixerUnbindOutput(vipMixer, vipOutput[0], NULL);
		NvMediaVideoOutputDestroy(vipOutput[0]);
	}
	if (vipOutput[1])
	{
		NvMediaVideoMixerUnbindOutput(vipMixer, vipOutput[1], NULL);
		NvMediaVideoOutputDestroy(vipOutput[1]);
	}
	if (vipMixer)
		NvMediaVideoMixerDestroy(vipMixer);


	// Destroy NvMedia device
	if (device)
		NvMediaDeviceDestroy(device);

	// Destroy NvMedia capture(s)
	if (vipCapture)
	{
		NvMediaVideoCaptureDestroy(vipCapture);
		// Reset VIP settings of the board
		switch (testArgs.vipDeviceInUse)
		{
		case AnalogDevices_ADV7180: // TBD
			break;
		case AnalogDevices_ADV7182: // TBD
			//testutil_capture_input_close(handle);
			break;
		default:
			break;
		}
	}

	return err;
}

static void start_setting(int velocity){
	int i, j;
	double gap;
	CarControlInit();
	SpeedControlOnOff_Write(CONTROL);
	DesireSpeed_Write(velocity);
	drive_status = STOP_WAIT_START_SIGNAL1;
	CameraYServoControl_Write(1570);
	CarLight_Write(FRONT_ON);
	PositionControlOnOff_Write(CONTROL);
	EncoderCounter_Write(0);
	PositionControlOnOff_Write(UNCONTROL);
	//BirdView Initialize
	for (i = 0; i < 180; i++){
		gap = (109. + 210. / 180. * i) / 240.;
		int y = (int)(60. / 180. * i) + 70;
		for (j = 0; j < 240; j++){
			img2bird[i][j][0] = 120 - (int)(120. / 180. * i) + (int)(gap * j);
			img2bird[i][j][1] = y;
		}
	}
}

static int init_count = 0;

static void Stop_wait_start_signal1(IplImage* imgYCrCb, IplImage* imgGray){
	cvInRangeS(imgYCrCb, cvScalar(0, 133, 77, 0), cvScalar(255, 173, 127, 0), imgGray);
	unsigned j, k, num, count;
	count = 0;
	for (j = 0; j < RESIZE_HEIGHT; j++){
		for (k = 0; k < RESIZE_WIDTH; k++){
			if (imgGray->imageData[k + RESIZE_WIDTH * j] != 0){
				count++;
			}
		}
	}
	if (init_count == 0) init_count = count;
	if (init_count + 1000 < count){
		Alarm_Write(ON);
		init_count = 0;
		drive_status = STOP_WAIT_START_SIGNAL2;
	}
}

static void Stop_wait_start_signal2(){
	if (++init_count > 20){
		init_count = 0;
		drive_status = DRIVE;
		Alarm_Write(ON);
		DesireSpeed_Write(50);
	}
}

static void StopSignChecking(IplImage* imgHSV, IplImage* imgGray){
	cvInRangeS(imgHSV, cvScalar(0, 0, 0, 0), cvScalar(10, 255, 255, 0), imgGray);
}

static void InversePerspectiveMapping(IplImage* imgOrigin, IplImage* imgBird){
	int frameWidth = 320;
	int frameHeight = 240;
	int alpha_ = 13, beta_ = 90, gamma_ = 90;
	int f_ = 400, dist_ = 300;
	float focalLength, dist, alpha, beta, gamma;

	alpha = (float)(((float)alpha_ - 90) * PI / 180);
	beta = (float)(((float)beta_ - 90) * PI / 180);
	gamma = (float)(((float)gamma_ - 90) * PI / 180);
	focalLength = (float)f_;
	dist = (float)dist_;
	CvSize image_size = cvSize(320, 240);
	float w = (float)image_size.width, h = (float)image_size.height;

	float A1[4][3] = { 1, 0, -w / 2,
		0, 1, -h / 2,
		0, 0, 0,
		0, 0, 1 };

	float RX[4][4] = { 1, 0, 0, 0,
		0, cos(alpha), -sin(alpha), 0,
		0, sin(alpha), cos(alpha), 0,
		0, 0, 0, 1 };

	float RY[4][4] = {
		cos(beta), 0, -sin(beta), 0,
		0, 1, 0, 0,
		sin(beta), 0, cos(beta), 0,
		0, 0, 0, 1 };

	float RZ[4][4] = {
		cos(gamma), -sin(gamma), 0, 0,
		sin(gamma), cos(gamma), 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1 };

	float T[4][4] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, dist,
		0, 0, 0, 1 };

	float K[3][4] = {
		focalLength, 0, w / 2, 0,
		0, focalLength, h / 2, 0,
		0, 0, 1, 0 };

	float RXRY[4][4], RXRYRZ[4][4];
	MatrixMultiply(&RX[0][0], &RY[0][0], &RXRY[0][0], 4, 4, 4);
	MatrixMultiply(&RXRY[0][0], &RZ[0][0], &RXRYRZ[0][0], 4, 4, 4);
	float RA1[4][3], TRA1[4][3], transformationData[3][3];
	MatrixMultiply(&RXRYRZ[0][0], &A1[0][0], &RA1[0][0], 4, 4, 3);
	MatrixMultiply(&T[0][0], &RA1[0][0], &TRA1[0][0], 4, 4, 3);
	MatrixMultiply(&K[0][0], &TRA1[0][0], &transformationData[0][0], 3, 4, 3);
	CvMat transformation = cvMat(3, 3, CV_32FC1, transformationData);
	cvWarpPerspective(imgOrigin, imgBird, &transformation, CV_INTER_CUBIC | CV_WARP_INVERSE_MAP, cvScalarAll(0));
}

static void MatrixMultiply(float *a, float *b, float *result, int col1, int col2, int row2){
	int i, j, k;
	for (i = 0; i < col1; i++){
		for (j = 0; j < row2; j++){
			result[i * row2 + j] = 0;
		}
	}
	for (i = 0; i < col1; i++){
		for (j = 0; j < row2; j++){
			for (k = 0; k < col2; k++){
				result[i * row2 + j] += a[i * col2 + k] * b[k * row2 + j];
			}
		}
	}
}

static void lineDetection(IplImage* imgBird, IplImage* imgGray, IplImage* imgYUV) {
	int lineX[10000], lineY[10000], lineIdx;
	int base[10] = { 0, };
	int baseCount = birdEyeView(imgGray, imgBird, imgYUV, base);
	int i;
	/*for (i = 0; i < baseCount; i++) {
	lineIdx = slidingWindow(imgBird, base[i],lineX,lineY);
	float curves = polyFit(imgBird, lineX, lineY, lineIdx);
	printf("%f", curves);
	}*/


	float intercept[10] = { 0, };
	int idxx = 0;
	for (i = 0; i < baseCount; i++) {
		lineIdx = slidingWindow(imgGray, base[i], lineX, lineY);
		//intercept[idxx] = polyFit(imgGray, lineX, lineY, lineIdx);
		//printf("%d %f\n", i, intercept[idxx++]);
		//if (idxx == 2) {
		//	float cal = (intercept[0] + intercept[1]) / 2;
		//	printf("%f\n", cal - ((320*XM_PER_PIX) / 2.0));
		//}
	}
}

static int birdEyeView(IplImage* imgGray, IplImage* imgBird, IplImage* imgYUV, int* base) {
	int hit[320] = { 0, };
	int i, j, baseCount = 0;
	cvCvtColor(imgBird, imgYUV, CV_BGR2YUV);
	for (i = 0; i < 240; i++) {
		for (j = 0; j < 320; j++) {
			if ((imgBird->imageData[(i * 320 + j) * 3 + 0] > 90) && (imgBird->imageData[(i * 320 + j) * 3 + 1] < 0) && (imgBird->imageData[(i * 320 + j) * 3 + 1] > -80)) {
				imgGray->imageData[i * 320 + j] = 0;
				hit[j]++;
			}
			else {
				imgGray->imageData[i * 320 + j] = 255;
			}
		}
	}

	for (i = 0; i < 320; i++) {
		while (hit[i] && i < 320) {
			if (30<hit[i] && hit[base[baseCount]] < hit[i])
				base[baseCount] = i;
			i++;
		}
		if (base[baseCount])baseCount++;
	}
	return baseCount;
}

static int nonZero(IplImage* imgGray, int top, int bottom, int left, int right, int *tempX, int *tempY) {
	int i, j;
	int tempIdx = 0;
	for (i = top; i < bottom; i++) {
		for (j = left; j < right; j++) {
			if (imgGray->imageData[i * 320 + j] == 0) {
				imgGray->imageData[i * 240 + j] = 125;
				tempX[tempIdx] = j;
				tempY[tempIdx++] = i;
			}
		}
	}
	return tempIdx;
}

static int slidingWindow(IplImage* imgGray, int base, int *lineX, int *lineY) {
	int tempX[1000], tempY[1000];
	int i, j, lineIdx = 0;
	for (j = 0; j < WINDOW_COUNT; j++) {
		int top = 240 - (240 / WINDOW_COUNT)*(j + 1);
		int bottom = 240 - (240 / WINDOW_COUNT)*j;
		int left = (base - MARGIN > 0) ? base - MARGIN : 0;
		int right = (base + MARGIN < 320) ? base + MARGIN : 320;

		int line = nonZero(imgGray, top, bottom, left, right, tempX, tempY);
		int meanX = 0;

		cvRectangle(imgGray, cvPoint(left, top), cvPoint(right, bottom), cvScalar(128,0,0,0), 3, 0, 0);

		if (line > MIN_PIXEL) {
			for (i = 0; i < line; i++) {
				meanX += tempX[i];
			}
			base = (fmod(meanX, line) >= 0.5) ? meanX / line + 1 : meanX / line;
		}

		for (i = 0; i < line; i++) {
			lineX[lineIdx] = tempX[i];
			lineY[lineIdx++] = tempY[i];
		}
	}
	return lineIdx;
}

static void SignalHandler(int signal){
	stop = NVMEDIA_TRUE;
	MESSAGE_PRINTF("%d signal received\n", signal);
}

static void GetTime(NvMediaTime *time){
	struct timeval t;
	gettimeofday(&t, NULL);
	time->tv_sec = t.tv_sec;
	time->tv_nsec = t.tv_usec * 1000;
}

static void AddTime(NvMediaTime *time, NvU64 uSec, NvMediaTime *res){
	NvU64 t, newTime;
	t = (NvU64)time->tv_sec * 1000000000LL + (NvU64)time->tv_nsec;
	newTime = t + uSec * 1000LL;
	res->tv_sec = newTime / 1000000000LL;
	res->tv_nsec = newTime % 1000000000LL;
}

static NvBool SubTime(NvMediaTime *time1, NvMediaTime *time2){
	NvS64 t1, t2, delta;
	t1 = (NvS64)time1->tv_sec * 1000000000LL + (NvS64)time1->tv_nsec;
	t2 = (NvS64)time2->tv_sec * 1000000000LL + (NvS64)time2->tv_nsec;
	delta = t1 - t2;
	return delta > 0LL;
}

static void DisplayUsage(void){
	printf("Usage : nvmedia_capture [options]\n");
	printf("Brief: Displays this help if no arguments are given. Engages the respective capture module whenever a single \'c\' or \'v\' argument is supplied using default values for the missing parameters.\n");
	printf("Options:\n");
	printf("-va <aspect ratio>    VIP aspect ratio (default = 1.78 (16:9))\n");
	printf("-vmr <width>x<height> VIP mixer resolution (default 800x480)\n");
	printf("-vf <file name>       VIP output file name; default = off\n");
	printf("-vt [seconds]         VIP capture duration (default = 10 secs); overridden by -vn; default = off\n");
	printf("-vn [frames]          # VIP frames to be captured (default = 300); default = on if -vt is not used\n");
}

static int ParseOptions(int argc, char *argv[], TestArgs *args){
	int i = 1;

	// Set defaults if necessary - TBD
	args->i2cDevice = I2C4;     // i2c chnnel

	args->vipDeviceInUse = AnalogDevices_ADV7182;
	args->vipInputtVideoStd = NVMEDIA_VIDEO_CAPTURE_INTERFACE_FORMAT_VIP_NTSC;
	args->vipInputWidth = 720;
	args->vipInputHeight = 480;
	args->vipAspectRatio = 0.0f;

	args->vipMixerWidth = 800;
	args->vipMixerHeight = 480;

	args->vipDisplayEnabled = NVMEDIA_FALSE;
	args->vipOutputType = NvMediaVideoOutputType_OverlayYUV;
	args->vipOutputDevice[0] = NvMediaVideoOutputDevice_LVDS;
	args->vipFileDumpEnabled = NVMEDIA_FALSE;
	args->vipOutputFileName = NULL;

	args->vipCaptureTime = 0;
	args->vipCaptureCount = 0;

	if (i < argc && argv[i][0] == '-')
	{
		while (i < argc && argv[i][0] == '-')
		{
			if (i > 1 && argv[i][1] == '-')
			{
				MESSAGE_PRINTF("Using basic and custom options together is not supported\n");
				return 0;
			}

			// Get options
			if (!strcmp(argv[i], "-va"))
			{
				if (++i < argc)
				{
					if (sscanf(argv[i], "%f", &args->vipAspectRatio) != 1 || args->vipAspectRatio <= 0.0f) // TBC
					{
						MESSAGE_PRINTF("Bad VIP aspect ratio: %s\n", argv[i]);
						return 0;
					}
				}
				else
				{
					MESSAGE_PRINTF("Missing VIP aspect ratio\n");
					return 0;
				}
			}
			else if (!strcmp(argv[i], "-vmr"))
			{
				if (++i < argc)
				{
					if (sscanf(argv[i], "%ux%u", &args->vipMixerWidth, &args->vipMixerHeight) != 2)
					{
						MESSAGE_PRINTF("Bad VIP mixer resolution: %s\n", argv[i]);
						return 0;
					}
				}
				else
				{
					MESSAGE_PRINTF("Missing VIP mixer resolution\n");
					return 0;
				}
			}
			else if (!strcmp(argv[i], "-vf"))
			{
				args->vipFileDumpEnabled = NVMEDIA_TRUE;
				if (++i < argc)
					args->vipOutputFileName = argv[i];
				else
				{
					MESSAGE_PRINTF("Missing VIP output file name\n");
					return 0;
				}
			}
			else if (!strcmp(argv[i], "-vt"))
			{
				if (++i < argc)
					if (sscanf(argv[i], "%u", &args->vipCaptureTime) != 1)
					{
						MESSAGE_PRINTF("Bad VIP capture duration: %s\n", argv[i]);
						return 0;
					}
			}
			else if (!strcmp(argv[i], "-vn"))
			{
				if (++i < argc)
					if (sscanf(argv[i], "%u", &args->vipCaptureCount) != 1)
					{
						MESSAGE_PRINTF("Bad VIP capture count: %s\n", argv[i]);
						return 0;
					}
			}
			else
			{
				MESSAGE_PRINTF("%s is not a supported option\n", argv[i]);
				return 0;
			}

			i++;
		}
	}

	if (i < argc)
	{
		MESSAGE_PRINTF("%s is not a supported option\n", argv[i]);
		return 0;
	}

	// Check for consistency
	if (i < 2)
	{
		DisplayUsage();
		return 0;
	}

	if (args->vipAspectRatio == 0.0f)
		args->vipAspectRatio = 1.78f;

	if (!args->vipDisplayEnabled && !args->vipFileDumpEnabled)
		args->vipDisplayEnabled = NVMEDIA_TRUE;

	if (!args->vipCaptureTime && !args->vipCaptureCount)
		args->vipCaptureCount = 300;
	else if (args->vipCaptureTime && args->vipCaptureCount)
		args->vipCaptureTime = 0;

	return 1;
}

static int DumpFrame(FILE *fout, NvMediaVideoSurface *surf){
	NvMediaVideoSurfaceMap surfMap;
	unsigned int width, height;

	if (NvMediaVideoSurfaceLock(surf, &surfMap) != NVMEDIA_STATUS_OK)
	{
		MESSAGE_PRINTF("NvMediaVideoSurfaceLock() failed in DumpFrame()\n");
		return 0;
	}

	width = surf->width;
	height = surf->height;

	unsigned char *pY[2] = { surfMap.pY, surfMap.pY2 };
	unsigned char *pU[2] = { surfMap.pU, surfMap.pU2 };
	unsigned char *pV[2] = { surfMap.pV, surfMap.pV2 };
	unsigned int pitchY[2] = { surfMap.pitchY, surfMap.pitchY2 };
	unsigned int pitchU[2] = { surfMap.pitchU, surfMap.pitchU2 };
	unsigned int pitchV[2] = { surfMap.pitchV, surfMap.pitchV2 };
	unsigned int i, j;

	for (i = 0; i < 2; i++)
	{
		for (j = 0; j < height / 2; j++)
		{
			fwrite(pY[i], width, 1, fout);
			pY[i] += pitchY[i];
		}
		for (j = 0; j < height / 2; j++)
		{
			fwrite(pU[i], width / 2, 1, fout);
			pU[i] += pitchU[i];
		}
		for (j = 0; j < height / 2; j++)
		{
			fwrite(pV[i], width / 2, 1, fout);
			pV[i] += pitchV[i];
		}
	}


	NvMediaVideoSurfaceUnlock(surf);

	return 1;
}

static int Frame2Ipl(IplImage* img){
	NvMediaVideoSurfaceMap surfMap;
	unsigned int resWidth, resHeight;
	int r, g, b;
	unsigned char y, u, v;
	int num;

	if (NvMediaVideoSurfaceLock(capSurf, &surfMap) != NVMEDIA_STATUS_OK)
	{
		MESSAGE_PRINTF("NvMediaVideoSurfaceLock() failed in Frame2Ipl()\n");
		return 0;
	}

	unsigned char *pY[2] = { surfMap.pY, surfMap.pY2 };
	unsigned char *pU[2] = { surfMap.pU, surfMap.pU2 };
	unsigned char *pV[2] = { surfMap.pV, surfMap.pV2 };
	unsigned int pitchY[2] = { surfMap.pitchY, surfMap.pitchY2 };
	unsigned int pitchU[2] = { surfMap.pitchU, surfMap.pitchU2 };
	unsigned int pitchV[2] = { surfMap.pitchV, surfMap.pitchV2 };
	unsigned int i, j, k, x;
	unsigned int stepY, stepU, stepV;

	resWidth = RESIZE_WIDTH;
	resHeight = RESIZE_HEIGHT;

	// Frame2Ipl
	img->nSize = 112;
	img->ID = 0;
	img->nChannels = 3;
	img->alphaChannel = 0;
	img->depth = IPL_DEPTH_8U;    // 8
	img->colorModel[0] = 'R';
	img->colorModel[1] = 'G';
	img->colorModel[2] = 'B';
	img->channelSeq[0] = 'B';
	img->channelSeq[1] = 'G';
	img->channelSeq[2] = 'R';
	img->dataOrder = 0;
	img->origin = 0;
	img->align = 4;
	img->width = resWidth;
	img->height = resHeight;
	img->imageSize = resHeight*resWidth * 3;
	img->widthStep = resWidth * 3;
	img->BorderMode[0] = 0;
	img->BorderMode[1] = 0;
	img->BorderMode[2] = 0;
	img->BorderMode[3] = 0;
	img->BorderConst[0] = 0;
	img->BorderConst[1] = 0;
	img->BorderConst[2] = 0;
	img->BorderConst[3] = 0;

	stepY = 0;
	stepU = 0;
	stepV = 0;
	i = 0;

	for (j = 0; j < resHeight; j++)
	{
		for (k = 0; k < resWidth; k++)
		{
			x = ResTableX_720To320[k];
			y = pY[i][stepY + x];
			u = pU[i][stepU + x / 2];
			v = pV[i][stepV + x / 2];

			// YUV to RGB
			r = y + 1.4075*(v - 128);
			g = y - 0.34455*(u - 128) - 0.7169*(v - 128);
			b = y + 1.779*(u - 128);

			r = r>255 ? 255 : r<0 ? 0 : r;
			g = g>255 ? 255 : g<0 ? 0 : g;
			b = b>255 ? 255 : b<0 ? 0 : b;

			num = 3 * k + 3 * resWidth*(j);
			img->imageData[num] = b;
			img->imageData[num + 1] = g;
			img->imageData[num + 2] = r;
		}
		stepY += pitchY[i];
		stepU += pitchU[i];
		stepV += pitchV[i];
	}


	NvMediaVideoSurfaceUnlock(capSurf);

	return 1;
}

static unsigned int CaptureThread(void *params){
	int i = 0;
	NvU64 stime, ctime;
	NvMediaTime t1 = { 0 }, t2 = { 0 }, st = { 0 }, ct = { 0 };
	CaptureContext *ctx = (CaptureContext *)params;
	NvMediaVideoSurface *releaseList[4] = { NULL }, **relList;
	NvMediaRect primarySrcRect;
	NvMediaPrimaryVideo primaryVideo;

	primarySrcRect.x0 = 0;
	primarySrcRect.y0 = 0;
	primarySrcRect.x1 = ctx->inputWidth;
	primarySrcRect.y1 = ctx->inputHeight;

	primaryVideo.next = NULL;
	primaryVideo.previous = NULL;
	primaryVideo.previous2 = NULL;
	primaryVideo.srcRect = &primarySrcRect;
	primaryVideo.dstRect = NULL;


	NvSemaphoreDecrement(ctx->semStart, NV_TIMEOUT_INFINITE);

	if (ctx->timeNotCount)
	{
		GetTime(&t1);
		AddTime(&t1, ctx->last * 1000000LL, &t1);
		GetTime(&t2);
		printf("timeNotCount\n");
	}
	GetTime(&st);
	stime = (NvU64)st.tv_sec * 1000000000LL + (NvU64)st.tv_nsec;

	while ((ctx->timeNotCount ? (SubTime(&t1, &t2)) : ((unsigned int)i < ctx->last)) && !stop)
	{
		GetTime(&ct);
		ctime = (NvU64)ct.tv_sec * 1000000000LL + (NvU64)ct.tv_nsec;
		//printf("frame=%3d, time=%llu.%09llu[s] \n", i, (ctime-stime)/1000000000LL, (ctime-stime)%1000000000LL);

		pthread_mutex_lock(&mutex);            // for ControlThread()

		if (!(capSurf = NvMediaVideoCaptureGetFrame(ctx->capture, ctx->timeout)))
		{ // TBD
			MESSAGE_PRINTF("NvMediaVideoCaptureGetFrame() failed in %sThread\n", ctx->name);
			stop = NVMEDIA_TRUE;
			break;
		}

		if (i % 3 == 0)                        // once in three loop = 10 Hz
			pthread_cond_signal(&cond);        // ControlThread() is called

		pthread_mutex_unlock(&mutex);        // for ControlThread()

		primaryVideo.current = capSurf;
		primaryVideo.pictureStructure = NVMEDIA_PICTURE_STRUCTURE_TOP_FIELD;

		if (NVMEDIA_STATUS_OK != NvMediaVideoMixerRender(ctx->mixer, // mixer
			NVMEDIA_OUTPUT_DEVICE_0, // outputDeviceMask
			NULL, // background
			&primaryVideo, // primaryVideo
			NULL, // secondaryVideo
			NULL, // graphics0
			NULL, // graphics1
			releaseList, // releaseList
			NULL)) // timeStamp
		{ // TBD
			MESSAGE_PRINTF("NvMediaVideoMixerRender() failed for the top field in %sThread\n", ctx->name);
			stop = NVMEDIA_TRUE;
		}

		primaryVideo.pictureStructure = NVMEDIA_PICTURE_STRUCTURE_BOTTOM_FIELD;
		if (NVMEDIA_STATUS_OK != NvMediaVideoMixerRender(ctx->mixer, // mixer
			NVMEDIA_OUTPUT_DEVICE_0, // outputDeviceMask
			NULL, // background
			&primaryVideo, // primaryVideo
			NULL, // secondaryVideo
			NULL, // graphics0
			NULL, // graphics1
			releaseList, // releaseList
			NULL)) // timeStamp
		{ // TBD
			MESSAGE_PRINTF("NvMediaVideoMixerRender() failed for the bottom field in %sThread\n", ctx->name);
			stop = NVMEDIA_TRUE;
		}

		if (ctx->fileDumpEnabled)
		{
			if (!DumpFrame(ctx->fout, capSurf))
			{ // TBD
				MESSAGE_PRINTF("DumpFrame() failed in %sThread\n", ctx->name);
				stop = NVMEDIA_TRUE;
			}

			if (!ctx->displayEnabled)
				releaseList[0] = capSurf;
		}

		relList = &releaseList[0];

		while (*relList)
		{
			if (NvMediaVideoCaptureReturnFrame(ctx->capture, *relList) != NVMEDIA_STATUS_OK)
			{ // TBD
				MESSAGE_PRINTF("NvMediaVideoCaptureReturnFrame() failed in %sThread\n", ctx->name);
				stop = NVMEDIA_TRUE;
				break;
			}
			relList++;
		}

		if (ctx->timeNotCount)
			GetTime(&t2);

		i++;
	} // while end

	// Release any left-over frames

	if (ctx->displayEnabled && capSurf)
	{
		NvMediaVideoMixerRender(ctx->mixer, // mixer
			NVMEDIA_OUTPUT_DEVICE_0, // outputDeviceMask
			NULL, // background
			NULL, // primaryVideo
			NULL, // secondaryVideo
			NULL, // graphics0
			NULL, // graphics1
			releaseList, // releaseList
			NULL); // timeStamp

		relList = &releaseList[0];

		while (*relList)
		{
			if (NvMediaVideoCaptureReturnFrame(ctx->capture, *relList) != NVMEDIA_STATUS_OK)
				MESSAGE_PRINTF("NvMediaVideoCaptureReturnFrame() failed in %sThread\n", ctx->name);

			relList++;
		}
	}

	NvSemaphoreIncrement(ctx->semDone);
	return 0;
}

static void CheckDisplayDevice(NvMediaVideoOutputDevice deviceType, NvMediaBool *enabled, unsigned int *displayId){
	int outputDevices;
	NvMediaVideoOutputDeviceParams *outputParams;
	int i;

	// By default set it as not enabled (initialized)
	*enabled = NVMEDIA_FALSE;
	*displayId = 0;

	// Get the number of devices
	if (NvMediaVideoOutputDevicesQuery(&outputDevices, NULL) != NVMEDIA_STATUS_OK) {
		return;
	}

	// Allocate memory for information for all devices
	outputParams = malloc(outputDevices * sizeof(NvMediaVideoOutputDeviceParams));
	if (!outputParams) {
		return;
	}

	// Get device information for acll devices
	if (NvMediaVideoOutputDevicesQuery(&outputDevices, outputParams) != NVMEDIA_STATUS_OK) {
		free(outputParams);
		return;
	}

	// Find desired device
	for (i = 0; i < outputDevices; i++) {
		if ((outputParams + i)->outputDevice == deviceType) {
			// Return information
			*enabled = (outputParams + i)->enabled;
			*displayId = (outputParams + i)->displayId;
			break;
		}
	}

	// Free information memory
	free(outputParams);
}

int ParkingAreaCheck(void){
	const static int verticalParkingMax = 1200; 
	const static int verticalParkingMin = 500; 
	const static int parallelParkingMax = 2400; 
	const static int parallelParkingMin = 1000; 
	const static int obstacleDistance = 1000; 
	const static int spaceDistance = 130; 
	const static int defaultParkingSpace = 2000; 

	int sensorValue = 0;
	int remainDistance = 0;
	int resultDistance = 0;

	sensorValue = DistanceSensor(6);
	if (!obstacleFlag&&sensorValue >= obstacleDistance)
	{
		obstacleFlag = 1;
	}
	
	if (!spaceFlag&&sensorValue >= spaceDistance && sensorValue < obstacleDistance)
	{
		spaceFlag = 1;
	}

	if (obstacleFlag == 1 && spaceFlag == 1)
	{
		printf("check parking Area\n");
		Winker_Write(ALL_ON);
		DesireSpeed_Write(SEARCH_AREA_SPEED);
		PositionControlOnOff_Write(CONTROL);
		EncoderCounter_Write(0);
		DesireEncoderCount_Write(defaultParkingSpace);

		while (sensorValue < obstacleDistance)
		{
			sensorValue = DistanceSensor(6);
			remainDistance = DesireEncoderCount_Read();
			resultDistance = defaultParkingSpace - remainDistance;
			printf("sansorvalue : %d remain : %d result : %d \n",sensorValue,remainDistance,resultDistance);
			usleep(5);
		}
		Winker_Write(ALL_OFF);
		PositionControlOnOff_Write(UNCONTROL);
		DesireSpeed_Write(RUN_SPEED);

		if (resultDistance >= verticalParkingMin && resultDistance < verticalParkingMax)
		{
			printf("verticalFlag \n");
			verticalFlag = 1;
			obstacleFlag=0;
			spaceFlag=0;
			return resultDistance;
		}
		else if (resultDistance >= parallelParkingMin && resultDistance < parallelParkingMax)
		{
			printf("parallelFlag \n");
			parallelFlag = 1;
			obstacleFlag=0;
			spaceFlag=0;
			return resultDistance;
		}
		else{
			obstacleFlag=0;
			spaceFlag=0;
			return resultDistance;
		}
	}
}

void *ControlThread(void *unused){
	int i = 0;
	int temp;
	char fileName[30];
	NvMediaTime pt1 = { 0 }, pt2 = { 0 };
	NvU64 ptime1, ptime2;
	struct timespec;

	// cvCreateImage
	IplImage* imgOrigin = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 3);
	IplImage* imgYCrCb = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 3);
	IplImage* imgYUV = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 3);
	IplImage* imgBird = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 3);
	IplImage* imgHSV = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 3);
	IplImage* imgGray = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 1);
	int a, b;
	for (a = 0; a < 240; a++){
		for (b = 0; b < 320; b++){
			imgGray->imageData[a * 320 + b] = 0;
			imgBird->imageData[(a * 320 + b) * 3 + 0] = 0;
			imgBird->imageData[(a * 320 + b) * 3 + 1] = 0;
			imgBird->imageData[(a * 320 + b) * 3 + 2] = 0;
		}
	}
	while (1)
	{
		pthread_mutex_lock(&mutex);
		pthread_cond_wait(&cond, &mutex);

		GetTime(&pt1);
		ptime1 = (NvU64)pt1.tv_sec * 1000000000LL + (NvU64)pt1.tv_nsec;

		Frame2Ipl(imgOrigin); // save image to IplImage structure & resize image from 720x480 to 320x240
		pthread_mutex_unlock(&mutex);
		// TODO : control steering angle based on captured image ---------------
		if (drive_status == DRIVE){
			InversePerspectiveMapping(imgOrigin, imgBird);
			lineDetection(imgBird, imgGray, imgYUV);
			//StopSignChecking(imgHSV, imgGray);
			sprintf(fileName, "captureImage/imgGray%d.png", i);
			cvSaveImage(fileName, imgYUV, 0);
		}
		else if (drive_status == STOP_WAIT_START_SIGNAL2){
			Stop_wait_start_signal2();
		}
		else if (drive_status == STOP_WAIT_START_SIGNAL1){
			cvCvtColor(imgOrigin, imgYCrCb, CV_BGR2YCrCb);
			Stop_wait_start_signal1(imgYCrCb, imgGray);
		}
		// ---------------------------------------------------------------------
		//GetTime(&pt2);
		//ptime2 = (NvU64)pt2.tv_sec * 1000000000LL + (NvU64)pt2.tv_nsec;
		//printf("--------------------------------operation time=%llu.%09llu[s]\n", (ptime2-ptime1)/1000000000LL, (ptime2-ptime1)%1000000000LL);
		i++;
	}
}