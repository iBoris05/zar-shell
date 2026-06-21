// ============================================================================
//  ZAR-SHELL  —  El shell que Windows no merece y tú sí
//  Compilar:  g++ -std=c++17 -O2 -o zarshell.exe zar_shell.cpp
//             cl /std:c++17 /O2 zar_shell.cpp /Fe:zarshell.exe
//  Autor:     ZAR-NEUTRON KERNEL INTERFACE PROJECT
//  Nota:      CMD.exe puede irse a la mierda.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace std;

// ============================================================================
//  CONSTANTES Y COLORES ANSI
// ============================================================================

#define RESET       "\x1B[0m"
#define BOLD        "\x1B[1m"
#define DIM         "\x1B[2m"
#define RED         "\x1B[38;5;196m"
#define GREEN       "\x1B[38;5;82m"
#define YELLOW      "\x1B[38;5;226m"
#define CYAN        "\x1B[38;5;45m"
#define MAGENTA     "\x1B[38;5;177m"
#define ORANGE      "\x1B[38;5;208m"
#define BLUE        "\x1B[38;5;69m"
#define GRAY        "\x1B[38;5;245m"
#define WHITE       "\x1B[38;5;255m"
#define BG_RED      "\x1B[41m"

static const int MAX_HISTORY    = 500;
static const int MAX_PATH_DISP  = 50;

// ============================================================================
//  ESTADO GLOBAL DEL SHELL
// ============================================================================

struct BackgroundJob {
    DWORD   pid;
    HANDLE  hProcess;
    string  command;
};

struct ShellState {
    // Historial
    vector<string>        history;
    int                   historyIndex = -1;
    string                historyFile;

    // Alias
    map<string, string>   aliases;

    // Variables de sesión (distintas de las de entorno)
    map<string, string>   vars;

    // Prompt
    string                hostname;
    string                username;

    // Jobs en background
    vector<BackgroundJob> jobs;
    DWORD                 nextJobId = 1;

    // Estado del último comando
    int                   lastExitCode = 0;
    DWORD                 lastElapsedMs = 0;

    // Ruta previa para `cd -`
    string                prevDir;

    // Es administrador?
    bool                  isAdmin = false;

    // Shell sigue corriendo?
    bool                  running = true;
};

static ShellState shell;

// ============================================================================
//  UTILIDADES DE CONSOLA
// ============================================================================

static HANDLE hStdOut;
static HANDLE hStdIn;

void InitConsole() {
    hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    hStdIn  = GetStdHandle(STD_INPUT_HANDLE);

    // Habilitar secuencias ANSI/VT100
    DWORD outMode = 0;
    GetConsoleMode(hStdOut, &outMode);
    SetConsoleMode(hStdOut, outMode
        | ENABLE_VIRTUAL_TERMINAL_PROCESSING
        | DISABLE_NEWLINE_AUTO_RETURN);

    // Habilitar modo de entrada extendido para capturar teclas especiales
    DWORD inMode = 0;
    GetConsoleMode(hStdIn, &inMode);
    SetConsoleMode(hStdIn, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT
        | ENABLE_EXTENDED_FLAGS);

    // UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Título de la ventana
    SetConsoleTitleA("ZAR-SHELL [Kernel Interface]");
}

void ClearScreen() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hStdOut, &csbi);
    DWORD cellCount = csbi.dwSize.X * csbi.dwSize.Y;
    DWORD count;
    COORD homeCoords = {0, 0};
    FillConsoleOutputCharacterA(hStdOut, ' ', cellCount, homeCoords, &count);
    FillConsoleOutputAttribute(hStdOut, csbi.wAttributes, cellCount, homeCoords, &count);
    SetConsoleCursorPosition(hStdOut, homeCoords);
}

// Mover cursor N posiciones a la izquierda en la misma línea
void MoveCursorLeft(int n) {
    if (n > 0) printf("\x1B[%dD", n);
}

// Borrar desde cursor hasta fin de línea
void ClearToEOL() {
    printf("\x1B[K");
}

// ============================================================================
//  ESCALADA DE PRIVILEGIOS
// ============================================================================

static void ElevatePrivilege(LPCSTR privilegeName) {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &hToken)) return;

    LUID luid;
    if (LookupPrivilegeValueA(NULL, privilegeName, &luid)) {
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount           = 1;
        tp.Privileges[0].Luid       = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
    }
    CloseHandle(hToken);
}

bool CheckIsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    AllocateAndInitializeSid(&ntAuth, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &adminGroup);
    CheckTokenMembership(NULL, adminGroup, &isAdmin);
    FreeSid(adminGroup);
    return isAdmin == TRUE;
}

void ElevarPrivilegios() {
    ElevatePrivilege(SE_DEBUG_NAME);
    ElevatePrivilege(SE_BACKUP_NAME);
    ElevatePrivilege(SE_RESTORE_NAME);
    ElevatePrivilege(SE_TAKE_OWNERSHIP_NAME);
    shell.isAdmin = CheckIsAdmin();
}

// ============================================================================
//  HISTORIAL PERSISTENTE
// ============================================================================

string GetAppDataPath() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        return string(path) + "\\ZarShell";
    }
    return ".";
}

void LoadHistory() {
    shell.historyFile = GetAppDataPath() + "\\history.txt";
    CreateDirectoryA(GetAppDataPath().c_str(), NULL);

    ifstream f(shell.historyFile);
    string line;
    while (getline(f, line)) {
        if (!line.empty()) shell.history.push_back(line);
    }
    // Limitar al máximo
    if ((int)shell.history.size() > MAX_HISTORY) {
        shell.history.erase(shell.history.begin(),
            shell.history.begin() + (shell.history.size() - MAX_HISTORY));
    }
}

void SaveHistory() {
    ofstream f(shell.historyFile, ios::trunc);
    for (auto& h : shell.history) f << h << "\n";
}

void AddHistory(const string& cmd) {
    if (cmd.empty()) return;
    // No duplicar el último
    if (!shell.history.empty() && shell.history.back() == cmd) return;
    shell.history.push_back(cmd);
    if ((int)shell.history.size() > MAX_HISTORY)
        shell.history.erase(shell.history.begin());
    shell.historyIndex = -1;
}

