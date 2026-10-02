#pragma once

#include <algorithm>
#include <cmath>

namespace sv
{
// Page relative to the current viewport, including seeks across several pages.
inline double playbackPageStart(double positionQuarter, double leftQuarter, double visibleQuarters)
{
    if (visibleQuarters <= 0.0 || positionQuarter < 0.0 || (positionQuarter >= leftQuarter && positionQuarter < leftQuarter + visibleQuarters))
    {
        return leftQuarter;
    }
    const auto pageOffset = std::floor((positionQuarter - leftQuarter) / visibleQuarters);
    return std::max(0.0, leftQuarter + pageOffset * visibleQuarters);
}
} // namespace sv
