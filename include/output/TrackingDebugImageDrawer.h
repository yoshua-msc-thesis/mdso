#ifndef INCLUDE_TRACKINGDEBUGIMAGEOBSERVER
#define INCLUDE_TRACKINGDEBUGIMAGEOBSERVER

#include "output/FrameTrackerObserver.h"

DECLARE_double(tracking_rel_point_size);
DECLARE_int32(tracking_res_image_width);
DECLARE_double(debug_max_residual);

namespace mdso {

class TrackingDebugImageDrawer : public FrameTrackerObserver {
public:
  TrackingDebugImageDrawer(CameraBundle camPyr[],
                           const Settings::FrameTracker &frameTrackerSettings,
                           const Settings::Pyramid &pyrSettings,
                           const std::vector<int> &drawingOrder);

  void startTracking(const PreKeyFrame &frame) override;
  void levelTracked(int pyrLevel, const FrameTracker::TrackingResult &result,
                    const std::vector<std::vector<std::pair<Vec2, double>>>
                        &pointResiduals) override;

  cv::Mat3b drawAllLevels();
  cv::Mat3b drawFinestLevel();

private:
  cv::Mat3b drawLevel(int pyrLevel);

  Settings::FrameTracker frameTrackerSettings;
  Settings::Pyramid pyrSettings;

  CameraBundle *camPyr;
  std::vector<std::vector<cv::Mat1b>> curFramePyr;
  std::vector<std::vector<cv::Mat3b>> residualsImg;
  std::vector<int> drawingOrder;
};

} // namespace mdso

#endif
