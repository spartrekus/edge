#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

extern "C" {
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
}

#include <glog/logging.h>

#include "config.h"

#include "audio.h"
#include "editor.h"
#include "file_link_mode.h"
#include "lazy_string.h"
#include "run_command_handler.h"
#include "screen.h"
#include "screen_curses.h"
#include "screen_vm.h"
#include "server.h"
#include "terminal.h"
#include "vm/public/value.h"
#include "wstring.h"

namespace {

using namespace afc::editor;

static const char* kEdgeParentAddress = "EDGE_PARENT_ADDRESS";
static std::unique_ptr<EditorState> global_editor_state;

EditorState* editor_state() { return global_editor_state.get(); }

void (*default_interrupt_handler)(int sig);
void (*default_stop_handler)(int sig);

void SignalHandler(int sig) {
  if (sig == SIGINT) {
    if (!editor_state()->handling_interrupts()) {
      signal(SIGINT, default_interrupt_handler);
      raise(SIGINT);
      return;
    }

    // Normally, when the buffer consumes the signal, it'll overwrite the
    // status right away. So we just put a default message in case the signal is
    // not consumed.
    editor_state()->SetWarningStatus(
        L"'Saq' to quit -- pending changes won't be saved.");
  } else if (sig == SIGTSTP) {
    if (!editor_state()->handling_stop_signals()) {
      signal(SIGINT, default_stop_handler);
      raise(SIGINT);
      signal(SIGINT, &SignalHandler);
      return;
    }
  }

  editor_state()->PushSignal(sig);
}

struct Args {
  string binary_name;
  vector<string> files_to_open;
  vector<string> commands_to_fork;

  // Contains C++ (VM) code to execute.
  string commands_to_run;

  bool server = false;
  string server_path = "";

  // If non-empty, path of the server to connect to.
  string client = "";

