#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <algorithm>

extern "C" {
#include <dirent.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
}

#include "char_buffer.h"
#include "file_link_mode.h"
#include "editor.h"
#include "run_command_handler.h"

namespace {
using std::make_pair;
using std::shared_ptr;
using std::unique_ptr;
using std::sort;
using namespace afc::editor;

class FileBuffer : public OpenBuffer {
 public:
  FileBuffer(const string& path) : path_(path) {}

  void Reload(EditorState* editor_state) {
    struct stat sb;
    if (stat(path_.c_str(), &sb) == -1) {
      return;
    }

    contents_.clear();
    editor_state->screen_needs_redraw = true;

    if (!S_ISDIR(sb.st_mode)) {
      char* tmp = strdup(path_.c_str());
      if (0 == strcmp(basename(tmp), "passwd")) {
        RunCommandHandler("parsers/passwd <" + path_, editor_state);
      } else {
        LoadMemoryMappedFile(path_, this);
      }
      saveable_ = true;
      return;
    }

    unique_ptr<Line> line(new Line);
    line->contents.reset(NewCopyString("File listing: " + path_).release());
    contents_.push_back(std::move(line));

    DIR* dir = opendir(path_.c_str());
    assert(dir != nullptr);
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      unique_ptr<Line> line(new Line);
      line->contents.reset(NewCopyCharBuffer(entry->d_name).release());
      line->activate.reset(NewFileLinkMode(path_ + "/" + entry->d_name, 0).release());
      contents_.push_back(std::move(line));
    }
    closedir(dir);

    struct Compare {
      bool operator()(const shared_ptr<Line>& a, const shared_ptr<Line>& b) {
        return *a->contents < *b->contents;
      }
    } compare;

    sort(contents_.begin() + 1, contents_.end(), compare);
  }

 private:
  const string path_;
};

class FileLinkMode : public EditorMode {
 public:
  FileLinkMode(const string& path, size_t position)
      : path_(realpath(path.c_str(), nullptr)), position_(position) {}

  void ProcessInput(int c, EditorState* editor_state) {
    auto it = editor_state->buffers.insert(make_pair(path_.get(), nullptr));
    editor_state->current_buffer = it.first;
    if (it.second) {
      it.first->second.reset(new FileBuffer(path_.get()));
      it.first->second->Reload(editor_state);
    }
    it.first->second->set_current_position_line(position_);
    editor_state->screen_needs_redraw = true;
    editor_state->mode = std::move(NewCommandMode());
  }

  unique_ptr<char> path_;
  size_t position_;
};

}  // namespace

namespace afc {
namespace editor {

using std::unique_ptr;
using std::stoi;

unique_ptr<EditorMode> NewFileLinkMode(const string& path, int position) {
  string actual_path;
  size_t pos = path.find_last_of(':');
  if (pos != path.npos && pos > 0) {
    string test_path = path.substr(0, pos);
    struct stat dummy;
    if (stat(test_path.c_str(), &dummy) != -1) {
      actual_path = test_path;
      position = stoi(path.substr(pos + 1)) - 1;
    }
  } else {
    actual_path = path;
  }
  return std::move(unique_ptr<EditorMode>(new FileLinkMode(actual_path, position)));
}

}  // namespace afc
}  // namespace editor
