#include "util.h"

#include <stb/stb_image.h>
#define STB_DXT_IMPLEMENTATION
#include <stb/stb_dxt.h>
// Implementation lives in manager.cpp; here we only need the declarations.
#include <stb/stb_image_resize2.h>

#include <fstream> // for std::ofstream
#include <vector>
#include <iostream>
#include <filesystem>
#include <cstring> // for memcpy
#include <sstream>
#include <cctype>
#include <algorithm>       // for std::max
#include <cmath>           // for std::sqrt
#include <assimp/config.h> // for AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY
#include <assimp/DefaultLogger.hpp>
#include <assimp/LogStream.hpp>
#include <assimp/Logger.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

#include <imgui.h>

uint32_t utf8Next(const char *&it, const char *end)
{
    if (it >= end)
        return 0;

    unsigned char c = static_cast<unsigned char>(*it++);
    if (c < 0x80)
        return c; // ASCII

    // Multibyte
    int extra = 0;
    uint32_t cp = 0;
    if ((c & 0xE0) == 0xC0)
    {
        cp = c & 0x1F;
        extra = 1;
    }
    else if ((c & 0xF0) == 0xE0)
    {
        cp = c & 0x0F;
        extra = 2;
    }
    else if ((c & 0xF8) == 0xF0)
    {
        cp = c & 0x07;
        extra = 3;
    }
    else
        return 0xFFFD;

    for (int i = 0; i < extra; i++)
    {
        if (it >= end)
            return 0xFFFD;
        unsigned char cc = static_cast<unsigned char>(*it);
        if ((cc & 0xC0) != 0x80)
            return 0xFFFD;
        cp = (cp << 6) | (cc & 0x3F);
        ++it;
    }
    return cp;
}

