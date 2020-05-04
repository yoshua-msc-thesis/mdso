#include "optimize/StepController.h"

namespace mdso::optimize {

StepController::StepController(
    const Settings::Optimization::StepControl &newSettings)
    : mLambda(newSettings.initialLambda)
    , failMultiplier(newSettings.initialFailMultiplier)
    , settings(newSettings) {}

bool StepController::newStep(double oldEnergy, double newEnergy,
                             double predictedEnergy) {
  double predictedDiff = oldEnergy - predictedEnergy;
  double actualDiff = oldEnergy - newEnergy;
  LOG(INFO) << "actualDiff = " << actualDiff
            << " predictedDiff = " << predictedDiff
            << " relative diff = " << actualDiff / predictedDiff;
  if (predictedDiff < 0) {
    predictedDiff *= -1;
    actualDiff *= -1;
  }

  double oldLambda = mLambda;
  double predictionQuality = actualDiff / predictedDiff;
  double q2m1 = 2 * predictionQuality - 1;
  bool isStepSuccessful = predictionQuality > settings.acceptedQuality;
  if (isStepSuccessful) {
    mLambda *= std::max(settings.minLambdaMultiplier, 1.0 - q2m1 * q2m1 * q2m1);
    failMultiplier = settings.initialFailMultiplier;
  } else {
    mLambda *= failMultiplier;
    failMultiplier *= settings.failMultiplierMultiplier;
  }
  LOG(INFO) << "lambda: " << oldLambda << " -> " << mLambda;

  return isStepSuccessful;
}

double StepController::lambda() const { return mLambda; }

} // namespace mdso::optimize