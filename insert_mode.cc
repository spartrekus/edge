#include "insert_mode.h"

#include <cassert>
#include <memory>

extern "C" {
#include <unistd.h>
}

#include "command_mode.h"
#include "editable_string.h"
#include "editor.h"
#include "file_link_mode.h"
#include "lazy_string_append.h"
#include "substring.h"
#include "terminal.h"

namespace {
using namespace afc::editor;

class InsertMode : public EditorMode {
 public:
  InsertMode(shared_ptr<EditableString>& line) : line_(line) {}

  void ProcessInput(int c, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer()->second;
    buffer->MaybeAdjustPositionCol();
    switch (c) {
      case Terminal::ESCAPE:
        editor_state->ResetStatus();
        editor_state->ResetMode();
        editor_state->ResetRepetitions();
        return;
      case Terminal::BACKSPACE:
        if (line_->Backspace()) {
          buffer->set_modified(true);
          editor_state->ScheduleRedraw();
          buffer->set_current_position_col(buffer->current_position_col() - 1);
        } else if (buffer->at_beginning_of_line()) {
          // Join lines.
          if (buffer->at_beginning()) { return; }
          auto current_line =
              buffer->contents()->begin() + buffer->current_position_line();
          auto previous_line = current_line - 1;
          if ((*previous_line)->size() == 0) {
            if ((*previous_line)->activate.get() != nullptr) {
              (*previous_line)->activate->ProcessInput('d', editor_state);
            }
            buffer->contents()->erase(previous_line);
          } else {
            if (buffer->atomic_lines()
                && (*current_line)->contents->size() > 0) { return; }
            line_ = EditableString::New(StringAppend((*previous_line)->contents,
                                                     (*current_line)->contents),
                                        (*previous_line)->contents->size());
            buffer->set_current_position_col(
                (*previous_line)->contents->size());
            (*previous_line).reset(new Line(line_));
            buffer->contents()->erase(current_line);
          }
          buffer->set_modified(true);
          buffer->set_current_position_line(buffer->current_position_line() - 1);
          editor_state->ScheduleRedraw();
        } else {
          auto prefix = Substring(
              buffer->current_line()->contents, 0,
              min(buffer->current_position_col(),
                  buffer->current_line()->contents->size()));
          line_ = EditableString::New(
              Substring(buffer->current_line()->contents,
                        buffer->current_position_col()),
              0,
              prefix->ToString());
          buffer->current_line()->contents = line_;
          assert(line_->Backspace());
          buffer->set_modified(true);
          editor_state->ScheduleRedraw();
          buffer->set_current_position_col(buffer->current_position_col() - 1);
        }
        return;
      case '\n':
        size_t pos = buffer->current_position_col();
        if (buffer->atomic_lines()
            && pos != 0
            && pos != buffer->current_line()->contents->size()) {
          return;
        }

        // Adjust the old line.
        buffer->current_line()->contents =
            Substring(buffer->current_line()->contents, 0, pos);

        // Create a new line and insert it.
        line_ = EditableString::New(Substring(line_, pos), 0);

        shared_ptr<Line> line(new Line());
        line->contents = line_;
        buffer->contents()->insert(
            buffer->contents()->begin() + buffer->current_position_line() + 1,
            line);
        buffer->set_modified(true);

        // Move to the new line and schedule a redraw.
        buffer->set_current_position_line(buffer->current_position_line() + 1);
        buffer->set_current_position_col(0);
        editor_state->ScheduleRedraw();
        return;
    }
    line_->Insert(c);
    buffer->set_modified(true);
    editor_state->ScheduleRedraw();
    buffer->set_current_position_col(buffer->current_position_col() + 1);
  }

 private:
  shared_ptr<EditableString> line_;
};

class RawInputTypeMode : public EditorMode {
  void ProcessInput(int c, EditorState* editor_state) {
    switch (c) {
      case Terminal::ESCAPE:
        editor_state->ResetMode();
        editor_state->ResetStatus();
        break;
      default:
        string str(1, static_cast<char>(c));
        write(editor_state->current_buffer()->second->fd(), str.c_str(), 1);
    };
  }
};

}  // namespace

namespace afc {
namespace editor {

using std::unique_ptr;
using std::shared_ptr;

void EnterInsertCharactersMode(EditorState* editor_state) {
  auto buffer = editor_state->current_buffer()->second;
  shared_ptr<EditableString> new_line;
  editor_state->PushCurrentPosition();
  if (buffer->contents()->empty()) {
    new_line = EditableString::New("");
    buffer->AppendLine(new_line);
  } else {
    buffer->MaybeAdjustPositionCol();
    new_line = EditableString::New(
        buffer->current_line()->contents, buffer->current_position_col());
    buffer->contents()->at(buffer->current_position_line()).reset(new Line(new_line));
  }
  editor_state->SetStatus("type");
  editor_state->set_mode(unique_ptr<EditorMode>(new InsertMode(new_line)));
}

void EnterInsertMode(EditorState* editor_state) {
  editor_state->ResetStatus();

  if (!editor_state->has_current_buffer()) {
    OpenAnonymousBuffer(editor_state);
  }
  if (editor_state->current_buffer()->second->fd() != -1) {
    editor_state->SetStatus("type (raw)");
    editor_state->set_mode(unique_ptr<EditorMode>(new RawInputTypeMode()));
  } else if (editor_state->structure() == EditorState::CHAR) {
    editor_state->current_buffer()->second->CheckPosition();
    EnterInsertCharactersMode(editor_state);
  } else if (editor_state->structure() == EditorState::LINE) {
    editor_state->current_buffer()->second->CheckPosition();
    auto buffer = editor_state->current_buffer()->second;
    shared_ptr<Line> line(new Line());
    line->contents = EmptyString();
    if (editor_state->direction() == BACKWARDS) {
      buffer->set_current_position_line(buffer->current_position_line() + 1);
    }
    buffer->contents()->insert(
        buffer->contents()->begin() + buffer->current_position_line(),
        line);
    EnterInsertCharactersMode(editor_state);
    editor_state->ScheduleRedraw();
  }
  editor_state->ResetDirection();
  editor_state->ResetStructure();
}

}  // namespace afc
}  // namespace editor
