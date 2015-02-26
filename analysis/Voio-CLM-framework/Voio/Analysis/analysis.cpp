//
//  analysis.cpp
//  VOIO_analysis
//
//  Created by Laurynas Karazija on 18/02/15.
//

//CLM
#include <CLM.h>
#include <CLMTracker.h>
#include <CLMParameters.h>
#include <CLM_utils.h>

//standard CPP
#include <fstream>
#include <sstream>
#include <array>
#include <cmath>

//openCV
#include <cv.h>

//TBB
#include<tbb/tbb.h>

#include "analysis.h"

#define NUM_Faces 8
#define SEARCH_FREQ 8
#define MAX_NUMBER_SEGMENTS 60
using namespace std;
using namespace cv;



//weight function
double inline wF(array<double, 3> in ){
    // size times orientation
    return 0.8+(in[1] /0.4 * in[2]);
    // face taking 40% of the screen is massive :)
    // 0.8 - face pressence wiegth
    // certainty - unimportant - we care about displayable faces - HAAR gives wierd certainties
}

double inline get_orientation_weight(double alfa, double beta, double gama){
    //Strategy rotate 1 0 0 vector though said angles -> project to camera plane
    //Vec3d v(1.0, .0, .0);
    Mat_<double> v = Mat(3, 1, CV_64F);
    v.at<double>(0,0) = 1;
    v.at<double>(1,0) = 0;
    v.at<double>(2,0) = 0;
    Matx33d rot = CLMTracker::Euler2RotationMatrix(Vec3d(alfa, beta, gama));
    // Rotate the vector
    v = Mat(rot) * v;
    double x =v.at<double>(0,0);
    double y =v.at<double>(1,0);
    return sqrt(x*x + y*y);
}

vector<string> get_arguments(int argc, char **argv)
{
    
    vector<string> arguments;
    
    for(int i = 1; i < argc; ++i)
    {
        arguments.push_back(string(argv[i]));
    }
    return arguments;
}

void NonOverlapingDetections(const vector<CLMTracker::CLM>& clm_models, vector<Rect_<double> >& face_detections)
{
    
    // Go over the model and eliminate detections that are not informative (there already is a tracker there)
    for(size_t model = 0; model < clm_models.size(); ++model)
    {
        
        // See if the detections intersect
        Rect_<double> model_rect = clm_models[model].GetBoundingBox();
        
        for(int detection = face_detections.size()-1; detection >=0; --detection)
        {
            double intersection_area = (model_rect & face_detections[detection]).area();
            double union_area = model_rect.area() + face_detections[detection].area() - 2 * intersection_area;
            
            // If the model is already tracking what we're detecting ignore the detection, this is determined by amount of overlap
            if( intersection_area/union_area > 0.5)
            {
                face_detections.erase(face_detections.begin() + detection);
            }
        }
    }
}

struct FrameInfo{
    double frametime;
    double weight;
    int facecount;
    vector<array<double, 4>> fv;
};

