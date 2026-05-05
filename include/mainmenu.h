#ifndef MAIN_MENU_H
#define MAIN_MENU_H

#include "kemena/kemena.h"

#include "manager.h"

#include <SDL3/SDL_dialog.h>

#include "imgui_internal.h"   // Required for ImGuiSettingsHandler
#include "stb/stb_image.h"

#include <windows.h>

using namespace kemena;

struct ShowPanel
{
	bool world = true;
	bool inspector = true;
	bool hierarchy = true;
	bool console = true;
	bool project = true;
	bool shaderEditor = false;
	bool game = false;
	bool prefab = false;   // Toggled by Manager::editPrefab / closePrefabEditor.
};

inline ShowPanel showPanel;
inline ImGuiSettingsHandler ini_handler;
inline bool isReloadLayout = false;
inline kString layoutFileName = "layout.ini";
inline bool showSplashScreen = false;
inline bool showAbout        = false;

class MainMenu
{
	public:
		MainMenu(kGuiManager* setGuiManager, Manager* setManager);
		static void SDLCALL saveWorkspaceCallback(void* userdata, const char* const* filelist, int filter);
		static void SDLCALL loadWorkspaceCallback(void* userdata, const char* const* filelist, int filter);
		void draw(kWindow* window, ShowPanel& showPanel);

		static void* readOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* name);
		static void readLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line);
		static void writeAll(ImGuiContext*, ImGuiSettingsHandler*, ImGuiTextBuffer* out_buf);
		static void registerPanelStateHandler();

		void drawAbout();

		Manager* manager;
		kGuiManager* gui;

	private:
		ImTextureRef texAboutLogo = nullptr;
};

#endif

