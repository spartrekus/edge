#ifndef __AFC_EDITOR_BUFFER_H__
#define __AFC_EDITOR_BUFFER_H__

#include <condition_variable>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <glog/logging.h>

#include "buffer_contents.h"
#include "cursors.h"
#include "lazy_string.h"
#include "line.h"
#include "line_column.h"
#include "line_marks.h"
#include "map_mode.h"
#include "parse_tree.h"
#include "substring.h"
#include "transformation.h"
#include "tree.h"
#include "variables.h"
#include "vm/public/environment.h"
#include "vm/public/value.h"
#include "vm/public/vm.h"

namespace afc {
namespace editor {

using std::iterator;
using std::list;
using std::map;
using std::max;
using std::min;
using std::multimap;
using std::ostream;
using std::shared_ptr;
using std::unique_ptr;
using std::vector;

using namespace afc::vm;

class OpenBuffer {
 public:
  // Name of a special buffer that shows the list of buffers.
  static const wstring kBuffersName;
  static const wstring kPasteBuffer;

  static void RegisterBufferType(EditorState* editor_state,
                                 Environment* environment);

  OpenBuffer(EditorState* editor_state, const wstring& name);
  ~OpenBuffer();

  bool PrepareToClose(EditorState* editor_state);
  void Close(EditorState* editor_state);

  // If the buffer is still being read (fd_ != -1), adds an observer to
  // end_of_file_observers_. Otherwise just calls the observer directly.
  void AddEndOfFileObserver(std::function<void()> observer);

  virtual void Visit(EditorState* editor_state);
  time_t last_visit() const;
  time_t last_action() const;

  // Saves state of this buffer (not including contents). Currently that means
  // the values of variables, but in the future it could include other things.
  // Returns true if the state could be persisted successfully.
  virtual bool PersistState() const;

  void ClearContents(EditorState* editor_state);
  void AppendEmptyLine(EditorState* editor_state);

  virtual void ReloadInto(EditorState*, OpenBuffer*) {}
  virtual void Save(EditorState* editor_state);

  void MaybeFollowToEndOfFile();

  virtual bool ShouldDisplayProgress() const;
  void RegisterProgress();

  void ReadData(EditorState* editor_state);
  void ReadErrorData(EditorState* editor_state);

  void Reload(EditorState* editor_state);
  virtual void EndOfFile(EditorState* editor_state);

  // Sort all lines in range [first, last) according to a compare function.
  void SortContents(size_t first, size_t last,
                    std::function<bool(const shared_ptr<const Line>&,
                                       const shared_ptr<const Line>&)>
                        compare);

  template <typename T>
  void ForEachLine(T callback) const {
    contents_.ForEach(callback);
  }

  bool empty() const { return contents_.empty(); }
  size_t lines_size() const { return contents_.size(); }

  EditorMode* mode() const { return mode_.get(); }
  std::shared_ptr<EditorMode> ResetMode() {
    auto copy = std::move(mode_);
    mode_.reset(new MapMode(default_commands_));
    return copy;
  }
  void set_mode(std::shared_ptr<EditorMode> mode) { mode_ = std::move(mode); }

  // Erases all lines in range [first, last).
  void EraseLines(size_t first, size_t last);

  // Inserts a new line into the buffer at a given position.
  void InsertLine(size_t line_position, shared_ptr<Line> line);

  void AppendLazyString(EditorState* editor_state,
                        shared_ptr<LazyString> input);
  void AppendLine(EditorState* editor_state, shared_ptr<LazyString> line);
  virtual void AppendRawLine(EditorState* editor_state,
                             shared_ptr<LazyString> str);

  // Insert a line at the end of the buffer.
  void AppendRawLine(EditorState* editor, shared_ptr<Line> line);

  size_t ProcessTerminalEscapeSequence(EditorState* editor_state,
                                       shared_ptr<LazyString> str,
                                       size_t read_index,
                                       LineModifierSet* modifiers);
  void AppendToLastLine(EditorState* editor_state, shared_ptr<LazyString> str);
  void AppendToLastLine(EditorState* editor_state, shared_ptr<LazyString> str,
                        const vector<LineModifierSet>& modifiers);

