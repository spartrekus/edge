#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <algorithm>
#include <stdexcept>

extern "C" {
#include <dirent.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
}

#include "char_buffer.h"
#include "file_link_mode.h"
#include "editor.h"
#include "line_prompt_mode.h"
#include "run_command_handler.h"
#include "search_handler.h"

namespace {
using std::make_pair;
using std::shared_ptr;
using std::to_string;
using std::unique_ptr;
using std::sort;
using namespace afc::editor;

class FileBuffer : public OpenBuffer {
 public:
  FileBuffer(const string& path) : OpenBuffer(path) {
    set_string_variable(variable_path(), path);
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    const string& path = read_string_variable(variable_path());
    if (stat(path.c_str(), &stat_buffer_) == -1) {
      return;
    }

    target->ClearContents();
    editor_state->ScheduleRedraw();

    if (!S_ISDIR(stat_buffer_.st_mode)) {
      char* tmp = strdup(path.c_str());
      if (0 == strcmp(basename(tmp), "passwd")) {
        RunCommandHandler("parsers/passwd <" + path, editor_state);
      } else {
        LoadMemoryMappedFile(path, target);
      }
      editor_state->CheckPosition();
      editor_state->PushCurrentPosition();
      return;
    }

    set_bool_variable(variable_atomic_lines(), true);
    target->AppendLine(shared_ptr<LazyString>(
        NewCopyString("File listing: " + path).release()));

    DIR* dir = opendir(path.c_str());
    assert(dir != nullptr);
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      target->AppendLine(shared_ptr<LazyString>(
          NewCopyCharBuffer(entry->d_name).release()));
    }
    closedir(dir);

    struct Compare {
      bool operator()(const shared_ptr<Line>& a, const shared_ptr<Line>& b) {
        return *a->contents < *b->contents;
      }
    } compare;

    sort(target->contents()->begin() + 1, target->contents()->end() - 1, compare);
    editor_state->CheckPosition();
    editor_state->PushCurrentPosition();
  }

  void Save(EditorState* editor_state) {
    if (S_ISDIR(stat_buffer_.st_mode)) {
      OpenBuffer::Save(editor_state);
      return;
    }

    const string& path = read_string_variable(variable_path());
    if (SaveContentsToFile(editor_state, this, path)) {
      set_modified(false);
      editor_state->SetStatus("Saved: " + path);
    }
  }

 private:
  struct stat stat_buffer_;
};

static string realpath_safe(const string& path) {
  char* result = realpath(path.c_str(), nullptr);
  return result == nullptr ? path : string(result);
}

string GetAnonymousBufferName(size_t i) {
  return "[anonymous buffer " + to_string(i) + "]";
}

class FileLinkMode : public EditorMode {
 public:
  FileLinkMode(const string& path, size_t line, size_t col,
               const string& pattern)
      : path_(realpath_safe(path)),
        line_(line),
        col_(col),
        pattern_(pattern) {
    assert(path_ != "");
  }

  void ProcessInput(int c, EditorState* editor_state) {
    switch (c) {
      case '\n':
        {
          auto it = afc::editor::OpenFile(editor_state, path_, path_);
          if (it == editor_state->buffers()->end()) { return; }
          editor_state->set_current_buffer(it);
          it->second->set_position(OpenBuffer::Position(line_, col_));
          SearchHandler(pattern_, editor_state);
          return;
        }

      case 'd':
        {
          string path = path_;  // Capture for the lambda.
          unique_ptr<Command> command(NewLinePromptCommand(
              "rm " + path_ + "? ",
              "Confirmation",
              [path](const string input, EditorState* editor_state) {
                if (input == "yes") {
                  unlink(path.c_str());
                  editor_state->SetStatus("removed");
                } else {
                  // TODO: insert it again?  Actually, only let it be erased
                  // in the other case.
                }
                editor_state->ResetMode();
              }));
          command->ProcessInput('\n', editor_state);
        }
        return;

      default:
        editor_state->SetStatus("Invalid command: " + string(1, static_cast<char>(c)));
    }
  }

  string path_;
  size_t line_;
  size_t col_;
  const string pattern_;
};

