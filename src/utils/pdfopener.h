#pragma once
#include <QString>

/**
 * @brief Utility class that launches a PDF in an external viewer.
 *
 * Launch strategy (in order):
 *  1. Try Okular (Linux/KDE).
 *  2. Fall back to QDesktopServices::openUrl() which invokes the OS default
 *     viewer (Evince/GNOME, Preview/macOS, Edge/Windows, etc.).
 *
 * All methods are static – the class has no instance state.
 */
class PdfOpener
{
public:
    PdfOpener() = delete;

    /**
     * @brief Open @p filePath with the preferred viewer.
     * @return true  if the launch command was dispatched successfully.
     *         false if the file does not exist or no viewer could be started.
     */
    static bool open(const QString& filePath);

    /**
     * @brief Attempt to open with Okular specifically.
     * @return true if Okular was found and launched.
     */
    static bool openWithOkular(const QString& filePath);

    /**
     * @brief Open with the OS default application (always available).
     */
    static bool openWithDefault(const QString& filePath);

    /// Returns true if Okular is installed and reachable on PATH.
    static bool isOkularAvailable();
};
