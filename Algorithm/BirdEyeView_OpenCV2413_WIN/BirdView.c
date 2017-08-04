#include <opencv/highgui.h>
#include <opencv/cv.h>
#include <stdio.h>

int main(){
	IplImage* imgOrigin = cvLoadImage("imgOrigin0.png", CV_LOAD_IMAGE_UNCHANGED);
	IplImage* imgGray = cvCreateImage(cvGetSize(imgOrigin), IPL_DEPTH_8U, 1);
	IplImage* imgYCrCb = cvCreateImage(cvGetSize(imgOrigin), IPL_DEPTH_8U, 3);
	IplImage* imgBird = cvCreateImage(cvSize(111,61), IPL_DEPTH_8U, 3);
	imgBird->widthStep = 111 * 3;

	cvCvtColor(imgOrigin, imgGray, CV_BGR2GRAY);

	for (int i = 0; i <= 60; i++){
		double gap = (109. + 10. / 3. * i) / 111.;
		for (int j = 0; j <= 110; j++){
			int x = 120 - 2 * i + (int)(gap * j);
			int y = i + 70;
			imgBird->imageData[i * 111 * 3 + j * 3] = imgOrigin->imageData[y * 320 * 3 + x * 3];
			imgBird->imageData[i * 111 * 3 + j * 3 + 1] = imgOrigin->imageData[y * 320 * 3 + x * 3 + 1];
			imgBird->imageData[i * 111 * 3 + j * 3 + 2] = imgOrigin->imageData[y * 320 * 3 + x * 3 + 2];
		}
	}

	//cvCvtColor(imgBird, imgBird_GRAY, CV_BGR2GRAY);
	cvNamedWindow("Example", CV_WINDOW_AUTOSIZE);
	cvShowImage("Example", imgOrigin);
	cvShowImage("Example2", imgBird);
	cvWaitKey(0);
	cvDestroyWindow("Example");
	return 0;
}