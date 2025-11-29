#include "SprayOptimizer.h"

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

namespace {
constexpr double kRateFactor = 0.06;        // Q[L/min] = kRateFactor * R_d * v * S
constexpr double kSpeedStep = 0.2;          // m/s step for brute-force search
constexpr double kEpsilon = 1e-6;           // Generic numerical tolerance

struct DropletSpacingSample {
    double dropletMicron;
    double spacingMeters;
};

struct AxisScaleSample {
    double input;
    double scale;
};

constexpr DropletSpacingSample kDropletSpacingLut[] = {
    {35.0, 12.0},
    {80.0, 11.0},
    {150.0, 9.0},
    {220.0, 7.5},
    {280.0, 6.0},
    {350.0, 5.0},
};

constexpr AxisScaleSample kAltitudeScaleLut[] = {
    {1.5, 0.70},
    {2.0, 0.85},
    {3.0, 1.00},
    {4.0, 1.12},
    {5.0, 1.20},
    {6.0, 1.28},
};

constexpr AxisScaleSample kFlowScaleLut[] = {
    {0.5, 0.55},
    {0.8, 0.75},
    {1.2, 1.00},
    {1.6, 1.18},
    {2.0, 1.30},
    {2.5, 1.40},
    {3.0, 1.48},
    {3.5, 1.54},
    {4.0, 1.58},
};

double interpolateScale(double value, const AxisScaleSample* lut, size_t count)
{
    if (!lut || count == 0 || !qIsFinite(value) || value <= 0.0) {
        return qQNaN();
    }

    if (value <= lut[0].input) {
        return lut[0].scale;
    }

    const AxisScaleSample& last = lut[count - 1];
    if (value >= last.input) {
        return last.scale;
    }

    for (size_t i = 1; i < count; ++i) {
        const AxisScaleSample& hi = lut[i];
        const AxisScaleSample& lo = lut[i - 1];
        if (value <= hi.input) {
            const double span = hi.input - lo.input;
            const double t = qFuzzyIsNull(span) ? 0.0 : (value - lo.input) / span;
            return lo.scale + t * (hi.scale - lo.scale);
        }
    }

    return last.scale;
}
}

struct Candidate {
    double v;
    double S;
    double Q;
    double coverageWidth;
    double drift;
    double productivity;
};

