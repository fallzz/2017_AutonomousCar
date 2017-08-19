#include <opencv/highgui.h>
#include <opencv/cv.h>
#include <stdio.h>

int main() {
	char fileName[100];
	int i, j;
	IplImage* imgHSV = cvLoadImage(fileName, CV_LOAD_IMAGE_UNCHANGED);
	IplImage* imgSign = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 1);
	sprintf(fileName, "image/imgHSV7.png");
	imgHSV = cvLoadImage(fileName, CV_LOAD_IMAGE_UNCHANGED);

	int count = 0;

	for (i = 30; i < 170; i++){
		for (j = 20; j < 300; j++){
			int now = i * 320 + j;
			if ((uchar)imgHSV->imageData[now * 3 + 2] > 160 && (uchar)imgHSV->imageData[now * 3 + 1] > 50 && (uchar)imgHSV->imageData[now * 3] < 15){
				count++;
			}
		}
	}
	if (count > 10000) /* Á¤Áö */;

	cvNamedWindow("HSV", CV_WINDOW_AUTOSIZE);
	cvNamedWindow("Sign", CV_WINDOW_AUTOSIZE);
	cvShowImage("HSV", imgHSV);
	cvShowImage("Sign", imgSign);
	cvWaitKey(0);
	cvDestroyWindow("HSV");
	cvDestroyWindow("Sign");
}