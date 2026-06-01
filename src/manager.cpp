#include "manager.h"

#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#define STB_IMAGE_RESIZE2_IMPLEMENTATION
#include <stb/stb_image_resize2.h>

#include <kemena/kmesh.h>
#include <kemena/klight.h>
#include <kemena/kcamera.h>
#include <kemena/kmeshgenerator.h>
#include <kemena/kshadernode.h>
#include <kemena/kscriptgraph.h>

#include <set>
#include <functional>
#include <ctime>
#include <cstdlib>

namespace fs = std::filesystem;

Manager::Manager(kWindow* setWindow, kWorld* setWorld, kRenderer* setRenderer)
{
	window = setWindow;
	world = setWorld;
	renderer = setRenderer;
	initialWindowTitle = window->getWindowTitle();

	try
	{
#ifdef _WIN32
		char buffer[MAX_PATH];
		DWORD len = GetModuleFileNameA(NULL, buffer, MAX_PATH);
		exePath = kString(buffer, len);
#elif __APPLE__
		char buffer[PATH_MAX];
		uint32_t size = sizeof(buffer);
		if (_NSGetExecutablePath(buffer, &size) == 0)
			exePath = fs::canonical(buffer).string();
		else
			exePath.clear();
#elif __linux__
		char buffer[PATH_MAX];
		ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
		if (len != -1)
		{
			buffer[len] = '\0';
			exePath = fs::canonical(buffer).string();
		}
		else
		{
			exePath.clear();
		}
#else
		exePath = fs::current_path().string(); // fallback for unknown platforms
#endif

		if (!exePath.empty())
			baseDir = fs::path(exePath).parent_path().string();
		else
			baseDir = fs::current_path().string(); // fallback

	}
	catch (const fs::filesystem_error& e)
	{
		std::cerr << "Error resolving executable path: " << e.what() << std::endl;
		exePath.clear();
		baseDir = fs::current_path().string(); // fallback
	}

	loadRecentProjects();
}

Manager::~Manager() = default;

kString Manager::getCurrentDirPath()
{
	fs::path path = projectPath;

	for (const auto& dir : currentDir)
	{
		path /= dir;
	}

	return path.string();
}

void Manager::openFolder(kString name)
{
	currentDir.push_back(name);
}

void Manager::closeFolder()
{
	if (currentDir.size() > 1)
	{
		currentDir.pop_back();
	}
}

bool Manager::newProject()
{
	// Check if project is saved
	if (!projectSaved)
	{
		showingMessageBox = true;

		auto result = pfd::message(
						  "Unsaved Changes",
						  "You have unsaved changes. Do you want to create a new project without saving?",
						  pfd::choice::yes_no,
						  pfd::icon::warning
					  ).result();

		if (result == pfd::button::no)
			return false;
	}

	auto path = pfd::select_folder("Select project folder").result();

	if (path.empty())
	{
		return false;
	}

	if (!fs::exists(path) || !fs::is_directory(path))
	{
		kString msg = "Directory does not exist:\n" + path;

		pfd::message(
			"Invalid Directory",     // title
			msg,                     // message
			pfd::choice::ok,         // only an OK button
			pfd::icon::warning       // warning icon
		).result();

		return false;
	}

	// Create project directory
	fs::path fullPath = fs::path(path);

	std::error_code ec;

	// Create required subfolders
	fs::create_directories(fullPath / "Assets", ec);
	fs::create_directories(fullPath / "Library", ec);
	fs::create_directories(fullPath / "Library" / "Metadata", ec);
	fs::create_directories(fullPath / "Library" / "Thumbnails", ec);
	fs::create_directories(fullPath / "Library" / "ImportedAssets", ec);
	fs::create_directories(fullPath / "Config", ec);

	kString msg = "Project created at: " + fullPath.string();

	pfd::message(
		"Success",     // title
		msg,                     // message
		pfd::choice::ok,         // only an OK button
		pfd::icon::warning       // warning icon
	).result();

	// Extract the folder name
	projectName = fullPath.filename().string();
	projectOpened = true;
	projectSaved = false;
	refreshWindowTitle();

	if (!renderer->getEnableObjectPicking())
	    renderer->setEnableObjectPicking(true);

	projectPath = path;
	currentDir.clear();
	currentDir.push_back("Assets");

	// TODO: Create project config file
	checkAssetChange();

	if (panelProject != nullptr)
	{
		panelProject->refreshTreeList();
		panelProject->refreshThumbnailList();
	}

	// WIP: Load scenes of the world

	if (panelHierarchy != nullptr)
		panelHierarchy->refreshList();

	addRecentProject(projectPath.string());

	return true;
}

bool Manager::openProject()
{
	showingMessageBox = true;

	// Check if project is saved
	if (!projectSaved)
	{
		auto result = pfd::message(
						  "Unsaved Changes",
						  "You have unsaved changes. Do you want to open a new project without saving?",
						  pfd::choice::yes_no,
						  pfd::icon::warning
					  ).result();

		if (result == pfd::button::no)
			return false;
	}

	auto path = pfd::select_folder("Select project folder").result();

	if (path.empty())
	{
		return false;
	}

	if (!fs::exists(path) || !fs::is_directory(path))
	{
		kString msg = "Directory does not exist:\n" + path;

		pfd::message(
			"Invalid Directory",     // title
			msg,                     // message
			pfd::choice::ok,         // only an OK button
			pfd::icon::warning       // warning icon
		).result();

		return false;
	}

	fs::path fullPath = fs::path(path);

	fs::path assetsPath  = fullPath / "Assets";
	fs::path libraryPath = fullPath / "Library";
	fs::path configPath  = fullPath / "Config";

	if (!(fs::exists(assetsPath) && fs::exists(libraryPath) && fs::exists(configPath)))
	{
		kString msg = "Failed to open project. Invalid directory structure.\n";

		pfd::message(
			"Invalid Directory",     // title
			msg,                     // message
			pfd::choice::ok,         // only an OK button
			pfd::icon::warning       // warning icon
		).result();

		return false;
	}

	// Open project successful

	// Extract the folder name
	projectName = fullPath.filename().string();
	projectOpened = true;
	projectSaved = false;
	refreshWindowTitle();

	if (!renderer->getEnableObjectPicking())
	    renderer->setEnableObjectPicking(true);

	projectPath = path;
	currentDir.clear();
	currentDir.push_back("Assets");

	// Create other essential folders if don't exist
	std::error_code ec;

	fs::create_directories(fullPath / "Assets", ec);
	fs::path metadataPath = fullPath / "Library" / "Metadata";
	if (!(fs::exists(metadataPath)))
		fs::create_directories(fullPath / "Library" / "Metadata", ec);

	fs::path thumbnailsPath = fullPath / "Library" / "Thumbnails";
	if (!(fs::exists(thumbnailsPath)))
		fs::create_directories(fullPath / "Library" / "Thumbnails", ec);

	fs::path importedAssetsPath = fullPath / "Library" / "ImportedAssets";
	if (!(fs::exists(importedAssetsPath)))
		fs::create_directories(fullPath / "Library" / "ImportedAssets", ec);

	// TODO: check project config file
	// Need this to false to check assets
	showingMessageBox = false;
	checkAssetChange();

	if (panelProject != nullptr)
	{
		panelProject->refreshTreeList();
		panelProject->refreshThumbnailList();
	}

	// WIP: Load scenes of the world

	if (panelHierarchy != nullptr)
		panelHierarchy->refreshList();

	// If a previous session recorded a last-opened world, load it now.
	{
		fs::path last = loadLastWorldPath();
		if (!last.empty())
			loadWorld(last.string());
	}

	addRecentProject(projectPath.string());

	return true;
}

bool Manager::openProjectFromPath(const kString& path)
{
	if (path.empty()) return false;

	fs::path fullPath = fs::path(path);

	if (!fs::exists(fullPath) || !fs::is_directory(fullPath))
		return false;

	if (!(fs::exists(fullPath / "Assets") &&
		  fs::exists(fullPath / "Library") &&
		  fs::exists(fullPath / "Config")))
		return false;

	projectName   = fullPath.filename().string();
	projectOpened = true;
	projectSaved  = false;
	refreshWindowTitle();

	if (!renderer->getEnableObjectPicking())
		renderer->setEnableObjectPicking(true);

	projectPath = path;
	currentDir.clear();
	currentDir.push_back("Assets");

	std::error_code ec;
	fs::create_directories(fullPath / "Assets", ec);

	if (!fs::exists(fullPath / "Library" / "Metadata"))
		fs::create_directories(fullPath / "Library" / "Metadata", ec);
	if (!fs::exists(fullPath / "Library" / "Thumbnails"))
		fs::create_directories(fullPath / "Library" / "Thumbnails", ec);
	if (!fs::exists(fullPath / "Library" / "ImportedAssets"))
		fs::create_directories(fullPath / "Library" / "ImportedAssets", ec);

	showingMessageBox = false;
	checkAssetChange();

	if (panelProject != nullptr)
	{
		panelProject->refreshTreeList();
		panelProject->refreshThumbnailList();
	}

	if (panelHierarchy != nullptr)
		panelHierarchy->refreshList();

	// Auto-load the previously-opened world for this project (if any).
	{
		fs::path last = loadLastWorldPath();
		if (!last.empty())
			loadWorld(last.string());
	}

	addRecentProject(path);

	return true;
}

void Manager::loadRecentProjects()
{
	fs::path configFile = fs::path(baseDir) / "recent_projects.json";
	if (!fs::exists(configFile)) return;

	try
	{
		std::ifstream f(configFile);
		json j = json::parse(f);
		recentProjects.clear();
		// Filter out empties and duplicates — older builds occasionally
		// recorded a blank entry when project creation was interrupted.
		std::set<kString> seen;
		for (auto& entry : j["projects"])
		{
			kString p = entry.get<std::string>();
			if (p.empty() || !seen.insert(p).second)
				continue;
			recentProjects.push_back(std::move(p));
		}
	}
	catch (...) {}
}

void Manager::saveRecentProjects()
{
	fs::path configFile = fs::path(baseDir) / "recent_projects.json";

	try
	{
		json j;
		j["projects"] = recentProjects;
		std::ofstream f(configFile);
		f << j.dump(4);
	}
	catch (...) {}
}

void Manager::addRecentProject(const kString& path)
{
	if (path.empty()) return;

	// Copy first: callers (the splash) frequently pass a reference straight
	// out of `recentProjects` — erase() below would otherwise invalidate the
	// referent, and the subsequent insert() would read garbage and store an
	// empty / corrupted entry. That's why the list went empty after opening
	// a recent project.
	kString p = path;

	auto it = std::find(recentProjects.begin(), recentProjects.end(), p);
	if (it != recentProjects.end())
		recentProjects.erase(it);

	recentProjects.insert(recentProjects.begin(), p);

	if (recentProjects.size() > 8)
		recentProjects.resize(8);

	saveRecentProjects();
}

void Manager::checkAssetChange()
{
	if (projectOpened && !showingMessageBox)
	{
		bool anyChanges = false;
		fileMap.clear();
		importTasks.clear();
		fs::path libraryFolder = projectPath / "Library";
		fs::path assetsJsonFile = libraryFolder / "assets.json";
		fs::path assetsPath = projectPath / "Assets";

		// Check whether assets.json exist or not
		try
		{
			if (!fs::exists(libraryFolder))
			{
				fs::create_directories(libraryFolder);
				std::cout << "Created Library folder: " << libraryFolder << "\n";
			}

			if (!fs::exists(assetsJsonFile))
			{
				json j;
				j["files"] = json::array();

				std::ofstream ofs(assetsJsonFile);
				ofs << j.dump(4); // pretty print
				ofs.close();

				std::cout << "Created assets.json at: " << assetsJsonFile << "\n";
			}
			else
			{
				std::cout << "assets.json already exists.\n";
			}
		}
		catch (const std::exception& e)
		{
			std::cerr << "Error: " << e.what() << "\n";
			return;
		}

		// Read assets.json
		// Check whether the files exist or not
		json j;
		// Check if file is empty
		if (fs::is_empty(assetsJsonFile))
		{
			std::cout << "assets.json is empty, reinitializing...\n";
			j["files"] = json::array();

			std::ofstream ofs(assetsJsonFile);
			ofs << j.dump(4);
			ofs.close();
		}
		else
		{
			std::ifstream ifs(assetsJsonFile);
			try
			{
				ifs >> j;
			}
			catch (const std::exception& e)
			{
				std::cerr << "Failed to parse assets.json: " << e.what() << "\n";
				j["files"] = json::array(); // reset to valid state
			}
		}

		if (!j.contains("files") || !j["files"].is_array())
		{
			std::cerr << "Invalid assets.json format\n";
			return;
		}

		if (!j["files"].empty())
		{
			for (auto it = j["files"].begin(); it != j["files"].end();)
			{
				kString uuid = (*it)["uuid"].get<kString>();
				kString relativePath = (*it)["name"].get<kString>();
				kString checksum = (*it).value("checksum", "");
				kString type = (*it)["type"].get<kString>();

				fs::path filePath = assetsPath / relativePath;

				if (!fs::exists(filePath))
				{
					std::cout << "Missing: " << relativePath << " (removed from list)\n";
					it = j["files"].erase(it);
					anyChanges = true;

					// Delete metadata, thumbnail and imported asset?

					// Delete metadata
					fs::path metadataFile = libraryFolder / "Metadata" / (uuid + ".json");
					if (fs::exists(metadataFile))
					{
						try
						{
							if (fs::remove(metadataFile))
							{
								std::cout << "Deleted file: " << metadataFile << "\n";
							}
							else
							{
								std::cout << "Failed to delete file (unknown reason): " << metadataFile << "\n";
							}
						}
						catch (const fs::filesystem_error& e)
						{
							std::cerr << "Error deleting file: " << e.what() << "\n";
						}
					}
					else
					{
						std::cout << "File does not exist: " << metadataFile << "\n";
					}

					// Delete imported assets and thumbnail
					kString assetExt;
					if (type == "mesh")  assetExt = ".glb";
					else if (type == "image") assetExt = ".dds";

					auto tryDelete = [](const fs::path &p) {
						if (!fs::exists(p)) return;
						try { fs::remove(p); std::cout << "Deleted: " << p << "\n"; }
						catch (const fs::filesystem_error &e) { std::cerr << "Error deleting: " << e.what() << "\n"; }
					};

					if (!assetExt.empty())
						tryDelete(libraryFolder / "ImportedAssets" / (uuid + assetExt));

					tryDelete(libraryFolder / "Thumbnails" / (uuid + ".png"));

					continue;
				}

				// Fill struct and store in map
				FileInfo info{ relativePath, checksum, type };
				fileMap[uuid] = info;

				++it;
			}
		}

		// Build a reverse lookup from path -> uuid for convenience
		uuidMap.clear();
		for (const auto& [uuid, info] : fileMap)
			uuidMap[info.path] = uuid;

		// Check all files in the Assets folder
		for (auto &p : fs::recursive_directory_iterator(assetsPath))
		{
			if (!p.is_regular_file()) continue;

			kString relativePath = fs::relative(p.path(), assetsPath).generic_string();
			kString checksum = generateFileChecksum(p.path().string());

			kString fileUuid;
			kString fileType;
			bool needImport = false;

			// Check with assets.json
			auto it = uuidMap.find(relativePath);
			if (it == uuidMap.end())
			{
				// New file
				kString uuid = generateUuid();
				kString type = checkAssetType(p.path());

				fileUuid = uuid;
				fileType = type;
				needImport = true;  // Need import

				FileInfo info{ relativePath, checksum, type };
				fileMap[uuid] = info;
				uuidMap[relativePath] = uuid;

				json newEntry =
				{
					{"name", relativePath},
					{"uuid", uuid},
					{"checksum", checksum},
					{"type", type}
				};
				j["files"].push_back(newEntry);

				std::cout << "New file added: " << relativePath << "\n";
			}
			else
			{
				// Existing file, check checksum
				kString uuid = it->second;
				FileInfo& info = fileMap[uuid];

				fileUuid = uuid;
				fileType = info.type;

				// Different checksum
				if (info.checksum != checksum)
				{
					std::cout << "File changed: " << relativePath << "\n";
					info.checksum = checksum;
					needImport = true;  // Need import

					// Update JSON as well
					for (auto& entry : j["files"])
					{
						if (entry["uuid"] == uuid)
						{
							entry["checksum"] = checksum;
							break;
						}
					}
				}
			}

			if (!fileUuid.empty() && !fileType.empty())
			{
				// Determine dest extension
				kString uuidExt;
				if (fileType == "mesh")       uuidExt = ".glb";
				else if (fileType == "image") uuidExt = ".dds";

				fs::path srcFullPath   = assetsPath / relativePath;
				fs::path destDir       = libraryFolder / "ImportedAssets";
				fs::path destFile      = destDir / (fileUuid + uuidExt);
				fs::path thumbnailPath = libraryFolder / "Thumbnails" / (fileUuid + ".png");
				fs::path metaPath      = libraryFolder / "Metadata"   / (fileUuid + ".json");

				// Write / overwrite metadata whenever it's missing or the file changed
				if (!fs::exists(metaPath) || needImport)
				{
					nlohmann::json meta;
					meta["type"]         = fileType;
					meta["last_change"]  = static_cast<int64_t>(std::time(nullptr));
					meta["src_checksum"] = checksum;
					meta["src_path"]     = fs::relative(srcFullPath.parent_path(), projectPath).generic_string();
					meta["src_file"]     = srcFullPath.filename().generic_string();
					meta["dest_path"]    = fs::relative(destDir, projectPath).generic_string();
					meta["dest_file"]    = fileUuid + uuidExt;

					std::ofstream mf(metaPath);
					if (mf) { mf << meta.dump(4); mf.close(); }
					else std::cerr << "Failed to write metadata: " << metaPath << "\n";
				}

				// Queue conversion if imported asset is missing or source changed
				if (!fs::exists(destFile))
					needImport = true;

				// If the thumbnail is missing, force a re-import/re-generation.
				if (!fs::exists(thumbnailPath))
				{
					if (fileType == "mesh" && !uuidExt.empty())
					{
						// For meshes, delete the imported asset and metadata to trigger full re-import.
						auto tryRemove = [](const fs::path &p) {
							if (!fs::exists(p)) return;
							std::error_code ec;
							fs::remove(p, ec);
							if (!ec) std::cout << "Removed stale Library file: " << p << "\n";
							else     std::cerr << "Failed to remove: " << p << " (" << ec.message() << ")\n";
						};
						tryRemove(destFile);
						tryRemove(metaPath);
						needImport = true;
					}
					else if (fileType == "image" && fs::exists(srcFullPath) && !needImport)
					{
						// For images, just queue a thumbnail generation from the source file.
						thumbnailQueue.push_back({ fileUuid, srcFullPath, thumbnailPath, "image" });
						anyChanges = true;
					}
				}

				if (needImport && (fileType == "mesh" || fileType == "image"))
				{
					std::cout << srcFullPath << " -> " << destFile << "\n";
					importTasks.push_back({ srcFullPath, destFile, fileType,
					                        fileUuid, thumbnailPath, false, false });
					anyChanges = true;
				}

				if (fileType == "material" && (!fs::exists(thumbnailPath) || needImport))
				{
					if (fs::exists(thumbnailPath)) fs::remove(thumbnailPath);
					thumbnailQueue.push_back({ fileUuid, srcFullPath, thumbnailPath, "material" });
					anyChanges = true;
				}
			}
		}

		// Save back
		std::ofstream out(assetsJsonFile);
		out << j.dump(4);
		std::cout << "assets.json updated.\n";

		// Begin batch imports
		startBatchImport(importTasks);

		// Refresh project panel if anything changed
		if (anyChanges && panelProject != nullptr)
			panelProject->triggerRefresh();
	}

	// Reset so that it will check again
	showingMessageBox = false;
}