SpraySolution SprayOptimizer::solve(const SprayInputs& in) const {
    SpraySolution solution;

    auto fail = [&](const QString& msg) -> SpraySolution {
        SpraySolution s;
        s.valid = false;
        s.errorMessage = msg;
        return s;
    };

    if (!qIsFinite(in.litersPerDekar) || in.litersPerDekar <= 0.0) {
        return fail(QStringLiteral("Application rate must be greater than zero."));
    }
    if (!qIsFinite(in.dropletMicron) || in.dropletMicron <= 0.0) {
        return fail(QStringLiteral("Droplet size must be greater than zero."));
    }
    if (!qIsFinite(in.altitudeMeters) || in.altitudeMeters <= 0.0) {
        return fail(QStringLiteral("Spray altitude must be greater than zero."));
    }
    if (!qIsFinite(in.overlapFactor) || in.overlapFactor <= 0.0 || in.overlapFactor > 1.0) {
        return fail(QStringLiteral("Overlap factor must be within (0, 1]."));
    }
    if (!qIsFinite(in.minSpeed) || !qIsFinite(in.maxSpeed) || in.minSpeed <= 0.0 || in.maxSpeed <= 0.0 || in.minSpeed > in.maxSpeed) {
        return fail(QStringLiteral("Vehicle speed limits invalid."));
    }
    if (!qIsFinite(in.minFlow) || !qIsFinite(in.maxFlow) || in.minFlow <= 0.0 || in.maxFlow <= 0.0 || in.minFlow > in.maxFlow) {
        return fail(QStringLiteral("Vehicle flow limits invalid."));
    }

    const double baseSpacing = spacingFromDroplet(in.dropletMicron);
    if (!qIsFinite(baseSpacing) || baseSpacing <= 0.0) {
        return fail(QStringLiteral("Unable to derive coverage width from droplet table."));
    }

    const double fallbackSpacingMin = qMax(0.5, baseSpacing * 0.5);
    const double fallbackSpacingMax = qMax(fallbackSpacingMin + 0.5, baseSpacing * 2.0);
    const double spacingMin = (qIsFinite(in.minSpacing) && in.minSpacing > 0.0) ? in.minSpacing : fallbackSpacingMin;
    const double rawSpacingMax = (qIsFinite(in.maxSpacing) && in.maxSpacing > 0.0) ? in.maxSpacing : fallbackSpacingMax;
    const double spacingMax = qMax(spacingMin + 0.1, rawSpacingMax);
    if (!qIsFinite(spacingMin) || !qIsFinite(spacingMax) || spacingMin <= 0.0 || spacingMin > spacingMax) {
        return fail(QStringLiteral("Invalid spacing search range."));
    }

    std::vector<Candidate> candidates;
    candidates.reserve(128);

    for (double v = in.minSpeed; v <= in.maxSpeed + kEpsilon; v += kSpeedStep) {
        const double slope = kRateFactor * in.litersPerDekar * v;
        if (!qIsFinite(slope) || slope <= 0.0) {
            continue;
        }

        const double flowSpacingMin = in.minFlow / slope;
        const double flowSpacingMax = in.maxFlow / slope;
        if (!qIsFinite(flowSpacingMin) || !qIsFinite(flowSpacingMax) ||
            flowSpacingMin <= 0.0 || flowSpacingMin > flowSpacingMax) {
            continue;
        }

        const double spacingLower = qMax(spacingMin, flowSpacingMin);
        const double spacingUpper = qMin(spacingMax, flowSpacingMax);
        if (!qIsFinite(spacingLower) || !qIsFinite(spacingUpper) || spacingLower <= 0.0 || spacingLower > spacingUpper + kEpsilon) {
            continue;
        }

        double spacing = qQNaN();
        double flowLpm = qQNaN();
        double coverageSample = qQNaN();
        if (!solveSpacingForSpeed(in, baseSpacing, v, spacingLower, spacingUpper, spacing, flowLpm, coverageSample)) {
            continue;
        }

        const double drift = computeDriftScore(in.dropletMicron, v, in.altitudeMeters);
        if (!qIsFinite(drift)) {
            continue;
        }

        Candidate c;
        c.v = v;
        c.S = spacing;
        c.Q = flowLpm;
        c.coverageWidth = coverageSample;
        c.drift = drift;
        c.productivity = v * spacing;
        candidates.push_back(c);
    }

    if (candidates.empty()) {
        return fail(QStringLiteral("No feasible spray configuration for spacing/speed search."));
    }

    // Stage 1: minimize drift
    auto minDriftIt = std::min_element(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.drift < b.drift;
    });
    const double minDrift = minDriftIt->drift;
    const double driftTolerance = std::fabs(minDrift) * 0.05 + 1e-3;

    std::vector<const Candidate*> driftFiltered;
    driftFiltered.reserve(candidates.size());
    for (const Candidate& c : candidates) {
        if (c.drift <= minDrift + driftTolerance) {
            driftFiltered.push_back(&c);
        }
    }

    // Stage 2: maximize productivity (v * S)
    auto maxProdIt = std::max_element(driftFiltered.begin(), driftFiltered.end(), [](const Candidate* a, const Candidate* b) {
        return a->productivity < b->productivity;
    });
    const double maxProd = (*maxProdIt)->productivity;
    const double prodTolerance = std::fabs(maxProd) * 0.02 + 1e-3;

    std::vector<const Candidate*> prodFiltered;
    prodFiltered.reserve(driftFiltered.size());
    for (const Candidate* c : driftFiltered) {
        if (c->productivity + prodTolerance >= maxProd) {
            prodFiltered.push_back(c);
        }
    }

    // Stage 3: tie-breaker by lowest flow rate (gentler on pump hardware)
    auto bestIt = std::min_element(prodFiltered.begin(), prodFiltered.end(), [](const Candidate* a, const Candidate* b) {
        return a->Q > b->Q;
    });
    const Candidate* best = (bestIt != prodFiltered.end()) ? *bestIt : nullptr;

    if (!best) {
        return fail(QStringLiteral("Optimizer failed to select a spray configuration."));
    }

    solution.valid = true;
    solution.vehicleSpeed = best->v;
    solution.spacing = best->S;
    solution.flowRate = best->Q;
    solution.driftScore = best->drift;
    solution.productivityScore = best->productivity;
    solution.coverageWidth = best->coverageWidth;
    return solution;
}

