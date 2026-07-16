#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <random>
#include <iostream>
#include <filesystem>
#include <fstream>

#include <sstream>
#include <iomanip>

#include <kemena/kwindow.h>
#include <kemena/kworld.h>
#include <kemena/krenderer.h>
#include <kemena/kscene.h>
#include <kemena/kmeshgenerator.h>
#include <kemena/klight.h>
#include <kemena/kcamera.h>
#include <kemena/kassetmanager.h>
#include <kemena/koffscreenrenderer.h>
#include <kemena/kprefab.h>
#include <kemena/kterrain.h>
#include <kemena/kpackage.h>

#include "commands.h"
#include <portable-file-dialogs.h>
#include <kemena/kguimanager.h>
#include <ImGuizmo.h>

#include "panel_project.h"
#include "panel_hierarchy.h"
#include "panel_console.h"
#include "panel_game.h"
#include "panel_terrain.h"
#include "panel_animator.h"
#include "panel_animation.h"
#include "panel_particle.h"

#ifdef _WIN32
#include <windows.h>
#elif __APPLE__
#include <mach-o/dyld.h>
#elif __linux__
#include <unistd.h>
#include <limits.h>
#endif

using namespace kemena;
namespace fs = std::filesystem;
using json = nlohmann::json;

/**
 * @brief Live shader-preview state shared between PanelShaderEditor and PanelInspector.
 *
 * Holds the most recently compiled shader so the inspector can show a preview
 * that tracks edits made in the shader editor.
 */
struct ShaderPreviewState
{
    bool active = false;
    std::string uuid;
    std::string glslSource;
    std::string shaderType;
    std::string shaderName;
};

/**
 * @brief Describes a single project asset file tracked by the project panel.
 */
struct FileInfo
{
    kString path;
    kString checksum;
    kString type;
};

/**
 * @brief A queued asset conversion/import job (mesh or image).
 */
struct ImportTask
{
    fs::path inputPath;
    fs::path outputPath;
    kString type;
    kString uuid;
    fs::path thumbnailPath;
    bool success = false;
    bool reported = false;
    kString errorMsg;
    std::vector<std::string> warnings;
    bool warningsLogged = false;
};

/**
 * @brief A queued thumbnail render, processed on the main thread after batch import.
 */
struct ThumbnailTask
{
    kString uuid;
    fs::path srcPath;
    fs::path thumbnailPath;
    kString type;
};

/**
 * @brief Lightweight reference to a scene object, used by the hierarchy panel.
 */
struct ObjectInfo
{
    kObject *object;
};

class PanelProject;
class PanelHierarchy;
class PanelConsole;
class PanelScriptEditor;
class PanelGame;
class PanelTerrain;
class PanelAnimator;
class PanelAnimation;
class PanelParticle;

/**
 * @brief Central editor controller for a Kemena3D Studio project.
 *
 * Owns and coordinates the editor's project, world, scene and selection state.
 * Acts as the hub for project lifecycle (new/open/save), asset management and
 * import, object creation and editing, prefab authoring, material assignment,
 * navigation-mesh baking, physics simulation, scripting, and game export. The
 * UI panels delegate their actions here.
 */
class Manager
{
public:
    Manager(kWindow *setWindow, kWorld *setWorld, kRenderer *setRenderer);
    virtual ~Manager();

    void setScene(kScene *s) { scene = s; }
    kScene *getScene() { return scene; }
    kRenderer *getRenderer() { return renderer; }
    kObject *findObjectByUuid(const kString &uuid);
    void deleteSelectedObjects();
    void duplicateSelectedObjects();
    std::vector<TransformState> captureSelectedTransforms();

    // --- Accessors ----------------------------------------------------------
    kWindow *getWindow() { return window; }
    kWorld *getWorld() { return world; }
    kAssetManager *getAssetManager() { return world ? world->getAssetManager() : nullptr; }

    // --- Edit actions -------------------------------------------------------
    void selectAll();
    void deselectAll();
    void invertSelection();

    // --- Object creation ----------------------------------------------------
    void createSceneObject();
    void createEmpty();
    void createMeshPrimitive(kMesh *mesh, const kString &name);
    void createMeshFromFile();
    void createLight(kLightType type);
    void createCamera();
    void createTerrain();
    void createNavMesh();
    void createAudio();

    // --- Publish ------------------------------------------------------------

    /** @brief Per-platform publish configuration. */
    struct PlatformPublishSettings
    {
        bool enabled = true;
        std::string gameName = "Game";
        std::string title = "My Game";
        int width = 1280;
        int height = 720;
        bool fullscreen = false;
        std::string outputDir;
        std::string templateDir;
        std::string iconPath;
        std::string compression = "Default";
    };

    /** @brief Global publish settings for the project. */
    struct PublishSettings
    {
        PlatformPublishSettings platforms[3];  ///< 0=Windows, 1=macOS, 2=Linux
        std::vector<std::string> includeWorlds; ///< World file paths to package
        std::string defaultLevel;               ///< First world to load (relative path)
        std::string templateDir;                ///< Global template dir fallback
    };