void Manager::refreshWindowTitle()
{
	if (!projectOpened)
	{
		window->setWindowTitle(initialWindowTitle);
	}
	else
	{
		if (worldName == "")
			window->setWindowTitle(initialWindowTitle + " - " + projectName + " - Untitled");
		else
			window->setWindowTitle(initialWindowTitle + " - " + projectName + " - " + worldName + ".world");

		if (!projectSaved)
			window->setWindowTitle(window->getWindowTitle() + "*");
	}
}

void Manager::closeEditor()
{
	if (!projectSaved)
	{
		showingMessageBox = true;

		auto result = pfd::message(
						  "Unsaved Changes",
						  "Project not saved. Do you really want to quit?",
						  pfd::choice::yes_no,
						  pfd::icon::warning
					  ).result();

		if (result == pfd::button::yes)
		{
			window->setRunning(false); // user confirmed quit
		}
	}
	else
	{
		window->setRunning(false);
	}
}

void Manager::clearWorld(bool forced)
{
    if (!forced)
    {
        if (projectOpened && worldName != "" && !projectSaved)
        {
            showingMessageBox = true;

            auto result = pfd::message(
                              "Unsaved Changes",
                              "World not saved. Do you really want to close this world?",
                              pfd::choice::yes_no,
                              pfd::icon::warning
                          ).result();

            if (result == pfd::button::no)
            {
                return;
            }
        }
    }

    if (!world->getScenes().empty())
    {
        for (kScene* scene : world->getScenes())
        {
            if (scene && scene->getRootNode())
            {
                for (kObject* child : scene->getRootNode()->getChildren())
                {
                    deleteObjectRecursive(child);
                }
                scene->getRootNode()->getChildren().clear();
                delete scene->getRootNode();
                delete scene;
            }
        }
        world->getScenes().clear();
    }
}

void Manager::deleteObjectRecursive(kObject* node)
{
    if (!node) return;

    // Delete all children first
    for (kObject* child : node->getChildren())
    {
        deleteObjectRecursive(child);
    }

    // Clear the children list to avoid dangling pointers
    node->getChildren().clear();

    // Finally delete this node
    delete node;
}

kString Manager::checkAssetType(const fs::path &p)
{
	auto ext = p.extension().string();

	if (ext == ".txt" || ext == ".ini" || ext == ".xml" || ext == ".json")
		return "text";
	else if (ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".png" || ext == ".gif" || ext == ".tiff" || ext == ".tga")
		return "image";
	else if (ext == ".as")
		return "script";
	else if (ext == ".mp3" || ext == ".wav" || ext == ".ogg")
		return "audio";
	else if (ext == ".mp4" || ext == ".mov" || ext == ".avi" || ext == ".webm")
		return "video";
	else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae" || ext == ".stl")
		return "mesh";
	else if (ext == ".prefab" || ext == ".pfb")
		return "prefab";
	else if (ext == ".particle")
		return "particle";
	else if (ext == ".world")
		return "world";
	else if (ext == ".mat")
		return "material";

	return "unknown";     // Unknown
}

void Manager::startBatchImport(const std::vector<ImportTask>& tasks)
{
	if (tasks.empty()) return;

	{
		std::scoped_lock lock(queueMutex);
		importQueue = tasks;
	}

	filesProcessed = 0;
	batchDone = false;
	showImportPopup = true;

	importFuture = std::async(std::launch::async, [this]()
	{
		for (auto& task : importQueue)
		{
			if (task.type == "mesh")
			{
				task.success = convertMeshToGlb(task.inputPath, task.outputPath);
			}
			else if (task.type == "image")
			{
				task.success = convertImageToDxt5(task.inputPath, task.outputPath);
			}
			else
			{
				// Not handled yet -> mark as skipped
				task.success = false;
				std::cout << "Skipping: " << task.inputPath << " (type=" << task.type << " not supported yet)\n";
			}

			filesProcessed++;
		}
		batchDone = true;
	});
}

void Manager::drawImportPopup(PanelConsole* console)
{
	if (showImportPopup)
	{
		kGuiManager *gui = console->gui;
		kVec2 center = gui->getMainViewportCenter();
		gui->setNextWindowPos(center, ImGuiCond_Appearing, kVec2(0.5f, 0.5f));

		if (gui->popupModal("Importing Assets...", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			int totalFiles = (int)importQueue.size();
			int processed  = filesProcessed.load();

			float progress = (totalFiles > 0) ? (float)processed / (float)totalFiles : 0.0f;

			if (!batchDone)
			{
				gui->text("Converting files...");
				gui->progressBar(progress, kVec2(250.f, 0.f));
				gui->text("Processed " + std::to_string(processed) + " / " + std::to_string(totalFiles));
				gui->popupEnd();
			}
			else
			{
				if (importEndTime.time_since_epoch().count() == 0)
					importEndTime = std::chrono::steady_clock::now();

				gui->text("Import complete!");

				gui->progressBar(progress, kVec2(250.f, 0.f));
				gui->text("Processed " + std::to_string(processed) + " / " + std::to_string(totalFiles));

				for (auto& task : importQueue)
				{
					if (!task.success && !task.reported)
					{
						console->addLog(LogLevel::Error, (kString("[Error] Failed to convert: ") + task.inputPath.generic_string()).c_str());
						task.reported = true;
					}
					else if (task.success && !task.reported && task.type == "mesh" && !task.thumbnailPath.empty())
					{
						thumbnailQueue.push_back({ task.uuid, task.outputPath, task.thumbnailPath, "mesh" });
						task.reported = true;
					}
					else if (task.success && !task.reported && task.type == "image" && !task.thumbnailPath.empty())
					{
						thumbnailQueue.push_back({ task.uuid, task.inputPath, task.thumbnailPath, "image" });
						task.reported = true;
					}
				}

				auto now = std::chrono::steady_clock::now();
				if (std::chrono::duration_cast<std::chrono::seconds>(now - importEndTime).count() >= 2)
				{
					gui->closeCurrentPopup();
					showImportPopup = false;
					importEndTime = {};
				}

				gui->popupEnd();

				importTasks.clear();
			}
		}
	}
}

void Manager::processThumbnailQueue(PanelConsole *console)
{
	if (thumbnailQueue.empty()) return;

	ThumbnailTask task = thumbnailQueue.front();
	thumbnailQueue.erase(thumbnailQueue.begin());

	// Skip if thumbnail already exists
	if (fs::exists(task.thumbnailPath)) return;

	if (task.type == "image")
	{
		// Load source image, fit into 128x128 canvas, save as PNG
		const int THUMB = 128;
		const uint8_t BG = 42;

		int w, h, c;
		unsigned char *src = stbi_load(task.srcPath.string().c_str(), &w, &h, &c, 4);
		if (!src)
		{
			console->addLog(LogLevel::Error,
			    ("[Error] Thumbnail: failed to load image " + task.srcPath.generic_string()).c_str());
			return;
		}

		float scale = std::min((float)THUMB / w, (float)THUMB / h);
		int fitW = std::max(1, (int)(w * scale));
		int fitH = std::max(1, (int)(h * scale));

		std::vector<uint8_t> resized(fitW * fitH * 4);
		stbir_resize_uint8_linear(src, w, h, 0, resized.data(), fitW, fitH, 0, STBIR_RGBA);
		stbi_image_free(src);

		// Fill canvas with background, then blit the resized image centered
		std::vector<uint8_t> canvas(THUMB * THUMB * 4);
		for (int i = 0; i < THUMB * THUMB * 4; i += 4)
		{
			canvas[i]     = BG;
			canvas[i + 1] = BG;
			canvas[i + 2] = BG;
			canvas[i + 3] = 255;
		}
		int offX = (THUMB - fitW) / 2;
		int offY = (THUMB - fitH) / 2;
		for (int y = 0; y < fitH; y++)
			for (int x = 0; x < fitW; x++)
			{
				int s = (y * fitW + x) * 4;
				int d = ((offY + y) * THUMB + (offX + x)) * 4;
				canvas[d]     = resized[s];
				canvas[d + 1] = resized[s + 1];
				canvas[d + 2] = resized[s + 2];
				canvas[d + 3] = resized[s + 3];
			}

		bool saved = stbi_write_png(task.thumbnailPath.string().c_str(),
		                            THUMB, THUMB, 4, canvas.data(), THUMB * 4) != 0;
		if (!saved)
			console->addLog(LogLevel::Error,
			    ("[Error] Thumbnail: failed to save " + task.thumbnailPath.generic_string()).c_str());
		else if (panelProject != nullptr)
			panelProject->triggerRefresh();

		return;
	}

	// Material thumbnail — render a sphere with the material's shader
	if (task.type == "material")
	{
		static const char *kPinkVS = R"(
			#version 330 core
			layout(location = 0) in vec3 aPosition;
			uniform mat4 modelMatrix;
			uniform mat4 viewMatrix;
			uniform mat4 projectionMatrix;
			void main() {
				gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(aPosition, 1.0);
			}
		)";
		static const char *kPinkFS = R"(
			#version 330 core
			out vec4 fragColor;
			void main() { fragColor = vec4(1.0, 0.0, 1.0, 1.0); }
		)";

		nlohmann::json matJson;
		{
			std::ifstream f(task.srcPath);
			if (!f.is_open())
			{
				console->addLog(LogLevel::Error,
				    ("[Error] Material thumbnail: cannot open " + task.srcPath.generic_string()).c_str());
				return;
			}
			try { f >> matJson; }
			catch (...)
			{
				console->addLog(LogLevel::Error,
				    ("[Error] Material thumbnail: invalid JSON in " + task.srcPath.generic_string()).c_str());
				return;
			}
		}

		kMesh *sphere = kMeshGenerator::generateSphere(1.0f, 32, 32);
		if (!sphere) return;
		sphere->calculateModelMatrix();

		// Cached built-in shaders for material thumbnails (loaded once, owned by asset manager)
		static kShader *s_shaderUnlit = nullptr;
		static kShader *s_shaderPhong = nullptr;
		static kShader *s_shaderPbr   = nullptr;
		static kShader *s_shaderPink  = nullptr;

		kAssetManager *am = getAssetManager();
		kString shaderName = matJson.value("shader", "Phong");
		kShader *shader = nullptr;

		if (shaderName == "Unlit" && am) {
			if (!s_shaderUnlit) s_shaderUnlit = am->loadGlslFromResource("SHADER_MESH_FLAT");
			shader = s_shaderUnlit;
		} else if (shaderName == "Phong" && am) {
			if (!s_shaderPhong) s_shaderPhong = am->loadGlslFromResource("SHADER_MESH_PHONG");
			shader = s_shaderPhong;
		} else if (shaderName == "PBR" && am) {
			if (!s_shaderPbr) s_shaderPbr = am->loadGlslFromResource("SHADER_MESH_PBR");
			shader = s_shaderPbr;
		}

		// "Custom" or null/invalid → pink fallback
		if (!shader || shader->getShaderProgram() == 0) {
			if (!s_shaderPink) {
				s_shaderPink = new kShader();
				s_shaderPink->loadShadersCode(kPinkVS, kPinkFS);
			}
			shader = s_shaderPink;
		}

		kMaterial *mat = new kMaterial();
		mat->setShader(shader);

		// Apply colour properties from JSON
		auto readVec3 = [&](const char *key, kVec3 def) -> kVec3 {
			if (matJson.contains(key) && matJson[key].is_array() && matJson[key].size() >= 3)
				return kVec3(matJson[key][0].get<float>(), matJson[key][1].get<float>(), matJson[key][2].get<float>());
			return def;
		};
		mat->setDiffuseColor (readVec3("diffuse",  kVec3(1.0f)));
		mat->setAmbientColor (readVec3("ambient",  kVec3(1.0f)));
		mat->setSpecularColor(readVec3("specular", kVec3(1.0f)));
		mat->setShininess(matJson.value("shininess", 32.0f));
		mat->setMetallic (matJson.value("metallic",  0.0f));
		mat->setRoughness(matJson.value("roughness", 0.5f));

		sphere->setMaterial(mat);

		bool saved = false;
		try
		{
			thumbnailRenderer.setBackgroundColor(kVec4(42/255.0f, 42/255.0f, 42/255.0f, 1.0f));
			thumbnailRenderer.renderMeshWithMaterial(sphere);
			saved = thumbnailRenderer.saveToFile(task.thumbnailPath.generic_string());
		}
		catch (...) { saved = false; }

		// Neither kMesh nor kMaterial take ownership, delete each separately
		// shader is cached static — not deleted here
		delete sphere;
		delete mat;

		if (!saved)
			console->addLog(LogLevel::Error,
			    ("[Error] Material thumbnail: failed to save " + task.thumbnailPath.generic_string()).c_str());
		else if (panelProject != nullptr)
			panelProject->triggerRefresh();
		return;
	}

	// Mesh thumbnail — rendered via offscreen renderer
	kAssetManager *am = getAssetManager();
	if (!am) return;
	kMesh *mesh = am->loadMesh(task.srcPath.generic_string());
	if (!mesh)
	{
		console->addLog(LogLevel::Error,
		    ("[Error] Thumbnail: failed to load mesh " + task.srcPath.generic_string()).c_str());
		return;
	}

	mesh->calculateModelMatrix();

	bool saved = false;
	try
	{
		thumbnailRenderer.setBackgroundColor(kVec4(42/255.0f, 42/255.0f, 42/255.0f, 1.0f));
		thumbnailRenderer.renderMesh(mesh);
		saved = thumbnailRenderer.saveToFile(task.thumbnailPath.generic_string());
	}
	catch (...)
	{
		saved = false;
	}

	if (!saved)
		console->addLog(LogLevel::Error,
		    ("[Error] Thumbnail: failed to save " + task.thumbnailPath.generic_string()).c_str());
	else if (panelProject != nullptr)
		panelProject->triggerRefresh();
	// Mesh is owned by the asset manager — do not delete manually.
}

void Manager::selectObject(const kString uuid, bool clearList)
{
    selectedScene  = nullptr;
    worldSelected  = false;

    if (clearList)
        selectedObjects.clear();

    auto it = std::find(selectedObjects.begin(), selectedObjects.end(), uuid);
    if (it == selectedObjects.end())
    {
        selectedObjects.push_back(uuid);
    }
}