  unique_ptr<Expression> CompileString(EditorState* editor_state,
                                       const wstring& str,
                                       wstring* error_description);
  void EvaluateExpression(EditorState* editor_state, Expression* expr,
                          std::function<void(std::unique_ptr<Value>)> consumer);
  bool EvaluateString(EditorState* editor_state, const wstring& str,
                      std::function<void(std::unique_ptr<Value>)> consumer);
  bool EvaluateFile(EditorState* editor_state, const wstring& path);

  const wstring& name() const { return name_; }

  void DeleteRange(const Range& range);

  // If modifiers is nullptr, takes the modifiers from the insertion (i.e. from
  // the input). Otherwise, uses modifiers for every character inserted.
  LineColumn InsertInPosition(const OpenBuffer& insertion,
                              const LineColumn& position,
                              const LineModifierSet* modifiers);
  // Checks that line column is in the expected range (between 0 and the length
  // of the current line).
  void AdjustLineColumn(LineColumn* output) const;

  // Like AdjustLineColumn but for the current cursor.
  void MaybeAdjustPositionCol();

  // Makes sure that the current line (position) is not greater than the number
  // of elements in contents().  Note that after this, it may still not be a
  // valid index for contents() (it may be just at the end).
  void CheckPosition();

  CursorsSet* FindCursors(const wstring& name);
  CursorsSet* active_cursors();
  // Removes all active cursors and replaces them with the ones given. The old
  // cursors are saved and can be restored with ToggleActiveCursors.
  void set_active_cursors(const vector<LineColumn>& positions);

  // Restores the last cursors available.
  void ToggleActiveCursors();
  void PushActiveCursors();
  void PopActiveCursors();
  // Replaces the set of active cursors with one cursor in every position with
  // a mark (based on line_marks_).
  void SetActiveCursorsToMarks();

  void set_current_cursor(CursorsSet::value_type new_cursor);
  void CreateCursor();
  CursorsSet::iterator FindPreviousCursor(LineColumn cursor);
  CursorsSet::iterator FindNextCursor(LineColumn cursor);
  void DestroyCursor();
  void DestroyOtherCursors();

  const ParseTree* current_tree(const ParseTree* root) const;
  void set_lines_for_zoomed_out_tree(size_t lines);
  std::shared_ptr<const ParseTree> zoomed_out_tree() const;

  // Moves position in the specified direction until we're inside the structure
  // of the type specified that starts after position. No-op if we're already
  // inside the structure.
  void SeekToStructure(Structure structure, Direction direction,
                       LineColumn* position);
  // Moves position in the specified direction until we're just outside of the
  // current structure of the type specified. No-op if we're already outside the
  // structure. Returns a boolean indicating whether it successfully found a
  // position outside of the structure.
  bool SeekToLimit(Structure structure, Direction direction,
                   LineColumn* position);

  // TODO: Change start and end to be a Range?
  bool FindPartialRange(const Modifiers& modifiers, const LineColumn& position,
                        LineColumn* start, LineColumn* end);

  // May return nullptr if the current_cursor is at the end of file.
  const shared_ptr<const Line> current_line() const;

  shared_ptr<const Line> LineFront() const {
    CHECK(!contents_.empty());
    return LineAt(0);
  }
  shared_ptr<const Line> LineBack() const {
    CHECK(!contents_.empty());
    return LineAt(lines_size() - 1);
  }
  shared_ptr<const Line> LineAt(size_t line_number) const {
    CHECK(!contents_.empty());
    if (line_number >= contents_.size()) {
      return nullptr;
    }
    return contents_.at(line_number);
  }
  char character_at(LineColumn position) const {
    return contents_.character_at(position);
  }

  // If there's a buffer associated with the current line (looking up the
  // "buffer" variable in the line's environment), returns it. Returns nullptr
  // otherwise.
  std::shared_ptr<OpenBuffer> GetBufferFromCurrentLine();

