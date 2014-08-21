#include "buffer.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

extern "C" {
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include "char_buffer.h"
#include "editor.h"
#include "file_link_mode.h"
#include "run_command_handler.h"
#include "lazy_string_append.h"
#include "substring.h"

namespace {

using namespace afc::editor;
using std::cerr;

void SaveDiff(EditorState* editor_state, OpenBuffer* buffer) {
  unique_ptr<OpenBuffer> original(new OpenBuffer("- original diff"));
  buffer->ReloadInto(editor_state, original.get());
  while (original->fd() != -1) {
    original->ReadData(editor_state);
  }

  char* path_old_diff = strdup("patch-old-diff-XXXXXX");
  int fd_old_diff = mkstemp(path_old_diff);
  char* path_new_diff = strdup("patch-new-diff-XXXXXX");
  int fd_new_diff = mkstemp(path_new_diff);

  SaveContentsToOpenFile(editor_state, original.get(), path_old_diff, fd_old_diff);
  SaveContentsToOpenFile(editor_state, buffer, path_new_diff, fd_new_diff);
  close(fd_old_diff);
  close(fd_new_diff);
  RunCommandHandler("./diff_writer.py " + string(path_old_diff) + " " + string(path_new_diff), editor_state);
  editor_state->SetStatus("Changing diff");
}

}  // namespace

namespace afc {
namespace editor {

using std::to_string;

OpenBuffer::OpenBuffer(const string& name)
    : name_(name),
      fd_(-1),
      fd_is_terminal_(false),
      buffer_(nullptr),
      buffer_line_start_(0),
      buffer_length_(0),
      buffer_size_(0),
      child_pid_(-1),
      child_exit_status_(0),
      view_start_line_(0),
      view_start_column_(0),
      current_position_line_(0),
      current_position_col_(0),
      modified_(false),
      reading_from_parser_(false),
      reload_after_exit_(false),
      bool_variables_(BoolStruct()->NewInstance()),
      string_variables_(StringStruct()->NewInstance()) {
}

void OpenBuffer::EndOfFile(EditorState* editor_state) {
  close(fd_);
  buffer_ = static_cast<char*>(realloc(buffer_, buffer_length_));
  if (child_pid_ != -1) {
    if (waitpid(child_pid_, &child_exit_status_, 0) == -1) {
      editor_state->SetStatus("waitpid failed: " + string(strerror(errno)));
      return;
    }
  }
  fd_ = -1;
  child_pid_ = -1;
  if (reload_after_exit_) {
    reload_after_exit_ = false;
    Reload(editor_state);
  }
  if (read_bool_variable(variable_close_after_clean_exit())
      && WIFEXITED(child_exit_status_)
      && WEXITSTATUS(child_exit_status_) == 0) {
    auto it = editor_state->buffers()->find(name_);
    if (it != editor_state->buffers()->end()) {
      editor_state->CloseBuffer(it);
    }
  }
}

void OpenBuffer::ReadData(EditorState* editor_state) {
  assert(fd_ > 0);
  assert(buffer_line_start_ <= buffer_length_);
  assert(buffer_length_ <= buffer_size_);
  if (buffer_length_ == buffer_size_) {
    buffer_size_ = buffer_size_ ? buffer_size_ * 2 : 64 * 1024;
    buffer_ = static_cast<char*>(realloc(buffer_, buffer_size_));
  }
  ssize_t characters_read = read(fd_, buffer_ + buffer_length_, buffer_size_ - buffer_length_);
  if (characters_read == -1) {
    if (errno == EAGAIN) {
      return;
    }
    return EndOfFile(editor_state);
  }
  assert(characters_read >= 0);
  if (characters_read == 0) {
    return EndOfFile(editor_state);
  }

  shared_ptr<LazyString> buffer_wrapper(
      NewMoveableCharBuffer(
          &buffer_, buffer_length_ + static_cast<size_t>(characters_read)));
  for (size_t i = buffer_length_;
       i < buffer_length_ + static_cast<size_t>(characters_read);
       i++) {
    if (buffer_[i] == '\n') {
      if (current_position_line_ == contents_.size()) {
        current_position_line_ ++;
      }
      AppendLine(Substring(buffer_wrapper, buffer_line_start_, i - buffer_line_start_));
      assert(current_position_line_ <= contents_.size());
      buffer_line_start_ = i + 1;
      if (editor_state->has_current_buffer()
          && editor_state->current_buffer()->second.get() == this
          && contents_.size() <= view_start_line_ + editor_state->visible_lines()) {
        editor_state->ScheduleRedraw();
      }
    }
  }
  buffer_length_ += static_cast<size_t>(characters_read);
}

void OpenBuffer::Reload(EditorState* editor_state) {
  if (child_pid_ != -1) {
    kill(-child_pid_, SIGTERM);
    reload_after_exit_ = true;
    return;
  }
  ReloadInto(editor_state, this);
  set_modified(false);
  CheckPosition();
}

void OpenBuffer::Save(EditorState* editor_state) {
  if (read_bool_variable(variable_diff())) {
    SaveDiff(editor_state, this);
    return;
  }
  editor_state->SetStatus("Buffer can't be saved.");
}

void OpenBuffer::AppendLazyString(shared_ptr<LazyString> input) {
  size_t size = input->size();
  size_t start = 0;
  for (size_t i = 0; i < size; i++) {
    if (input->get(i) == '\n') {
      AppendLine(Substring(input, start, i - start));
      start = i + 1;
    }
  }
}

static void AddToParseTree(const shared_ptr<LazyString>& str_input) {
  string str = str_input->ToString();
}

shared_ptr<Line> OpenBuffer::AppendLine(shared_ptr<LazyString> str) {
  if (reading_from_parser_) {
    switch (str->get(0)) {
      case 'E':
        return AppendRawLine(Substring(str, 1));

      case 'T':
        AddToParseTree(str);
        return nullptr;
    }
    return nullptr;
  }

  if (contents_.empty()) {
    if (str->ToString() == "EDGE PARSER v1.0") {
      reading_from_parser_ = true;
      return nullptr;
    }
  }

  return AppendRawLine(str);
}

shared_ptr<Line> OpenBuffer::AppendRawLine(shared_ptr<LazyString> str) {
  shared_ptr<Line> line(new Line);
  line->contents = str;
  contents_.push_back(line);
  return line;
}

void OpenBuffer::InsertInCurrentPosition(const vector<shared_ptr<Line>>& insertion) {
  auto begin = insertion.begin();
  auto end = insertion.end();
  if (begin == end) { return; }
  CheckPosition();
  MaybeAdjustPositionCol();
  if (!at_beginning_of_line()) {
    auto tail = current_line_tail();
    replace_current_line(shared_ptr<Line>(new Line(
        StringAppend(current_line_head(), (*begin)->contents))));
    begin++;
    current_position_col_ = 0;
    current_position_line_ ++;
    contents_.insert(contents_.begin() + current_position_line_,
        shared_ptr<Line>(new Line(tail)));
  }
  if (begin == end) { return; }
  contents_.insert(contents_.begin() + current_position_line_, begin, end);
  current_position_line_ += end - begin;
  current_position_col_ = 0;
}

void OpenBuffer::MaybeAdjustPositionCol() {
  if (contents_.empty()) { return; }
  size_t line_length = current_line()->contents->size();
  if (current_position_col_ > line_length) {
    current_position_col_ = line_length;
  }
}

void OpenBuffer::CheckPosition() {
  if (current_position_line_ >= contents_.size()) {
    current_position_line_ = contents_.size();
    if (current_position_line_ > 0) {
      current_position_line_--;
    }
  }
}

void OpenBuffer::SetInputFile(
    int input_fd, bool fd_is_terminal, pid_t child_pid) {
  contents_.clear();
  buffer_ = nullptr;
  buffer_line_start_ = 0;
  buffer_length_ = 0;
  buffer_size_ = 0;
  if (fd_ != -1) {
    close(fd_);
  }
  assert(child_pid_ == -1);
  fd_ = input_fd;
  fd_is_terminal_ = fd_is_terminal;
  child_pid_ = child_pid;
}

string OpenBuffer::FlagsString() const {
  string output;
  if (modified()) {
    output += "~";
  }
  if (fd() != -1) {
    output += "< l:" + to_string(contents_.size());
  }
  if (child_pid_ != -1) {
    output += " pid:" + to_string(child_pid_);
  } else if (child_exit_status_ != 0) {
    if (WIFEXITED(child_exit_status_)) {
      output += " exit:" + to_string(WEXITSTATUS(child_exit_status_));
    } else if (WIFSIGNALED(child_exit_status_)) {
      output += " signal:" + to_string(WTERMSIG(child_exit_status_));
    } else {
      output += " exit-status:" + to_string(child_exit_status_);
    }
  }
  return output;
}

/* static */ EdgeStruct<char>* OpenBuffer::BoolStruct() {
  static EdgeStruct<char>* output = nullptr;
  if (output == nullptr) {
    output = new EdgeStruct<char>;
    // Trigger registration of all fields.
    OpenBuffer::variable_pts();
    OpenBuffer::variable_close_after_clean_exit();
    OpenBuffer::variable_reload_on_enter();
    OpenBuffer::variable_atomic_lines();
    OpenBuffer::variable_diff();
  }
  return output;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_pts() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      "pts",
      "If a command is forked that writes to this buffer, should it be run "
      "with its own pseudoterminal?",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_close_after_clean_exit() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      "close_after_clean_exit",
      "If a command is forked that writes to this buffer, should the buffer be "
      "when the command exits with a successful status code?",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_reload_on_enter() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      "reload_on_enter",
      "Should this buffer be reloaded automatically when visited?",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_atomic_lines() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      "atomic_lines",
      "If true, lines can't be joined (e.g. you can't delete the last "
      "character in a line unless the line is empty).  This is used by certain "
      "buffers that represent lists of things (each represented as a line), "
      "for which this is a natural behavior.",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_diff() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      "",
      "Does this buffer represent a diff?  If true, when it gets saved the "
      "original contents are reloaded into a separate buffer, an attempt is "
      "made to revert them and then an attempt is made to apply the new "
      "contents.",
      false);
  return variable;
}

/* static */ EdgeStruct<string>* OpenBuffer::StringStruct() {
  static EdgeStruct<string>* output = nullptr;
  if (output == nullptr) {
    output = new EdgeStruct<string>;
    // Trigger registration of all fields.
    OpenBuffer::variable_word_characters();
    OpenBuffer::variable_path_characters();
  }
  return output;
}

/* static */ EdgeVariable<string>* OpenBuffer::variable_word_characters() {
  static EdgeVariable<string>* variable = StringStruct()->AddVariable(
      "word_characters",
      "String with all the characters that should be considered part of a "
      "word.",
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_");
  return variable;
}

/* static */ EdgeVariable<string>* OpenBuffer::variable_path_characters() {
  static EdgeVariable<string>* variable = StringStruct()->AddVariable(
      "path_characters",
      "String with all the characters that should be considered part of a "
      "path.",
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.*:");
  return variable;
}

bool OpenBuffer::read_bool_variable(const EdgeVariable<char>* variable) {
  return static_cast<bool>(bool_variables_.Get(variable));
}

void OpenBuffer::set_bool_variable(
    const EdgeVariable<char>* variable, bool value) {
  bool_variables_.Set(variable, static_cast<char>(value));
}

void OpenBuffer::toggle_bool_variable(const EdgeVariable<char>* variable) {
  set_bool_variable(variable, !read_bool_variable(variable));
}

const string& OpenBuffer::read_string_variable(const EdgeVariable<string>* variable) {
  return string_variables_.Get(variable);
}

void OpenBuffer::set_string_variable(
    const EdgeVariable<string>* variable, const string& value) {
  string_variables_.Set(variable, value);
}

void OpenBuffer::CopyVariablesFrom(const shared_ptr<const OpenBuffer>& src) {
  assert(src.get() != nullptr);
  bool_variables_.CopyFrom(src->bool_variables_);
  string_variables_.CopyFrom(src->string_variables_);
  reload_after_exit_ = src->reload_after_exit_;
}

}  // namespace editor
}  // namespace afc
