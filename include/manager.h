#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <string>
#include <vector>
#include <map>
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

#include "commands.h"
#include <portable-file-dialogs.h>
#include <kemena/kguimanager.h>
#include <ImGuizmo.h>

#include "panel_project.h"
#include "panel_hierarchy.h"
#include "panel_console.h"
#include "panel_game.h"

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

// Live state shared between PanelShaderEditor and PanelInspector
struct ShaderPreviewState
{
    bool        active     = false;
    std::string uuid;
    std::string glslSource;   // compiled GLSL; empty until first compile
    std::string shaderType;   // "Flat", "Phong", "PBR"
    std::string shaderName;   // display name
};

// For project panel
struct FileInfo
{
    kString path; // path/name.ext
    kString checksum;
    kString type; // model, image, prefab, etc.
};

// For converting meshes or images
struct ImportTask
{
    fs::path inputPath;
    fs::path outputPath;
    kString  type;    // mesh, image, etc.
    kString  uuid;
    fs::path thumbnailPath; // where to save uuid.png after import
    bool success  = false;
    bool reported = false;
};

// Queued thumbnail render (processed on main thread after batch import)
struct ThumbnailTask
{
    kString  uuid;
    fs::path srcPath;       // GLB path for mesh, original image path for image
    fs::path thumbnailPath;
    kString  type;          // "mesh" or "image"
};

// For hierarchy panel
struct ObjectInfo
{
    kObject *object; // Pointer to the object
};

class PanelProject;
class PanelHierarchy;
class PanelConsole;
class PanelGame;

class Manager
{
public:
    Manager(kWindow *setWindow, kWorld *setWorld, kRenderer *setRenderer);
    virtual ~Manager();

    void setScene(kScene *s)  { scene = s; }
    kScene    *getScene()     { return scene; }
    kRenderer *getRenderer()  { return renderer; }

    kObject *findObjectByUuid(const kString &uuid);
    void deleteSelectedObjects();
    std::vector<TransformState> captureSelectedTransforms();

    // --- Accessors ----------------------------------------------------------
    kWindow       *getWindow()       { return window; }
    kWorld        *getWorld()        { return world; }
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

    // --- Asset creation -----------------------------------------------------
    void createNewMaterial();
    void deleteAssets(const std::vector<fs::path> &paths);

    kString getCurrentDirPath();

    void openFolder(kString name);
    void closeFolder();

    bool newProject();
    bool openProject();
    bool openProjectFromPath(const kString& path);

    // Recent projects (persisted to <exeDir>/recent_projects.json)
    std::vector<kString> recentProjects;
    void loadRecentProjects();
    void saveRecentProjects();
    void addRecentProject(const kString& path);

    void checkAssetChange();

    void refreshWindowTitle();
    void closeEditor();

    void clearWorld(bool forced = false);
    void deleteObjectRecursive(kObject *node);

    void saveWorld();
    void saveWorldAs(const kString &path);
    void loadWorld(const kString &path);

    // --- Prefabs ------------------------------------------------------------

    /**
     * @brief Saves the currently-selected object subtree as a new .prefab asset
     *        in the current project folder.
     *
     * @param prefabName Display name and base filename for the new prefab.
     * @return true on success.
     */
    bool saveSelectedAsPrefab(const kString &prefabName);

    /**
     * @brief Loads a .prefab file and instantiates it as a new top-level object
     *        in the current scene, with fresh per-node UUIDs.
     *
     * @param prefabPath Filesystem path of the .prefab file to load.
     * @return Pointer to the instance root, or nullptr on failure.
     */
    kObject *instantiatePrefabInScene(const fs::path &prefabPath);

    /**
     * @brief Opens the prefab editor for the given .prefab file. The main world
     *        view is hidden while the prefab editor is active.
     */
    void editPrefab(const fs::path &prefabPath);

    /**
     * @brief Closes the prefab editor. If @p saveChanges is true, serializes
     *        the editor scene back into the .prefab file.
     */
    void closePrefabEditor(bool saveChanges);

    bool prefabEditing = false;        ///< True while the prefab editor panel is active.
    fs::path editingPrefabPath;        ///< Path to the .prefab file being edited.
    kPrefab editingPrefab;             ///< Loaded prefab data while editing.
    kScene *prefabScene = nullptr;     ///< Isolated scene used by the prefab editor.
    kCamera *prefabCamera = nullptr;   ///< Editor camera for the prefab scene.
    kObject *prefabRoot = nullptr;     ///< Root instance of the prefab inside prefabScene.

    // --- Drag-and-drop helpers ----------------------------------------------

    /** @brief Spawns a project-asset (mesh/prefab/audio) into the current scene. */
    kObject *instantiateAssetFromUuid(const kString &assetUuid, const kVec3 &positionHint = kVec3(0));

    /** @brief Locates the on-disk path of a project asset by its UUID. */
    fs::path findAssetPathByUuid(const kString &assetUuid);

