#include "optimize/EnergyFunction.h"
#include "optimize/Accumulator.h"

namespace mdso::optimize {

std::unique_ptr<ceres::LossFunction> getLoss(Settings::Optimization::Loss type,
                                             double outlierDiff) {
  using loss_ptr = std::unique_ptr<ceres::LossFunction>;

  switch (type) {
  case Settings::Optimization::TRIVIAL:
    return loss_ptr(new ceres::TrivialLoss());
  case Settings::Optimization::HUBER:
    return loss_ptr(new ceres::HuberLoss(outlierDiff));
  default:
    return loss_ptr(new ceres::TrivialLoss());
  }
}

EnergyFunction::EnergyFunction(CameraBundle *camBundle, KeyFrame **keyFrames,
                               int numKeyFrames,
                               const EnergyFunctionSettings &settings)
    : parameters(camBundle, keyFrames, numKeyFrames)
    , lossFunction(getLoss(settings.optimization.lossType,
                           settings.residual.intensity.outlierDiff))
    , cam(camBundle)
    , settings(settings) {
  CHECK(numKeyFrames >= 2);

  int PH = settings.residual.residualPattern.height;

  PrecomputedHostToTarget hostToTarget(cam, &parameters);
  std::vector<OptimizedPoint *> optimizedPoints;

  for (int hostInd = 0; hostInd < numKeyFrames; ++hostInd)
    for (int hostCamInd = 0; hostCamInd < cam->bundle.size(); ++hostCamInd) {
      for (OptimizedPoint &op :
           keyFrames[hostInd]->frames[hostCamInd].optimizedPoints) {
        if (op.state != OptimizedPoint::ACTIVE)
          continue;
        Vec3t ray = (op.depth() * op.dir).cast<T>();
        bool hasResiduals = false;
        for (int targetInd = 0; targetInd < numKeyFrames; ++targetInd) {
          if (hostInd == targetInd)
            continue;
          for (int targetCamInd = 0; targetCamInd < cam->bundle.size();
               ++targetCamInd) {
            SE3t hostToTargetImage =
                hostToTarget.get(hostInd, hostCamInd, targetInd, targetCamInd);
            Vec3t rayTarget = hostToTargetImage * ray;
            CameraModel &camTarget = cam->bundle[targetCamInd].cam;
            if (!camTarget.isMappable(rayTarget))
              continue;
            Vec2t pointTarget = camTarget.map(rayTarget);
            if (!camTarget.isOnImage(pointTarget.cast<double>(), PH))
              continue;

            if (!hasResiduals) {
              hasResiduals = true;
              optimizedPoints.push_back(&op);
            }

            residuals.emplace_back(hostInd, hostCamInd, targetInd, targetCamInd,
                                   optimizedPoints.size() - 1, cam,
                                   &keyFrames[hostInd]->frames[hostCamInd],
                                   &keyFrames[targetInd]->frames[targetCamInd],
                                   &op, op.logDepth, hostToTargetImage,
                                   lossFunction.get(), settings.residual);
          }
        }
      }
    }

  parameters.setPoints(std::move(optimizedPoints));

  LOG(INFO) << "Created EnergyFunction with " << residuals.size()
            << " residuals\n";
}

int EnergyFunction::numPoints() const { return parameters.numPoints(); }

VecRt EnergyFunction::getResidualValues(int residualInd) {
  CHECK_GE(residualInd, 0);
  CHECK_LT(residualInd, residuals.size());
  return getAllValues().values(residualInd);
}

Hessian EnergyFunction::getHessian() {
  PrecomputedHostToTarget hostToTarget(cam, &parameters);
  PrecomputedMotionDerivatives motionDerivatives(cam, &parameters);
  PrecomputedLightHostToTarget lightHostToTarget(&parameters);
  const Values &values = getAllValues(hostToTarget, lightHostToTarget);
  const Derivatives &derivatives =
      getDerivatives(hostToTarget, motionDerivatives, lightHostToTarget);
  return getHessian(values, derivatives);
}

Hessian EnergyFunction::getHessian(const Values &precomputedValues,
                                   const Derivatives &precomputedDerivatives) {
  Hessian::AccumulatedBlocks accumulatedBlocks(parameters.numKeyFrames(),
                                               parameters.camBundleSize(),
                                               parameters.numPoints());
  for (int ri = 0; ri < residuals.size(); ++ri) {
    const Residual &residual = residuals[ri];
    Residual::DeltaHessian deltaHessian =
        residual.getDeltaHessian(precomputedValues.values(ri),
                                 precomputedDerivatives.residualJacobians[ri]);
    accumulatedBlocks.add(residual, deltaHessian);
  }

  return Hessian(accumulatedBlocks,
                 precomputedDerivatives.parametrizationJacobians,
                 settings.optimization);
}

Gradient EnergyFunction::getGradient() {
  PrecomputedHostToTarget hostToTarget(cam, &parameters);
  PrecomputedMotionDerivatives motionDerivatives(cam, &parameters);
  PrecomputedLightHostToTarget lightHostToTarget(&parameters);
  const Values &valuesRef = getAllValues(hostToTarget, lightHostToTarget);
  const Derivatives &derivativesRef =
      getDerivatives(hostToTarget, motionDerivatives, lightHostToTarget);
  return getGradient(valuesRef, derivativesRef);
}

Gradient
EnergyFunction::getGradient(const Values &precomputedValues,
                            const Derivatives &precomputedDerivatives) {
  Gradient::AccumulatedBlocks accumulatedBlocks(parameters.numKeyFrames(),
                                                parameters.camBundleSize(),
                                                parameters.numPoints());

  for (int ri = 0; ri < residuals.size(); ++ri) {
    const Residual &residual = residuals[ri];
    Residual::DeltaGradient deltaGradient =
        residual.getDeltaGradient(precomputedValues.values(ri),
                                  precomputedDerivatives.residualJacobians[ri]);
    accumulatedBlocks.add(residual, deltaGradient);
  }

  return Gradient(accumulatedBlocks,
                  precomputedDerivatives.parametrizationJacobians);
}

void EnergyFunction::precomputeValuesAndDerivatives() {
  PrecomputedHostToTarget hostToTarget(cam, &parameters);
  PrecomputedMotionDerivatives motionDerivatives(cam, &parameters);
  PrecomputedLightHostToTarget lightHostToTarget(&parameters);
  getAllValues(hostToTarget, lightHostToTarget);
  getDerivatives(hostToTarget, motionDerivatives, lightHostToTarget);
}

void EnergyFunction::clearPrecomputations() {
  values.reset();
  derivatives.reset();
}

void EnergyFunction::optimize(int maxIterations) {
  T lambda = settings.optimization.initialLambda;
  auto hostToTarget = precomputeHostToTarget();
  auto motionDerivatives = precomputeMotionDerivatives();
  auto lightHostToTarget = precomputeLightHostToTarget();
  Values curValues = createValues(hostToTarget, lightHostToTarget);
  Derivatives curDerivatives = createDerivatives(
      curValues, hostToTarget, motionDerivatives, lightHostToTarget);
  Hessian hessian = getHessian(curValues, curDerivatives);
  Gradient gradient = getGradient(curValues, curDerivatives);
  bool parametersUpdated = false;
  for (int it = 0; it < maxIterations; ++it) {
    std::cout << "it = " << it << "\n";
    TimePoint start, end;
    start = now();

    T curEnergy = curValues.totalEnergy();
    std::cout << "cur energy = " << curEnergy << "\n";

    if (parametersUpdated) {
      motionDerivatives = precomputeMotionDerivatives();
      curDerivatives = createDerivatives(curValues, hostToTarget,
                                         motionDerivatives, lightHostToTarget);
      hessian = getHessian(curValues, curDerivatives);
      gradient = getGradient(curValues, curDerivatives);
    }

    Hessian dampedHessian = hessian.levenbergMarquardtDamp(lambda);

    DeltaParameterVector delta = dampedHessian.solve(gradient);

    Parameters::State savedState = parameters.saveState();
    parameters.update(delta);

    auto newHostToTarget = precomputeHostToTarget();
    auto newLightHostToTarget = precomputeLightHostToTarget();
    Values newValues = createValues(newHostToTarget, newLightHostToTarget);

    T newEnergy = newValues.totalEnergy();
    std::cout << "new energy = " << newEnergy
              << " delta = " << newEnergy - curEnergy << "\n";

    LOG(INFO) << "optimization step #" << it << ": curEnergy = " << curEnergy
              << " newEnergy = " << newEnergy;

    if (newEnergy >= curEnergy) {
      parameters.recoverState(std::move(savedState));
      lambda *= settings.optimization.failMultiplier;
      parametersUpdated = false;
    } else {
      lambda *= settings.optimization.successMultiplier;
      curValues = std::move(newValues);
      hostToTarget = std::move(newHostToTarget);
      lightHostToTarget = std::move(newLightHostToTarget);
      parametersUpdated = true;
    }

    end = now();
    LOG(INFO) << "step took " << secondsBetween(start, end);
  }

  parameters.apply();
}

EnergyFunction::Values::Values(const StdVector<Residual> &residuals,
                               const Parameters &parameters,
                               const ceres::LossFunction *lossFunction,
                               PrecomputedHostToTarget &hostToTarget,
                               PrecomputedLightHostToTarget &lightHostToTarget)
    : lossFunction(lossFunction) {
  valsAndCache.reserve(residuals.size());
  for (const Residual &res : residuals) {
    int hi = res.hostInd(), hci = res.hostCamInd(), ti = res.targetInd(),
        tci = res.targetCamInd(), pi = res.pointInd();
    valsAndCache.push_back(
        {VecRt(res.patternSize()), Residual::CachedValues(res.patternSize())});
    valsAndCache.back().first =
        res.getValues(hostToTarget.get(hi, hci, ti, tci),
                      lightHostToTarget.get(hi, hci, ti, tci),
                      parameters.logDepth(pi), &valsAndCache.back().second);
  }
}

const VecRt &EnergyFunction::Values::values(int residualInd) const {
  CHECK_GE(residualInd, 0);
  CHECK_LT(residualInd, valsAndCache.size());
  return valsAndCache[residualInd].first;
}

const Residual::CachedValues &
EnergyFunction::Values::cachedValues(int residualInd) const {
  CHECK_GE(residualInd, 0);
  CHECK_LT(residualInd, valsAndCache.size());
  return valsAndCache[residualInd].second;
}

T EnergyFunction::Values::totalEnergy() const {
  CHECK_GT(valsAndCache.size(), 0);
  int patternSize = valsAndCache[0].first.size();
  Accumulator<T> energy;
  for (const auto &[vals, cache] : valsAndCache)
    for (int i = 0; i < patternSize; ++i) {
      double v2 = vals[i] * vals[i];
      double rho[3];
      lossFunction->Evaluate(v2, rho);
      energy += T(rho[0]);
    }
  return energy.accumulated();
}

EnergyFunction::PrecomputedHostToTarget::PrecomputedHostToTarget(
    CameraBundle *cam, const Parameters *parameters)
    : parameters(parameters)
    , camToBody(cam->bundle.size())
    , bodyToCam(cam->bundle.size())
    , hostToTarget(
          boost::extents[parameters->numKeyFrames()][cam->bundle.size()]
                        [parameters->numKeyFrames()][cam->bundle.size()]) {
  for (int ci = 0; ci < cam->bundle.size(); ++ci) {
    camToBody[ci] = cam->bundle[ci].thisToBody.cast<T>();
    bodyToCam[ci] = cam->bundle[ci].bodyToThis.cast<T>();
  }

  for (int hostInd = 0; hostInd < parameters->numKeyFrames(); ++hostInd) {
    for (int targetInd = 0; targetInd < parameters->numKeyFrames();
         ++targetInd) {
      if (hostInd == targetInd)
        continue;
      SE3t hostBodyToTargetBody =
          parameters->getBodyToWorld(targetInd).inverse() *
          parameters->getBodyToWorld(hostInd);
      for (int hostCamInd = 0; hostCamInd < cam->bundle.size(); ++hostCamInd) {
        SE3t hostFrameToTargetBody =
            hostBodyToTargetBody * camToBody[hostCamInd];
        for (int targetCamInd = 0; targetCamInd < cam->bundle.size();
             ++targetCamInd) {
          hostToTarget[hostInd][hostCamInd][targetInd][targetCamInd] =
              bodyToCam[targetCamInd] * hostFrameToTargetBody;
        }
      }
    }
  }
}

SE3t EnergyFunction::PrecomputedHostToTarget::get(int hostInd, int hostCamInd,
                                                  int targetInd,
                                                  int targetCamInd) {
  return hostToTarget[hostInd][hostCamInd][targetInd][targetCamInd];
}

EnergyFunction::PrecomputedMotionDerivatives::PrecomputedMotionDerivatives(
    CameraBundle *cam, const Parameters *parameters)
    : parameters(parameters)
    , camToBody(cam->bundle.size())
    , bodyToCam(cam->bundle.size())
    , hostToTargetDiff(
          boost::extents[parameters->numKeyFrames()][cam->bundle.size()]
                        [parameters->numKeyFrames()][cam->bundle.size()]) {
  for (int ci = 0; ci < cam->bundle.size(); ++ci) {
    camToBody[ci] = cam->bundle[ci].thisToBody.cast<T>();
    bodyToCam[ci] = cam->bundle[ci].bodyToThis.cast<T>();
  }
}

const MotionDerivatives &EnergyFunction::PrecomputedMotionDerivatives::get(
    int hostInd, int hostCamInd, int targetInd, int targetCamInd) {
  std::optional<MotionDerivatives> &derivatives =
      hostToTargetDiff[hostInd][hostCamInd][targetInd][targetCamInd];
  if (derivatives)
    return derivatives.value();
  derivatives.emplace(
      camToBody[hostCamInd], parameters->getBodyToWorld(hostInd),
      parameters->getBodyToWorld(targetInd), bodyToCam[targetCamInd]);
  return derivatives.value();
}

EnergyFunction::PrecomputedLightHostToTarget::PrecomputedLightHostToTarget(
    const Parameters *parameters)
    : parameters(parameters)
    , lightHostToTarget(boost::extents[parameters->numKeyFrames()]
                                      [parameters->camBundleSize()]
                                      [parameters->numKeyFrames()]
                                      [parameters->camBundleSize()]) {
  //  for (int hostInd = 0; hostInd < parameters->numKeyFrames(); ++hostInd)
  //    for (int hostCa)
}

AffLightT EnergyFunction::PrecomputedLightHostToTarget::get(int hostInd,
                                                            int hostCamInd,
                                                            int targetInd,
                                                            int targetCamInd) {
  std::optional<AffLightT> &result =
      lightHostToTarget[hostInd][hostCamInd][targetInd][targetCamInd];
  if (result)
    return result.value();
  result.emplace(
      parameters->getLightWorldToFrame(targetInd, targetCamInd) *
      parameters->getLightWorldToFrame(hostInd, hostCamInd).inverse());
  return result.value();
}

EnergyFunction::Derivatives::Derivatives(
    const Parameters &parameters, const StdVector<Residual> &residuals,
    const Values &values, PrecomputedHostToTarget &hostToTarget,
    PrecomputedMotionDerivatives &motionDerivatives,
    PrecomputedLightHostToTarget &lightHostToTarget)
    : parametrizationJacobians(parameters) {
  residualJacobians.reserve(residuals.size());
  for (int ri = 0; ri < residuals.size(); ++ri) {
    const Residual &res = residuals[ri];
    int hi = res.hostInd(), hci = res.hostCamInd(), ti = res.targetInd(),
        tci = res.targetCamInd(), pi = res.pointInd();
    SE3t curHostToTarget = hostToTarget.get(hi, hci, ti, tci);
    AffLightT curLightHostToTarget = lightHostToTarget.get(hi, hci, ti, tci);
    AffLightT lightWorldToHost = parameters.getLightWorldToFrame(hi, hci);
    const MotionDerivatives &dHostToTarget =
        motionDerivatives.get(hi, hci, ti, tci);

    T logDepth = parameters.logDepth(pi);
    residualJacobians.push_back(res.getJacobian(
        curHostToTarget, dHostToTarget, lightWorldToHost, curLightHostToTarget,
        logDepth, values.cachedValues(ri)));
  }
}

EnergyFunction::PrecomputedHostToTarget
EnergyFunction::precomputeHostToTarget() const {
  return PrecomputedHostToTarget(cam, &parameters);
}
EnergyFunction::PrecomputedMotionDerivatives
EnergyFunction::precomputeMotionDerivatives() const {
  return PrecomputedMotionDerivatives(cam, &parameters);
}

EnergyFunction::PrecomputedLightHostToTarget
EnergyFunction::precomputeLightHostToTarget() const {
  return PrecomputedLightHostToTarget(&parameters);
}

EnergyFunction::Values
EnergyFunction::createValues(PrecomputedHostToTarget &hostToTarget,
                             PrecomputedLightHostToTarget &lightHostToTarget) {
  return Values(residuals, parameters, lossFunction.get(), hostToTarget,
                lightHostToTarget);
}

EnergyFunction::Values &EnergyFunction::getAllValues() {
  PrecomputedHostToTarget hostToTarget = precomputeHostToTarget();
  PrecomputedLightHostToTarget lightHostToTarget =
      precomputeLightHostToTarget();
  return getAllValues(hostToTarget, lightHostToTarget);
}

EnergyFunction::Values &
EnergyFunction::getAllValues(PrecomputedHostToTarget &hostToTarget,
                             PrecomputedLightHostToTarget &lightHostToTarget) {
  if (!values)
    values.emplace(residuals, parameters, lossFunction.get(), hostToTarget,
                   lightHostToTarget);
  return values.value();
}

EnergyFunction::Derivatives EnergyFunction::createDerivatives(
    const Values &values, PrecomputedHostToTarget &hostToTarget,
    PrecomputedMotionDerivatives &motionDerivatives,
    PrecomputedLightHostToTarget &lightHostToTarget) {
  return Derivatives(parameters, residuals, values, hostToTarget,
                     motionDerivatives, lightHostToTarget);
}

EnergyFunction::Derivatives &EnergyFunction::getDerivatives(
    PrecomputedHostToTarget &hostToTarget,
    PrecomputedMotionDerivatives &motionDerivatives,
    PrecomputedLightHostToTarget &lightHostToTarget) {
  if (!derivatives)
    derivatives.emplace(parameters, residuals,
                        getAllValues(hostToTarget, lightHostToTarget),
                        hostToTarget, motionDerivatives, lightHostToTarget);
  return derivatives.value();
}

} // namespace mdso::optimize