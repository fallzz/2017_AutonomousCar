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

#define CAMERA_ANGLE 1700
#define DRIVE_VELOCITY 80

//DRIVE STATUS FLAG
#define DRIVE 0
#define STOP_WAIT_START_SIGNAL1 1 
#define STOP_WAIT_START_SIGNAL2 2 
#define STOP_FRONT_SENSOR 3
#define STOP_LINE_SIGNAL 4
#define STOP_SIGN_SIGNAL 5
#define LEFT_VERTICAL_PARKING 6
#define LEFT_HORIZONTAL_PARKING 7
#define RIGHT_VERTICAL_PARKING 8
#define RIGHT_HORIZONTAL_PARKING 9

//OVERALL VARIABLES
static char drive_status;
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

//Basic Driving Function
static void start_setting(int velocity);
static void Stop_wait_start_signal1(IplImage* imgYCrCb, IplImage* imgGray);
static void Stop_wait_start_signal2();
static void Stop_line_signal();
static void Stop_sign_singal(IplImage* imgHSV);
static void LineTracing(IplImage* imgHSV, IplImage* imgLine, IplImage* imgBird);
static void Detect_stopLine();
static void Detect_stopSign(IplImage* imgHSV);
static void Parking_Detect();
static void Left_Vertical_Parking();
static void Left_Horizontal_Parking();
static void Right_Vertical_Parking();
static void Right_Horizontal_Parking();
static void Promgram_EXIT_setting();

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

	start_setting(0);

	switch (testArgs.vipDeviceInUse){
	case AnalogDevices_ADV7180:
		break;
	case AnalogDevices_ADV7182:
	{
		CaptureInputConfigParams params;

		params.width = testArgs.vipInputWidth;
		params.height = testArgs.vipInputHeight;
		params.vip.std = testArgs.vipInputtVideoStd;

		if (testutil_capture_input_open(testArgs.i2cDevice, testArgs.vipDeviceInUse, NVMEDIA_TRUE, &handle) < 0){
			goto fail;
		}

		if (testutil_capture_input_configure(handle, &params) < 0){
			goto fail;
		}

		break;
	}
	default:
		goto fail;
	}


	if (!(vipCapture = NvMediaVideoCaptureCreate(testArgs.vipInputtVideoStd, NULL, VIP_BUFFER_SIZE))){
		goto fail;
	}

	if (!(device = NvMediaDeviceCreate())){
		goto fail;
	}

	unsigned int features = 0;

	features |= NVMEDIA_VIDEO_MIXER_FEATURE_VIDEO_SURFACE_TYPE_YV16X2;
	features |= NVMEDIA_VIDEO_MIXER_FEATURE_PRIMARY_VIDEO_DEINTERLACING;
	if (testArgs.vipOutputType != NvMediaVideoOutputType_OverlayYUV)
		features |= NVMEDIA_VIDEO_MIXER_FEATURE_DVD_MIXING_MODE;

	if (!(vipMixer = NvMediaVideoMixerCreate(device, testArgs.vipMixerWidth, testArgs.vipMixerHeight, testArgs.vipAspectRatio, testArgs.vipInputWidth, testArgs.vipInputHeight, 0, 0, 0, 0, 0, 0, features, nullOutputList))){
		goto fail;
	}

	CheckDisplayDevice(testArgs.vipOutputDevice[0], &deviceEnabled, &displayId);

	if ((vipOutput[0] = NvMediaVideoOutputCreate(testArgs.vipOutputType, testArgs.vipOutputDevice[0], NULL, deviceEnabled, displayId, NULL))){
		if (NvMediaVideoMixerBindOutput(vipMixer, vipOutput[0], NVMEDIA_OUTPUT_DEVICE_0) != NVMEDIA_STATUS_OK){
			goto fail;
		}
	}
	else{
		goto fail;
	}

	if (testArgs.vipFileDumpEnabled){
		vipFile = fopen(testArgs.vipOutputFileName, "w");
		if (!vipFile || ferror(vipFile)){
			goto fail;
		}
	}

	if (NvSemaphoreCreate(&vipStartSem, 0, 1) != RESULT_OK) {
		goto fail;
	}

	if (NvSemaphoreCreate(&vipDoneSem, 0, 1) != RESULT_OK) {
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

	if (testArgs.vipCaptureTime){
		vipCtx.timeNotCount = NVMEDIA_TRUE;
		vipCtx.last = testArgs.vipCaptureTime;
	}
	else{
		vipCtx.timeNotCount = NVMEDIA_FALSE;
		vipCtx.last = testArgs.vipCaptureCount;
	}

	if (NvThreadCreate(&vipThread, CaptureThread, &vipCtx, NV_THREAD_PRIORITY_NORMAL) != RESULT_OK){
		goto fail;
	}

	sleep(1);
	NvMediaVideoCaptureStart(vipCapture);
	NvSemaphoreIncrement(vipStartSem);
	pthread_create(&cntThread, NULL, &ControlThread, NULL);
	NvSemaphoreDecrement(vipDoneSem, NV_TIMEOUT_INFINITE);
	err = 0;

fail:
	Promgram_EXIT_setting();
	if (vipThread)   NvThreadDestroy(vipThread);
	if (vipDoneSem)  NvSemaphoreDestroy(vipDoneSem);
	if (vipStartSem) NvSemaphoreDestroy(vipStartSem);
	if (vipFile)     fclose(vipFile);

	if (vipOutput[0]){
		NvMediaVideoMixerUnbindOutput(vipMixer, vipOutput[0], NULL);
		NvMediaVideoOutputDestroy(vipOutput[0]);
	}
	if (vipOutput[1]){
		NvMediaVideoMixerUnbindOutput(vipMixer, vipOutput[1], NULL);
		NvMediaVideoOutputDestroy(vipOutput[1]);
	}

	if (vipMixer) NvMediaVideoMixerDestroy(vipMixer);
	if (device) NvMediaDeviceDestroy(device);
	if (vipCapture){
		NvMediaVideoCaptureDestroy(vipCapture);
		switch (testArgs.vipDeviceInUse) {
		case AnalogDevices_ADV7180:
			break;
		case AnalogDevices_ADV7182:
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
	CameraYServoControl_Write(CAMERA_ANGLE);
	CarLight_Write(FRONT_ON);
	PositionControlOnOff_Write(CONTROL);
	EncoderCounter_Write(0);
	PositionControlOnOff_Write(UNCONTROL);
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
		Winker_Write(ALL_ON);
	}
}

static void Stop_wait_start_signal2(){
	if (++init_count > 20){
		Winker_Write(ALL_OFF);
		init_count = 0;
		drive_status = DRIVE;
		Alarm_Write(ON);
		DesireSpeed_Write(DRIVE_VELOCITY);
	}
}

static void Stop_line_signal(){
	if(init_count++ > 5){

	}
}

static void Stop_sign_singal(IplImage* imgHSV){
	int i, j;
	int count = 0;
	for (i = 30; i < 170; i++){
		for (j = 20; j < 300; j++){
			int now = i * 320 + j;
			if ((uchar)imgHSV->imageData[now * 3 + 2] > 160 && (uchar)imgHSV->imageData[now * 3 + 1] > 50 && (uchar)imgHSV->imageData[now * 3] < 15){
				count++;
			}
		}
	}
	if (count < 2000){
		init_count = 0;
		drive_status = STOP_WAIT_START_SIGNAL2;
		Winker_Write(ALL_ON);
	}
}

static void LineTracing(IplImage* imgHSV, IplImage* imgLine, IplImage* imgBird){
	int i, j;
	for (i = 0; i < 240; i++){
		for (j = 0; j < 320; j++){
			int now = i * 320 + j;
			if (imgHSV->imageData[now * 3] > 20 && imgHSV->imageData[now * 3] < 55){
				imgLine->imageData[i * 320 + j] = 0;
			}
			else{
				imgLine->imageData[i * 320 + j] = 255;
			}
		}
	}

	//MAKE BIRD-EYE-VIEW
	int a = 0;
	for (i = 160; i < 240; i++){
		double left_x = -3./2.*i + 270;
		double right_x = 4./3.*i + 80;
		double gap = (right_x - left_x)/320;
		int height = a++ * 3;
		for (j = 0; j < 320; j++){
			int enterPixel = (int)(left_x + gap * (double)j);
			if (enterPixel < 0 || enterPixel >= 320){
				imgBird->imageData[height * 320 + j] = 255;
				imgBird->imageData[(height + 1) * 320 + j] = 255;
				imgBird->imageData[(height + 2) * 320 + j] = 255;
			}
			else{
				imgBird->imageData[height * 320 + j] = imgLine->imageData[i * 320 + enterPixel];
				imgBird->imageData[(height + 1) * 320 + j] = imgLine->imageData[i * 320 + enterPixel];
				imgBird->imageData[(height + 2) * 320 + j] = imgLine->imageData[i * 320 + enterPixel];
			}
		}
	}
		
	//MORPHOLOGY(CLOSING)
	int filterSize = 3;
	IplConvKernel *convKernel = cvCreateStructuringElementEx(filterSize, filterSize, (filterSize - 1) / 2, (filterSize - 1) / 2, CV_SHAPE_RECT, NULL);
	cvMorphologyEx(imgBird, imgLine, NULL, convKernel, CV_MOP_CLOSE, 2);

	//SLIDING WINDOW
	int left_count = 0;
	int right_count = 0;
	int left_detection = -20;
	int right_detection = -20;
	int left_line_window_y = 200;
	int left_line_window_y_gap = 8;
	int right_line_window_y = 200;
	int right_line_window_y_gap = 8;
	//FIND LEFT
	for (; left_line_window_y >= 0; left_line_window_y -= left_line_window_y_gap){
		int sum = 0;
		for (i = left_line_window_y; i < left_line_window_y + left_line_window_y_gap; i++){
			for (j = 0; j < 100; j++){
				if((unsigned char)imgLine->imageData[i * 320 + j] == 0) sum++;
			}
		}
		if (sum > 50){
			if(left_count++ == 0) left_detection = left_line_window_y;
		}
	}

	//FIND RIGHT
	for (; right_line_window_y >= 0; right_line_window_y -= right_line_window_y_gap){
		int sum = 0;
		for (i = right_line_window_y; i < right_line_window_y + right_line_window_y_gap; i++){
			for (j = 220; j < 320; j++){
				if ((unsigned char)imgLine->imageData[i * 320 + j] == 0) sum++;
			}
		}
		if (sum > 50){
			if(right_count++ == 0) right_detection = right_line_window_y;
		}
	}
	if(right_detection + 8 <= left_detection) SteeringServoControl_Write(1000);
	else if(right_detection >= left_detection + 8) SteeringServoControl_Write(2000);
	else if(left_count > right_count + 2) SteeringServoControl_Write(1300);
	else if(left_count + 2 < right_count) SteeringServoControl_Write(1700);
	else SteeringServoControl_Write(1500);
}

static void Detect_stopLine(){
	int sensorValue = (int)LineSensor_Read();
	int count = 0;
	while(sensorValue != 0){
		if(sensorValue % 2 == 1) count++;
		sensorValue /= 2;
	}
	if(count <= 3){
		DesireSpeed_Write(0);
		drive_status = STOP_LINE_SIGNAL;
		init_count = 0;
		//CameraYServoControl_Write(1500);
		Winker_Write(ALL_ON);
	}
}

static void Detect_stopSign(IplImage* imgHSV){
	int i, j;
	int count = 0;
	for (i = 30; i < 170; i++){
		for (j = 20; j < 300; j++){
			int now = i * 320 + j;
			if ((uchar)imgHSV->imageData[now * 3 + 2] > 160 && (uchar)imgHSV->imageData[now * 3 + 1] > 50 && (uchar)imgHSV->imageData[now * 3] < 15){
				count++;
			}
		}
	}
	if (count > 8000){
		DesireSpeed_Write(0);
		drive_status = STOP_SIGN_SIGNAL;
	}
}

int TWO_SENSOR_STATUS = 0;
int SIX_SENSOR_STATUS = 0;
int stacked_frame = 0;
char PARKING_STATE = 0;
int a=0,sum=0,i=0;
int temp[200];

static void Parking_Detect(){
	int TWO_SENSOR_VALUE = DistanceSensor(2);
	int SIX_SENSOR_VALUE = DistanceSensor(6);
	printf("sensor value : %d\n",SIX_SENSOR_VALUE);
	//Left Parking Detect
	if(SIX_SENSOR_VALUE > 800 && SIX_SENSOR_STATUS == 0){
		printf("sensor value : %d\n",SIX_SENSOR_VALUE);
		SIX_SENSOR_STATUS = 1;
		stacked_frame = 0;
	}
	else if(SIX_SENSOR_VALUE < 800 && SIX_SENSOR_STATUS == 1){
		printf("sensor value : %d\n",SIX_SENSOR_VALUE);
		SIX_SENSOR_STATUS = 2;
	}	
	else if(SIX_SENSOR_VALUE <= 800 && SIX_SENSOR_STATUS == 2){
		stacked_frame++;
		printf("sensor value : %d\n",SIX_SENSOR_VALUE);
		printf("stack frame : %d\n",stacked_frame);
	}
	else if(SIX_SENSOR_VALUE > 800 && SIX_SENSOR_STATUS == 2){
		if(stacked_frame >= 40){
			SIX_SENSOR_STATUS = 0;
		}
		else if(stacked_frame < 23){
			drive_status = LEFT_VERTICAL_PARKING;
			printf("VERTICAL_PARKING\nstack frame : %d\n",stacked_frame);
			/*
			PositionControlOnOff_Write(100);
			//EncoderCounter_Write(0);
			DesireEncoderCount_Write(100);
			while(1)
			{
				printf("DesireEncoderCount_Read() : %d\n",DesireEncoderCount_Read());
				if(DesireEncoderCount_Read() == 0)
					break;
			}
			PositionControlOnOff_Write(0);
			DesireSpeed_Write(-100);
			EncoderCounter_Write(0);
			DesireEncoderCount_Write(100);
			while(1)
			{
				if(DesireEncoderCount_Read() == 0)
					break;
			}
			*/
			/*
			while(1)
			{
				DesireSpeed_Write(100);
				SteeringServoControl_Write(1500);
				SIX_SENSOR_VALUE = DistanceSensor(6);
				if(i==100)
					break;
				else
				{
					temp[i]=SIX_SENSOR_VALUE;
					i++;
				}
				//printf("i : %d\n",i);
				//printf("sensor : %d\n",SIX_SENSOR_VALUE);
				
			}
			while(1)
			{
				DesireSpeed_Write(-100);
				SteeringServoControl_Write(1500);
				SIX_SENSOR_VALUE = DistanceSensor(6);
				if(i==200)
					break;
				else
				{
					temp[i]=SIX_SENSOR_VALUE;
					i++;
				}
				//printf("i : %d\n",i);
				//printf("sensor : %d\n",SIX_SENSOR_VALUE);
			}
			
			for(i=0 ; i<200 ; i++)
			{
				sum+=temp[i];
			}
			SIX_SENSOR_VALUE=sum/200;

			printf("sum : %d\n",SIX_SENSOR_VALUE);
			*/
			if(SIX_SENSOR_VALUE>800&&SIX_SENSOR_VALUE<=850)
				a=6;
			else if(SIX_SENSOR_VALUE>850&&SIX_SENSOR_VALUE<=900)
				a=5;
			else if(SIX_SENSOR_VALUE>900&&SIX_SENSOR_VALUE<=1000)
				a=4;
			else if(SIX_SENSOR_VALUE>1000&&SIX_SENSOR_VALUE<=1100)
				a=3;
			else if(SIX_SENSOR_VALUE>1100&&SIX_SENSOR_VALUE<=1200)
				a=2;
			else if(SIX_SENSOR_VALUE>1200&&SIX_SENSOR_VALUE<=1300)
				a=1;
			else
				a=0;
			printf("a : %d\n",a);
			
			TWO_SENSOR_STATUS = 0;
		}
		else if(stacked_frame < 40){
			drive_status = LEFT_HORIZONTAL_PARKING;
			printf("HORIZONTAL_PARKING\nstack frame : %d\n",stacked_frame);
			TWO_SENSOR_STATUS = 0;
		}
		PARKING_STATE = 0;
		stacked_frame = 0;
	}

	//RIGHT Parking Detect
	if(TWO_SENSOR_VALUE > 1000 && TWO_SENSOR_STATUS == 0){
		TWO_SENSOR_STATUS = 1;
		stacked_frame = 0;
	}
	else if(TWO_SENSOR_VALUE < 1000 && TWO_SENSOR_STATUS == 1){
		TWO_SENSOR_STATUS = 2;
	}
	else if(TWO_SENSOR_VALUE <= 1000 && TWO_SENSOR_STATUS == 2){
		stacked_frame++;
	}
	else if(TWO_SENSOR_VALUE > 1000 && TWO_SENSOR_STATUS == 2){
		if(stacked_frame >= 40){
			TWO_SENSOR_STATUS = 0;
		}
		else if(stacked_frame < 20){
			drive_status = RIGHT_VERTICAL_PARKING;
			SIX_SENSOR_STATUS = 0;
		}
		else if(stacked_frame < 40){
			drive_status = RIGHT_HORIZONTAL_PARKING;
			SIX_SENSOR_STATUS = 0;
		}
		PARKING_STATE = 0;
		stacked_frame = 0;
	}
}

static void Left_Vertical_Parking(){
	int SIX_SENSOR_VALUE = DistanceSensor(6);
	int FOUR_SENSOR_VALUE = DistanceSensor(4);
	if(PARKING_STATE == 0){
		if(stacked_frame == 0){
			DesireSpeed_Write(110);
			SteeringServoControl_Write(1000); // right
		}
		else if(stacked_frame == 22+a){ // 18 -> 24
			DesireSpeed_Write(-100);
			SteeringServoControl_Write(1500); // center
		}
		else if(stacked_frame == 32+a){
			SteeringServoControl_Write(2000); // left
		}
		else if(stacked_frame == 42+a){
			SteeringServoControl_Write(1500); // center
		}
		else if(stacked_frame > 42 && FOUR_SENSOR_VALUE > 2000){
			DesireSpeed_Write(0);
			sleep(1);
			CarLight_Write(ALL_OFF);
			EncoderCounter_Write(0);
			DesireSpeed_Write(0);
			Alarm_Write(ON);
			usleep(1000000);
			Alarm_Write(OFF);
			usleep(1000000);
			DesireSpeed_Write(DRIVE_VELOCITY);
			PARKING_STATE = 1;
			stacked_frame = 0;
		}
		stacked_frame++;
	}
	else{
		if(stacked_frame == 11+a){
			SteeringServoControl_Write(2000);
		}
		else if(stacked_frame == 50+a){
			stacked_frame = -1;
			SteeringServoControl_Write(1500);
			drive_status = DRIVE;
			SIX_SENSOR_STATUS = 0;
		}
		stacked_frame++;
	}
}

static void Left_Horizontal_Parking(){
	int FOUR_SENSOR_VALUE = DistanceSensor(4);
	if(PARKING_STATE == 0){
		if(stacked_frame == 0){
			SteeringServoControl_Write(1400);
		}
		else if(stacked_frame == 15){
			SteeringServoControl_Write(2000);
			DesireSpeed_Write(-80);
		}
		else if(stacked_frame == 35){
			SteeringServoControl_Write(1500);

		}
		else if(stacked_frame == 40){
			SteeringServoControl_Write(1000);
		}
		else if(stacked_frame == 60){
			SteeringServoControl_Write(1500);
			PARKING_STATE = 1;
			stacked_frame = -1;
		}
		stacked_frame++;
	}
	else if (PARKING_STATE == 1){
		if(FOUR_SENSOR_VALUE > 2000){
			DesireSpeed_Write(0);
			sleep(1);
			CarLight_Write(ALL_OFF);
			EncoderCounter_Write(0);
			DesireSpeed_Write(0);
			Alarm_Write(ON);
			usleep(1000000);
			Alarm_Write(OFF);
			usleep(1000000);
			PARKING_STATE = 3;
			stacked_frame = 0;
			DesireSpeed_Write(DRIVE_VELOCITY);
			SteeringServoControl_Write(1000);
		}
	}
	else{
		if(stacked_frame++ == 15+a){
			SteeringServoControl_Write(1500);
		}
		else if(stacked_frame == 35+a){
			SteeringServoControl_Write(2000);
		}
		else if(stacked_frame == 55+a){
			SteeringServoControl_Write(1500);
		}
		else if(stacked_frame == 100+a){
			stacked_frame = 0;
			drive_status = DRIVE;
			SIX_SENSOR_STATUS = 0;
		}
	}
}

static void Right_Vertical_Parking(){
	int TWO_SENSOR_VALUE = DistanceSensor(2);
	int FOUR_SENSOR_VALUE = DistanceSensor(4);
	if(PARKING_STATE == 0){
		if(stacked_frame == 0){
			DesireSpeed_Write(100);
			SteeringServoControl_Write(2000);
		}
		else if(stacked_frame == 23){
			DesireSpeed_Write(-100);
			SteeringServoControl_Write(1500);
		}
		else if(stacked_frame == 32){
			SteeringServoControl_Write(1000);
		}
		else if(stacked_frame == 45){
			SteeringServoControl_Write(1500);
		}
		else if(stacked_frame > 45 && FOUR_SENSOR_VALUE > 2000){
			DesireSpeed_Write(DRIVE_VELOCITY);
			Alarm_Write(ON);
			PARKING_STATE = 1;
			stacked_frame = 0;
		}
		stacked_frame++;
	}
	else{
		if(stacked_frame == 11){
			SteeringServoControl_Write(1000);
		}
		else if(stacked_frame == 60){
			stacked_frame = -1;
			SteeringServoControl_Write(1500);
			drive_status = DRIVE;
			TWO_SENSOR_STATUS = 0;
		}
		stacked_frame++;
	}
}

static void Right_Horizontal_Parking(){
	int FOUR_SENSOR_VALUE = DistanceSensor(4);
	if(PARKING_STATE == 0){
		if(stacked_frame == 0){
			SteeringServoControl_Write(1600);
		}
		else if(stacked_frame == 20){
			SteeringServoControl_Write(1000);
			DesireSpeed_Write(-80);
		}
		else if(stacked_frame == 40){
			SteeringServoControl_Write(1500);

		}
		else if(stacked_frame == 50){
			SteeringServoControl_Write(2000);
		}
		else if(stacked_frame == 80){
			SteeringServoControl_Write(1500);
			PARKING_STATE = 1;
			stacked_frame = -1;
		}
		stacked_frame++;
	}
	else if (PARKING_STATE == 1){
		if(FOUR_SENSOR_VALUE > 2000){
			PARKING_STATE = 3;
			stacked_frame = 0;
			Alarm_Write(ON);
			DesireSpeed_Write(DRIVE_VELOCITY);
			SteeringServoControl_Write(2000);
		}
	}
	else{
		if(stacked_frame++ == 30){
			SteeringServoControl_Write(1500);
		}
		else if(stacked_frame == 35){
			SteeringServoControl_Write(1000);
		}
		else if(stacked_frame == 70){
			SteeringServoControl_Write(1500);
		}
		else if(stacked_frame == 100){
			stacked_frame = 0;
			drive_status = DRIVE;
			TWO_SENSOR_STATUS = 0;
		}
	}
}

static void Promgram_EXIT_setting(){
	DesireSpeed_Write(0);
	CameraYServoControl_Write(1500);
	SteeringServoControl_Write(1500);
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

static int ParseOptions(int argc, char *argv[], TestArgs *args){
	int i = 1;

	args->i2cDevice = I2C4;

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
				return 0;
			}

			// Get options
			if (!strcmp(argv[i], "-va"))
			{
				if (++i < argc)
				{
					if (sscanf(argv[i], "%f", &args->vipAspectRatio) != 1 || args->vipAspectRatio <= 0.0f) // TBC
					{
						return 0;
					}
				}
				else
				{
					return 0;
				}
			}
			else if (!strcmp(argv[i], "-vmr"))
			{
				if (++i < argc)
				{
					if (sscanf(argv[i], "%ux%u", &args->vipMixerWidth, &args->vipMixerHeight) != 2)
					{
						return 0;
					}
				}
				else
				{
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
					return 0;
				}
			}
			else if (!strcmp(argv[i], "-vt"))
			{
				if (++i < argc)
					if (sscanf(argv[i], "%u", &args->vipCaptureTime) != 1)
					{
						return 0;
					}
			}
			else if (!strcmp(argv[i], "-vn"))
			{
				if (++i < argc)
					if (sscanf(argv[i], "%u", &args->vipCaptureCount) != 1)
					{
						return 0;
					}
			}
			else
			{
				return 0;
			}

			i++;
		}
	}

	if (i < argc) return 0;
	if (i < 2) return 0;
	if (args->vipAspectRatio == 0.0f) args->vipAspectRatio = 1.78f;
	if (!args->vipDisplayEnabled && !args->vipFileDumpEnabled) args->vipDisplayEnabled = NVMEDIA_TRUE;
	if (!args->vipCaptureTime && !args->vipCaptureCount) args->vipCaptureCount = 300;
	else if (args->vipCaptureTime && args->vipCaptureCount) args->vipCaptureTime = 0;

	return 1;
}

