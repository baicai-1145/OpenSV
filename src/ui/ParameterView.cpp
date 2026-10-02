#include "ParameterView.h"

#include "StudioTheme.h"
#include "core/ParameterCurve.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sv
{
ParameterView::ParameterView(ProjectDocument& document) : document(document)
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    document.addChangeListener(this);
}

ParameterView::~ParameterView()
{
    document.removeChangeListener(this);
}

void ParameterView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(colours::background);
    const auto grid = getGridBounds();
    if (grid.isEmpty())
    {
        return;
    }

    graphics.setFont(juce::FontOptions(11.0f));
    for (const auto value : {300, 150, 0, -150, -300})
    {
        const auto y = yAt(value);
        graphics.setColour(value == 0 ? colours::raised.brighter(0.12f) : colours::panel);
        graphics.drawHorizontalLine(juce::roundToInt(y), grid.getX(), grid.getRight());
        graphics.setColour(colours::subdued);
        const auto label = value > 0 ? "+" + juce::String(value) : juce::String(value);
        const auto labelY = juce::jlimit(0, std::max(0, getHeight() - 16), juce::roundToInt(y) - 8);
        graphics.drawText(label + " c", 4, labelY, rulerWidth - 12, 16, juce::Justification::centredRight);
    }

    graphics.setColour(colours::border);
    graphics.drawVerticalLine(rulerWidth - 1, 0.0f, static_cast<float>(getHeight()));

    const juce::Graphics::ScopedSaveState state(graphics);
    graphics.reduceClipRegion(grid.toNearestInt());

    const auto leftQuarter = static_cast<double>(left) / static_cast<double>(blicksPerQuarter);
    const auto rightQuarter = leftQuarter + grid.getWidth() / pixelsPerQuarter;
    const auto step = pixelsPerQuarter >= 48.0 ? 1.0 : 4.0;
    for (auto quarter = std::floor(leftQuarter / step) * step; quarter <= rightQuarter; quarter += step)
    {
        const auto isBar = std::fmod(quarter, 4.0) == 0.0;
        graphics.setColour(isBar ? colours::raised : colours::panel);
        const auto x = grid.getX() + static_cast<float>((quarter - leftQuarter) * pixelsPerQuarter);
        graphics.drawVerticalLine(juce::roundToInt(x), grid.getY(), grid.getBottom());
    }

    const auto& points = getDisplayedPoints();
    if (!points.empty())
    {
        const auto& curve = getDisplayedCurve();
        juce::Path path;
        path.startNewSubPath(grid.getX(), yAt(sampleParameterCurve(curve, blickAt(grid.getX()))));
        for (float x = grid.getX() + 1.0f; x < grid.getRight(); x += 1.0f)
        {
            path.lineTo(x, yAt(sampleParameterCurve(curve, blickAt(x))));
        }
        path.lineTo(grid.getRight(), yAt(sampleParameterCurve(curve, blickAt(grid.getRight()))));
        graphics.setColour(colours::accent);
        graphics.strokePath(path, juce::PathStrokeType(1.6f));

        for (std::size_t index = 0; index < points.size(); ++index)
        {
            const auto x = xAt(points[index].position);
            const auto y = yAt(points[index].value);
            if (x < grid.getX() - 5.0f || x > grid.getRight() + 5.0f)
            {
                continue;
            }
            graphics.setColour(gesturePoint == index ? colours::text : colours::accent);
            graphics.fillEllipse(x - 3.5f, y - 3.5f, 7.0f, 7.0f);
            graphics.setColour(colours::background);
            graphics.fillEllipse(x - 1.5f, y - 1.5f, 3.0f, 3.0f);
        }
    }

    const auto playheadX = xAt(playhead - document.getActiveGroupOffset());
    graphics.setColour(juce::Colour(0xfff4bc64));
    graphics.drawVerticalLine(juce::roundToInt(playheadX), grid.getY(), grid.getBottom());
}

void ParameterView::mouseDown(const juce::MouseEvent& event)
{
    if (!getGridBounds().contains(event.position))
    {
        return;
    }
    grabKeyboardFocus();
    const auto hit = pointAt(event.position);
    if (event.mods.isPopupMenu())
    {
        if (hit.has_value())
        {
            deletePoint(*hit);
        }
        return;
    }
    if (!event.mods.isLeftButtonDown())
    {
        return;
    }

    gestureCurve = document.getActiveGroup().pitchDelta;
    gestureTrackIndex = document.getActiveTrackIndex();
    gestureGroupId = document.getActiveGroup().id;
    gestureRevision = document.getRevision();
    gestureChanged = false;
    gesturePoint = hit;

    if (!gesturePoint.has_value())
    {
        const AutomationPoint point{blickAt(event.position.x), centsAt(event.position.y)};
        const auto insertion = std::lower_bound(gestureCurve.points.begin(), gestureCurve.points.end(), point.position, [](const AutomationPoint& existing, Blick position)
                                                { return existing.position < position; });
        gesturePoint = static_cast<std::size_t>(std::distance(gestureCurve.points.begin(), insertion));
        if (insertion != gestureCurve.points.end() && insertion->position == point.position)
        {
            insertion->value = point.value;
        }
        else
        {
            gestureCurve.points.insert(insertion, point);
        }
        gestureChanged = true;
    }
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    repaint();
}