// ============================================================================
//  INFORMACIÓN DEL SISTEMA PARA EL PROMPT
// ============================================================================

void InitHostInfo() {
    char buf[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD sz = sizeof(buf);
    GetComputerNameA(buf, &sz);
    shell.hostname = buf;

    char user[256];
    DWORD usz = sizeof(user);
    GetUserNameA(user, &usz);
    shell.username = user;
}

// Obtener rama git del directorio actual (rápido, sin bloquear)
string GetGitBranch() {
    // Buscar .git/HEAD subiendo desde el CWD
    try {
        fs::path p = fs::current_path();
        while (true) {
            fs::path gitHead = p / ".git" / "HEAD";
            if (fs::exists(gitHead)) {
                ifstream f(gitHead);
                string line;
                if (getline(f, line)) {
                    const string prefix = "ref: refs/heads/";
                    if (line.rfind(prefix, 0) == 0)
                        return line.substr(prefix.size());
                    if (line.size() >= 7)
                        return line.substr(0, 7); // detached HEAD
                }
                return "git";
            }
            if (p == p.parent_path()) break;
            p = p.parent_path();
        }
    } catch (...) {}
    return "";
}

// Acortar la ruta para mostrarla en el prompt
string ShortenPath(const string& fullPath) {
    // Reemplazar el home (si aplica) con ~
    string p = fullPath;
    // Normalizar separadores
    replace(p.begin(), p.end(), '\\', '/');

    if ((int)p.size() <= MAX_PATH_DISP) return p;

    // Mostrar solo los últimos dos componentes
    size_t pos = p.rfind('/');
    if (pos != string::npos && pos > 0) {
        size_t pos2 = p.rfind('/', pos - 1);
        if (pos2 != string::npos)
            return "…/" + p.substr(pos2 + 1);
    }
    return "…" + p.substr(p.size() - MAX_PATH_DISP);
}

// Formato de tiempo transcurrido
string FormatElapsed(DWORD ms) {
    if (ms < 1000) return "";
    if (ms < 60000) return to_string(ms / 1000) + "s";
    return to_string(ms / 60000) + "m" + to_string((ms % 60000) / 1000) + "s";
}

// Hora actual HH:MM
string GetShellCurrentTime() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", st.wHour, st.wMinute);
    return buf;
}

// ============================================================================
//  RENDERIZAR PROMPT
// ============================================================================

string RenderPrompt() {
    char cwd[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, cwd);
    string shortCwd = ShortenPath(cwd);
    string branch   = GetGitBranch();
    string elapsed  = FormatElapsed(shell.lastElapsedMs);

    ostringstream ss;

    // ── Línea superior ────────────────────────────────────────────────────
    ss << "\n" << BOLD << CYAN << "╭─ " << RESET;

    // Indicador admin
    if (shell.isAdmin)
        ss << BG_RED << WHITE << BOLD << " ⚡ADMIN " << RESET << " ";

    // Identificador fijo del shell
    ss << BOLD << MAGENTA << "ZAR" << RESET
       << GRAY << "-" << RESET
       << BOLD << CYAN << "NEUTRON" << RESET;

    // CWD
    ss << "  " << BOLD << GREEN << shortCwd << RESET;

    // Rama git
    if (!branch.empty())
        ss << "  " << MAGENTA << "[" << branch << "]" << RESET;

    // Exit code del último comando
    if (shell.lastExitCode == 0) {
        ss << "  " << GREEN << "✓" << RESET;
    } else {
        ss << "  " << RED << "✗" << shell.lastExitCode << RESET;
    }

    // Tiempo de ejecución si fue notable
    if (!elapsed.empty())
        ss << "  " << GRAY << "⏱ " << elapsed << RESET;

    // Hora
    ss << "  " << DIM << GRAY << GetShellCurrentTime() << RESET;

    // ── Línea inferior ────────────────────────────────────────────────────
    ss << "\n" << BOLD << CYAN << "╰─ " << RESET;
    ss << BOLD << YELLOW << "# " << RESET;

    return ss.str();
}

// ============================================================================
//  LECTURA DE LÍNEA CON HISTORIAL (↑↓) SIN READLINE
// ============================================================================

