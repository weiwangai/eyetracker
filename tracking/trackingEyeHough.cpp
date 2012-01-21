#include "trackingEyeHough.h"

#include <iostream>

// opencv
#include "highgui.h"


//------------------------------------------------------------------------------
TrackingEyeHough::TrackingEyeHough(const int eye_cam, int show_binary)
  : m_eye_cam(eye_cam), m_bw_threshold(5), m_hough_minDist(30), m_hough_dp(2),
    m_hough_param1(30), m_hough_param2(1), m_hough_minRadius(10), m_hough_maxRadius(40),
    m_show_binary(show_binary)
{
  m_eye = new EyeCapture(m_eye_cam, 1);

  TrackedPupil tmp;
  tmp.position.push_back(cv::Point2f(0,0));
  tmp.radius.push_back(0);
  tmp.frame = m_eye->getFrame();

  m_curr_pupil = tmp;
  m_prev_pupil = tmp;

}

//------------------------------------------------------------------------------
TrackingEyeHough::~TrackingEyeHough()
{
}

//------------------------------------------------------------------------------
TrackedPupil TrackingEyeHough::getPupil()
{
  TrackedPupil pupil;
  // temporary pupil which is updated for every frame
  TrackedPupil tmp_pupil, tmp_prev_pupil;
  tmp_prev_pupil = m_prev_pupil;
  m_prev_pupil = m_curr_pupil;
  tmp_pupil = m_curr_pupil;
  HoughCirclesPupil(pupil);

  // find closest circle
  double min = std::numeric_limits<double>::max();
  for(int i = 0; i < pupil.position.size(); i++)
  {
    double dist = distance(m_curr_pupil.position[0], pupil.position[i]);
    if(dist < min)
    {
      min = dist;
      tmp_pupil.position[0] = pupil.position[i];
      tmp_pupil.radius[0] = pupil.radius[i];
      tmp_pupil.frame = pupil.frame;
    }
  }

  // Do all the fancy ellipse stuff here.
  // First: Define bounding box (ROI) based on HoughCircle
  int tol = 2;
  int maxheight = m_binary_frame.rows;
  int maxwidth = m_binary_frame.cols;
  int x1 = tmp_pupil.position[0].x - tmp_pupil.radius[0] - tol < 0 ? 0 : tmp_pupil.position[0].x - tmp_pupil.radius[0] - tol;
  int y1 = tmp_pupil.position[0].y - tmp_pupil.radius[0] - tol < 0 ? 0 : tmp_pupil.position[0].y - tmp_pupil.radius[0] - tol;
  int x2 = x1 + 2*(tmp_pupil.radius[0]+tol) > maxwidth ? 2 : 2*(tmp_pupil.radius[0]+tol);
  int y2 = y1 + 2*(tmp_pupil.radius[0]+tol) > maxheight ? 2 : 2*(tmp_pupil.radius[0]+tol);
  cv::Rect roi_pupil(x1, y1, x2, y2);
  cv::Mat extracted_pupil;
  extracted_pupil = m_binary_frame(roi_pupil).clone();
  
  // Find contours of pupil
  std::vector<std::vector<cv::Point> > contours;
  std::vector<cv::Vec4i> hierarchy;
  cv::findContours(extracted_pupil, contours, hierarchy, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_NONE, 
                   cv::Point(x1, y1));
  // get convex hull of pupil to make it more stable
  std::vector<cv::Point> hull;
  std::vector<std::vector<cv::Point> > hull_t;
  if(contours.size() > 0) // don't care if no contours are found
  {
    cv::convexHull(contours[0], hull);
    // the next line is just that draw contours stops complaining
    hull_t.push_back(hull);
    //cv::drawContours(tmp_pupil.frame, contours, -1, cv::Scalar(255));
    //cv::drawContours(tmp_pupil.frame, hull_t, -1, cv::Scalar(235));
    
    // For an ellipse, apparently 5 points are necessary
    if(hull.size() > 5)
    {
      cv::RotatedRect rect_n = cv::fitEllipse(hull);
      cv::ellipse(tmp_pupil.frame, rect_n, cv::Scalar(200));
      tmp_pupil.position[0] = rect_n.center;
    }
  }

  // check if found circle is close enough
  // Actually, not necessary anymore.
  //if(min < 0)
  if(distance(m_curr_pupil.position[0], tmp_pupil.position[0]) < 0)
  {
    // this never happens. this is ugly
    m_curr_pupil.frame = tmp_pupil.frame.clone();
  }
  else
  {
    //m_curr_pupil.position[0].x = (m_curr_pupil.position[0].x + tmp_pupil.position[0].x + tmp_prev_pupil.position[0].x)/3;
    //m_curr_pupil.position[0].y = (m_curr_pupil.position[0].y + tmp_pupil.position[0].y + tmp_prev_pupil.position[0].y)/3;
    m_curr_pupil.position[0].x = (m_curr_pupil.position[0].x + tmp_pupil.position[0].x)/2;
    m_curr_pupil.position[0].y = (m_curr_pupil.position[0].y + tmp_pupil.position[0].y)/2;
    m_curr_pupil.frame = tmp_pupil.frame.clone();
  }

  return m_curr_pupil;
}