void ParameterView::mouseDrag(const juce::MouseEvent& event)
{
    if (!gesturePoint.has_value())
    {
        return;
    }
    const auto index = *gesturePoint;
    auto position = blickAt(event.position.x);
    if (index > 0)
    {
        position = std::max(position, gestureCurve.points[index - 1].position + 1);
    }
    if (index + 1 < gestureCurve.points.size())
    {
        position = std::min(position, gestureCurve.points[index + 1].position - 1);
    }
    auto& point = gestureCurve.points[index];
    const auto value = centsAt(event.position.y);
    if (point.position != position || point.value != value)
    {
        point = {position, value};
        gestureChanged = true;
        repaint();
    }
}

void ParameterView::mouseUp(const juce::MouseEvent&)
{
    commitGesture();
}

void ParameterView::mouseMove(const juce::MouseEvent& event)
{
    setMouseCursor(pointAt(event.position).has_value() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor);
}

bool ParameterView::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && gesturePoint.has_value())
    {
        cancelGesture();
        return true;
    }
    return false;
}

void ParameterView::setView(double quarterWidth, Blick leftPosition)
{
    if (!std::isfinite(quarterWidth) || quarterWidth <= 0.0)
    {
        return;
    }
    pixelsPerQuarter = quarterWidth;
    left = std::max(Blick{0}, leftPosition);
    repaint();
}

void ParameterView::setPlayhead(Blick absolutePosition)
{
    if (playhead != absolutePosition)
    {
        playhead = absolutePosition;
        repaint();
    }
}

void ParameterView::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (gesturePoint.has_value() && (gestureRevision != document.getRevision() || gestureTrackIndex != document.getActiveTrackIndex() || gestureGroupId != document.getActiveGroup().id))
    {
        cancelGesture();
    }
    repaint();
}

juce::Rectangle<float> ParameterView::getGridBounds() const
{
    return {static_cast<float>(rulerWidth), 8.0f, static_cast<float>(std::max(0, getWidth() - rulerWidth - scrollBarSize)), static_cast<float>(std::max(0, getHeight() - 16))};
}

float ParameterView::xAt(Blick position) const
{
    return static_cast<float>(rulerWidth + (static_cast<double>(position) - static_cast<double>(left)) / static_cast<double>(blicksPerQuarter) * pixelsPerQuarter);
}

float ParameterView::yAt(double cents) const
{
    const auto grid = getGridBounds();
    return grid.getCentreY() - static_cast<float>(cents / rangeCents * grid.getHeight() * 0.5);
}

Blick ParameterView::blickAt(float x) const
{
    const auto position = static_cast<double>(left) + (static_cast<double>(x) - rulerWidth) / pixelsPerQuarter * static_cast<double>(blicksPerQuarter);
    return static_cast<Blick>(std::llround(std::max(0.0, position)));
}

double ParameterView::centsAt(float y) const
{
    const auto grid = getGridBounds();
    if (grid.getHeight() <= 0.0f)
    {
        return 0.0;
    }
    const auto value = static_cast<double>((grid.getCentreY() - y) / (grid.getHeight() * 0.5f)) * rangeCents;
    return std::round(juce::jlimit(-rangeCents, rangeCents, value));
}

const ParameterCurve& ParameterView::getDisplayedCurve() const
{
    return gesturePoint.has_value() ? gestureCurve : document.getActiveGroup().pitchDelta;
}

const std::vector<AutomationPoint>& ParameterView::getDisplayedPoints() const
{
    return getDisplayedCurve().points;
}

std::optional<std::size_t> ParameterView::pointAt(juce::Point<float> position) const
{
    if (!getGridBounds().contains(position))
    {
        return std::nullopt;
    }
    const auto& points = getDisplayedPoints();
    auto closestDistance = 9.0f * 9.0f;
    std::optional<std::size_t> closest;
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        const juce::Point<float> point{xAt(points[index].position), yAt(points[index].value)};
        const auto distance = point.getDistanceSquaredFrom(position);
        if (distance <= closestDistance)
        {
            closestDistance = distance;
            closest = index;
        }
    }
    return closest;
}

void ParameterView::cancelGesture()
{
    gesturePoint.reset();
    gestureCurve.points.clear();
    gestureChanged = false;
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    repaint();
}

void ParameterView::commitGesture()
{
    if (!gesturePoint.has_value())
    {
        return;
    }
    if (!gestureChanged || gestureRevision != document.getRevision() || gestureTrackIndex != document.getActiveTrackIndex() || gestureGroupId != document.getActiveGroup().id)
    {
        cancelGesture();
        return;
    }

    const auto trackIndex = gestureTrackIndex;
    const auto groupId = gestureGroupId;
    auto points = std::move(gestureCurve.points);
    cancelGesture();
    document.performEdit(juce::String::fromUTF8("编辑音高偏移"), [trackIndex, groupId, points = std::move(points)](Project& project)
                         {
        if (trackIndex >= 0 && trackIndex < static_cast<int>(project.tracks.size()))
        {
            auto& group = project.tracks[static_cast<std::size_t>(trackIndex)].mainGroup;
            if (group.id == groupId)
            {
                group.pitchDelta.points = points;
            }
        } });
}

void ParameterView::deletePoint(std::size_t index)
{
    const auto trackIndex = document.getActiveTrackIndex();
    document.performEdit(juce::String::fromUTF8("删除音高偏移控制点"), [trackIndex, index](Project& project)
                         {
        if (trackIndex >= 0 && trackIndex < static_cast<int>(project.tracks.size()))
        {
            auto& points = project.tracks[static_cast<std::size_t>(trackIndex)].mainGroup.pitchDelta.points;
            if (index < points.size())
            {
                points.erase(points.begin() + static_cast<std::ptrdiff_t>(index));
            }
        } });
}
} // namespace sv