string ReadLineWithHistory() {
    string line;
    int    cursorPos = 0;
    int    histIdx   = (int)shell.history.size(); // apunta DESPUÉS del último

    // Flush
    fflush(stdout);

    while (true) {
        INPUT_RECORD ir;
        DWORD        read;
        if (!ReadConsoleInputA(hStdIn, &ir, 1, &read)) break;
        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;

        WORD vk   = ir.Event.KeyEvent.wVirtualKeyCode;
        char ch   = ir.Event.KeyEvent.uChar.AsciiChar;
        DWORD ctrl = ir.Event.KeyEvent.dwControlKeyState;
        (void)ctrl;

        if (vk == VK_RETURN) {
            printf("\n");
            break;
        }

        if (vk == VK_ESCAPE) {
            // Borrar línea actual
            MoveCursorLeft(cursorPos);
            ClearToEOL();
            line.clear();
            cursorPos = 0;
            histIdx = (int)shell.history.size();
            continue;
        }

        if (vk == VK_BACK) {
            if (cursorPos > 0) {
                line.erase(cursorPos - 1, 1);
                cursorPos--;
                MoveCursorLeft(1);
                printf("%s ", line.c_str() + cursorPos);
                MoveCursorLeft((int)line.size() - cursorPos + 1);
            }
            continue;
        }

        if (vk == VK_DELETE) {
            if (cursorPos < (int)line.size()) {
                line.erase(cursorPos, 1);
                printf("%s ", line.c_str() + cursorPos);
                MoveCursorLeft((int)line.size() - cursorPos + 1);
            }
            continue;
        }

        if (vk == VK_LEFT) {
            if (cursorPos > 0) { cursorPos--; MoveCursorLeft(1); }
            continue;
        }

        if (vk == VK_RIGHT) {
            if (cursorPos < (int)line.size()) {
                printf("%c", line[cursorPos]);
                cursorPos++;
            }
            continue;
        }

        if (vk == VK_HOME) {
            if (cursorPos > 0) { MoveCursorLeft(cursorPos); cursorPos = 0; }
            continue;
        }

        if (vk == VK_END) {
            int rem = (int)line.size() - cursorPos;
            if (rem > 0) { printf("%s", line.c_str() + cursorPos); cursorPos = (int)line.size(); }
            continue;
        }

        // Historial ↑
        if (vk == VK_UP) {
            if (!shell.history.empty() && histIdx > 0) {
                histIdx--;
                MoveCursorLeft(cursorPos);
                ClearToEOL();
                line = shell.history[histIdx];
                cursorPos = (int)line.size();
                printf("%s", line.c_str());
            }
            continue;
        }

        // Historial ↓
        if (vk == VK_DOWN) {
            if (histIdx < (int)shell.history.size()) {
                histIdx++;
                MoveCursorLeft(cursorPos);
                ClearToEOL();
                if (histIdx == (int)shell.history.size()) {
                    line.clear();
                    cursorPos = 0;
                } else {
                    line = shell.history[histIdx];
                    cursorPos = (int)line.size();
                    printf("%s", line.c_str());
                }
            }
            continue;
        }

        // Ctrl+C
        if (vk == 'C' && (ctrl & LEFT_CTRL_PRESSED)) {
            printf("^C\n");
            line.clear();
            cursorPos = 0;
            break;
        }

        // Ctrl+L — limpiar pantalla
        if (vk == 'L' && (ctrl & LEFT_CTRL_PRESSED)) {
            ClearScreen();
            printf("%s", RenderPrompt().c_str());
            printf("%s", line.c_str());
            cursorPos = (int)line.size();
            continue;
        }

        // Carácter imprimible
        if (ch >= 0x20 && ch != 0x7F) {
            line.insert(cursorPos, 1, ch);
            cursorPos++;
            // Redibujar desde la posición actual
            printf("%s", line.c_str() + cursorPos - 1);
            int trail = (int)line.size() - cursorPos;
            if (trail > 0) MoveCursorLeft(trail);
        }
    }

    return line;
}

// ============================================================================
//  TOKENIZADOR / PARSER
// ============================================================================

struct Token {
    string value;
    bool   isQuoted = false; // Estaba entre comillas
};

// Tokenizar respetando comillas simples y dobles
vector<Token> Tokenize(const string& s) {
    vector<Token> tokens;
    int i = 0;
    int n = (int)s.size();

    while (i < n) {
        // Saltar espacios
        while (i < n && isspace((unsigned char)s[i])) i++;
        if (i >= n) break;

        Token tok;
        char  delim = 0;

        if (s[i] == '"' || s[i] == '\'') {
            delim = s[i++];
            tok.isQuoted = true;
            while (i < n && s[i] != delim) {
                if (s[i] == '\\' && delim == '"' && i + 1 < n) {
                    i++;
                    switch (s[i]) {
                        case 'n': tok.value += '\n'; break;
                        case 't': tok.value += '\t'; break;
                        default:  tok.value += s[i]; break;
                    }
                } else {
                    tok.value += s[i];
                }
                i++;
            }
            if (i < n) i++; // Cerrar comilla
        } else if (s[i] == '|' || s[i] == '>' || s[i] == '<' || s[i] == '&') {
            // Operadores de control
            tok.value += s[i++];
            if (i < n && s[i] == '>' && tok.value == ">") {
                tok.value += s[i++]; // >>
            }
        } else {
            while (i < n && !isspace((unsigned char)s[i])
                   && s[i] != '|' && s[i] != '>' && s[i] != '<' && s[i] != '&') {
                if (s[i] == '\\' && i + 1 < n) {
                    i++;
                    tok.value += s[i];
                } else {
                    tok.value += s[i];
                }
                i++;
            }
        }

        if (!tok.value.empty())
            tokens.push_back(tok);
    }
    return tokens;
}

// Estructura de un comando simple
struct SimpleCmd {
    vector<string> args;
    string         stdinFile;
    string         stdoutFile;
    bool           appendStdout = false;
    bool           background   = false;
};

// Estructura de un pipeline completo: cmd1 | cmd2 | cmd3
struct Pipeline {
    vector<SimpleCmd> cmds;
    bool              background = false;
};

// Expandir variables $VAR, $?, $$
string ExpandVars(const string& s) {
    string result;
    int n = (int)s.size();
    for (int i = 0; i < n; i++) {
        if (s[i] == '$' && i + 1 < n) {
            i++;
            if (s[i] == '?') {
                result += to_string(shell.lastExitCode);
            } else if (s[i] == '$') {
                result += to_string(GetCurrentProcessId());
            } else if (s[i] == '0') {
                result += "zarshell";
            } else if (s[i] == '{') {
                // ${VAR}
                i++;
                string varName;
                while (i < n && s[i] != '}') varName += s[i++];
                // Buscar en vars de sesión primero, luego entorno
                if (shell.vars.count(varName))
                    result += shell.vars[varName];
                else {
                    char* ev = getenv(varName.c_str());
                    if (ev) result += ev;
                }
            } else if (isalpha((unsigned char)s[i]) || s[i] == '_') {
                string varName;
                while (i < n && (isalnum((unsigned char)s[i]) || s[i] == '_'))
                    varName += s[i++];
                i--;
                if (shell.vars.count(varName))
                    result += shell.vars[varName];
                else {
                    char* ev = getenv(varName.c_str());
                    if (ev) result += ev;
                }
            } else {
                result += '$';
                result += s[i];
            }
        } else {
            result += s[i];
        }
    }
    return result;
}

// Expandir alias (solo en el primer token)
string ExpandAlias(const string& cmd) {
    auto it = shell.aliases.find(cmd);
    if (it != shell.aliases.end()) return it->second;
    return cmd;
}

