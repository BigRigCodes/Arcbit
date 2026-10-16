#include <arcbit/ecs/AnimatorStateMachine.h>

#include <algorithm>

namespace Arcbit
{

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void AnimatorStateMachine::AddState(const std::string_view name, const std::string_view clipName)
{
    _states[std::string(name)].ClipName = std::string(clipName);
}

void AnimatorStateMachine::AddTransition(const std::string_view fromState, AnimatorTransition transition)
{
    _states[std::string(fromState)].Transitions.push_back(std::move(transition));
}

void AnimatorStateMachine::SetDefaultState(const std::string_view name)
{
    _currentState = std::string(name);
}

// ---------------------------------------------------------------------------
// Parameter setters
// ---------------------------------------------------------------------------

void AnimatorStateMachine::SetFloat(const std::string_view name, const f32 value)
{
    _params[std::string(name)] = value;
}

void AnimatorStateMachine::SetBool(const std::string_view name, const bool value)
{
    _params[std::string(name)] = value ? 1.0f : 0.0f;
}

void AnimatorStateMachine::SetTrigger(const std::string_view name)
{
    _params[std::string(name)] = 1.0f;
}

// ---------------------------------------------------------------------------
// Introspection
// ---------------------------------------------------------------------------

const AnimatorState* AnimatorStateMachine::CurrentState() const
{
    const auto it = _states.find(_currentState);
    return it != _states.end() ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Transition evaluation
// ---------------------------------------------------------------------------

std::string AnimatorStateMachine::EvaluateTransitions(const f32 clipProgress)
{
    const auto stateIt = _states.find(_currentState);
    if (stateIt == _states.end())
        return {};

    for (const auto& trans : stateIt->second.Transitions)
    {
        if (trans.HasExitTime && clipProgress < trans.ExitTime)
            continue;

        const bool allPass = std::all_of(trans.Conditions.begin(), trans.Conditions.end(),
                                         [this](const AnimatorCondition& c) { return EvaluateCondition(c); });
        if (allPass)
        {
            ConsumeTriggers(trans);
            return trans.TargetState;
        }
    }

    return {};
}

void AnimatorStateMachine::TransitionTo(const std::string_view stateName)
{
    _currentState = std::string(stateName);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool AnimatorStateMachine::EvaluateCondition(const AnimatorCondition& cond) const
{
    // Unset parameters read as 0 (false / not triggered); see _params.
    const auto it = _params.find(cond.Name);
    const f32  v  = it != _params.end() ? it->second : 0.0f;
    using Op = AnimatorCondition::Op;
    switch (cond.Cmp)
    {
        case Op::Greater:   return v >  cond.Threshold;
        case Op::Less:      return v <  cond.Threshold;
        case Op::GreaterEq: return v >= cond.Threshold;
        case Op::LessEq:    return v <= cond.Threshold;
        case Op::Equal:     return v == cond.Threshold;
        case Op::NotEqual:  return v != cond.Threshold;
        case Op::IsTrue:    return v != 0.0f;
        case Op::IsFalse:   return v == 0.0f;
        case Op::Triggered: return v != 0.0f;
    }
    return false;
}

void AnimatorStateMachine::ConsumeTriggers(const AnimatorTransition& trans)
{
    for (const auto& cond : trans.Conditions)
        if (cond.Cmp == AnimatorCondition::Op::Triggered)
            if (const auto it = _params.find(cond.Name); it != _params.end())
                it->second = 0.0f;
}

} // namespace Arcbit
