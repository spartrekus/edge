#ifndef __AFC_EDITOR_COMMAND_MODE_H__
#define __AFC_EDITOR_COMMAND_MODE_H__

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "editor_mode.h"
#include "map_mode.h"

namespace afc {
namespace editor {

class EditorState;

using std::unique_ptr;

std::unique_ptr<MapModeCommands> NewCommandMode(EditorState* editor_state);

}  // namespace editor
}  // namespace afc

#endif