// Parsear tokens en pipeline
Pipeline ParsePipeline(const string& rawInput) {
    Pipeline pl;

    // Expandir el primer token como alias
    string input = rawInput;

    auto tokens = Tokenize(input);
    if (tokens.empty()) return pl;

    SimpleCmd current;
    bool       parsingArgs = true;

    for (int i = 0; i < (int)tokens.size(); i++) {
        const string& tv = tokens[i].value;

        if (tv == "|") {
            pl.cmds.push_back(current);
            current = SimpleCmd();
            parsingArgs = true;
            continue;
        }
        if (tv == "&") {
            pl.background = true;
            continue;
        }
        if (tv == ">" || tv == ">>") {
            if (i + 1 < (int)tokens.size()) {
                current.stdoutFile    = ExpandVars(tokens[++i].value);
                current.appendStdout  = (tv == ">>");
            }
            continue;
        }
        if (tv == "<") {
            if (i + 1 < (int)tokens.size())
                current.stdinFile = ExpandVars(tokens[++i].value);
            continue;
        }

        // Primer argumento: expandir alias
        string expanded = (current.args.empty() && parsingArgs)
            ? ExpandAlias(tokens[i].value)
            : tokens[i].value;

        // Si el alias se expandió a múltiples palabras, re-parsear
        if (current.args.empty() && expanded != tokens[i].value) {
            auto aliasTokens = Tokenize(expanded);
            for (auto& at : aliasTokens)
                current.args.push_back(ExpandVars(at.value));
        } else {
            current.args.push_back(ExpandVars(tokens[i].value));
        }
    }

    if (!current.args.empty())
        pl.cmds.push_back(current);

    return pl;
}

// ============================================================================
//  BÚSQUEDA EN PATH
// ============================================================================

string FindInPath(const string& name) {
    // Si ya es una ruta absoluta/relativa
    if (name.find('/') != string::npos || name.find('\\') != string::npos) {
        if (fs::exists(name)) return name;
        return "";
    }

    // Lista de extensiones a probar
    vector<string> exts = {"", ".exe", ".cmd", ".bat", ".com"};

    // Directorio actual primero
    for (auto& ext : exts) {
        fs::path p = fs::current_path() / (name + ext);
        if (fs::exists(p)) return p.string();
    }

    // Recorrer PATH
    char* pathEnv = getenv("PATH");
    if (!pathEnv) return "";
    string pathStr(pathEnv);

    istringstream ss(pathStr);
    string dir;
    while (getline(ss, dir, ';')) {
        if (dir.empty()) continue;
        for (auto& ext : exts) {
            fs::path p = fs::path(dir) / (name + ext);
            if (fs::exists(p)) return p.string();
        }
    }
    return "";
}

// ============================================================================
//  BUILTINS
// ============================================================================

// Devuelve true si el comando fue manejado como builtin
bool ExecuteBuiltin(const SimpleCmd& cmd);

// ── cd ────────────────────────────────────────────────────────────────────────
bool Builtin_cd(const vector<string>& args) {
    string target;

    if (args.size() < 2) {
        // Sin argumento: ir a USERPROFILE
        char* home = getenv("USERPROFILE");
        if (!home) home = getenv("HOME");
        target = home ? home : "C:\\";
    } else if (args[1] == "-") {
        // cd - : volver al directorio anterior
        if (shell.prevDir.empty()) {
            fprintf(stderr, "%s[zar] No hay directorio previo.%s\n", RED, RESET);
            return true;
        }
        target = shell.prevDir;
    } else {
        target = args[1];
    }

    char cwd[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, cwd);
    shell.prevDir = cwd;

    if (!SetCurrentDirectoryA(target.c_str())) {
        fprintf(stderr, "%s[zar] cd: '%s': No existe o acceso denegado.%s\n",
                RED, target.c_str(), RESET);
    }
    return true;
}

// ── pwd ───────────────────────────────────────────────────────────────────────
bool Builtin_pwd(const vector<string>&) {
    char cwd[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, cwd);
    printf("%s%s%s\n", GREEN, cwd, RESET);
    return true;
}

// ── echo ──────────────────────────────────────────────────────────────────────
bool Builtin_echo(const vector<string>& args) {
    for (int i = 1; i < (int)args.size(); i++) {
        if (i > 1) printf(" ");
        printf("%s", args[i].c_str());
    }
    printf("\n");
    return true;
}

// ── set / export ──────────────────────────────────────────────────────────────
bool Builtin_set(const vector<string>& args, bool exportEnv) {
    if (args.size() < 2) {
        // Mostrar todas las variables
        for (auto& [k, v] : shell.vars)
            printf("%s%s%s=%s%s%s\n", CYAN, k.c_str(), RESET, YELLOW, v.c_str(), RESET);
        return true;
    }
    // set VAR=value o set VAR value
    string combined;
    for (int i = 1; i < (int)args.size(); i++) {
        if (i > 1) combined += " ";
        combined += args[i];
    }
    size_t eq = combined.find('=');
    string varName, varVal;
    if (eq != string::npos) {
        varName = combined.substr(0, eq);
        varVal  = combined.substr(eq + 1);
    } else {
        varName = combined;
    }
    shell.vars[varName] = varVal;
    if (exportEnv) SetEnvironmentVariableA(varName.c_str(), varVal.c_str());
    return true;
}

// ── unset ─────────────────────────────────────────────────────────────────────
bool Builtin_unset(const vector<string>& args) {
    for (int i = 1; i < (int)args.size(); i++) {
        shell.vars.erase(args[i]);
        SetEnvironmentVariableA(args[i].c_str(), NULL);
    }
    return true;
}

// ── env ───────────────────────────────────────────────────────────────────────
bool Builtin_env(const vector<string>&) {
    // Listar variables de entorno del sistema
    LPCH env = GetEnvironmentStringsA();
    for (LPCH p = env; *p; p += strlen(p) + 1) {
        printf("%s\n", p);
    }
    FreeEnvironmentStringsA(env);
    return true;
}