static int DumpFrame(FILE *fout, NvMediaVideoSurface *surf){
	NvMediaVideoSurfaceMap surfMap;
	unsigned int width, height;

	if (NvMediaVideoSurfaceLock(surf, &surfMap) != NVMEDIA_STATUS_OK) return 0;

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

	if (NvMediaVideoSurfaceLock(capSurf, &surfMap) != NVMEDIA_STATUS_OK) return 0;

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

	if (ctx->timeNotCount){
		GetTime(&t1);
		AddTime(&t1, ctx->last * 1000000LL, &t1);
		GetTime(&t2);
	}
	GetTime(&st);
	stime = (NvU64)st.tv_sec * 1000000000LL + (NvU64)st.tv_nsec;

	while ((ctx->timeNotCount ? (SubTime(&t1, &t2)) : ((unsigned int)i < ctx->last)) && !stop) {
		GetTime(&ct);
		ctime = (NvU64)ct.tv_sec * 1000000000LL + (NvU64)ct.tv_nsec;

		pthread_mutex_lock(&mutex);

		if (!(capSurf = NvMediaVideoCaptureGetFrame(ctx->capture, ctx->timeout))){
			stop = NVMEDIA_TRUE;
			break;
		}

		if (i % 3 == 0) pthread_cond_signal(&cond);

		pthread_mutex_unlock(&mutex);

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
			stop = NVMEDIA_TRUE;
		}

		if (ctx->fileDumpEnabled)
		{
			if (!DumpFrame(ctx->fout, capSurf))
			{ // TBD
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

	*enabled = NVMEDIA_FALSE;
	*displayId = 0;

	if (NvMediaVideoOutputDevicesQuery(&outputDevices, NULL) != NVMEDIA_STATUS_OK) {
		return;
	}

	outputParams = malloc(outputDevices * sizeof(NvMediaVideoOutputDeviceParams));
	if (!outputParams) {
		return;
	}

	if (NvMediaVideoOutputDevicesQuery(&outputDevices, outputParams) != NVMEDIA_STATUS_OK) {
		free(outputParams);
		return;
	}

	for (i = 0; i < outputDevices; i++) {
		if ((outputParams + i)->outputDevice == deviceType) {
			*enabled = (outputParams + i)->enabled;
			*displayId = (outputParams + i)->displayId;
			break;
		}
	}

	free(outputParams);
}

void *ControlThread(void *unused){
	int i = 0;
	int temp;
	char fileName[30];
	NvMediaTime pt1 = { 0 }, pt2 = { 0 };
	NvU64 ptime1, ptime2;
	struct timespec;

	// cvCreateImage
	IplImage* imgOrigin = cvCreateImage(cvSize(RESIZE_WIDTH, RESIZE_HEIGHT), IPL_DEPTH_8U, 3);
	IplImage* imgOriginBlur = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 3);	
	IplImage* imgYCrCb = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 3);
	IplImage* imgHSV = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 3);
	IplImage* imgLine = cvCreateImage(cvSize(320,240), IPL_DEPTH_8U, 1);
	IplImage* imgGray = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 1);

	while (1)
	{
		pthread_mutex_lock(&mutex);
		pthread_cond_wait(&cond, &mutex);
		Frame2Ipl(imgOrigin); // save image to IplImage structure & resize image from 720x480 to 320x240
		pthread_mutex_unlock(&mutex);

		// TODO : control steering angle based on captured image ---------------
		if (drive_status == DRIVE){
			//Detect_stopLine();
			//cvSmooth(imgOrigin, imgOriginBlur, CV_GAUSSIAN, 3, 3, 0., 0.);
			//cvCvtColor(imgOriginBlur, imgHSV, CV_BGR2HSV);
			//LineTracing(imgHSV, imgLine, imgGray);
			//Detect_stopSign(imgHSV);
			Parking_Detect();
			//sprintf(fileName, "captureImage/imgOrigin%d.png", i);
			//cvSaveImage(fileName, imgHSV, 0);
			i++;
		}
		else if(drive_status == STOP_LINE_SIGNAL){
			Stop_line_signal();
			i++;
		}
		else if(drive_status == STOP_SIGN_SIGNAL){
			cvSmooth(imgOrigin, imgOriginBlur, CV_GAUSSIAN, 3, 3, 0., 0.);
			cvCvtColor(imgOriginBlur, imgHSV, CV_BGR2HSV);
			Stop_sign_singal(imgHSV);
		}
		else if(drive_status == LEFT_VERTICAL_PARKING){
			Left_Vertical_Parking();
		}
		else if(drive_status == LEFT_HORIZONTAL_PARKING){
			Left_Horizontal_Parking();
		}
		else if(drive_status == RIGHT_VERTICAL_PARKING){
			Right_Vertical_Parking();
		}
		else if(drive_status == RIGHT_HORIZONTAL_PARKING){
			Right_Horizontal_Parking();
		}
		else if (drive_status == STOP_WAIT_START_SIGNAL2){
			Stop_wait_start_signal2();
		}
		else if (drive_status == STOP_WAIT_START_SIGNAL1){
			cvCvtColor(imgOrigin, imgYCrCb, CV_BGR2YCrCb);
			Stop_wait_start_signal1(imgYCrCb, imgGray);
		}
	}
}
