#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#include "resource.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

#define IDC_ROOT_EDIT 1001
#define IDC_BROWSE 1002
#define IDC_VIDEO_FILE 1003
#define IDC_AUDIO_FILE 1004
#define IDC_LOAD_VIDEO 1005
#define IDC_LOAD_AUDIO 1006
#define IDC_DOWNLOAD_VIDEO 1007
#define IDC_DOWNLOAD_AUDIO 1008
#define IDC_DEBUG 1009
#define IDC_LOG 1010
#define IDC_STATUS 1011

struct AppState {
    HWND hwnd{};
    HWND rootEdit{};
    HWND videoFileEdit{};
    HWND audioFileEdit{};
    HWND logEdit{};
    HWND statusLabel{};
    HWND debugCheck{};
    HWND downloadVideoButton{};
    HWND downloadAudioButton{};
    std::atomic_bool busy{false};
    fs::path appDir;
};

struct ModeConfig {
    bool audio{};
    std::wstring inputFile;
    std::wstring outputSubdir;
    std::wstring logFile;
    std::wstring errorLog;
    std::wstring title;
};

AppState g_app;

std::wstring ToWide(const std::string& text) {
    if (text.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        size = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring out(size, L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
        return out;
    }
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring GetWindowTextString(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring text(len + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), len + 1);
    text.resize(len);
    return text;
}

std::wstring Quote(const std::wstring& value) {
    std::wstring escaped = value;
    size_t pos = 0;
    while ((pos = escaped.find(L'"', pos)) != std::wstring::npos) {
        escaped.insert(pos, L"\\");
        pos += 2;
    }
    return L"\"" + escaped + L"\"";
}

std::wstring WrapForCmd(const std::wstring& command) {
    return L"cmd.exe /d /s /c \"" + command + L"\"";
}

void AppendLog(const std::wstring& text) {
    if (!g_app.logEdit) return;
    int len = GetWindowTextLengthW(g_app.logEdit);
    SendMessageW(g_app.logEdit, EM_SETSEL, len, len);
    SendMessageW(g_app.logEdit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
}

void SetStatus(const std::wstring& text) {
    if (g_app.statusLabel) SetWindowTextW(g_app.statusLabel, text.c_str());
}

void SetBusy(bool busy) {
    g_app.busy = busy;
    EnableWindow(g_app.downloadVideoButton, !busy);
    EnableWindow(g_app.downloadAudioButton, !busy);
    SetStatus(busy ? L"Running..." : L"Ready");
}

void AppendLogFromWorker(const std::wstring& text) {
    auto* copy = new std::wstring(text);
    PostMessageW(g_app.hwnd, WM_APP + 1, 0, reinterpret_cast<LPARAM>(copy));
}

void SetStatusFromWorker(const std::wstring& text) {
    auto* copy = new std::wstring(text);
    PostMessageW(g_app.hwnd, WM_APP + 2, 0, reinterpret_cast<LPARAM>(copy));
}

std::wstring Trim(const std::wstring& text) {
    const wchar_t* whitespace = L" \t\r\n";
    size_t first = text.find_first_not_of(whitespace);
    if (first == std::wstring::npos) return L"";
    size_t last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

std::vector<std::wstring> ReadUrls(const fs::path& file) {
    std::vector<std::wstring> urls;
    std::wifstream input(file);
    input.imbue(std::locale(""));
    std::wstring line;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == L'#') continue;
        urls.push_back(line);
    }
    return urls;
}

void CreateTemplateFile(const fs::path& file) {
    std::ofstream output(file);
    output << "# Add YouTube URLs below, one per line.\n";
    output << "# Lines starting with # are comments and ignored.\n";
    output << "# Example:\n";
    output << "# https://www.youtube.com/watch?v=dQw4w9WgXcQ\n";
}

fs::path FindYtDlp() {
    fs::path local = g_app.appDir / L"yt-dlp.exe";
    if (fs::exists(local)) return local;
    return L"yt-dlp";
}

fs::path LocalFfmpegBin() {
    return g_app.appDir / L"ffmpeg-master-latest-win64-gpl-shared" / L"bin";
}

fs::path RootFfmpegBin(const fs::path& root) {
    return root / L"ffmpeg-master-latest-win64-gpl-shared" / L"bin";
}

bool ExistingFfmpegBin(const fs::path& bin) {
    return fs::exists(bin / L"ffmpeg.exe");
}

void EnsureFfmpegPath(const fs::path& bin) {
    fs::path exe = bin / L"ffmpeg.exe";
    if (!fs::exists(exe)) return;
    std::wstring current;
    DWORD needed = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (needed > 0) {
        current.resize(needed - 1);
        GetEnvironmentVariableW(L"PATH", current.data(), needed);
    }
    std::wstring updated = bin.wstring() + L";" + current;
    SetEnvironmentVariableW(L"PATH", updated.c_str());
}

int RunCommand(const std::wstring& command, const fs::path& cwd, const fs::path* appendFile = nullptr) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return -1;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::wstring cmd = WrapForCmd(command);
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                             cwd.empty() ? nullptr : cwd.wstring().c_str(), &si, &pi);
    CloseHandle(writePipe);

    if (!ok) {
        CloseHandle(readPipe);
        AppendLogFromWorker(L"Failed to start command.\r\n");
        return -1;
    }

    std::ofstream logFile;
    if (appendFile) logFile.open(*appendFile, std::ios::app | std::ios::binary);

    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer) - 1, &read, nullptr) && read > 0) {
        buffer[read] = '\0';
        if (logFile) logFile.write(buffer, read);
        AppendLogFromWorker(ToWide(std::string(buffer, read)));
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(readPipe);
    return static_cast<int>(exitCode);
}