// Encode UTF-8 codepoint
void utf8Encode(uint32_t cp, kString &out)
{
    if (cp < 0x80)
        out.push_back((char)cp);
    else if (cp < 0x800)
    {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

kString fitTextWithEllipsisUtf8(kGuiManager *gui, const kString &text, float maxWidth)
{
    if (text.empty())
        return "";

    if (gui->calcTextSize(text).x <= maxWidth)
        return text;

    const kString ell = "...";

    const char *begin = text.c_str();
    const char *end = text.c_str() + text.size();
    const char *it = begin;

    kString out;
    while (it < end)
    {
        const char *prev = it;
        uint32_t cp = utf8Next(it, end);

        kString tmp = out;
        utf8Encode(cp, tmp);
        tmp += ell;

        if (gui->calcTextSize(tmp).x > maxWidth)
            break;

        out.append(prev, it);
    }

    out += ell;
    return out;
}

// Recursively scale a node's local translation.
static void scaleNodeTranslations(aiNode *n, float s)
{
    if (!n)
        return;
    n->mTransformation.a4 *= s;
    n->mTransformation.b4 *= s;
    n->mTransformation.c4 *= s;
    for (unsigned int i = 0; i < n->mNumChildren; ++i)
        scaleNodeTranslations(n->mChildren[i], s);
}

// Bake a uniform scale into a mutable scene's geometry. The runtime mesh loader
// ignores node transforms, so the scale has to live in the vertices (and, for
// skinned/animated meshes, in the bone bind offsets and animation position
// keys) rather than in a node matrix.
static void scaleSceneUniform(aiScene *s, float scale)
{
    if (!s || scale == 1.0f)
        return;

    for (unsigned int m = 0; m < s->mNumMeshes; ++m)
    {
        aiMesh *mesh = s->mMeshes[m];
        for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
            mesh->mVertices[v] *= scale;
        for (unsigned int b = 0; b < mesh->mNumBones; ++b)
        {
            aiMatrix4x4 &o = mesh->mBones[b]->mOffsetMatrix;
            o.a4 *= scale;
            o.b4 *= scale;
            o.c4 *= scale; // translation part
        }
    }

    for (unsigned int a = 0; a < s->mNumAnimations; ++a)
    {
        aiAnimation *anim = s->mAnimations[a];
        for (unsigned int c = 0; c < anim->mNumChannels; ++c)
        {
            aiNodeAnim *ch = anim->mChannels[c];
            for (unsigned int k = 0; k < ch->mNumPositionKeys; ++k)
                ch->mPositionKeys[k].mValue *= scale;
        }
    }

    scaleNodeTranslations(s->mRootNode, scale);
}

// Forwards Assimp warning-severity log lines into a std::vector<std::string>.
// Only attached for Warn severity, so anything it receives is a real warning.
namespace
{
    class WarnCaptureStream : public Assimp::LogStream
    {
    public:
        explicit WarnCaptureStream(std::vector<std::string> *out) : out(out) {}
        void write(const char *message) override
        {
            if (!out || !message)
                return;
            std::string s(message);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
                s.pop_back();
            if (!s.empty())
                out->push_back(s);
        }

    private:
        std::vector<std::string> *out;
    };
}

bool convertMeshToGlbEx(const fs::path &inputPath, const fs::path &outputPath,
                        const MeshImportOptions &opt, std::string *errorOut,
                        std::vector<std::string> *warningsOut)
{
    Assimp::Importer importer;

    // Optionally capture Assimp warnings into warningsOut. The DefaultLogger is a
    // global singleton; create one only if the app hasn't, and kill only what we
    // created. (Imports never run concurrently — they're gated behind a modal.)
    WarnCaptureStream warnStream(warningsOut);
    bool createdLogger = false;
    if (warningsOut)
    {
        if (Assimp::DefaultLogger::isNullLogger())
        {
            // defStreams = 0 so Assimp doesn't spawn its default file/debugger logs.
            Assimp::DefaultLogger::create("", Assimp::Logger::NORMAL, 0);
            createdLogger = true;
        }
        Assimp::DefaultLogger::get()->attachStream(&warnStream, Assimp::Logger::Warn);
    }
    struct WarnGuard
    {
        WarnCaptureStream *s;
        bool *created;
        bool active;
        ~WarnGuard()
        {
            if (!active)
                return;
            Assimp::DefaultLogger::get()->detachStream(s, Assimp::Logger::Warn);
            if (*created)
                Assimp::DefaultLogger::kill();
        }
    } warnGuard{&warnStream, &createdLogger, warningsOut != nullptr};

    // GenSmoothNormals (not GenNormals) so meshes without normals get smooth
    // shading instead of facets. ValidateDataStructure is intentionally NOT used:
    // it rejects otherwise-loadable glTF and was causing "Failed to convert".
    // Scale is baked into geometry below (not via aiProcess_GlobalScale) because
    // the runtime loader ignores node transforms.
    unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_LimitBoneWeights |
        aiProcess_SortByPType |
        aiProcess_ImproveCacheLocality;
    if (opt.tangents == 1)
        flags |= aiProcess_CalcTangentSpace; // needed for normal mapping

    const aiScene *scene = importer.ReadFile(inputPath.string(), flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
    {
        if (errorOut)
            *errorOut = importer.GetErrorString();
        std::cerr << "Assimp failed to load " << inputPath
                  << ": " << importer.GetErrorString() << "\n";
        return false;
    }

    const bool doScale = (opt.scaleFactor > 0.0f && opt.scaleFactor != 1.0f);
    const bool doStripAnim = (!opt.importAnimation && scene->mNumAnimations > 0);

    // We need a mutable scene to bake scale and/or drop animations.
    aiScene *owned = nullptr;
    const aiScene *toExport = scene;
    if (doScale || doStripAnim)
    {
        owned = importer.GetOrphanedScene(); // ownership transfers to us
        toExport = owned;

        if (doStripAnim)
        {
            for (unsigned int i = 0; i < owned->mNumAnimations; ++i)
                delete owned->mAnimations[i];
            delete[] owned->mAnimations;
            owned->mAnimations = nullptr;
            owned->mNumAnimations = 0;
        }
        if (doScale)
            scaleSceneUniform(owned, opt.scaleFactor);
    }
    else if (scene->mNumAnimations > 0)
    {
        std::cout << inputPath << " contains " << scene->mNumAnimations
                  << " animation(s). They will be exported.\n";
    }

    Assimp::Exporter exporter;
    aiReturn ret = exporter.Export(toExport, "glb2", outputPath.string());
    delete owned;

    if (ret != AI_SUCCESS)
    {
        if (errorOut)
            *errorOut = exporter.GetErrorString();
        std::cerr << "Assimp failed to export " << outputPath
                  << ": " << exporter.GetErrorString() << "\n";
        return false;
    }

    std::cout << "Converted: " << inputPath << " -> " << outputPath << "\n";
    return true;
}

bool convertMeshToGlb(const fs::path &inputPath, const fs::path &outputPath)
{
    return convertMeshToGlbEx(inputPath, outputPath, MeshImportOptions{});
}

// DXT5-compress a single mip level (RGBA8, tightly packed) into a block buffer.
// Partial edge blocks are padded with zeros, matching the DDS block layout.
static std::vector<unsigned char> dxt5CompressLevel(const unsigned char *rgba, int w, int h, int quality)
{
    int blockCountX = (w + 3) / 4;
    int blockCountY = (h + 3) / 4;
    std::vector<unsigned char> out((size_t)blockCountX * blockCountY * 16);

    unsigned char *outPtr = out.data();
    for (int by = 0; by < h; by += 4)
    {
        for (int bx = 0; bx < w; bx += 4)
        {
            unsigned char block[64] = {0}; // 4x4 RGBA pixels
            for (int yy = 0; yy < 4; yy++)
            {
                for (int xx = 0; xx < 4; xx++)
                {
                    int sx = bx + xx;
                    int sy = by + yy;
                    unsigned char *dst = &block[4 * (yy * 4 + xx)];
                    if (sx < w && sy < h)
                        memcpy(dst, rgba + 4 * ((size_t)sy * w + sx), 4);
                    else
                        memset(dst, 0, 4);
                }
            }
            stb_compress_dxt_block(outPtr, block, 1 /*alpha=DXT5*/, quality);
            outPtr += 16;
        }
    }
    return out;
}

// Half a dimension, never below 1 (standard mip-chain reduction).
static inline int mipHalf(int v) { return v > 1 ? v / 2 : 1; }

// Convert a grayscale height map (RGBA in, height = Rec.601 luma) into a
// tangent-space normal map (RGBA out, +Z up, alpha opaque). `bumpiness` scales
// the slope. When `smooth` is set the height field is pre-blurred (separable
// 5-tap box) before differencing, giving noticeably softer normals; `sharp`
// differences the raw height for crisp, high-frequency detail. Edges clamp.
static std::vector<unsigned char> heightToNormal(const unsigned char *rgba, int w, int h, float bumpiness, bool smooth)
{
    const size_t n = (size_t)w * h;

    // Build a float height field (Rec.601 luma, 0..1).
    std::vector<float> hf(n);
    for (size_t i = 0; i < n; ++i)
    {
        const unsigned char *p = rgba + i * 4;
        hf[i] = (p[0] * 0.299f + p[1] * 0.587f + p[2] * 0.114f) / 255.0f;
    }

    auto clampX = [&](int x)
    { return std::min(std::max(x, 0), w - 1); };
    auto clampY = [&](int y)
    { return std::min(std::max(y, 0), h - 1); };

    // Smooth: separable 5-tap box blur of the height field. This is what makes
    // the Sharp/Smooth choice clearly visible.
    if (smooth)
    {
        const int R = 2; // radius -> 5 taps
        std::vector<float> tmp(n);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                float s = 0.0f;
                for (int k = -R; k <= R; ++k)
                    s += hf[(size_t)y * w + clampX(x + k)];
                tmp[(size_t)y * w + x] = s / (2 * R + 1);
            }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                float s = 0.0f;
                for (int k = -R; k <= R; ++k)
                    s += tmp[(size_t)clampY(y + k) * w + x];
                hf[(size_t)y * w + x] = s / (2 * R + 1);
            }
    }

    auto at = [&](int x, int y) -> float
    { return hf[(size_t)clampY(y) * w + clampX(x)]; };

    std::vector<unsigned char> out(n * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            float dx = at(x + 1, y) - at(x - 1, y);
            float dy = at(x, y + 1) - at(x, y - 1);
            float nx = -dx * bumpiness;
            float ny = -dy * bumpiness;
            float nz = 1.0f;
            float inv = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            nx *= inv;
            ny *= inv;
            nz *= inv;
            unsigned char *o = out.data() + 4 * ((size_t)y * w + x);
            o[0] = (unsigned char)(std::min(std::max((nx * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f), 255.0f));
            o[1] = (unsigned char)(std::min(std::max((ny * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f), 255.0f));
            o[2] = (unsigned char)(std::min(std::max((nz * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f), 255.0f));
            o[3] = 255;
        }
    return out;
}

bool convertImageToDDS(const fs::path &inputPath, const fs::path &outputPath, const ImageImportOptions &opt)
{
    // stb's flip flag is a sticky global; set it for this load only and reset
    // it afterwards so other studio image loads aren't affected.
    stbi_set_flip_vertically_on_load(opt.flipVertical ? 1 : 0);
    int srcW, srcH, comp;
    unsigned char *src = stbi_load(inputPath.string().c_str(), &srcW, &srcH, &comp, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!src)
    {
        std::cerr << "Failed to load image: " << inputPath << "\n";
        return false;
    }

    if (opt.isNormalMap)
    {
        // --- Normal-map processing ---------------------------------------
        if (opt.fromGrayscale)
        {
            std::vector<unsigned char> nrm =
                heightToNormal(src, srcW, srcH, opt.bumpiness, opt.normalFilter == 1);
            memcpy(src, nrm.data(), (size_t)srcW * srcH * 4);
        }
        if (opt.flipGreen)
            for (size_t i = 0; i < (size_t)srcW * srcH; ++i)
                src[i * 4 + 1] = (unsigned char)(255 - src[i * 4 + 1]);
        // Normal maps don't carry alpha — keep it opaque.
        for (size_t i = 0; i < (size_t)srcW * srcH; ++i)
            src[i * 4 + 3] = 255;
    }
    else
    {
        // --- Alpha source ------------------------------------------------
        // 0 = None (force opaque), 1 = keep input alpha, 2 = derive from grayscale.
        if (opt.alphaSource == 0)
        {
            for (size_t i = 0; i < (size_t)srcW * srcH; ++i)
                src[i * 4 + 3] = 255;
        }
        else if (opt.alphaSource == 2)
        {
            for (size_t i = 0; i < (size_t)srcW * srcH; ++i)
            {
                unsigned char *p = src + i * 4;
                // Rec.601 luma; good enough for an authored grayscale->alpha mask.
                p[3] = (unsigned char)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
            }
        }

        // --- Linear Grayscale channel: collapse RGB to luma --------------
        if (opt.grayscale)
        {
            for (size_t i = 0; i < (size_t)srcW * srcH; ++i)
            {
                unsigned char *p = src + i * 4;
                unsigned char y = (unsigned char)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
                p[0] = p[1] = p[2] = y;
            }
        }
    }

    // --- Resize to clamp the longest edge to Max Size ---------------------
    // Always resize into a caller-owned vector (never the NULL-output allocator)
    // so ownership is unambiguous across translation units.
    int w = srcW, h = srcH;
    std::vector<unsigned char> top;
    int maxSize = opt.maxSize > 0 ? opt.maxSize : 4096;
    int longest = std::max(srcW, srcH);
    if (longest > maxSize)
    {
        double scale = (double)maxSize / (double)longest;
        w = std::max(1, (int)(srcW * scale + 0.5));
        h = std::max(1, (int)(srcH * scale + 0.5));
        top.resize((size_t)w * h * 4);
        unsigned char *ok = opt.sRGB
                                ? stbir_resize_uint8_srgb(src, srcW, srcH, 0, top.data(), w, h, 0, STBIR_RGBA)
                                : stbir_resize_uint8_linear(src, srcW, srcH, 0, top.data(), w, h, 0, STBIR_RGBA);
        if (!ok)
        {
            std::cerr << "Failed to resize image: " << inputPath << "\n";
            stbi_image_free(src);
            return false;
        }
    }
    else
    {
        top.assign(src, src + (size_t)srcW * srcH * 4);
    }
    stbi_image_free(src);
    src = nullptr;

    // --- Build the mip chain ---------------------------------------------
    int levels = 1;
    if (opt.generateMipmap)
    {
        int lw = w, lh = h;
        while (lw > 1 || lh > 1)
        {
            lw = mipHalf(lw);
            lh = mipHalf(lh);
            ++levels;
        }
    }

    const bool compressed = (opt.compression != 0);
    const int quality = (opt.compression >= 3) ? STB_DXT_HIGHQUAL : STB_DXT_NORMAL;

    // Concatenate every level's bytes (top first), generating each mip by
    // downsampling the *previous* level so the chain stays consistent.
    std::vector<unsigned char> payload;
    std::vector<unsigned char> prev = std::move(top);
    int lw = w, lh = h;
    for (int level = 0; level < levels; ++level)
    {
        if (level > 0)
        {
            int nw = mipHalf(lw), nh = mipHalf(lh);
            std::vector<unsigned char> next((size_t)nw * nh * 4);
            unsigned char *ok = opt.sRGB
                                    ? stbir_resize_uint8_srgb(prev.data(), lw, lh, 0, next.data(), nw, nh, 0, STBIR_RGBA)
                                    : stbir_resize_uint8_linear(prev.data(), lw, lh, 0, next.data(), nw, nh, 0, STBIR_RGBA);
            if (!ok)
            {
                std::cerr << "Mip resize failed: " << inputPath << "\n";
                return false;
            }
            prev.swap(next);
            lw = nw;
            lh = nh;
        }

        if (compressed)
        {
            std::vector<unsigned char> blk = dxt5CompressLevel(prev.data(), lw, lh, quality);
            payload.insert(payload.end(), blk.begin(), blk.end());
        }
        else
        {
            payload.insert(payload.end(), prev.begin(), prev.end());
        }
    }

    // --- DDS header -------------------------------------------------------
    DDSHeader header{};
    header.dwMagic = 0x20534444; // "DDS "
    header.dwSize = 124;
    header.dwHeight = h;
    header.dwWidth = w;
    header.dwMipMapCount = (uint32_t)levels;

    uint32_t flags = 0x1 | 0x2 | 0x4 | 0x1000; // CAPS | HEIGHT | WIDTH | PIXELFORMAT
    if (levels > 1)
        flags |= 0x20000; // DDSD_MIPMAPCOUNT
    header.ddpf.dwSize = 32;

    if (compressed)
    {
        flags |= 0x80000; // DDSD_LINEARSIZE
        header.dwPitchOrLinearSize = (uint32_t)((((w + 3) / 4) * (size_t)((h + 3) / 4)) * 16);
        header.ddpf.dwFlags = 0x4; // DDPF_FOURCC
        header.ddpf.dwFourCC = ('D') | ('X' << 8) | ('T' << 16) | ('5' << 24);
    }
    else
    {
        flags |= 0x8; // DDSD_PITCH
        header.dwPitchOrLinearSize = (uint32_t)(w * 4);
        header.ddpf.dwFlags = 0x41; // DDPF_RGB | DDPF_ALPHAPIXELS
        header.ddpf.dwRGBBitCount = 32;
        header.ddpf.dwRBitMask = 0x000000FF; // memory order R,G,B,A
        header.ddpf.dwGBitMask = 0x0000FF00;
        header.ddpf.dwBBitMask = 0x00FF0000;
        header.ddpf.dwABitMask = 0xFF000000;
    }

    header.dwFlags = flags;
    header.caps.dwCaps1 = 0x1000; // DDSCAPS_TEXTURE
    if (levels > 1)
        header.caps.dwCaps1 |= 0x8 | 0x400000; // COMPLEX | MIPMAP

    std::ofstream ofs(outputPath, std::ios::binary);
    if (!ofs)
    {
        std::cerr << "Failed to open output file: " << outputPath << "\n";
        return false;
    }
    ofs.write(reinterpret_cast<const char *>(&header), sizeof(header));
    ofs.write(reinterpret_cast<const char *>(payload.data()), payload.size());
    ofs.close();

    std::cout << "Converted: " << inputPath << " -> " << outputPath
              << " (" << w << "x" << h << ", " << levels << " level(s), "
              << (compressed ? "DXT5" : "RGBA8") << ")\n";
    return true;
}

// Back-compat wrapper: default import settings (used by the initial batch
// import before the user customizes per-texture settings).
bool convertImageToDxt5(const fs::path &inputPath, const fs::path &outputPath)
{
    return convertImageToDDS(inputPath, outputPath, ImageImportOptions{});
}

// ---------------------------------------------------------------------------
// Shader reflection
// ---------------------------------------------------------------------------

static kString svTrim(const kString &s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a]))
        a++;
    while (b > a && std::isspace((unsigned char)s[b - 1]))
        b--;
    return s.substr(a, b - a);
}

// Turn a uniform name into a friendlier label: drop a leading "material." and a
// trailing "Map", and Title-case the first letter. e.g. "material.diffuse" ->
// "Diffuse", "albedoMap" -> "Albedo".
static kString svPrettyLabel(const kString &name)
{
    kString n = name;
    size_t dot = n.rfind('.');
    if (dot != kString::npos)
        n = n.substr(dot + 1);
    if (n.size() > 3 && n.compare(n.size() - 3, 3, "Map") == 0)
        n = n.substr(0, n.size() - 3);
    if (!n.empty())
        n[0] = (char)std::toupper((unsigned char)n[0]);
    return n;
}

std::vector<ShaderVar> parseShaderVars(const kString &source)
{
    std::vector<ShaderVar> vars;
    std::istringstream ss(source);
    std::string line;
    while (std::getline(ss, line))
    {
        size_t cpos = line.find("//");
        if (cpos == std::string::npos)
            continue;
        size_t apos = line.find("@var", cpos);
        if (apos == std::string::npos)
            continue;

        std::istringstream ls(line.substr(apos + 4)); // after "@var"
        ShaderVar v;
        if (!(ls >> v.type))
            continue;
        if (!(ls >> v.name))
            continue;

        std::string rest;
        std::getline(ls, rest);
        rest = svTrim(rest);
        // Allow an optional quoted or bare label.
        if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"')
            rest = rest.substr(1, rest.size() - 2);
        v.label = rest.empty() ? svPrettyLabel(v.name) : rest;

        vars.push_back(v);
    }
    return vars;
}

kString getEmbeddedResourceText(const kString &resourceName)
{
#ifdef _WIN32
    HRSRC hRes = FindResourceA(NULL, resourceName.c_str(), RT_RCDATA);
    if (!hRes)
        return "";
    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData)
        return "";
    DWORD size = SizeofResource(NULL, hRes);
    const char *data = static_cast<const char *>(LockResource(hData));
    if (!data || size == 0)
        return "";
    return kString(data, size);
#else
    (void)resourceName;
    return "";
#endif
}

kString builtinShaderResource(const kString &shaderName)
{
    if (shaderName == "Unlit")
        return "SHADER_MESH_FLAT";
    if (shaderName == "Phong")
        return "SHADER_MESH_PHONG";
    if (shaderName == "PBR")
        return "SHADER_MESH_PBR";
    if (shaderName == "Terrain PBR")
        return "SHADER_TERRAIN_PBR";
    return "";
}
