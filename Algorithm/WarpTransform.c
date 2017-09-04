#include <opencv/highgui.h>
#include <opencv/cv.h>
#include <stdio.h>

int main() {
	char fileName[100];
	int i, j;
	IplImage* imgOrigin = cvLoadImage(fileName, CV_LOAD_IMAGE_UNCHANGED);
	IplImage* imgOriginBlur = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 3);
	IplImage* imgHSV = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 3);
	IplImage* imgLine = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 1);
	IplImage* imgBird = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 1);
	for (int q = 27; q < 70; q++) {
		sprintf(fileName, "../../curveImage/imgOrigin%d.png", q);
		imgOrigin = cvLoadImage(fileName, CV_LOAD_IMAGE_UNCHANGED);
		//LINE DETECTION BASED ON COLOR(HSV-MODEL)
		cvSmooth(imgOrigin, imgOriginBlur, CV_GAUSSIAN, 3, 3, 0., 0.);
		cvCvtColor(imgOriginBlur, imgHSV, CV_BGR2HSV);
		for (i = 0; i < 240; i++){
			for (j = 0; j < 320; j++){
				int now = i * 320 + j;
				if (imgHSV->imageData[now * 3] > 20 && imgHSV->imageData[now * 3] < 40){
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
		int left_detection = -20;
		int right_detection = -20;
		int left_line_window_y = 200;
		int left_line_window_y_gap = 5;
		int right_line_window_y = 200;
		int right_line_window_y_gap = 5;
		//FIND LEFT
		for (; left_line_window_y >= 0; left_line_window_y -= left_line_window_y_gap){
			int sum = 0;
			for (i = left_line_window_y; i < left_line_window_y + 10; i++){
				for (j = 0; j < 40; j++){
					if((unsigned char)imgLine->imageData[i * 320 + j] == 0) sum++;
				}
			}
			if (sum > 100){
				left_detection = left_line_window_y;
				break;
			}
		}

		//FIND RIGHT
		for (; right_line_window_y >= 0; right_line_window_y -= right_line_window_y_gap){
			int sum = 0;
			for (i = right_line_window_y; i < right_line_window_y + 10; i++){
				for (j = 280; j < 320; j++){
					if ((unsigned char)imgLine->imageData[i * 320 + j] == 0) sum++;
				}
			}
			if (sum > 100){
				right_detection = right_line_window_y;
				break;
			}
		}
		if (left_detection > right_detection + 5){
			printf("우회줜\n");
		}
		else if (right_detection > left_detection + 5){
			printf("좌회줜\n");
		}
		else{
			printf("직진\n");
		}

		cvNamedWindow("Original", CV_WINDOW_AUTOSIZE);
		cvNamedWindow("Line", CV_WINDOW_AUTOSIZE);
		cvShowImage("Original", imgOrigin); 
		cvShowImage("HSV", imgHSV);
		cvShowImage("Line", imgLine);
		cvWaitKey(0);
		cvDestroyWindow("Original");
		cvDestroyWindow("Line");
	}
}