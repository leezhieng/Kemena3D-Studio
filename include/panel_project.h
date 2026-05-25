#ifndef PANEL_PROJECT_H
#define PANEL_PROJECT_H

#include "kemena/kemena.h"

#include "manager.h"
#include "util.h"

#include <GL/glew.h>
#include <imgui.h>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <unordered_map>

using namespace kemena;
namespace fs = std::filesystem;

class Manager;

class PanelProject
{
	private:
		ImTextureRef iconUp;
		ImTextureRef iconAdd;
		ImTextureRef iconMag;

		ImTextureRef iconFolder;
		ImTextureRef iconText;
		ImTextureRef iconImage;
		ImTextureRef iconScript;
		ImTextureRef iconAudio;
		ImTextureRef iconVideo;
		ImTextureRef iconModel;
		ImTextureRef iconPrefab;
		ImTextureRef iconWorld;
		ImTextureRef iconMaterial;
		ImTextureRef iconOther;

		char searchBuffer[128] = "";

		ImVec4 upTint = ImVec4(1, 1, 1, 1);
		ImVec4 addTint = ImVec4(1, 1, 1, 1);

		ImTextureRef iconList;
		ImTextureRef iconThumbnail;

		bool displayThumbnail = true;

		struct Node
		{
			kString name;
			bool isSelected = false;
			std::vector<std::unique_ptr<Node>> children;

			kString uuid;
			ImTextureRef icon = nullptr;
			int type = 0; // 0 - Folder, 1 - File
			fs::path fullPath;

			Node(const kString& n, const kString& g, ImTextureRef i = nullptr, int t = 0) : name(n), uuid(g), icon(i), type(t) {}
		};

		Node rootTree;
		Node rootThumbnail;
		bool needRefreshList = false;

		// Rename modal state.
		bool     openRenamePopup = false;
		fs::path renameTargetPath;
		char     renameBuffer[256] = "";

		kAssetManager* assetManager = nullptr;
		std::unordered_map<kString, std::pair<GLuint, ImTextureRef>> thumbnailCache;

		ImTextureRef getThumbnailIcon(const kString& uuid, ImTextureRef defaultIcon);
		void clearThumbnailCache();
		void invalidateThumbnail(const kString& uuid);

	public:
	    PanelProject(kGuiManager* setGuiManager, Manager* setManager, kAssetManager* assetManager);

	    void triggerRefresh() { needRefreshList = true; }

	    // Called when a file is double-clicked; receives the full filesystem path
	    std::function<void(const std::string&)> onFileDoubleClicked;

	    kString pendingSelectUuid;

		struct SelectedProjectAsset
		{
			int          count    = 0;      // 0=none, 1=single, >1=multi
			kString      name;
			kString      uuid;
			kString      fileType;          // "mesh","image", etc. (empty for folders)
			bool         isFolder = false;
			ImTextureRef thumbnail = nullptr;
			fs::path     metaPath;
		};
		SelectedProjectAsset getProjectSelection() const;

		void deselectAll(Node& root);
		void clearSelection();

		void drawProjectPanel(Node& rootTree, Node& rootThumbnail, bool* opened);
		void draw(bool& opened);

		void refreshTreeList();
		void drawTreeNode(Node& node, Node& rootTree, int level = 0);
		void populateTree(Node& parent, const fs::path& fullPath);

		void refreshThumbnailList();
		void drawThumbnailNode(const Node& currentDir);
		void drawBreadcrumb();
		void executeDeleteSelected();

		// Begins a rename for the given asset, then renders the modal.
		void beginRename(const fs::path& path, const kString& name);
		void drawRenameModal();

		Manager* manager;
		kGuiManager* gui;
};

#endif