bool DownloadFilePowerShell(const std::wstring& url, const fs::path& output) {
    fs::create_directories(output.parent_path());
    std::wstring command = L"powershell -NoProfile -ExecutionPolicy Bypass -Command \"[Net.ServicePointManager]::SecurityProtocol='Tls12'; Invoke-WebRequest -Uri '";
    command += url + L"' -OutFile " + Quote(output.wstring()) + L" -UseBasicParsing\"";
    return RunCommand(command, g_app.appDir) == 0;
}

bool EnsureYtDlp(bool debug) {
    fs::path local = g_app.appDir / L"yt-dlp.exe";
    if (!fs::exists(local)) {
        AppendLogFromWorker(L"yt-dlp.exe not found. Downloading latest release...\r\n");
        if (!DownloadFilePowerShell(L"https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe", local)) {
            AppendLogFromWorker(L"Could not download yt-dlp.exe.\r\n");
            return false;
        }
    }

    std::wstring command = Quote(local.wstring()) + L" --update-to nightly";
    if (debug) AppendLogFromWorker(L"DEBUG command: " + command + L"\r\n");
    int exitCode = RunCommand(command, g_app.appDir);
    if (exitCode != 0) AppendLogFromWorker(L"yt-dlp update failed; continuing with current copy.\r\n");
    return true;
}