  bool mute = false;
  bool background = false;
};

static const char* kDefaultCommandsToRun = "ForkCommand(\"sh -l\", true);";

string CommandsToRun(Args args) {
  string commands_to_run = args.commands_to_run;
  for (auto& path : args.files_to_open) {
    string full_path;
    if (!path.empty() && string("/~").find(path[0]) != string::npos) {
      LOG(INFO) << "Will open an absolute path: " << path;
      full_path = path;
    } else {
      LOG(INFO) << "Will open a relative path: " << path;
      char* dir = get_current_dir_name();
      full_path = string(dir) + "/" + path;
      free(dir);
    }
    commands_to_run += "OpenFile(\"" + full_path + "\");\n";
  }
  for (auto& command_to_fork : args.commands_to_fork) {
    commands_to_run += "ForkCommand(\"" + string(command_to_fork) + "\", " +
                       (args.background ? "false" : "true") + ");\n";
  }
  if (!args.client.empty()) {
    commands_to_run += "Screen screen = RemoteScreen(\"" +
                       string(getenv(kEdgeParentAddress)) + "\");\n";
  }
  if (commands_to_run.empty()) {
    return kDefaultCommandsToRun;
  }
  return commands_to_run;
}

Args ParseArgs(int* argc, const char*** argv) {
  using std::cerr;
  using std::cout;

  string kHelpString =
      "Usage: edge [OPTION]... [FILE]...\n"
      "Open the files given.\n\nEdge supports the following options:\n"
      "  -f, --fork <shellcmd>  Creates a buffer running a shell command\n"
      "  -h, --help             Displays this message\n"
      "  --run <vmcmd>          Runs a VM command\n"
      "  --load <path>          Loads a file with VM commands\n"
      "  -s, --server <path>    Runs in daemon mode at path given\n"
      "  -c, --client <path>    Connects to daemon at path given\n"
      "  --mute                 Disables audio output\n"
      "  --bg                   -f opens buffers in background\n";

  Args output;
  auto pop_argument = [argc, argv, &output]() {
    if (*argc == 0) {
      cerr << output.binary_name << ": Parameters missing." << std::endl;
      exit(1);
    }
    (*argv)++;
    (*argc)--;
    return (*argv)[-1];
  };

  output.binary_name = pop_argument();

  while (*argc > 0) {
    string cmd = pop_argument();
    if (cmd.empty()) {
      continue;
    }
    if (cmd[0] != '-') {
      output.files_to_open.push_back(cmd);
    } else if (cmd == "--help" || cmd == "-h") {
      cout << kHelpString;
      exit(0);
    } else if (cmd == "--fork" || cmd == "-f") {
      CHECK_GT(*argc, 0) << output.binary_name << ": " << cmd
                         << ": Expected command to fork.\n";
      output.commands_to_fork.push_back(pop_argument());
    } else if (cmd == "--run") {
      CHECK_GT(*argc, 0) << output.binary_name << ": " << cmd
                         << ": Expected command to run.\n";
      output.commands_to_run += pop_argument();
    } else if (cmd == "--load" || cmd == "-l") {
      CHECK_GT(*argc, 0) << output.binary_name << ": " << cmd
                         << ": Expected path to VM commands to run.\n";
      output.commands_to_run +=
          "buffer.EvaluateFile(\"" +
          ToByteString(CppEscapeString(FromByteString(pop_argument()))) +
          "\");";
    } else if (cmd == "--server" || cmd == "-s") {
      output.server = true;
      if (*argc > 0) {
        output.server_path = pop_argument();
      }
    } else if (cmd == "--client" || cmd == "-c") {
      output.client = pop_argument();
      if (output.client.empty()) {
        cerr << output.binary_name << ": --client: Missing server path."
             << std::endl;
        exit(1);
      }
    } else if (cmd == "--mute") {
      output.mute = true;
    } else if (cmd == "--bg") {
      output.background = true;
    } else {
      cerr << output.binary_name << ": Invalid flag: " << cmd << std::endl;
      exit(1);
    }
  }

  return output;
}

void SendCommandsToParent(int fd, const string commands_to_run) {
  CHECK_NE(fd, -1);
  using std::cerr;
  LOG(INFO) << "Sending commands to parent: " << commands_to_run;
  if (write(fd, commands_to_run.c_str(), commands_to_run.size()) == -1) {
    cerr << "write: " << strerror(errno);
    exit(1);
  }
}

wstring StartServer(const Args& args) {
  LOG(INFO) << "Starting server.";

  wstring address;
  std::unordered_set<int> surviving_fds = {1, 2};
  if (args.server && !args.server_path.empty()) {
    address = FromByteString(args.server_path);
    // We can't close stdout until we've printed the address in which the server
    // will run.
    Daemonize(surviving_fds);
  }
  wstring actual_address;
  wstring error;
  if (!StartServer(editor_state(), address, &actual_address, &error)) {
    LOG(FATAL) << args.binary_name << ": Unable to start server: " << error;
  }
  if (args.server) {
    std::cout << args.binary_name << ": Server starting at: " << actual_address
              << std::endl;
    for (int fd : surviving_fds) {
      close(fd);
    }
  }
  return actual_address;
}

std::wstring GetGreetingMessage() {
  static std::vector<wstring> errors({
      L"Welcome to Edge!",
      L"Edge, your favorite text editor.",
      L"It looks like you're writing a letter. Would you like help?",
      L"Edge, a text editor.",
      L"All modules are now active.",
      L"Booting up Edge. . . . . . . . . . . . . DONE",
      L"What are you up to today?",
      L"The trouble is, you think you have time.",
      L"Happiness is here, and now.",
      L"The journey of a thousand miles begins with a single step.",
  });
  return errors[rand() % errors.size()];
}
}  // namespace

