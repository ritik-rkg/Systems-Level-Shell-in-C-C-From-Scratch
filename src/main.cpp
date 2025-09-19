#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <readline/history.h>
#include <readline/readline.h>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>
static std::string last_prefix;
static bool last_multiple_matches = false;
static int tab_press_count = 0;
static std::vector<std::string> last_matches;
char *command_generator(const char *text, int state) {
    static size_t match_index;
    if (state == 0) {
        last_matches.clear();
        match_index = 0;
        std::string textStr(text);
        std::unordered_set<std::string> seen;
        const std::vector<std::string> vocabulary{"echo", "exit", "type",
                                                  "pwd",  "cd",   "history"};
        for (const auto &word : vocabulary) {
            if (word.compare(0, textStr.size(), textStr) == 0 &&
                word != textStr) {
                last_matches.push_back(word);
                seen.insert(word);
            }
        }
        const char *path_env = std::getenv("PATH");
        if (path_env) {
            std::istringstream iss(path_env);
            std::string dir;
            while (std::getline(iss, dir, ':')) {
                try {
                    if (!std::filesystem::exists(dir) ||
                        !std::filesystem::is_directory(dir))
                        continue;
                    for (const auto &entry :
                         std::filesystem::directory_iterator(dir)) {
                        std::string filename = entry.path().filename().string();
                        if (filename.compare(0, textStr.size(), textStr) == 0 &&
                            filename != textStr &&
                            access(entry.path().c_str(), X_OK) == 0 &&
                            seen.find(filename) == seen.end()) {
                            last_matches.push_back(filename);
                            seen.insert(filename);
                        }
                    }
                } catch (...) {
                    continue;
                }
            }
        }
        // **Filter out the prefix itself if it somehow got added**
        last_matches.erase(
            std::remove(last_matches.begin(), last_matches.end(), textStr),
            last_matches.end());
        std::sort(last_matches.begin(), last_matches.end());
    }
    if (match_index >= last_matches.size()) {
        return nullptr;
    } else {
        return strdup(last_matches[match_index++].c_str());
    }
}
char **custom_completion(const char *text, int start, int end) {
    rl_attempted_completion_over = 1;
    std::string current_prefix(text);
    if (current_prefix != last_prefix) {
        last_prefix = current_prefix;
        tab_press_count = 0;
        last_multiple_matches = false;
    }
    char **matches = rl_completion_matches(text, command_generator);
    int match_count = 0;
    if (matches) {
        while (matches[match_count] != nullptr)
            match_count++;
    }
    if (match_count > 1) {
        tab_press_count++;
        last_multiple_matches = true;
        if (tab_press_count == 1) {
            // ring the bell only, no printing
            std::cout << "\a" << std::flush;
            return matches;
        } else if (tab_press_count == 2) {
            std::cout << std::endl;
            // Skip the prefix itself if it's included as a match
            int start_index = 0;
            if (match_count > 0 && strcmp(matches[0], text) == 0) {
                start_index = 1;
            }
            for (int i = start_index; i < match_count; i++) {
                std::cout << matches[i] << "  ";
            }
            std::cout << std::endl;
            rl_on_new_line();
            rl_replace_line(rl_line_buffer, 0);
            rl_redisplay();
        }
    } else {
        tab_press_count = 0;
        last_multiple_matches = false;
    }
    return matches;
}
std::vector<std::string> split(const std::string &input) {
    std::vector<std::string> tokens;
    std::string token;
    enum State { Unquoted, InSingleQuote, InDoubleQuote } state = Unquoted;
    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (state == Unquoted) {
            if (c == '\'') {
                state = InSingleQuote;
            } else if (c == '"') {
                state = InDoubleQuote;
            } else if (std::isspace(c)) {
                if (!token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                }
            } else if (c == '\\') {
                if (i + 1 < input.size()) {
                    token += input[i + 1];
                    ++i;
                }
            } else {
                token += c;
            }
        } else if (state == InSingleQuote) {
            if (c == '\'') {
                state = Unquoted;
            } else {
                token += c;
            }
        } else if (state == InDoubleQuote) {
            if (c == '\\') {
                if (i + 1 < input.size()) {
                    char next = input[i + 1];
                    if (next == '\\' || next == '"' || next == '$' ||
                        next == '\n') {
                        token += next;
                        ++i;
                    } else {
                        token += c;
                    }
                } else {
                    token += c;
                }
            } else if (c == '"') {
                state = Unquoted;
            } else {
                token += c;
            }
        }
    }
    if (!token.empty()) {
        tokens.push_back(token);
    }
    return tokens;
}
std::string find_executable(const std::string &command) {
    const char *path_env = std::getenv("PATH");
    if (!path_env)
        return "";
    std::istringstream iss(path_env);
    std::string dir;
    while (std::getline(iss, dir, ':')) {
        std::filesystem::path full_path = std::filesystem::path(dir) / command;
        if (std::filesystem::exists(full_path) &&
            access(full_path.c_str(), X_OK) == 0)
            return full_path.string();
    }
    return "";
}
bool run_pwd() {
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
        std::cout << cwd << std::endl;
        return true;
    } else {
        perror("pwd");
        return false;
    }
}
bool run_cd(const std::string &path) {
    if (path.empty()) {
        std::cerr << "cd: " << path << ": No such file or directory"
                  << std::endl;
        return false;
    }
    std::string clean_path = path;
    if (clean_path[0] == '~') {
        const char *home = std::getenv("HOME");
        if (!home) {
            std::cerr << "cd: HOME not set" << std::endl;
            return false;
        }
        clean_path = (clean_path == "~")
                         ? home
                         : std::string(home) + clean_path.substr(1);
    }
    std::filesystem::path abs_path = std::filesystem::absolute(clean_path);
    if (!std::filesystem::exists(abs_path) ||
        !std::filesystem::is_directory(abs_path)) {
        std::cerr << "cd: " << path << ": No such file or directory"
                  << std::endl;
        return false;
    }
    return chdir(abs_path.c_str()) == 0;
}
bool handle_builtin(const std::string &input) {
    std::vector<std::string> args = split(input);
    if (args.empty())
        return false;
    if (args[0] == "echo") {
        std::vector<std::string> echo_args;
        std::string stdout_file, stderr_file;
        bool redirect_stdout = false, redirect_stderr = false,
             stdout_append = false, stderr_append = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if ((args[i] == ">" || args[i] == "1>") && i + 1 < args.size()) {
                redirect_stdout = true;
                stdout_file = args[i + 1];
                stdout_append = false;
                ++i;
            } else if ((args[i] == ">>" || args[i] == "1>>") &&
                       i + 1 < args.size()) {
                redirect_stdout = true;
                stdout_file = args[i + 1];
                stdout_append = true;
                ++i;
            } else if (args[i] == "2>" && i + 1 < args.size()) {
                redirect_stderr = true;
                stderr_file = args[i + 1];
                stderr_append = false;
                ++i;
            } else if (args[i] == "2>>" && i + 1 < args.size()) {
                redirect_stderr = true;
                stderr_file = args[i + 1];
                stderr_append = true;
                ++i;
            } else {
                echo_args.push_back(args[i]);
            }
        }
        int saved_stdout = -1, saved_stderr = -1;
        int fd_out = -1, fd_err = -1;
        if (redirect_stdout) {
            fd_out =
                open(stdout_file.c_str(),
                     O_CREAT | O_WRONLY | (stdout_append ? O_APPEND : O_TRUNC),
                     0644);
            if (fd_out < 0) {
                perror("open stdout");
                return true;
            }
            saved_stdout = dup(STDOUT_FILENO);
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }
        if (redirect_stderr) {
            fd_err =
                open(stderr_file.c_str(),
                     O_CREAT | O_WRONLY | (stderr_append ? O_APPEND : O_TRUNC),
                     0644);
            if (fd_err < 0) {
                perror("open stderr");
                return true;
            }
            saved_stderr = dup(STDERR_FILENO);
            dup2(fd_err, STDERR_FILENO);
            close(fd_err);
        }
        // Actual echo output to stdout
        for (size_t i = 0; i < echo_args.size(); ++i) {
            std::cout << echo_args[i];
            if (i + 1 < echo_args.size())
                std::cout << " ";
        }
        std::cout << std::endl;
        if (redirect_stdout) {
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdout);
        }
        if (redirect_stderr) {
            dup2(saved_stderr, STDERR_FILENO);
            close(saved_stderr);
        }
        return true;
    }
    if (input == "exit 0") {
        std::exit(0);
    }
    if (args[0] == "type" && args.size() == 2) {
        const std::string &arg = args[1];
        if (arg == "echo" || arg == "exit" || arg == "type" || arg == "pwd" ||
            arg == "cd" || arg == "history") {
            std::cout << arg << " is a shell builtin" << std::endl;
        } else {
            std::string path = find_executable(arg);
            if (!path.empty()) {
                std::cout << arg << " is " << path << std::endl;
            } else {
                std::cout << arg << ": not found" << std::endl;
            }
        }
        return true;
    }
    if (args[0] == "pwd") {
        return run_pwd();
    }
    if (args[0] == "cd") {
        if (args.size() == 1) {
            const char *home = std::getenv("HOME");
            if (!home) {
                std::cerr << "cd: HOME not set" << std::endl;
                return true;
            }
            run_cd(home);
        } else if (args.size() == 2) {
            run_cd(args[1]);
        } else {
            std::cerr << "cd: too many arguments" << std::endl;
        }
        return true;
    }
    if (args[0] == "history") {
        HIST_ENTRY **the_list = history_list();
        if (!the_list) return true;
        int total = 0;
        while (the_list[total]) total++;
        int limit = total;
        if (args.size() == 2) {
            try {
                int n = std::stoi(args[1]);
                if (n >= 0 && n <= total) {
                    limit = n;
                }
            } catch (...) {
                // if stoi fail, print full hist.
            }
        }
        for (int i = total - limit; i < total; ++i) {
            std::cout << i + history_base << "  " << the_list[i]->line
                      << std::endl;
        }
        
        return true;
    }
    return false;
}
void execute_command(const std::vector<std::string> &args,
                     const std::string &path) {
    pid_t pid = fork();
    if (pid == 0) {
        int stdout_fd = -1, stderr_fd = -1;
        std::vector<char *> exec_args;
        for (size_t i = 0; i < args.size(); ++i) {
            if ((args[i] == ">" || args[i] == "1>") && i + 1 < args.size()) {
                stdout_fd = open(args[i + 1].c_str(),
                                 O_CREAT | O_WRONLY | O_TRUNC, 0644);
                ++i;
            } else if ((args[i] == ">>" || args[i] == "1>>") &&
                       i + 1 < args.size()) {
                stdout_fd = open(args[i + 1].c_str(),
                                 O_CREAT | O_WRONLY | O_APPEND, 0644);
                ++i;
            } else if ((args[i] == "2>" && i + 1 < args.size())) {
                stderr_fd = open(args[i + 1].c_str(),
                                 O_CREAT | O_WRONLY | O_TRUNC, 0644);
                ++i;
            } else if ((args[i] == "2>>" && i + 1 < args.size())) {
                stderr_fd = open(args[i + 1].c_str(),
                                 O_CREAT | O_WRONLY | O_APPEND, 0644);
                ++i;
            } else {
                exec_args.push_back(const_cast<char *>(args[i].c_str()));
            }
        }
        if (stdout_fd >= 0) {
            dup2(stdout_fd, STDOUT_FILENO);
            close(stdout_fd);
        }
        if (stderr_fd >= 0) {
            dup2(stderr_fd, STDERR_FILENO);
            close(stderr_fd);
        }
        exec_args.push_back(nullptr);
        execv(path.c_str(), exec_args.data());
        perror("execv");
        std::exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    } else {
        perror("fork");
    }
}
std::vector<std::string> split_pipeline(const std::string &input) {
    std::vector<std::string> parts;
    std::string token;
    bool in_single_quote = false, in_double_quote = false;
    for (char c : input) {
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        } else if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        } else if (c == '|' && !in_single_quote && !in_double_quote) {
            parts.push_back(token);
            token.clear();
            continue;
        }
        token += c;
    }
    if (!token.empty())
        parts.push_back(token);
    return parts;
}
int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    rl_attempted_completion_function = custom_completion;
    char *buf;
    std::string cmd;
    while ((buf = readline("$ ")) != nullptr) {
        cmd = std::string(buf);
        free(buf);
        if (cmd.empty())
            continue;
        add_history(cmd.c_str());
        std::vector<std::string> pipeline_parts = split_pipeline(cmd);
        if (pipeline_parts.size() >= 2) {
            size_t n = pipeline_parts.size();
            std::vector<int> pipes(2 * (n - 1)); // each pipe has 2 fds
            for (size_t i = 0; i < n - 1; ++i) {
                if (pipe(&pipes[2 * i]) < 0) {
                    perror("pipe");
                    exit(EXIT_FAILURE);
                }
            }
            std::vector<pid_t> pids;
            for (size_t i = 0; i < n; ++i) {
                pid_t pid = fork();
                if (pid < 0) {
                    perror("fork");
                    exit(EXIT_FAILURE);
                } else if (pid == 0) {
                    // Setup input pipe
                    if (i > 0) {
                        dup2(pipes[2 * (i - 1)], STDIN_FILENO);
                    }
                    // Setup output pipe
                    if (i < n - 1) {
                        dup2(pipes[2 * i + 1], STDOUT_FILENO);
                    }
                    // Close all pipes in child
                    for (size_t j = 0; j < 2 * (n - 1); ++j) {
                        close(pipes[j]);
                    }
                    std::string part = pipeline_parts[i];
                    if (!handle_builtin(part)) {
                        std::vector<std::string> args = split(part);
                        if (args.empty())
                            exit(1);
                        std::string path = find_executable(args[0]);
                        if (path.empty()) {
                            std::cerr << args[0] << ": command not found"
                                      << std::endl;
                            exit(1);
                        }
                        std::vector<char *> c_args;
                        for (auto &arg : args) {
                            c_args.push_back(const_cast<char *>(arg.c_str()));
                        }
                        c_args.push_back(nullptr);
                        execv(path.c_str(), c_args.data());
                        perror("execv");
                        exit(1);
                    }
                    exit(0);
                }
                pids.push_back(pid);
            }
            for (size_t i = 0; i < 2 * (n - 1); ++i) {
                close(pipes[i]);
            }
            for (pid_t pid : pids) {
                waitpid(pid, nullptr, 0);
            }
            continue;
        }
        if (handle_builtin(cmd)) {
            continue;
        }
        std::vector<std::string> args = split(cmd);
        if (args.empty())
            continue;
        std::string path = find_executable(args[0]);
        if (path.empty()) {
            std::cerr << args[0] << ": command not found" << std::endl;
            continue;
        }
        execute_command(args, path);
    }
    std::cout << std::endl;
    return EXIT_SUCCESS;
}