// ── alias ─────────────────────────────────────────────────────────────────────
bool Builtin_alias(const vector<string>& args) {
    if (args.size() < 2) {
        for (auto& [k, v] : shell.aliases)
            printf("%salias %s%s=%s'%s'%s\n", DIM, CYAN, k.c_str(), YELLOW, v.c_str(), RESET);
        return true;
    }
    string combined;
    for (int i = 1; i < (int)args.size(); i++) {
        if (i > 1) combined += " ";
        combined += args[i];
    }
    size_t eq = combined.find('=');
    if (eq == string::npos) {
        auto it = shell.aliases.find(combined);
        if (it != shell.aliases.end())
            printf("alias %s='%s'\n", it->first.c_str(), it->second.c_str());
        return true;
    }
    shell.aliases[combined.substr(0, eq)] = combined.substr(eq + 1);
    return true;
}

// ── unalias ───────────────────────────────────────────────────────────────────
bool Builtin_unalias(const vector<string>& args) {
    for (int i = 1; i < (int)args.size(); i++) shell.aliases.erase(args[i]);
    return true;
}

// ── history ───────────────────────────────────────────────────────────────────
bool Builtin_history(const vector<string>& args) {
    int n = (int)shell.history.size();
    int show = 20;
    if (args.size() >= 2) show = stoi(args[1]);
    int start = max(0, n - show);
    for (int i = start; i < n; i++)
        printf("%s%4d%s  %s\n", GRAY, i + 1, RESET, shell.history[i].c_str());
    return true;
}

// ── which ─────────────────────────────────────────────────────────────────────
bool Builtin_which(const vector<string>& args) {
    for (int i = 1; i < (int)args.size(); i++) {
        string found = FindInPath(args[i]);
        if (!found.empty())
            printf("%s%s%s\n", GREEN, found.c_str(), RESET);
        else
            printf("%s%s: no encontrado en PATH%s\n", RED, args[i].c_str(), RESET);
    }
    return true;
}

// ── jobs ──────────────────────────────────────────────────────────────────────
bool Builtin_jobs(const vector<string>&) {
    // Primero limpiar los que ya terminaron
    vector<BackgroundJob> alive;
    for (auto& job : shell.jobs) {
        DWORD code;
        if (GetExitCodeProcess(job.hProcess, &code) && code == STILL_ACTIVE) {
            alive.push_back(job);
        } else {
            CloseHandle(job.hProcess);
        }
    }
    shell.jobs = alive;

    if (shell.jobs.empty()) {
        printf("%sNo hay jobs en background.%s\n", GRAY, RESET);
        return true;
    }
    for (int i = 0; i < (int)shell.jobs.size(); i++) {
        printf("%s[%d]%s PID %s%lu%s  %s\n",
               CYAN, i + 1, RESET,
               YELLOW, shell.jobs[i].pid, RESET,
               shell.jobs[i].command.c_str());
    }
    return true;
}

// ── kill ──────────────────────────────────────────────────────────────────────
bool Builtin_kill(const vector<string>& args) {
    if (args.size() < 2) {
        fprintf(stderr, "%s[zar] kill: necesita un PID%s\n", RED, RESET);
        return true;
    }
    DWORD pid = (DWORD)stoul(args[1]);
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (h) {
        TerminateProcess(h, 1);
        CloseHandle(h);
        printf("%s[✓] Proceso %lu terminado.%s\n", GREEN, pid, RESET);
    } else {
        fprintf(stderr, "%s[zar] kill: no se pudo matar PID %lu (error %lu)%s\n",
                RED, pid, GetLastError(), RESET);
    }
    return true;
}

// ── whoami ────────────────────────────────────────────────────────────────────
bool Builtin_whoami(const vector<string>&) {
    printf("%s%s%s@%s%s%s\n",
           MAGENTA, shell.username.c_str(), RESET,
           CYAN,    shell.hostname.c_str(), RESET);
    if (shell.isAdmin)
        printf("%s[⚡] Corriendo como ADMINISTRADOR — con gran poder...%s\n", YELLOW, RESET);
    else
        printf("%s[·] Usuario estándar (sin privilegios de admin)%s\n", GRAY, RESET);
    return true;
}

// ── help ──────────────────────────────────────────────────────────────────────
bool Builtin_help(const vector<string>&) {
    printf("\n");
    printf("%s%s╔══════════════════════════════════════════════════╗%s\n", BOLD, CYAN, RESET);
    printf("%s%s║         ZAR-SHELL — COMANDOS INTEGRADOS          ║%s\n", BOLD, CYAN, RESET);
    printf("%s%s╚══════════════════════════════════════════════════╝%s\n", BOLD, CYAN, RESET);
    printf("\n");

    auto H = [](const char* cmd, const char* desc) {
        printf("  %s%-20s%s %s%s%s\n", YELLOW, cmd, RESET, GRAY, desc, RESET);
    };

    printf("%s  Navegación:%s\n", BOLD, RESET);
    H("cd [dir]",         "Cambiar directorio (cd - para volver al anterior)");
    H("cd -",             "Volver al directorio previo");
    H("pwd",              "Mostrar directorio actual");

    printf("\n%s  Variables y entorno:%s\n", BOLD, RESET);
    H("set VAR=val",      "Definir variable de sesión");
    H("export VAR=val",   "Definir y exportar variable al entorno");
    H("unset VAR",        "Eliminar variable");
    H("env",              "Mostrar variables de entorno del sistema");

    printf("\n%s  Alias:%s\n", BOLD, RESET);
    H("alias name=cmd",   "Definir alias");
    H("alias",            "Mostrar todos los alias");
    H("unalias name",     "Eliminar alias");

    printf("\n%s  Historial:%s\n", BOLD, RESET);
    H("history [n]",      "Mostrar últimos N comandos (default: 20)");
    H("!n",               "Re-ejecutar comando número N");
    H("↑ / ↓",           "Navegar el historial");

    printf("\n%s  Procesos:%s\n", BOLD, RESET);
    H("jobs",             "Listar procesos en background");
    H("kill PID",         "Matar proceso por PID");
    H("cmd &",            "Ejecutar comando en background");

    printf("\n%s  Información:%s\n", BOLD, RESET);
    H("whoami",           "Mostrar usuario y nivel de privilegios");
    H("which cmd",        "Encontrar la ruta de un ejecutable");

    printf("\n%s  E/S:%s\n", BOLD, RESET);
    H("cmd | cmd2",       "Pipeline: stdout → stdin");
    H("cmd > file",       "Redirigir stdout a archivo (sobrescribir)");
    H("cmd >> file",      "Redirigir stdout a archivo (añadir)");
    H("cmd < file",       "Leer stdin desde archivo");

    printf("\n%s  Shell:%s\n", BOLD, RESET);
    H("source file",      "Ejecutar script de ZAR-SHELL");
    H("clear / cls",      "Limpiar pantalla");
    H("help",             "Esta ayuda");
    H("exit [code]",      "Salir del shell");
    printf("\n");
    return true;
}

