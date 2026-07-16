#include "mainmenu.h"
#include <kemena/kmeshgenerator.h>
#include <kemena/klight.h>

#include "portable-file-dialogs.h"
#include <cstring>
#include <string>

using namespace kemena;

MainMenu::MainMenu(kGuiManager *setGuiManager, Manager *setManager)
{
	gui = setGuiManager;
	manager = setManager;
}

void SDLCALL MainMenu::saveWorkspaceCallback(void *userdata, const char *const *filelist, int filter)
{
	if (!filelist)
	{
		SDL_Log("Save dialog error: %s", SDL_GetError());
	}
	else if (!*filelist)
	{
		SDL_Log("Save dialog cancelled");
	}
	else
	{
		const char *path = filelist[0];
		SDL_Log("Saving layout to: %s", path);
		ImGui::SaveIniSettingsToDisk(path);
	}
}

void SDLCALL MainMenu::loadWorkspaceCallback(void *userdata, const char *const *filelist, int filter)
{
	if (!filelist)
	{
		SDL_Log("Load dialog error: %s", SDL_GetError());
	}
	else if (!*filelist)
	{
		SDL_Log("Load dialog cancelled");
	}
	else
	{
		const char *path = filelist[0];
		SDL_Log("Loading layout from: %s", path);

		isReloadLayout = true;
		layoutFileName = path;
	}
}

// Helper: text input with a char buffer
static void inputStr(const char *label, std::string &str, float w = 340.0f)
{
	char buf[1024];
	strncpy_s(buf, sizeof(buf), str.c_str(), _TRUNCATE);
	buf[sizeof(buf) - 1] = '\0';
	ImGui::SetNextItemWidth(w);
	if (ImGui::InputText(label, buf, sizeof(buf)))
		str = buf;
}

// Helper: browse-for-folder button
static bool browseFolder(const char *label, std::string &path)
{
	inputStr(label, path);
	ImGui::SameLine();
	if (ImGui::SmallButton("..."))
	{
		auto sel = pfd::select_folder("Choose folder").result();
		if (!sel.empty()) { path = sel; return true; }
	}
	return false;
}

// Helper: browse-for-file button
static bool browseFile(const char *label, std::string &path, const std::string &filter)
{
	inputStr(label, path);
	ImGui::SameLine();
	if (ImGui::SmallButton("..."))
	{
		auto sel = pfd::open_file("Choose file", "", {filter}).result();
		if (!sel.empty()) { path = sel[0]; return true; }
	}
	return false;
}