bool EnsureFfmpeg(const fs::path& root) {
    fs::path localBin = LocalFfmpegBin();
    if (ExistingFfmpegBin(localBin)) {
        EnsureFfmpegPath(localBin);
        return true;
    }

    fs::path rootBin = RootFfmpegBin(root);
    if (ExistingFfmpegBin(rootBin)) {
        EnsureFfmpegPath(rootBin);
        return true;
    }

    if (RunCommand(L"where ffmpeg", g_app.appDir) == 0) return true;

    AppendLogFromWorker(L"ffmpeg.exe not found. Downloading portable build to " + root.wstring() + L" (~100MB)...\r\n");
    fs::path temp = fs::temp_directory_path() / (L"yt-downloader-ffmpeg-" + std::to_wstring(GetTickCount64()));
    fs::path zip = temp / L"ffmpeg.zip";
    if (!DownloadFilePowerShell(L"https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip", zip)) return false;

    AppendLogFromWorker(L"Extracting ffmpeg...\r\n");
    std::wstring command = L"powershell -NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -Path " + Quote(zip.wstring()) + L" -DestinationPath " + Quote(temp.wstring()) + L" -Force\"";
    if (RunCommand(command, g_app.appDir) != 0) return false;

    fs::path found;
    for (const auto& entry : fs::directory_iterator(temp)) {
        fs::path candidate = entry.path() / L"bin" / L"ffmpeg.exe";
        if (fs::exists(candidate)) {
            found = entry.path();
            break;
        }
    }
    if (found.empty()) return false;

    fs::path target = root / L"ffmpeg-master-latest-win64-gpl-shared";
    std::error_code ec;
    fs::remove_all(target, ec);
    fs::rename(found, target, ec);
    fs::remove_all(temp, ec);
    EnsureFfmpegPath(target / L"bin");
    return ExistingFfmpegBin(target / L"bin");
}

void WriteRunHeader(const fs::path& logFile, const std::wstring& title, bool debug) {
    std::ofstream out(logFile, std::ios::binary);
    out << ToUtf8(title) << " started\r\n";
    out << "Debug: " << (debug ? "on" : "off") << "\r\n";
    out << "========================================\r\n";
}

void DownloadWorker(ModeConfig config, fs::path root, bool debug) {
    PostMessageW(g_app.hwnd, WM_APP + 3, TRUE, 0);
    AppendLogFromWorker(L"\r\n=== " + config.title + L" ===\r\n");

    try {
        fs::create_directories(root);
        fs::path inputFile = root / config.inputFile;
        fs::path outputDir = root / config.outputSubdir;
        fs::path logFile = root / L"results" / config.logFile;
        fs::path errorLog = root / L"results" / config.errorLog;
        fs::create_directories(outputDir);
        fs::create_directories(logFile.parent_path());

        if (!fs::exists(inputFile)) {
            CreateTemplateFile(inputFile);
            AppendLogFromWorker(L"Created template input file: " + inputFile.wstring() + L"\r\nAdd URLs and run again.\r\n");
            SetStatusFromWorker(L"Template created");
            PostMessageW(g_app.hwnd, WM_APP + 3, FALSE, 0);
            return;
        }

        std::vector<std::wstring> urls = ReadUrls(inputFile);
        if (urls.empty()) {
            AppendLogFromWorker(L"No valid URLs in " + inputFile.wstring() + L"\r\n");
            SetStatusFromWorker(L"No URLs found");
            PostMessageW(g_app.hwnd, WM_APP + 3, FALSE, 0);
            return;
        }

        WriteRunHeader(logFile, config.title, debug);
        std::ofstream errors(errorLog, std::ios::binary);

        if (!EnsureYtDlp(debug)) {
            SetStatusFromWorker(L"yt-dlp missing");
            PostMessageW(g_app.hwnd, WM_APP + 3, FALSE, 0);
            return;
        }
        if (!EnsureFfmpeg(root)) {
            AppendLogFromWorker(L"ffmpeg setup failed. Some downloads may fail.\r\n");
            if (config.audio) {
                SetStatusFromWorker(L"ffmpeg missing");
                PostMessageW(g_app.hwnd, WM_APP + 3, FALSE, 0);
                return;
            }
        }

        fs::path ytdlp = FindYtDlp();
        int success = 0;
        int failed = 0;
        for (size_t i = 0; i < urls.size(); ++i) {
            const std::wstring& url = urls[i];
            AppendLogFromWorker(L"\r\n[" + std::to_wstring(i + 1) + L"/" + std::to_wstring(urls.size()) + L"] " + url + L"\r\n");
            bool done = false;
            for (int attempt = 1; attempt <= 3 && !done; ++attempt) {
                AppendLogFromWorker(L"Attempt " + std::to_wstring(attempt) + L"/3...\r\n");
                std::wstring command = Quote(ytdlp.wstring()) + L" --extractor-args \"youtube:player_client=web,android,ios\" ";
                if (config.audio) {
                    command += L"-f \"bestaudio/best\" -x --audio-format \"mp3\" --audio-quality \"0\" ";
                } else {
                    command += L"-f \"bv*[height<=1080]+ba/b\" --embed-subs --sub-langs \"en.*\" ";
                }
                command += L"--no-playlist --embed-metadata --embed-thumbnail --convert-thumbnails jpg --retries 3 --fragment-retries 3 --no-overwrites --no-mtime ";
                if (debug) command += L"--verbose ";
                command += L"-o " + Quote((outputDir / L"%(title)s.%(ext)s").wstring()) + L" " + Quote(url);
                if (debug) AppendLogFromWorker(L"DEBUG command: " + command + L"\r\n");
                int exitCode = RunCommand(command, root, &logFile);
                done = exitCode == 0;
                if (!done && attempt < 3) Sleep(5000);
            }
            if (done) {
                ++success;
                AppendLogFromWorker(L"OK\r\n");
            } else {
                ++failed;
                errors << "FAILED: " << ToUtf8(url) << "\r\n";
                AppendLogFromWorker(L"FAILED after 3 attempts\r\n");
            }
        }

        AppendLogFromWorker(L"\r\nSummary: " + std::to_wstring(success) + L" ok / " + std::to_wstring(failed) + L" fail\r\n");
        AppendLogFromWorker(L"Output: " + outputDir.wstring() + L"\r\n");
        SetStatusFromWorker(failed == 0 ? L"Done" : L"Done with failures");
    } catch (const std::exception& ex) {
        AppendLogFromWorker(L"Error: " + ToWide(ex.what()) + L"\r\n");
        SetStatusFromWorker(L"Error");
    }

    PostMessageW(g_app.hwnd, WM_APP + 3, FALSE, 0);
}

