/**
 * @file panel_terrain.h
 * @brief Editor panel for managing grid-based terrain tiles.
 */

#ifndef PANEL_TERRAIN_H
#define PANEL_TERRAIN_H

#include <string>
#include <filesystem>
#include <unordered_map>

#include <kemena/kemena.h>
#include <kemena/kterrain.h>

#include "manager.h"

namespace fs = std::filesystem;
using namespace kemena;

class Manager;

class PanelTerrain
{
public:
    bool enabled = false;

    struct SculptState
    {
        bool active = false;
        kTerrain::BrushMode mode = kTerrain::BrushMode::Raise;
        float radius = 5.0f;
        float strength = 0.5f;
        float flattenHeight = 0.0f;
        bool needsRebuild = false;
        kTerrain *affectedTile = nullptr;
    };
    SculptState sculpt;

    struct PaintState
    {
        bool active = false;
        int channel = -1;
        bool heightActive = false;
        float strength = 0.5f;
        float fade = 0.3f;
        kString maskTexturePath;
        bool needsRebuild = false;
        kTerrain *affectedTile = nullptr;
    };
    PaintState paint;

    kVec3 paintCursorPos = kVec3(0.0f);
    kVec3 paintCursorNormal = kVec3(0.0f, 1.0f, 0.0f);
    bool paintCursorValid = false;

    PanelTerrain(kGuiManager *setGuiManager, Manager *setManager);

    void draw(bool &isOpened);
    void onTerrainFileRenamed(const kString &oldPath, const kString &newPath);
    void loadProjectTerrains();
    void saveProjectTerrains() const;
    void sculptAtWorldPos(const kVec3 &worldPos);
    void rebuildSculptedTile();
    void paintAtWorldPos(const kVec3 &worldPos, bool lower);
    void rebuildPaintedTile();
    void paintSplatAtWorldPos(const kVec3 &worldPos, bool erase);
    void rebuildPaintedSplatTile();
    void applyNoiseToTile(kTerrain *tile, float scale, float amplitude,
                          int octaves, int seed, bool additive);

private:
    void addTerrainTile(int gridX, int gridZ);
    void expandTerrain(int gridX, int gridZ, const kString &direction);
    static kString getDataDirForTerrain(const kString &terrainFilePath);
    static float noiseValue(int x, int y, int seed);
    static float noiseSmooth(float x, float y, int seed);

    kGuiManager *gui = nullptr;
    Manager *manager = nullptr;

    float m_newTileSize = 256.0f;
    int m_newHeightRes = 513;
    float m_defaultHeight = 0.0f;
    int m_loadRadius = 512;
    int m_unloadRadius = 768;
    bool m_autoUpdate = true;

    float m_noiseScale = 50.0f;
    float m_noiseAmplitude = 10.0f;
    int m_noiseOctaves = 4;
    int m_noiseSeed = 42;
    bool m_noiseAdditive = false;

    kString m_terrainDirectory;
};

#endif // PANEL_TERRAIN_H