// ── source ────────────────────────────────────────────────────────────────────
// (declaración adelantada necesaria)
void ExecuteInput(const string& line);

bool Builtin_source(const vector<string>& args) {
    if (args.size() < 2) {
        fprintf(stderr, "%s[zar] source: falta argumento%s\n", RED, RESET);
        return true;
    }
    ifstream f(args[1]);
    if (!f) {
        fprintf(stderr, "%s[zar] source: no se puede abrir '%s'%s\n",
                RED, args[1].c_str(), RESET);
        return true;
    }
    string line;
    while (getline(f, line)) {
        // Saltar comentarios y líneas vacías
        auto trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        if (trimmed.empty() || trimmed[0] == '#') continue;
        ExecuteInput(line);
    }
    return true;
}

// ── Despachador de builtins ───────────────────────────────────────────────────
bool ExecuteBuiltin(const SimpleCmd& cmd) {
    if (cmd.args.empty()) return false;
    const string& name = cmd.args[0];

    if (name == "cd")                  return Builtin_cd(cmd.args);
    if (name == "pwd")                 return Builtin_pwd(cmd.args);
    if (name == "echo")                return Builtin_echo(cmd.args);
    if (name == "set")                 return Builtin_set(cmd.args, false);
    if (name == "export")              return Builtin_set(cmd.args, true);
    if (name == "unset")               return Builtin_unset(cmd.args);
    if (name == "env")                 return Builtin_env(cmd.args);
    if (name == "alias")               return Builtin_alias(cmd.args);
    if (name == "unalias")             return Builtin_unalias(cmd.args);
    if (name == "history")             return Builtin_history(cmd.args);
    if (name == "which")               return Builtin_which(cmd.args);
    if (name == "jobs")                return Builtin_jobs(cmd.args);
    if (name == "kill")                return Builtin_kill(cmd.args);
    if (name == "whoami")              return Builtin_whoami(cmd.args);
    if (name == "help")                return Builtin_help(cmd.args);
    if (name == "source")              return Builtin_source(cmd.args);
    if (name == "clear" || name == "cls") { ClearScreen(); return true; }
    if (name == "exit" || name == "salir") {
        shell.lastExitCode = (cmd.args.size() >= 2) ? stoi(cmd.args[1]) : 0;
        shell.running = false;
        return true;
    }

    return false; // No es un builtin
}

// ============================================================================
//  EJECUCIÓN DE EXTERNOS (SIN CMD.EXE)
// ============================================================================

// Construir el string de línea de comandos desde args
string BuildCommandLine(const vector<string>& args) {
    string cl;
    for (int i = 0; i < (int)args.size(); i++) {
        if (i > 0) cl += ' ';
        // Si contiene espacios, entrecomillar
        bool needsQuote = args[i].find(' ') != string::npos
                       || args[i].find('\t') != string::npos
                       || args[i].empty();
        if (needsQuote) {
            cl += '"';
            for (char c : args[i]) {
                if (c == '"') cl += "\\\"";
                else cl += c;
            }
            cl += '"';
        } else {
            cl += args[i];
        }
    }
    return cl;
}