void Manager::deselectObject(const kString uuid)
{
    auto it = std::find(selectedObjects.begin(), selectedObjects.end(), uuid);
    if (it != selectedObjects.end())
    {
        selectedObjects.erase(it);
    }
}

kObject *Manager::findObjectByUuid(const kString &uuid)
{
    // Fast path: objectMap (populated by hierarchy panel)
    auto it = objectMap.find(uuid);
    if (it != objectMap.end() && it->second.object)
        return it->second.object;

    // Fallback: traverse the scene graph directly (recursively, so nested
    // sub-meshes resolve even when objectMap hasn't been rebuilt yet).
    if (!scene || !scene->getRootNode()) return nullptr;
    std::function<kObject *(kObject *)> find = [&](kObject *node) -> kObject * {
        for (kObject *child : node->getChildren())
        {
            if (child->getUuid() == uuid) return child;
            if (kObject *hit = find(child)) return hit;
        }
        return nullptr;
    };
    return find(scene->getRootNode());
}

std::vector<TransformState> Manager::captureSelectedTransforms()
{
    std::vector<TransformState> states;
    for (const auto &uuid : selectedObjects)
    {
        kObject *obj = findObjectByUuid(uuid);
        if (!obj) continue;
        states.push_back({ uuid, obj->getPosition(), obj->getRotation(), obj->getScale() });
    }
    return states;
}

// Forward decls — defined further down in this file but needed by
// duplicateSelectedObjects below.
static kObject *loadObjectFromJson(const nlohmann::json &obj, kScene *scene, kWorld *world,
                                   kAssetManager *am, const fs::path &projectPath,
                                   kCamera *editorCamera, kObject *parent);
static void pushInstantiateCommand(Manager *mgr, kObject *root);
static void reapplyMaterialsRecursive(Manager *mgr, kObject *node, const fs::path &projectPath);
static void assignImportChildUuidsRec(kObject *root);

// Walk a serialized kObject JSON tree and replace every UUID with a fresh one
// so the cloned subtree doesn't collide with its original. Recurses into the
// "children" array and also bumps the per-component UUIDs that kObject emits
// (scripts, particles, audio sources / listeners) so attachments stay unique.
static void regenerateUuidsRecursive(nlohmann::json &j)
{
    if (!j.is_object()) return;

    if (j.contains("uuid") && j["uuid"].is_string())
        j["uuid"] = generateUuid();

    for (const char *key : { "script", "scripts", "particles",
                             "audio_sources", "audio_listeners" })
    {
        if (j.contains(key) && j[key].is_array())
            for (auto &item : j[key])
                if (item.is_object() && item.contains("uuid") && item["uuid"].is_string())
                    item["uuid"] = generateUuid();
    }

    if (j.contains("children") && j["children"].is_array())
        for (auto &c : j["children"])
            regenerateUuidsRecursive(c);
}

void Manager::duplicateSelectedObjects()
{
    if (!scene || selectedObjects.empty()) return;

    kAssetManager *am = getAssetManager();

    // Snapshot the current selection — loadObjectFromJson + pushInstantiateCommand
    // will rewrite selectedObjects below, so we can't iterate the live vector.
    std::vector<kString> sourceUuids = selectedObjects;
    std::vector<kObject *> duplicates;
    duplicates.reserve(sourceUuids.size());

    for (const auto &uuid : sourceUuids)
    {
        kObject *src = findObjectByUuid(uuid);
        if (!src) continue;

        nlohmann::json j = src->serialize();
        regenerateUuidsRecursive(j);

        kObject *clone = loadObjectFromJson(j, scene, world, am,
                                            projectPath, editorCamera, nullptr);
        if (clone)
        {
            // Rebuild the runtime material from the cloned material UUID so the
            // duplicate looks identical immediately (not only after a reload).
            reapplyMaterialsRecursive(this, clone, projectPath);
            duplicates.push_back(clone);
        }
    }

    if (duplicates.empty()) return;

    // Make the new objects the active selection.
    selectedObjects.clear();
    for (kObject *obj : duplicates)
        selectedObjects.push_back(obj->getUuid());
    selectedObject = duplicates.back();

    if (panelHierarchy)
        panelHierarchy->refreshList();

    projectSaved = false;
    refreshWindowTitle();

    // One undo step per spawned root — mirrors how individual createMesh /
    // createLight / etc. push their own InstantiateCommand. Ctrl+Z undoes the
    // duplicates one at a time, in reverse spawn order.
    for (kObject *obj : duplicates)
        pushInstantiateCommand(this, obj);
}

void Manager::deleteSelectedObjects()
{
    if (!scene || selectedObjects.empty()) return;

    std::vector<DeletedObjectInfo> deleted;

    for (const auto &uuid : selectedObjects)
    {
        kObject *obj = findObjectByUuid(uuid);
        if (!obj) continue;

        kNodeType type = obj->getType();

        if (type == NODE_TYPE_LIGHT)
            scene->removeLight(static_cast<kLight *>(obj));
        else
            scene->removeMesh(static_cast<kMesh *>(obj));

        deleted.push_back({ obj, type, scene });
    }

    if (deleted.empty()) return;

    auto cmd = std::make_unique<DeleteCommand>(
        this, std::move(deleted), selectedObjects, selectedObject);

    selectedObjects.clear();
    selectedObject = nullptr;

    if (panelHierarchy)
        panelHierarchy->refreshList();

    undoRedo.push(std::move(cmd));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Recursively collect all UUIDs reachable under node (excluding the node itself).
static void collectUuids(kObject *node, std::vector<kString> &out, kObject **firstObj)
{
    for (kObject *child : node->getChildren())
    {
        out.push_back(child->getUuid());
        if (*firstObj == nullptr) *firstObj = child;
        collectUuids(child, out, firstObj);
    }
}

// Apply a Phong material to a mesh.
static void applyDefaultMaterial(kMesh *mesh, kAssetManager *am)
{
    kShader   *shader = am->loadGlslFromResource("SHADER_MESH_PHONG");
    kMaterial *mat    = am->createMaterial(shader);
    mat->setAmbientColor(kVec3(1.0f, 1.0f, 1.0f));
    mat->setDiffuseColor(kVec3(0.5f, 0.5f, 0.5f));
    mesh->setMaterial(mat);
}

// Apply a gizmo icon material to a light.
static void applyLightIcon(kLight *light, kAssetManager *am, const char *gizmoResource)
{
    kShader    *shader = am->loadGlslFromResource("SHADER_ICON");
    kMaterial  *mat    = am->createMaterial(shader);
    kTexture2D *tex    = am->loadTexture2DFromResource(gizmoResource, "albedoMap",
                                                        kTextureFormat::TEX_FORMAT_RGBA);
    mat->addTexture(tex);
    light->setMaterial(mat);
}

// Apply the GIZMO_CAMERA icon material to a camera so it renders as a
// billboard in the editor view (skipped automatically by the renderer when
// the camera being drawn is also the active view camera).
static void applyCameraIcon(kCamera *cam, kAssetManager *am)
{
    kShader    *shader = am->loadGlslFromResource("SHADER_ICON");
    kMaterial  *mat    = am->createMaterial(shader);
    kTexture2D *tex    = am->loadTexture2DFromResource("GIZMO_CAMERA", "albedoMap",
                                                        kTextureFormat::TEX_FORMAT_RGBA);
    mat->addTexture(tex);
    cam->setMaterial(mat);
}

// ---------------------------------------------------------------------------
// Edit — selection helpers
// ---------------------------------------------------------------------------

void Manager::selectAll()
{
    if (!scene) return;

    std::vector<kString> newSel;
    kObject *newSelObj = nullptr;
    collectUuids(scene->getRootNode(), newSel, &newSelObj);

    if (newSel == selectedObjects) return;

    auto before    = selectedObjects;
    auto beforeObj = selectedObject;

    selectedObjects = newSel;
    selectedObject  = newSelObj;

    undoRedo.push(std::make_unique<SelectCommand>(this, before, beforeObj, newSel, newSelObj));
}

void Manager::deselectAll()
{
    if (selectedObjects.empty()) return;

    auto before    = selectedObjects;
    auto beforeObj = selectedObject;

    selectedObjects.clear();
    selectedObject = nullptr;

    undoRedo.push(std::make_unique<SelectCommand>(this, before, beforeObj, std::vector<kString>{}, nullptr));
}

void Manager::invertSelection()
{
    if (!scene) return;

    std::set<kString> currentSet(selectedObjects.begin(), selectedObjects.end());

    std::vector<kString> allUuids;
    kObject *dummy = nullptr;
    collectUuids(scene->getRootNode(), allUuids, &dummy);

    std::vector<kString> newSel;
    kObject *newSelObj = nullptr;
    for (const auto &uuid : allUuids)
    {
        if (currentSet.find(uuid) == currentSet.end())
        {
            newSel.push_back(uuid);
            if (newSelObj == nullptr)
                newSelObj = findObjectByUuid(uuid);
        }
    }

    auto before    = selectedObjects;
    auto beforeObj = selectedObject;

    selectedObjects = newSel;
    selectedObject  = newSelObj;

    undoRedo.push(std::make_unique<SelectCommand>(this, before, beforeObj, newSel, newSelObj));
}

// ---------------------------------------------------------------------------
// Object creation — shared post-creation logic
// ---------------------------------------------------------------------------

static void finishCreate(Manager *mgr, kObject *obj, kScene *scene,
                         std::function<void()> undoFn, std::function<void()> redoFn)
{
    kString uuid = obj->getUuid();
    mgr->selectedObject = obj;
    mgr->selectObject(uuid, true);

    if (mgr->panelHierarchy)
        mgr->panelHierarchy->refreshList();

    mgr->undoRedo.push(std::make_unique<PropertyCommand>(
        std::move(undoFn), std::move(redoFn)));
}

// ---------------------------------------------------------------------------
// Create Scene
// ---------------------------------------------------------------------------

void Manager::createSceneObject()
{
    kScene *newScene = world->createScene("New Scene");

    undoRedo.push(std::make_unique<PropertyCommand>(
        [this, newScene]() { world->removeScene(newScene); },
        [this, newScene]() { world->addScene(newScene);    }
    ));
}

// ---------------------------------------------------------------------------
// Create Empty
// ---------------------------------------------------------------------------

void Manager::createEmpty()
{
    if (!scene) return;

    kObject *obj = new kObject();
    obj->setName("Empty");
    scene->addObject(obj);
    kString uuid = obj->getUuid();

    finishCreate(this, obj, scene,
        [this, obj, uuid]()
        {
            scene->removeObject(obj);
            selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(), uuid),
                                  selectedObjects.end());
            if (selectedObject == obj) selectedObject = nullptr;
            if (panelHierarchy) panelHierarchy->refreshList();
        },
        [this, obj, uuid]()
        {
            scene->addObject(obj, uuid);
            selectedObject = obj;
            selectObject(uuid, true);
            if (panelHierarchy) panelHierarchy->refreshList();
        }
    );
}

// ---------------------------------------------------------------------------
// Create Mesh Primitive
// ---------------------------------------------------------------------------

void Manager::createMeshPrimitive(kMesh *mesh, const kString &name)
{
    if (!scene) return;

    kAssetManager *am = getAssetManager();
    if (am) applyDefaultMaterial(mesh, am);

    mesh->setName(name);
    scene->addMesh(mesh);
    kString uuid = mesh->getUuid();

    finishCreate(this, mesh, scene,
        [this, mesh, uuid]()
        {
            scene->removeMesh(mesh);
            selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(), uuid),
                                  selectedObjects.end());
            if (selectedObject == mesh) selectedObject = nullptr;
            if (panelHierarchy) panelHierarchy->refreshList();
        },
        [this, mesh, uuid]()
        {
            scene->addMesh(mesh, uuid);
            selectedObject = mesh;
            selectObject(uuid, true);
            if (panelHierarchy) panelHierarchy->refreshList();
        }
    );
}

// ---------------------------------------------------------------------------
// Create Mesh from file
// ---------------------------------------------------------------------------

void Manager::createMeshFromFile()
{
    if (!scene) return;

    auto result = pfd::open_file("Open Mesh", "",
        { "Mesh files", "*.obj *.fbx *.gltf *.glb",
          "All files",  "*" }).result();

    if (result.empty()) return;

    kString filePath = result[0];
    kAssetManager *am = getAssetManager();
    if (!am) return;

    kMesh *mesh = am->loadMesh(filePath);
    if (!mesh) return;

    mesh->setLoaded(true);
    mesh->setFileName(filePath);

    // Derive a display name from the filename
    fs::path p(filePath);
    mesh->setName(p.stem().string());

    if (am) applyDefaultMaterial(mesh, am);
    assignImportChildUuidsRec(mesh);

    scene->addMesh(mesh);
    kString uuid = mesh->getUuid();

    finishCreate(this, mesh, scene,
        [this, mesh, uuid]()
        {
            scene->removeMesh(mesh);
            selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(), uuid),
                                  selectedObjects.end());
            if (selectedObject == mesh) selectedObject = nullptr;
            if (panelHierarchy) panelHierarchy->refreshList();
        },
        [this, mesh, uuid]()
        {
            scene->addMesh(mesh, uuid);
            selectedObject = mesh;
            selectObject(uuid, true);
            if (panelHierarchy) panelHierarchy->refreshList();
        }
    );
}

// ---------------------------------------------------------------------------
// Create Light
// ---------------------------------------------------------------------------

void Manager::createLight(kLightType type)
{
    if (!scene) return;

    kAssetManager *am = getAssetManager();

    kLight *light = nullptr;
    const char *gizmoRes = "GIZMO_SUN_LIGHT";

    if (type == LIGHT_TYPE_SUN)
    {
        light = scene->addSunLight(kVec3(0, 3, 0), kVec3(0,-1,0),
                                    kVec3(1.0f, 1.0f, 1.0f),
                                    kVec3(1.0f, 1.0f, 1.0f));
        light->setName("Sun Light");
        gizmoRes = "GIZMO_SUN_LIGHT";
    }
    else if (type == LIGHT_TYPE_POINT)
    {
        light = scene->addPointLight(kVec3(0, 2, 0),
                                      kVec3(1.0f, 1.0f, 1.0f),
                                      kVec3(1.0f, 1.0f, 1.0f));
        light->setName("Point Light");
        gizmoRes = "GIZMO_POINT_LIGHT";
    }
    else if (type == LIGHT_TYPE_SPOT)
    {
        light = scene->addSpotLight(kVec3(0, 3, 0),
                                     kVec3(1.0f, 1.0f, 1.0f),
                                     kVec3(1.0f, 1.0f, 1.0f));
        light->setName("Spot Light");
        gizmoRes = "GIZMO_SPOT_LIGHT";
    }
    else return;

    light->setPower(1.0f);
    if (am) applyLightIcon(light, am, gizmoRes);

    kString uuid = light->getUuid();

    finishCreate(this, light, scene,
        [this, light, uuid]()
        {
            scene->removeLight(light);
            selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(), uuid),
                                  selectedObjects.end());
            if (selectedObject == light) selectedObject = nullptr;
            if (panelHierarchy) panelHierarchy->refreshList();
        },
        [this, light, uuid]()
        {
            scene->addLight(light);
            selectedObject = light;
            selectObject(uuid, true);
            if (panelHierarchy) panelHierarchy->refreshList();
        }
    );
}

// ---------------------------------------------------------------------------
// Create Camera
// ---------------------------------------------------------------------------

void Manager::createCamera()
{
    if (!scene) return;

    kCamera *cam = new kCamera();
    cam->setName("Camera");
    cam->setPosition(kVec3(0.0f, 1.0f, 5.0f));
    cam->setLookAt(kVec3(0.0f, 0.0f, 0.0f));
    cam->setFOV(60.0f);

    scene->addObject(cam);
    world->addCamera(cam, cam->getUuid()); // also register in world camera list
    if (kAssetManager *am = getAssetManager())
        applyCameraIcon(cam, am);
    kString uuid = cam->getUuid();

    finishCreate(this, cam, scene,
        [this, cam, uuid]()
        {
            scene->removeObject(cam);
            world->removeCamera(cam);
            selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(), uuid),
                                  selectedObjects.end());
            if (selectedObject == cam) selectedObject = nullptr;
            if (panelHierarchy) panelHierarchy->refreshList();
        },
        [this, cam, uuid]()
        {
            scene->addObject(cam, uuid);
            world->addCamera(cam, uuid);
            selectedObject = cam;
            selectObject(uuid, true);
            if (panelHierarchy) panelHierarchy->refreshList();
        }
    );
}

void Manager::createNavMesh()
{
    if (!scene) return;

    // A navigation surface is a plain object carrying a nav-mesh component; the
    // object's transform also positions the optional area box.
    kObject *obj = new kObject();
    obj->setName("Nav Mesh");
    obj->setHasNavMeshDesc(true);
    scene->addObject(obj);
    kString uuid = obj->getUuid();

    finishCreate(this, obj, scene,
        [this, obj, uuid]()
        {
            clearNavMesh(obj);
            scene->removeObject(obj);
            selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(), uuid),
                                  selectedObjects.end());
            if (selectedObject == obj) selectedObject = nullptr;
            if (panelHierarchy) panelHierarchy->refreshList();
        },
        [this, obj, uuid]()
        {
            scene->addObject(obj, uuid);
            selectedObject = obj;
            selectObject(uuid, true);
            if (panelHierarchy) panelHierarchy->refreshList();
        }
    );
}

