#pragma once

#include <QtGlobal>
#include <QString>

// Pure logic helper for agricultural spray optimization.
struct SprayInputs {
    double litersPerDekar = qQNaN();     // R_d: desired application rate [L/dekar]
    double dropletMicron = qQNaN();      // Target D50 droplet size [microns]
    double altitudeMeters = qQNaN();     // Spray altitude [m]
    double overlapFactor = qQNaN();      // Fractional overlap target (0-1)

    double minSpeed = qQNaN();           // Vehicle minimum speed [m/s]
    double maxSpeed = qQNaN();           // Vehicle maximum speed [m/s]
    double minFlow = qQNaN();            // Pump minimum flow [L/min]
    double maxFlow = qQNaN();            // Pump maximum flow [L/min]

    double minSpacing = qQNaN();         // Search lower bound for spacing [m]
    double maxSpacing = qQNaN();         // Search upper bound for spacing [m]
};

struct SpraySolution {
    bool    valid = false;
    QString errorMessage;

    double vehicleSpeed = qQNaN();   // v_opt [m/s]
    double spacing = qQNaN();        // S_opt [m]
    double flowRate = qQNaN();       // Q_opt [L/min]

    double driftScore = qQNaN();
    double productivityScore = qQNaN(); // v * S
    double coverageWidth = qQNaN();
};

class SprayOptimizer {
public:
    SpraySolution solve(const SprayInputs& in) const;

private:
    // TODO: Replace the placeholder lookup below with measured nozzle spacing data.
    double spacingFromDroplet(double dropletMicron) const;
    double coverageWidth(double dropletMicron, double altitudeMeters, double flowLpm) const;
    double computeDriftScore(double dropletMicron, double speed, double altitudeMeters) const;
    bool solveSpacingForSpeed(const SprayInputs& in,
                              double baseSpacing,
                              double speed,
                              double spacingLower,
                              double spacingUpper,
                              double& outSpacing,
                              double& outFlowLpm,
                              double& outCoverageWidth) const;
};
