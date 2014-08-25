#include <memory>
#include <limits>
#include <string>

#include "char_buffer.h"
#include "command.h"
#include "command_mode.h"
#include "file_link_mode.h"
#include "line_prompt_mode.h"
#include "editable_string.h"
#include "editor.h"
#include "terminal.h"

namespace {
using namespace afc::editor;
using std::make_pair;
using std::numeric_limits;

const string kHistoryName = "- prompt history";
const string kHistoryPath = "/.edge/prompt_history";

class LinePromptMode : public EditorMode {
 public:
  LinePromptMode(const string& prompt, const string& initial_value,
                 LinePromptHandler handler)
      : prompt_(prompt),
        handler_(handler),
        input_(EditableString::New(initial_value)) {}

  static shared_ptr<OpenBuffer> FindHistoryBuffer(EditorState* editor_state) {
    auto result = editor_state->buffers()->find(kHistoryName);
    if (result == editor_state->buffers()->end()) {
      return shared_ptr<OpenBuffer>(nullptr);
    }
    return result->second;
  }

  void ProcessInput(int c, EditorState* editor_state) {
    switch (c) {
      case '\n':
        InsertToHistory(editor_state);
        editor_state->set_status_prompt(false);
        editor_state->ResetStatus();
        handler_(input_->ToString(), editor_state);
        return;

      case Terminal::ESCAPE:
        editor_state->set_status_prompt(false);
        editor_state->ResetStatus();
        handler_("", editor_state);
        return;

      case Terminal::BACKSPACE:
        input_->Backspace();
        break;

      case Terminal::CTRL_U:
        input_->Clear();
        break;

      case Terminal::UP_ARROW:
        {
          auto buffer = FindHistoryBuffer(editor_state);
          if (buffer == nullptr || buffer->contents()->size() == 1) { return; }
          OpenBuffer::Position position = buffer->position();
          if (position.line > 0) {
            position.line --;
            buffer->set_position(position);
          }
          SetInputFromCurrentLine(buffer);
        }
        break;

      case Terminal::DOWN_ARROW:
        {
          auto buffer = FindHistoryBuffer(editor_state);
          if (buffer == nullptr || buffer->contents()->size() == 1) { return; }
          OpenBuffer::Position position = buffer->position();
          if (position.line + 1 < buffer->contents()->size()) {
            position.line ++;
            buffer->set_position(position);
          }
          SetInputFromCurrentLine(buffer);
        }
        break;

      default:
        input_->Insert(static_cast<char>(c));
    }
    UpdateStatus(editor_state);
  }

  void UpdateStatus(EditorState* editor_state) {
    editor_state->SetStatus(prompt_ + input_->ToString());
  }

 private:
  void SetInputFromCurrentLine(const shared_ptr<OpenBuffer>& buffer) {
    if (buffer == nullptr) {
      input_ = EditableString::New("");
      return;
    }
    input_ = EditableString::New(buffer->current_line()->contents->ToString());
  }

  void InsertToHistory(EditorState* editor_state) {
    if (input_->size() == 0) { return; }
    auto it = editor_state->buffers()->find(kHistoryName);
    if (it == editor_state->buffers()->end()) {
      it = OpenFile(
          editor_state, kHistoryName,
          editor_state->home_directory() + kHistoryPath);
      it->second->set_bool_variable(
          OpenBuffer::variable_save_on_close(), true);
      if (!editor_state->has_current_buffer()) {
        // Seems lame, but what can we do?
        editor_state->set_current_buffer(it);
        editor_state->ScheduleRedraw();
      }
    }
    assert(it != editor_state->buffers()->end());
    assert(it->second != nullptr);
    it->second->AppendLine(input_);
  }

  const string prompt_;
  LinePromptHandler handler_;
  shared_ptr<EditableString> input_;
};

class LinePromptCommand : public Command {
 public:
  LinePromptCommand(const string& prompt,
                    const string& description,
                    LinePromptHandler handler)
      : prompt_(prompt), description_(description), handler_(handler) {}

  const string Description() {
    return description_;
  }

  void ProcessInput(int c, EditorState* editor_state) {
    Prompt(editor_state, prompt_, "", handler_);
  }

 private:
  const string prompt_;
  const string description_;
  LinePromptHandler handler_;
};

}  // namespace

namespace afc {
namespace editor {

using std::unique_ptr;
using std::shared_ptr;

void Prompt(EditorState* editor_state,
            const string& prompt,
            const string& initial_value,
            LinePromptHandler handler) {
  std::unique_ptr<LinePromptMode> line_prompt_mode(
      new LinePromptMode(prompt, initial_value, handler));
  auto history = editor_state->buffers()->find(kHistoryName);
  if (history != editor_state->buffers()->end()) {
    assert(!history->second->contents()->empty());
    history->second->set_current_position_line(
        history->second->contents()->size() - 1);
  }
  line_prompt_mode->UpdateStatus(editor_state);
  editor_state->set_mode(std::move(line_prompt_mode));
  editor_state->set_status_prompt(true);
}

unique_ptr<Command> NewLinePromptCommand(
    const string& prompt,
    const string& description,
    LinePromptHandler handler) {
  return std::move(unique_ptr<Command>(
      new LinePromptCommand(prompt, description, handler)));
}

}  // namespace afc
}  // namespace editor