// ---------------------------------------------------------------------------
// Navigation baking
// ---------------------------------------------------------------------------

void Manager::bakeNavMesh(kObject *navObj)
{
    if (!navObj || !scene || !navObj->getHasNavMeshDesc())
        return;

    kNavMeshDesc &desc = navObj->getNavMeshDesc();

    // Area box centred on the nav object (full extents = areaSize).
    navObj->calculateModelMatrix();
    kVec3 center = navObj->getGlobalPosition();
    kVec3 half   = desc.areaSize * 0.5f;
    kAABB areaBox(center - half, center + half);

    std::vector<float> verts;
    std::vector<int>   tris;

    std::function<void(kObject *)> walk = [&](kObject *node) {
        if (!node) return;

        // Only static mesh geometry contributes to the walkable surface.
        if (node->getType() == NODE_TYPE_MESH && node->getStatic())
        {
            kMesh *mesh = static_cast<kMesh *>(node);
            mesh->calculateModelMatrix();

            if (!desc.useArea || areaBox.overlaps(mesh->getWorldAABB()))
            {
                kMat4 world = mesh->getModelMatrixWorld();
                std::vector<kVec3>    mv = mesh->getVertices();
                std::vector<uint32_t> mi = mesh->getIndices();

                int base = (int)(verts.size() / 3);
                for (const kVec3 &v : mv)
                {
                    kVec3 w = kVec3(world * kVec4(v, 1.0f));
                    verts.push_back(w.x);
                    verts.push_back(w.y);
                    verts.push_back(w.z);
                }
                for (uint32_t idx : mi)
                    tris.push_back(base + (int)idx);
            }
        }
        for (kObject *c : node->getChildren()) walk(c);
    };
    walk(scene->getRootNode());

    if (verts.empty() || tris.empty())
    {
        std::cerr << "Nav: no static mesh geometry found to bake.\n";
        return;
    }

    // Replace any previous bake for this object.
    clearNavMesh(navObj);

    kNavMesh *nav = new kNavMesh();
    if (nav->bake(verts, tris, desc.config))
    {
        bakedNavMeshes[navObj->getUuid()] = nav;
    }
    else
    {
        std::cerr << "Nav: bake failed.\n";
        delete nav;
    }
}

void Manager::clearNavMesh(kObject *navObj)
{
    if (!navObj) return;
    auto it = bakedNavMeshes.find(navObj->getUuid());
    if (it != bakedNavMeshes.end())
    {
        delete it->second;
        bakedNavMeshes.erase(it);
    }
}

void Manager::clearAllNavMeshes()
{
    for (auto &pair : bakedNavMeshes)
        delete pair.second;
    bakedNavMeshes.clear();
}

bool Manager::isNavMeshBaked(kObject *navObj) const
{
    if (!navObj) return false;
    auto it = bakedNavMeshes.find(navObj->getUuid());
    return it != bakedNavMeshes.end() && it->second && it->second->isBaked();
}

