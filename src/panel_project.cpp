#include "panel_project.h"

#include <cstring>

using namespace kemena;

PanelProject::PanelProject(kGuiManager* setGuiManager, Manager* setManager, kAssetManager* assetManager)
	: rootTree("Assets", "asset", nullptr, 0), rootThumbnail("Assets", "asset", nullptr, 0)
{
    gui = setGuiManager;
	manager = setManager;
	manager->panelProject = this;
	this->assetManager = assetManager;

	// Button icons
	kTexture2D* tex_up = assetManager->loadTexture2DFromResource("ICON_FOLDER_UP_BUTTON", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconUp = (ImTextureRef)(intptr_t)tex_up->getTextureID();

	kTexture2D* tex_add = assetManager->loadTexture2DFromResource("ICON_ADD_ROUND_BUTTON", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconAdd = (ImTextureRef)(intptr_t)tex_add->getTextureID();

	kTexture2D* tex_mag = assetManager->loadTexture2DFromResource("ICON_MAGNIFIER_LABEL", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconMag = (ImTextureRef)(intptr_t)tex_mag->getTextureID();

	// List and thumbnail icon
	kTexture2D* tex_thumb = assetManager->loadTexture2DFromResource("ICON_SCENE_LABEL", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconThumbnail = (ImTextureRef)(intptr_t)tex_thumb->getTextureID();

	kTexture2D* tex_list = assetManager->loadTexture2DFromResource("ICON_HIERARCHY_LABEL", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconList = (ImTextureRef)(intptr_t)tex_list->getTextureID();

	// File icons (built-in)
	kTexture2D* tex_folder = assetManager->loadTexture2DFromResource("ICON_FOLDER_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconFolder = (ImTextureRef)(intptr_t)tex_folder->getTextureID();

	kTexture2D* tex_text = assetManager->loadTexture2DFromResource("ICON_TEXT_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconText = (ImTextureRef)(intptr_t)tex_text->getTextureID();

	kTexture2D* tex_image = assetManager->loadTexture2DFromResource("ICON_IMAGE_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconImage = (ImTextureRef)(intptr_t)tex_image->getTextureID();

	kTexture2D* tex_script = assetManager->loadTexture2DFromResource("ICON_SCRIPT_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconScript = (ImTextureRef)(intptr_t)tex_script->getTextureID();

	kTexture2D* tex_audio = assetManager->loadTexture2DFromResource("ICON_AUDIO_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconAudio = (ImTextureRef)(intptr_t)tex_audio->getTextureID();

	kTexture2D* tex_video = assetManager->loadTexture2DFromResource("ICON_VIDEO_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconVideo = (ImTextureRef)(intptr_t)tex_video->getTextureID();

	kTexture2D* tex_model = assetManager->loadTexture2DFromResource("ICON_MODEL_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconModel = (ImTextureRef)(intptr_t)tex_model->getTextureID();

	kTexture2D* tex_prefab = assetManager->loadTexture2DFromResource("ICON_PREFAB_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconPrefab = (ImTextureRef)(intptr_t)tex_prefab->getTextureID();

	kTexture2D* tex_world = assetManager->loadTexture2DFromResource("ICON_WORLD_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconWorld = (ImTextureRef)(intptr_t)tex_world->getTextureID();

	kTexture2D* tex_material = assetManager->loadTexture2DFromResource("ICON_MATERIAL_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconMaterial = (ImTextureRef)(intptr_t)tex_material->getTextureID();

	kTexture2D* tex_other = assetManager->loadTexture2DFromResource("ICON_OTHER_FILE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconOther = (ImTextureRef)(intptr_t)tex_other->getTextureID();

	rootTree = Node("Asset", "asset", (ImTextureID)(intptr_t)tex_folder->getTextureID(), 0);
	rootThumbnail = Node("Asset", "asset", (ImTextureID)(intptr_t)tex_folder->getTextureID(), 0);
}

ImTextureRef PanelProject::getThumbnailIcon(const kString& uuid, ImTextureRef defaultIcon)
{
	if (uuid.empty()) return defaultIcon;

	auto it = thumbnailCache.find(uuid);
	if (it != thumbnailCache.end())
		return it->second.second;

	fs::path thumbPath = manager->projectPath / "Library" / "Thumbnails" / (uuid + ".png");
	if (!fs::exists(thumbPath)) return defaultIcon;

	try
	{
		kString thumbKey = uuid + "_thumb";
		kTexture2D* tex = assetManager->loadTexture2D(
			thumbPath.string(), thumbKey, kTextureFormat::TEX_FORMAT_RGBA, false);
		if (tex && tex->getTextureID() != 0)
		{
			GLuint glId = (GLuint)tex->getTextureID();
			ImTextureRef ref = (ImTextureRef)(intptr_t)glId;
			thumbnailCache[uuid] = { glId, ref };
			return ref;
		}
	}
	catch (...) {}

	return defaultIcon;
}

PanelProject::SelectedProjectAsset PanelProject::getProjectSelection() const
{
	SelectedProjectAsset result;

	// Collect all selected nodes from the active view (flat for thumbnail, recursive for tree)
	std::vector<const Node*> selected;

	std::function<void(const Node&)> collect = [&](const Node& n) {
		for (auto& child : n.children) {
			if (child->isSelected)
				selected.push_back(child.get());
			collect(*child);
		}
	};

	collect(displayThumbnail ? rootThumbnail : rootTree);

	result.count = (int)selected.size();
	if (result.count == 1)
	{
		const Node* n   = selected[0];
		result.name     = n->name;
		result.uuid     = n->uuid;
		result.isFolder = (n->type == 0);
		result.thumbnail = n->icon;

		if (!result.isFolder && !result.uuid.empty())
		{
			auto it = manager->fileMap.find(result.uuid);
			if (it != manager->fileMap.end())
				result.fileType = it->second.type;
			result.metaPath = manager->projectPath / "Library" / "Metadata" / (result.uuid + ".json");
		}
	}

	return result;
}

void PanelProject::deselectAll(Node& root)
{
	root.isSelected = false;
	for (auto& child : root.children)
		deselectAll(*child);
}

void PanelProject::clearSelection()
{
	deselectAll(rootTree);
	deselectAll(rootThumbnail);
}

void PanelProject::drawProjectPanel(Node& rootTree, Node& rootThumbnail, bool* opened)
{
	gui->beginDisabled(!manager->projectOpened);

	gui->windowStart("Project", opened);
	{
		gui->pushStyleColor(ImGuiCol_Button, kVec4(0, 0, 0, 0));
		gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0, 0, 0, 0));
		gui->pushStyleColor(ImGuiCol_ButtonActive, kVec4(0, 0, 0, 0));

		gui->pushStyleVar(ImGuiStyleVar_ItemSpacing, kVec2(2, 0));

		// Up button
		{
			if (ImGui::ImageButton("UpButton", iconUp, ImVec2(16, 16)))
			{
				manager->closeFolder();
				needRefreshList = true;
			}
			upTint = gui->isItemActive() ? ImVec4(1,1,1,0.5f) : ImVec4(1,1,1,1);

			if (gui->isItemHovered())
				gui->setItemTooltip("Go to parent directory");
		}

		gui->sameLine();

		// Add button
		{
			ImGui::ImageButton("AddButton", iconAdd, ImVec2(16, 16));
			addTint = gui->isItemActive() ? ImVec4(1, 1, 1, 0.5f) : ImVec4(1, 1, 1, 1);

			if (gui->isItemHovered())
				gui->setItemTooltip("Import assets to project");
		}

		gui->popStyleVar();
		gui->popStyleColor(3);

		gui->sameLine();

		gui->pushStyleVar(ImGuiStyleVar_ItemSpacing, kVec2(0, 0));

		// Search bar
		gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(4, (22 - gui->getFontSize()) * 0.5f));
		gui->pushItemWidth(-FLT_MIN);

		gui->popStyleVar();

		gui->groupStart();
		{
			float iconSize = gui->getFontSize() * 0.8f;
			kVec2 cursor = gui->getCursorScreenPos();
			float buttonWidth = 18.0f;
			float searchWidth = gui->getContentRegionAvail().x - buttonWidth - 15;

			ImGui::GetWindowDrawList()->AddImage(
				iconMag,
				ImVec2(cursor.x + 4, cursor.y + (gui->getFrameHeight() - iconSize) * 0.5f),
				ImVec2(cursor.x + 4 + iconSize, cursor.y + (gui->getFrameHeight() + iconSize) * 0.5f)
			);

			gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(iconSize + 8, 3));
			gui->setNextItemWidth(searchWidth);
			ImGui::InputTextWithHint("##SearchProject", "Search...", searchBuffer, IM_ARRAYSIZE(searchBuffer));
			gui->popStyleVar();

			gui->sameLine(0.0f, 8.0f);

			// List or Thumbnail button
			{
				if (displayThumbnail)
				{
					if (ImGui::ImageButton("ListButton", iconList, ImVec2(16, 16)))
						displayThumbnail = false;

					if (gui->isItemHovered())
						gui->setItemTooltip("Switch to list view");
				}
				else
				{
					if (ImGui::ImageButton("ThumbnailButton", iconThumbnail, ImVec2(16, 16)))
						displayThumbnail = true;

					if (gui->isItemHovered())
						gui->setItemTooltip("Switch to thumbnail view");
				}
			}
		}
		gui->groupEnd();

		gui->popItemWidth();
		gui->popStyleVar();

		gui->spacing();
		gui->spacing();

		float availableHeight = gui->getContentRegionAvail().y - 4;

		if (displayThumbnail)
		{
			gui->childStart("ProjectThumbnail", kVec2(0, availableHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
			{
				drawBreadcrumb();
				drawThumbnailNode(rootThumbnail);
			}
			gui->childEnd();
		}
		else
		{
			gui->childStart("ProjectTree", kVec2(0, availableHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
			{
				drawTreeNode(rootTree, rootTree);
			}
			gui->childEnd();
		}

		gui->spacing();

		// Delete key shortcut
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		    ImGui::IsKeyPressed(ImGuiKey_Delete) &&
		    getProjectSelection().count > 0)
		{
			executeDeleteSelected();
		}

		drawRenameModal();
	}
	gui->windowEnd();

	gui->endDisabled();
}

void PanelProject::draw(bool& opened)
{
	if (manager->projectOpened)
	{
		if (needRefreshList)
		{
			clearThumbnailCache();
			refreshTreeList();
			refreshThumbnailList();
			needRefreshList = false;

			if (!pendingSelectUuid.empty())
			{
				std::function<void(Node&)> sel = [&](Node& n) {
					for (auto& child : n.children) {
						if (child->uuid == pendingSelectUuid) child->isSelected = true;
						sel(*child);
					}
				};
				sel(displayThumbnail ? rootThumbnail : rootTree);

				// Keep pending until thumbnail is stable so subsequent
				// thumbnail-triggered refreshes don't erase the selection
				fs::path thumbPath = manager->projectPath / "Library" / "Thumbnails"
				                     / (pendingSelectUuid + ".png");
				if (fs::exists(thumbPath))
					pendingSelectUuid = "";
			}
		}

		drawProjectPanel(rootTree, rootThumbnail, &opened);
	}
}

void PanelProject::clearThumbnailCache()
{
	for (auto& [uuid, entry] : thumbnailCache)
	{
		if (entry.first) glDeleteTextures(1, &entry.first);
	}
	thumbnailCache.clear();
}

void PanelProject::invalidateThumbnail(const kString& uuid)
{
	auto it = thumbnailCache.find(uuid);
	if (it != thumbnailCache.end())
	{
		if (it->second.first) glDeleteTextures(1, &it->second.first);
		thumbnailCache.erase(it);
	}
}

void PanelProject::refreshTreeList()
{
	if (!manager->projectOpened)
		return;

	fs::path fullPath = manager->projectPath;
	fullPath /= "Assets"; // Tree view always show the full project structure

	if (!fs::exists(fullPath) || !fs::is_directory(fullPath))
	{
		std::cerr << "Path does not exist or is not a directory: " << fullPath << "\n";
		return;
	}

	// Root node setup
	kString folderName = fullPath.filename().string();
	rootTree.name = folderName;
	rootTree.uuid = "asset";
	rootTree.icon = iconFolder;
	rootTree.type = 0;
	rootTree.isSelected = false;
	rootTree.children.clear();

	// Fill recursively
	populateTree(rootTree, fullPath);
}

void PanelProject::drawTreeNode(Node& node, Node& rootTree, int level)
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (node.isSelected) flags |= ImGuiTreeNodeFlags_Selected;
	if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;

	// Only expand the first node by default
	if (&node == &rootTree)
	{
		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}

	ImGui::Image(node.icon, ImVec2(16, 16));
	gui->sameLine();

	bool nodeOpen = gui->treeStartEx(node.uuid, node.name, flags);

	if (gui->isItemHovered() && gui->isMouseDoubleClicked(ImGuiMouseButton_Left))
	{
		std::cout << "Double-clicked: " << node.uuid.c_str() << " ,Level:" << level << std::endl;
	}

	if (gui->isItemClicked())
	{
		if (!gui->isKeyShift())
			deselectAll(rootTree);
		node.isSelected = !node.isSelected || gui->isKeyShift();
		manager->selectedObjects.clear();
		manager->selectedObject = nullptr;
		manager->selectedScene  = nullptr;
		manager->worldSelected  = false;
	}

	// Drag source for file items in the tree view.
	if (node.type == 1 && !node.uuid.empty() &&
		ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		ImGui::SetDragDropPayload("PROJECT_ASSET",
			node.uuid.c_str(), node.uuid.size() + 1);
		ImGui::Image(node.icon, ImVec2(16, 16));
		ImGui::SameLine();
		ImGui::TextUnformatted(node.name.c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginPopupContextItem("##TreeCtx"))
	{
		if (!node.isSelected)
		{
			deselectAll(rootTree);
			node.isSelected = true;
		}
		if (ImGui::MenuItem("Rename"))
			beginRename(node.fullPath, node.name);
		if (ImGui::MenuItem("Delete"))
			executeDeleteSelected();
		ImGui::EndPopup();
	}

	if (nodeOpen)
	{
		level++;

		for (auto& child : node.children)
			drawTreeNode(*child, rootTree, level);

		gui->treePop();
	}
}

void PanelProject::populateTree(Node& parent, const fs::path& fullPath)
{
	if (fs::is_empty(fullPath))
		return;

    fs::path assetsPath = manager->projectPath / "Assets";

	// --- First: Directories ---
	for (const auto& entry : fs::directory_iterator(fullPath))
	{
		if (entry.is_directory())
		{
			auto child = std::make_unique<Node>(
							 entry.path().filename().string(),
							 "Folder_" + entry.path().filename().string(),
							 iconFolder,
							 0
						 );
			child->fullPath = entry.path();

			// Recursively fill subfolder
			populateTree(*child, entry.path());

			parent.children.emplace_back(std::move(child));
		}
	}

	// --- Second: Files ---
	for (const auto& entry : fs::directory_iterator(fullPath))
	{
		if (entry.is_regular_file())
		{
			auto ext = entry.path().extension().string();
			ImTextureRef icon;

			if (ext == ".txt" || ext == ".ini" || ext == ".xml" || ext == ".json")
				icon = iconText;
			else if (ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".png" || ext == ".gif" || ext == ".tiff" || ext == ".tga")
				icon = iconImage;
			else if (ext == ".as")
				icon = iconScript;
			else if (ext == ".mp3" || ext == ".wav" || ext == ".ogg")
				icon = iconAudio;
			else if (ext == ".mp4" || ext == ".mov" || ext == ".avi" || ext == ".webm")
				icon = iconVideo;
			else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae" || ext == ".stl")
				icon = iconModel;
			else if (ext == ".prefab" || ext == ".pfb")
				icon = iconPrefab;
			else if (ext == ".particle")
				icon = iconOther; // No dedicated particle icon yet — fallback.
			else if (ext == ".world")
				icon = iconWorld;
			else if (ext == ".mat")
				icon = iconMaterial;
			else
				icon = iconOther;

            kString relativePath = fs::relative(entry.path(), assetsPath).generic_string();

            kString uuid = manager->uuidMap[relativePath];
            //std::cout << relativePath << " -> " << uuid << std::endl;

			icon = getThumbnailIcon(uuid, icon);

			auto fileNode = std::make_unique<Node>(
											 entry.path().filename().string(),
											 uuid,
											 icon,
											 1
										 );
			fileNode->fullPath = entry.path();
			parent.children.emplace_back(std::move(fileNode));
		}
	}
}

void PanelProject::refreshThumbnailList()
{
	if (manager->projectOpened)
	{
		fs::path fullPath = manager->projectPath;
		fs::path assetsPath = manager->projectPath / "Assets";

		if (!manager->currentDir.empty())
		{
			for (const auto& dir : manager->currentDir)
            {
				fullPath /= dir;  // appends with correct separator for the platform
            }
		}

		// Navigate up if the current directory no longer exists
		while ((!fs::exists(fullPath) || !fs::is_directory(fullPath)) && manager->currentDir.size() > 1)
		{
			manager->currentDir.pop_back();
			fullPath = manager->projectPath;
			for (const auto& dir : manager->currentDir)
				fullPath /= dir;
		}

		if (!fs::exists(fullPath) || !fs::is_directory(fullPath))
		{
			std::cerr << "Path does not exist or is not a directory: " << fullPath << "\n";
			return;
		}

		if (fs::is_directory(fullPath))
		{
			kString folderName = fullPath.filename().string();
			//std::cout << "Last folder: " << folderName << "\n";
			//std::cout << "Last folder: " << fullPath << "\n";

			// Check whether uuid already exist for this folder
			// Generate a uuid if haven't, then save to the list

			// Reset root safely
			rootThumbnail.name = folderName;
			rootThumbnail.uuid = "Assets";
			rootThumbnail.icon = iconFolder;
			rootThumbnail.type = 0;
			rootThumbnail.isSelected = false;
			rootThumbnail.children.clear();
		}

		// Loop through all the folders and files in fullPath
		if (!fs::exists(fullPath))
		{
			std::cerr << "Path does not exist: " << fullPath << "\n";
			return;
		}

		// Do directory first
		{
			if (!fs::is_empty(fullPath))
			{
				// Do for folders
				{
					for (const auto& entry : fs::directory_iterator(fullPath))
					{
						if (entry.is_directory())
						{
							auto folderNode = std::make_unique<Node>(
																	entry.path().filename().string(),
																	"Folder_" + entry.path().filename().string(),
																	iconFolder,
																	0
																);
							folderNode->fullPath = entry.path();
							rootThumbnail.children.emplace_back(std::move(folderNode));
						}
					}
				}

				// Do for files
				{
					for (const auto& entry : fs::directory_iterator(fullPath))
					{
						if (entry.is_regular_file())
						{
							auto ext = entry.path().extension().string();
							ImTextureRef icon;

							if (ext == ".txt" || ext == ".ini" || ext == ".xml" || ext == ".json")
								icon = iconText;
							else if (ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".png" || ext == ".gif" || ext == ".tiff" || ext == ".tga")
								icon = iconImage;
							else if (ext == ".as")
								icon = iconScript;
							else if (ext == ".mp3" || ext == ".wav" || ext == ".ogg")
								icon = iconAudio;
							else if (ext == ".mp4" || ext == ".mov" || ext == ".avi" || ext == ".webm")
								icon = iconVideo;
							else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae" || ext == ".stl")
								icon = iconModel;
							else if (ext == ".pfb")
								icon = iconPrefab;
							else if (ext == ".world")
								icon = iconWorld;
							else if (ext == ".mat")
								icon = iconMaterial;
							else
								icon = iconOther;

                            kString relativePath = fs::relative(entry.path(), assetsPath).generic_string();
                            kString uuid = manager->uuidMap[relativePath];
                            //std::cout << relativePath << " -> " << uuid << std::endl;

							icon = getThumbnailIcon(uuid, icon);

							auto thumbNode = std::make_unique<Node>(
																	entry.path().filename().string(),
																	uuid,
																	icon,
																	1
																);
							thumbNode->fullPath = entry.path();
							rootThumbnail.children.emplace_back(std::move(thumbNode));
						}
						else
						{
							// Don't display anything if it's not a folder or file (could be symlink, drive, etc.)
						}
					}
				}
			}
		}
	}
}

void PanelProject::drawThumbnailNode(const Node& currentDir)
{
	if (manager->projectOpened)
	{
		float thumbSize   = 48.0f;   // icon size
		float cellPadding = 12.0f;   // spacing between columns
		float cellWidth   = thumbSize + cellPadding;

		float panelWidth  = gui->getContentRegionAvail().x;
		int columns = (int)(panelWidth / cellWidth);
		if (columns < 1) columns = 1;

		if (currentDir.children.size() > 0)
		{
			gui->columnsStart(columns, "", false);

			for (auto& child : currentDir.children)
			{
				gui->groupStart();
				gui->pushId(child.get());

				float columnWidth = gui->getColumnWidth();
				float topMargin   = 4.0f;
				float cellHeight  = topMargin + thumbSize + gui->getTextLineHeight() + 4.0f;

				// Full-cell selectable
				bool selected = child->isSelected;
				if (gui->selectable("##thumb", selected, 0, kVec2(columnWidth, cellHeight)))
				{
					if (!gui->isKeyShift())
						deselectAll(rootThumbnail);
					child->isSelected = !child->isSelected || gui->isKeyShift();
					manager->selectedObjects.clear();
					manager->selectedObject = nullptr;
					manager->selectedScene  = nullptr;
					manager->worldSelected  = false;
					pendingSelectUuid = "";
				}

				// Drag source — file items only (folders aren't draggable assets).
				// Payload is the asset UUID; the receiver looks up type via Manager::fileMap.
				if (child->type == 1 && !child->uuid.empty() &&
					ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
				{
					ImGui::SetDragDropPayload("PROJECT_ASSET",
						child->uuid.c_str(), child->uuid.size() + 1);
					ImGui::Image(child->icon, ImVec2(32, 32));
					ImGui::SameLine();
					ImGui::TextUnformatted(child->name.c_str());
					ImGui::EndDragDropSource();
				}

				// Handle double-click
				if (gui->isItemHovered() && gui->isMouseDoubleClicked(ImGuiMouseButton_Left))
				{
				    if (child->type == 0)
                    {
                        manager->openFolder(child->name);
                        needRefreshList = true;
                        gui->columnsEnd();
                        gui->popId();
                        gui->groupEnd();
                        return;
                    }
                    else
                    {
                        if (onFileDoubleClicked)
                            onFileDoubleClicked(child->fullPath.string());
                    }
				}

				// Per-item right-click context menu
				if (ImGui::BeginPopupContextItem("##ItemCtx"))
				{
					if (!child->isSelected)
					{
						deselectAll(rootThumbnail);
						child->isSelected = true;
					}
					if (ImGui::MenuItem("Rename"))
						beginRename(child->fullPath, child->name);
					if (ImGui::MenuItem("Delete"))
						executeDeleteSelected();
					ImGui::EndPopup();
				}

				kString displayName = fitTextWithEllipsisUtf8(gui, child->name, columnWidth - 4.0f);

				if (displayName != child->name && gui->isItemHovered())
				{
					gui->beginTooltip();
					gui->textUnformatted(child->name);
					gui->endTooltip();
				}

				kVec2 cellPos  = gui->getItemRectMin();
				float iconX = cellPos.x + (columnWidth - thumbSize) * 0.5f;
				float iconY = cellPos.y + topMargin;
				gui->setCursorScreenPos(kVec2(iconX, iconY));
				ImGui::Image(child->icon, ImVec2(thumbSize, thumbSize));

				float textWidth = gui->calcTextSize(displayName).x;
				float textX = cellPos.x + (columnWidth - textWidth) * 0.5f;
				float textY = iconY + thumbSize + 2.0f;
				gui->setCursorScreenPos(kVec2(textX, textY));
				gui->textUnformatted(displayName);

				gui->popId();
				gui->groupEnd();
				gui->nextColumn();
			}

			gui->columnsEnd();
		}
		else
		{
			kString text = "Empty folder";
			float textWidth = gui->calcTextSize(text).x;
			float columnWidth = gui->getColumnWidth();
			float textX = gui->getCursorPosX() + (columnWidth - textWidth) * 0.5f;
			gui->setCursorPosX(textX);
			gui->text(text);
		}

		// Right-click context menu on empty space
		if (ImGui::BeginPopupContextWindow("##ProjectContextMenu",
		        ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::BeginMenu("Create"))
			{
				if (ImGui::MenuItem("Folder"))
					{ manager->createNewFolder();     needRefreshList = true; }
				if (ImGui::MenuItem("Shader"))
					{ manager->createNewShader();     needRefreshList = true; }
				if (ImGui::MenuItem("Material"))
					{ manager->createNewMaterial();   needRefreshList = true; }
				if (ImGui::MenuItem("Script"))
					{ manager->createNewScript();     needRefreshList = true; }
				if (ImGui::MenuItem("Logic Graph"))
					{ manager->createNewLogicGraph(); needRefreshList = true; }
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}
	}
	else
	{
		kString text = "No project found";
		float textWidth = gui->calcTextSize(text).x;
		float columnWidth = gui->getColumnWidth();
		float textX = gui->getCursorPosX() + (columnWidth - textWidth) * 0.5f;
		gui->setCursorPosX(textX);
		gui->text(text);
	}
}

void PanelProject::executeDeleteSelected()
{
	auto sel = getProjectSelection();
	if (sel.count == 0) return;

	std::string msg = (sel.count == 1)
		? "Delete \"" + sel.name + "\"?\nThis action cannot be undone."
		: "Delete " + std::to_string(sel.count) + " selected asset(s)?\nThis action cannot be undone.";

	auto result = pfd::message("Confirm Delete", msg, pfd::choice::yes_no, pfd::icon::warning).result();
	if (result != pfd::button::yes) return;

	std::vector<fs::path> paths;

	std::function<void(const Node&)> collect = [&](const Node& n) {
		for (auto& child : n.children) {
			if (child->isSelected) {
				if (!child->fullPath.empty())
					paths.push_back(child->fullPath);
			} else {
				collect(*child);
			}
		}
	};

	collect(displayThumbnail ? rootThumbnail : rootTree);

	if (!paths.empty())
	{
		manager->deleteAssets(paths);
		clearSelection();
		needRefreshList = true;
	}
}

void PanelProject::beginRename(const fs::path& path, const kString& name)
{
	renameTargetPath = path;
	std::strncpy(renameBuffer, name.c_str(), sizeof(renameBuffer) - 1);
	renameBuffer[sizeof(renameBuffer) - 1] = '\0';
	openRenamePopup = true;
}

void PanelProject::drawRenameModal()
{
	if (openRenamePopup)
	{
		ImGui::OpenPopup("Rename Asset");
		openRenamePopup = false;
	}

	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("New name:");

		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(320.0f);
		bool commit = ImGui::InputText("##renameinput", renameBuffer, sizeof(renameBuffer),
		                               ImGuiInputTextFlags_EnterReturnsTrue);

		ImGui::Spacing();
		bool doRename = ImGui::Button("Rename", ImVec2(120, 0)) || commit;
		ImGui::SameLine();
		bool doCancel = ImGui::Button("Cancel", ImVec2(120, 0));

		if (doRename)
		{
			kString newName = renameBuffer;
			if (!newName.empty() && !renameTargetPath.empty() &&
			    manager->renameAsset(renameTargetPath, newName))
			{
				clearSelection();
				needRefreshList = true;
			}
			ImGui::CloseCurrentPopup();
		}
		else if (doCancel)
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

void PanelProject::drawBreadcrumb()
{
	if (manager->currentDir.size() > 0)
	{
		// Start from project root
		fs::path basePath = manager->projectPath;

		// Make the breadcrumb button just submitted accept dragged file assets
		// and move them into `targetDir` on drop. The file's UUID lives in its
		// JSON content, so moving the file preserves it.
		auto acceptDropInto = [&](const fs::path& targetDir)
		{
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload =
				        ImGui::AcceptDragDropPayload("PROJECT_ASSET"))
				{
					kString uuid(static_cast<const char*>(payload->Data));
					fs::path src = manager->findAssetPathByUuid(uuid);
					if (!src.empty() && manager->moveAsset(src, targetDir))
					{
						clearSelection();
						needRefreshList = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
		};

		// "Assets" — root of the project's asset tree.
		if (gui->button("Assets"))
		{
			manager->currentDir.erase(manager->currentDir.begin() + 1, manager->currentDir.end());
			needRefreshList = true;
		}
		acceptDropInto(basePath / manager->currentDir[0]);

		fs::path segPath = basePath / manager->currentDir[0];
		for (size_t i = 1; i < manager->currentDir.size(); ++i)
		{
			gui->sameLine();
			gui->setCursorPosX(gui->getCursorPosX() - 3.0f);
			gui->text(">");
			gui->sameLine();
			gui->setCursorPosX(gui->getCursorPosX() - 4.0f);

			segPath /= manager->currentDir[i];

			if (gui->button(manager->currentDir[i]))
			{
				manager->currentDir.erase(manager->currentDir.begin() + i + 1, manager->currentDir.end());
				needRefreshList = true;
			}
			acceptDropInto(segPath);
		}

		gui->dummy(kVec2(0.0f, 4.0f));
	}
}

