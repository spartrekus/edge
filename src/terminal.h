#ifndef __AFC_EDITOR_TERMINAL_H__
#define __AFC_EDITOR_TERMINAL_H__

#include <cwchar>
#include <memory>
#include <list>
#include <string>

#include "editor.h"

namespace afc {
namespace editor {

class Terminal {
 public:
  static constexpr int ESCAPE = -2;
  static constexpr int DOWN_ARROW = -3;
  static constexpr int UP_ARROW = -4;
  static constexpr int LEFT_ARROW = -5;
  static constexpr int RIGHT_ARROW = -6;
  static constexpr int BACKSPACE = -7;
  static constexpr int PAGE_DOWN = -8;
  static constexpr int PAGE_UP = -9;
  static constexpr int CTRL_L = -10;
  static constexpr int CTRL_V = -11;
  static constexpr int CTRL_U = -12;
  static constexpr int CTRL_K = -13;
  static constexpr int CHAR_EOF = -14;
  static constexpr int CTRL_A = -15;
  static constexpr int CTRL_E = -16;

  Terminal();
  ~Terminal();

  void Display(EditorState* editor_state);
  void SetStatus(const std::wstring& status);
  wint_t Read(EditorState* editor_state);

 private:
  void ShowStatus(const EditorState& editor_state);
  wstring GetBufferContext(
      const EditorState& editor_state,
      const shared_ptr<OpenBuffer>& buffer);
  void ShowBuffer(const EditorState* editor_state);
  void AdjustPosition(const shared_ptr<OpenBuffer> buffer);

  std::wstring status_;

  std::mbstate_t mbstate_;
};

}  // namespace editor
}  // namespace afc

#endif