    /** @brief Reparents @p uuid under @p newParentUuid. Empty newParentUuid = scene root. */
    void reparentObject(const kString &uuid, const kString &newParentUuid);

    /** @brief Reorders an object within its parent's children list, before @p siblingUuid. */
    void reorderBefore(const kString &uuid, const kString &siblingUuid);

    /** @brief Returns the prefab-instance root that owns @p obj, or nullptr if @p obj is not in a prefab. */
    kObject *findPrefabInstanceRoot(kObject *obj);

    /** @brief Creates a .prefab from the current selection, plus an instance link in the scene. */
    bool createPrefabFromSelection();

    /** @brief Pushes the in-scene state of @p instanceRoot back into its source .prefab template. */
    bool applyPrefabInstance(kObject *instanceRoot);

    /**
     * @brief Replaces every prefab instance of @p prefabUuid in every scene
     *        with a fresh instantiation of the (possibly-just-rewritten)
     *        .prefab template. World transform and parent of each instance
     *        are preserved; per-instance edits are discarded.
     */
    void refreshAllPrefabInstances(const kString &prefabUuid);

    /** @brief Clears prefab linkage from @p instanceRoot and all descendants. */
    void unpackPrefabInstance(kObject *instanceRoot);

    /** @brief Applies a material asset (.mat) to @p obj. */
    bool applyMaterialToObject(kObject *obj, const fs::path &materialPath);

    // --- Material drag-preview state ----------------------------------------
    // While a .mat payload is hovering over a scene object, we swap the material
    // for live preview. If the drop is committed we keep it (with undo); if the
    // user drags away or cancels we restore the original.
    kObject   *matPreviewObject       = nullptr;
    kMaterial *matPreviewOriginal     = nullptr;
    kString    matPreviewSourceUuid;

    // --- Drag-hover highlight ----------------------------------------------
    // While any drag-and-drop is over the World viewport, the panel records
    // the picked object here so the main render loop can paint an outline on
    // it, telling the user which object their cursor is over.
    kString    dragHoverObjectUuid;

    // Editor path and directory
    fs::path exePath;
    fs::path baseDir;

    // Project info
    kString projectName;
    bool projectOpened = false;
    bool projectSaved = true;

    // Project path and directory
    fs::path projectPath;
    std::vector<kString> currentDir;

    // World info
    kString worldName = "";
    fs::path worldPath;
    // kString worldUuid = "";

    PanelProject   *panelProject   = nullptr;
    PanelHierarchy *panelHierarchy = nullptr;
    PanelGame      *panelGame      = nullptr;

    // Camera used by the editor viewport (excluded from game camera candidates)
    kCamera *editorCamera      = nullptr;
    // Explicitly-chosen default camera for the Game panel (nullptr = auto-pick)
    kCamera *defaultGameCamera = nullptr;

    std::vector<ImportTask> importQueue;
    std::future<void> importFuture;
    std::atomic<int> filesProcessed{0};
    std::atomic<bool> batchDone{false};
    std::mutex queueMutex;

    // Prevents asset checks from running while a message box is open, even if the application remains focused
    bool showingMessageBox = false;

    // For batch import
    bool showImportPopup = false;
    std::chrono::steady_clock::time_point importEndTime;

    std::vector<ImportTask>   importTasks;
    std::vector<ThumbnailTask> thumbnailQueue;
    kOffscreenRenderer        thumbnailRenderer{128, 128};

    void drawImportPopup(PanelConsole *console);
    void processThumbnailQueue(PanelConsole *console);

    // Check project files
    std::unordered_map<kString, FileInfo> fileMap; // Key = uuid
    std::unordered_map<kString, kString> uuidMap;  // Reverse lookup, key = filename

    // Check world objects
    std::unordered_map<kString, ObjectInfo> objectMap; // Key = uuid

    std::vector<kString> selectedObjects;
    void selectObject(const kString uuid, bool clearList = false);
    void deselectObject(const kString uuid);

    kObject *selectedObject = nullptr;
    kScene  *selectedScene  = nullptr;
    bool     worldSelected  = false;

    ImGuizmo::OPERATION manipulatorType = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE manipulatorMode = ImGuizmo::LOCAL;

    UndoRedoManager undoRedo;
    PivotMode pivotMode = PivotMode::LastSelected;

    ShaderPreviewState shaderPreview;

private:
    kWindow   *window;
    kWorld    *world;
    kRenderer     *renderer;
    kScene    *scene = nullptr;
    kString    initialWindowTitle;

    // int initialResizeCount = 0;

    // std::map<kString, kString> fileDirty;   // Files that need to be put into fileGUID, or refresh checksum into fileMD5, or regenerate thumbnail etc.
    // kString latestFileUuid = "";

    kString checkAssetType(const fs::path &p);
    void startBatchImport(const std::vector<ImportTask> &tasks);
};

#endif // FILEMANAGER_H