//Function to walk the video and use CLM to extract and track faces
bool runCLManalysisFile (vector<FrameInfo> &result, double &framerate,string filename, bool verbose = false, bool output_to_file=false, bool debug= false)
{
    CLMTracker::CLMParameters params;
    params.use_face_template = true;
    params.quiet_mode = !verbose;
    // This is so that the model would not try re-initialising itself
    params.reinit_video_every = -1;
    //params.curr_face_detector = CLMTracker::CLMParameters::HOG_SVM_DETECTOR;  -->slow
    params.curr_face_detector = CLMTracker::CLMParameters::HAAR_DETECTOR;     //-->try insteads
    vector<CLMTracker::CLMParameters> clm_parameters;
    clm_parameters.push_back(params);
    // The modules that are being used for tracking
    vector<CLMTracker::CLM> models;
    vector<bool> active_models;
    //How MANY faces we track... SIGNIFICANT IMPACT ON PERFORMACE
    int num_faces_max = NUM_Faces;
    
    CLMTracker::CLM model(clm_parameters[0].model_location);
    model.face_detector_HAAR.load(clm_parameters[0].face_detector_location);
    model.face_detector_location = clm_parameters[0].face_detector_location;
    models.reserve(num_faces_max);
    for (int i = 0; i < num_faces_max; i++)
    {
        models.push_back(model);
        active_models.push_back(false);
        clm_parameters.push_back(params);
    }
    
    int frame_count = 0;
    
    // Do some grabbing
    VideoCapture video_capture(filename);
    if(!video_capture.isOpened())  // check if we succeeded
        return false;
    Mat frame;
    video_capture >> frame;
    frame_count++;
    //Some Constants:
    float cx = frame.cols / 2.0f;
    float cy = frame.rows / 2.0f;
    float fx = 600, fy = 600;
    int vidFC = video_capture.get(CV_CAP_PROP_FRAME_COUNT);
    framerate = video_capture.get(CV_CAP_PROP_FPS);
    
    //Visualisations --------------
    VideoWriter writerFace;
    int w, h;
    Mat disp_image;
    if(verbose){
        if(debug){
            w =video_capture.get(CV_CAP_PROP_FRAME_WIDTH);
            h =video_capture.get(CV_CAP_PROP_FRAME_HEIGHT);
            Size vidSize(w,h);
            //output file
            writerFace = VideoWriter(filename+"_out.avi", CV_FOURCC('D', 'V', 'I', 'X'), 25, vidSize);
            if(!writerFace.isOpened()){
                cout<<"The output file is bad"<<endl;
                return false;
            }
        }
        namedWindow(filename,1);
    }
    //Visualisations --------------
    
    //prepare result vector;
    result.clear();
    result.reserve(vidFC);
    
    Mat_<float> depth_image;
    Mat_<uchar> grayscale_image;
    // Starting tracking
    while(!frame.empty())
    {
        FrameInfo frameFeatures;
        
        // Reading the images
        disp_image = frame.clone();
        
        if(frame.channels() == 3)
        {
            cvtColor(frame, grayscale_image, CV_BGR2GRAY);
        }
        else
        {
            grayscale_image = frame.clone();
        }
        
        
        vector<Rect_<double> > face_detections;
        
        bool all_models_active = true;
        for(unsigned int model = 0; model < models.size(); ++model)
        {
            if(!active_models[model])
            {
                all_models_active = false;
            }
        }
        //SIGNIFICANT IMPACT ON PERFORMACE ---- when do we run facedetection?
        if(frame_count % SEARCH_FREQ == 0 && !all_models_active)
        {
    
            CLMTracker::DetectFaces(face_detections, grayscale_image, models[0].face_detector_HAAR);
            
        }
        
        // Keep only non overlapping detections (also convert to a concurrent vector
        NonOverlapingDetections(models, face_detections);
        
        
        vector<tbb::atomic<bool> > face_detections_used(face_detections.size());
        
        // Go through every model and update the trackingpull out as a separate parallel/non-parallel method
        tbb::parallel_for(0, (int)models.size(), [&](int model){
            bool detection_success = false;
            
            // If the current model has failed more than 4 times in a row, remove it
            if(models[model].failures_in_a_row > 4)
            {
                active_models[model] = false;
                models[model].Reset();
                
            }
            
            // If the model is inactive reactivate it with new detections
            if(!active_models[model])
            {
                
                for(size_t detection_ind = 0; detection_ind < face_detections.size(); ++detection_ind)
                {
                    // if it was not taken by another tracker take it (if it is false swap it to true and enter detection, this makes it parallel safe)
                    if(face_detections_used[detection_ind].compare_and_swap(true, false) == false)
                    {
                        
                        // Reinitialise the model
                        models[model].Reset();
                        
                        // This ensures that a wider window is used for the initial landmark localisation
                        models[model].detection_success = false;
                        detection_success = CLMTracker::DetectLandmarksInVideo(grayscale_image, depth_image, face_detections[detection_ind], models[model], clm_parameters[model]);
                        
                        // This activates the model
                        active_models[model] = true;
                        
                        // break out of the loop as the tracker has been reinitialised
                        break;
                    }
                    
                }
            }
            else
            {
                // The actual facial landmark detection / tracking
                detection_success = CLMTracker::DetectLandmarksInVideo(grayscale_image, depth_image, models[model], clm_parameters[model]);
            }
        });
        
        // Going through every model and visualising the results
        
        int num_active_models = 0;
        vector<std::array<double, 3> > frameFV;
        
        for(size_t model = 0; model < models.size(); ++model)
        {
            // Visualising the results
            // Drawing the facial landmarks on the face and the bounding box around it if tracking is successful and initialised
            double certainty = models[model].detection_certainty;
            
            double visualisation_boundary = -0.1;
            
            // Only draw if the reliability is reasonable, the value is slightly ad-hoc
            if(certainty < visualisation_boundary)
            {
                CLMTracker::Draw(disp_image, models[model]);
                
                if(certainty > 1)
                    certainty = 1;
                if(certainty < -1)
                    certainty = -1;
                
                certainty = (certainty + 1)/(visualisation_boundary +1);
                
                // Work out the pose of the head from the tracked model
                Vec6d estimatedPose = CLMTracker::GetCorrectedPoseCameraPlane(models[model], fx, fy, cx, cy, clm_parameters[model]);
                
                double orientation = get_orientation_weight(estimatedPose[3], estimatedPose[4], estimatedPose[5]);
                
                double face_size = models[model].GetBoundingBox().area()/ (frame.cols * frame.rows);
                
                array<double, 3> arr({{certainty, face_size, orientation}});
                frameFV.push_back(arr);
                
                //Visualisations -------------- Begin
                if(verbose){
                    int thickness = (int)std::ceil(2.0* ((double)frame.cols) / 640.0);
                    // Draw it in reddish if uncertain, blueish if certain
                    CLMTracker::DrawBox(disp_image, estimatedPose, Scalar(face_size*20 *255,orientation*255, certainty *2 *255), thickness, fx, fy, cx, cy);
                }
                //Visualisations -------------- End
                
                num_active_models++;
            }
        }
        double frameWeight = 0;
        for(int i =0; i<frameFV.size(); i++){
            double single_weight =wF(frameFV[i]);
            frameWeight += single_weight;
            array<double, 4> arr({{single_weight, frameFV[i][0], frameFV[i][1], frameFV[i][2]}});
            frameFeatures.fv.push_back(arr);
        }
        //result.push_back(frameWeight);
        frameFeatures.facecount = num_active_models;
        frameFeatures.frametime = video_capture.get(CV_CAP_PROP_POS_MSEC);
        frameFeatures.weight = frameWeight;
        //Visualisations -------------- BEGIN
        if(verbose){
            stringstream prep;
            prep<<"Progress: "<<frame_count<<" // "<<vidFC<<"  T-"<<frameFeatures.frametime;
            putText(disp_image, prep.str(), Point(10,20), CV_FONT_HERSHEY_SIMPLEX, 0.5, CV_RGB(255,0,0));
            prep.str("");
            prep<<"Active models: "<<num_active_models<<" // "<<num_faces_max;
            putText(disp_image, prep.str(), Point(10,40), CV_FONT_HERSHEY_SIMPLEX, 0.5, CV_RGB(0,0,255));
            prep.str("");
            prep<<"Weight: "<<frameWeight;
            putText(disp_image, prep.str(), Point(10,60), CV_FONT_HERSHEY_SIMPLEX, 0.5, CV_RGB(125,0,255));
            //cout<<frameFeatures.fv.size()<<endl;
            for(int i = 0; i<frameFeatures.fv.size(); i++){
                prep.str("");
                prep<<"#"<<i<< " CERT: "<< int(frameFeatures.fv[i][1]*100) <<"% SIZE: "<<frameFeatures.fv[i][2]<<" ORTN: "<< frameFeatures.fv[i][3] << " W: "<< frameFeatures.fv[i][0];
                //cout<<prep.str()<<endl;
                putText(disp_image, prep.str(), Point(10, frame.rows - 20 - i*20), CV_FONT_HERSHEY_SIMPLEX, 0.5, CV_RGB(125,125,255));
            }
            imshow(filename, disp_image);
            if(debug){
                writerFace << disp_image;
            }
            if(waitKey(30) >= 0){
                while (true) {
                    if(waitKey(30) >= 0) break;
                }
            }
        }
        //Visualisations -------------- END
        result.push_back(frameFeatures);
        video_capture >> frame;
        frame_count++;
    }
    return true;
}


