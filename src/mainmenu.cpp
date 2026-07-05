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
		// ImGui::LoadIniSettingsFromDisk(path);

		isReloadLayout = true;
		layoutFileName = path;
	}
}

void MainMenu::draw(kWindow *window, ShowPanel &showPanel)
{
	if (gui->menuBar())
	{
		// File menu
		if (gui->menu("File"))
		{
			if (gui->menuItem("New World", "", false, manager->projectOpened))
			{
				manager->newWorld();
			}
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
			{
				manager->newProject();
			}
			if (gui->menuItem("Open Project", ""))
			{
				manager->openProject();
			}
			if (gui->menuItem("Save Project", "", false, manager->projectOpened))
			{
			}
			gui->separator();
			if (gui->menuItem("Build Settings", "", false, manager->projectOpened))
				manager->showExportDialog = true;
			if (gui->menuItem("Build And Run", "", false, manager->projectOpened))
			{
			}
			gui->separator();
			if (gui->menuItem("Exit"))
			{
				manager->closeEditor();
			}

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
			{
				manager->buildScripts();
			}
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

			// 3D Object submenu
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

			// Light submenu
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

			// if (gui->menuItem("Rendering", "", showRendering))
			// showRendering = !showRendering;

			// if (gui->menuItem("Animation", "", showAnimation))
			// showAnimation = !showAnimation;

			// if (gui->menuItem("Audio", "", showAudio))
			// showAudio = !showAudio;

			// if (gui->menuItem("Sequencing", "", showSequencing))
			// showSequencing = !showSequencing;

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
						saveWorkspaceCallback,
						nullptr, // userdata
						window->getSdlWindow(),
						filters,
						SDL_arraysize(filters),
						"layout.ini" // default filename
					);
				}
				if (gui->menuItem("Load", ""))
				{
					SDL_DialogFileFilter filters[] =
						{
							{"Ini files", "ini"},
							{"All files", "*"}};
					SDL_ShowOpenFileDialog(
						loadWorkspaceCallback,
						nullptr, // userdata
						window->getSdlWindow(),
						filters,
						SDL_arraysize(filters),
						"layout.ini", // default start manager/dir
						false);
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
			{
				showAbout = true;
			}
			if (gui->menuItem("Splash Screen", ""))
			{
				showSplashScreen = true;
			}
			gui->separator();
			if (gui->menuItem("Manual", ""))
			{
				SDL_OpenURL("https://kemena3d.com/manual");
			}
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

	// ---- Build Settings / Export dialog ------------------------------------
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

		auto inputStr = [](const char *label, std::string &str, float w = 340.0f)
		{
			char buf[1024];
			strncpy_s(buf, sizeof(buf), str.c_str(), _TRUNCATE);
			buf[sizeof(buf) - 1] = '\0';
			ImGui::SetNextItemWidth(w);
			if (ImGui::InputText(label, buf, sizeof(buf)))
				str = buf;
		};

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

		inputStr("Output Folder", es.outputDir);
		ImGui::SameLine();
		if (ImGui::SmallButton("...##exportOut"))
		{
			auto sel = pfd::select_folder("Choose output folder").result();
			if (!sel.empty())
				es.outputDir = sel;
		}

		inputStr("Runtime Template Folder", es.templateDir);
		ImGui::SameLine();
		if (ImGui::SmallButton("...##exportTpl"))
		{
			auto sel = pfd::select_folder("Choose runtime template folder").result();
			if (!sel.empty())
				es.templateDir = sel;
		}
		ImGui::TextDisabled("  Folder with the built kemena3d-runtime + its libs.");

		if (es.platform == 0)
		{
			inputStr("Icon (.ico)", es.iconPath);
			ImGui::SameLine();
			if (ImGui::SmallButton("...##exportIco"))
			{
				auto sel = pfd::open_file("Choose icon", "",
										  {"Icon (*.ico)", "*.ico"})
							   .result();
				if (!sel.empty())
					es.iconPath = sel[0];
			}
		}

		ImGui::Separator();
		if (ImGui::Button("Export", ImVec2(120, 0)))
		{
			if (manager->exportGame(es))
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
	if (!showAbout)
		return;

	// Lazy-load the logo texture once
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

		// Logo
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

		// Link buttons — three equal-width columns
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

		// "Created by" line
		const char *credit = "Created by Lee Zhi Eng";
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (winW - ImGui::CalcTextSize(credit).x) * 0.5f);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.62f, 0.68f, 1.0f));
		ImGui::TextUnformatted(credit);
		ImGui::PopStyleColor();

		ImGui::Spacing();

		ImGui::EndPopup();
	}

	ImGui::PopStyleVar(3);

	if (!stillOpen)
		showAbout = false;
}

void *MainMenu::readOpen(ImGuiContext *, ImGuiSettingsHandler *, const char *name)
{
	// We only care about our custom section
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