// ---------------------------------------------------------------------------
// Publish Dialog
// ---------------------------------------------------------------------------
static void drawPublishDialog(Manager *manager)
{
	if (!manager->showPublishDialog) return;

	Manager::PublishSettings &ps = manager->publishSettings;

	ImGui::OpenPopup("Publish Game");
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(620, 520), ImGuiCond_FirstUseEver);

	if (!ImGui::BeginPopupModal("Publish Game", &manager->showPublishDialog))
		return;

	static int activeTab = 0;
	const char *tabNames[] = {"Windows", "macOS", "Linux"};

	// ---- Platform tabs -------------------------------------------------
	if (ImGui::BeginTabBar("PublishTabs"))
	{
		for (int i = 0; i < 3; ++i)
		{
			if (!ImGui::BeginTabItem(tabNames[i])) continue;
			activeTab = i;

			Manager::PlatformPublishSettings &plat = ps.platforms[i];

			ImGui::Checkbox("Build for this platform", &plat.enabled);
			ImGui::Separator();

			inputStr("Game Name", plat.gameName);
			inputStr("Window Title", plat.title);
			ImGui::SetNextItemWidth(160.0f);
			ImGui::InputInt("Width", &plat.width);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(160.0f);
			ImGui::InputInt("Height", &plat.height);
			ImGui::Checkbox("Fullscreen", &plat.fullscreen);

			ImGui::Separator();
			browseFolder("Output Folder", plat.outputDir);

			inputStr("Template Folder", plat.templateDir);
			ImGui::SameLine();
			if (ImGui::SmallButton("...##tpl"))
			{
				auto sel = pfd::select_folder("Choose runtime template folder").result();
				if (!sel.empty()) plat.templateDir = sel;
			}

			// Windows-specific: icon
			if (i == 0)
				browseFile("Icon (.ico)", plat.iconPath, "Icon (*.ico)");

			// Compression
			ImGui::Separator();
			const char *compLevels[] = {"None", "Fast", "Default", "Best"};
			int compIdx = 0;
			if (plat.compression == "Fast") compIdx = 1;
			else if (plat.compression == "Default") compIdx = 2;
			else if (plat.compression == "Best") compIdx = 3;
			ImGui::SetNextItemWidth(200.0f);
			if (ImGui::Combo("Compression", &compIdx, compLevels, IM_ARRAYSIZE(compLevels)))
				plat.compression = compLevels[compIdx];

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::Separator();

	// ---- World selection -----------------------------------------------
	ImGui::TextUnformatted("Worlds to include:");
	ImGui::BeginChild("WorldList", ImVec2(0, 120), true);

	// Gather all .world files in the project Assets directory
	std::vector<fs::path> worldFiles;
	if (manager->projectOpened)
	{
		std::error_code ec;
		fs::path assetsDir = manager->projectPath / "Assets";
		if (fs::exists(assetsDir, ec))
		{
			for (const auto &entry : fs::recursive_directory_iterator(assetsDir, ec))
			{
				if (entry.is_regular_file() && entry.path().extension() == ".world")
					worldFiles.push_back(entry.path());
			}
		}
	}

	for (const auto &wf : worldFiles)
	{
		std::string relPath = fs::relative(wf, manager->projectPath / "Assets").generic_string();
		bool checked = false;
		for (const auto &iw : ps.includeWorlds)
			if (iw == relPath) { checked = true; break; }

		if (ImGui::Checkbox(relPath.c_str(), &checked))
		{
			if (checked)
				ps.includeWorlds.push_back(relPath);
			else
				ps.includeWorlds.erase(
					std::remove(ps.includeWorlds.begin(), ps.includeWorlds.end(), relPath),
					ps.includeWorlds.end());
		}
	}

	if (worldFiles.empty())
		ImGui::TextDisabled("  No .world files found in Assets/");

	ImGui::EndChild();

	// ---- Default level -------------------------------------------------
	ImGui::Spacing();
	ImGui::SetNextItemWidth(400.0f);
	if (ImGui::BeginCombo("Default Level", ps.defaultLevel.empty() ? "(none)" : ps.defaultLevel.c_str()))
	{
		for (const auto &iw : ps.includeWorlds)
		{
			bool isSelected = (ps.defaultLevel == iw);
			if (ImGui::Selectable(iw.c_str(), isSelected))
				ps.defaultLevel = iw;
			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::TextDisabled("  First world loaded when the game starts.");

	ImGui::Separator();

	// ---- Action buttons ------------------------------------------------
	if (ImGui::Button("Publish", ImVec2(120, 0)))
	{
		// Publish for the currently active tab
		if (manager->publishGame(activeTab))
		{
			manager->savePublishSettings();
			ImGui::CloseCurrentPopup();
			manager->showPublishDialog = false;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Publish All", ImVec2(120, 0)))
	{
		bool any = false;
		for (int i = 0; i < 3; ++i)
		{
			if (ps.platforms[i].enabled)
			{
				if (manager->publishGame(i)) any = true;
			}
		}
		if (any)
		{
			manager->savePublishSettings();
			ImGui::CloseCurrentPopup();
			manager->showPublishDialog = false;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
	{
		ImGui::CloseCurrentPopup();
		manager->showPublishDialog = false;
	}

	ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Project Settings Dialog
// ---------------------------------------------------------------------------
static void drawProjectSettingsDialog(Manager *manager)
{
	if (!manager->showProjectSettings) return;

	ImGui::OpenPopup("Project Settings");
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(450, 300), ImGuiCond_FirstUseEver);

	if (!ImGui::BeginPopupModal("Project Settings", &manager->showProjectSettings))
		return;

	Manager::PublishSettings &ps = manager->publishSettings;

	inputStr("Default Game Name", ps.platforms[0].gameName);
	// Sync to other platforms
	for (int i = 1; i < 3; ++i)
		ps.platforms[i].gameName = ps.platforms[0].gameName;

	ImGui::Separator();

	// Default level
	ImGui::SetNextItemWidth(400.0f);
	std::vector<fs::path> worldFiles;
	if (manager->projectOpened)
	{
		std::error_code ec;
		fs::path assetsDir = manager->projectPath / "Assets";
		if (fs::exists(assetsDir, ec))
		{
			for (const auto &entry : fs::recursive_directory_iterator(assetsDir, ec))
				if (entry.is_regular_file() && entry.path().extension() == ".world")
					worldFiles.push_back(entry.path());
		}
	}

	if (ImGui::BeginCombo("Default Level", ps.defaultLevel.empty() ? "(none)" : ps.defaultLevel.c_str()))
	{
		for (const auto &wf : worldFiles)
		{
			std::string relPath = fs::relative(wf, manager->projectPath / "Assets").generic_string();
			bool isSelected = (ps.defaultLevel == relPath);
			if (ImGui::Selectable(relPath.c_str(), isSelected))
				ps.defaultLevel = relPath;
			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	ImGui::Separator();

	browseFolder("Runtime Template Folder", ps.templateDir);
	ImGui::TextDisabled("  Folder with the built kemena3d-runtime + its libs.");

	ImGui::Separator();

	if (ImGui::Button("Save", ImVec2(120, 0)))
	{
		manager->savePublishSettings();
		ImGui::CloseCurrentPopup();
		manager->showProjectSettings = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
	{
		ImGui::CloseCurrentPopup();
		manager->showProjectSettings = false;
	}

	ImGui::EndPopup();
}

// ===========================================================================
// Main draw
// ===========================================================================
void MainMenu::draw(kWindow *window, ShowPanel &showPanel)
{
	if (gui->menuBar())
	{
		// File menu
		if (gui->menu("File"))
		{
			if (gui->menuItem("New World", "", false, manager->projectOpened))
				manager->newWorld();
			if (gui->menuItem("Open World", "", false, manager->projectOpened))
			{
				auto files = pfd::open_file("Open World", manager->projectPath.string(),
											{"World Files", "*.world", "All Files", "*"})
								 .result();
				if (!files.empty())
					manager->loadWorld(files[0]);
			}
			if (gui->menuItem("Open Recent World", "", false, manager->projectOpened))
			{
			}
			gui->separator();
			if (gui->menuItem("Save World", "Ctrl+S", false, manager->projectOpened))
				manager->saveWorld();
			if (gui->menuItem("Save As...", "", false, manager->projectOpened))
				manager->saveWorldAs();
			gui->separator();
			if (gui->menuItem("New Project", ""))
				manager->newProject();
			if (gui->menuItem("Open Project", ""))
				manager->openProject();
			if (gui->menuItem("Save Project", "", false, manager->projectOpened))
			{
			}
			gui->separator();
			if (gui->menuItem("Publish...", "", false, manager->projectOpened))
			{
				manager->loadPublishSettings();
				manager->showPublishDialog = true;
			}
			gui->separator();
			if (gui->menuItem("Exit"))
				manager->closeEditor();

			gui->menuEnd();
		}

		// Edit menu
		if (gui->menu("Edit"))
		{
			if (gui->menuItem("Undo", "Ctrl+Z", false, manager->projectOpened && manager->undoRedo.canUndo()))
				manager->undoRedo.undo();
			if (gui->menuItem("Redo", "Ctrl+Y", false, manager->projectOpened && manager->undoRedo.canRedo()))
				manager->undoRedo.redo();
			if (gui->menuItem("Undo History", "", false, manager->projectOpened))
			{
			}
			gui->separator();
			if (gui->menuItem("Select All", "", false, manager->projectOpened))
				manager->selectAll();
			if (gui->menuItem("Deselect All", "", false, manager->projectOpened))
				manager->deselectAll();
			if (gui->menuItem("Select Children", "", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("Select Prefab Root", "", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("Invert Selection", "", false, manager->projectOpened))
				manager->invertSelection();
			gui->separator();
			if (gui->menuItem("Cut", "Ctrl+X", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("Copy", "Ctrl+C", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("Paste", "Ctrl+V", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("Paste As Child", "", false, manager->projectOpened))
			{
			}
			gui->separator();
			if (gui->menuItem("Frame Selected", "", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("Lock View To Selected", "", false, manager->projectOpened))
			{
			}
			gui->separator();
			if (gui->menuItem("Play", "", false, manager->projectOpened))
			{
				if (manager->panelGame)
					manager->panelGame->pressPlay();
			}
			if (gui->menuItem("Pause", "", false, manager->projectOpened))
			{
				if (manager->panelGame)
					manager->panelGame->pressPause();
			}
			if (gui->menuItem("Stop", "", false, manager->projectOpened && manager->panelGame && manager->panelGame->getPlayState() != GamePlayState::Stopped))
			{
				if (manager->panelGame)
					manager->panelGame->pressStop();
			}
			if (gui->menuItem("Build Scripts", "", false, manager->projectOpened))
				manager->buildScripts();
			gui->separator();
			if (gui->menuItem("Sign In", ""))
			{
			}
			if (gui->menuItem("Sign Out", ""))
			{
			}
			gui->separator();
			if (gui->menuItem("Project Settings", "", false, manager->projectOpened))
			{
				manager->loadPublishSettings();
				manager->showProjectSettings = true;
			}
			if (gui->menuItem("Preferences", ""))
			{
			}
			if (gui->menuItem("Shortcuts", ""))
			{
			}
			if (gui->menuItem("Clear All PlayerPrefs", "", false, manager->projectOpened))
			{
			}

			gui->menuEnd();
		}

		// Assets Menu
		if (gui->menu("Assets"))
		{
			if (ImGui::BeginMenu("Create", manager->projectOpened))
			{
				if (ImGui::MenuItem("Folder"))
					manager->createNewFolder();
				if (ImGui::MenuItem("Shader"))
					manager->createNewShader();
				if (ImGui::MenuItem("Raw Shader"))
					manager->createNewRawShader();
				if (ImGui::MenuItem("Material"))
					manager->createNewMaterial();
				if (ImGui::MenuItem("Script"))
					manager->createNewScript();
				if (ImGui::MenuItem("Logic Graph"))
					manager->createNewLogicGraph();
				ImGui::Separator();
				if (ImGui::MenuItem("Animator"))
					manager->createNewAnimator();
				if (ImGui::MenuItem("Animation Clip"))
					manager->createNewAnimation();
				ImGui::EndMenu();
			}
			if (gui->menuItem("Show In Explorer", "", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("Open", "", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("Delete", "", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("Rename", "", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("Copy Path", "", false, manager->projectOpened))
			{
			}
			gui->separator();
			if (gui->menuItem("Refresh", "", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("Reimport", "", false, manager->projectOpened))
			{
			}
			gui->separator();
			if (gui->menuItem("Reimport All", "", false, manager->projectOpened))
			{
			}
			gui->separator();
			if (gui->menuItem("Generate Lighting", "", false, manager->projectOpened))
			{
			}

			gui->menuEnd();
		}

		// Object Menu
		if (ImGui::BeginMenu("Object", manager->projectOpened))
		{
			if (gui->menuItem("Create Scene", "", false, manager->projectOpened))
				manager->createSceneObject();
			gui->separator();
			if (gui->menuItem("Create Empty", "", false, manager->projectOpened))
				manager->createEmpty();

			if (gui->menu("3D Object"))
			{
				if (gui->menuItem("Empty", "", false, manager->projectOpened))
					manager->createEmpty();
				if (gui->menuItem("Cube", "", false, manager->projectOpened))
					manager->createMeshPrimitive(kMeshGenerator::generateCube(), "Cube");
				if (gui->menuItem("Sphere", "", false, manager->projectOpened))
					manager->createMeshPrimitive(kMeshGenerator::generateSphere(), "Sphere");
				if (gui->menuItem("Capsule", "", false, manager->projectOpened))
					manager->createMeshPrimitive(kMeshGenerator::generateCapsule(), "Capsule");
				if (gui->menuItem("Cylinder", "", false, manager->projectOpened))
					manager->createMeshPrimitive(kMeshGenerator::generateCylinder(), "Cylinder");
				if (gui->menuItem("Plane", "", false, manager->projectOpened))
					manager->createMeshPrimitive(kMeshGenerator::generatePlane(), "Plane");
				if (gui->menuItem("Mesh...", "", false, manager->projectOpened))
					manager->createMeshFromFile();
				gui->menuEnd();
			}

			if (gui->menuItem("Effects", "", false, manager->projectOpened))
			{
			}

			if (gui->menu("Light"))
			{
				if (gui->menuItem("Sun", "", false, manager->projectOpened))
					manager->createLight(LIGHT_TYPE_SUN);
				if (gui->menuItem("Point", "", false, manager->projectOpened))
					manager->createLight(LIGHT_TYPE_POINT);
				if (gui->menuItem("Spot", "", false, manager->projectOpened))
					manager->createLight(LIGHT_TYPE_SPOT);
				gui->menuEnd();
			}

			if (gui->menuItem("Audio", "", false, manager->projectOpened))
				manager->createAudio();
			if (gui->menuItem("Video", "", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("UI", "", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("Camera", "", false, manager->projectOpened))
				manager->createCamera();
			if (gui->menuItem("Terrain", "", false, manager->projectOpened))
				manager->createTerrain();
			if (gui->menuItem("Nav Mesh", "", false, manager->projectOpened))
				manager->createNavMesh();

			ImGui::EndMenu();
		}

		// Component Menu
		if (gui->menu("Component"))
		{
			if (gui->menuItem("Audio", "", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("Effect", "", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("Mesh", "", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("Physics", "", false, manager->projectOpened))
			{
			}
			if (gui->menuItem("Scripts", "", false, manager->projectOpened))
			{
			}

			gui->menuEnd();
		}

		// Window menu
		if (gui->menu("Window"))
		{
			if (ImGui::BeginMenu("General", manager->projectOpened))
			{
				if (gui->menuItem("Inspector", "", showPanel.inspector))
					showPanel.inspector = !showPanel.inspector;
				if (gui->menuItem("Hierarchy", "", showPanel.hierarchy))
					showPanel.hierarchy = !showPanel.hierarchy;
				if (gui->menuItem("Project", "", showPanel.project))
					showPanel.project = !showPanel.project;
				if (gui->menuItem("Console", "", showPanel.console))
					showPanel.console = !showPanel.console;
				if (gui->menuItem("Shader Editor", "", showPanel.shaderEditor))
					showPanel.shaderEditor = !showPanel.shaderEditor;
				if (gui->menuItem("Script Editor", "", showPanel.scriptEditor))
					showPanel.scriptEditor = !showPanel.scriptEditor;
				if (gui->menuItem("Game", "", showPanel.game))
					showPanel.game = !showPanel.game;
				if (gui->menuItem("Animator Editor", "", showPanel.animatorEditor))
					showPanel.animatorEditor = !showPanel.animatorEditor;
				if (gui->menuItem("Animation Editor", "", showPanel.animationEditor))
					showPanel.animationEditor = !showPanel.animationEditor;
				ImGui::EndMenu();
			}

			gui->separator();

			if (ImGui::BeginMenu("Workspace", manager->projectOpened))
			{
				if (gui->menuItem("Save", ""))
				{
					SDL_DialogFileFilter filters[] =
						{
							{"Ini files", "ini"},
							{"All files", "*"}};
					SDL_ShowSaveFileDialog(
						saveWorkspaceCallback, nullptr,
						window->getSdlWindow(),
						filters, SDL_arraysize(filters),
						"layout.ini");
				}
				if (gui->menuItem("Load", ""))
				{
					SDL_DialogFileFilter filters[] =
						{
							{"Ini files", "ini"},
							{"All files", "*"}};
					SDL_ShowOpenFileDialog(
						loadWorkspaceCallback, nullptr,
						window->getSdlWindow(),
						filters, SDL_arraysize(filters),
						"layout.ini", false);
				}
				gui->separator();
				if (gui->menuItem("Reset", ""))
				{
					isReloadLayout = true;
					layoutFileName = "layout.ini";
				}
				ImGui::EndMenu();
			}
			gui->menuEnd();
		}

		// Help Menu
		if (gui->menu("Help"))
		{
			if (gui->menuItem("About", ""))
				showAbout = true;
			if (gui->menuItem("Splash Screen", ""))
				showSplashScreen = true;
			gui->separator();
			if (gui->menuItem("Manual", ""))
				SDL_OpenURL("https://kemena3d.com/manual");
			if (gui->menuItem("Scripting Reference", ""))
			{
			}
			gui->separator();
			if (gui->menuItem("Release Notes", ""))
			{
			}
			if (gui->menuItem("Software Licenses", ""))
			{
			}
			if (gui->menuItem("Report a Bug", ""))
			{
			}

			gui->menuEnd();
		}

		gui->menuBarEnd();
	}

	// ---- Publish Dialog -----------------------------------------------------
	drawPublishDialog(manager);

	// ---- Project Settings Dialog --------------------------------------------
	drawProjectSettingsDialog(manager);

	// ---- Legacy Build Settings dialog (kept for backward compat) ------------
	if (manager->showExportDialog)
	{
		ImGui::OpenPopup("Build Settings");
		manager->showExportDialog = false;
	}
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Build Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		Manager::ExportSettings &es = manager->exportSettings;

		const char *platforms[] = {"Windows", "Linux", "macOS"};
		ImGui::SetNextItemWidth(340.0f);
		ImGui::Combo("Platform", &es.platform, platforms, IM_ARRAYSIZE(platforms));

		inputStr("Game Name", es.gameName);
		inputStr("Window Title", es.title);
		ImGui::SetNextItemWidth(160.0f);
		ImGui::InputInt("Width", &es.width);
		ImGui::SetNextItemWidth(160.0f);
		ImGui::InputInt("Height", &es.height);
		ImGui::Checkbox("Fullscreen", &es.fullscreen);

		ImGui::Separator();

		browseFolder("Output Folder", es.outputDir);

		inputStr("Template Folder", es.templateDir);
		ImGui::SameLine();
		if (ImGui::SmallButton("...##exportTpl"))
		{
			auto sel = pfd::select_folder("Choose runtime template folder").result();
			if (!sel.empty()) es.templateDir = sel;
		}
		ImGui::TextDisabled("  Folder with the built kemena3d-runtime + its libs.");

		if (es.platform == 0)
			browseFile("Icon (.ico)", es.iconPath, "Icon (*.ico)");

		ImGui::Separator();
		if (ImGui::Button("Export", ImVec2(120, 0)))
		{
			// Route to new publish system
			Manager::PublishSettings &ps = manager->publishSettings;
			ps.platforms[es.platform].gameName = es.gameName;
			ps.platforms[es.platform].title = es.title;
			ps.platforms[es.platform].width = es.width;
			ps.platforms[es.platform].height = es.height;
			ps.platforms[es.platform].fullscreen = es.fullscreen;
			ps.platforms[es.platform].outputDir = es.outputDir;
			ps.platforms[es.platform].templateDir = es.templateDir;
			ps.platforms[es.platform].iconPath = es.iconPath;
			if (manager->publishGame(es.platform))
				ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0)))
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}
}

void MainMenu::drawAbout()
{
	if (!showAbout) return;

	if (texAboutLogo == nullptr)
	{
		kAssetManager *am = manager->getAssetManager();
		if (am)
		{
			kTexture2D *logo = am->loadTexture2DFromResource(
				"IMAGE_KEMENA_LOGO_INV", "aboutLogo", kTextureFormat::TEX_FORMAT_RGBA);
			if (logo)
				texAboutLogo = (ImTextureRef)(intptr_t)logo->getTextureID();
		}
	}

	ImGui::OpenPopup("About Kemena3D");
	ImGuiIO &io = ImGui::GetIO();
	ImVec2 center(floorf(io.DisplaySize.x * 0.5f), floorf(io.DisplaySize.y * 0.5f));
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);

	bool stillOpen = true;
	if (ImGui::BeginPopupModal("About Kemena3D", &stillOpen,
							   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
								   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize))
	{
		float winW = ImGui::GetContentRegionAvail().x;

		if (texAboutLogo != nullptr)
		{
			constexpr float LOGO_W = 200.0f;
			constexpr float LOGO_H = 58.0f;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (winW - LOGO_W) * 0.5f);
			ImGui::Image(texAboutLogo, ImVec2(LOGO_W, LOGO_H));
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		constexpr float BTN_H = 28.0f;
		constexpr float GAP = 6.0f;
		float btnW = (winW - GAP * 2.0f) / 3.0f;

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.80f, 0.90f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.52f, 0.92f, 1.00f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.32f, 0.68f, 1.00f));

		if (ImGui::Button("Website", ImVec2(btnW, BTN_H)))
			SDL_OpenURL("https://kemena3d.com");
		ImGui::SameLine(0.0f, GAP);
		if (ImGui::Button("Discord", ImVec2(btnW, BTN_H)))
			SDL_OpenURL("https://discord.gg/eNCZAzAntF");
		ImGui::SameLine(0.0f, GAP);
		if (ImGui::Button("GitHub", ImVec2(btnW, BTN_H)))
			SDL_OpenURL("https://github.com/leezhieng/kemena3d");

		ImGui::PopStyleColor(3);

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		const char *credit = "Created by Lee Zhi Eng";
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (winW - ImGui::CalcTextSize(credit).x) * 0.5f);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.62f, 0.68f, 1.0f));
		ImGui::TextUnformatted(credit);
		ImGui::PopStyleColor();

		ImGui::Spacing();
		ImGui::EndPopup();
	}

	ImGui::PopStyleVar(3);
	if (!stillOpen) showAbout = false;
}

void *MainMenu::readOpen(ImGuiContext *, ImGuiSettingsHandler *, const char *name)
{
	if (strcmp(name, "Panels") == 0)
		return (void *)1;
	return nullptr;
}

void MainMenu::readLine(ImGuiContext *, ImGuiSettingsHandler *, void *, const char *line)
{
	int tmp;
	if (sscanf_s(line, "WorldOpened=%d", &tmp) == 1)
		showPanel.world = (tmp != 0);
	else if (sscanf_s(line, "InspectorOpened=%d", &tmp) == 1)
		showPanel.inspector = (tmp != 0);
	else if (sscanf_s(line, "HierarchyOpened=%d", &tmp) == 1)
		showPanel.hierarchy = (tmp != 0);
	else if (sscanf_s(line, "ConsoleOpened=%d", &tmp) == 1)
		showPanel.console = (tmp != 0);
	else if (sscanf_s(line, "ProjectOpened=%d", &tmp) == 1)
		showPanel.project = (tmp != 0);
	else if (sscanf_s(line, "ShaderEditorOpened=%d", &tmp) == 1)
		showPanel.shaderEditor = (tmp != 0);
	else if (sscanf_s(line, "GameOpened=%d", &tmp) == 1)
		showPanel.game = (tmp != 0);
	else if (sscanf_s(line, "AnimatorEditorOpened=%d", &tmp) == 1)
		showPanel.animatorEditor = (tmp != 0);
	else if (sscanf_s(line, "AnimationEditorOpened=%d", &tmp) == 1)
		showPanel.animationEditor = (tmp != 0);
}

void MainMenu::writeAll(ImGuiContext *, ImGuiSettingsHandler *, ImGuiTextBuffer *out_buf)
{
	out_buf->appendf("[Panels]\n");
	out_buf->appendf("WorldOpened=%d\n", showPanel.world ? 1 : 0);
	out_buf->appendf("InspectorOpened=%d\n", showPanel.inspector ? 1 : 0);
	out_buf->appendf("HierarchyOpened=%d\n", showPanel.hierarchy ? 1 : 0);
	out_buf->appendf("ConsoleOpened=%d\n", showPanel.console ? 1 : 0);
	out_buf->appendf("ProjectOpened=%d\n", showPanel.project ? 1 : 0);
	out_buf->appendf("ShaderEditorOpened=%d\n", showPanel.shaderEditor ? 1 : 0);
	out_buf->appendf("GameOpened=%d\n", showPanel.game ? 1 : 0);
	out_buf->appendf("AnimatorEditorOpened=%d\n", showPanel.animatorEditor ? 1 : 0);
	out_buf->appendf("AnimationEditorOpened=%d\n", showPanel.animationEditor ? 1 : 0);
	out_buf->append("\n");
}

void MainMenu::registerPanelStateHandler()
{
	ini_handler.TypeName = "Panels";
	ini_handler.TypeHash = ImHashStr("Panels");
	ini_handler.ReadOpenFn = readOpen;
	ini_handler.ReadLineFn = readLine;
	ini_handler.WriteAllFn = writeAll;
	ini_handler.ClearAllFn = nullptr;
	ini_handler.ApplyAllFn = nullptr;

	ImGui::AddSettingsHandler(&ini_handler);
}