//------------------------------------------------------------------------------
void TrackingEyeHough::HoughCirclesPupil(TrackedPupil &pupil)
{
  cv::Mat gray, gray_blur, binary, edges;
  cv::Rect roi(1/4*640+60, 1/4*480+60, 640/2-120, 480/2-120);

  gray = m_eye->getFrame();
  gray = gray(roi).clone();
  equalizeHist(gray, gray);
  cv::erode(gray, gray, cv::Mat());

  cv::GaussianBlur(gray, gray_blur, cv::Size(9,9), 5, 5);
  //gray_blur = gray.clone();
  cv::threshold(gray_blur, binary, m_bw_threshold, 255, cv::THRESH_BINARY_INV);

  for(int i = 0; i < 2; i++)
    cv::dilate(binary, binary, cv::Mat());

  std::vector<cv::Vec3f> circles;

  int adapt_param2 = m_hough_param2;
  while(circles.size() < 8 && adapt_param2 > 0)
  {
    cv::HoughCircles(binary, circles, CV_HOUGH_GRADIENT, m_hough_dp, 
                     m_hough_minDist, m_hough_param1, adapt_param2, m_hough_minRadius, 
                     m_hough_maxRadius);

    // decrease threshold if not enough circles are found
    adapt_param2 -= 1;
  }

  // push back the found circles
  for(int i = 0; i < circles.size(); i++)
  {
    pupil.position.push_back(cv::Point2f(circles[i][0], circles[i][1]));
    pupil.radius.push_back(circles[i][2]);
  }
  if(m_show_binary)
    pupil.frame = binary.clone();
  else
    pupil.frame = gray.clone();

  m_binary_frame = binary.clone();
}

//------------------------------------------------------------------------------
// THIS ONE IS NOT NEEDED ANYMORE BECAUSE WE DONT NEED INITIALIZATION BY THE 
// USER!
void mouse_callback(int event, int x, int y, int flags, void* user_data)
{
  switch(event)
  {
  case CV_EVENT_LBUTTONDOWN:
    {
      CallbackData *data = static_cast<CallbackData*>(user_data);

      // Find line which is nearest to click position
      cv::Point2f position;
      float radius;
      cv::Point click(x,y);
      double min = std::numeric_limits<double>::max();
      for (size_t i = 0; i < data->detected_positions->position.size(); ++i)
      {
        // update someday to distance function
        double d = sqrt(pow(click.x - (data->detected_positions->position.at(i)).x,2)+
                 pow(click.y - (data->detected_positions->position.at(i)).y,2));
        if (d < min)
        {
          min = d;
          position = data->detected_positions->position.at(i);
          radius = data->detected_positions->radius.at(i);
        }
      }

      TrackedPupil pupil;
      pupil.position.push_back(position);
      pupil.radius.push_back(radius);
      *(data->pupil_to_track) = pupil;

      // Visualize
      cv::circle((data->detected_positions->frame), 
          cv::Point(pupil.position[0].x, pupil.position[0].y), 
          pupil.radius[0], cv::Scalar(255), 2);
      cv::imshow(INPUT_WINDOW_NAME, (data->detected_positions->frame));
    }
    break;

  default:
    break;
  }
}

double TrackingEyeHough::distance(const cv::Point2f pupil_to_track, 
                                  const cv::Point2f found_pupil)
{
  // Euclidian distance
  return sqrt(pow(pupil_to_track.x - found_pupil.x, 2) +
              pow(pupil_to_track.y - found_pupil.y, 2));
}

// Print Parameters for tracking
void TrackingEyeHough::printParams()
{
  std::cout << "TrackingEyeHough Parameters" << std::endl;
  std::cout << " m_bw_threshold:       " << m_bw_threshold    << std::endl;
  std::cout << " m_hough_minDist:      " << m_hough_minDist   << std::endl;
  std::cout << " m_hough_dp:           " << m_hough_dp        << std::endl;
  std::cout << " m_hough_param1:       " << m_hough_param1    << std::endl;
  std::cout << " m_hough_param2:       " << m_hough_param2    << std::endl;
  std::cout << " m_hough_minRadius:    " << m_hough_minRadius << std::endl;
  std::cout << " m_hough_maxRadius:    " << m_hough_maxRadius << std::endl;
}
