#pragma once
#include "kemena/kemena.h"
#include "manager.h"
#include <string>
#include <vector>
#include <filesystem>
#include <functional>

using namespace kemena;
namespace fs = std::filesystem;

// ===========================================================================
// Particle asset data structures
// ===========================================================================

/**
 * @brief A single keyframe value for a float property curve.
 */
struct ParticleKeyFloat
{
    float time  = 0.0f;  ///< Normalised time (0..1) across the emitter's lifetime.
    float value = 1.0f;  ///< Property value at this time.
};

/**
 * @brief A single keyframe value for a colour property curve.
 */
struct ParticleKeyColor
{
    float time  = 0.0f;               ///< Normalised time (0..1) across the emitter's lifetime.
    kVec4 value = kVec4(1,1,1,1);     ///< RGBA colour at this time.
};

/**
 * @brief A curve with keyframes for a float property over lifetime.
 */
struct ParticleCurveFloat
{
    std::vector<ParticleKeyFloat> keys;
};

/**
 * @brief A curve with keyframes for a colour property over lifetime.
 */
struct ParticleCurveColor
{
    std::vector<ParticleKeyColor> keys;
};

/**
 * @brief Emission shape types for a particle emitter.
 */
enum class ParticleEmitShape
{
    Point,
    Sphere,
    Cone,
    Box
};

/**
 * @brief A single emitter within a particle system asset.
 */
struct ParticleEmitterDef
{
    std::string name = "Emitter";
    float startTime    = 0.0f;   ///< Seconds into the system when this emitter activates.
    float endTime      = 5.0f;   ///< Seconds into the system when this emitter stops (0 = forever).
    bool  forever      = false;  ///< If true, the emitter never stops (looping systems).

    // Emission
    float emissionRate  = 10.0f;  ///< Particles emitted per second.
    int   maxParticles  = 100;    ///< Maximum live particles at once.
    float lifetime      = 2.0f;   ///< Base particle lifetime in seconds.
    float lifetimeVar   = 0.0f;   ///< Random variance added to lifetime.

    // Velocity
    float speed         = 1.0f;   ///< Base emission speed.
    float speedVar      = 0.2f;   ///< Random speed variance.
    float gravityScale  = 1.0f;   ///< Gravity multiplier.

    // Emission shape
    ParticleEmitShape shape     = ParticleEmitShape::Cone;
    kVec3             shapeSize = kVec3(0.5f, 1.0f, 0.5f); ///< Half-extents / radius.

    // Visual
    kVec4 colorStart = kVec4(1,1,1,1);
    kVec4 colorEnd   = kVec4(1,1,1,0);
    float sizeStart  = 0.1f;
    float sizeEnd    = 0.0f;

    // Sprite / mesh rendering
    std::string spritePath;   ///< Optional sprite texture path (billboard).
    std::string meshUuid;     ///< Optional 3D mesh UUID (mesh particles).
    std::string materialUuid; ///< Optional material UUID for the mesh.

    // Physics
    bool  physicsEnabled = false;
    float physicsMass    = 0.1f;
    float physicsDrag    = 0.0f;

    // Curves (size/color/speed over lifetime)
    ParticleCurveFloat sizeOverLifetime;
    ParticleCurveColor colorOverLifetime;
    ParticleCurveFloat speedOverLifetime;

    // Serialisation
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);
};

/**
 * @brief Full .particle document loaded into the editor.
 */
struct ParticleDoc
{
    std::string uuid;
    std::string name;
    float       duration = 5.0f;  ///< Total system duration in seconds.
    bool        looping  = true;
    std::vector<ParticleEmitterDef> emitters;

    bool dirty = false;

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);
};

// ===========================================================================
// Particle Editor Panel
// ===========================================================================

/**
 * @brief Particle system editor with a timeline for emitter scheduling.
 *
 * Works with .particle files. Each emitter is shown as a horizontal bar on the
 * timeline, with per-emitter property editing in the detail panel below.
 */
class PanelParticle
{
public:
    bool focused = false;

    PanelParticle(kGuiManager* setGui, Manager* setManager);

    void draw(bool& isOpened);

    /** @brief Open a .particle file into the editor. */
    void openFile(const std::string& path);

    void saveCurrent() { saveDoc(); }

private:
    kGuiManager* gui     = nullptr;
    Manager*     manager = nullptr;

    ParticleDoc doc;
    std::string filePath;

    // --- Timeline state ---
    float timelineZoom   = 1.0f;
    float timelineScroll = 0.0f;
    float currentTime    = 0.0f;
    bool  isPlaying      = false;
    bool  loopPlayback   = true;

    int  selectedEmitter = -1;  ///< Index of the emitter being edited in the detail panel.

    // --- UI constants ---
    static constexpr float TIMELINE_HEADER_H = 28.f;
    static constexpr float EMITTER_ROW_H     = 36.f;
    static constexpr float LABEL_W           = 140.f;
    static constexpr float TRACK_START_X     = 150.f;

    // --- Methods ---
    void drawToolbar();
    void drawTimeline();
    void drawEmitterDetail();

    // Timeline helpers
    float timeToScreenX(float time, float originX, float width) const;
    float screenXToTime(float sx, float originX, float width) const;

    // File I/O
    void newDoc();
    void saveDoc();
    void saveDocAs();
    void loadDoc(const std::string& path);

    void addEmitter();
    void removeEmitter(int index);
    void duplicateEmitter(int index);

    static std::string generateUuid();
    static void SDLCALL saveParticleCallback(void* userdata, const char* const* filelist, int filter);
};
