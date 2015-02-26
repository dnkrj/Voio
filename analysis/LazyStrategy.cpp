#include "LazyStrategy.h"
#include "gifs/gifcv.h"
#ifndef _OPEN_CV
#define _OPEN_CV
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/core/core.hpp"
#include "opencv2/objdetect/objdetect.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#endif

#include <string>

using namespace cv;

std::vector<Timestamp>  LazyStrategy::processVideo(const std::string& filename, int secondsPerClip)
{

	VideoCapture vidReader(filename);
	long frameCount = vidReader.get(CV_CAP_PROP_FRAME_COUNT);
	double fps = vidReader.get(CV_CAP_PROP_FPS);

	std::vector<Timestamp> timestamps;
	
	long totalTimeSeconds = frameCount / fps;
	int totalTimeMinutes = totalTimeSeconds / 60;
	
	if (totalTimeMinutes == 0)
	{
		if (totalTimeSeconds > secondsPerClip)
		{
			timestamps.push_back(Timestamp(0.0, secondsPerClip*1000));
		}
		return timestamps;
	}
	
	for (int i = 0; i < totalTimeMinutes; i++)
	{
		int randomTime = rand() % (60-secondsPerClip);
		std::cout << "Random: " << rand() << " " << randomTime << std::endl;
		randomTime += i * 60;
		timestamps.push_back(
		Timestamp((randomTime)*1000, (randomTime + secondsPerClip)*1000));
	}

	return timestamps;
}