fs::path GetDefaultRoot() {
    return g_app.appDir;
}

void BrowseForRoot(HWND hwnd) {
    BROWSEINFOW bi{};
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"Select download root directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH]{};
    if (SHGetPathFromIDListW(pidl, path)) SetWindowTextW(g_app.rootEdit, path);
    CoTaskMemFree(pidl);
}

void LoadFileIntoLog(const std::wstring& fileName) {
    fs::path root = GetWindowTextString(g_app.rootEdit);
    fs::path file = root / fileName;
    if (!fs::exists(file)) {
        CreateTemplateFile(file);
        AppendLog(L"Created " + file.wstring() + L"\r\n");
        return;
    }
    std::vector<std::wstring> urls = ReadUrls(file);
    AppendLog(L"\r\n" + file.wstring() + L"\r\n");
    AppendLog(L"Found " + std::to_wstring(urls.size()) + L" URL(s).\r\n");
    for (size_t i = 0; i < urls.size(); ++i) {
        AppendLog(L"  " + std::to_wstring(i + 1) + L". " + urls[i] + L"\r\n");
    }
}

void StartDownload(bool audio) {
    if (g_app.busy) return;
    fs::path root = GetWindowTextString(g_app.rootEdit);
    bool debug = SendMessageW(g_app.debugCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    ModeConfig config;
    if (audio) {
        config = {true, L"audio.txt", L"results\\audio", L"download_log_audio.txt", L"error_log_audio.txt", L"Audio download"};
    } else {
        config = {false, L"video.txt", L"results", L"download_log.txt", L"error_log.txt", L"Video download"};
    }
    std::thread(DownloadWorker, config, root, debug).detach();
}

void CreateControls(HWND hwnd) {
    CreateWindowW(L"STATIC", L"Root directory", WS_CHILD | WS_VISIBLE, 12, 14, 100, 20, hwnd, nullptr, nullptr, nullptr);
    g_app.rootEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", GetDefaultRoot().wstring().c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                     115, 10, 500, 24, hwnd, reinterpret_cast<HMENU>(IDC_ROOT_EDIT), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Browse", WS_CHILD | WS_VISIBLE, 625, 9, 90, 26, hwnd, reinterpret_cast<HMENU>(IDC_BROWSE), nullptr, nullptr);

    CreateWindowW(L"STATIC", L"Video file", WS_CHILD | WS_VISIBLE, 12, 48, 100, 20, hwnd, nullptr, nullptr, nullptr);
    g_app.videoFileEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"video.txt", WS_CHILD | WS_VISIBLE | ES_READONLY,
                                          115, 44, 180, 24, hwnd, reinterpret_cast<HMENU>(IDC_VIDEO_FILE), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Load Video URLs", WS_CHILD | WS_VISIBLE, 305, 43, 130, 26, hwnd, reinterpret_cast<HMENU>(IDC_LOAD_VIDEO), nullptr, nullptr);
    g_app.downloadVideoButton = CreateWindowW(L"BUTTON", L"Download Video", WS_CHILD | WS_VISIBLE, 445, 43, 130, 26, hwnd, reinterpret_cast<HMENU>(IDC_DOWNLOAD_VIDEO), nullptr, nullptr);

    CreateWindowW(L"STATIC", L"Audio file", WS_CHILD | WS_VISIBLE, 12, 82, 100, 20, hwnd, nullptr, nullptr, nullptr);
    g_app.audioFileEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"audio.txt", WS_CHILD | WS_VISIBLE | ES_READONLY,
                                          115, 78, 180, 24, hwnd, reinterpret_cast<HMENU>(IDC_AUDIO_FILE), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Load Audio URLs", WS_CHILD | WS_VISIBLE, 305, 77, 130, 26, hwnd, reinterpret_cast<HMENU>(IDC_LOAD_AUDIO), nullptr, nullptr);
    g_app.downloadAudioButton = CreateWindowW(L"BUTTON", L"Download Audio", WS_CHILD | WS_VISIBLE, 445, 77, 130, 26, hwnd, reinterpret_cast<HMENU>(IDC_DOWNLOAD_AUDIO), nullptr, nullptr);

    g_app.debugCheck = CreateWindowW(L"BUTTON", L"Debug mode (--verbose + command log)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                     12, 116, 280, 24, hwnd, reinterpret_cast<HMENU>(IDC_DEBUG), nullptr, nullptr);
    g_app.statusLabel = CreateWindowW(L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE, 305, 120, 410, 20, hwnd, reinterpret_cast<HMENU>(IDC_STATUS), nullptr, nullptr);

    g_app.logEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                                    12, 150, 703, 360, hwnd, reinterpret_cast<HMENU>(IDC_LOG), nullptr, nullptr);
    SendMessageW(g_app.logEdit, EM_LIMITTEXT, 0, 0);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_app.hwnd = hwnd;
        CreateControls(hwnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BROWSE:
            BrowseForRoot(hwnd);
            break;
        case IDC_LOAD_VIDEO:
            LoadFileIntoLog(L"video.txt");
            break;
        case IDC_LOAD_AUDIO:
            LoadFileIntoLog(L"audio.txt");
            break;
        case IDC_DOWNLOAD_VIDEO:
            StartDownload(false);
            break;
        case IDC_DOWNLOAD_AUDIO:
            StartDownload(true);
            break;
        }
        return 0;
    case WM_APP + 1: {
        auto* text = reinterpret_cast<std::wstring*>(lParam);
        AppendLog(*text);
        delete text;
        return 0;
    }
    case WM_APP + 2: {
        auto* text = reinterpret_cast<std::wstring*>(lParam);
        SetStatus(*text);
        delete text;
        return 0;
    }
    case WM_APP + 3:
        SetBusy(wParam != FALSE);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    g_app.appDir = fs::path(exePath).parent_path();

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    const wchar_t className[] = L"YtDownloaderGuiWindow";
    HICON appIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hIcon = appIcon;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, className, L"YT Downloader GUI", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 745, 565, nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;
    if (appIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIcon));
    }

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
