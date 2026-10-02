#pragma once

#include "Project.h"

namespace sv
{
// Points must be sorted with unique positions, as established by normaliseProject.
// Empty curves use defaultValue; values outside the point range hold the endpoint.
[[nodiscard]] double sampleParameterCurve(const ParameterCurve& curve, Blick position, double defaultValue = 0.0);
} // namespace sv
