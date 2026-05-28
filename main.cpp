#include "kemena/kemena.h"

#include "manager.h"
#include "commands.h"
#include "mainmenu.h"
#include "panel_world.h"
#include "panel_inspector.h"
#include "panel_hierarchy.h"
#include "panel_project.h"
#include "panel_console.h"
#include "panel_shader_editor.h"
#include "panel_script_editor.h"
#include "panel_game.h"
#include "panel_prefab.h"
#include "splash_screen.h"
#include "crashhandler.h"

#include "imgui_internal.h" // <-- required for ImGuiSettingsHandler

using namespace kemena;

const kString windowTitle = "Kemena3D Studio";

// Project config
kString projectName = "New Game";
kString developerName = "My Company";
kString projectVersion = "0.0.1";

int main()
{
	// Install the crash reporter before anything else so an early fault still
	// produces kemena3d_crash.log (the Release GUI build has no console).
	installCrashHandler();

	// Create window and renderer
	kWindow *window = createWindow(1024, 768, windowTitle, true);
	kRenderer *renderer = createRenderer(window);
	renderer->setEnableScreenBuffer(true);
	renderer->setEnableShadow(true);
	renderer->setEnableObjectPicking(true);
	renderer->setClearColor(kVec4(0.2f, 0.4f, 0.6f, 1.0f));

	// Setup GUI manager
	kGuiManager *gui = createGuiManager(renderer);

	// Create the asset manager
	kAssetManager *assetManager = createAssetManager();

	// Switch default font
	gui->loadDefaultFontFromResource("FONT_OPENSANS");

	// Create the world and scene
	kWorld *world = createWorld(assetManager);
	kScene *sceneEditor = world->createScene("Editor Scene");
	kScene *scene = world->createScene("Scene");

	// Editor manager
	Manager *manager = new Manager(window, world, renderer);
	manager->setScene(scene);

	// Prefab editor renders into its own offscreen target with a dark-grey
	// background so it's visually distinct from the World panel.
	manager->prefabRenderer.setBackgroundColor(kVec4(0.2f, 0.2f, 0.2f, 1.0f));

	// Initialize panels
	MainMenu *mainmenu = new MainMenu(gui, manager);
	PanelWorld *panelWorld = new PanelWorld(gui, manager);
	PanelInspector *panelInspector = new PanelInspector(gui, manager);
	PanelProject *panelProject = new PanelProject(gui, manager, assetManager);
	PanelHierarchy *panelHierarchy = new PanelHierarchy(gui, manager, assetManager, world);
	PanelConsole *panelConsole = new PanelConsole(gui, manager);
	PanelShaderEditor *panelShaderEditor = new PanelShaderEditor(gui, manager);
	PanelScriptEditor *panelScriptEditor = new PanelScriptEditor(gui, manager);
	PanelGame *panelGame = new PanelGame(gui, manager);
	manager->panelGame = panelGame;
	PanelPrefab *panelPrefab = new PanelPrefab(gui, manager);

	// Route .shader and .prefab double-clicks from the project panel.
	panelProject->onFileDoubleClicked = [&](const std::string& path)
	{
		if (path.size() >= 7 && path.substr(path.size() - 7) == ".shader")
		{
			showPanel.shaderEditor = true;
			panelShaderEditor->openFile(path);
		}
		else if (path.size() >= 6 && path.substr(path.size() - 6) == ".logic")
		{
			showPanel.scriptEditor = true;
			panelScriptEditor->openFile(path);
		}
		else if (path.size() >= 7 && path.substr(path.size() - 7) == ".prefab")
		{
			manager->editPrefab(path);
			showPanel.prefab = manager->prefabEditing;
		}
	};

	// Load default editor layout from embedded resource
	mainmenu->registerPanelStateHandler();
	{
		HRSRC hRes = FindResource(NULL, "LAYOUT_DEFAULT", RT_RCDATA);
		if (hRes)
		{
			HGLOBAL hData = LoadResource(NULL, hRes);
			DWORD size = SizeofResource(NULL, hRes);
			const char *data = static_cast<const char *>(LockResource(hData));
			if (data && size > 0)
				gui->loadIniSettingsFromMemory(data, size);
		}
	}

	// Default skybox
	kShader *skyShader = assetManager->loadGlslFromResource("SHADER_SKYBOX");
	kMaterial *skyMaterial = assetManager->createMaterial(skyShader);
	kTextureCube *skyTexture = assetManager->loadTextureCubeFromResource("TEXTURE_SKYBOX_RIGHT",
																		 "TEXTURE_SKYBOX_LEFT",
																		 "TEXTURE_SKYBOX_TOP",
																		 "TEXTURE_SKYBOX_BOTTOM",
																		 "TEXTURE_SKYBOX_FRONT",
																		 "TEXTURE_SKYBOX_BACK",
																		 "cubeMap");
	skyMaterial->addTexture(skyTexture);
	skyMaterial->setSingleSided(false);
	kMesh *skyMesh = kMeshGenerator::generateCube();
	skyMesh->setMaterial(skyMaterial);
	scene->setSkybox(skyMaterial, skyMesh);

	// Editor grid
	kMesh *gridMesh = kMeshGenerator::generatePlane();
	sceneEditor->setFrustumCullingEnabled(false);
	sceneEditor->addMesh(gridMesh);
	kShader *gridShader = assetManager->loadGlslFromResource("SHADER_GRID");
	kMaterial *gridMat = assetManager->createMaterial(gridShader);
	gridMat->setTransparent(kTransparentType::TRANSP_TYPE_BLEND);
	gridMat->setSingleSided(false);
	gridMesh->setMaterial(gridMat);

	// Default scene content (cube + sun light + game camera) lives in the
	// embedded WORLD_DEFAULT RCDATA — see res/default.world. Loading it here
	// keeps the placeholder data in one file instead of hand-coded objects.
	manager->loadDefaultWorldInto(scene);

	// Editor camera
	kCamera *cameraEditor = world->addCamera(kVec3(-7, 4, 12), kVec3(0, 3.5, 0), kCameraType::CAMERA_TYPE_FREE);
	cameraEditor->setFOV(60.0f);
	world->setMainCamera(cameraEditor);
	manager->editorCamera = cameraEditor;

	bool dragging = false;
	kVec2 dragStart;
	kQuat camRot;

	bool altPressed = false;
	bool ctrlPressed = false;
	bool shiftPressed = false;

	// Splash screen (shown at startup until a project is chosen or dismissed)
	SplashScreen* splashScreen = new SplashScreen(gui, assetManager, manager);

	// Game loop
	kSystemEvent event;
	while (window->getRunning())
	{
		// Must reset the layout at the beginning of the frame
		if (isReloadLayout)
		{
			showPanel = ShowPanel();
			gui->loadIniSettingsFromDisk(layoutFileName);

			isReloadLayout = false;
		}

		float deltaTime = window->getTimer()->getDeltaTime();
		gui->processEvent(event);

		// Event
		if (event.hasEvent())
		{
			int eventType = event.getType();

			if (eventType == K_EVENT_QUIT)
			{
				manager->closeEditor();
			}
			else if (eventType == SDL_EVENT_WINDOW_FOCUS_GAINED)
			{
				// Check asset changes
				if (manager->projectOpened && !manager->showImportPopup)
				{
					manager->checkAssetChange();
				}
			}
			else if (eventType == SDL_EVENT_DROP_FILE)
			{
				// OS file dropped onto the window — copy it into the currently-
				// browsed project folder so checkAssetChange picks it up via the
				// existing import pipeline. Drops outside an open project are
				// ignored.
				const char *droppedPath = event.getSdlEvent()->drop.data;
				if (droppedPath && manager->projectOpened)
				{
					try
					{
						std::filesystem::path src(droppedPath);
						if (std::filesystem::exists(src) && std::filesystem::is_regular_file(src))
						{
							std::filesystem::path destDir = manager->getCurrentDirPath();
							std::filesystem::path dst = destDir / src.filename();
							int counter = 1;
							while (std::filesystem::exists(dst))
							{
								std::string stem = src.stem().string();
								std::string ext  = src.extension().string();
								dst = destDir / (stem + " " + std::to_string(counter) + ext);
								counter++;
							}
							std::filesystem::copy_file(src, dst);
							manager->checkAssetChange();
							if (manager->panelProject)
								manager->panelProject->triggerRefresh();
						}
					}
					catch (const std::exception &e)
					{
						std::cerr << "drop file: " << e.what() << "\n";
					}
				}
			}
			else if (eventType == K_EVENT_MOUSEBUTTONDOWN)
			{
				if (panelWorld->enabled && panelWorld->hovered)
				{
					if (event.getMouseButton() == K_MOUSEBUTTON_LEFT && altPressed)
					{
						dragging = true;

						dragStart.x = event.getMouseX();
						dragStart.y = event.getMouseY();

						camRot = cameraEditor->getRotation();
					}
					else if (event.getMouseButton() == K_MOUSEBUTTON_LEFT && !altPressed && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
					{
						// Snapshot selection before picking (for undo)
						auto selBefore = manager->selectedObjects;
						auto selObjBefore = manager->selectedObject;

						// ImGui::GetIO().MousePos and panelPos are both in the same
						// screen-absolute coordinate space. Multiply by 2 for physical pixels.
						kVec2 imMouse = gui->getMousePos();
						int vpMouseX = (int)((imMouse.x - panelWorld->panelPos.x) * 2.0f);
						int vpMouseY = (int)((imMouse.y - panelWorld->panelPos.y) * 2.0f);

						kScene *pickScene = scene;

						kObject *picked = renderer->pickObject(
							world, pickScene,
							vpMouseX, vpMouseY,
							panelWorld->width * 2, panelWorld->height * 2);

						// Walk up to the direct child of the scene root so we always
						// select the top-level object, not a sub-mesh leaf.
						if (picked != nullptr)
						{
							kObject *sceneRoot = pickScene->getRootNode();
							while (picked->getParent() != nullptr && picked->getParent() != sceneRoot)
								picked = picked->getParent();
						}

						if (picked != nullptr)
						{
							manager->worldSelected  = false;
							manager->selectedScene  = nullptr;
							manager->selectedObject = picked;
							manager->selectObject(picked->getUuid(), !shiftPressed);
							if (manager->panelProject != nullptr)
								manager->panelProject->clearSelection();
						}
						else if (!shiftPressed)
						{
							manager->worldSelected  = false;
							manager->selectedObject = nullptr;
							manager->selectedObjects.clear();
						}

						// Push selection undo if it changed
						auto selAfter = manager->selectedObjects;
						auto selObjAfter = manager->selectedObject;
						if (selBefore != selAfter || selObjBefore != selObjAfter)
						{
							manager->undoRedo.push(std::make_unique<SelectCommand>(
								manager,
								selBefore, selObjBefore,
								selAfter, selObjAfter));
						}
					}
				}
				else
				{
					dragging = false;
				}
			}
			else if (eventType == K_EVENT_MOUSEBUTTONUP)
			{
				if (dragging)
					dragging = false;

				if (panelWorld->enabled && panelWorld->hovered)
				{
					if (event.getMouseButton() == K_MOUSEBUTTON_LEFT)
					{
						camRot = cameraEditor->getRotation();
					}
				}
			}
			else if (eventType == K_EVENT_MOUSEMOTION)
			{
				if (panelWorld->enabled && panelWorld->hovered)
				{
					if (dragging)
					{
						float deltaX = dragStart.x - event.getMouseX(); // horizontal mouse movement
						float deltaY = dragStart.y - event.getMouseY(); // vertical mouse movement

						if (cameraEditor->getCameraType() == kCameraType::CAMERA_TYPE_FREE)
						{
							cameraEditor->rotateByMouse(camRot, -deltaX, -deltaY);
						}
					}
				}
			}
			else if (eventType == K_EVENT_MOUSEWHEEL)
			{
				if (panelWorld->enabled && panelWorld->hovered)
				{
					cameraEditor->setPosition(cameraEditor->getPosition() + cameraEditor->calculateForward() * 2.0f * event.getMouseWheelY());
				}
			}
			else if (eventType == K_EVENT_KEYDOWN)
			{
				if (event.getKeyButton() == K_KEY_1)
				{
					if (panelWorld->enabled && panelWorld->hovered)
						manager->manipulatorType = ImGuizmo::TRANSLATE;
				}
				else if (event.getKeyButton() == K_KEY_2)
				{
					if (panelWorld->enabled && panelWorld->hovered)
						manager->manipulatorType = ImGuizmo::ROTATE;
				}
				else if (event.getKeyButton() == K_KEY_3)
				{
					if (panelWorld->enabled && panelWorld->hovered)
						manager->manipulatorType = ImGuizmo::SCALE;
				}
				else if (event.getKeyButton() == K_KEY_LALT)
				{
					altPressed = true;
				}
				else if (event.getKeyButton() == K_KEY_LCTRL)
				{
					ctrlPressed = true;
				}
				else if (event.getKeyButton() == K_KEY_LSHIFT)
				{
					shiftPressed = true;
				}
				else if (event.getKeyButton() == K_KEY_DELETE)
				{
					// Only delete scene objects when the World or Hierarchy panel
					// has focus — other panels (script/shader editors, project)
					// handle Delete for their own selections in their draw().
					if (!gui->getWantTextInput() && manager->projectOpened &&
						(panelWorld->focused || panelHierarchy->focused))
						manager->deleteSelectedObjects();
				}
				else if (event.getKeyButton() == K_KEY_Z && ctrlPressed)
				{
					if (!gui->getWantTextInput())
						manager->undoRedo.undo();
				}
				else if (event.getKeyButton() == K_KEY_Y && ctrlPressed)
				{
					if (!gui->getWantTextInput())
						manager->undoRedo.redo();
				}
				else if (event.getKeyButton() == K_KEY_S && ctrlPressed)
				{
					// Ctrl+S routes to whichever editor has focus.
					// Fall back to the project (world) save for everything else
					// — covers World, Hierarchy, Inspector, Project, Console, etc.
					if (gui->getWantTextInput())
					{
						// User is typing — let the input field consume the keystroke.
					}
					else if (panelScriptEditor->focused)
						panelScriptEditor->saveCurrent();
					else if (panelShaderEditor->focused)
						panelShaderEditor->saveCurrent();
					else if (manager->projectOpened)
						manager->saveWorld();
				}
				else if (event.getKeyButton() == K_KEY_D && ctrlPressed)
				{
					// Ctrl+D duplicates the current selection. Fires from the
					// World OR Hierarchy panel — anywhere a scene-object
					// selection is the active context — but stays out of the
					// script/shader editors and any text-input field.
					if (!gui->getWantTextInput() && manager->projectOpened &&
						(panelWorld->focused || panelHierarchy->focused))
						manager->duplicateSelectedObjects();
				}
			}
			else if (eventType == K_EVENT_KEYUP)
			{
				if (event.getKeyButton() == K_KEY_LALT)
				{
					altPressed = false;
				}
				else if (event.getKeyButton() == K_KEY_LCTRL)
				{
					ctrlPressed = false;
				}
				else if (event.getKeyButton() == K_KEY_LSHIFT)
				{
					shiftPressed = false;
				}
			}
		}

		// WASD camera movement — skip when a modifier is held so Ctrl+S /
		// Ctrl+Z / etc. don't double-fire with the camera shortcut.
		if (panelWorld->enabled && panelWorld->focused && !ctrlPressed && !altPressed && !shiftPressed)
		{
			if (event.getKeyDown(K_KEY_W))
			{
				cameraEditor->setPosition(cameraEditor->getPosition() + cameraEditor->calculateForward() * deltaTime * 10.0f);
			}
			else if (event.getKeyDown(K_KEY_S))
			{
				cameraEditor->setPosition(cameraEditor->getPosition() + cameraEditor->calculateForward() * deltaTime * -10.0f);
			}
			else if (event.getKeyDown(K_KEY_A))
			{
				cameraEditor->setPosition(cameraEditor->getPosition() + cameraEditor->calculateRight() * deltaTime * 10.0f);
			}
			else if (event.getKeyDown(K_KEY_D))
			{
				cameraEditor->setPosition(cameraEditor->getPosition() + cameraEditor->calculateRight() * deltaTime * -10.0f);
			}
		}

		renderer->clear();

		// The World panel always renders the main scene from the editor camera.
		// The prefab editor (below) uses its OWN renderer, so opening it never
		// disturbs this view.
		world->setMainCamera(cameraEditor);

		int viewportW = panelWorld->width;
		int viewportH = panelWorld->height;

		// Fix aspect ratio
		if (viewportW > 0 && viewportH > 0)
		{
			// Pass 0 as deltaTime when paused so physics/animations freeze
			float gameDt = panelGame->getEffectiveDeltaTime(deltaTime);

			// Physics only ticks while Playing (not Paused, not Stopped, never
			// during prefab editing). gameDt is already 0 when Paused, but we
			// also gate on play state so a Stopped editor session never builds
			// momentum or does collision callbacks.
			if (panelGame->getPlayState() == GamePlayState::Playing &&
				!manager->prefabEditing && gameDt > 0.0f)
			{
				manager->stepPhysics(gameDt);
				// Dispatch FixedUpdate() then Update()/LateUpdate() to scripts.
				world->fixedUpdateScripts(gameDt);
				world->updateScripts(gameDt);
			}

			// While stopped, watch script source files and recompile on save.
			if (panelGame->getPlayState() == GamePlayState::Stopped)
				manager->pollScriptChanges(deltaTime);

			renderer->render(world, scene, 0, 0, viewportW * 2, viewportH * 2, gameDt, false);

			// Editor scene (grid) always renders in Full mode — debug modes don't apply to it.
			{
				kRenderMode savedMode = renderer->getRenderMode();
				renderer->setRenderMode(kRenderMode::RENDER_MODE_FULL);
				renderer->render(world, sceneEditor, 0, 0, viewportW * 2, viewportH * 2, deltaTime, false);
				renderer->setRenderMode(savedMode);
			}

			// Always render picking pass so click selection and outline are always fresh.
			renderer->renderPickingPass(world, scene, viewportW * 2, viewportH * 2);

			// Outline selected objects (orange)
			if (manager->projectOpened && !manager->selectedObjects.empty())
				renderer->renderOutline(world, scene, manager->selectedObjects,
										kVec4(1.0f, 0.55f, 0.0f, 1.0f), 3.0f);

			// Drag-hover outline (yellow) — applied on top of any selection
			// outline so the user sees which object is under the cursor while
			// they're holding a project asset over the viewport.
			if (manager->projectOpened && !manager->dragHoverObjectUuid.empty())
			{
				std::vector<kString> hoverList = { manager->dragHoverObjectUuid };
				renderer->renderOutline(world, scene, hoverList,
										kVec4(1.0f, 0.85f, 0.0f, 0.85f), 3.0f);
			}

			// Debug shapes for selected lights and cameras
			if (manager->projectOpened && !manager->selectedObjects.empty())
				renderer->renderDebugShapes(world, scene, manager->selectedObjects);

			// Octree debug visualization
			if (manager->projectOpened)
				renderer->renderOctreeDebug(world, scene);

			// Nav-mesh wireframe (blue) for a selected, baked navigation object.
			if (manager->projectOpened && manager->selectedObject &&
				manager->selectedObject->getHasNavMeshDesc())
			{
				kNavMesh *nav = manager->getBakedNavMesh(manager->selectedObject);
				if (nav && nav->isBaked())
				{
					std::vector<kVec3> navLines;
					nav->getDebugLines(navLines);
					renderer->renderDebugLines(world, navLines, kVec3(0.25f, 0.55f, 1.0f));
				}
			}

			// Thumbnail generation (one per frame, main thread only)
			if (manager->projectOpened)
				manager->processThumbnailQueue(panelConsole);
		}

		// Prefab editor renders its isolated scene through its OWN renderer into
		// a separate texture, so the World panel above is left untouched. Its
		// background is dark grey (set once after Manager construction).
		if (manager->prefabEditing && manager->prefabScene && manager->prefabCamera &&
			panelPrefab->width > 0 && panelPrefab->height > 0)
		{
			manager->prefabRenderer.resize(panelPrefab->width * 2, panelPrefab->height * 2);
			manager->prefabRenderer.render(world, manager->prefabScene, manager->prefabCamera);
		}

		// std::cout << panelWorld->width << "," << panelWorld->height << std::endl;

		gui->canvasStart();
		gui->dockSpaceStart("MainDockSpace");

		mainmenu->draw(window, showPanel);

		manager->shaderPreview.active = showPanel.shaderEditor;

		// The World panel stays visible even while the prefab editor is open —
		// the two now render to separate targets.
		bool worldVisible = showPanel.world;
		panelWorld->draw(worldVisible, renderer, cameraEditor);
		panelInspector->draw(showPanel.inspector);
		panelHierarchy->draw(showPanel.hierarchy);
		panelProject->draw(showPanel.project);
		panelConsole->draw(showPanel.console);
		panelShaderEditor->draw(showPanel.shaderEditor);
		panelScriptEditor->draw(showPanel.scriptEditor);
		panelGame->draw(showPanel.game);
		panelPrefab->draw(showPanel.prefab);

		// If there's a need to import assets
		manager->drawImportPopup(panelConsole);
		if (manager->showImportPopup)
			gui->openPopup("Importing Assets...");

		gui->dockSpaceEnd();

		if (showSplashScreen) { splashScreen->show(); showSplashScreen = false; }
		if (splashScreen->isOpen())
			splashScreen->draw();

		mainmenu->drawAbout();

		gui->canvasEnd();

		window->swap();
	}

	// Clean up
	delete splashScreen;
	gui->destroy();
	renderer->destroy();
	window->destroy();
	return 0;
}
