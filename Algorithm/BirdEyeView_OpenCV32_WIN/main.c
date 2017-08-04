#ifdef _DEBUG 
#pragma comment (lib, "opencv_world320.lib") 
#else 
#pragma comment (lib, "opencv_world320d.lib")
#endif 

#include<opencv2/opencv.hpp>
#include<iostream>

using namespace cv;
using namespace std;

#define RESIZEW 0.5
#define RESIZEH 0.5
#define DOTCOUNT 30
#define BVIEWW 250
#define BVIEWH 200
#define WINDOW 15

int sizeW, sizeH;
int dotTerm;

void setup(VideoCapture *capture) {
   Mat test;

   if (!(capture->isOpened()))
   {
      exit(0);
   }

   capture->read(test);

   if (test.empty()) {
      exit(0);
   }

   sizeW = test.cols * RESIZEW, sizeH = test.rows * RESIZEH;
}
void SlidingWindow(Mat dstFrame, uchar * data, int base, int margin) {
   int leftx[WINDOW];

   for (int i = 0; i < WINDOW; i++) {
      int top = BVIEWH - (BVIEWH / (WINDOW))*(i + 1);
      int bottom = BVIEWH - (BVIEWH / (WINDOW))*i;
      int middle = (top + bottom) / 2;
      int leftx = base - (margin / 2);
      int rightx = base + (margin / 2);

      for (int j = rightx; j > leftx; j--) {
         if (data[middle*BVIEWW + j] == 255) {
            rightx += (j - base) > 0 ? (j - base) : (base - j);
            leftx += (j - base) > 0 ? (j - base) : (base - j);
            break;
         }
      }
      rectangle(dstFrame, Rect(Point2i(leftx, top), Point2i(rightx, bottom)), 255);
   }
}
int main() {
   VideoCapture capture("../../testVideo.avi");

   Mat frame, dstFrame;
   int dotX[DOTCOUNT], dotY[DOTCOUNT];
   bool failDot[DOTCOUNT];

   setup(&capture);

   double fps = 15;
   int fourcc = VideoWriter::fourcc('X', 'V', 'I', 'D'); // opencv3.0�̻�

   bool isColor = true;

   VideoWriter *video = new VideoWriter;

   if (!video->open("result.avi", fourcc, fps, Size(250, 200), isColor)) {
      delete video;
   }

   while (1) {
      capture.read(frame);
      if (frame.empty())break;
      resize(frame, frame, Size(), RESIZEW, RESIZEH);

      Size WarpSize(BVIEWW, BVIEWH);

      Point2f src[4], dst[4];
      src[0] = Point2f(0, 163); 
      src[1] = Point2f(630, 163);
      src[2] = Point2f(0, 358);
      src[3] = Point2f(630, 358);

      /*src[0] = Point2f(0, 237);
      src[1] = Point2f(625, 237);
      src[2] = Point2f(0, 350);
      src[3] = Point2f(625, 350);*/

      /*src[0] = Point2f(266, 237);
      src[1] = Point2f(374, 237);
      src[2] = Point2f(15, 350);
      src[3] = Point2f(625, 350);*/

      dst[0] = Point2f(0, 0);
      dst[1] = Point2f(WarpSize.width, 0);
      dst[2] = Point2f(100, WarpSize.height);
      dst[3] = Point2f(WarpSize.width-100, WarpSize.height);

      Mat M = getPerspectiveTransform(src, dst);
      warpPerspective(frame, dstFrame, M, WarpSize, INTER_LINEAR);

      /*inRange(dstFrame, Scalar(0, 155, 155), Scalar(255, 255, 255), dstFrame);

      uchar * data = (uchar*)dstFrame.data;

      int hit[BVIEWW] = { 0, };
      for (int i = 0; i < BVIEWH; i++) {
         for (int j = 0; j < BVIEWW; j++) {
            if (data[i*BVIEWW + j] == 255)hit[j]++;
         }
      }

      int left_base = 0, right_base = 0;
      for (int i = 0; i < BVIEWW / 2; i++) {
         if (hit[left_base] < hit[i])left_base = i;
      }
      for (int i = BVIEWW / 2; i < BVIEWW; i++) {
         if (hit[right_base] < hit[i])right_base = i;
      }
      int window = 15;
      int margin = 30;
      SlidingWindow(dstFrame, data, left_base, margin);
      SlidingWindow(dstFrame, data, right_base, margin);
      */
      *video << dstFrame;

      imshow("Live", frame);
      imshow("Roi", dstFrame);

      char c = waitKey(33);
      if (c == 27)break;
      else if (c == 112) {
         waitKey();
      }
   }
   delete video;
   capture.release();
   cv::destroyAllWindows();

   return 0;
}