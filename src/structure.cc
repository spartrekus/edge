#include "structure.h"

#include <glog/logging.h>

namespace afc {
namespace editor {

Structure LowerStructure(Structure s) {
  switch (s) {
    case CHAR: return CHAR;
    case WORD: return CHAR;
    case LINE: return WORD;
    case MARK: return LINE;
    case PAGE: return MARK;
    case SEARCH: return PAGE;
    case REGION: return SEARCH;
    case BUFFER: return REGION;
  }
  CHECK(false);
}

}  // namespace editor
}  // namespace afc
