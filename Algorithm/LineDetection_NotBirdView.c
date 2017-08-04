#include <opencv/cv.h>
#include <opencv/highgui.h>
#include <stdio.h>

int main(void){
	IplImage* imgOrigin = cvLoadImage("imgOrigin0.png", CV_LOAD_IMAGE_UNCHANGED);
	IplImage* imgHSV = cvCreateImage(cvGetSize(imgOrigin), IPL_DEPTH_8U, 3);
	IplImage* imgLine = cvCreateImage(cvSize(320,40), IPL_DEPTH_8U, 1);
	cvCvtColor(imgOrigin, imgHSV, CV_BGR2HSV);
	int i, j;
	int lineData[90];

	unsigned long long datasum = 0;
	for (i = 200; i < 240; i++){
		unsigned long long left_center_LINE = 0;
		unsigned long long right_center_LINE = 0;
		int left_count = 0;
		int right_count = 0;
		for (j = 0; j < 320; j++){
			int now = i * 320 + j;
			imgLine->imageData[(i - 200) * 320 + j] = 255;
			if (imgHSV->imageData[now * 3] > 30 && imgHSV->imageData[now * 3] < 40){
				if (j < 160){
					left_center_LINE += j;
					left_count++;
				}
				else{
					right_center_LINE += j;
					right_count++;
				}
				imgLine->imageData[(i - 200) * 320 + j] = 0;
			}
		}
		if (left_count == 0 && right_count == 0){
			lineData[i - 200] = 160;
		}
		else if (left_count == 0){
			lineData[i - 200] = (int)(right_center_LINE / right_count) / 2;
		}
		else if (right_count == 0){
			lineData[i - 200] = (int)(left_center_LINE / left_count + 319) / 2;
		}
		else{
			lineData[i - 200] = (int)(left_center_LINE / left_count + right_center_LINE / right_count) / 2;
		}
		imgLine->imageData[(i - 200) * 320 + lineData[i - 200]] = 0;
		datasum += lineData[i - 200];
	}
	datasum /= 90;
	//if (datasum < 130){
	//	SteeringServoControl_Write(1400);
	//}
	//else if (datasum > 170){
	//	SteeringServoControl_Write(1600);
	//}
	//else{
	//	SteeringServoControl_Write(1500);
	//}
	cvNamedWindow("IMG", CV_WINDOW_AUTOSIZE);
	cvShowImage("IMG", imgLine);
	cvWaitKey(0);
	cvDestroyWindow("IMG");
}