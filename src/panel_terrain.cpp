/**
 * @file panel_terrain.cpp
 * @brief Editor panel for managing grid-based terrain tiles.
 */

#include "panel_terrain.h"
#include "util.h"

#include <imgui.h>
#include <set>

PanelTerrain::PanelTerrain(kGuiManager *setGuiManager, Manager *setManager)
    : gui(setGuiManager), manager(setManager)
{
}

void PanelTerrain::sculptAtWorldPos(const kVec3 &worldPos)
{
    if (!manager || !manager->terrainManager)
        return;

    // Search for the tile containing the world position
    kTerrain *foundTile = nullptr;
    for (auto &pair : manager->terrainManager->getTiles())
    {
        kTerrain *tile = pair.second.get();
        if (!tile || !tile->isLoaded())
            continue;

        int hx, hz;
        if (tile->worldToHeightmap(worldPos, hx, hz))
        {
            foundTile = tile;
            break;
        }
    }

    if (!foundTile)
        return;

    // On first stroke in Flatten mode, sample the target height
    if (sculpt.mode == kTerrain::BrushMode::Flatten && sculpt.affectedTile != foundTile)
    {
        sculpt.flattenHeight = foundTile->sampleHeight(worldPos);
    }

    foundTile->applyBrush(worldPos, sculpt.radius, sculpt.strength,
                          sculpt.mode, sculpt.flattenHeight);

    // Fast polygon update for live visual feedback (updates vertex Y and normals on GPU)
    foundTile->updateMesh();

    sculpt.affectedTile = foundTile;
    sculpt.needsRebuild = true;
}

