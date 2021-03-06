#ifndef __AFC_EDITOR_FILE_LINK_MODE_H__
#define __AFC_EDITOR_FILE_LINK_MODE_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "editor.h"

namespace afc {
namespace editor {

using std::string;
using std::unique_ptr;

// Saves the contents of the buffer to the path given.  If there's an error,
// updates the editor status and returns false; otherwise, returns true (and
// leaves the status unmodified).
bool SaveContentsToFile(EditorState* editor_state, OpenBuffer* buffer,
                        const wstring& path);

// Saves the contents of the buffer directly to an already open file.  Like
// SaveContentsToFile, either returns true (on success) or updates the editor
// status.
bool SaveContentsToOpenFile(EditorState* editor_state, OpenBuffer* buffer,
                            const wstring& path, int fd);

struct OpenFileOptions {
  OpenFileOptions() {}

  EditorState* editor_state = nullptr;

  // Name can be empty, in which case the name will come from the path.
  wstring name;

  // The path of the file to open.
  wstring path;
  bool ignore_if_not_found = false;
  bool make_current_buffer = true;

  // Should the contents of the search paths buffer be used to find the file?
  bool use_search_paths = true;
  vector<wstring> initial_search_paths;
};

shared_ptr<OpenBuffer> GetSearchPathsBuffer(EditorState* editor_state);
void GetSearchPaths(EditorState* editor_state, vector<wstring>* output);

// Takes a specification of a path (which can be absolute or relative) and, if
// relative, looks it up in the search paths. If a file is found, returns an
// absolute path pointing to it.
struct ResolvePathOptions {
  EditorState* editor_state;
  wstring path;

  // Optional.
  std::function<bool(const wstring&)> validator;

  // Where to write the results: the absolute path pointing to the file.
  wstring* output_path;

  // Optional.
  LineColumn* output_position = nullptr;

  // Optional.
  wstring* output_pattern = nullptr;
};

bool ResolvePath(ResolvePathOptions options);

// Creates a new buffer for the file at the path given.
map<wstring, shared_ptr<OpenBuffer>>::iterator OpenFile(
    const OpenFileOptions& options);

map<wstring, shared_ptr<OpenBuffer>>::iterator OpenAnonymousBuffer(
    EditorState* editor_state);

}  // namespace editor
}  // namespace afc

#endif
