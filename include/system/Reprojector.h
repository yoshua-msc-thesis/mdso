#ifndef INCLUDE_REPROJECTOR
#define INCLUDE_REPROJECTOR

#include "system/KeyFrame.h"

namespace mdso {

struct Reprojection {
  int hostInd;
  int hostCamInd;
  int targetCamInd;
  int pointInd;
  Vec2 reprojected;
  double reprojectedDepth;
};

template <typename PointType> class Reprojector {
public:
  Reprojector(const KeyFrame *const *keyFrames, int numKeyFrames,
              const SE3 &targetBodyToWorld, int borderSize = 0)
      : targetWorldToBody(targetBodyToWorld.inverse())
      , keyFrames(keyFrames, keyFrames + numKeyFrames)
      , cam(numKeyFrames <= 0 ? nullptr : keyFrames[0]->preKeyFrame->cam)
      , borderSize(borderSize)
      , numCams(cam->bundle.size())
      , numKeyFrames(numKeyFrames) {
    CHECK_GE(numKeyFrames, 0);
  }

  StdVector<Reprojection> reproject() const;

private:
  static const StdVector<PointType> &getPoints(const KeyFrameEntry &entry);
  static double getDepth(const PointType &p);

  SE3 targetWorldToBody;
  std::vector<const KeyFrame *> keyFrames;
  CameraBundle *cam;
  int borderSize;
  int numCams;
  int numKeyFrames;
};

} // namespace mdso

#endif