int main(int argc, const char** argv) {
  using namespace afc::editor;
  using std::cerr;
  using std::unique_ptr;

  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  srand(time(NULL));

  string locale = std::setlocale(LC_ALL, "");
  LOG(INFO) << "Using locale: " << locale;

  Args args = ParseArgs(&argc, &argv);

  auto audio_player = args.mute ? NewNullAudioPlayer() : NewAudioPlayer();
  global_editor_state = std::make_unique<EditorState>(audio_player.get());

  int remote_server_fd = -1;
  if (!args.client.empty()) {
    wstring parent_server_error;
    remote_server_fd = MaybeConnectToServer(args.client, &parent_server_error);
    if (remote_server_fd == -1) {
      cerr << args.binary_name
           << ": Unable to connect to remote server: " << parent_server_error
           << std::endl;
      exit(1);
    }
  } else {
    remote_server_fd = MaybeConnectToParentServer(nullptr);
    if (remote_server_fd != -1) {
      SendCommandsToParent(remote_server_fd, CommandsToRun(args));
      cerr << args.binary_name << ": Waiting for EOF ...\n";
      char buffer[4096];
      while (read(0, buffer, sizeof(buffer)) > 0) continue;
      cerr << args.binary_name << ": EOF received, exiting.\n";
      exit(0);
    }
  }

  std::shared_ptr<Screen> screen_curses;
  if (!args.server) {
    screen_curses = NewScreenCurses();
  }
  RegisterScreenType(editor_state()->environment());
  editor_state()->environment()->Define(
      L"screen", afc::vm::Value::NewObject(L"Screen", screen_curses));

  auto server_path = StartServer(args);

  auto commands_to_run = CommandsToRun(args);
  if (!commands_to_run.empty()) {
    int self_fd;
    wstring errors;
    if (remote_server_fd != -1) {
      self_fd = remote_server_fd;
    } else if (args.server && !args.server_path.empty()) {
      self_fd = MaybeConnectToServer(args.server_path, &errors);
    } else {
      self_fd = MaybeConnectToParentServer(&errors);
    }
    if (self_fd == -1) {
      std::cerr << args.binary_name << ": " << errors << std::endl;
      exit(1);
    }
    CHECK_NE(self_fd, -1);
    SendCommandsToParent(self_fd, commands_to_run);
  }

  std::mbstate_t mbstate = std::mbstate_t();
  Terminal terminal;
  if (!args.server) {
    default_interrupt_handler = signal(SIGINT, &SignalHandler);
    default_stop_handler = signal(SIGTSTP, &SignalHandler);
  }

  // This is only meaningful if we're running with args.client: it contains the
  // last observed size of our screen (to detect that we need to propagate
  // changes to the server).
  std::pair<size_t, size_t> last_screen_size = {-1, -1};

  BeepFrequencies(audio_player.get(), {783.99, 723.25, 783.99});
  editor_state()->SetStatus(GetGreetingMessage());

  while (!editor_state()->terminate()) {
    editor_state()->UpdateBuffers();
    auto screen_state = editor_state()->FlushScreenState();
    if (screen_curses != nullptr) {
      if (args.client.empty()) {
        terminal.Display(editor_state(), screen_curses.get(), screen_state);
      } else {
        screen_curses->Refresh();  // Don't want this to be buffered!
        auto screen_size =
            std::make_pair(screen_curses->columns(), screen_curses->lines());
        if (screen_size != last_screen_size) {
          LOG(INFO) << "Sending screen size update to server.";
          SendCommandsToParent(remote_server_fd,
                               "screen.set_size(" +
                                   std::to_string(screen_size.first) + "," +
                                   std::to_string(screen_size.second) + ");" +
                                   "set_screen_needs_hard_redraw(true);\n");
          last_screen_size = screen_size;
        }
      }
    }
    VLOG(5) << "Updating remote screens.";
    for (auto& buffer : *editor_state()->buffers()) {
      auto value = buffer.second->environment()->Lookup(L"screen");
      if (value->type.type != VMType::OBJECT_TYPE ||
          value->type.object_type != L"Screen") {
        continue;
      }
      auto buffer_screen = static_cast<Screen*>(value->user_value.get());
      if (buffer_screen == nullptr) {
        continue;
      }
      if (buffer_screen == screen_curses.get()) {
        continue;
      }
      LOG(INFO) << "Remote screen for buffer: " << buffer.first;
      terminal.Display(editor_state(), buffer_screen, screen_state);
    }

    std::vector<std::shared_ptr<OpenBuffer>> buffers;

    // The file descriptor at position i will be either fd or fd_error of
    // buffers[i]. The exception to this is fd 0 (at the end).
    struct pollfd fds[editor_state()->buffers()->size() * 2 + 3];
    buffers.reserve(sizeof(fds) / sizeof(fds[0]));

    for (auto& buffer : *editor_state()->buffers()) {
      if (buffer.second->fd() != -1) {
        VLOG(5) << buffer.first
                << ": Installing (out) fd: " << buffer.second->fd();
        fds[buffers.size()].fd = buffer.second->fd();
        fds[buffers.size()].events = POLLIN | POLLPRI;
        buffers.push_back(buffer.second);
      }
      if (buffer.second->fd_error() != -1) {
        VLOG(5) << buffer.first
                << ": Installing (err) fd: " << buffer.second->fd();
        fds[buffers.size()].fd = buffer.second->fd_error();
        fds[buffers.size()].events = POLLIN | POLLPRI;
        buffers.push_back(buffer.second);
      }
    }

    if (screen_curses != nullptr) {
      fds[buffers.size()].fd = 0;
      fds[buffers.size()].events = POLLIN | POLLPRI;
      buffers.push_back(nullptr);
    }

    fds[buffers.size()].fd = editor_state()->fd_to_detect_internal_events();
    fds[buffers.size()].events = POLLIN | POLLPRI;
    buffers.push_back(nullptr);

    // TODO: Change to -1. Requires figuring out a way for background threads of
    // buffers to trigger redraws.
    if (poll(fds, buffers.size(), 1000) == -1) {
      CHECK_EQ(errno, EINTR) << "poll failed: " << strerror(errno);

      LOG(INFO) << "Received signals.";
      if (args.client.empty()) {
        // We schedule a redraw in case the signal was SIGWINCH (the screen
        // size has changed). Ideally we'd only do that for that signal, to
        // avoid spurious refreshes, but... who cares.
        editor_state()->set_screen_needs_hard_redraw(true);

        editor_state()->ProcessSignals();
      }

      continue;
    }

    for (size_t i = 0; i < buffers.size(); i++) {
      if (!(fds[i].revents & (POLLIN | POLLPRI | POLLHUP))) {
        continue;
      }
      if (fds[i].fd == 0) {
        CHECK(screen_curses != nullptr);
        wint_t c;
        while ((c = ReadChar(&mbstate)) != static_cast<wint_t>(-1)) {
          if (remote_server_fd == -1) {
            editor_state()->ProcessInput(c);
          } else {
            SendCommandsToParent(remote_server_fd,
                                 "ProcessInput(" + std::to_string(c) + ");\n");
          }
        }
        continue;
      }

      if (fds[i].fd == editor_state()->fd_to_detect_internal_events()) {
        char buffer[4096];
        VLOG(5) << "Internal events detected.";
        while (read(fds[i].fd, buffer, sizeof(buffer)) > 0) continue;
        continue;
      }

      CHECK_LE(i, buffers.size());
      CHECK(buffers[i] != nullptr);
      if (buffers[i] && fds[i].fd == buffers[i]->fd()) {
        LOG(INFO) << "Reading (normal): " << buffers[i]->name();
        buffers[i]->ReadData(editor_state());
      } else if (buffers[i] && fds[i].fd == buffers[i]->fd_error()) {
        LOG(INFO) << "Reading (error): " << buffers[i]->name();
        buffers[i]->ReadErrorData(editor_state());
      } else {
        LOG(FATAL) << "Invalid file descriptor.";
      }
    }
  }

  LOG(INFO) << "Removing server file: " << server_path;
  unlink(ToByteString(server_path).c_str());
  return editor_state()->exit_value();
}