// Ejecutar UN proceso con handles de stdin/stdout dados
// Devuelve el HANDLE del proceso (o INVALID_HANDLE_VALUE si falla)
HANDLE SpawnProcess(const SimpleCmd& cmd,
                    HANDLE hStdinOverride,
                    HANDLE hStdoutOverride,
                    bool   waitForIt)
{
    // Buscar el ejecutable
    string exe = FindInPath(cmd.args[0]);
    if (exe.empty()) {
        // Intentar directamente (puede ser un PATH largo)
        exe = cmd.args[0];
    }

    // Construir la línea de comandos completa
    string cmdLine = "\"" + exe + "\"";
    for (int i = 1; i < (int)cmd.args.size(); i++) {
        cmdLine += ' ';
        bool needsQuote = cmd.args[i].find(' ') != string::npos || cmd.args[i].empty();
        if (needsQuote) {
            cmdLine += '"';
            for (char c : cmd.args[i]) {
                if (c == '"') cmdLine += "\\\"";
                else cmdLine += c;
            }
            cmdLine += '"';
        } else {
            cmdLine += cmd.args[i];
        }
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // Si se pasa override de stdin/stdout, configurarlo
    HANDLE hIn  = INVALID_HANDLE_VALUE;
    HANDLE hOut = INVALID_HANDLE_VALUE;

    if (hStdinOverride != INVALID_HANDLE_VALUE) {
        si.hStdInput = hStdinOverride;
        si.dwFlags |= STARTF_USESTDHANDLES;
    }
    if (hStdoutOverride != INVALID_HANDLE_VALUE) {
        si.hStdOutput = hStdoutOverride;
        si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
        si.dwFlags |= STARTF_USESTDHANDLES;
    }
    if (si.dwFlags & STARTF_USESTDHANDLES) {
        if (!(si.dwFlags & STARTF_USESTDHANDLES) || hStdinOverride == INVALID_HANDLE_VALUE)
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        if (hStdoutOverride == INVALID_HANDLE_VALUE)
            si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }

    // Redirección de archivo stdin
    if (!cmd.stdinFile.empty()) {
        SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
        hIn = CreateFileA(cmd.stdinFile.c_str(), GENERIC_READ,
                          FILE_SHARE_READ, &sa, OPEN_EXISTING, 0, NULL);
        if (hIn == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "%s[zar] No se puede abrir '%s': %lu%s\n",
                    RED, cmd.stdinFile.c_str(), GetLastError(), RESET);
        } else {
            si.hStdInput = hIn;
            si.dwFlags |= STARTF_USESTDHANDLES;
            if (!(si.dwFlags & STARTF_USESTDHANDLES) || si.hStdOutput == NULL)
                si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
            si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        }
    }

    // Redirección de archivo stdout
    if (!cmd.stdoutFile.empty()) {
        SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
        DWORD creation = cmd.appendStdout ? OPEN_ALWAYS : CREATE_ALWAYS;
        hOut = CreateFileA(cmd.stdoutFile.c_str(), GENERIC_WRITE,
                           0, &sa, creation, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hOut == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "%s[zar] No se puede crear '%s': %lu%s\n",
                    RED, cmd.stdoutFile.c_str(), GetLastError(), RESET);
        } else {
            if (cmd.appendStdout) SetFilePointer(hOut, 0, NULL, FILE_END);
            si.hStdOutput = hOut;
            si.dwFlags |= STARTF_USESTDHANDLES;
            if (si.hStdInput == NULL)
                si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        }
    }

    // Asegurarse de que si usamos STARTF_USESTDHANDLES todos los handles sean válidos
    if (si.dwFlags & STARTF_USESTDHANDLES) {
        if (!si.hStdInput)  si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
        if (!si.hStdOutput) si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!si.hStdError)  si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    }

    vector<char> cl(cmdLine.begin(), cmdLine.end());
    cl.push_back('\0');

    DWORD flags = 0;
    if (waitForIt == false) flags |= DETACHED_PROCESS;

    BOOL ok = CreateProcessA(
        NULL,
        cl.data(),
        NULL,
        NULL,
        TRUE,  // Heredar handles para que pipes funcionen
        flags,
        NULL,
        NULL,
        &si,
        &pi
    );

    // Cerrar handles de archivo abiertos localmente
    if (hIn  != INVALID_HANDLE_VALUE) CloseHandle(hIn);
    if (hOut != INVALID_HANDLE_VALUE) CloseHandle(hOut);

    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            fprintf(stderr, "%s[zar] '%s': comando no encontrado%s\n",
                    RED, cmd.args[0].c_str(), RESET);
        else
            fprintf(stderr, "%s[zar] Error al lanzar '%s': %lu%s\n",
                    RED, cmd.args[0].c_str(), err, RESET);
        return INVALID_HANDLE_VALUE;
    }

    CloseHandle(pi.hThread);
    return pi.hProcess;
}

// ============================================================================
//  EJECUTAR PIPELINE COMPLETO
// ============================================================================

