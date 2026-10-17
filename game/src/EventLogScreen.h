#pragma once

#include <arcbit/events/EventBus.h>
#include <arcbit/ui/UIScreen.h>

#include <chrono>
#include <deque>
#include <string>
#include <vector>

namespace Arcbit
{
class Label;
}

// ---------------------------------------------------------------------------
// EventHistory - the last few events delivered on a bus, with when they were
// delivered. Listens from construction, so the F3 overlay can show what
// happened before it was opened.
// ---------------------------------------------------------------------------
class EventHistory
{
public:
    struct Line
    {
        Arcbit::f32 Seconds = 0.0f; // since the history started
        std::string Name;
        std::string Payload;
        bool        Engine = false; // an engine.* event
    };

    explicit EventHistory(Arcbit::EventBus& bus, size_t capacity = 16);

    [[nodiscard]] const std::deque<Line>& Lines() const { return _lines; }
    [[nodiscard]] size_t                  Capacity() const { return _capacity; }

private:
    std::deque<Line>                      _lines;
    size_t                                _capacity;
    std::chrono::steady_clock::time_point _start;
    Arcbit::Subscription                  _all;
};

// ---------------------------------------------------------------------------
// EventLogScreen - F3 in the demo app: an overlay listing the recent events,
// newest at the bottom; game events stand out from the engine's. Doesn't
// take input, and stays above scene HUDs.
// ---------------------------------------------------------------------------
class EventLogScreen : public Arcbit::UIScreen
{
public:
    const EventHistory* History = nullptr;

    EventLogScreen()
    {
        StaysOnTop      = true;
        TransitionSpeed = 6.0f;
    }

    void OnEnter() override;
    void OnTick(Arcbit::f32 dt, const Arcbit::InputManager& input) override;

private:
    std::vector<Arcbit::Label*> _rows;

    void Refresh();
};
