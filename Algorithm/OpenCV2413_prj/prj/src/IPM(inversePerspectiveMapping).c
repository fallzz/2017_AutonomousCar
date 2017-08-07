#include <opencv/cv.h>
#include <opencv/highgui.h>
#include <stdio.h>
#include <math.h>

#define PI 3.1415926

int frameWidth = 320;
int frameHeight = 240;
void MatrixMultiply(float **a, float **b, float **result, int col1, int col2, int row2);

int main(void){
	IplImage* imgOrigin = cvLoadImage("imgOrigin0.png", CV_LOAD_IMAGE_UNCHANGED);
	IplImage* imgDest = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 3);
	int alpha_ = 10, beta_ = 90, gamma_ = 90;
	int f_ = 150, dist_ = 150;

	cvNamedWindow("IMG", CV_WINDOW_AUTOSIZE);

	float focalLength, dist, alpha, beta, gamma;

	alpha = ((float)alpha_ - 90) * PI / 180;
	beta = ((float)beta_ - 90) * PI / 180;
	gamma = ((float)gamma_ - 90) * PI / 180;
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
	MatrixMultiply(RX, RY, RXRY, 4, 4, 4);
	MatrixMultiply(RXRY, RZ, RXRYRZ, 4, 4, 4);
	float RA1[4][3], TRA1[4][3], transformationData[3][3];
	MatrixMultiply(RXRYRZ, A1, RA1, 4, 4, 3);
	MatrixMultiply(T, RA1, TRA1, 4, 4, 3);
	MatrixMultiply(K, TRA1, transformationData, 3, 4, 3);
	CvMat transformation = cvMat(3, 3, CV_32FC1, transformationData);
	cvWarpPerspective(imgOrigin, imgDest, &transformation, CV_INTER_CUBIC | CV_WARP_INVERSE_MAP, cvScalarAll(0));

	cvShowImage("IMG", imgDest);
	cvWaitKey(0);
	cvDestroyWindow("IMG");
}

void MatrixMultiply(float *a, float *b, float *result, int col1, int col2, int row2){
	for (int i = 0; i < col1; i++){
		for (int j = 0; j < row2; j++){
			result[i * row2 + j] = 0;
		}
	}
	for (int i = 0; i < col1; i++){
		for (int j = 0; j < row2; j++){
			for (int k = 0; k < col2; k++){
				result[i * row2 + j] += a[i * col2 + k] * b[k * row2 + j];
			} 
		}
	}
}