kNavMesh *Manager::getBakedNavMesh(kObject *navObj) const
{
    if (!navObj) return nullptr;
    auto it = bakedNavMeshes.find(navObj->getUuid());
    return it != bakedNavMeshes.end() ? it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Game export
// ---------------------------------------------------------------------------

bool Manager::exportGame(const ExportSettings &s)
{
    if (!projectOpened)
    {
        std::cerr << "Export: no project open.\n";
        return false;
    }
    if (s.outputDir.empty())
    {
        std::cerr << "Export: no output directory.\n";
        return false;
    }

    fs::path templ = s.templateDir;
    if (templ.empty() || !fs::exists(templ))
    {
        std::cerr << "Export: runtime template folder not found: " << s.templateDir << "\n";
        return false;
    }

    std::error_code ec;

    // 1. Ensure compiled bytecode and an up-to-date scene.world.
    buildScripts();
    saveWorld();

    // 2. Prepare output folder.
    fs::path outDir = s.outputDir;
    fs::create_directories(outDir, ec);

    // 3. Copy the runtime template (player exe + dependency runtime libs).
    for (const auto &entry : fs::directory_iterator(templ, ec))
    {
        fs::copy(entry.path(), outDir / entry.path().filename(),
                 fs::copy_options::overwrite_existing | fs::copy_options::recursive, ec);
    }

    // 4. Rename the generic runtime executable to the game name.
    const bool win = (s.platform == 0);
    fs::path runExe  = outDir / (win ? "Kemena3DRuntime.exe" : "Kemena3DRuntime");
    fs::path gameExe = outDir / (win ? (s.gameName + ".exe")  : s.gameName);
    if (fs::exists(runExe))
        fs::rename(runExe, gameExe, ec);

    // 5. Bundle game data under data/.
    fs::path dataDir = outDir / "data";
    fs::create_directories(dataDir, ec);

    if (fs::exists(worldPath))
        fs::copy(worldPath, dataDir / "scene.world", fs::copy_options::overwrite_existing, ec);

    auto copyLibFolder = [&](const char *sub) {
        fs::path src = projectPath / "Library" / sub;
        if (fs::exists(src))
            fs::copy(src, dataDir / "Library" / sub,
                     fs::copy_options::overwrite_existing | fs::copy_options::recursive, ec);
    };
    copyLibFolder("ImportedAssets"); // .glb meshes (textures baked in)
    copyLibFolder("Shaders");        // compiled shader graphs

    // Scripts: ship compiled bytecode only — never the .as / .logic sources.
    {
        fs::path src = projectPath / "Library" / "Scripts";
        fs::path dst = dataDir / "Library" / "Scripts";
        if (fs::exists(src))
        {
            fs::create_directories(dst, ec);
            for (const auto &e : fs::directory_iterator(src, ec))
                if (e.path().extension() == ".kbc")
                    fs::copy(e.path(), dst / e.path().filename(),
                             fs::copy_options::overwrite_existing, ec);
        }
    }

    // 6. Write game.config (read by the runtime at startup).
    {
        nlohmann::json cfg;
        cfg["title"]      = s.title.empty() ? s.gameName : s.title;
        cfg["width"]      = s.width;
        cfg["height"]     = s.height;
        cfg["fullscreen"] = s.fullscreen;
        std::ofstream f(dataDir / "game.config");
        if (f.is_open()) f << cfg.dump(4);
    }

    // 7. Per-OS metadata (best-effort). Windows exe icon via rcedit if present.
    if (win && !s.iconPath.empty() && fs::exists(s.iconPath))
    {
        std::string cmd = "rcedit \"" + gameExe.string() +
                          "\" --set-icon \"" + s.iconPath + "\"";
        int r = std::system(cmd.c_str()); // requires rcedit on PATH; non-fatal
        if (r != 0)
            std::cerr << "Export: rcedit icon step skipped (rcedit not found?).\n";
    }

    std::cout << "Export complete: " << outDir << "\n";
    return true;
}

// ---------------------------------------------------------------------------
// Create New Material asset file
// ---------------------------------------------------------------------------

void Manager::createNewMaterial()
{
    if (!projectOpened) return;

    fs::path dir = getCurrentDirPath();

    kString baseName = "New Material";
    fs::path filePath = dir / (baseName + ".mat");
    int counter = 1;
    while (fs::exists(filePath))
    {
        filePath = dir / (baseName + " " + std::to_string(counter) + ".mat");
        counter++;
    }

    kString matUuid = generateUuid();

    nlohmann::json mat;
    mat["uuid"]      = matUuid;
    mat["shader"]    = "Phong";
    mat["diffuse"]   = { 1.0f, 1.0f, 1.0f };
    mat["ambient"]   = { 1.0f, 1.0f, 1.0f };
    mat["specular"]  = { 1.0f, 1.0f, 1.0f };
    mat["shininess"] = 32.0f;
    mat["metallic"]  = 0.0f;
    mat["roughness"] = 0.5f;
    mat["uv_tiling"] = { 1.0f, 1.0f };
    mat["transparent"]  = 0;
    mat["single_sided"] = true;
    mat["cull_back"]    = true;
    mat["texture_albedo"]             = "";
    mat["texture_normal"]             = "";
    mat["texture_metallic_roughness"] = "";
    mat["texture_ao"]                 = "";
    mat["texture_emissive"]           = "";

    std::ofstream f(filePath);
    if (!f.is_open())
    {
        std::cerr << "Failed to create material file: " << filePath << "\n";
        return;
    }
    f << mat.dump(4);
    f.close();

    checkAssetChange();
}

void Manager::createNewFolder()
{
    if (!projectOpened) return;

    fs::path dir = getCurrentDirPath();
    kString baseName = "New Folder";
    fs::path folderPath = dir / baseName;
    int counter = 1;
    while (fs::exists(folderPath))
        folderPath = dir / (baseName + " " + std::to_string(counter++));

    std::error_code ec;
    fs::create_directory(folderPath, ec);
    if (ec)
    {
        std::cerr << "Failed to create folder: " << folderPath << " (" << ec.message() << ")\n";
        return;
    }
    checkAssetChange();
}

void Manager::createNewShader()
{
    if (!projectOpened) return;

    fs::path dir = getCurrentDirPath();
    kString baseName = "New Shader";
    fs::path filePath = dir / (baseName + ".shader");
    int counter = 1;
    while (fs::exists(filePath))
        filePath = dir / (baseName + " " + std::to_string(counter++) + ".shader");

    // Seed with a default PBR output node so the graph opens ready to edit.
    kShaderGraph graph;
    graph.uuid = generateUuid();
    graph.name = filePath.stem().string();
    graph.nodes.push_back(graph.makeNode(kShaderNodeType::OutputPBR, 500.0f, 200.0f));

    std::ofstream f(filePath);
    if (!f.is_open())
    {
        std::cerr << "Failed to create shader file: " << filePath << "\n";
        return;
    }
    f << graph.toJson().dump(4);
    f.close();

    checkAssetChange();
}

void Manager::createNewScript()
{
    if (!projectOpened) return;

    fs::path dir = getCurrentDirPath();
    kString baseName = "New Script";
    fs::path filePath = dir / (baseName + ".as");
    int counter = 1;
    while (fs::exists(filePath))
        filePath = dir / (baseName + " " + std::to_string(counter++) + ".as");

    std::ofstream f(filePath);
    if (!f.is_open())
    {
        std::cerr << "Failed to create script file: " << filePath << "\n";
        return;
    }
    f << "// AngelScript — attach to an object's Scripts component in the Inspector.\n"
         "// Host API: getSelf(), getDeltaTime(), print(string), kVec3, kObject.\n\n"
         "void Start()\n"
         "{\n"
         "}\n\n"
         "void Update()\n"
         "{\n"
         "}\n";
    f.close();

    checkAssetChange();
}

void Manager::createNewLogicGraph()
{
    if (!projectOpened) return;

    fs::path dir = getCurrentDirPath();
    kString baseName = "New Logic Graph";
    fs::path filePath = dir / (baseName + ".logic");
    int counter = 1;
    while (fs::exists(filePath))
        filePath = dir / (baseName + " " + std::to_string(counter++) + ".logic");

    // Seed with an On Update event so the canvas is not empty.
    kScriptGraph graph;
    graph.uuid = generateUuid();
    graph.name = filePath.stem().string();
    graph.nodes.push_back(graph.makeNode(kScriptNodeType::EventUpdate, 120.0f, 120.0f));

    std::ofstream f(filePath);
    if (!f.is_open())
    {
        std::cerr << "Failed to create logic graph file: " << filePath << "\n";
        return;
    }
    f << graph.toJson().dump(2);
    f.close();

    checkAssetChange();
}

bool Manager::moveAsset(const fs::path &srcPath, const fs::path &destDir)
{
    if (!projectOpened || srcPath.empty() || destDir.empty()) return false;

    std::error_code ec;
    if (!fs::exists(srcPath, ec) || ec)
    {
        std::cerr << "Move failed — source does not exist: " << srcPath << "\n";
        return false;
    }
    if (!fs::is_directory(destDir, ec) || ec)
    {
        std::cerr << "Move failed — destination is not a folder: " << destDir << "\n";
        return false;
    }

    fs::path newPath = destDir / srcPath.filename();
    if (newPath == srcPath) return true; // already in that folder

    if (fs::exists(newPath, ec))
    {
        std::cerr << "Move failed — '" << newPath.filename().string()
                  << "' already exists in the target folder.\n";
        return false;
    }

    fs::rename(srcPath, newPath, ec);
    if (ec)
    {
        std::cerr << "Move failed: " << ec.message() << "\n";
        return false;
    }

    // The file's content — including its embedded UUID — is untouched; only
    // the path changed. checkAssetChange() refreshes Manager::fileMap so the
    // new location is the one resolved by findAssetPathByUuid().
    checkAssetChange();
    return true;
}

bool Manager::renameAsset(const fs::path &oldPath, const kString &newName)
{
    if (!projectOpened || newName.empty()) return false;

    fs::path newPath = oldPath.parent_path() / newName;
    if (newPath == oldPath) return true;

    if (fs::exists(newPath))
    {
        std::cerr << "Rename failed — a file named '" << newName << "' already exists.\n";
        return false;
    }

    std::error_code ec;
    fs::rename(oldPath, newPath, ec);
    if (ec)
    {
        std::cerr << "Rename failed: " << ec.message() << "\n";
        return false;
    }

    checkAssetChange();
    return true;
}

// ---------------------------------------------------------------------------
// Save / Load world
// ---------------------------------------------------------------------------

void Manager::saveProjectConfig()
{
    if (!projectOpened || projectPath.empty()) return;

    fs::path cfgDir  = projectPath / "Config";
    std::error_code ec;
    fs::create_directories(cfgDir, ec);
    fs::path cfgFile = cfgDir / "project.json";

    try
    {
        json j;
        if (fs::exists(cfgFile))
        {
            std::ifstream in(cfgFile);
            try { in >> j; } catch (...) {}
        }
        if (!worldPath.empty() && fs::exists(worldPath))
        {
            std::error_code rel_ec;
            fs::path rel = fs::relative(worldPath, projectPath, rel_ec);
            j["last_world"] = rel_ec ? worldPath.string()
                                     : rel.generic_string();
        }
        else
        {
            j["last_world"] = "";
        }
        std::ofstream out(cfgFile);
        if (out.is_open())
            out << j.dump(4);
    }
    catch (const std::exception &e)
    {
        std::cerr << "saveProjectConfig: " << e.what() << "\n";
    }
}

fs::path Manager::loadLastWorldPath() const
{
    if (projectPath.empty()) return {};

    // Preferred: the explicit "last_world" record written by saveWorld.
    fs::path cfgFile = projectPath / "Config" / "project.json";
    if (fs::exists(cfgFile))
    {
        try
        {
            std::ifstream in(cfgFile);
            json j; in >> j;
            std::string rel = j.value("last_world", std::string());
            if (!rel.empty())
            {
                fs::path abs = projectPath / rel;
                if (fs::exists(abs)) return abs;
            }
        }
        catch (...) {}
    }

    // Fallback A: any .world file under Assets/. Pick the most-recently-
    // modified one so a project that has multiple .world files surfaces the
    // one the user most likely worked on last.
    fs::path assetsDir = projectPath / "Assets";
    if (fs::exists(assetsDir))
    {
        std::error_code ec;
        fs::path           best;
        fs::file_time_type bestTime{};
        for (auto it = fs::recursive_directory_iterator(assetsDir, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec) break;
            if (!it->is_regular_file()) continue;
            if (it->path().extension() != ".world") continue;
            auto mt = fs::last_write_time(it->path(), ec);
            if (best.empty() || mt > bestTime) { best = it->path(); bestTime = mt; }
        }
        if (!best.empty()) return best;
    }

    // Fallback B: pre-Assets-rule projects kept the world at the project root.
    fs::path legacy = projectPath / "scene.world";
    if (fs::exists(legacy)) return legacy;

    return {};
}

namespace
{
    // True when `child` lives inside `parent` (or equals a file directly in it).
    // Uses weakly_canonical so the comparison survives relative segments and
    // mixed separators that pfd may return on Windows.
    bool isPathUnder(const fs::path &child, const fs::path &parent)
    {
        std::error_code ec;
        fs::path c = fs::weakly_canonical(child, ec);
        fs::path p = fs::weakly_canonical(parent, ec);
        if (ec) return false;
        fs::path rel = c.lexically_relative(p);
        if (rel.empty()) return false;
        auto it = rel.begin();
        return it != rel.end() && *it != "..";
    }
}

kString Manager::promptWorldSavePath()
{
    if (!projectOpened) return "";

    fs::path assetsDir = projectPath / "Assets";
    std::error_code ec;
    fs::create_directories(assetsDir, ec);

    showingMessageBox = true;
    std::string chosen = pfd::save_file(
        "Save World", assetsDir.string(),
        { "World Files", "*.world", "All Files", "*" }).result();
    showingMessageBox = false;

    if (chosen.empty()) return "";

    fs::path p = chosen;
    if (p.extension() != ".world")
        p += ".world";

    if (!isPathUnder(p, assetsDir))
    {
        showingMessageBox = true;
        pfd::message(
            "Invalid Save Location",
            "Worlds can only be saved inside the project's Assets folder.\n\n"
            "Selected:  " + p.string() + "\n"
            "Required:  " + assetsDir.string(),
            pfd::choice::ok, pfd::icon::warning).result();
        showingMessageBox = false;
        return "";
    }

    return p.string();
}

void Manager::applyDefaultSkybox(kScene *target)
{
    if (!target) return;
    kAssetManager *am = getAssetManager();
    if (!am) return;

    kShader      *skyShader = am->loadGlslFromResource("SHADER_SKYBOX");
    kMaterial    *skyMat    = am->createMaterial(skyShader);
    kTextureCube *skyTex    = am->loadTextureCubeFromResource(
        "TEXTURE_SKYBOX_RIGHT", "TEXTURE_SKYBOX_LEFT",
        "TEXTURE_SKYBOX_TOP",   "TEXTURE_SKYBOX_BOTTOM",
        "TEXTURE_SKYBOX_FRONT", "TEXTURE_SKYBOX_BACK",
        "cubeMap");
    skyMat->addTexture(skyTex);
    skyMat->setSingleSided(false);

    kMesh *skyMesh = kMeshGenerator::generateCube();
    skyMesh->setMaterial(skyMat);
    target->setSkybox(skyMat, skyMesh);
}

void Manager::saveWorld()
{
    if (!projectOpened || !world) return;

    fs::path assetsDir = projectPath / "Assets";

    // Untitled, or pointing somewhere it shouldn't be (e.g. an older project
    // that saved scene.world at the project root) — prompt for a new path
    // before writing.
    if (worldPath.empty() || !isPathUnder(worldPath, assetsDir))
    {
        kString chosen = promptWorldSavePath();
        if (chosen.empty()) return; // cancelled or rejected
        worldPath = chosen;
    }

    json data = world->serialize(1); // skip editor scene at index 0

    // Persist the editor viewport camera so reopening the world restores the
    // same viewpoint (position, orientation, and orbit framing state).
    if (editorCamera)
    {
        kVec3 cp = editorCamera->getPosition();
        kQuat cq = editorCamera->getRotation();
        data["editor_camera"] = {
            {"position", {{"x", cp.x}, {"y", cp.y}, {"z", cp.z}}},
            {"rotation", {{"x", cq.x}, {"y", cq.y}, {"z", cq.z}, {"w", cq.w}}},
            {"orbit_pivot", {{"x", editorCamOrbitPivot.x},
                             {"y", editorCamOrbitPivot.y},
                             {"z", editorCamOrbitPivot.z}}},
            {"orbit_distance", editorCamOrbitDistance},
            {"fov", editorCamera->getFOV()},
        };
    }

    try
    {
        std::ofstream f(worldPath);
        if (!f.is_open())
        {
            std::cerr << "saveWorld: cannot open " << worldPath << "\n";
            return;
        }
        f << data.dump(4);
        projectSaved = true;
        worldName = worldPath.stem().string();
        refreshWindowTitle();
        // Record this as the last-opened world so reopening the project
        // auto-loads it.
        saveProjectConfig();
        std::cout << "World saved: " << worldPath << "\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "saveWorld: " << e.what() << "\n";
    }
}

void Manager::saveWorldAs(const kString &path)
{
    if (!projectOpened || !world) return;

    fs::path p = path;
    if (p.extension() != ".world")
        p += ".world";

    if (!isPathUnder(p, projectPath / "Assets"))
    {
        showingMessageBox = true;
        pfd::message(
            "Invalid Save Location",
            "Worlds can only be saved inside the project's Assets folder.\n\n"
            "Refused:  " + p.string(),
            pfd::choice::ok, pfd::icon::warning).result();
        showingMessageBox = false;
        return;
    }

    worldPath = p;
    saveWorld();
}

void Manager::saveWorldAs()
{
    if (!projectOpened || !world) return;

    kString chosen = promptWorldSavePath();
    if (chosen.empty()) return;
    worldPath = chosen;
    saveWorld();
}

static void applyLightIconLoad(kLight *light, kAssetManager *am, const char *gizmoResource)
{
    kShader    *shader = am->loadGlslFromResource("SHADER_ICON");
    kMaterial  *mat    = am->createMaterial(shader);
    kTexture2D *tex    = am->loadTexture2DFromResource(gizmoResource, "albedoMap",
                                                        kTextureFormat::TEX_FORMAT_RGBA);
    mat->addTexture(tex);
    light->setMaterial(mat);
}

static kObject* loadObjectFromJson(const json &obj, kScene *scene, kWorld *world,
                                   kAssetManager *am, const fs::path &projectPath,
                                   kCamera *editorCamera, kObject *parent = nullptr)
{
    std::string type = obj.value("type", "object");
    std::string uuid = obj.value("uuid", "");
    std::string name = obj.value("name", "");
    bool active      = obj.value("active", true);

    auto readVec3xyz = [&](const json &j, const char *key, kVec3 def = kVec3(0)) -> kVec3 {
        if (!j.contains(key)) return def;
        const auto &v = j[key];
        return kVec3(v.value("x", def.x), v.value("y", def.y), v.value("z", def.z));
    };
    auto readVec3rgb = [&](const json &j, const char *key, kVec3 def = kVec3(1)) -> kVec3 {
        if (!j.contains(key)) return def;
        const auto &v = j[key];
        return kVec3(v.value("r", def.x), v.value("g", def.y), v.value("b", def.z));
    };

    kVec3 pos   = readVec3xyz(obj, "position");
    kVec3 rotEu = readVec3xyz(obj, "rotation");
    kVec3 scale = readVec3xyz(obj, "scale", kVec3(1.0f));

    // Top-level objects attach to the scene root via scene->add*; nested objects
    // (parent != nullptr) are created standalone and parented manually so they
    // don't end up double-tracked.
    bool topLevel = (parent == nullptr);

    kObject *result = nullptr;

    if (type == "mesh")
    {
        std::string refName = obj.value("reference", "");
        std::string fileName = obj.value("file_name", "");
        std::string primitive = obj.value("primitive", "");

        fs::path meshPath;
        if (!refName.empty())
            meshPath = projectPath / "Library" / "ImportedAssets" / (refName + ".glb");
        else if (!fileName.empty())
            meshPath = fs::path(fileName);

        kMesh *mesh = nullptr;
        if (!meshPath.empty() && fs::exists(meshPath))
            mesh = am->loadMesh(meshPath.string());

        // Procedurally-generated primitive (cube / sphere / etc.) — used by
        // res/default.world to seed a startup scene without shipping a model
        // file. Only kicks in when no asset file is present.
        if (!mesh && !primitive.empty())
        {
            if      (primitive == "cube")     mesh = kMeshGenerator::generateCube();
            else if (primitive == "sphere")   mesh = kMeshGenerator::generateSphere();
            else if (primitive == "cylinder") mesh = kMeshGenerator::generateCylinder();
            else if (primitive == "capsule")  mesh = kMeshGenerator::generateCapsule();
            else if (primitive == "plane")    mesh = kMeshGenerator::generatePlane();
            if (mesh && am)
                applyDefaultMaterial(mesh, am);
        }

        if (!mesh)
        {
            mesh = new kMesh();
            std::cerr << "loadObjectFromJson: mesh file not found for '" << name << "'\n";
        }

        mesh->setName(name);
        mesh->setActive(active);
        mesh->setCastShadow(obj.value("cast_shadow", true));
        mesh->setReceiveShadow(obj.value("receive_shadow", true));
        if (topLevel)
        {
            scene->addMesh(mesh, uuid);
        }
        else
        {
            mesh->setUuid(uuid.empty() ? generateUuid() : uuid);
            mesh->setParent(parent);
        }
        // Give the model's import-derived sub-meshes UUIDs so they show
        // uniquely in the hierarchy and can be selected (they remain excluded
        // from serialization via the import-child flag).
        assignImportChildUuidsRec(mesh);
        result = mesh;
    }
    else if (type == "light")
    {
        std::string ltStr = obj.value("light_type", "sun");
        kVec3 diff  = readVec3rgb(obj, "diffuse",  kVec3(1));
        kVec3 spec  = readVec3rgb(obj, "specular", kVec3(1));
        float power = obj.value("power", 1.0f);
        float constant  = obj.value("constant",  1.0f);
        float linear    = obj.value("linear",    0.7f);
        float quadratic = obj.value("quadratic", 1.8f);
        kVec3 dir   = readVec3xyz(obj, "direction", kVec3(0,-1,0));
        float cutOff      = obj.value("cut_off",       glm::cos(glm::radians(15.0f)));
        float outerCutOff = obj.value("outer_cut_off", glm::cos(glm::radians(20.0f)));

        kLight *light = nullptr;
        const char *gizmoRes = "GIZMO_SUN_LIGHT";

        if (topLevel)
        {
            if (ltStr == "point")
            {
                light = scene->addPointLight(pos, diff, spec, uuid);
                gizmoRes = "GIZMO_POINT_LIGHT";
            }
            else if (ltStr == "spot")
            {
                light = scene->addSpotLight(pos, diff, spec, uuid);
                gizmoRes = "GIZMO_SPOT_LIGHT";
            }
            else
            {
                light = scene->addSunLight(pos, dir, diff, spec, uuid);
                gizmoRes = "GIZMO_SUN_LIGHT";
            }
        }
        else
        {
            light = new kLight();
            if (ltStr == "point")     { light->setLightType(kLightType::LIGHT_TYPE_POINT); gizmoRes = "GIZMO_POINT_LIGHT"; }
            else if (ltStr == "spot") { light->setLightType(kLightType::LIGHT_TYPE_SPOT);  gizmoRes = "GIZMO_SPOT_LIGHT";  }
            else                      { light->setLightType(kLightType::LIGHT_TYPE_SUN);   gizmoRes = "GIZMO_SUN_LIGHT";   }
            light->setDiffuseColor(diff);
            light->setSpecularColor(spec);
            light->setUuid(uuid.empty() ? generateUuid() : uuid);
            light->setParent(parent);
        }

        light->setName(name);
        light->setActive(active);
        light->setPower(power);
        light->setConstant(constant);
        light->setLinear(linear);
        light->setQuadratic(quadratic);
        light->setDirection(dir);
        light->setCutOff(cutOff);
        light->setOuterCutOff(outerCutOff);
        if (am) applyLightIconLoad(light, am, gizmoRes);

        result = light;
    }
    else if (type == "camera")
    {
        std::string camTypeStr = obj.value("camera_type", "free");
        kCameraType camType = (camTypeStr == "locked") ? kCameraType::CAMERA_TYPE_LOCKED
                                                       : kCameraType::CAMERA_TYPE_FREE;
        kVec3 lookAt = readVec3xyz(obj, "look_at");
        float fov    = obj.value("fov", 60.0f);
        float nearClip = obj.value("near_clip", 0.1f);
        float farClip  = obj.value("far_clip",  1000.0f);

        kCamera *cam = new kCamera();
        cam->setName(name);
        cam->setActive(active);
        cam->setCameraType(camType);
        cam->setLookAt(lookAt);
        cam->setFOV(fov);
        cam->setNearClip(nearClip);
        cam->setFarClip(farClip);

        cam->setSceneUuid(obj.value("scene_uuid", std::string("")));

        if (topLevel)
        {
            scene->addObject(cam, uuid);
        }
        else
        {
            cam->setUuid(uuid.empty() ? generateUuid() : uuid);
            cam->setParent(parent);
        }
        // Cameras always register with the world so they can be selected as game cameras.
        world->addCamera(cam, cam->getUuid());

        // Editor-side gizmo (icon billboard) — the renderer auto-hides it when
        // the camera is also the active view camera.
        if (am) applyCameraIcon(cam, am);

        result = cam;
    }
    else
    {
        kObject *empty = new kObject();
        empty->setName(name);
        empty->setActive(active);
        if (topLevel)
        {
            scene->addObject(empty, uuid);
        }
        else
        {
            empty->setUuid(uuid.empty() ? generateUuid() : uuid);
            empty->setParent(parent);
        }
        result = empty;
    }

    if (result)
    {
        result->setPosition(pos);
        result->setRotation(kQuat(glm::radians(rotEu)));
        result->setScale(scale);

        // Prefab linkage — only set when present in JSON, otherwise stays empty.
        if (obj.contains("prefab_ref"))    result->setPrefabRef(obj["prefab_ref"].get<std::string>());
        if (obj.contains("template_uuid")) result->setTemplateUuid(obj["template_uuid"].get<std::string>());

        // Assigned material asset UUID. Only the reference is restored here; the
        // runtime material is rebuilt afterwards by Manager::reapplyStoredMaterials
        // (which has the fileMap needed to resolve the UUID to a .mat path).
        if (obj.contains("material_uuid"))
            result->setMaterialUuid(obj["material_uuid"].get<std::string>());

        // Physics body descriptor — fields mirror kPhysicsObjectDesc; missing
        // fields fall back to descriptor defaults.
        if (obj.contains("physics") && obj["physics"].is_object())
        {
            const json &phys = obj["physics"];
            kPhysicsObjectDesc &desc = result->getPhysicsDesc();
            desc.shape.type = (kPhysicsShapeType)phys.value("shape_type", 0);
            if (phys.contains("half_extents") && phys["half_extents"].is_object())
            {
                const auto &he = phys["half_extents"];
                desc.shape.halfExtents = kVec3(
                    he.value("x", 0.5f), he.value("y", 0.5f), he.value("z", 0.5f));
            }
            desc.shape.radius   = phys.value("radius", 0.5f);
            desc.shape.height   = phys.value("height", 1.0f);
            desc.type           = (kPhysicsObjectType)phys.value("body_type", 0);
            desc.mass           = phys.value("mass",            1.0f);
            desc.friction       = phys.value("friction",        0.5f);
            desc.restitution    = phys.value("restitution",     0.0f);
            desc.linearDamping  = phys.value("linear_damping",  0.05f);
            desc.angularDamping = phys.value("angular_damping", 0.05f);
            desc.gravityFactor  = phys.value("gravity_factor",  1.0f);
            result->setHasPhysicsDesc(true);
        }

        // Character controller descriptor.
        if (obj.contains("character") && obj["character"].is_object())
        {
            const json &ch = obj["character"];
            kCharacterControllerDesc &cd = result->getCharacterDesc();
            cd.radius        = ch.value("radius",         0.3f);
            cd.height        = ch.value("height",         1.8f);
            cd.mass          = ch.value("mass",           80.0f);
            cd.friction      = ch.value("friction",       0.5f);
            cd.gravityFactor = ch.value("gravity_factor", 1.0f);
            cd.slopeLimit    = ch.value("slope_limit",    45.0f);
            cd.stepHeight    = ch.value("step_height",    0.3f);
            result->setHasCharacterDesc(true);
        }

        // Navigation surface descriptor (bake settings only).
        if (obj.contains("navmesh_surface") && obj["navmesh_surface"].is_object())
        {
            const json &nm = obj["navmesh_surface"];
            kNavMeshDesc &nd = result->getNavMeshDesc();
            nd.useArea = nm.value("use_area", false);
            if (nm.contains("area_size") && nm["area_size"].is_object())
            {
                const auto &as = nm["area_size"];
                nd.areaSize = kVec3(as.value("x", 20.0f), as.value("y", 10.0f), as.value("z", 20.0f));
            }
            nd.config.cellSize      = nm.value("cell_size",       0.3f);
            nd.config.cellHeight    = nm.value("cell_height",     0.2f);
            nd.config.agentHeight   = nm.value("agent_height",    2.0f);
            nd.config.agentRadius   = nm.value("agent_radius",    0.6f);
            nd.config.agentMaxClimb = nm.value("agent_max_climb", 0.9f);
            nd.config.agentMaxSlope = nm.value("agent_max_slope", 45.0f);
            nd.config.tileSize      = nm.value("tile_size",       48.0f);
            result->setHasNavMeshDesc(true);
        }

        // Script components — restore each AngelScript attachment. The runtime
        // module is built later (on Play) by kWorld::startScripts().
        if (obj.contains("script") && obj["script"].is_array())
        {
            for (const auto &sj : obj["script"])
            {
                kScript s;
                s.uuid       = sj.value("uuid", generateUuid());
                s.scriptUuid = sj.value("script_uuid", std::string(""));
                s.fileName   = sj.value("file_name",   std::string(""));
                s.checksum   = sj.value("checksum",    std::string(""));
                s.isActive   = sj.value("active", true);
                result->addScript(s);
            }
        }

        // Recursively load children, parented to this node.
        if (obj.contains("children") && obj["children"].is_array())
        {
            for (const auto &child : obj["children"])
                loadObjectFromJson(child, scene, world, am, projectPath, editorCamera, result);
        }
    }

    return result;
}

void Manager::loadWorld(const kString &path)
{
    if (!projectOpened || !world) return;

    fs::path loadPath = path;
    if (!fs::exists(loadPath))
    {
        std::cerr << "loadWorld: file not found: " << path << "\n";
        return;
    }

    // Old baked nav meshes are keyed by now-stale object UUIDs — drop them.
    clearAllNavMeshes();

    json data;
    try
    {
        std::ifstream f(loadPath);
        data = json::parse(f);
    }
    catch (const std::exception &e)
    {
        std::cerr << "loadWorld: parse error: " << e.what() << "\n";
        return;
    }

    // Remove non-editor cameras from world first (before deleting scene objects)
    {
        auto cams = world->getCameras();
        for (kCamera *c : cams)
            if (c != editorCamera)
                world->removeCamera(c);
    }

    // Clear game scenes (keep editor scene at index 0)
    {
        auto scenes = world->getScenes();
        for (size_t i = 1; i < scenes.size(); ++i)
        {
            kScene *s = scenes[i];
            if (s && s->getRootNode())
            {
                for (kObject *child : s->getRootNode()->getChildren())
                    deleteObjectRecursive(child);
                delete s->getRootNode();
            }
            world->removeScene(s);
            delete s;
        }
    }
    defaultGameCamera = nullptr;
    selectedObject    = nullptr;
    selectedScene     = nullptr;
    selectedObjects.clear();

    kAssetManager *am = getAssetManager();

    if (!data.contains("scenes") || !data["scenes"].is_array())
    {
        std::cerr << "loadWorld: no scenes array in file\n";
        return;
    }

    for (const auto &sceneJson : data["scenes"])
    {
        std::string sceneUuid = sceneJson.value("uuid", "");
        std::string sceneName = sceneJson.value("name", "Scene");
        bool sceneActive      = sceneJson.value("active", true);

        kScene *scene = world->createScene(sceneName, sceneUuid);
        scene->setActive(sceneActive);

        if (sceneJson.contains("ambient_light"))
        {
            const auto &al = sceneJson["ambient_light"];
            scene->setAmbientLightColor(kVec3(al.value("r", 0.1f), al.value("g", 0.1f), al.value("b", 0.1f)));
        }
        scene->setShadowsEnabled(sceneJson.value("shadows_enabled", true));
        scene->setShadowBias(sceneJson.value("shadow_bias", 0.0008f));
        scene->setShadowNormalBias(sceneJson.value("shadow_normal_bias", 0.003f));
        scene->setShadowMapResolution(sceneJson.value("shadow_map_resolution", 2048));
        scene->setShadowSoftness(sceneJson.value("shadow_softness", 1.5f));
        scene->setSkyboxAmbientEnabled(sceneJson.value("skybox_ambient_enabled", false));
        scene->setSkyboxAmbientStrength(sceneJson.value("skybox_ambient_strength", 1.0f));

        if (sceneJson.contains("objects") && sceneJson["objects"].is_array())
        {
            for (const auto &objJson : sceneJson["objects"])
            {
                loadObjectFromJson(objJson, scene, world, am, projectPath, editorCamera);
            }
        }

        // Use this scene as the main game scene
        setScene(scene);
    }

    // Second pass: rebuild runtime materials from the per-object material UUIDs
    // restored during load. Done here (not in loadObjectFromJson) because it
    // needs fileMap to resolve a UUID to its .mat path.
    reapplyStoredMaterials();

    // Restore the editor viewport camera to where it was when saved. Orbit
    // pivot/distance are staged for main.cpp via editorCamLoadPending so its
    // interaction state matches the restored pose.
    if (editorCamera && data.contains("editor_camera") && data["editor_camera"].is_object())
    {
        const json &ec = data["editor_camera"];
        if (ec.contains("position"))
        {
            const auto &p = ec["position"];
            editorCamera->setPosition(kVec3(p.value("x", 0.0f), p.value("y", 0.0f), p.value("z", 0.0f)));
        }
        if (ec.contains("rotation"))
        {
            const auto &r = ec["rotation"];
            // glm::quat ctor is (w, x, y, z).
            editorCamera->setRotation(kQuat(
                r.value("w", 1.0f), r.value("x", 0.0f), r.value("y", 0.0f), r.value("z", 0.0f)));
        }
        if (ec.contains("orbit_pivot"))
        {
            const auto &op = ec["orbit_pivot"];
            editorCamOrbitPivot = kVec3(op.value("x", 0.0f), op.value("y", 0.0f), op.value("z", 0.0f));
        }
        editorCamOrbitDistance = ec.value("orbit_distance", editorCamOrbitDistance);
        if (ec.contains("fov"))
            editorCamera->setFOV(ec.value("fov", 60.0f));
        editorCamLoadPending = true;
    }

    worldName    = loadPath.stem().string();
    worldPath    = loadPath;
    projectSaved = true;
    refreshWindowTitle();

    if (panelHierarchy)
        panelHierarchy->refreshList();

    std::cout << "World loaded: " << loadPath << "\n";
}

void Manager::loadDefaultWorldInto(kScene *target)
{
    if (!target || !world)
        return;

    HRSRC hRes = FindResource(NULL, "WORLD_DEFAULT", RT_RCDATA);
    if (!hRes)
    {
        std::cerr << "loadDefaultWorldInto: WORLD_DEFAULT resource not found\n";
        return;
    }
    HGLOBAL hData = LoadResource(NULL, hRes);
    DWORD size = SizeofResource(NULL, hRes);
    const char *bytes = static_cast<const char *>(LockResource(hData));
    if (!bytes || size == 0)
    {
        std::cerr << "loadDefaultWorldInto: WORLD_DEFAULT resource is empty\n";
        return;
    }

    json data;
    try
    {
        data = json::parse(bytes, bytes + size);
    }
    catch (const std::exception &e)
    {
        std::cerr << "loadDefaultWorldInto: parse error: " << e.what() << "\n";
        return;
    }

    if (!data.contains("scenes") || !data["scenes"].is_array() || data["scenes"].empty())
    {
        std::cerr << "loadDefaultWorldInto: no scenes in WORLD_DEFAULT\n";
        return;
    }

    // We only consume the first scene — the default world is intentionally
    // a single-scene placeholder; additional scenes would need their own
    // kScene allocation, which is outside this helper's contract.
    const auto &sceneJson = data["scenes"][0];

    if (sceneJson.contains("ambient_light"))
    {
        const auto &al = sceneJson["ambient_light"];
        target->setAmbientLightColor(kVec3(al.value("r", 0.1f),
                                           al.value("g", 0.1f),
                                           al.value("b", 0.1f)));
    }
    target->setShadowsEnabled(sceneJson.value("shadows_enabled", true));
    target->setShadowBias(sceneJson.value("shadow_bias", 0.0008f));
    target->setShadowNormalBias(sceneJson.value("shadow_normal_bias", 0.003f));
    target->setShadowMapResolution(sceneJson.value("shadow_map_resolution", 2048));
    target->setShadowSoftness(sceneJson.value("shadow_softness", 1.5f));
    target->setSkyboxAmbientEnabled(sceneJson.value("skybox_ambient_enabled", false));
    target->setSkyboxAmbientStrength(sceneJson.value("skybox_ambient_strength", 1.0f));

    if (sceneJson.contains("objects") && sceneJson["objects"].is_array())
    {
        kAssetManager *am = getAssetManager();
        for (const auto &objJson : sceneJson["objects"])
            loadObjectFromJson(objJson, target, world, am, projectPath, editorCamera);
    }
}

// ---------------------------------------------------------------------------
// Prefabs
// ---------------------------------------------------------------------------

// Forward declaration so prefab functions can push InstantiateCommands.
// Definition lives in the drag-and-drop helpers section further below.
static void pushInstantiateCommand(Manager *mgr, kObject *root);

bool Manager::saveSelectedAsPrefab(const kString &prefabName)
{
    if (!projectOpened || !selectedObject)
    {
        std::cerr << "saveSelectedAsPrefab: no project or no selection\n";
        return false;
    }

    // Serialize the selected object subtree (kObject::serialize already recurses
    // into children, so the full tree is captured).
    json rootJson = selectedObject->serialize();

    kPrefab prefab;
    prefab.setUuid(generateUuid());
    prefab.setName(prefabName);
    prefab.setRootJson(rootJson);

    // Place the .prefab into the currently-browsed project folder, picking a
    // unique filename if one already exists.
    fs::path dir = getCurrentDirPath();
    fs::path filePath = dir / (prefabName + ".prefab");
    int counter = 1;
    while (fs::exists(filePath))
    {
        filePath = dir / (prefabName + " " + std::to_string(counter) + ".prefab");
        counter++;
    }

    if (!prefab.saveToFile(filePath.string()))
        return false;

    checkAssetChange();
    if (panelProject != nullptr)
    {
        panelProject->triggerRefresh();
        panelProject->pendingSelectUuid = prefab.getUuid();
    }
    return true;
}

kObject *Manager::instantiatePrefabInScene(const fs::path &prefabPath)
{
    if (!projectOpened || !scene)
        return nullptr;

    kPrefab prefab;
    if (!prefab.loadFromFile(prefabPath.string()))
        return nullptr;

    // Generate fresh per-node UUIDs while preserving template UUIDs so the
    // editor can match instance nodes back to the prefab template later.
    json instanceJson = kPrefab::instantiateJson(prefab.getRootJson());

    kAssetManager *am = getAssetManager();
    kObject *root = loadObjectFromJson(instanceJson, scene, world, am,
                                       projectPath, editorCamera, nullptr);
    if (!root)
        return nullptr;

    // Tag the instance root with the source prefab UUID. Each instance node
    // already has its own template_uuid set by instantiateJson + the recursive
    // loader, but the prefab_ref only belongs on the root.
    root->setPrefabRef(prefab.getUuid());

    if (panelHierarchy)
        panelHierarchy->refreshList();

    selectedObject = root;
    selectObject(root->getUuid(), true);
    projectSaved = false;
    refreshWindowTitle();

    pushInstantiateCommand(this, root);
    return root;
}

void Manager::editPrefab(const fs::path &prefabPath)
{
    if (!projectOpened || !world)
        return;

    // If we're already editing a different prefab, save & close it first.
    if (prefabEditing)
        closePrefabEditor(true);

    if (!editingPrefab.loadFromFile(prefabPath.string()))
    {
        std::cerr << "editPrefab: failed to load " << prefabPath << "\n";
        return;
    }

    editingPrefabPath = prefabPath;

    // Build an isolated scene + camera for the prefab editor. The prefab is
    // loaded with its template UUIDs preserved (no instantiateJson) so saving
    // round-trips cleanly back to the same node identities.
    prefabScene = world->createScene("__PrefabEdit__");

    kAssetManager *am = getAssetManager();
    prefabRoot = loadObjectFromJson(editingPrefab.getRootJson(),
                                    prefabScene, world, am,
                                    projectPath, editorCamera, nullptr);

    // Position an editor camera so the prefab is framed reasonably. The camera
    // is registered with the world so the renderer can use it; we tag it with a
    // sentinel scene_uuid that the game-camera picker should ignore.
    prefabCamera = world->addCamera(kVec3(-5, 3, 8), kVec3(0, 1, 0),
                                    kCameraType::CAMERA_TYPE_FREE);
    prefabCamera->setFOV(60.0f);
    prefabCamera->setName("__PrefabEditCamera__");

    prefabEditing = true;

    // Clear the main scene's selection — the prefab editor has its own context.
    selectedObjects.clear();
    selectedObject = prefabRoot;
    if (prefabRoot)
        selectedObjects.push_back(prefabRoot->getUuid());

    if (panelHierarchy)
        panelHierarchy->refreshList();
}

void Manager::closePrefabEditor(bool saveChanges)
{
    if (!prefabEditing) return;

    bool savedSuccessfully = false;
    kString savedPrefabUuid;

    if (saveChanges && prefabRoot)
    {
        // Serialize the (possibly-edited) root subtree back into the prefab JSON
        // and write the .prefab file. UUIDs are preserved because we never
        // re-randomized them on load.
        editingPrefab.setRootJson(prefabRoot->serialize());
        savedSuccessfully = editingPrefab.saveToFile(editingPrefabPath.string());
        savedPrefabUuid   = editingPrefab.getUuid();
    }

    // Tear down the editor scene. Children are owned by the scene-graph nodes,
    // so deleting children of the root recursively frees the prefab subtree.
    if (prefabScene)
    {
        if (prefabScene->getRootNode())
        {
            for (kObject *child : prefabScene->getRootNode()->getChildren())
                deleteObjectRecursive(child);
        }
        if (world) world->removeScene(prefabScene);
        delete prefabScene;
        prefabScene = nullptr;
    }

    if (prefabCamera && world)
    {
        world->removeCamera(prefabCamera);
        delete prefabCamera;
        prefabCamera = nullptr;
    }

    prefabRoot = nullptr;
    prefabEditing = false;
    editingPrefabPath.clear();

    selectedObjects.clear();
    selectedObject = nullptr;

    // Propagate template edits to live instances. Each instance's world
    // transform (offset, rotation, scale) is preserved by
    // refreshAllPrefabInstances; per-instance subtree edits are dropped, since
    // those are only kept until the user clicks "Apply to Prefab".
    if (savedSuccessfully && !savedPrefabUuid.empty())
        refreshAllPrefabInstances(savedPrefabUuid);

    if (panelHierarchy)
        panelHierarchy->refreshList();
}

// ---------------------------------------------------------------------------
// Physics simulation
// ---------------------------------------------------------------------------
//
// On Play we walk the active scene, build a Jolt body per kObject that has
// a physics descriptor, and attach it via kObject::attachPhysics. The body's
// initial transform is taken from the object's CURRENT world transform, not
// the descriptor's stored position — the descriptor's position field is
// effectively unused after creation since the editor authors transforms via
// kObject::setPosition / setRotation directly.
//
// On Stop we destroy every body and detach. The kObject's local transform is
// restored separately by PanelGame's snapshot mechanism.

void Manager::startPhysicsSimulation()
{
    if (!scene) return;
    if (!physicsManager)
    {
        physicsManager = createPhysicsManager();
        if (!physicsManager || !physicsManager->init())
        {
            std::cerr << "Physics: failed to initialise kPhysicsManager\n";
            physicsManager = nullptr;
            return;
        }
    }

    physicsBodies.clear();
    characterBodies.clear();

    std::function<void(kObject *)> walk = [&](kObject *node) {
        if (!node) return;
        if (node->getHasPhysicsDesc())
        {
            kPhysicsObjectDesc desc = node->getPhysicsDesc();
            // Seed the body from the object's current world transform so it
            // appears where the editor placed it, not at the descriptor's
            // (default-zero) position.
            desc.position = node->getGlobalPosition();
            desc.rotation = node->getGlobalRotation();
            kPhysicsObject *body = physicsManager->createObject(desc);
            if (body)
            {
                node->attachPhysics(body);
                physicsBodies.push_back(node);
            }
            else
            {
                std::cerr << "Physics: createObject failed for '" << node->getName() << "'\n";
            }
        }
        // A character controller is independent of the rigid-body descriptor.
        if (node->getHasCharacterDesc())
        {
            kCharacterControllerDesc cd = node->getCharacterDesc();
            cd.position = node->getGlobalPosition();
            cd.rotation = node->getGlobalRotation();
            kCharacterController *cc = physicsManager->createCharacter(cd);
            if (cc)
            {
                node->attachCharacter(cc);
                characterBodies.push_back(node);
            }
            else
            {
                std::cerr << "Physics: createCharacter failed for '" << node->getName() << "'\n";
            }
        }
        for (kObject *c : node->getChildren()) walk(c);
    };
    walk(scene->getRootNode());
}

void Manager::stopPhysicsSimulation()
{
    if (!physicsManager) return;
    for (kObject *obj : physicsBodies)
    {
        if (kPhysicsObject *body = obj->getPhysicsObject())
        {
            obj->detachPhysics();
            physicsManager->destroyObject(body);
        }
    }
    physicsBodies.clear();

    for (kObject *obj : characterBodies)
    {
        if (kCharacterController *cc = obj->getCharacterController())
        {
            obj->detachCharacter();
            physicsManager->destroyCharacter(cc);
        }
    }
    characterBodies.clear();
}

void Manager::stepPhysics(float dt)
{
    if (!physicsManager || dt <= 0.0f) return;
    physicsManager->update(dt);
    // Sync each body's transform back into the scene-graph node so the
    // renderer picks up the new positions on the next draw.
    for (kObject *obj : physicsBodies)
        if (obj->getPhysicsObject())
            obj->syncFromPhysics();

    // Characters update their ground state inside physicsManager->update();
    // copy their resolved position back into the scene node.
    for (kObject *obj : characterBodies)
        if (obj->getCharacterController())
            obj->syncFromCharacter();
}

// ---------------------------------------------------------------------------
// Scripting
//
// Each attached AngelScript source is compiled to bytecode under
// Library/Scripts/<scriptUuid>.kbc. A script asset is identified per distinct
// source file, so two objects sharing one .as file share one compiled module
// asset. Compilation is checksum-gated: an unchanged source is not rebuilt.
// ---------------------------------------------------------------------------

void Manager::buildScripts()
{
    if (!world || !scene) return;
    kScriptManager *sm = world->getScriptManager();
    if (!sm) return;

    fs::path scriptsDir = projectPath / "Library" / "Scripts";
    std::error_code ec;
    fs::create_directories(scriptsDir, ec);

    std::map<kString, kString> fileToAsset;   // source path -> script-asset UUID
    std::map<kString, bool>    fileCompiled;  // script-asset UUID -> compile OK
    bool changedMeta = false;

    std::function<void(kObject *)> walk = [&](kObject *node) {
        if (!node) return;
        for (kScript &comp : node->getScripts())
        {
            if (comp.fileName.empty())
                continue;
            if (!fs::exists(fs::path(comp.fileName)))
            {
                std::cerr << "buildScripts: source missing: " << comp.fileName << "\n";
                continue;
            }

            // One script-asset UUID per distinct source file.
            auto fit = fileToAsset.find(comp.fileName);
            bool firstForFile = (fit == fileToAsset.end());
            kString sid = firstForFile
                ? (comp.scriptUuid.empty() ? generateUuid() : comp.scriptUuid)
                : fit->second;

            kString srcSum = generateFileChecksum(comp.fileName);
            fs::path bc    = scriptsDir / (sid + ".kbc");

            if (firstForFile)
            {
                fileToAsset[comp.fileName] = sid;
                sm->registerScriptAsset(sid, comp.fileName);

                bool ok = true;
                if (comp.checksum != srcSum || !fs::exists(bc))
                {
                    ok = sm->compileToBytecode(sid, bc.string());
                    if (!ok)
                        std::cerr << "buildScripts: compile failed: " << comp.fileName << "\n";
                }
                else
                {
                    sm->setBytecodePath(sid, bc.string());
                }
                fileCompiled[sid] = ok;
            }

            if (comp.scriptUuid != sid) { comp.scriptUuid = sid; changedMeta = true; }
            // Only record the checksum once the source actually compiled, so a
            // broken script is retried on the next build.
            if (fileCompiled[sid] && comp.checksum != srcSum)
            {
                comp.checksum = srcSum;
                changedMeta = true;
            }
        }
        for (kObject *c : node->getChildren())
            walk(c);
    };
    walk(scene->getRootNode());

    if (changedMeta)
        projectSaved = false;
}

void Manager::startScripts()
{
    if (!world) return;
    buildScripts();          // ensure bytecode is fresh
    world->startScripts();   // Awake() + Start() across the scene
}

void Manager::stopScripts()
{
    if (world)
        world->stopScripts();
}

void Manager::pollScriptChanges(float dt)
{
    // File-watch: while the editor is idle, periodically recompile any script
    // whose source changed on disk. buildScripts() is checksum-gated, so this
    // only does real work when a .as file was actually saved.
    if (!projectOpened || !world)
        return;
    scriptWatchTimer += dt;
    if (scriptWatchTimer < 1.5f)
        return;
    scriptWatchTimer = 0.0f;
    buildScripts();
}

// ---------------------------------------------------------------------------
// Drag-and-drop helpers
// ---------------------------------------------------------------------------

// Recursive tree walk to find any descendant by UUID. The cheap findObjectByUuid
// only scans direct children of the scene root; reparenting and prefab-aware
// operations need the full tree.
static kObject *findInTree(kObject *node, const kString &uuid)
{
    if (!node) return nullptr;
    if (node->getUuid() == uuid) return node;
    for (kObject *child : node->getChildren())
    {
        kObject *found = findInTree(child, uuid);
        if (found) return found;
    }
    return nullptr;
}

fs::path Manager::findAssetPathByUuid(const kString &assetUuid)
{
    auto it = fileMap.find(assetUuid);
    if (it == fileMap.end()) return {};
    return projectPath / "Assets" / it->second.path;
}

// Push an InstantiateCommand for a freshly-spawned subtree rooted at `root`.
// All instantiate-* helpers funnel through this so the resulting commands
// match in shape (and undo behavior).
static void pushInstantiateCommand(Manager *mgr, kObject *root)
{
    if (!root || !mgr || !mgr->getScene()) return;
    auto cmd = std::make_unique<InstantiateCommand>();
    cmd->manager = mgr;
    cmd->root    = root;
    cmd->parent  = root->getParent();
    cmd->scene   = mgr->getScene();
    // nextSibling: the sibling immediately following `root` in parent's
    // children list, or nullptr if it's the last child. Used by undo→redo
    // round-trips to preserve original sibling order.
    if (cmd->parent)
    {
        auto kids = cmd->parent->getChildren();
        for (size_t i = 0; i < kids.size(); ++i)
            if (kids[i] == root && i + 1 < kids.size()) { cmd->nextSibling = kids[i + 1]; break; }
    }
    cmd->attached    = true;
    cmd->ownsSubtree = false;
    mgr->undoRedo.push(std::move(cmd));
}

kObject *Manager::instantiateAssetFromUuid(const kString &assetUuid, const kVec3 &positionHint)
{
    if (!projectOpened || !scene) return nullptr;

    auto it = fileMap.find(assetUuid);
    if (it == fileMap.end()) return nullptr;
    const FileInfo &info = it->second;
    fs::path assetPath = projectPath / "Assets" / info.path;
    kAssetManager *am  = getAssetManager();

    if (info.type == "mesh")
    {
        // Imported mesh lives under Library/ImportedAssets/<uuid>.glb.
        fs::path glb = projectPath / "Library" / "ImportedAssets" / (assetUuid + ".glb");
        kMesh *mesh = nullptr;
        if (fs::exists(glb))
            mesh = am->loadMesh(glb.string());
        if (!mesh)
            mesh = new kMesh();
        if (am) applyDefaultMaterial(mesh, am);
        assignImportChildUuidsRec(mesh);

        mesh->setName(fs::path(info.path).stem().string());
        scene->addMesh(mesh);
        mesh->setPosition(positionHint);
        kString uuid = mesh->getUuid();

        if (panelHierarchy) panelHierarchy->refreshList();
        selectedObject = mesh;
        selectObject(uuid, true);
        projectSaved = false;
        refreshWindowTitle();
        pushInstantiateCommand(this, mesh);
        return mesh;
    }
    else if (info.type == "prefab")
    {
        // instantiatePrefabInScene already pushes its own InstantiateCommand
        // (added below), so we just forward.
        kObject *root = instantiatePrefabInScene(assetPath);
        if (root) root->setPosition(positionHint);
        return root;
    }
    else if (info.type == "audio" || info.type == "particle")
    {
        // Spawn an empty object carrying the descriptor. The descriptor is
        // stored on the kObject; runtime spawns the actual source at game-start.
        kObject *obj = new kObject();
        obj->setName(fs::path(info.path).stem().string());
        scene->addObject(obj);
        obj->setPosition(positionHint);

        if (info.type == "audio")
        {
            kAudioSource src;
            src.uuid      = generateUuid();
            src.audioFile = assetUuid;  // reference the audio asset by its project UUID
            obj->addAudioSource(src);
        }
        else
        {
            // .particle files only define parameters; the runtime kParticle
            // descriptor stores them inline. For now we create a default
            // emitter and tag its name with the source asset uuid so the
            // editor can later round-trip the params in/out of the file.
            kParticle p;
            p.uuid = generateUuid();
            p.name = fs::path(info.path).stem().string();
            obj->addParticle(p);
        }

        if (panelHierarchy) panelHierarchy->refreshList();
        selectedObject = obj;
        selectObject(obj->getUuid(), true);
        projectSaved = false;
        refreshWindowTitle();
        pushInstantiateCommand(this, obj);
        return obj;
    }

    // Unknown / unsupported asset type — silently no-op.
    return nullptr;
}

// Returns the sibling immediately following `obj` in its parent's children
// list, or nullptr if `obj` is the last child or has no parent. Used to
// snapshot sibling order for reparent/reorder undo.
static kObject *nextSiblingOf(kObject *obj)
{
    if (!obj || !obj->getParent()) return nullptr;
    auto kids = obj->getParent()->getChildren();
    for (size_t i = 0; i < kids.size(); ++i)
        if (kids[i] == obj && i + 1 < kids.size())
            return kids[i + 1];
    return nullptr;
}

// Insert `child` before `beforeSibling` in `parent`'s children list. Mirror
// of the helper in commands.cpp (kept duplicated to avoid leaking it through
// a public header).
static void insertBefore(kObject *parent, kObject *child, kObject *beforeSibling)
{
    if (!parent || !child) return;
    child->detachFromParent();
    if (!beforeSibling)
    {
        child->setParent(parent);
        return;
    }
    auto kids = parent->getChildren();
    std::vector<kObject *> tail;
    bool found = false;
    for (kObject *k : kids)
    {
        if (k == beforeSibling) found = true;
        if (found) tail.push_back(k);
    }
    for (kObject *k : tail) k->detachFromParent();
    child->setParent(parent);
    for (kObject *k : tail) k->setParent(parent);
}

void Manager::reparentObject(const kString &uuid, const kString &newParentUuid)
{
    if (!scene) return;
    kObject *obj = findInTree(scene->getRootNode(), uuid);
    if (!obj) return;

    kObject *newParent = newParentUuid.empty()
                          ? scene->getRootNode()
                          : findInTree(scene->getRootNode(), newParentUuid);
    if (!newParent) return;

    // Refuse the move if it would create a cycle (new parent is the obj or
    // a descendant of it).
    for (kObject *p = newParent; p != nullptr; p = p->getParent())
        if (p == obj) return;

    if (obj->getParent() == newParent) return;

    auto cmd = std::make_unique<ReparentCommand>();
    cmd->manager        = this;
    cmd->objUuid        = uuid;
    cmd->oldParent      = obj->getParent();
    cmd->oldNextSibling = nextSiblingOf(obj);
    cmd->oldLocalPos    = obj->getPosition();
    cmd->oldLocalRot    = obj->getRotation();
    cmd->oldLocalScale  = obj->getScale();

    // Reparent while preserving the object's world transform so the user
    // doesn't see the object jump when dragging it under a new parent.
    obj->setParentKeepTransform(newParent);

    cmd->newParent      = newParent;
    cmd->newNextSibling = nextSiblingOf(obj);
    cmd->newLocalPos    = obj->getPosition();
    cmd->newLocalRot    = obj->getRotation();
    cmd->newLocalScale  = obj->getScale();
    undoRedo.push(std::move(cmd));

    if (panelHierarchy) panelHierarchy->refreshList();
    projectSaved = false;
    refreshWindowTitle();
}

void Manager::reorderBefore(const kString &uuid, const kString &siblingUuid)
{
    if (!scene) return;
    kObject *obj = findInTree(scene->getRootNode(), uuid);
    kObject *sib = findInTree(scene->getRootNode(), siblingUuid);
    if (!obj || !sib) return;
    if (obj->getParent() != sib->getParent()) return;

    kObject *parent = obj->getParent();
    if (!parent) return;

    auto cmd = std::make_unique<ReparentCommand>();
    cmd->manager        = this;
    cmd->objUuid        = uuid;
    cmd->oldParent      = parent;
    cmd->oldNextSibling = nextSiblingOf(obj);
    cmd->oldLocalPos    = obj->getPosition();

    insertBefore(parent, obj, sib);

    cmd->newParent      = parent;
    cmd->newNextSibling = nextSiblingOf(obj);
    cmd->newLocalPos    = obj->getPosition();
    undoRedo.push(std::move(cmd));

    if (panelHierarchy) panelHierarchy->refreshList();
    projectSaved = false;
    refreshWindowTitle();
}

kObject *Manager::findPrefabInstanceRoot(kObject *obj)
{
    for (kObject *p = obj; p != nullptr; p = p->getParent())
        if (!p->getPrefabRef().empty())
            return p;
    return nullptr;
}

bool Manager::createPrefabFromSelection()
{
    if (!projectOpened || !scene) return false;
    if (selectedObjects.empty())  return false;

    // Resolve the selected objects in their original list order. The "last
    // selected" is the one held in `selectedObject` (if it's in the list),
    // matching how the rest of the editor treats the active selection.
    std::vector<kObject *> objs;
    for (const kString &uuid : selectedObjects)
    {
        kObject *o = findInTree(scene->getRootNode(), uuid);
        if (o) objs.push_back(o);
    }
    if (objs.empty()) return false;

    kObject *lastSel = selectedObject ? selectedObject : objs.back();

    auto cmd = std::make_unique<CreatePrefabCommand>();
    cmd->manager = this;

    kObject *root = nullptr;
    bool createdEmpty = false;

    if (objs.size() == 1)
    {
        // Single selection — the object itself becomes the prefab root.
        root = objs[0];
    }
    else
    {
        // Multi-select: create an empty parent at the last-selected's position,
        // reparent every selected top-level object under it, save that as the
        // prefab.
        kObject *empty = new kObject();
        empty->setName(lastSel->getName().empty()
                          ? kString("PrefabGroup")
                          : (lastSel->getName() + "_Group"));
        scene->addObject(empty);
        empty->setPosition(lastSel->getPosition());

        cmd->createdEmpty       = empty;
        cmd->createdEmptyParent = scene->getRootNode();
        cmd->createdEmptyPos    = empty->getPosition();

        for (kObject *o : objs)
        {
            // Snapshot pre-move state so undo can restore parent + sibling +
            // full local transform exactly.
            CreatePrefabCommand::ChildMove m;
            m.objUuid        = o->getUuid();
            m.oldParent      = o->getParent();
            m.oldNextSibling = nextSiblingOf(o);
            m.oldLocalPos    = o->getPosition();
            m.oldLocalRot    = o->getRotation();
            m.oldLocalScale  = o->getScale();

            // Reparent under the wrapper while preserving the world transform so
            // the grouped objects don't visually shift.
            o->setParentKeepTransform(empty);

            m.newLocalPos    = o->getPosition();
            m.newLocalRot    = o->getRotation();
            m.newLocalScale  = o->getScale();
            cmd->childMoves.push_back(m);
        }
        root = empty;
        createdEmpty = true;
    }

    kString prefabName = root->getName().empty() ? kString("New Prefab") : root->getName();

    // Build the template JSON from the in-scene subtree, then walk it to
    // assign fresh template UUIDs while remembering the matching scene UUIDs
    // so we can stamp template_uuid back onto the in-scene objects.
    json templateJson = root->serialize();

    std::unordered_map<kString, kString> sceneToTemplate;
    std::function<void(json &)> assignTemplateUuids = [&](json &node) {
        kString sceneUuid = node.value("uuid", kString(""));
        kString tplUuid   = generateUuid();
        node["uuid"] = tplUuid;
        // Strip prefab linkage from the template — only instances carry it.
        node.erase("prefab_ref");
        node.erase("template_uuid");
        if (!sceneUuid.empty())
            sceneToTemplate[sceneUuid] = tplUuid;
        if (node.contains("children") && node["children"].is_array())
            for (auto &c : node["children"])
                assignTemplateUuids(c);
    };
    assignTemplateUuids(templateJson);

    // Write the .prefab into the currently-browsed project folder, picking a
    // unique filename if one already exists.
    fs::path dir = getCurrentDirPath();
    fs::path filePath = dir / (prefabName + ".prefab");
    int counter = 1;
    while (fs::exists(filePath))
    {
        filePath = dir / (prefabName + " " + std::to_string(counter) + ".prefab");
        counter++;
    }

    kPrefab prefab;
    prefab.setUuid(generateUuid());
    prefab.setName(prefabName);
    prefab.setRootJson(templateJson);
    if (!prefab.saveToFile(filePath.string()))
    {
        // If we created a wrapper empty for multi-select but failed to save,
        // tear it down so the scene isn't left in a half-baked state.
        if (createdEmpty && root)
            scene->removeObject(root);
        return false;
    }

    // Stamp every in-scene node with the matching template_uuid so the subtree
    // becomes a live instance of the prefab we just created. Capture before/
    // after stamps so the command can revert them on undo.
    std::function<void(kObject *)> stampInstance = [&](kObject *node) {
        auto it = sceneToTemplate.find(node->getUuid());
        if (it != sceneToTemplate.end())
        {
            CreatePrefabCommand::StampChange s;
            s.objUuid         = node->getUuid();
            s.oldPrefabRef    = node->getPrefabRef();
            s.oldTemplateUuid = node->getTemplateUuid();
            s.newPrefabRef    = ""; // root gets prefab_ref below; descendants stay empty
            s.newTemplateUuid = it->second;
            cmd->stamps.push_back(s);

            node->setTemplateUuid(it->second);
        }
        for (kObject *c : node->getChildren())
            stampInstance(c);
    };
    stampInstance(root);

    // The prefab_ref belongs only on the root. Patch the corresponding stamp
    // entry so the command reproduces this on redo.
    {
        CreatePrefabCommand::StampChange s;
        s.objUuid         = root->getUuid();
        s.oldPrefabRef    = root->getPrefabRef();
        s.oldTemplateUuid = root->getTemplateUuid(); // already set above
        s.newPrefabRef    = prefab.getUuid();
        s.newTemplateUuid = root->getTemplateUuid();
        cmd->stamps.push_back(s);
        root->setPrefabRef(prefab.getUuid());
    }

    selectedObject = root;
    selectObject(root->getUuid(), true);

    if (panelHierarchy) panelHierarchy->refreshList();
    checkAssetChange();
    if (panelProject)
    {
        panelProject->triggerRefresh();
        panelProject->pendingSelectUuid = prefab.getUuid();
    }
    projectSaved = false;
    refreshWindowTitle();

    undoRedo.push(std::move(cmd));
    return true;
}

bool Manager::applyPrefabInstance(kObject *instanceRoot)
{
    if (!instanceRoot || instanceRoot->getPrefabRef().empty()) return false;

    fs::path prefabPath;
    {
        kString refUuid = instanceRoot->getPrefabRef();
        for (const auto &p : fs::recursive_directory_iterator(projectPath / "Assets"))
        {
            if (!p.is_regular_file()) continue;
            if (p.path().extension() != ".prefab") continue;
            kPrefab tmp;
            if (tmp.loadFromFile(p.path().string()) && tmp.getUuid() == refUuid)
            {
                prefabPath = p.path();
                break;
            }
        }
    }
    if (prefabPath.empty()) return false;

    kPrefab prefab;
    if (!prefab.loadFromFile(prefabPath.string())) return false;

    // Serialize the in-scene subtree, then convert it into a template by
    // rewriting every node's UUID. Existing template_uuid values are preserved
    // so old-template nodes keep their identity; new nodes added in-instance
    // get fresh template UUIDs assigned now.
    json instanceJson = instanceRoot->serialize();

    std::function<void(json &)> toTemplate = [&](json &node) {
        kString existingTpl = node.value("template_uuid", kString(""));
        kString newUuid     = existingTpl.empty() ? generateUuid() : existingTpl;
        node["uuid"] = newUuid;
        node.erase("prefab_ref");
        node.erase("template_uuid");
        if (node.contains("children") && node["children"].is_array())
            for (auto &c : node["children"])
                toTemplate(c);
    };
    toTemplate(instanceJson);

    prefab.setRootJson(instanceJson);
    if (!prefab.saveToFile(prefabPath.string())) return false;

    // Re-stamp the in-scene subtree with template_uuids that match the freshly
    // written template, so further "Apply" calls don't keep churning UUIDs.
    std::function<void(json &, kObject *)> stamp = [&](json &node, kObject *obj) {
        if (!obj) return;
        obj->setTemplateUuid(node.value("uuid", kString("")));
        if (!node.contains("children") || !node["children"].is_array()) return;
        auto kids = obj->getChildren();
        for (size_t i = 0; i < kids.size() && i < node["children"].size(); ++i)
            stamp(node["children"][i], kids[i]);
    };
    stamp(instanceJson, instanceRoot);

    return true;
}

void Manager::refreshAllPrefabInstances(const kString &prefabUuid)
{
    if (!world || prefabUuid.empty()) return;

    // Locate the .prefab file by UUID and load the (latest) template.
    fs::path prefabPath;
    for (const auto &p : fs::recursive_directory_iterator(projectPath / "Assets"))
    {
        if (!p.is_regular_file()) continue;
        if (p.path().extension() != ".prefab") continue;
        kPrefab tmp;
        if (tmp.loadFromFile(p.path().string()) && tmp.getUuid() == prefabUuid)
        {
            prefabPath = p.path();
            break;
        }
    }
    if (prefabPath.empty()) return;

    kPrefab prefab;
    if (!prefab.loadFromFile(prefabPath.string())) return;

    // ---------------------------------------------------------------------
    // Phase 1 — snapshot every live instance of this prefab.
    //
    // We capture enough state per instance to recreate it identically once
    // the old subtree has been destroyed: the original root UUID, world
    // transform, parent + sibling placement, name/active flags, the full
    // template_uuid → scene_uuid map for every node in the subtree (so
    // descendants keep their scene UUIDs across refresh), plus selection
    // info covering both the root and any descendant.
    // ---------------------------------------------------------------------
    struct InstanceState {
        kScene  *scene             = nullptr;
        kObject *root              = nullptr;   // valid only during snapshot
        kString  rootUuid;
        kObject *parent            = nullptr;
        kObject *nextSibling       = nullptr;
        kVec3    pos               = kVec3(0);
        kQuat    rot               = kQuat();
        kVec3    scl               = kVec3(1);
        kString  name;
        bool     active            = true;
        bool     wasInSelectedList = false;
        bool     wasSelectedObject = false;        // root was the active selection
        kString  selectedDescendantUuid;           // selectedObject was a descendant; empty otherwise

        // template_uuid → old scene_uuid for every node in the subtree, root
        // included. Used to re-stamp UUIDs on the rebuilt instance JSON so
        // existing references (selectedObjects, scripts, etc.) keep working.
        std::unordered_map<kString, kString> templateToScene;
    };
    std::vector<InstanceState> instances;

    std::function<void(kScene *, kObject *)> collect = [&](kScene *s, kObject *node) {
        if (!node->getPrefabRef().empty() && node->getPrefabRef() == prefabUuid)
        {
            InstanceState st;
            st.scene             = s;
            st.root              = node;
            st.rootUuid          = node->getUuid();
            st.parent            = node->getParent();
            st.nextSibling       = nextSiblingOf(node);
            st.pos               = node->getPosition();
            st.rot               = node->getRotation();
            st.scl               = node->getScale();
            st.name              = node->getName();
            st.active            = node->getActive();
            st.wasInSelectedList = std::find(selectedObjects.begin(),
                                              selectedObjects.end(),
                                              st.rootUuid) != selectedObjects.end();
            st.wasSelectedObject = (selectedObject == node);

            // Walk the whole subtree to (a) record template→scene mappings
            // and (b) detect if the active selection points at any descendant.
            std::function<void(kObject *)> walk = [&](kObject *n) {
                if (!n->getTemplateUuid().empty())
                    st.templateToScene[n->getTemplateUuid()] = n->getUuid();
                if (selectedObject == n && !st.wasSelectedObject)
                    st.selectedDescendantUuid = n->getUuid();
                for (kObject *c : n->getChildren()) walk(c);
            };
            walk(node);

            instances.push_back(st);
            return; // don't descend into the prefab's own subtree
        }
        for (kObject *c : node->getChildren()) collect(s, c);
    };

    for (kScene *s : world->getScenes())
        if (s && s->getRootNode())
            collect(s, s->getRootNode());

    if (instances.empty()) return;

    kAssetManager *am = getAssetManager();
    kScene *savedScene = scene;

    // Selection pointer may dangle once we delete subtrees; clear it now and
    // re-point at the new root or new descendant in Phase 3.
    bool anySelectedRefreshed = false;
    for (const auto &i : instances)
        if (i.wasSelectedObject || !i.selectedDescendantUuid.empty())
            anySelectedRefreshed = true;
    if (anySelectedRefreshed) selectedObject = nullptr;

    // ---------------------------------------------------------------------
    // Phase 2 — destroy each old subtree, build a fresh one from the
    // template, re-stamp the captured UUIDs, restore properties.
    // ---------------------------------------------------------------------
    for (auto &i : instances)
    {
        i.root->detachFromParent();
        deleteObjectRecursive(i.root);
        i.root = nullptr;

        // Fresh JSON: instantiateJson writes template_uuid + a fresh scene
        // uuid on every node. We then walk that JSON and, for each node
        // whose template_uuid is in our map, override its uuid with the
        // captured scene uuid. Nodes that weren't in the old instance
        // (template additions) keep their fresh uuids.
        json instanceJson = kPrefab::instantiateJson(prefab.getRootJson());

        std::function<void(json &)> applyUuidMap = [&](json &n) {
            if (n.contains("template_uuid") && n["template_uuid"].is_string())
            {
                kString tplUuid = n["template_uuid"].get<std::string>();
                auto it = i.templateToScene.find(tplUuid);
                if (it != i.templateToScene.end())
                    n["uuid"] = it->second;
            }
            if (n.contains("children") && n["children"].is_array())
                for (auto &c : n["children"]) applyUuidMap(c);
        };
        applyUuidMap(instanceJson);

        // Defensive root override — covers the rare case where the old root
        // had no template_uuid (e.g. it was hand-built rather than spawned
        // by instantiatePrefabInScene).
        instanceJson["uuid"] = i.rootUuid;

        scene = i.scene;
        kObject *newRoot = loadObjectFromJson(instanceJson, i.scene, world,
                                              am, projectPath, editorCamera, nullptr);
        scene = savedScene;
        if (!newRoot) continue;

        // Re-stamp linkage and captured properties on the root.
        newRoot->setPrefabRef(prefabUuid);
        if (!i.name.empty()) newRoot->setName(i.name);
        newRoot->setActive(i.active);
        newRoot->setPosition(i.pos);
        newRoot->setRotation(i.rot);
        newRoot->setScale(i.scl);

        // Restore parent + sibling placement.
        if (i.parent && i.parent != i.scene->getRootNode())
            insertBefore(i.parent, newRoot, i.nextSibling);
        else if (i.nextSibling)
            insertBefore(i.scene->getRootNode(), newRoot, i.nextSibling);

        // ---- Phase 3 — re-resolve selection pointer + list ----
        if (i.wasSelectedObject)
        {
            selectedObject = newRoot;
        }
        else if (!i.selectedDescendantUuid.empty())
        {
            // Descendant selection: walk the new subtree to find the node
            // that now carries the preserved scene UUID.
            std::function<kObject *(kObject *)> findUuid =
                [&](kObject *n) -> kObject * {
                if (!n) return nullptr;
                if (n->getUuid() == i.selectedDescendantUuid) return n;
                for (kObject *c : n->getChildren())
                    if (kObject *f = findUuid(c)) return f;
                return nullptr;
            };
            if (kObject *found = findUuid(newRoot))
                selectedObject = found;
        }

        // Defensive: the root UUID should still be in selectedObjects (we
        // never removed it), but re-add if it slipped out.
        if (i.wasInSelectedList &&
            std::find(selectedObjects.begin(), selectedObjects.end(),
                      i.rootUuid) == selectedObjects.end())
        {
            selectedObjects.push_back(i.rootUuid);
        }
    }

    if (panelHierarchy) panelHierarchy->refreshList();
    projectSaved = false;
    refreshWindowTitle();
}

void Manager::unpackPrefabInstance(kObject *instanceRoot)
{
    if (!instanceRoot) return;

    auto cmd = std::make_unique<UnpackPrefabCommand>();
    cmd->manager = this;

    std::function<void(kObject *)> walk = [&](kObject *node) {
        UnpackPrefabCommand::Entry e;
        e.objUuid      = node->getUuid();
        e.prefabRef    = node->getPrefabRef();
        e.templateUuid = node->getTemplateUuid();
        cmd->entries.push_back(e);

        node->setPrefabRef("");
        node->setTemplateUuid("");
        for (kObject *c : node->getChildren())
            walk(c);
    };
    walk(instanceRoot);

    if (panelHierarchy) panelHierarchy->refreshList();
    projectSaved = false;
    refreshWindowTitle();

    undoRedo.push(std::move(cmd));
}

bool Manager::applyMaterialToObject(kObject *obj, const fs::path &materialPath,
                                    const kString &materialUuid)
{
    if (!obj) return false;
    kAssetManager *am = getAssetManager();
    if (!am) return false;

    // Read the .mat JSON and build a runtime kMaterial. Mirrors
    // PanelInspector::rebuildMatViewMaterial — kept duplicated here to avoid
    // pulling the inspector into Manager's dependency surface.
    json matJson;
    try
    {
        std::ifstream f(materialPath);
        if (!f.is_open()) return false;
        matJson = json::parse(f);
    }
    catch (...) { return false; }

    kString shaderName = matJson.value("shader", "Phong");
    kShader *shader = nullptr;
    if      (shaderName == "Unlit") shader = am->loadGlslFromResource("SHADER_MESH_FLAT");
    else if (shaderName == "Phong") shader = am->loadGlslFromResource("SHADER_MESH_PHONG");
    else if (shaderName == "PBR")   shader = am->loadGlslFromResource("SHADER_MESH_PBR");
    if (!shader) return false;

    auto readVec3 = [&](const char *key, kVec3 def) -> kVec3 {
        if (matJson.contains(key) && matJson[key].is_array() && matJson[key].size() >= 3)
            return kVec3(matJson[key][0].get<float>(),
                         matJson[key][1].get<float>(),
                         matJson[key][2].get<float>());
        return def;
    };

    kMaterial *mat = am->createMaterial(shader);
    mat->setDiffuseColor (readVec3("diffuse",  kVec3(1.0f)));
    mat->setAmbientColor (readVec3("ambient",  kVec3(1.0f)));
    mat->setSpecularColor(readVec3("specular", kVec3(1.0f)));
    mat->setShininess(matJson.value("shininess", 32.0f));
    mat->setMetallic (matJson.value("metallic",  0.0f));
    mat->setRoughness(matJson.value("roughness", 0.5f));

    // Apply to the object AND its sub-meshes. Imported models keep their
    // geometry in import-derived child meshes, so applying only to the root
    // would have no visible effect. MaterialCommand snapshots the whole
    // subtree, so undo still restores per-part materials cleanly.
    obj->setMaterial(mat, /*setChildren*/ true);
    // Record the source asset UUID on the root so the assignment is persisted
    // on save and can be re-applied (and re-propagated) on load.
    obj->setMaterialUuid(materialUuid);
    projectSaved = false;
    refreshWindowTitle();
    return true;
}

void Manager::applyDefaultMaterialToObject(kObject *obj)
{
    if (!obj) return;
    kAssetManager *am = getAssetManager();
    if (!am) return;

    // Mirrors the default material assigned to freshly-created meshes.
    kShader   *shader = am->loadGlslFromResource("SHADER_MESH_PHONG");
    kMaterial *mat    = am->createMaterial(shader);
    mat->setAmbientColor(kVec3(1.0f, 1.0f, 1.0f));
    mat->setDiffuseColor(kVec3(0.5f, 0.5f, 0.5f));
    obj->setMaterial(mat, /*setChildren*/ true);
    obj->setMaterialUuid("");
    projectSaved = false;
    refreshWindowTitle();
}

// Recursively re-apply the stored material UUID for a node and its descendants.
static void reapplyMaterialsRecursive(Manager *mgr, kObject *node,
                                      const fs::path &projectPath)
{
    if (!node) return;
    const kString mu = node->getMaterialUuid();
    if (!mu.empty())
    {
        auto it = mgr->fileMap.find(mu);
        if (it != mgr->fileMap.end() && it->second.type == "material")
            mgr->applyMaterialToObject(node, projectPath / "Assets" / it->second.path, mu);
        // else: asset missing — keep the UUID, leave the default material.
    }
    for (kObject *child : node->getChildren())
        reapplyMaterialsRecursive(mgr, child, projectPath);
}

void Manager::reapplyStoredMaterials()
{
    if (!world) return;
    for (kScene *s : world->getScenes())
        if (s && s->getRootNode())
            for (kObject *child : s->getRootNode()->getChildren())
                reapplyMaterialsRecursive(this, child, projectPath);
}

static void captureMaterialSubtreeRec(kObject *node, std::vector<MaterialSnapshot> &out)
{
    if (!node) return;
    out.push_back({ node->getUuid(), node->getMaterial(), node->getMaterialUuid() });
    for (kObject *c : node->getChildren())
        captureMaterialSubtreeRec(c, out);
}

std::vector<MaterialSnapshot> Manager::captureMaterialSubtree(kObject *root)
{
    std::vector<MaterialSnapshot> snap;
    captureMaterialSubtreeRec(root, snap);
    return snap;
}

void Manager::restoreMaterialSubtree(const std::vector<MaterialSnapshot> &snap)
{
    for (const MaterialSnapshot &s : snap)
    {
        kObject *obj = findObjectByUuid(s.uuid);
        if (!obj) continue;
        obj->setMaterial(s.material, /*setChildren*/ false);
        obj->setMaterialUuid(s.materialUuid);
    }
    projectSaved = false;
    refreshWindowTitle();
}

// Free helper so the static loadObjectFromJson (which has no Manager) can use it.
static void assignImportChildUuidsRec(kObject *root)
{
    if (!root) return;
    for (kObject *c : root->getChildren())
    {
        // UUID-less descendants are the importer's sub-meshes: give them a
        // stable id and flag them so they stay out of serialization but become
        // selectable/duplicable in the hierarchy.
        if (c->getUuid().empty())
        {
            c->setUuid(generateUuid());
            c->setImportChild(true);
        }
        assignImportChildUuidsRec(c);
    }
}

void Manager::assignImportChildUuids(kObject *root)
{
    assignImportChildUuidsRec(root);
}

void Manager::deleteAssets(const std::vector<fs::path> &paths)
{
    for (const auto &p : paths)
    {
        if (!fs::exists(p))
            continue;

        std::error_code ec;
        if (fs::is_directory(p))
            fs::remove_all(p, ec);
        else
            fs::remove(p, ec);

        if (ec)
            std::cerr << "Failed to delete " << p << ": " << ec.message() << "\n";
    }

    checkAssetChange();
}