int ExecutePipeline(const Pipeline& pl) {
    if (pl.cmds.empty()) return 0;

    // Caso trivial: un solo comando
    if (pl.cmds.size() == 1) {
        const SimpleCmd& cmd = pl.cmds[0];

        // Intentar builtin primero
        if (ExecuteBuiltin(cmd)) return shell.lastExitCode;

        HANDLE hProc = SpawnProcess(cmd, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, !pl.background);

        if (hProc == INVALID_HANDLE_VALUE) return 1;

        if (pl.background) {
            DWORD pid = GetProcessId(hProc);
            printf("%s[bg] PID %lu%s\n", CYAN, pid, RESET);
            BackgroundJob job;
            job.pid      = pid;
            job.hProcess = hProc;
            job.command  = pl.cmds[0].args.empty() ? "" : pl.cmds[0].args[0];
            shell.jobs.push_back(job);
            return 0;
        }

        WaitForSingleObject(hProc, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(hProc, &code);
        CloseHandle(hProc);
        return (int)code;
    }

    // Pipeline multi-proceso: cmd0 | cmd1 | cmd2 | ...
    int n = (int)pl.cmds.size();
    vector<HANDLE> procs;
    HANDLE prevReadEnd = INVALID_HANDLE_VALUE;

    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};

    for (int i = 0; i < n; i++) {
        HANDLE pipeRead  = INVALID_HANDLE_VALUE;
        HANDLE pipeWrite = INVALID_HANDLE_VALUE;

        // Crear pipe hacia el siguiente proceso (salvo el último)
        if (i < n - 1) {
            if (!CreatePipe(&pipeRead, &pipeWrite, &sa, 0)) {
                fprintf(stderr, "%s[zar] Error creando pipe: %lu%s\n",
                        RED, GetLastError(), RESET);
                break;
            }
            // El write-end no debe ser heredado por el proceso siguiente
            SetHandleInformation(pipeRead, HANDLE_FLAG_INHERIT, 0);
        }

        HANDLE hStdoutOverride = (i < n - 1) ? pipeWrite : INVALID_HANDLE_VALUE;

        // Si el comando tiene redirección explícita de stdout, hStdoutOverride no aplica
        if (!pl.cmds[i].stdoutFile.empty()) hStdoutOverride = INVALID_HANDLE_VALUE;

        HANDLE hProc = SpawnProcess(pl.cmds[i], prevReadEnd, hStdoutOverride, true);

        // Cerrar el write-end en el padre (el hijo ya lo heredó)
        if (pipeWrite != INVALID_HANDLE_VALUE) CloseHandle(pipeWrite);

        // Cerrar el read-end anterior (el hijo anterior ya lo envió al hijo actual)
        if (prevReadEnd != INVALID_HANDLE_VALUE) CloseHandle(prevReadEnd);

        prevReadEnd = pipeRead;

        if (hProc != INVALID_HANDLE_VALUE) procs.push_back(hProc);
    }

    // Esperar al último proceso del pipeline
    int exitCode = 0;
    for (HANDLE h : procs) {
        WaitForSingleObject(h, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(h, &code);
        exitCode = (int)code; // Solo nos importa el código del último
        CloseHandle(h);
    }

    return exitCode;
}

// ============================================================================
//  EJECUTAR LÍNEA DE INPUT
// ============================================================================

void ExecuteInput(const string& rawLine) {
    // Trim leading/trailing whitespace
    string line = rawLine;
    while (!line.empty() && isspace((unsigned char)line.front())) line.erase(line.begin());
    while (!line.empty() && isspace((unsigned char)line.back()))  line.pop_back();

    if (line.empty() || line[0] == '#') return;

    // Expansión de !n (re-ejecutar historial)
    if (line[0] == '!') {
        try {
            int idx = stoi(line.substr(1)) - 1;
            if (idx >= 0 && idx < (int)shell.history.size()) {
                line = shell.history[idx];
                printf("%s[hist] %s%s\n", GRAY, line.c_str(), RESET);
            } else {
                fprintf(stderr, "%s[zar] !%s: índice de historial fuera de rango%s\n",
                        RED, line.substr(1).c_str(), RESET);
                shell.lastExitCode = 1;
                return;
            }
        } catch (...) {
            fprintf(stderr, "%s[zar] !%s: índice inválido%s\n",
                    RED, line.substr(1).c_str(), RESET);
            shell.lastExitCode = 1;
            return;
        }
    }

    // Comandos separados por ';'
    // (Soporte básico de secuencia sin control de flujo)
    // Buscar ';' fuera de comillas
    {
        bool inQ = false;
        char qChar = 0;
        for (int i = 0; i < (int)line.size(); i++) {
            if (!inQ && (line[i] == '"' || line[i] == '\'')) {
                inQ = true; qChar = line[i];
            } else if (inQ && line[i] == qChar) {
                inQ = false;
            } else if (!inQ && line[i] == ';') {
                string first = line.substr(0, i);
                string rest  = line.substr(i + 1);
                ExecuteInput(first);
                ExecuteInput(rest);
                return;
            }
        }
    }

    Pipeline pl = ParsePipeline(line);
    if (pl.cmds.empty()) return;

    DWORD t0 = GetTickCount();
    shell.lastExitCode = ExecutePipeline(pl);
    shell.lastElapsedMs = GetTickCount() - t0;
}

// ============================================================================
//  BANNER DE BIENVENIDA
// ============================================================================

void PrintBanner() {
    ClearScreen();
    printf("\n");
    printf("%s%s", BOLD, CYAN);
    printf("  ███████╗ █████╗ ██████╗      ███████╗██╗  ██╗███████╗██╗     ██╗     \n");
    printf("  ╚══███╔╝██╔══██╗██╔══██╗     ██╔════╝██║  ██║██╔════╝██║     ██║     \n");
    printf("    ███╔╝ ███████║██████╔╝     ███████╗███████║█████╗  ██║     ██║     \n");
    printf("   ███╔╝  ██╔══██║██╔══██╗     ╚════██║██╔══██║██╔══╝  ██║     ██║     \n");
    printf("  ███████╗██║  ██║██║  ██║     ███████║██║  ██║███████╗███████╗███████╗\n");
    printf("  ╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝     ╚══════╝╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝\n");
    printf("%s", RESET);
    printf("\n");
    printf("%s  [ Kernel Interface v2.0 — El shell que Windows no merece ]%s\n", ORANGE, RESET);
    printf("%s  [ CMD.exe puede irse al carajo. Este es territorio ZAR.  ]%s\n", GRAY,   RESET);
    printf("\n");

    if (shell.isAdmin)
        printf("%s%s  ⚡ MODO ADMINISTRADOR ACTIVO — con privilegios extendidos%s\n\n",
               BOLD, YELLOW, RESET);

    printf("%s  ↑↓ historial  |  pipe  >/</>  redirección  cmd & background%s\n", DIM, RESET);
    printf("%s  Escribe 'help' para ver todos los comandos integrados.%s\n\n", DIM, RESET);
    printf("%s  ─────────────────────────────────────────────────────────%s\n\n", GRAY, RESET);
}

// ============================================================================
//  ALIASES POR DEFECTO
// ============================================================================

void LoadDefaultAliases() {
    // Los que esperas de un shell normal
    shell.aliases["ll"]    = "dir /a";
    shell.aliases["ls"]    = "dir /b";
    shell.aliases["la"]    = "dir /a /b";
    shell.aliases["grep"]  = "findstr";
    shell.aliases["cat"]   = "type";
    shell.aliases["rm"]    = "del";
    shell.aliases["rmdir"] = "rd /s /q";
    shell.aliases["cp"]    = "copy";
    shell.aliases["mv"]    = "move";
    shell.aliases["ps"]    = "tasklist";
    shell.aliases["top"]   = "tasklist /v";
    shell.aliases["ifconfig"] = "ipconfig /all";
    shell.aliases[".."]    = "cd ..";
    shell.aliases["..."]   = "cd ../..";
    shell.aliases["...."]  = "cd ../../..";
}

// ============================================================================
//  MAIN
// ============================================================================

int main() {
    // 1. Consola
    InitConsole();

    // 2. Privilegios máximos disponibles en Ring 3
    ElevarPrivilegios();

    // 3. Info de host/usuario
    InitHostInfo();

    // 4. Historial persistente
    LoadHistory();

    // 5. Aliases inteligentes
    LoadDefaultAliases();

    // 6. Banner
    PrintBanner();

    // ── Bucle principal ───────────────────────────────────────────────────
    while (shell.running) {
        // Mostrar prompt
        printf("%s", RenderPrompt().c_str());
        fflush(stdout);

        // Leer línea con soporte de flechas
        string line = ReadLineWithHistory();

        // Trim
        while (!line.empty() && isspace((unsigned char)line.back())) line.pop_back();

        if (line.empty()) continue;

        // Agregar al historial
        AddHistory(line);

        // Ejecutar
        ExecuteInput(line);
    }

    // ── Limpieza ──────────────────────────────────────────────────────────
    SaveHistory();

    printf("\n%s[ZAR] Hasta la próxima. Windows puede seguir sufriendo.%s\n\n",
           CYAN, RESET);

    return shell.lastExitCode;
}