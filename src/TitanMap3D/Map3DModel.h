#pragma once

#include <vector>

#include <QtCore/QString>

#include "MAVLinkLib.h"

/// The vehicle's 3D model, as Mission Planner's STLModelLoader picks and parses it.
namespace Map3D::Model {

/// Resource path of the built-in model for a vehicle type; quadPlane is Q_ENABLE set on a fixed-wing ArduPlane.
QString defaultModelPath(MAV_TYPE vehicleType, bool quadPlane);

/// Parses a binary or ASCII STL into interleaved position + normal floats (6 per vertex) in millimetres,
/// rotated -90 degrees about Z as Mission Planner does (nose +Y, right wing +X). Empty on failure.
std::vector<float> loadStl(const QString& path, QString* error = nullptr);

}  // namespace Map3D::Model
