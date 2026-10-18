// TextInput editing with UTF-8 text: the cursor, deletion and limits move
// by whole characters.

#include <arcbit/ui/InputWidgets.h>
#include <arcbit/ui/UIScreen.h>

#include <catch2/catch_test_macros.hpp>

using namespace Arcbit;

namespace
{
constexpr const char* EAcute = "\xC3\xA9"; // escaped: the source stays ASCII
constexpr const char* Euro   = "\xE2\x82\xAC";

struct Fixture
{
    UIScreen   screen;
    TextInput* input = screen.Add<TextInput>();

    Fixture() { screen.FocusAt(0); }
    std::string Text() const { return input->Text; }
};
} // namespace

TEST_CASE("Typing UTF-8 inserts whole characters at the cursor", "[ui][textinput]")
{
    Fixture f;
    f.input->OnTextInput(std::string("a") + EAcute + Euro);
    CHECK(f.Text() == std::string("a") + EAcute + Euro);

    f.input->OnControlKey(UIControlKey::Left); // before the euro sign, not inside it
    f.input->OnTextInput("b");
    CHECK(f.Text() == std::string("a") + EAcute + "b" + Euro);
}

TEST_CASE("Backspace and Delete remove whole characters", "[ui][textinput]")
{
    Fixture f;
    f.input->OnTextInput(std::string(EAcute) + Euro + EAcute);
    f.input->OnControlKey(UIControlKey::Backspace);
    CHECK(f.Text() == std::string(EAcute) + Euro);

    f.input->OnControlKey(UIControlKey::Home);
    f.input->OnControlKey(UIControlKey::Delete);
    CHECK(f.Text() == Euro);
}

TEST_CASE("MaxLength counts characters; Numeric rejects letters beyond ASCII", "[ui][textinput]")
{
    Fixture f;
    f.input->MaxLength = 2;
    f.input->OnTextInput(std::string(Euro) + Euro + Euro);
    CHECK(f.Text() == std::string(Euro) + Euro);

    Fixture n;
    n.input->InputMode = TextInput::Mode::Numeric;
    n.input->OnTextInput(std::string("1") + EAcute + "2");
    CHECK(n.Text() == "12");
}

TEST_CASE("Text changed from outside leaves the cursor on a character", "[ui][textinput]")
{
    Fixture f;
    f.input->OnTextInput("abc");
    f.input->Text = std::string("a") + Euro; // cursor at byte 3, inside the euro sign
    f.input->OnTextInput("x");
    CHECK(f.Text() == std::string("ax") + Euro);
}
