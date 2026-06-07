#ifndef PANEL_CONSOLE_H
#define PANEL_CONSOLE_H

#include "kemena/kemena.h"

#include "manager.h"

#include <imgui.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstdarg>

using namespace kemena;

/**
 * @brief Severity level of a console log entry.
 *
 * Drives the text color used when rendering the corresponding item.
 */
enum class LogLevel
{
	Info,    ///< Informational message, rendered white.
	Warning, ///< Warning message, rendered orange.
	Error    ///< Error message, rendered red.
};

/**
 * @brief A single entry shown in the console panel.
 *
 * Pairs the displayed text with its severity level.
 */
struct ConsoleItem
{
	kString text;   ///< The textual content of the log line.
	LogLevel level; ///< Severity level controlling the display color.
};

class Manager;

/**
 * @brief Editor panel that displays log output and accepts text commands.
 *
 * Renders an ImGui window containing a scrolling list of log items (with
 * per-line selection, copy-to-clipboard and a context menu) plus a command
 * input field. Disabled while no project is open.
 */
class PanelConsole
{
	private:
		std::vector<ConsoleItem> consoleItems;       ///< All log entries shown in the panel.
		char inputBuf[256] = "";                      ///< Backing buffer for the command input field.
		bool scrollToBottom = false;                  ///< When true, the view scrolls to the latest entry on next draw.

		std::unordered_set<int> selectedIndices;     ///< Indices of all currently selected lines.
        size_t lastClickedIndex = -1;               ///< Index of the last clicked line, used as the anchor for shift-range selection.

        bool showCopiedTooltip = false;              ///< Whether the "copied to clipboard" tooltip is currently shown.
        float copiedTooltipTime = 0.0f;              ///< Elapsed time the tooltip has been visible, in seconds.
        const float copiedTooltipDuration = 1.0f;    ///< Duration the copied tooltip stays visible, in seconds.

	public:
		/**
		 * @brief Constructs the console panel and logs a welcome message.
		 * @param setGuiManager GUI manager used to issue ImGui draw calls.
		 * @param setManager Editor manager providing application/project state.
		 */
		PanelConsole(kGuiManager* setGuiManager, Manager* setManager);

		/**
		 * @brief Appends a formatted log entry to the console.
		 *
		 * Formats the message printf-style and queues a scroll to the bottom.
		 * @param level Severity level of the entry.
		 * @param fmt printf-style format string.
		 * @param ... Variadic arguments matching @p fmt.
		 */
		void addLog(LogLevel level, const char* fmt, ...);

		/**
		 * @brief ImGui input text callback for the command field.
		 *
		 * Reserved for history browsing or autocompletion; currently a no-op.
		 * @param data ImGui callback data for the input text.
		 * @return Always 0.
		 */
		static int textEditCallback(ImGuiInputTextCallbackData* data);

		/**
		 * @brief Renders the console window for the current frame.
		 *
		 * Draws the scrolling log list, handles line selection, copy and the
		 * context menu, and processes the command input field.
		 * @param opened Reference to the panel's visibility flag; updated by the window close button.
		 */
		void draw(bool& opened);

		Manager* manager; ///< Editor manager providing application/project state.
		kGuiManager* gui; ///< GUI manager used to issue ImGui draw calls.
};

#endif