static string FindPath(
    const string& path, vector<int>* positions, string* pattern) {
  struct stat dummy;

  // Strip off any trailing colons.
  for (size_t str_end = path.size();
       str_end != path.npos && str_end != 0;
       str_end = path.find_last_of(':', str_end - 1)) {
    const string path_without_suffix = path.substr(0, str_end);
    assert(!path_without_suffix.empty());
    if (stat(path_without_suffix.c_str(), &dummy) == -1) {
      continue;
    }

    for (size_t i = 0; i < positions->size(); i++) {
      while (str_end < path.size() && ':' == path[str_end]) {
        str_end++;
      }
      if (str_end == path.size()) { break; }
      size_t next_str_end = path.find(':', str_end);
      const string arg = path.substr(str_end, next_str_end);
      if (i == 0 && arg.size() > 0 && arg[0] == '/') {
        *pattern = arg.substr(1);
        break;
      } else {
        try {
          positions->at(i) = stoi(arg);
          if (positions->at(i) > 0) { positions->at(i) --; }
        } catch (const std::invalid_argument& ia) {
          break;
        }
      }
      str_end = next_str_end;
      if (str_end == path.npos) { break; }
    }
    return path_without_suffix;
  }
  return "";
}

}  // namespace

namespace afc {
namespace editor {

using std::unique_ptr;

bool SaveContentsToFile(
    EditorState* editor_state, OpenBuffer* buffer, const string& path) {
  assert(buffer != nullptr);
  string tmp_path = path + ".tmp";
  int fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  if (fd == -1) {
    editor_state->SetStatus(tmp_path + ": open failed: " + strerror(errno));
    return false;
  }
  bool result = SaveContentsToOpenFile(editor_state, buffer, tmp_path, fd);
  close(fd);
  if (!result) {
    return false;
  }

  if (rename(tmp_path.c_str(), path.c_str()) == -1) {
    editor_state->SetStatus(path + ": rename failed: " + strerror(errno));
    return false;
  }

  return true;
}

bool SaveContentsToOpenFile(
    EditorState* editor_state, OpenBuffer* buffer, const string& path,
    int fd) {
  // TODO: It'd be significant more efficient to do fewer (bigger) writes.
  for (auto it = buffer->contents()->begin(); it != buffer->contents()->end();
       ++it) {
    const auto& str = (*it)->contents;
    char* tmp = static_cast<char*>(malloc(str->size() + 1));
    strcpy(tmp, str->ToString().c_str());
    tmp[str->size()] = '\n';
    bool include_newline = it + 1 != buffer->contents()->end();
    int write_result = write(fd, tmp, str->size() + (include_newline ? 1 : 0));
    free(tmp);
    if (write_result == -1) {
      editor_state->SetStatus(
          path + ": write failed: " + to_string(fd) + ": " + strerror(errno));
      return false;
    }
  }
  return true;
}

map<string, shared_ptr<OpenBuffer>>::iterator OpenFile(
    EditorState* editor_state, const string& name_input, const string& path) {
  editor_state->PushCurrentPosition();
  shared_ptr<OpenBuffer> buffer;
  string name;
  if (name_input.empty()) {
    size_t count = 0;
    while (editor_state->buffers()->find(GetAnonymousBufferName(count))
           != editor_state->buffers()->end()) {
      count++;
    }
    name = GetAnonymousBufferName(count);
    buffer.reset(new OpenBuffer(name));
  } else {
    name = name_input;
  }
  auto it = editor_state->buffers()->insert(make_pair(name, buffer));
  if (it.second) {
    if (it.first->second.get() == nullptr) {
      it.first->second.reset(new FileBuffer(path));
    }
    it.first->second->Reload(editor_state);
  }
  it.first->second->set_current_position_line(0);
  it.first->second->set_current_position_col(0);
  return it.first;
}

void OpenAnonymousBuffer(EditorState* editor_state) {
  editor_state->set_current_buffer(
      OpenFile(editor_state, "", ""));
}

unique_ptr<EditorMode> NewFileLinkMode(
    EditorState* editor_state, const string& path, int position,
    bool ignore_if_not_found) {
  vector<int> tokens { position, 0 };
  string pattern;
  // TODO: Also support ~user/foo.
  string path_after_home_dir =
      path != "~" && (path.size() < 2 || path.substr(0, 2) != "~/")
      ? path : editor_state->home_directory() + path.substr(1);
  string actual_path = FindPath(path_after_home_dir, &tokens, &pattern);
  if (actual_path.empty()) {
    if (ignore_if_not_found) {
      return nullptr;
    }
    actual_path = path;
  }
  return std::move(unique_ptr<EditorMode>(
      new FileLinkMode(actual_path, tokens[0], tokens[1], pattern)));
}

}  // namespace afc
}  // namespace editor
