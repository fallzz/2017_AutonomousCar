#include <opencv/highgui.h>
#include <opencv/cv.h>
#include <stdio.h>

#define WINDOW_COUNT 15
#define MARGIN 25
#define BIRD_WIDTH 240
#define BIRD_HEIGHT 180
#define MIN_PIXEL 50

#define YM_PER_PIX 30/BIRD_HEIGHT
#define XM_PER_PIX 3.7/BIRD_WIDTH
#define PI 3.1415926

static unsigned int img2bird[180][240][2];

static void InversePerspectiveMapping(IplImage* imgOrigin, IplImage* imgDest);
static int nonZero(IplImage** imgBird, int top, int bottom, int left, int right, int *tempX, int *tempY);
static int birdEyeView(IplImage** imgBird, IplImage** imgOrigin, int *base);
static void lineDetection(IplImage** imgBird, IplImage** imgOrigin);
static int slidingWindow(IplImage** imgBird, int base, int *lineX, int *lineY);
static float polyFit(IplImage** imgBird, int *lineX, int *lineY,int lineIdx);
static void MatrixMultiply(float *a, float *b, float *result, int col1, int col2, int row2);


int main() {
	char fileName[100];
	for (int q = 0; q < 6; q++) {

		sprintf(fileName, "image/imgOrigin%d.png", q);
		IplImage* imgOrigin = cvLoadImage(fileName, CV_LOAD_IMAGE_UNCHANGED);
		IplImage* imgBird = cvCreateImage(cvSize(BIRD_WIDTH, BIRD_HEIGHT), IPL_DEPTH_8U, 3);
		IplImage* imgDest = cvCreateImage(cvSize(500, 500), IPL_DEPTH_8U, 3);

		int i, j;
		float gap;
		InversePerspectiveMapping(imgOrigin, imgDest);
		for (i = 0; i < 180; i++) {
			gap = (109. + 210. / 179. * i) / 240.;
			int y = (int)(60. / 179. * i) + 70;
			for (j = 0; j < 240; j++) {
				img2bird[i][j][0] = 120 - (int)(120. / 179. * i) + (int)(gap * j);
				img2bird[i][j][1] = y;
			}
		}
		lineDetection(&imgBird, &imgOrigin);

		cvNamedWindow("Example", CV_WINDOW_AUTOSIZE);
		cvNamedWindow("Example2", CV_WINDOW_AUTOSIZE);
		cvNamedWindow("Example3", CV_WINDOW_AUTOSIZE);
		cvShowImage("Example", imgOrigin);
		cvShowImage("Example2", imgBird);
		cvShowImage("Example3", imgDest);
		cvWaitKey(0); 
		cvDestroyWindow("Example3");
	}
	return 0;
}
static void lineDetection(IplImage** imgBird, IplImage** imgOrigin) {
	int lineX[10000], lineY[10000], lineIdx;
	int base[10] = { 0, };
	int baseCount = birdEyeView(imgBird, imgOrigin, base);
	int i;
	/*for (i = 0; i < baseCount; i++) {
	lineIdx = slidingWindow(imgBird, base[i],lineX,lineY);
	float curves = polyFit(imgBird, lineX, lineY, lineIdx);
	printf("%f", curves);
	}*/
	float intercept[10] = { 0, };
	int idxx = 0;
	for (i = 0; i < baseCount; i++) {
		lineIdx = slidingWindow(imgBird, base[i], lineX, lineY);
		intercept[idxx] = polyFit(imgBird, lineX, lineY, lineIdx);
		printf("%d %f\n", i, intercept[idxx++]);

		if (idxx == 2) {
			float cal = (intercept[0] + intercept[1]) / 2;
			printf("%f\n", cal - ((BIRD_WIDTH*XM_PER_PIX) / 2.0));
		}
	}
}
static int birdEyeView(IplImage** imgBird, IplImage** imgOrigin, int *base) {
	int hit[BIRD_WIDTH] = { 0, };
	int i, j, x, y, baseCount = 0;

	for (i = 0; i < BIRD_HEIGHT; i++) {
		for (j = 0; j < 240; j++) {
			x = img2bird[i][j][0];
			y = img2bird[i][j][1];
			if ((unsigned char)((*imgOrigin)->imageData[y * 320 * 3 + x * 3 + 0]) > 0 && (unsigned char)((*imgOrigin)->imageData[y * 320 * 3 + x * 3 + 1]) >= 155 && (unsigned char)((*imgOrigin)->imageData[y * 320 * 3 + x * 3 + 2]) >= 155) {
				(unsigned char)((*imgBird)->imageData[i * 240 * 3 + j * 3 + 0]) = 0;
				(unsigned char)((*imgBird)->imageData[i * 240 * 3 + j * 3 + 1]) = 0;
				(unsigned char)((*imgBird)->imageData[i * 240 * 3 + j * 3 + 2]) = 0;
				hit[j]++;
			}
			else {
				(unsigned char)((*imgBird)->imageData[i * 240 * 3 + j * 3 + 0]) = 255;
				(unsigned char)((*imgBird)->imageData[i * 240 * 3 + j * 3 + 1]) = 255;
				(unsigned char)((*imgBird)->imageData[i * 240 * 3 + j * 3 + 2]) = 255;
			}
		}
	}

	for (i = 0; i < BIRD_WIDTH; i++) {
		while (hit[i] && i < BIRD_WIDTH) {
			if (30<hit[i] && hit[base[baseCount]] < hit[i])
				base[baseCount] = i;
			i++;
		}
		if (base[baseCount])baseCount++;
	}

	return baseCount;
}
static int nonZero(IplImage** imgBird, int top, int bottom, int left, int right, int *tempX, int *tempY) {
	int i, j;
	int tempIdx = 0;
	for (i = top; i < bottom; i++) {
		for (j = left; j < right; j++) {
			if ((unsigned char)(*imgBird)->imageData[i* BIRD_WIDTH * 3 + j * 3] == 0) {
				(unsigned char)(*imgBird)->imageData[i * 240 * 3 + j * 3 + 1] = 125;
				tempX[tempIdx] = j;
				tempY[tempIdx++] = i;
			}
		}
	}
	return tempIdx;
}
static int slidingWindow(IplImage** imgBird, int base, int *lineX, int *lineY) {
	int tempX[1000], tempY[1000];
	int i, j, lineIdx = 0;
	for (j = 0; j < WINDOW_COUNT; j++) {
		int top = BIRD_HEIGHT - (BIRD_HEIGHT / WINDOW_COUNT)*(j + 1);
		int bottom = BIRD_HEIGHT - (BIRD_HEIGHT / WINDOW_COUNT)*j;
		int left = (base - MARGIN > 0) ? base - MARGIN : 0;
		int right = (base + MARGIN < BIRD_WIDTH) ? base + MARGIN : BIRD_WIDTH;

		int line = nonZero(imgBird, top, bottom, left, right, tempX, tempY);
		int meanX = 0;

		cvRectangle(*imgBird, cvPoint(left, top), cvPoint(right, bottom), CV_RGB(0, 0, 122), 3, 0, 0);

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
float polyFit(IplImage** imgBird, int *lineX, int *lineY, int lineIdx) {
	int n = lineIdx;
	float sxi = 0, sxi2 = 0, sxi3 = 0, sxi4 = 0, syi = 0, sxiyi = 0, sxi2yi = 0;
	float a[3][3] = { 0, }, b[3] = { 0, }, c[3] = { 0, };

	int i, l, j;

	for (i = 0; i < n; i++) {
		//test 2 차량 중앙 인식 할때 주석 풀기 밑에 test 2 주석 같이 풀기
		float x = lineX[i] * XM_PER_PIX;
		float y = lineY[i] * YM_PER_PIX;
		//test2 

		//test 1 곡률 반경 구할때 주석 풀기
		//float x = lineX[i];
		//float y = lineY[i];
		//test 1
		sxi += y;
		sxi2 += y*y;
		sxi3 += y*y*y;
		sxi4 += y*y*y*y;
		syi += x;
		sxiyi += y*x;
		sxi2yi += y*y*x;
	}
	a[0][0] = n;
	a[0][1] = sxi;
	a[0][2] = sxi2;
	a[1][0] = sxi;
	a[1][1] = sxi2;
	a[1][2] = sxi3;
	a[2][0] = sxi2;
	a[2][1] = sxi3;
	a[2][2] = sxi4;

	b[0] = syi;
	b[1] = sxiyi;
	b[2] = sxi2yi;

	for (l = 0; l <= 1; l++) {
		for (i = l + 1; i <= 2; i++) {
			float temp = a[i][l] / a[l][l];
			for (j = l; j <= 2; j++) {
				a[i][j] = a[i][j] - temp*a[l][j];
			}
			b[i] = b[i] - temp*b[l];
		}
	}

	for (i = 2; i >= 0; i--) {
		c[i] = b[i] / a[i][i];
		for (j = 0; j <= i - 1; j++) {
			b[j] = b[j] - c[i] * a[j][i];
			a[j][i] = 0;
		}
	}
	int oldx = 0;
	int oldy = 0;

	for (i = 0; i < 180; i++) {
		int y = c[0] + (c[1] * i) + (c[2] * (i*i));
		if (0 < y&&y < BIRD_WIDTH) {
			if (oldy == 0)oldy = y;
			cvLine(*imgBird, cvPoint(oldy, i - 1), cvPoint(y, i), CV_RGB(0, 0, 0), 2, 8, 0);
			oldy = y;
		}
	}
	//test 1 곡률 반경 구하기 위에 test1 같이 주석 풀기
	/*float temp = 1 + (4 * c[2] * c[2] * BIRD_HEIGHT*YM_PER_PIX) + (2 * c[2] * c[1] * BIRD_HEIGHT) + (c[1] * c[1]);
	temp = pow(temp, 1.5);
	float curves = temp / fabs(2 * c[2]);
	return curves;*/
	//test 1


	// test 2 차량 위치가 중앙에서 얼마나 먼지 찾기   위에 test 2 같이 주석 풀기
	float h = BIRD_HEIGHT*YM_PER_PIX;
	float w = BIRD_WIDTH*XM_PER_PIX;

	float intercept = c[2] * h*h + c[1] * h + c[0];
	return intercept;
	// test 2  
}
static void InversePerspectiveMapping(IplImage* imgOrigin, IplImage* imgDest){
	int frameWidth = 320;
	int frameHeight = 240;
	int alpha_ = 10, beta_ = 90, gamma_ = 90;
	int f_ = 500, dist_ = 500;
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
	cvWarpPerspective(imgOrigin, imgDest, &transformation, CV_INTER_CUBIC | CV_WARP_INVERSE_MAP, cvScalarAll(0));
}
static void MatrixMultiply(float *a, float *b, float *result, int col1, int col2, int row2){
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