string ProcessCCInputLine(string line) {
  SetStatus("Got: " + line);
  return line;
}

string Basename(string path) {
  int last_slash = path.find_last_of("/", path.size());
  if (last_slash == -1) {
    return path;
  }
  return path.substr(last_slash + 1, path.size() - (last_slash + 1));
}

buffer.set_editor_commands_path("~/.edge/editor_commands/");

string path = buffer.path();
if (path == "") {
  buffer.set_follow_end_of_file(false);

  string command = buffer.command();
  if (command != "") {
    buffer.set_paste_mode(true);
  }
  int space = command.find_first_of(" ", 0);
  string base_command = space == -1 ? command : command.substr(0, space);
  if (base_command != "") {
    SetStatus("Running command: [" + base_command + "]");
    if (base_command == "bash" || base_command == "python"
        || base_command == "sh") {
      buffer.set_pts(true);
      buffer.set_follow_end_of_file(true);
      buffer.set_buffer_list_context_lines(5);
    }
    if (base_command == "make") {
      buffer.set_contains_line_marks(true);
      buffer.set_reload_on_buffer_write(true);
      buffer.set_follow_end_of_file(true);
      buffer.set_buffer_list_context_lines(5);
    }
    if (base_command == "grep") {
      buffer.set_contains_line_marks(true);
      buffer.set_allow_dirty_delete(true);
    }
    buffer.set_atomic_lines(false);
    buffer.set_reload_on_enter(false);
  }
} else {
  int dot = path.find_last_of(".", path.size());
  string extension = dot == -1 ? "" : path.substr(dot + 1, path.size() - dot - 1);
  string basename = Basename(path);
  if (extension == "cc" || extension == "h" || extension == "c") {
    buffer.set_line_prefix_characters(" /*");
    SetStatus("Loaded C file (" + extension + ")");
    return;
  }

  if (extension == "java") {
    buffer.set_line_prefix_characters(" /*");
    buffer.set_line_width(100);
    SetStatus("Loaded Java file (" + extension + ")");
    return;
  }

  if (basename == "COMMIT_EDITMSG") {
    buffer.set_line_prefix_characters(" #");
    SetStatus("GIT commit msg");
  }

  if (extension == "py") {
    buffer.set_editor_commands_path("~/.edge/editor_commands/");
    buffer.set_line_prefix_characters(" #");
    SetStatus("Loaded Python file (" + extension + ")");
  }
}
