// This file is isolated from raylib to avoid Windows header conflicts

#include "file_dialog.hpp"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#endif

#include <iostream>

namespace util {

std::string openFileDialog() {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "Audio Files\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac\0"
                      "MP3 Files\0*.mp3\0"
                      "WAV Files\0*.wav\0"
                      "FLAC Files\0*.flac\0"
                      "All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Select Audio File";
    ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    
    if (GetOpenFileNameA(&ofn)) {
        return std::string(filename);
    }
#else
    std::cout << "File dialog not supported on this platform.\n";
    std::cout << "Please provide the audio file path as a command-line argument.\n";
#endif
    return "";
}

} // namespace util