    PublishSettings publishSettings;
    bool showPublishDialog = false;
    bool showProjectSettings = false;

    // Legacy export settings (kept for backward compat)
    struct ExportSettings
    {
        int platform = 0;
        std::string gameName = "Game";
        std::string title = "My Game";
        int width = 1280;
        int height = 720;
        bool fullscreen = false;
        std::string outputDir;
        std::string templateDir;
        std::string iconPath;
    };
    ExportSettings exportSettings;
    bool showExportDialog = false;

    /**
     * @brief Legacy export function — builds a distributable game using the
     *        old ExportSettings struct. Kept for backward compatibility with
     *        the Build Settings dialog.
     * @return true on success.
     */
    bool exportGame(const ExportSettings &settings);

    /**
     * @brief Publishes the project for a single platform.
     * @param platformIdx 0=Windows, 1=macOS, 2=Linux.
     * @return true on success.
     */
    bool publishGame(int platformIdx);

    /**
     * @brief Collects asset paths referenced by a world file (JSON).
     * @param worldPath Path to the .world file.
     * @param outAssets Receives relative asset paths (e.g. "Library/ImportedAssets/<uuid>.glb").
     */
    void collectWorldAssetPaths(const fs::path &worldPath, std::set<kString> &outAssets);

    /** @brief Loads publish settings from project config. */
    void loadPublishSettings();

    /** @brief Saves publish settings to project config. */
    void savePublishSettings();

    /// Pending request to show the Animation Editor panel.
    bool pendingOpenAnimationEditor = false;

    // --- Navigation baking --------------------------------------------------
    void bakeNavMesh(kObject *navObj);
    void clearNavMesh(kObject *navObj);
    void clearAllNavMeshes();
    bool isNavMeshBaked(kObject *navObj) const;
    kNavMesh *getBakedNavMesh(kObject *navObj) const;

    // --- Asset creation -----------------------------------------------------
    void selectProjectAssetByPath(const fs::path &filePath);
    void createNewMaterial();
    void createNewFolder();
    void createNewShader();
    void createNewRawShader();
    void createNewScript();
    void createNewLogicGraph();
    void createNewAnimator();
    void createNewAnimation();
    void createNewParticle();
    void createNewAnimationFromMesh(const kString &meshUuid, const fs::path &meshPath);
    void deleteAssets(const std::vector<fs::path> &paths);
    bool renameAsset(const fs::path &oldPath, const kString &newName);
    bool duplicateAsset(const fs::path &srcPath);
    bool moveAsset(const fs::path &srcPath, const fs::path &destDir);
    kString getCurrentDirPath();
    void openFolder(kString name);
    void closeFolder();
    bool newProject();
    bool openProject();
    bool newWorld();
    bool openProjectFromPath(const kString &path);

    // Recent projects
    std::vector<kString> recentProjects;
    void loadRecentProjects();
    void saveRecentProjects();
    void addRecentProject(const kString &path);

    // Per-project config
    void saveProjectConfig();
    fs::path loadLastWorldPath() const;
    void checkAssetChange();
    void refreshWindowTitle();
    void closeEditor();
    void clearWorld(bool forced = false);
    void resetToFreshWorld();
    void deleteObjectRecursive(kObject *node);
    void saveWorld();
    void saveWorldAs(const kString &path);
    void saveWorldAs();
    kString promptWorldSavePath();
    void applyDefaultSkybox(kScene *target);
    void loadWorld(const kString &path);
    void loadDefaultWorldInto(kScene *target);

    // --- Prefabs ------------------------------------------------------------
    bool saveSelectedAsPrefab(const kString &prefabName);
    kObject *instantiatePrefabInScene(const fs::path &prefabPath);
    void editPrefab(const fs::path &prefabPath);
    void closePrefabEditor(bool saveChanges);

    bool prefabEditing = false;
    fs::path editingPrefabPath;
    kPrefab editingPrefab;
    kScene *prefabScene = nullptr;
    kCamera *prefabCamera = nullptr;
    kObject *prefabRoot = nullptr;
    kOffscreenRenderer prefabRenderer{512, 512};

    // --- Audio preview -------------------------------------------------------
    kAudioManager *audioPreviewManager = nullptr;
    void startAudioPreview(kAudioSource &src);
    void stopAudioPreview(kAudioSource &src);

