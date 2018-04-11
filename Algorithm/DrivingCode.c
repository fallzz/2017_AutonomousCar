#include <opencv/highgui.h>
#include <opencv/cv.h>
#include <stdio.h>

int main() {
	char fileName[100];
	for (int q = 27; q < 28; q++) {

		sprintf(fileName, "../../curveImage/imgOrigin%d.png", q);
		IplImage* imgOrigin = cvLoadImage(fileName, CV_LOAD_IMAGE_UNCHANGED);
		IplImage* imgD = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 1);
		IplImage* imgT = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 1);
		IplImage* imgHSV = cvCreateImage(cvGetSize(imgOrigin), IPL_DEPTH_8U, 3);

		cvCvtColor(imgOrigin, imgHSV, CV_BGR2HSV);

		for (int i = 0; i < 240; i++) {
			for (int j = 0; j < 320; j++) {
				if (imgHSV->imageData[i * 320 * 3 + j * 3] > 20 && imgHSV->imageData[i * 320 * 3 + j * 3] < 50)
					((uchar)imgD->imageData[i * 320 + j]) = 255;
				else ((uchar)imgD->imageData[i * 320 + j]) = 0;
			}
		}

		int startRightLine=-1, startLeftLine=-1;
		int leftLineX[300], rightLineX[300];
		int leftLineY[300], rightLineY[300];
		int leftIdx=0, rightIdx=0;
		int line[2][20], idx[2] = { 0, };

		for (int i = 239; 0 <= i&&idx[0]<20&&idx[1]<20; i--) {
			for (int j = 160; j >= 0&&idx[0]<20; j--) {
				if (((uchar)imgD->imageData[i * 320 + j]) == 255) {
					line[0][idx[0]++] = j;
					((uchar)imgT->imageData[i * 320 + j]) = 255;
					break;
				}
			}
			for (int j = 160; j <320 && idx[1]<20; j++) {
				if (((uchar)imgD->imageData[i * 320 + j]) == 255) {
					line[1][idx[1]++] = j;
					((uchar)imgT->imageData[i * 320 + j]) = 255;
					break;
				}
			}
		}

		for (int t = 0; t < 2; t++) {
			if (line[t][0] < line[t][idx[t] - 1]) {
				int temp = 0;
				for (int i = 0; i < idx[t]; i++) {
					temp += line[t][i];
				}
				startLeftLine = temp / idx[t];
			}
			else {
				int temp = 0;
				for (int i = 0; i < idx[t]; i++) {
					temp += line[t][i];
				}
				startRightLine = temp / idx[t];
			}
		}

		for (int i = 239; i >= 0; i--) {
			if (startLeftLine != -1) {

				for (int j = startLeftLine + 20; j >= startLeftLine - 20&&j>=0; j--) {
					if (((uchar)imgD->imageData[i * 320 + j]) == 255) {
						leftLineX[leftIdx] = j;
						leftLineY[leftIdx++] = i;
						startLeftLine = j;
						((uchar)imgT->imageData[i * 320 + j]) = 255;
						break;
					}
				}
			}
			if (startRightLine != -1) {
				for (int j = startRightLine - 20; j < startRightLine + 20&&j<320; j++) {
					if (((uchar)imgD->imageData[i * 320 + j]) == 255) {
						rightLineX[rightIdx] = j;
						rightLineY[rightIdx++] = i;
						startRightLine = j;
						((uchar)imgT->imageData[i * 320 + j]) = 255;
						break;
					}
				}
			}
		}

		double xiyi=0, xi=0, yi=0, xi2=0;
		
		for (int i = 0; i < leftIdx; i++) {
			xiyi += leftLineX[i] * leftLineY[i];
			xi += leftLineX[i];
			yi += leftLineY[i];
			xi2 += leftLineX[i] * leftLineX[i];
		}
		double leftcurve = (leftIdx*xiyi - xi*yi) / (leftIdx*xi2 - pow(xi, 2));
		double leftOffset = leftLineY[0] - leftcurve*leftLineX[0];
		
		xiyi = 0, xi = 0, yi = 0, xi2 = 0;

		for (int i = 0; i < rightIdx; i++) {
			xiyi += rightLineX[i] * rightLineY[i];
			xi += rightLineX[i];
			yi += rightLineY[i];
			xi2 += rightLineX[i] * rightLineX[i];
		}	
		double rightcurve = (rightIdx*xiyi - xi*yi) / (rightIdx*xi2 - pow(xi, 2));
		double rightOffset = rightLineY[0] - rightcurve*rightLineX[0];

		for (int i = 0; i < 320; i++) {
			int x = i;
			int y = leftcurve * i + leftOffset;
			((uchar)imgD->imageData[y * 320 + x]) = 255;
		}
		for (int i = 0; i < 320; i++) {
			int x = i;
			int y = rightcurve * i + rightOffset;
			((uchar)imgD->imageData[y * 320 + x]) = 255;
		}

		printf("leftCurve : %f\n", leftcurve);
		printf("rightCurve : %f\n", rightcurve);

		cvNamedWindow("Example", CV_WINDOW_AUTOSIZE);
		cvNamedWindow("Example2", CV_WINDOW_AUTOSIZE);
		cvNamedWindow("Example3", CV_WINDOW_AUTOSIZE);
		cvShowImage("Example", imgOrigin);
		cvShowImage("Example2", imgD);
		cvShowImage("Example3", imgT);
		cvWaitKey(0); 
		cvDestroyWindow("Example2");
	}
	return 0;
}