#include "settings.h"
#include "defs.h"

#include <opencv2/core.hpp>

namespace fishdso {

// Point-of-interest detector
double settingGradThreshold1 = 30.0;
double settingGradThreshold2 = 6.0;
double settingGradThreshold3 = 3.0;

int settingInitialAdaptiveBlockSize = 25;
int settingInterestPointsAdaptTo = 2150;
int settingInterestPointsUsed = 2000;

int settingCameraMapPolyDegree = 10;
int settingCameraMapPolyPoints = 2000;

double settingInitKeypointsObserveAngle = M_PI_2;
float settingMatchNonMove = 9.0;
int settingFirstFramesSkip = 0;
double settingOrbInlierProb = 0.5;
double settingEssentialSuccessProb = 0.999;
double settingEssentialReprojErrThreshold = 4.0;
double settingRemoveResidualsRatio = 0.5;

int settingHalfFillingFilterSize = 1;

} // namespace fishdso