    // --- Drag-and-drop helpers ----------------------------------------------
    kObject *instantiateAssetFromUuid(const kString &assetUuid, const kVec3 &positionHint = kVec3(0));
    fs::path findAssetPathByUuid(const kString &assetUuid);
    void reparentObject(const kString &uuid, const kString &newParentUuid);
    void reorderBefore(const kString &uuid, const kString &siblingUuid);
    kObject *findPrefabInstanceRoot(kObject *obj);
    bool createPrefabFromSelection();
    bool applyPrefabInstance(kObject *instanceRoot);
    void refreshAllPrefabInstances(const kString &prefabUuid);
    void unpackPrefabInstance(kObject *instanceRoot);
    bool applyMaterialToObject(kObject *obj, const fs::path &materialPath,
                               const kString &materialUuid = "");
    kMaterial *buildMaterialFromJson(const nlohmann::json &matJson);
    kTexture2D *getProjectTexture(const kString &textureUuid, const kString &uniformName);
    bool reimportTexture(const kString &textureUuid);
    bool reimportMesh(const kString &meshUuid);
    void processPendingMeshReloads();
    std::vector<kString> pendingMeshReloads;
    kShader *getRawShader(const kString &shaderUuid);
    kString getMaterialShaderSource(const nlohmann::json &matJson);
    void applyDefaultMaterialToObject(kObject *obj);
    void reapplyStoredMaterials();
    std::vector<MaterialSnapshot> captureMaterialSubtree(kObject *root);
    void restoreMaterialSubtree(const std::vector<MaterialSnapshot> &snap);
    void assignImportChildUuids(kObject *root);

    // Material drag-preview state
    kObject *matPreviewObject = nullptr;
    std::vector<MaterialSnapshot> matPreviewSnapshot;
    kString matPreviewSourceUuid;

    std::unordered_map<kString, kTexture2D *> textureCache;
    std::unordered_map<kString, kShader *> shaderCache;

    kString dragHoverObjectUuid;

    // --- Physics simulation -------------------------------------------------
    void startPhysicsSimulation();
    void stopPhysicsSimulation();
    void stepPhysics(float dt);

    kPhysicsManager *physicsManager = nullptr;
    std::vector<kObject *> physicsBodies;
    std::vector<kObject *> characterBodies;

    std::map<kString, kNavMesh *> bakedNavMeshes;

    // --- Scripting ----------------------------------------------------------
    void buildScripts(bool logSummary = false);
    void startScripts();
    void stopScripts();
    void pollScriptChanges(float dt);
    float scriptWatchTimer = 0.0f;

    // Editor path
    fs::path exePath;
    fs::path baseDir;

    // Project info
    kString projectName;
    bool projectOpened = false;
    bool projectSaved = true;

    // Project path
    fs::path projectPath;
    std::vector<kString> currentDir;

    // World info
    kString worldName = "";
    fs::path worldPath;

    PanelProject *panelProject = nullptr;
    PanelHierarchy *panelHierarchy = nullptr;
    PanelScriptEditor *panelScriptEditor = nullptr;
    PanelConsole *panelConsole = nullptr;
    PanelGame *panelGame = nullptr;
    PanelTerrain *panelTerrain = nullptr;
    PanelAnimator *panelAnimator = nullptr;
    PanelAnimation *panelAnimation = nullptr;
    PanelParticle  *panelParticle  = nullptr;

    kTerrainManager *terrainManager = nullptr;
    kCamera *editorCamera = nullptr;
    kCamera *defaultGameCamera = nullptr;

    // Editor-camera orbit state
    kVec3 editorCamOrbitPivot = kVec3(0.0f, 3.5f, 0.0f);
    float editorCamOrbitDistance = 14.0f;
    bool editorCamLoadPending = false;

    std::vector<ImportTask> importQueue;
    std::future<void> importFuture;
    std::atomic<int> filesProcessed{0};
    std::atomic<bool> batchDone{false};
    std::mutex queueMutex;
    bool showingMessageBox = false;

    bool showImportPopup = false;
    std::chrono::steady_clock::time_point importEndTime;
    std::vector<ImportTask> importTasks;
    std::vector<ThumbnailTask> thumbnailQueue;
    kOffscreenRenderer thumbnailRenderer{128, 128};

    void drawImportPopup(PanelConsole *console);
    void processThumbnailQueue(PanelConsole *console);

    std::unordered_map<kString, FileInfo> fileMap;
    std::unordered_map<kString, kString> uuidMap;
    std::unordered_map<kString, ObjectInfo> objectMap;

    std::vector<kString> selectedObjects;
    void selectObject(const kString uuid, bool clearList = false);
    void deselectObject(const kString uuid);

    kObject *selectedObject = nullptr;
    kScene *selectedScene = nullptr;
    bool worldSelected = false;

    ImGuizmo::OPERATION manipulatorType = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE manipulatorMode = ImGuizmo::LOCAL;

    UndoRedoManager undoRedo;
    PivotMode pivotMode = PivotMode::LastSelected;
    ShaderPreviewState shaderPreview;

private:
    kWindow *window;
    kWorld *world;
    kRenderer *renderer;
    kScene *scene = nullptr;
    kString initialWindowTitle;

    kString checkAssetType(const fs::path &p);
    void startBatchImport(const std::vector<ImportTask> &tasks);
};

#endif // FILEMANAGER_H
