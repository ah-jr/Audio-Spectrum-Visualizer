#pragma once

#include <string>

namespace util {

/**
 * Open a native file dialog to select an audio file
 * @return Selected file path, or empty string if cancelled
 */
std::string openFileDialog();

} // namespace util


