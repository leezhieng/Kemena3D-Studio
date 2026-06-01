#ifndef MAIN_MENU_H
#define MAIN_MENU_H

#include "kemena/kemena.h"

#include "manager.h"

#include <SDL3/SDL_dialog.h>

#include "imgui_internal.h"   // Required for ImGuiSettingsHandler
#include "stb/stb_image.h"

#include <windows.h>

using namespace kemena;

/**
 * @brief Visibility flags for the editor's dockable panels.
 *
 * Each member toggles whether the corresponding editor panel is currently
 * shown. The state is persisted to and restored from the ImGui layout .ini
 * file via the panel-state settings handler in MainMenu.
 */
struct ShowPanel
{
	bool world = true;          ///< Show the World (viewport) panel.
	bool inspector = true;      ///< Show the Inspector panel.
	bool hierarchy = true;      ///< Show the Hierarchy panel.
	bool console = true;        ///< Show the Console panel.
	bool project = true;        ///< Show the Project (asset browser) panel.
	bool shaderEditor = false;  ///< Show the Shader Editor panel.
	bool scriptEditor = false;  ///< Show the Script Editor panel.
	bool game = false;          ///< Show the Game (play-mode) panel.
	bool prefab = false;        ///< Show the Prefab editor; toggled by Manager::editPrefab / closePrefabEditor.
};

inline ShowPanel showPanel;                       ///< Global panel-visibility state shared across the editor UI.
inline ImGuiSettingsHandler ini_handler;          ///< ImGui settings handler used to persist panel state in the layout .ini.
inline bool isReloadLayout = false;               ///< When true, the editor reloads the layout from @ref layoutFileName next frame.
inline kString layoutFileName = "layout.ini";     ///< Path of the layout .ini file to (re)load.
inline bool showSplashScreen = false;             ///< When true, the splash screen popup is displayed.
inline bool showAbout        = false;             ///< When true, the About dialog is displayed.

/**
 * @brief The editor's top main menu bar and its associated dialogs.
 *
 * MainMenu renders the application menu bar (File, Edit, Assets, Object,
 * Component, Window, Help) using the GUI manager, dispatching the chosen
 * actions to the editor Manager. It also owns the Build Settings/export popup
 * and the About dialog, and provides the ImGui settings handler that persists
 * panel visibility state to the layout .ini file.
 */
class MainMenu
{
	public:
		/**
		 * @brief Constructs the main menu, binding it to the GUI and editor managers.
		 * @param setGuiManager GUI manager used to draw the menu bar and items.
		 * @param setManager Editor manager that the menu actions operate on.
		 */
		MainMenu(kGuiManager* setGuiManager, Manager* setManager);

		/**
		 * @brief SDL file-dialog callback that saves the current workspace layout.
		 *
		 * Writes the active ImGui layout to the file chosen in the save dialog.
		 * @param userdata Opaque user data passed to the SDL dialog (unused).
		 * @param filelist Null-terminated list of selected file paths; null on error, empty on cancel.
		 * @param filter Index of the file filter that was active when selected.
		 */
		static void SDLCALL saveWorkspaceCallback(void* userdata, const char* const* filelist, int filter);

		/**
		 * @brief SDL file-dialog callback that requests loading a workspace layout.
		 *
		 * Flags a deferred layout reload from the chosen file via @ref isReloadLayout
		 * and @ref layoutFileName.
		 * @param userdata Opaque user data passed to the SDL dialog (unused).
		 * @param filelist Null-terminated list of selected file paths; null on error, empty on cancel.
		 * @param filter Index of the file filter that was active when selected.
		 */
		static void SDLCALL loadWorkspaceCallback(void* userdata, const char* const* filelist, int filter);

		/**
		 * @brief Draws the menu bar, the Build Settings popup, and dispatches menu actions.
		 * @param window Application window, used for native dialogs and play controls.
		 * @param showPanel Panel-visibility state that the Window menu toggles.
		 */
		void draw(kWindow* window, ShowPanel& showPanel);

		/**
		 * @brief ImGui settings handler: opens the custom "Panels" section for reading.
		 * @param name Name of the .ini section being opened.
		 * @return A non-null opaque handle if @p name is the "Panels" section, otherwise nullptr.
		 */
		static void* readOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* name);

		/**
		 * @brief ImGui settings handler: parses one line of the "Panels" section.
		 *
		 * Restores individual @ref ShowPanel flags from the layout .ini.
		 * @param line A single line of text from the panel-state section.
		 */
		static void readLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line);

		/**
		 * @brief ImGui settings handler: serializes the panel state to the .ini buffer.
		 * @param out_buf Output text buffer the "Panels" section is appended to.
		 */
		static void writeAll(ImGuiContext*, ImGuiSettingsHandler*, ImGuiTextBuffer* out_buf);

		/**
		 * @brief Registers the panel-state settings handler with ImGui.
		 *
		 * Wires @ref readOpen, @ref readLine and @ref writeAll into @ref ini_handler
		 * so panel visibility is persisted across sessions.
		 */
		static void registerPanelStateHandler();

		/**
		 * @brief Draws the modal About dialog when @ref showAbout is set.
		 *
		 * Lazily loads the logo texture and shows project links and credits.
		 */
		void drawAbout();

		Manager* manager;   ///< Editor manager that menu actions are dispatched to.
		kGuiManager* gui;   ///< GUI manager used to render the menu bar and items.

	private:
		ImTextureRef texAboutLogo = nullptr;   ///< Cached About-dialog logo texture, lazily loaded on first draw.
};

#endif