  // Returns the substring of the current line until the current position.
  shared_ptr<LazyString> current_line_head() const {
    return current_line()->Substring(0, current_position_col());
  }
  // Returns the substring of the current line from the current to the last
  // position.
  shared_ptr<LazyString> current_line_tail() const {
    return current_line()->Substring(current_position_col());
  }
  // Serializes the buffer into a string.  This is not particularly fast (it's
  // meant more for debugging/testing rather than for real use).
  wstring ToString() const;

  void replace_current_line(const shared_ptr<Line>& line) {
    if (cursors_tracker_.position().line >= contents_.size()) {
      CHECK(!contents_.empty());
      set_current_position_line(contents_.size() - 1);
    }
    contents_.set_line(cursors_tracker_.position().line, line);
  }

  int fd() const { return fd_.fd; }
  int fd_error() const { return fd_error_.fd; }

  // We deliberately provide only a read view into our contents. All
  // modifications should be done through methods defined in this class.
  const BufferContents* contents() const { return &contents_; }
  // Delete characters in [column, column + amount).

  bool at_beginning() const {
    if (contents_.empty()) {
      return true;
    }
    return position().at_beginning();
  }
  bool at_beginning_of_line() const { return at_beginning_of_line(position()); }
  bool at_beginning_of_line(const LineColumn& position) const {
    if (contents_.empty()) {
      return true;
    }
    return position.at_beginning_of_line();
  }
  bool at_end() const { return at_end(position()); }
  bool at_end(const LineColumn& position) const {
    if (contents_.empty()) {
      return true;
    }
    return at_last_line(position) && at_end_of_line(position);
  }

  // Returns the position of just after the last character of the current file.
  LineColumn end_position() const {
    if (contents_.empty()) {
      return LineColumn(0, 0);
    }
    return LineColumn(contents_.size() - 1, contents_.back()->size());
  }

  bool at_last_line() const { return at_last_line(position()); }
  bool at_last_line(const LineColumn& position) const {
    return position.line == contents_.size() - 1;
  }
  bool at_end_of_line() const { return at_end_of_line(position()); }
  bool at_end_of_line(const LineColumn& position) const {
    if (contents_.empty()) {
      return true;
    }
    return position.column >= LineAt(position.line)->size();
  }
  char current_character() const {
    CHECK_LT(current_position_col(), current_line()->size());
    return current_line()->get(current_position_col());
  }
  char previous_character() const {
    CHECK_GT(current_position_col(), 0u);
    return current_line()->get(current_position_col() - 1);
  }

  void set_current_position_line(size_t line);
  size_t current_position_line() const;
  size_t current_position_col() const;
  void set_current_position_col(size_t column);

  const LineColumn position() const;

  void set_position(const LineColumn& position);

  void ClearModified() { modified_ = false; }
  bool modified() const { return modified_; }

  bool dirty() const {
    return modified_ || child_pid_ != -1 || !WIFEXITED(child_exit_status_) ||
           WEXITSTATUS(child_exit_status_) != 0;
  }
  virtual wstring FlagsString() const;

  void PushSignal(EditorState* editor_state, int signal);

  wstring TransformKeyboardText(wstring input);
  bool AddKeyboardTextTransformer(EditorState* editor_state,
                                  unique_ptr<Value> transformer);

  void SetInputFiles(EditorState* editor_state, int input_fd,
                     int input_fd_error, bool fd_is_terminal, pid_t child_pid);

  const bool& Read(const EdgeVariable<bool>* variable) const;
  void set_bool_variable(const EdgeVariable<bool>* variable, bool value);
  void toggle_bool_variable(const EdgeVariable<bool>* variable);

  const wstring& Read(const EdgeVariable<wstring>* variable) const;
  void set_string_variable(const EdgeVariable<wstring>* variable,
                           wstring value);

  const int& Read(const EdgeVariable<int>* variable) const;
  void set_int_variable(const EdgeVariable<int>* variable, int value);

  const double& Read(const EdgeVariable<double>* variable) const;
  void set_double_variable(const EdgeVariable<double>* variable, double value);

  void ApplyToCursors(unique_ptr<Transformation> transformation);
  void ApplyToCursors(unique_ptr<Transformation> transformation,
                      Modifiers::CursorsAffected cursors_affected);
  void RepeatLastTransformation();