void smooth(const vector<double> &f, vector<double> &f_prime, int smooth_size=3){
    f_prime.clear();
    f_prime.reserve(f.size());
    for(int i = (smooth_size+1)/2 ; i < f.size() - (smooth_size+1)/2; i++){
        double value=0;
        for(int j = 0; j < smooth_size; j ++){
            value+= f[i - (smooth_size+1)/2 + j];
        }
        f_prime.push_back(value/smooth_size);
    }
}

struct Segment{
    double framestart;
    double frameend;
    int start;
    double sum;
    //vector<FrameInfo> fv;
};

void printVectorToFile(const vector<double> &in, string file){
    ofstream out;
    out.open(file);
    out<<in.size();
    for(int i =0; i<in.size(); i++){
        out<<in[i]<<endl;
    }
}

bool getSegmentVector(const vector<FrameInfo> &in, vector<Segment> &out, int segLen){
    //retuns sums of segments
    if (segLen > in.size()) return false;
    out.clear();
    out.reserve(in.size()-segLen);
    double sum =0;
    Segment s;
    for(int i = 0; i <segLen; i++){
        sum+=in[i].weight;
    }
    s.start = 0;
    s.sum = sum;
    s.framestart = in[0].frametime;
    s.frameend = in[segLen-1].frametime;
    out.push_back(s);
    for(int i = segLen; i <in.size(); i++){
        Segment a;
        sum = sum + in[i].weight - in[i-segLen].weight;
        a.start =i-segLen +1;
        a.sum = sum;
        a.frameend=in[i].frametime;
        a.framestart = in[i-segLen-1].frametime;
        out.push_back(a);
    }
    return true;
}

