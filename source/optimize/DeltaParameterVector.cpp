#include "optimize/DeltaParameterVector.h"

namespace mdso::optimize {

DeltaParameterVector::DeltaParameterVector(int numKeyFrames, int numCameras,
                                           int numPoints)
    : frameParameterOrder(numKeyFrames, numCameras)
    , frame(frameParameterOrder.totalFrameParameters())
    , point(numPoints) {
  frame.setZero();
  point.setZero();
}

DeltaParameterVector::DeltaParameterVector(int numKeyFrames, int numCameras,
                                           const VecFt &frame,
                                           const VecPt &point)
    : frameParameterOrder(numKeyFrames, numCameras)
    , frame(frame)
    , point(point) {}

DeltaParameterVector operator*(double factor,
                               const DeltaParameterVector &delta) {
  int numKeyFrames = delta.frameParameterOrder.numKeyFrames(),
      numCameras = delta.frameParameterOrder.numCameras();
  return DeltaParameterVector(numKeyFrames, numCameras,
                              factor * delta.getFrame(),
                              factor * delta.getPoint());
}

T DeltaParameterVector::dot(const DeltaParameterVector &other) const {
  return frame.dot(other.frame) + point.dot(other.point);
}

void DeltaParameterVector::setAffineZero() {
  for (int frameInd = 1; frameInd < frameParameterOrder.numKeyFrames();
       ++frameInd)
    for (int camInd = 0; camInd < frameParameterOrder.numCameras(); ++camInd)
      affBlock(frameInd, camInd).setZero();
}

void DeltaParameterVector::constraintDepths(double maxAbsDeltaD) {
  for (T &d : point)
    if (std::abs(d) > maxAbsDeltaD)
      d = 0;
}

} // namespace mdso::optimize