  void PushTransformationStack();
  void PopTransformationStack();
  bool HasTransformationStack() const {
    return !last_transformation_stack_.empty();
  }

  enum UndoMode {
    // Default mode. Don't count transformations that didn't modify the buffer.
    SKIP_IRRELEVANT,
    // Count every transformation (even those that don't modify the buffer).
    ONLY_UNDO_THE_LAST,
  };
  void Undo(EditorState* editor_state);
  void Undo(EditorState* editor_state, UndoMode undo_mode);

  void set_filter(unique_ptr<Value> filter);
  bool IsLineFiltered(size_t line);

  pid_t child_pid() const { return child_pid_; }
  int child_exit_status() const { return child_exit_status_; }

  size_t last_highlighted_line() const { return last_highlighted_line_; }
  void set_last_highlighted_line(size_t value) {
    last_highlighted_line_ = value;
  }

  // Returns a multimap with all the marks for the current buffer, indexed by
  // the line they refer to. Each call may update the map.
  const multimap<size_t, LineMarks::Mark>* GetLineMarks(
      const EditorState& editor_state) const;
  wstring GetLineMarksText(const EditorState& editor_state) const;

  Environment* environment() { return &environment_; }

  std::shared_ptr<const ParseTree> parse_tree() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return parse_tree_;
  }

  std::shared_ptr<const ParseTree> simplified_parse_tree() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return simplified_parse_tree_;
  }

  size_t tree_depth() const { return tree_depth_; }
  void set_tree_depth(size_t tree_depth) { tree_depth_ = tree_depth; }

  LineColumn PositionBefore(LineColumn position) const {
    if (contents_.empty()) {
      position = LineColumn();
    } else if (position.column > 0) {
      position.column--;
    } else if (position.line > 0) {
      position.line = min(position.line - 1, contents_.size() - 1);
      position.column = contents_.at(position.line)->size();
    }
    return position;
  }

  // Only Editor::ProcessInputString should call this; everyone else should just
  // call Editor::ScheduleParseTreeUpdate.
  void ResetParseTree();

 protected:
  EditorState* editor_;

  wstring name_;

  struct Input {
    void Close();
    void Reset();
    void ReadData(EditorState* editor_state, OpenBuffer* target);

    // -1 means "no file descriptor" (i.e. not currently loading this).
    int fd = -1;

    // We read directly into low_buffer_ and then drain from that into
    // contents_. It's possible that not all bytes read can be converted (for
    // example, if the reading stops in the middle of a wide character).
    std::unique_ptr<char[]> low_buffer;
    size_t low_buffer_length = 0;

    LineModifierSet modifiers;
  };

  Input fd_;
  Input fd_error_;

  // This is used to remember if we obtained a terminal for the file descriptor
  // (for a subprocess).  Typically this has the same value of pts_ after a
  // subprocess is started, but it's a separate value to allow the user to
  // change the value of pts_ without breaking things (when one command is
  // already running).
  bool fd_is_terminal_ = false;

  // Functions to be called when the end of file is reached. The functions will
  // be called at most once (so they won't be notified if the buffer is
  // reloaded.
  vector<std::function<void()>> end_of_file_observers_;

  // -1 means "no child process"
  pid_t child_pid_;
  int child_exit_status_;

  LineColumn position_pts_;

  BufferContents contents_;

  bool modified_;
  bool reading_from_parser_;

  EdgeStructInstance<bool> bool_variables_;
  EdgeStructInstance<wstring> string_variables_;
  EdgeStructInstance<int> int_variables_;
  EdgeStructInstance<double> double_variables_;

  // When a transformation is done, we append its result to
  // transformations_past_, so that it can be undone.
  list<unique_ptr<Transformation::Result>> transformations_past_;
  list<unique_ptr<Transformation::Result>> transformations_future_;

  list<unique_ptr<Value>> keyboard_text_transformers_;
  Environment environment_;

