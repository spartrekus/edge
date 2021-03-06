#include "help_command.h"

#include <map>
#include <memory>

#include <glog/logging.h>

#include "buffer_variables.h"
#include "char_buffer.h"
#include "editor.h"
#include "lazy_string_append.h"

namespace afc {
namespace editor {

using std::map;
using std::shared_ptr;
using std::unique_ptr;

namespace {
wstring DescribeSequence(wstring input) {
  wstring output;
  for (wint_t c : input) {
    if (c == '\n') {
      output.push_back(L'↩');
    } else {
      output.push_back(static_cast<wchar_t>(c));
    }
  }
  return output;
}

class HelpCommand : public Command {
 public:
  HelpCommand(const MapModeCommands* commands, const wstring& mode_description)
      : commands_(commands), mode_description_(mode_description) {}

  const wstring Description() { return L"shows help about commands."; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    const wstring name = L"- help: " + mode_description_;
    auto it = editor_state->buffers()->insert(make_pair(name, nullptr));
    editor_state->set_current_buffer(it.first);
    if (it.second) {
      auto buffer = std::make_shared<OpenBuffer>(editor_state, name);
      buffer->AppendToLastLine(editor_state,
                               NewCopyString(L"Help: " + mode_description_));
      std::map<wstring, Command*> descriptions = commands_->Coallesce();
      for (const auto& it : descriptions) {
        buffer->AppendLine(editor_state,
                           NewCopyString(DescribeSequence(it.first) + L" - " +
                                         it.second->Description()));
      }

      DescribeVariables(
          editor_state, L"bool", buffer.get(), buffer_variables::BoolStruct(),
          [](const bool& value) { return value ? L"true" : L"false"; });
      DescribeVariables(editor_state, L"string", buffer.get(),
                        buffer_variables::StringStruct(),
                        [](const std::wstring& value) { return value; });
      DescribeVariables(
          editor_state, L"int", buffer.get(), buffer_variables::IntStruct(),
          [](const int& value) { return std::to_wstring(value); });

      it.first->second = buffer;
    }
    it.first->second->set_current_position_line(0);
    it.first->second->ResetMode();

    editor_state->ScheduleRedraw();
    editor_state->ResetRepetitions();
  }

 private:
  template <typename T, typename C>
  void DescribeVariables(EditorState* editor_state, wstring type_name,
                         OpenBuffer* buffer, EdgeStruct<T>* variables,
                         /*std::function<std::wstring(const T&)>*/ C print) {
    buffer->AppendEmptyLine(editor_state);
    buffer->AppendLine(editor_state,
                       NewCopyString(L"Variables (" + type_name + L"):"));
    for (const auto& variable : variables->variables()) {
      buffer->AppendLine(editor_state, NewCopyString(variable.second->name()));
      buffer->AppendLine(
          editor_state,
          StringAppend(NewCopyString(L"    "),
                       NewCopyString(variable.second->description())));
      buffer->AppendLine(
          editor_state,
          StringAppend(NewCopyString(L"    Default: "),
                       NewCopyString(print(variable.second->default_value()))));
    }
  }

  const MapModeCommands* const commands_;
  const wstring mode_description_;
};
}  // namespace

unique_ptr<Command> NewHelpCommand(const MapModeCommands* commands,
                                   const wstring& mode_description) {
  return std::make_unique<HelpCommand>(commands, mode_description);
}

}  // namespace editor
}  // namespace afc
