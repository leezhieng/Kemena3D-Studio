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
#include "panel_terrain.h"
#include "panel_animator.h"
#include "panel_animation.h"
#include "panel_particle.h"
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
	renderer->setClearColor(kVec4(0.2f, 0.2f, 0.2f, 1.0f));

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
	manager->prefabRenderer.setAssetManager(assetManager);
	manager->prefabRenderer.setBackgroundColor(kVec4(0.2f, 0.2f, 0.2f, 1.0f));

	// Thumbnail renderer also loads preview / shadow shaders from resources.
	manager->thumbnailRenderer.setAssetManager(assetManager);

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
	PanelTerrain *panelTerrain = new PanelTerrain(gui, manager);
	manager->panelTerrain = panelTerrain;
	PanelAnimator *panelAnimator = new PanelAnimator(gui, manager);
	manager->panelAnimator = panelAnimator;
	PanelAnimation *panelAnimation = new PanelAnimation(gui, manager);
	manager->panelAnimation = panelAnimation;
	PanelParticle *panelParticle = new PanelParticle(gui, manager);
	manager->panelParticle = panelParticle;
	PanelPrefab *panelPrefab = new PanelPrefab(gui, manager);

	// Route .shader and .prefab double-clicks from the project panel.
	panelProject->onFileDoubleClicked = [&](const std::string &path)
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
			showPanel.prefab = true;
			manager->setEditorMode(Manager::EditorMode::PrefabPreview, path, ".prefab");
			manager->editPrefab(path);
			showPanel.prefab = manager->prefabEditing;
		}
		else if (path.size() >= 9 && path.substr(path.size() - 9) == ".animator")
		{
			showPanel.animatorEditor = true;
			panelAnimator->openFile(path);
			manager->setEditorMode(Manager::EditorMode::AnimatorPreview, path, ".animator");
		}
		else if (path.size() >= 10 && path.substr(path.size() - 10) == ".animation")
		{
			showPanel.animationEditor = true;
			panelAnimation->openFile(path);
		}
		else if (path.size() >= 9 && path.substr(path.size() - 9) == ".particle")
		{
			showPanel.particleEditor = true;
			panelParticle->openFile(path);
			manager->setEditorMode(Manager::EditorMode::ParticlePreview, path, ".particle");
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

	// Default skybox — shared helper so the inspector's "Apply Default
	// Skybox" button and the per-frame scene-change guard can reuse it.
	manager->applyDefaultSkybox(scene);

	// Editor grid
	kMesh *gridMesh = kMeshGenerator::generatePlane();
	gridMesh->setPosition(kVec3(0.0f, -0.01f, 0.0f));
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

	// Middle-mouse pan state. Captures the camera position and orbit pivot at
	// drag start so the per-frame motion handler can slide both along the
	// camera-space right/up axes without drift.
	bool panning = false;
	kVec2 panStart;
	kVec3 panStartCamPos;
	kVec3 panStartPivot;

	// Editor camera orbit state. F-frames-selected updates the pivot; the
	// drag-rotate path keeps the camera at orbitDistance from orbitPivot;
	// the wheel changes orbitDistance (dolly-toward-pivot).
	kVec3 cameraOrbitPivot = kVec3(0.0f, 3.5f, 0.0f);
	float cameraOrbitDistance = glm::length(kVec3(-7.0f, 4.0f, 12.0f) - cameraOrbitPivot);

	// F-focus tween. When the user presses F the camera doesn't snap — it
	// glides into the framed pose over `cameraTweenDuration` seconds with a
	// smoothstep ease. Drag / wheel input cancels the tween.
	const float cameraTweenDuration = 0.5f;
	bool cameraTweenActive = false;
	float cameraTweenT = 0.0f;
	kVec3 cameraTweenFromPos = kVec3(0);
	kVec3 cameraTweenToPos = kVec3(0);
	kQuat cameraTweenFromRot = kQuat(1, 0, 0, 0);
	kQuat cameraTweenToRot = kQuat(1, 0, 0, 0);
	kVec3 cameraTweenToPivot = kVec3(0);
	float cameraTweenToDist = 1.0f;

	bool altPressed = false;
	bool ctrlPressed = false;
	bool shiftPressed = false;

	// Splash screen (shown at startup until a project is chosen or dismissed)
	SplashScreen *splashScreen = new SplashScreen(gui, assetManager, manager);

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

		// A world load (triggered by a menu action during a previous frame's
		// panel draw) restored the editor camera pose and staged its orbit
		// framing. Adopt it into our live locals BEFORE the mirror below, or the
		// mirror would overwrite the restored pivot/distance with stale values
		// and the first orbit drag would snap the camera. Cancel any focus tween
		// so nothing fights the restore.
		if (manager->editorCamLoadPending)
		{
			cameraOrbitPivot = manager->editorCamOrbitPivot;
			cameraOrbitDistance = manager->editorCamOrbitDistance;
			camRot = cameraEditor->getRotation();
			cameraTweenActive = false;
			manager->editorCamLoadPending = false;
		}

		// Keep the manager's copy of the editor-camera orbit state current so a
		// File>Save handled during this frame persists the live viewpoint.
		manager->editorCamOrbitPivot = cameraOrbitPivot;
		manager->editorCamOrbitDistance = cameraOrbitDistance;

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
								std::string ext = src.extension().string();
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
					if (event.getMouseButton() == K_MOUSEBUTTON_LEFT && altPressed && panelWorld->focused)
					{
						dragging = true;

						dragStart.x = event.getMouseX();
						dragStart.y = event.getMouseY();

						camRot = cameraEditor->getRotation();
					}
					else if (event.getMouseButton() == K_MOUSEBUTTON_MIDDLE && panelWorld->focused)
					{
						panning = true;
						panStart.x = event.getMouseX();
						panStart.y = event.getMouseY();
						panStartCamPos = cameraEditor->getPosition();
						panStartPivot = cameraOrbitPivot;
						// User is steering — cancel any in-flight F tween.
						cameraTweenActive = false;
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

						// Disable terrain sculpt mode when clicking a non-terrain
						// object (or empty space) in the scene viewport.
						if (manager->panelTerrain && manager->panelTerrain->sculpt.active)
						{
							bool isTerrainClick = false;
							if (picked != nullptr)
							{
								kMesh *mesh = dynamic_cast<kMesh *>(picked);
								if (mesh && mesh->getSerializeType() == "terrain")
									isTerrainClick = true;
							}
							if (!isTerrainClick)
								manager->panelTerrain->sculpt.active = false;
						}

						if (picked != nullptr)
						{
							manager->worldSelected = false;
							manager->selectedScene = nullptr;
							manager->selectedObject = picked;
							manager->selectObject(picked->getUuid(), !shiftPressed);
							if (manager->panelProject != nullptr)
								manager->panelProject->clearSelection();
						}
						else if (!shiftPressed)
						{
							manager->worldSelected = false;
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

				if (panning && event.getMouseButton() == K_MOUSEBUTTON_MIDDLE)
					panning = false;

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
							// Orient the camera, then re-anchor it so it stays
							// orbitDistance from cameraOrbitPivot — turns
							// rotate-in-place into orbit-around-pivot.
							// User is steering — cancel any in-flight F tween.
							cameraTweenActive = false;
							cameraEditor->rotateByMouse(camRot, -deltaX, -deltaY);
							kVec3 fwd = cameraEditor->calculateForward();
							cameraEditor->setPosition(cameraOrbitPivot - fwd * cameraOrbitDistance);
						}
					}
					else if (panning)
					{
						// Pan slides both the camera and its orbit pivot in the
						// view plane. Speed scales with orbit distance so the
						// world tracks the cursor at a consistent rate
						// regardless of zoom level.
						float deltaX = event.getMouseX() - panStart.x;
						float deltaY = event.getMouseY() - panStart.y;

						float panScale = cameraOrbitDistance * 0.0025f;
						kVec3 right = cameraEditor->calculateRight();
						kVec3 up = cameraEditor->calculateUp();
						kVec3 offset = (right * deltaX + up * deltaY) * panScale;

						cameraEditor->setPosition(panStartCamPos + offset);
						cameraOrbitPivot = panStartPivot + offset;
					}
				}
			}
			else if (eventType == K_EVENT_MOUSEWHEEL)
			{
				if (panelWorld->enabled && panelWorld->hovered)
				{
					// User is zooming — cancel any in-flight F tween.
					cameraTweenActive = false;
					// Wheel zooms by shrinking / growing the orbit distance.
					// Floor avoids collapsing the camera onto the pivot.
					float wheel = event.getMouseWheelY();
					cameraOrbitDistance = std::max(0.1f, cameraOrbitDistance - wheel * 2.0f);
					kVec3 fwd = cameraEditor->calculateForward();
					cameraEditor->setPosition(cameraOrbitPivot - fwd * cameraOrbitDistance);
				}
			}
			else if (eventType == K_EVENT_KEYDOWN)
			{
				if (event.getKeyButton() == K_KEY_W)
				{
					if (panelWorld->enabled && panelWorld->hovered)
						manager->manipulatorType = ImGuizmo::TRANSLATE;
				}
				else if (event.getKeyButton() == K_KEY_E)
				{
					if (panelWorld->enabled && panelWorld->hovered)
						manager->manipulatorType = ImGuizmo::ROTATE;
				}
				else if (event.getKeyButton() == K_KEY_R)
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
					else if (panelAnimator->focused)
						panelAnimator->saveCurrent();
					else if (panelAnimation->focused)
						panelAnimation->saveCurrent();
					else if (panelParticle->focused)
						panelParticle->saveCurrent();
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
				else if (event.getKeyButton() == K_KEY_F && !ctrlPressed)
				{
					// F frames the editor camera on the selected object and
					// sets the orbit pivot to its centre, so alt+drag from
					// now on rotates around it. Only fires from the World or
					// Hierarchy panel — anywhere else, F is free for shortcuts.
					if (!gui->getWantTextInput() && manager->projectOpened &&
						(panelWorld->focused || panelHierarchy->focused) &&
						!manager->selectedObjects.empty())
					{
						// Union AABB across all selected objects. Mesh nodes
						// contribute their world AABB; non-mesh nodes (lights,
						// cameras, empties) contribute a small bounding cube
						// around their position so they still pull the framing
						// rectangle without dominating it.
						kVec3 unionMin(std::numeric_limits<float>::infinity());
						kVec3 unionMax(-std::numeric_limits<float>::infinity());
						int count = 0;
						for (const kString &uuid : manager->selectedObjects)
						{
							kObject *o = manager->findObjectByUuid(uuid);
							if (!o)
								continue;
							if (o->getType() == NODE_TYPE_MESH)
							{
								kAABB box = ((kMesh *)o)->getWorldAABB();
								unionMin = glm::min(unionMin, box.min);
								unionMax = glm::max(unionMax, box.max);
							}
							else
							{
								kVec3 p = o->getGlobalPosition();
								unionMin = glm::min(unionMin, p - kVec3(0.25f));
								unionMax = glm::max(unionMax, p + kVec3(0.25f));
							}
							++count;
						}

						if (count > 0)
						{
							kVec3 target = (unionMin + unionMax) * 0.5f;
							float radius = glm::length((unionMax - unionMin) * 0.5f);
							if (radius < 0.5f)
								radius = 0.5f;

							float fovRad = glm::radians(cameraEditor->getFOV());
							float distance = (radius / std::sin(fovRad * 0.5f)) * 1.4f;

							// Keep the current viewing direction, but rebuild
							// the rotation cleanly from forward + world-up so
							// accumulated roll from previous drags is removed
							// — that's the "tilt after focus" the user saw.
							kVec3 forward = glm::normalize(cameraEditor->calculateForward());
							kVec3 worldUp = kVec3(0.0f, 1.0f, 0.0f);
							// If the user is staring straight up/down, give
							// quatLookAt a non-degenerate up vector.
							if (std::abs(glm::dot(forward, worldUp)) > 0.999f)
								worldUp = kVec3(0.0f, 0.0f, 1.0f);
							kQuat targetRot = glm::quatLookAt(forward, worldUp);
							kVec3 targetPos = target - forward * distance;

							// Start the tween — interpolation happens in the
							// per-frame block below.
							cameraTweenFromPos = cameraEditor->getPosition();
							cameraTweenToPos = targetPos;
							cameraTweenFromRot = cameraEditor->getRotation();
							cameraTweenToRot = targetRot;
							cameraTweenToPivot = target;
							cameraTweenToDist = distance;
							cameraTweenT = 0.0f;
							cameraTweenActive = true;

							// Update the look-at marker immediately so
							// other code (camera frustum debug, etc.) sees the
							// new focus point without waiting for the tween.
							cameraEditor->setLookAt(target);
						}
					}
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

		// (WASD camera movement removed — selection-driven framing via F is
		// the preferred navigation now. Alt+drag orbits, wheel zooms.)

		// Camera focus tween — F-frame-selected populates the from/to state
		// above; we advance the parameter here and write the interpolated
		// pose every frame. Smoothstep gives a soft start and stop.
		if (cameraTweenActive)
		{
			cameraTweenT += deltaTime / cameraTweenDuration;
			float u = std::min(cameraTweenT, 1.0f);
			float ease = u * u * (3.0f - 2.0f * u);

			kVec3 pos = glm::mix(cameraTweenFromPos, cameraTweenToPos, ease);
			kQuat rot = glm::slerp(cameraTweenFromRot, cameraTweenToRot, ease);
			cameraEditor->setPosition(pos);
			cameraEditor->setRotation(rot);

			if (cameraTweenT >= 1.0f)
			{
				cameraOrbitPivot = cameraTweenToPivot;
				cameraOrbitDistance = cameraTweenToDist;
				cameraTweenActive = false;
			}
		}

		renderer->clear();

		// Re-sync the local scene pointer from the manager. Manager::loadWorld
		// destroys the original game scene and creates new ones, so the local
		// here could otherwise dangle after a project open — which silently
		// produces garbage for things like scene->getShadowsEnabled() below.
		// When the pointer changes we also re-apply the default skybox so
		// the editor view doesn't end up empty, and re-arm the shadow
		// allocator in case it skipped earlier.
		{
			static kScene *lastSyncedScene = nullptr;
			if (manager->getScene() != nullptr)
			{
				scene = manager->getScene();
				if (scene != lastSyncedScene)
				{
					if (scene->getSkyboxMaterial() == nullptr)
						manager->applyDefaultSkybox(scene);
					renderer->setEnableShadow(scene->getShadowsEnabled());
		
				// In preview mode, swap the render target to the preview world.
				bool isPreviewMode = (manager->activeMode != Manager::EditorMode::GameWorld);
				if (isPreviewMode && manager->previewWorld)
				{
					world = manager->previewWorld;
					scene = manager->getActiveScene();
					// Use preview camera for the editor camera during preview.
					if (manager->previewCamera)
						cameraEditor = manager->previewCamera;
				}
				else if (!isPreviewMode)
				{
					// Restore game world pointers.
					world = manager->getWorld();
					scene = manager->getScene();
					cameraEditor = manager->editorCamera;
				}
					renderer->setShadowBias(scene->getShadowBias());
					renderer->setShadowNormalBias(scene->getShadowNormalBias());
					renderer->setShadowSoftness(scene->getShadowSoftness());
					// Resolution allocates the shadow texture, so only push it
					// on scene change (not every frame).
					if (renderer->getShadowResolution() != scene->getShadowMapResolution())
						renderer->setShadowResolution(scene->getShadowMapResolution());
					lastSyncedScene = scene;
				}
			}
		}

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

			// Game-specific logic — only in GameWorld mode.
			if (!isPreviewMode)
			{
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

				// Mirror the active scene's shadow toggle into the renderer.
				// setEnableShadow is lazy/idempotent in the SDK so this is cheap.
				renderer->setEnableShadow(scene ? scene->getShadowsEnabled() : true);
				if (scene)
				{
					renderer->setShadowBias(scene->getShadowBias());
					renderer->setShadowNormalBias(scene->getShadowNormalBias());
					renderer->setShadowSoftness(scene->getShadowSoftness());
					if (renderer->getShadowResolution() != scene->getShadowMapResolution())
						renderer->setShadowResolution(scene->getShadowMapResolution());
				}
			}
			else
			{
				// Preview mode: enable shadows on the preview scene.
				if (scene)
					renderer->setEnableShadow(scene->getShadowsEnabled());
			}

			renderer->render(world, scene, 0, 0, viewportW * 2, viewportH * 2, gameDt, false);

			// Editor scene (grid) only in GameWorld mode.
			if (!isPreviewMode)
			{
				kRenderMode savedMode = renderer->getRenderMode();
				renderer->setRenderMode(kRenderMode::RENDER_MODE_FULL);
				renderer->render(world, sceneEditor, 0, 0, viewportW * 2, viewportH * 2, deltaTime, false);
				renderer->setRenderMode(savedMode);
			}

			// Picking pass — only in GameWorld mode.
			if (!isPreviewMode)
				renderer->renderPickingPass(world, scene, viewportW * 2, viewportH * 2);

			// Outline / debug shapes — only in GameWorld mode.
			if (!isPreviewMode)
			{
				// Outline selected objects (orange)
				if (manager->projectOpened && !manager->selectedObjects.empty())
					renderer->renderOutline(world, scene, manager->selectedObjects,
											kVec4(1.0f, 0.55f, 0.0f, 1.0f), 3.0f);

				// Drag-hover outline (yellow)
				if (manager->projectOpened && !manager->dragHoverObjectUuid.empty())
				{
					std::vector<kString> hoverList = {manager->dragHoverObjectUuid};
					renderer->renderOutline(world, scene, hoverList,
											kVec4(1.0f, 0.85f, 0.0f, 0.85f), 3.0f);
				}

				// Debug shapes for selected lights and cameras
				if (manager->projectOpened && !manager->selectedObjects.empty())
					renderer->renderDebugShapes(world, scene, manager->selectedObjects);

				// Octree debug visualization
				if (manager->projectOpened)
					renderer->renderOctreeDebug(world, scene);
			}

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
			{
				manager->processThumbnailQueue(panelConsole);
				// Rebuild scene instances of any re-imported mesh (deferred from
				// the inspector's Apply so teardown never happens mid panel-draw).
				manager->processPendingMeshReloads();
			}
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
		// Consume pending Animation Editor open request from inspector
		if (manager->pendingOpenAnimationEditor)
		{
			showPanel.animationEditor = true;
			manager->pendingOpenAnimationEditor = false;
		}

		panelAnimator->draw(showPanel.animatorEditor);
		panelAnimation->draw(showPanel.animationEditor);
		panelParticle->draw(showPanel.particleEditor);

		// If there's a need to import assets
		manager->drawImportPopup(panelConsole);
		if (manager->showImportPopup)
			gui->openPopup("Importing Assets...");

		gui->dockSpaceEnd();

		if (showSplashScreen)
		{
			splashScreen->show();
			showSplashScreen = false;
		}
		if (splashScreen->isOpen())
			splashScreen->draw();

		mainmenu->drawAbout();

		gui->canvasEnd();

		window->swap();
	}

	// Clean up
	delete panelAnimation;
	delete panelParticle;
	delete panelAnimator;
	delete panelTerrain;
	delete splashScreen;
	gui->destroy();
	renderer->destroy();
	window->destroy();
	return 0;
}