  // A function that receives a string and returns a boolean. The function will
  // be evaluated on every line, to compute whether or not the line should be
  // shown.  This does not remove any lines: it merely hides them (by setting
  // the Line::filtered field).
  unique_ptr<Value> filter_;
  size_t filter_version_;

 private:
  static void EvaluateMap(EditorState* editor, OpenBuffer* buffer, size_t line,
                          Value::Callback map_callback,
                          TransformationStack* transformation,
                          Trampoline* trampoline);
  LineColumn Apply(EditorState* editor_state,
                   unique_ptr<Transformation> transformation);
  void BackgroundThread();
  // Destroys the background thread if it's running and if a given predicate
  // returns true. The predicate is evaluated with mutex_ held.
  void DestroyThreadIf(std::function<bool()> predicate);
  void UpdateTreeParser();

  // Adds a new line. If there's a previous line, notifies various things about
  // it.
  void StartNewLine(EditorState* editor_state);
  void ProcessCommandInput(EditorState* editor_state,
                           shared_ptr<LazyString> str);

  // Returns true if the position given is set to a value other than
  // LineColumn::Max and the buffer has read past that position.
  bool IsPastPosition(LineColumn position) const;

  // Whenever the contents are modified, we set this to the snapshot (after the
  // modification). The background thread will react to this: it'll take the
  // value out (and reset it to null). Once it's done, it'll update the parse
  // tree.
  std::unique_ptr<const BufferContents> contents_to_parse_;

  unique_ptr<Transformation> last_transformation_;

  // We allow the user to group many transformations in one.  They each get
  // applied immediately, but upon repeating, the whole operation gets repeated.
  // This is controlled through OpenBuffer::PushTransformationStack, which sets
  // this to non-null (to signal that we've entered this mode) and
  // OpenBuffer::PopTransformationStack (which sets this back to null and moves
  // this value to last_transformation_).
  list<unique_ptr<TransformationStack>> last_transformation_stack_;

  // If variable_atomic_lines is true, this will be set to the last line that
  // was highlighted.
  size_t last_highlighted_line_ = 0;

  // Index of the marks for the current buffer (i.e. Mark::target_buffer is the
  // current buffer). The key is the line (i.e. Mark::line).
  //
  // mutable because GetLineMarks will always update it if needed.
  mutable multimap<size_t, LineMarks::Mark> line_marks_;
  // The value that EditorState::marks_::updates had when we last computed
  // line_marks_. This allows us to avoid recomputing line_marks_ when no new
  // marks have been added.
  mutable size_t line_marks_last_updates_ = 0;

  CursorsTracker cursors_tracker_;

  // If we get a request to open a buffer and jump to a given line, we store
  // that value here. Once we've read enough, we stay at this position. It can
  // be set to LineColumn::Max to signify "no desired position".
  LineColumn desired_position_;

  std::shared_ptr<const ParseTree> parse_tree_;
  std::shared_ptr<const ParseTree> simplified_parse_tree_;
  std::shared_ptr<TreeParser> tree_parser_;
  size_t tree_depth_ = 0;
  size_t lines_for_zoomed_out_tree_ = 0;
  std::shared_ptr<const ParseTree> zoomed_out_tree_;

  std::shared_ptr<MapModeCommands> default_commands_;
  std::shared_ptr<EditorMode> mode_;

  // The time when the buffer was last selected as active.
  time_t last_visit_ = 0;
  // The time when the buffer last saw some action. This includes being visited,
  // receiving input and probably other things.
  time_t last_action_ = 0;

  // Protects all the variables that background thread may access.
  mutable std::mutex mutex_;
  std::condition_variable background_condition_;
  // Protects access to background_thread_ itself. Must never be acquired after
  // mutex_ (only before). Anybody assigning to background_thread_shutting_down_
  // must do so and join the thread while holding this mutex.
  mutable std::mutex thread_creation_mutex_;
  std::thread background_thread_;
  bool background_thread_shutting_down_ = false;

  // The time when variable_progress was last incremented.
  //
  // TODO: Add a Time type to the VM and expose this?
  struct timespec last_progress_update_ = {0, 0};
};

}  // namespace editor
}  // namespace afc

#endif