void PanelTerrain::rebuildSculptedTile()
{
    if (!sculpt.needsRebuild || !sculpt.affectedTile)
        return;

    // Use fast update instead of full rebuild to preserve the material.
    // updateMesh() updates vertex positions, normals, GPU buffers, and AABB
    // without destroying/recreating the mesh (which would delete the material).
    sculpt.affectedTile->updateMesh();
    if (manager && manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();
    sculpt.needsRebuild = false;
}

void PanelTerrain::paintAtWorldPos(const kVec3 &worldPos, bool lower)
{
    if (!manager || !manager->terrainManager)
        return;

    // Find the terrain tile under worldPos
    kTerrain *foundTile = nullptr;
    for (auto &pair : manager->terrainManager->getTiles())
    {
        kTerrain *tile = pair.second.get();
        if (!tile || !tile->isLoaded())
            continue;

        int hx, hz;
        if (tile->worldToHeightmap(worldPos, hx, hz))
        {
            foundTile = tile;
            break;
        }
    }

    if (!foundTile)
        return;

    // Apply height adjustment: lower = decrease (dark/down), raise = increase (light/up)
    const float radius = sculpt.radius; // reuse sculpt radius for paint size
    const float effectiveStrength = paint.strength * (lower ? -1.0f : 1.0f);

    int cx, cz;
    if (!foundTile->worldToHeightmap(worldPos, cx, cz))
        return;

    float sampleSpacing = foundTile->getWorldSize() / static_cast<float>(foundTile->getHeightRes() - 1);
    int radSamples = static_cast<int>(std::ceil(radius / sampleSpacing));
    int res = foundTile->getHeightRes();
    int xMin = (std::max)(0, cx - radSamples);
    int xMax = (std::min)(res - 1, cx + radSamples);
    int zMin = (std::max)(0, cz - radSamples);
    int zMax = (std::min)(res - 1, cz + radSamples);

    float worldRadiusSq = radius * radius;
    float *data = foundTile->getHeightData();

    for (int z = zMin; z <= zMax; ++z)
    {
        for (int x = xMin; x <= xMax; ++x)
        {
            kVec3 sampleWorld = foundTile->heightmapToWorld(x, z);
            float dx = sampleWorld.x - worldPos.x;
            float dz = sampleWorld.z - worldPos.z;
            float distSq = dx * dx + dz * dz;

            if (distSq > worldRadiusSq)
                continue;

            float dist = std::sqrt(distSq);
            float t = dist / radius;
            // Cosine falloff, blended with fade control
            float core = std::cos(t * 3.14159265358979323846f * 0.5f);
            float falloff = core * (1.0f - paint.fade) + paint.fade * (1.0f - t);
            falloff = (std::max)(0.0f, falloff);
            falloff *= effectiveStrength;

            int idx = z * res + x;
            float oldVal = data[idx];
            data[idx] = oldVal + falloff;
        }
    }

    // Fast polygon update for live visual feedback
    foundTile->updateMesh();

    paint.affectedTile = foundTile;
    paint.needsRebuild = true;
}

void PanelTerrain::rebuildPaintedTile()
{
    if (!paint.needsRebuild || !paint.affectedTile)
        return;

    // Use updateMesh to preserve material (same reason as sculpting)
    paint.affectedTile->updateMesh();
    if (manager && manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();
    paint.needsRebuild = false;
}

void PanelTerrain::paintSplatAtWorldPos(const kVec3 &worldPos, bool erase)
{
    if (!manager || !manager->terrainManager || paint.channel < 0 || paint.channel > 3)
        return;

    // Find the terrain tile under worldPos
    kTerrain *foundTile = nullptr;
    for (auto &pair : manager->terrainManager->getTiles())
    {
        kTerrain *tile = pair.second.get();
        if (!tile || !tile->isLoaded())
            continue;

        int hx, hz;
        if (tile->worldToHeightmap(worldPos, hx, hz))
        {
            foundTile = tile;
            break;
        }
    }

    if (!foundTile)
        return;

    const float radius = sculpt.radius;
    unsigned char *splat = foundTile->getSplatData();
    int res = foundTile->getHeightRes();

    int cx, cz;
    if (!foundTile->worldToHeightmap(worldPos, cx, cz))
        return;

    float sampleSpacing = foundTile->getWorldSize() / static_cast<float>(res - 1);
    int radSamples = static_cast<int>(std::ceil(radius / sampleSpacing));
    int xMin = (std::max)(0, cx - radSamples);
    int xMax = (std::min)(res - 1, cx + radSamples);
    int zMin = (std::max)(0, cz - radSamples);
    int zMax = (std::min)(res - 1, cz + radSamples);

    float worldRadiusSq = radius * radius;
    int channel = paint.channel;

    for (int z = zMin; z <= zMax; ++z)
    {
        for (int x = xMin; x <= xMax; ++x)
        {
            kVec3 sampleWorld = foundTile->heightmapToWorld(x, z);
            float dx = sampleWorld.x - worldPos.x;
            float dz = sampleWorld.z - worldPos.z;
            float distSq = dx * dx + dz * dz;

            if (distSq > worldRadiusSq)
                continue;

            float dist = std::sqrt(distSq);
            float t = dist / radius;
            float falloff = std::cos(t * 3.14159265358979323846f * 0.5f) * paint.strength;

            int idx = (z * res + x) * 4;

            if (erase)
            {
                // Reduce the selected channel, redistribute to others
                float oldVal = splat[idx + channel] / 255.0f;
                float reduction = oldVal * falloff;
                int newVal = (std::max)(0, static_cast<int>(roundf((oldVal - reduction) * 255.0f)));
                splat[idx + channel] = static_cast<unsigned char>(newVal);

                // Redistribute removed weight evenly to other channels
                float addEach = reduction / 3.0f;
                for (int c = 0; c < 4; ++c)
                {
                    if (c == channel)
                        continue;
                    float ov = splat[idx + c] / 255.0f;
                    float nv = ov + addEach;
                    splat[idx + c] = static_cast<unsigned char>((std::min)(255, static_cast<int>(roundf(nv * 255.0f))));
                }
            }
            else
            {
                // Paint the selected channel, reduce others proportionally
                float current = splat[idx + channel] / 255.0f;
                float remaining = 1.0f - current;
                float add = remaining * falloff;
                float newVal = current + add;
                splat[idx + channel] = static_cast<unsigned char>((std::min)(255, static_cast<int>(roundf(newVal * 255.0f))));

                // Reduce other channels proportionally to keep sum near 1.0
                float totalOther = 0.0f;
                float others[3];
                int oi = 0;
                for (int c = 0; c < 4; ++c)
                {
                    if (c == channel)
                        continue;
                    others[oi] = splat[idx + c] / 255.0f;
                    totalOther += others[oi];
                    ++oi;
                }
                if (totalOther > 0.001f)
                {
                    float scale = (1.0f - newVal) / totalOther;
                    oi = 0;
                    for (int c = 0; c < 4; ++c)
                    {
                        if (c == channel)
                            continue;
                        int v = static_cast<int>(roundf(others[oi] * scale * 255.0f));
                        splat[idx + c] = static_cast<unsigned char>((std::max)(0, (std::min)(255, v)));
                        ++oi;
                    }
                }
            }
        }
    }

    // Update GPU splat texture for live visual feedback
    foundTile->updateSplatTexture();

    paint.affectedTile = foundTile;
    paint.needsRebuild = true;
}

void PanelTerrain::rebuildPaintedSplatTile()
{
    if (!paint.needsRebuild || !paint.affectedTile)
        return;

    // No geometry changed — just ensure splat texture is up to date
    paint.affectedTile->updateSplatTexture();
    if (manager && manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();
    paint.needsRebuild = false;
}

// --- Noise helpers ----------------------------------------------------------

float PanelTerrain::noiseValue(int x, int y, int seed)
{
    unsigned int h = static_cast<unsigned int>(seed);
    h = ((h + static_cast<unsigned int>(x) * 374761393u) ^
         (static_cast<unsigned int>(y) * 668265263u)) *
        3284157443u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return static_cast<float>(h) / 4294967295.0f * 2.0f - 1.0f;
}

float PanelTerrain::noiseSmooth(float x, float y, int seed)
{
    int ix = static_cast<int>(std::floor(x));
    int iy = static_cast<int>(std::floor(y));
    float fx = x - std::floor(x);
    float fy = y - std::floor(y);

    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sy = fy * fy * (3.0f - 2.0f * fy);

    float n00 = noiseValue(ix + 0, iy + 0, seed);
    float n10 = noiseValue(ix + 1, iy + 0, seed);
    float n01 = noiseValue(ix + 0, iy + 1, seed);
    float n11 = noiseValue(ix + 1, iy + 1, seed);

    float nx0 = n00 + (n10 - n00) * sx;
    float nx1 = n01 + (n11 - n01) * sx;
    return nx0 + (nx1 - nx0) * sy;
}

void PanelTerrain::applyNoiseToTile(kTerrain *tile, float scale, float amplitude,
                                    int octaves, int seed, bool additive)
{
    if (!tile)
        return;

    int res = tile->getHeightRes();
    float *data = tile->getHeightData();

    for (int z = 0; z < res; ++z)
    {
        for (int x = 0; x < res; ++x)
        {
            float nx = static_cast<float>(x) / scale;
            float nz = static_cast<float>(z) / scale;

            float height = 0.0f;
            float freq = 1.0f;
            float amp = 1.0f;
            float maxAmp = 0.0f;

            for (int oct = 0; oct < octaves; ++oct)
            {
                height += noiseSmooth(nx * freq, nz * freq, seed + oct * 31337) * amp;
                maxAmp += amp;
                freq *= 2.0f;
                amp *= 0.5f;
            }

            height /= maxAmp;
            height *= amplitude;

            int idx = z * res + x;
            if (additive)
                data[idx] += height;
            else
                data[idx] = height;
        }
    }

    tile->rebuildMesh(true);
}

void PanelTerrain::draw(bool &isOpened)
{
    if (!isOpened || !enabled)
        return;

    ImGui::SetNextWindowSize(ImVec2(420, 650), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Terrain", &isOpened))
    {
        ImGui::End();
        return;
    }

    kWorld *world = manager->getWorld();
    kScene *scene = manager->getScene();
    kTerrainManager *terrainMgr = manager->terrainManager;

    if (!world || !scene || !terrainMgr)
    {
        ImGui::TextDisabled("No scene loaded.");
        ImGui::End();
        return;
    }

    // --- Settings ---
    ImGui::SeparatorText("Terrain Settings");
    ImGui::InputFloat("Tile Size", &m_newTileSize, 1.0f, 10.0f, "%.0f");
    ImGui::InputInt("Height Resolution", &m_newHeightRes);
    ImGui::InputFloat("Default Height", &m_defaultHeight);
    ImGui::InputInt("Load Radius", &m_loadRadius, 1, 100);
    ImGui::InputInt("Unload Radius", &m_unloadRadius, 1, 100);
    ImGui::Checkbox("Auto-Update Tiles", &m_autoUpdate);

    // Ensure reasonable values
    m_newTileSize = (std::max)(m_newTileSize, 1.0f);
    m_newHeightRes = (std::max)(m_newHeightRes, 2);
    m_loadRadius = (std::max)(m_loadRadius, 1);
    m_unloadRadius = (std::max)(m_unloadRadius, m_loadRadius + 1);

    ImGui::Spacing();

    // --- Sculpting Tools ---
    ImGui::SeparatorText("Sculpting");
    ImGui::Checkbox("Sculpt Mode Active", &sculpt.active);
    if (sculpt.active)
    {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Hold left mouse in viewport to sculpt");
    }

    // Brush mode radio buttons
    const char *modes[] = {"Raise", "Lower", "Flatten", "Smooth"};
    int currentMode = static_cast<int>(sculpt.mode);
    if (ImGui::Combo("Brush Mode", &currentMode, modes, 4))
    {
        sculpt.mode = static_cast<kTerrain::BrushMode>(currentMode);
        sculpt.affectedTile = nullptr; // Reset flatten sample on mode change
    }

    ImGui::SliderFloat("Brush Radius", &sculpt.radius, 0.5f, 50.0f, "%.1f");
    ImGui::SliderFloat("Brush Strength", &sculpt.strength, 0.01f, 1.0f, "%.2f");

    if (sculpt.mode == kTerrain::BrushMode::Flatten)
    {
        ImGui::Text("Flatten target: %.2f (sampled on first click)", sculpt.flattenHeight);
    }

    ImGui::Spacing();

    // --- Splat / Height Painting ---
    ImGui::SeparatorText("Painting");
    ImGui::Text("Splat Mask (R/G/B/A):");
    static bool splatSelected[4] = {false, false, false, false};
    ImU32 splatColors[4] = {
        IM_COL32(255, 60, 60, 255),
        IM_COL32(60, 255, 60, 255),
        IM_COL32(60, 60, 255, 255),
        IM_COL32(200, 200, 200, 255),
    };
    const char *splatLabels[4] = {"R", "G", "B", "A"};

    for (int i = 0; i < 4; ++i)
    {
        if (i > 0)
            ImGui::SameLine();
        if (ImGui::ColorButton(splatLabels[i], ImVec4(((splatColors[i] >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f, ((splatColors[i] >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f, ((splatColors[i] >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f, ((splatColors[i] >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f),
                               ((paint.channel == i) ? ImGuiColorEditFlags_NoTooltip : 0) | ImGuiColorEditFlags_NoBorder,
                               ImVec2(48, 24)))
        {
            // Turn off sculpt when entering paint mode
            sculpt.active = false;
            paint.heightActive = false;
            paint.active = true;
            paint.channel = i;
        }
    }

    ImGui::SameLine();
    ImGui::Checkbox("Paint Height", &paint.heightActive);
    if (paint.heightActive)
    {
        sculpt.active = false;
        paint.active = false;
        paint.channel = -1;
    }

    if (paint.active)
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                           "L-click paint, R-click erase mask channel. Ctrl+click=sculpt");
    if (paint.heightActive)
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                           "L-click lower, R-click raise height");

    ImGui::Spacing();

    // --- Noise Generation ---
    ImGui::SeparatorText("Noise Generation");
    ImGui::SliderFloat("Scale##noise", &m_noiseScale, 1.0f, 200.0f, "%.0f");
    ImGui::SliderFloat("Amplitude##noise", &m_noiseAmplitude, 0.1f, 100.0f, "%.1f");
    ImGui::SliderInt("Octaves##noise", &m_noiseOctaves, 1, 8);
    ImGui::InputInt("Seed##noise", &m_noiseSeed);
    ImGui::Checkbox("Additive##noise", &m_noiseAdditive);

    // Noise target tile
    static int noiseGridX = 0;
    static int noiseGridZ = 0;
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Tile X##noise", &noiseGridX);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Tile Z##noise", &noiseGridZ);

    kTerrain *noiseTile = terrainMgr->getTile(noiseGridX, noiseGridZ);
    if (!noiseTile)
        ImGui::TextDisabled("No tile at (%d, %d). Create one below.", noiseGridX, noiseGridZ);

    if (ImGui::Button("Apply Noise", ImVec2(-1, 0)) && noiseTile)
    {
        applyNoiseToTile(noiseTile, m_noiseScale, m_noiseAmplitude,
                         m_noiseOctaves, m_noiseSeed, m_noiseAdditive);
        if (manager && manager->getRenderer())
            manager->getRenderer()->setOctreeDirty();
    }

    ImGui::Spacing();

    // --- Tile Management ---
    ImGui::SeparatorText("Tile Management");

    // Add default tile at origin
    if (ImGui::Button("Add Default Terrain", ImVec2(-1, 0)))
    {
        addTerrainTile(0, 0);
    }

    ImGui::Spacing();

    // Show existing tiles in a grid view
    int totalTiles = terrainMgr->getTotalCount();
    int loadedCount = terrainMgr->getLoadedCount();
    ImGui::Text("Tiles: %d total, %d loaded", totalTiles, loadedCount);

    ImGui::Spacing();

    // --- Fill Grid: create all missing tiles in a rectangular range ---
    ImGui::Text("Fill Grid:");
    static int fillMinX = 0;
    static int fillMaxX = 2;
    static int fillMinZ = 0;
    static int fillMaxZ = 2;
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Min X", &fillMinX);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Max X", &fillMaxX);
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Min Z", &fillMinZ);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Max Z", &fillMaxZ);

    // Clamp sane ranges
    if (fillMaxX < fillMinX)
        fillMaxX = fillMinX;
    if (fillMaxZ < fillMinZ)
        fillMaxZ = fillMinZ;
    const int kMaxRange = 32;
    if (fillMaxX - fillMinX > kMaxRange)
        fillMaxX = fillMinX + kMaxRange;
    if (fillMaxZ - fillMinZ > kMaxRange)
        fillMaxZ = fillMinZ + kMaxRange;

    int cellCount = (fillMaxX - fillMinX + 1) * (fillMaxZ - fillMinZ + 1);
    int existingCount = 0;
    for (int gx = fillMinX; gx <= fillMaxX; ++gx)
        for (int gz = fillMinZ; gz <= fillMaxZ; ++gz)
            if (terrainMgr->getTile(gx, gz))
                ++existingCount;

    ImGui::Text("Cells: %d total, %d existing, %d to create", cellCount, existingCount, cellCount - existingCount);

    if (ImGui::Button("Fill Grid", ImVec2(-1, 0)))
    {
        for (int gx = fillMinX; gx <= fillMaxX; ++gx)
            for (int gz = fillMinZ; gz <= fillMaxZ; ++gz)
                addTerrainTile(gx, gz);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Single-tile expand/remove ---
    ImGui::Text("Single Tile:");
    static int expandGridX = 0;
    static int expandGridZ = 0;
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Grid X##single", &expandGridX);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Grid Z##single", &expandGridZ);

    kTerrain *singleTile = terrainMgr->getTile(expandGridX, expandGridZ);
    if (singleTile)
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Tile exists");
    else
        ImGui::TextDisabled("No tile here");

    if (ImGui::Button("N (+Z)##single", ImVec2(60, 30)))
        expandTerrain(expandGridX, expandGridZ, "north");
    ImGui::SameLine();
    if (ImGui::Button("S (-Z)##single", ImVec2(60, 30)))
        expandTerrain(expandGridX, expandGridZ, "south");
    ImGui::SameLine();
    if (ImGui::Button("E (+X)##single", ImVec2(60, 30)))
        expandTerrain(expandGridX, expandGridZ, "east");
    ImGui::SameLine();
    if (ImGui::Button("W (-X)##single", ImVec2(60, 30)))
        expandTerrain(expandGridX, expandGridZ, "west");

    ImGui::Spacing();

    // Remove tile
    if (ImGui::Button("Remove Tile", ImVec2(-1, 0)))
    {
        terrainMgr->removeTile(expandGridX, expandGridZ);
    }

    ImGui::Spacing();

    // --- Player position update ---
    ImGui::SeparatorText("Player Position");
    static kVec3 playerPos(0.0f, 0.0f, 0.0f);
    ImGui::DragFloat3("Position", &playerPos.x, 1.0f);

    if (m_autoUpdate)
    {
        terrainMgr->update(playerPos, static_cast<float>(m_loadRadius),
                           static_cast<float>(m_unloadRadius));
    }

    if (ImGui::Button("Manual Update", ImVec2(-1, 0)))
    {
        terrainMgr->update(playerPos, static_cast<float>(m_loadRadius),
                           static_cast<float>(m_unloadRadius));
    }

    ImGui::Spacing();

    // --- File Operations ---
    ImGui::SeparatorText("File Operations");

    if (ImGui::Button("Save All Terrains", ImVec2(-1, 0)))
    {
        saveProjectTerrains();
    }

    if (ImGui::Button("Load Terrains from Project", ImVec2(-1, 0)))
    {
        loadProjectTerrains();
    }

    ImGui::Spacing();

    // --- Layer Configuration ---
    ImGui::SeparatorText("Material Layers");

    static int selectedTileGridX = 0;
    static int selectedTileGridZ = 0;
    ImGui::InputInt("Tile Grid X##layer", &selectedTileGridX);
    ImGui::InputInt("Tile Grid Z##layer", &selectedTileGridZ);

    kTerrain *selectedTile = terrainMgr->getTile(selectedTileGridX, selectedTileGridZ);
    if (selectedTile)
    {
        auto &layers = selectedTile->getLayers();
        ImGui::Text("Layers for tile (%d, %d): %zu", selectedTileGridX, selectedTileGridZ, layers.size());

        for (size_t i = 0; i < layers.size(); ++i)
        {
            auto &layer = layers[i];
            kString label = layer.name + "##layer" + std::to_string(i);
            if (ImGui::TreeNode(label.c_str()))
            {
                static char nameBuf[128];
                strncpy(nameBuf, layer.name.c_str(), sizeof(nameBuf) - 1);
                if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
                    layer.name = nameBuf;
                ImGui::InputFloat("Tile Size", &layer.tileSize);
                ImGui::InputFloat("Min Height", &layer.minHeight);
                ImGui::InputFloat("Max Height", &layer.maxHeight);
                ImGui::InputFloat("Slope Min", &layer.slopeMin);
                ImGui::InputFloat("Slope Max", &layer.slopeMax);
                ImGui::TreePop();
            }
        }

        if (ImGui::Button("Add Layer"))
        {
            kTerrainLayer newLayer;
            newLayer.name = "New Layer";
            selectedTile->setLayer(newLayer);
        }

        ImGui::SameLine();
        if (ImGui::Button("Rebuild Mesh"))
        {
            selectedTile->rebuildMesh(true);
        }
    }
    else
    {
        ImGui::TextDisabled("No tile at (%d, %d). Create one above.", selectedTileGridX, selectedTileGridZ);
    }

    ImGui::End();
}

void PanelTerrain::addTerrainTile(int gridX, int gridZ)
{
    if (!manager || !manager->terrainManager)
        return;

    kTerrainManager *mgr = manager->terrainManager;
    kTerrain *tile = mgr->createTile(gridX, gridZ);
    if (tile)
    {
        tile->fillHeight(m_defaultHeight);
        manager->getRenderer()->setOctreeDirty();
    }
}

void PanelTerrain::expandTerrain(int gridX, int gridZ, const kString &direction)
{
    if (!manager || !manager->terrainManager)
        return;

    int newGridX = gridX;
    int newGridZ = gridZ;

    if (direction == "north")
        newGridZ += 1;
    else if (direction == "south")
        newGridZ -= 1;
    else if (direction == "east")
        newGridX += 1;
    else if (direction == "west")
        newGridX -= 1;

    addTerrainTile(newGridX, newGridZ);
}

kString PanelTerrain::getDataDirForTerrain(const kString &terrainFilePath)
{
    fs::path tp(terrainFilePath);
    kString stem = tp.stem().string();
    return (tp.parent_path() / (stem + "_data")).string();
}

void PanelTerrain::onTerrainFileRenamed(const kString &oldPath, const kString &newPath)
{
    kString oldDataDir = getDataDirForTerrain(oldPath);
    kString newDataDir = getDataDirForTerrain(newPath);

    if (fs::exists(oldDataDir) && fs::is_directory(oldDataDir))
    {
        try
        {
            fs::rename(oldDataDir, newDataDir);
        }
        catch (const fs::filesystem_error &e)
        {
            std::cerr << "[PanelTerrain] Failed to rename data folder: " << e.what() << std::endl;
        }
    }
}

void PanelTerrain::loadProjectTerrains()
{
    if (!manager || !manager->terrainManager)
        return;

    // Terrain data (height, splat) is stored in Library/Terrains/
    // using the mesh UUID as filename, linking terrain data to scene objects.
    fs::path libDir = manager->projectPath / "Library" / "Terrains";

    manager->terrainManager->loadFromDirectory(libDir.string());

    if (manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();

    m_terrainDirectory = libDir.string();
}

void PanelTerrain::saveProjectTerrains() const
{
    if (!manager)
    {
        std::cerr << "saveProjectTerrains: manager is null\n";
        return;
    }
    if (!manager->terrainManager)
    {
        std::cerr << "saveProjectTerrains: terrainManager is null\n";
        return;
    }

    fs::path libDir = manager->projectPath / "Library" / "Terrains";
    fs::create_directories(libDir);
    std::cerr << "saveProjectTerrains: saving " << manager->terrainManager->getTotalCount() << " tiles to " << libDir << "\n";

    for (const auto &pair : manager->terrainManager->getTiles())
    {
        const kTerrain *tile = pair.second.get();
        kMesh *mesh = tile->getMesh();
        if (!mesh)
        {
            std::cerr << "saveProjectTerrains: tile has no mesh, skipping\n";
            continue;
        }

        kString uuid = mesh->getUuid();
        if (uuid.empty())
        {
            std::cerr << "saveProjectTerrains: mesh has empty uuid, skipping\n";
            continue;
        }

        kString hFile = (libDir / (uuid + ".height")).string();
        kString sFile = (libDir / (uuid + ".splat")).string();
        std::cerr << "saveProjectTerrains: saving " << hFile << " and " << sFile << "\n";
        tile->saveHeightData(hFile);
        tile->saveSplatMap(sFile);
    }
}