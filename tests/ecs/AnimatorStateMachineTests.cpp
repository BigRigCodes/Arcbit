#include <arcbit/ecs/AnimatorStateMachine.h>

#include <catch2/catch_test_macros.hpp>

using namespace Arcbit;
using Op = AnimatorCondition::Op;

namespace
{
// idle ⇄ walk on a float "speed", walk → attack on a trigger.
AnimatorStateMachine MakeLocomotion()
{
    AnimatorStateMachine sm;
    sm.AddState("idle", "idle_down");
    sm.AddState("walk", "walk_down");
    sm.AddState("attack", "attack_down");
    sm.AddTransition("idle", { .TargetState = "walk", .Conditions = { { "speed", Op::Greater, 0.1f } } });
    sm.AddTransition("walk", { .TargetState = "attack", .Conditions = { { "attack", Op::Triggered } } });
    sm.AddTransition("walk", { .TargetState = "idle", .Conditions = { { "speed", Op::LessEq, 0.1f } } });
    sm.SetDefaultState("idle");
    return sm;
}
} // namespace

TEST_CASE("Default state and clip lookup", "[ecs][animator]")
{
    AnimatorStateMachine sm = MakeLocomotion();
    CHECK(sm.CurrentStateName() == "idle");
    REQUIRE(sm.CurrentState() != nullptr);
    CHECK(sm.CurrentState()->ClipName == "idle_down");

    sm.TransitionTo("nowhere");
    CHECK(sm.CurrentState() == nullptr);
    CHECK(sm.EvaluateTransitions(1.0f).empty());
}

TEST_CASE("Float conditions drive transitions; evaluation doesn't change state", "[ecs][animator]")
{
    AnimatorStateMachine sm = MakeLocomotion();
    sm.SetFloat("speed", 0.0f);
    CHECK(sm.EvaluateTransitions(0.0f).empty());

    sm.SetFloat("speed", 2.0f);
    CHECK(sm.EvaluateTransitions(0.0f) == "walk");
    CHECK(sm.CurrentStateName() == "idle"); // the caller (AnimatorStateMachineSystem) commits it

    sm.TransitionTo("walk");
    sm.SetFloat("speed", 0.05f);
    CHECK(sm.EvaluateTransitions(0.0f) == "idle");
}

TEST_CASE("Triggers fire once and are consumed", "[ecs][animator]")
{
    AnimatorStateMachine sm = MakeLocomotion();
    sm.TransitionTo("walk");
    sm.SetFloat("speed", 2.0f);

    sm.SetTrigger("attack");
    CHECK(sm.EvaluateTransitions(0.0f) == "attack");
    CHECK(sm.EvaluateTransitions(0.0f).empty()); // consumed; speed still blocks walk → idle
}

TEST_CASE("Transitions are tried in order; the first passing one wins", "[ecs][animator]")
{
    AnimatorStateMachine sm = MakeLocomotion();
    sm.TransitionTo("walk");
    sm.SetFloat("speed", 0.0f);  // walk → idle would pass
    sm.SetTrigger("attack");     // ...but walk → attack is listed first
    CHECK(sm.EvaluateTransitions(0.0f) == "attack");
}

TEST_CASE("Exit time gates a transition until the clip has progressed far enough", "[ecs][animator]")
{
    AnimatorStateMachine sm;
    sm.AddState("attack", "attack_down");
    sm.AddState("idle", "idle_down");
    sm.AddTransition("attack", { .TargetState = "idle", .HasExitTime = true, .ExitTime = 0.9f });
    sm.SetDefaultState("attack");

    CHECK(sm.EvaluateTransitions(0.5f).empty());
    CHECK(sm.EvaluateTransitions(0.9f) == "idle"); // no conditions: exit time alone decides
}

TEST_CASE("Every comparison operator", "[ecs][animator]")
{
    struct Case
    {
        Op   Cmp;
        f32  Threshold;
        f32  Value;
        bool Passes;
    };
    const Case cases[] = {
        { Op::Greater, 1.0f, 1.5f, true },   { Op::Greater, 1.0f, 1.0f, false },
        { Op::Less, 1.0f, 0.5f, true },      { Op::Less, 1.0f, 1.0f, false },
        { Op::GreaterEq, 1.0f, 1.0f, true }, { Op::LessEq, 1.0f, 1.0f, true },
        { Op::Equal, 2.0f, 2.0f, true },     { Op::NotEqual, 2.0f, 2.0f, false },
        { Op::IsTrue, 0.0f, 1.0f, true },    { Op::IsFalse, 0.0f, 0.0f, true },
    };
    for (const Case& c : cases) {
        AnimatorStateMachine sm;
        sm.AddState("a", "clip_a");
        sm.AddState("b", "clip_b");
        sm.AddTransition("a", { .TargetState = "b", .Conditions = { { "p", c.Cmp, c.Threshold } } });
        sm.SetDefaultState("a");
        sm.SetFloat("p", c.Value);
        INFO("op " << static_cast<int>(c.Cmp) << " threshold " << c.Threshold << " value " << c.Value);
        CHECK(sm.EvaluateTransitions(0.0f).empty() != c.Passes);
    }
}

TEST_CASE("Bools are stored as 0 / 1", "[ecs][animator]")
{
    AnimatorStateMachine sm;
    sm.AddState("a", "clip_a");
    sm.AddState("b", "clip_b");
    sm.AddTransition("a", { .TargetState = "b", .Conditions = { { "grounded", Op::IsFalse } } });
    sm.SetDefaultState("a");

    sm.SetBool("grounded", true);
    CHECK(sm.EvaluateTransitions(0.0f).empty());
    sm.SetBool("grounded", false);
    CHECK(sm.EvaluateTransitions(0.0f) == "b");
}

TEST_CASE("Parameters that were never set read as zero / false", "[ecs][animator]")
{
    struct Case
    {
        Op   Cmp;
        f32  Threshold;
        bool Passes;
    };
    const Case cases[] = {
        { Op::IsFalse, 0.0f, true },  // an unset bool is false
        { Op::IsTrue, 0.0f, false },
        { Op::Triggered, 0.0f, false },
        { Op::Less, 0.1f, true },     // an unset float is 0
        { Op::Equal, 0.0f, true },
        { Op::Greater, 0.0f, false },
    };
    for (const Case& c : cases) {
        AnimatorStateMachine sm;
        sm.AddState("a", "clip_a");
        sm.AddState("b", "clip_b");
        sm.AddTransition("a", { .TargetState = "b", .Conditions = { { "unset", c.Cmp, c.Threshold } } });
        sm.SetDefaultState("a");
        INFO("op " << static_cast<int>(c.Cmp) << " threshold " << c.Threshold);
        CHECK(sm.EvaluateTransitions(0.0f).empty() != c.Passes);
    }
}
