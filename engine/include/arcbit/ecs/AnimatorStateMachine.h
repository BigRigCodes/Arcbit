#pragma once

#include <arcbit/core/Types.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Arcbit
{
class SpriteSheet;

// ---------------------------------------------------------------------------
// AnimatorCondition
//
// A single predicate evaluated against a named parameter on the state machine.
// All conditions in a transition must pass for the transition to fire.
// ---------------------------------------------------------------------------
struct AnimatorCondition
{
    enum class Op
    {
        Greater, Less, GreaterEq, LessEq, // float comparisons against Threshold
        Equal, NotEqual,                  // float or bool equality
        IsTrue, IsFalse,                  // bool shorthand (Threshold ignored)
        Triggered,                        // one-shot; consumed after evaluation
    };

    std::string Name;
    Op          Cmp       = Op::IsTrue;
    f32         Threshold = 0.0f;
};

// ---------------------------------------------------------------------------
// AnimatorTransition
//
// Connects two states. Fires when all Conditions pass and (if HasExitTime)
// the active clip has progressed at least ExitTime (0–1 fraction of frames).
// ExitTime = 1.0 means "wait until the clip finishes" — useful for one-shot
// attack or death animations that should complete before returning to idle.
// ---------------------------------------------------------------------------
struct AnimatorTransition
{
    std::string                    TargetState;
    std::vector<AnimatorCondition> Conditions; // all must pass
    bool                           HasExitTime = false;
    f32                            ExitTime    = 1.0f; // 0–1 fraction; 1 = clip end
};

// ---------------------------------------------------------------------------
// AnimatorState
//
// One node in the state machine. Holds the name of an AnimationClip to play
// and the list of possible exits from this state.
// ---------------------------------------------------------------------------
struct AnimatorState
{
    std::string                     ClipName; // resolved via SpriteSheet at transition time
    std::vector<AnimatorTransition> Transitions;
};

// ---------------------------------------------------------------------------
// AnimatorStateMachine
//
// ECS component — add alongside Animator. AnimatorStateMachineSystem evaluates
// this component each tick, updating the paired Animator when a transition fires.
//
// Typical setup in OnStart:
//   sm.Sheet = &_playerSheet;
//   sm.AddState("idle", "idle_down");
//   sm.AddState("walk", "walk_down");
//   sm.AddTransition("idle", { .TargetState = "walk",
//       .Conditions = {{ .Name = "Moving", .Cmp = AnimatorCondition::Op::IsTrue }} });
//   sm.AddTransition("walk", { .TargetState = "idle",
//       .Conditions = {{ .Name = "Moving", .Cmp = AnimatorCondition::Op::IsFalse }} });
//   sm.SetDefaultState("idle");
//
// Each tick, game code sets parameters before systems run:
//   sm->SetBool("Moving", isMoving);
//   sm->SetFloat("Speed", speed);
//   sm->SetTrigger("Attack");
//
// The system reads these and switches the Animator clip automatically.
// ---------------------------------------------------------------------------
class AnimatorStateMachine
{
public:
    const SpriteSheet* Sheet = nullptr;

    // --- Setup (call before the entity enters the world) --------------------

    void AddState(std::string_view name, std::string_view clipName);
    void AddTransition(std::string_view fromState, AnimatorTransition transition);
    void SetDefaultState(std::string_view name);

    // --- Per-tick parameter setters -----------------------------------------

    void SetFloat(std::string_view name, f32 value);
    void SetBool(std::string_view name, bool value);
    // Triggers fire once then are automatically consumed after evaluation.
    void SetTrigger(std::string_view name);

    // --- Introspection ------------------------------------------------------

    [[nodiscard]] const std::string&   CurrentStateName() const { return _currentState; }
    [[nodiscard]] const AnimatorState* CurrentState() const;

    // --- Called by AnimatorStateMachineSystem -------------------------------

    // Evaluates all transitions from the current state. Returns the target
    // state name if one fired, or an empty string if nothing changed.
    // clipProgress: fraction of the active clip elapsed (0–1).
    [[nodiscard]] std::string EvaluateTransitions(f32 clipProgress);

    // Switches to a new state and clears consumed triggers.
    void TransitionTo(std::string_view stateName);

private:
    [[nodiscard]] bool EvaluateCondition(const AnimatorCondition& cond) const;
    void               ConsumeTriggers(const AnimatorTransition& trans);

    std::string _currentState;

    std::unordered_map<std::string, AnimatorState> _states;

    // Parameters are stored as floats regardless of semantic type.
    // Bool:    0.0 = false, any other value = true.
    // Trigger: 1.0 = pending, 0.0 = consumed.
    // A parameter that was never set reads as 0.0 — false / not triggered /
    // zero — so conditions on it evaluate normally instead of always failing.
    std::unordered_map<std::string, f32> _params;
};
} // namespace Arcbit