double SprayOptimizer::spacingFromDroplet(double dropletMicron) const {
    if (!qIsFinite(dropletMicron) || dropletMicron <= 0.0) {
        return qQNaN();
    }

    if (dropletMicron <= kDropletSpacingLut[0].dropletMicron) {
        return kDropletSpacingLut[0].spacingMeters;
    }
    const DropletSpacingSample& last = kDropletSpacingLut[std::size(kDropletSpacingLut) - 1];
    if (dropletMicron >= last.dropletMicron) {
        return last.spacingMeters;
    }

    for (size_t i = 1; i < std::size(kDropletSpacingLut); ++i) {
        const auto& hi = kDropletSpacingLut[i];
        const auto& lo = kDropletSpacingLut[i - 1];
        if (dropletMicron <= hi.dropletMicron) {
            const double span = hi.dropletMicron - lo.dropletMicron;
            const double t = qFuzzyIsNull(span) ? 0.0 : (dropletMicron - lo.dropletMicron) / span;
            return lo.spacingMeters + t * (hi.spacingMeters - lo.spacingMeters);
        }
    }

    return last.spacingMeters;
}

double SprayOptimizer::coverageWidth(double dropletMicron, double altitudeMeters, double flowLpm) const {
    if (!qIsFinite(dropletMicron) || dropletMicron <= 0.0 ||
        !qIsFinite(altitudeMeters) || altitudeMeters <= 0.0 ||
        !qIsFinite(flowLpm) || flowLpm <= 0.0) {
        return qQNaN();
    }

    const double baseSpacing = spacingFromDroplet(dropletMicron);
    if (!qIsFinite(baseSpacing) || baseSpacing <= 0.0) {
        return qQNaN();
    }

    const double altitudeScale = interpolateScale(altitudeMeters, kAltitudeScaleLut, std::size(kAltitudeScaleLut));
    const double flowScale = interpolateScale(flowLpm, kFlowScaleLut, std::size(kFlowScaleLut));
    if (!qIsFinite(altitudeScale) || !qIsFinite(flowScale)) {
        return qQNaN();
    }

    return qMax(0.0, baseSpacing * altitudeScale * flowScale);
}

double SprayOptimizer::computeDriftScore(double dropletMicron, double speed, double altitudeMeters) const {
    if (!qIsFinite(dropletMicron) || !qIsFinite(speed) || !qIsFinite(altitudeMeters) || dropletMicron <= 0.0) {
        return qQNaN();
    }

    constexpr double alpha = 15000.0;
    constexpr double beta = 0.6;
    constexpr double gamma = 0.25;
    return alpha / dropletMicron + beta * speed + gamma * altitudeMeters;
}

bool SprayOptimizer::solveSpacingForSpeed(const SprayInputs& in,
                                          double baseSpacing,
                                          double speed,
                                          double spacingLower,
                                          double spacingUpper,
                                          double& outSpacing,
                                          double& outFlowLpm,
                                          double& outCoverageWidth) const {
    if (!qIsFinite(baseSpacing) || baseSpacing <= 0.0 ||
        !qIsFinite(speed) || speed <= 0.0 ||
        !qIsFinite(spacingLower) || !qIsFinite(spacingUpper) ||
        spacingLower <= 0.0 || spacingUpper <= 0.0 || spacingLower - spacingUpper > kEpsilon) {
        return false;
    }

    spacingLower = qMax(0.01, spacingLower);
    spacingUpper = qMax(spacingLower, spacingUpper);

    const double slope = kRateFactor * in.litersPerDekar * speed;
    if (!qIsFinite(slope) || slope <= 0.0) {
        return false;
    }

    double spacing = std::clamp(baseSpacing * in.overlapFactor, spacingLower, spacingUpper);
    constexpr int kMaxIterations = 40;
    for (int iter = 0; iter < kMaxIterations; ++iter) {
        const double flowLpm = slope * spacing;
        if (!qIsFinite(flowLpm) || flowLpm <= 0.0) {
            return false;
        }

        const double coverage = coverageWidth(in.dropletMicron, in.altitudeMeters, flowLpm);
        if (!qIsFinite(coverage) || coverage <= 0.0) {
            return false;
        }

        const double targetSpacing = coverage * in.overlapFactor;
        const double boundedSpacing = std::clamp(targetSpacing, spacingLower, spacingUpper);
        if (std::fabs(boundedSpacing - spacing) < 1e-3) {
            outSpacing = boundedSpacing;
            outFlowLpm = slope * boundedSpacing;
            outCoverageWidth = coverage;
            return true;
        }

        spacing = boundedSpacing;
    }

    return false;
}
