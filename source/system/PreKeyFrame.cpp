#include "system/PreKeyFrame.h"
#include "PreKeyFrameInternals.h"
#include "util/util.h"
#include <ceres/cubic_interpolation.h>
#include <opencv2/opencv.hpp>

namespace fishdso {

PreKeyFrame::PreKeyFrame(CameraModel *cam, const cv::Mat &frameColored,
                         int globalFrameNum,
                         const Settings::Pyramid &_pyrSettings)
    : frameColored(frameColored)
    , framePyr(cvtBgrToGray(frameColored), _pyrSettings.levelNum)
    , cam(cam)
    , globalFrameNum(globalFrameNum)
    , pyrSettings(_pyrSettings)
    , internals(std::unique_ptr<PreKeyFrameInternals>(
          new PreKeyFrameInternals(framePyr, pyrSettings))) {
  grad(frame(), gradX, gradY, gradNorm);
}

PreKeyFrame::~PreKeyFrame() {}

}; // namespace fishdso