bool getMaxSeg(Segment &to_rtn, vector<Segment> &in, double sumTolerance,int segLen, int padding){
    //cout<<in.size()<<" ";
    auto first= in.begin();
    auto max = first;
    first++;
    auto end = in.end();
    if(in.size() < segLen)
        return false;
    while (first != end){
        if (first->sum >= sumTolerance)
            if(first->sum > max->sum){
                max = first;
            }
        first++;
    }
    
    to_rtn = *max;
    if(to_rtn.sum < sumTolerance){
        return false;
        //ran out of options;
    }
    auto erstart = max;
    while(erstart != in.begin()){
        if (erstart->start + padding < max->start) {
            erstart++;
            break;
        }
        erstart--;
    }
    auto erend = max+1;
    while(erend != in.end()){
        if (erend->start >= max->start+padding){
            break;
        }
        erend++;
    }
    in.erase(erstart, erend);
    return true;
}

void FtoMat(const vector<double> &in, Mat &out){
    out = Mat::zeros( NUM_Faces *3 *10,in.size(), CV_32S);
    for(int frame_count = 0; frame_count < in.size(); frame_count++){
        out.at<double>(out.rows - (int)in[frame_count]*20 -1, frame_count) = 255;
    }
}

void derivative(const vector<double> &f, vector<double> &f_prime){
    f_prime.clear();
    f_prime.reserve(f.size()-1);
    for(int i =1; i < f.size(); i++){
        f_prime.push_back(f[i]-f[i-1]);
    }
}


std::vector<Timestamp> CLMstatery::processVideoComp(const std::string& filename, int secondsPerClip, bool verbose, bool output_to_file){
    vector<FrameInfo> clm_results;
    double fps =30;
    runCLManalysisFile(clm_results,fps, filename, verbose);
    //cout<<clm_results.size()<<endl;
    int segLen = (int) fps * secondsPerClip;
    cout<<"Analysis Complete"<<endl;
    vector<Segment> segmentVec;
    getSegmentVector(clm_results, segmentVec, segLen);
    Segment s;
    int i =0;
    vector<Timestamp> result;
    while(getMaxSeg(s, segmentVec, 0.95,segLen, segLen*3)){
        Timestamp t(s.framestart, s.frameend);
        result.push_back(t);
        if(++i > MAX_NUMBER_SEGMENTS){
            break;
        }
    }
    return result;
}
std::vector<Timestamp> CLMstatery::processVideo(const std::string& filename, int secondsPerClip){
    return CLMstatery::processVideoComp(filename, secondsPerClip);
}