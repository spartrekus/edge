#include "search_command.h"

#include "buffer.h"
#include "command.h"
#include "editor.h"
#include "line_prompt_mode.h"
#include "search_handler.h"
#include "transformation.h"

namespace afc {
namespace editor {

namespace {
static void DoSearch(EditorState* editor_state,
                     const SearchOptions& options) {
  editor_state->current_buffer()
      ->second->set_active_cursors(SearchHandler(editor_state, options));
  editor_state->ResetMode();
  editor_state->ResetDirection();
  editor_state->ResetStructure();
  editor_state->ScheduleRedraw();
}

class SearchCommand : public Command {
 public:
  const wstring Description() {
    return L"Searches for a string.";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    switch (editor_state->structure()) {
      case WORD:
        {
          if (!editor_state->has_current_buffer()) { return; }
          auto buffer = editor_state->current_buffer()->second;
          LineColumn start, end;
          if (!buffer->FindPartialRange(
                   editor_state->modifiers(), buffer->position(), &start,
                   &end) || start == end) {
            editor_state->ResetStructure();
            return;
          }
          editor_state->ResetStructure();
          CHECK_LT(start, end);
          CHECK_EQ(start.line, end.line);
          CHECK_LT(start.column, end.column);
          buffer->set_position(start);
          {
            SearchOptions options;
            options.search_query =
                buffer->LineAt(start.line)
                    ->Substring(start.column, end.column - start.column)
                    ->ToString();
            options.starting_position = buffer->position();
            DoSearch(editor_state, options);
          }

          editor_state->ResetMode();
          editor_state->ResetDirection();
          editor_state->ResetStructure();
          editor_state->ScheduleRedraw();
        }
        break;

      default:
        SearchOptions search_options;
        auto buffer = editor_state->current_buffer()->second;
        if (editor_state->structure() == CURSOR) {
          if (!buffer->FindPartialRange(
                   editor_state->modifiers(), buffer->position(),
                   &search_options.starting_position,
                   &search_options.limit_position) ||
              search_options.starting_position ==
                  search_options.limit_position) {
            editor_state->ResetStructure();
            return;
          }
          CHECK_LE(search_options.starting_position,
                   search_options.limit_position);
          if (editor_state->modifiers().direction == BACKWARDS) {
            LOG(INFO) << "Swaping positions (backwards search).";
            LineColumn tmp = search_options.starting_position;
            search_options.starting_position = search_options.limit_position;
            search_options.limit_position = tmp;
          }
          LOG(INFO) << "Searching region: " << search_options.starting_position
                    << " to " << search_options.limit_position;
          search_options.has_limit_position = true;
        } else {
          search_options.starting_position =
              editor_state->current_buffer()->second->position();
        }
        PromptOptions options;
        options.prompt = L"/";
        options.history_file = L"search";
        options.handler = [search_options](const wstring& input,
                                           EditorState* editor_state) {
          SearchOptions options = search_options;
          options.search_query = input;
          DoSearch(editor_state, options);
        };
        options.predictor = SearchHandlerPredictor;
        Prompt(editor_state, std::move(options));
        break;
    }
  }
};
}  // namespace

std::unique_ptr<Command> NewSearchCommand() {
  return std::unique_ptr<Command>(new SearchCommand());
}

}  // namespace afc
}  // namespace editor