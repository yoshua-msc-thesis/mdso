#include "system/Reprojector.h"

namespace mdso {

template <>
const StdVector<ImmaturePoint> &
Reprojector<ImmaturePoint>::getPoints(const KeyFrameEntry &entry) {
  return entry.immaturePoints;
}
template <>
const StdVector<OptimizedPoint> &
Reprojector<OptimizedPoint>::getPoints(const KeyFrameEntry &entry) {
  return entry.optimizedPoints;
}

template <>
double Reprojector<ImmaturePoint>::getDepth(const ImmaturePoint &p) {
  return p.depth;
}
template <>
double Reprojector<OptimizedPoint>::getDepth(const OptimizedPoint &p) {
  return p.depth();
}

template <typename PointType>
StdVector<Reprojection> Reprojector<PointType>::reproject() const {
  StdVector<Reprojection> reprojections;
  for (int targetCamInd = 0; targetCamInd < numCams; ++targetCamInd) {
    CameraModel &targetCam = cam->bundle[targetCamInd].cam;
    SE3 worldToTargetCam =
        cam->bundle[targetCamInd].bodyToThis * targetWorldToBody;
    for (int hostInd = 0; hostInd < numKeyFrames; ++hostInd) {
      SE3 hostBodyToTargetCam =
          worldToTargetCam * keyFrames[hostInd]->thisToWorld();
      for (int hostCamInd = 0; hostCamInd < numCams; ++hostCamInd) {
        SE3 hostCamToTargetCam =
            hostBodyToTargetCam * cam->bundle[hostCamInd].thisToBody;

        const KeyFrameEntry &host = keyFrames[hostInd]->frames[hostCamInd];
        const auto &points = getPoints(host);
        for (int pointInd = 0; pointInd < points.size(); ++pointInd) {
          const PointType &p = points[pointInd];
          Vec3 vInTarget = hostCamToTargetCam * (getDepth(p) * p.dir);
          if (!targetCam.isMappable(vInTarget))
            continue;
          Vec2 reprojected = targetCam.map(vInTarget);
          if (!targetCam.isOnImage(reprojected, borderSize))
            continue;

          Reprojection reprojection;
          reprojection.hostInd = hostInd;
          reprojection.hostCamInd = hostCamInd;
          reprojection.targetCamInd = targetCamInd;
          reprojection.pointInd = pointInd;
          reprojection.reprojected = reprojected;
          reprojection.reprojectedDepth = vInTarget.norm();

          reprojections.push_back(reprojection);
        }
      }
    }
  }

  return reprojections;
}

template class Reprojector<ImmaturePoint>;
template class Reprojector<OptimizedPoint>;

} // namespace mdso