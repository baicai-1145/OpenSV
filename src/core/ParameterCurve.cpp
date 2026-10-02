#include "ParameterCurve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <string>

namespace sv
{
namespace
{
double blickDistance(Blick later, Blick earlier)
{
    // Ordered signed positions may span more than the signed Blick range.
    return static_cast<double>(static_cast<std::uint64_t>(later) - static_cast<std::uint64_t>(earlier));
}

double sampleCubic(const ParameterCurve& curve, std::size_t rightIndex, double interval, double position)
{
    const auto& points = curve.points;
    const auto& left = points[rightIndex - 1];
    const auto& right = points[rightIndex];
    const double precedingValue = rightIndex > 1 ? points[rightIndex - 2].value : left.value;
    const double followingValue = rightIndex + 1 < points.size() ? points[rightIndex + 1].value : right.value;
    const double precedingInterval = rightIndex > 1 ? blickDistance(right.position, points[rightIndex - 2].position) : interval + 1.0;
    const double followingInterval = rightIndex + 1 < points.size() ? blickDistance(points[rightIndex + 1].position, left.position) : interval + 1.0;
    const double spacing = std::max(0.0, interval / static_cast<double>(blicksPerQuarter) - 0.25);
    const double weight = spacing <= 4.0 ? 1.0 / (spacing * 10.0 + 1.0) : 0.0;
    // The original evaluator blends in a per-Blick slope here, without scaling
    // it by the interval. Long segments consequently approach smoothstep.
    const double segmentSlope = (right.value - left.value) / interval * (1.0 - weight);
    const double leftTangent = (right.value - precedingValue) / precedingInterval * interval * weight + segmentSlope;
    const double rightTangent = (followingValue - left.value) / followingInterval * interval * weight + segmentSlope;
    const double squared = position * position;
    const double cubed = squared * position;
    return (2.0 * cubed - 3.0 * squared + 1.0) * left.value + (position + cubed - 2.0 * squared) * leftTangent + (3.0 * squared - 2.0 * cubed) * right.value + (cubed - squared) * rightTangent;
}
} // namespace

double sampleParameterCurve(const ParameterCurve& curve, Blick position, double defaultValue)
{
    if (curve.points.empty())
    {
        return defaultValue;
    }
    const auto& points = curve.points;
    if (position <= points.front().position)
    {
        return points.front().value;
    }
    if (position >= points.back().position)
    {
        return points.back().value;
    }
    const auto right = std::lower_bound(points.begin(), points.end(), position, [](const AutomationPoint& point, Blick value)
                                        { return point.position < value; });
    if (right->position == position)
    {
        return right->value;
    }
    const auto& left = *(right - 1);
    const double interval = blickDistance(right->position, left.position);
    double fraction = blickDistance(position, left.position) / interval;
    if (curve.mode == "cubic" && points.size() > 2)
    {
        return sampleCubic(curve, static_cast<std::size_t>(right - points.begin()), interval, fraction);
    }
    if (curve.mode == "cosine")
    {
        fraction = (1.0 - std::cos(fraction * std::numbers::pi)) * 0.5;
    }
    else if (curve.mode != "linear" && curve.mode != "cubic")
    {
        throw std::invalid_argument("Unsupported parameter curve interpolation mode: " + curve.mode);
    }
    return left.value * (1.0 - fraction) + right->value * fraction;
}
} // namespace sv
