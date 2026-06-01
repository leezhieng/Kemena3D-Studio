#ifndef UTIL_H
#define UTIL_H

#include "kemena/kemena.h"

using namespace kemena;

#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <string>
#include <vector>
#include <future>
#include <atomic>
#include <mutex>

#include <filesystem>

namespace fs = std::filesystem;

/**
 * @brief Binary layout of a DirectDraw Surface (.dds) file header.
 *
 * Mirrors the Microsoft DDS_HEADER structure and is used when writing out
 * compressed (e.g. DXT5) textures. Fields follow the DDS specification and are
 * laid out for direct serialization to/from the start of a .dds file.
 */
struct DDSHeader
{
    uint32_t dwMagic;              ///< Magic value, always the four-character code "DDS ".
    uint32_t dwSize;              ///< Size of this header structure in bytes (excluding dwMagic).
    uint32_t dwFlags;             ///< Flags indicating which header members contain valid data.
    uint32_t dwHeight;            ///< Surface height in pixels.
    uint32_t dwWidth;             ///< Surface width in pixels.
    uint32_t dwPitchOrLinearSize; ///< Pitch (uncompressed) or total byte size (compressed) of the top mip.
    uint32_t dwDepth;             ///< Depth of a volume texture in pixels; otherwise unused.
    uint32_t dwMipMapCount;       ///< Number of mipmap levels stored.
    uint32_t dwReserved1[11];     ///< Reserved; unused.

    /** @brief Pixel format descriptor (DDS_PIXELFORMAT). */
    struct
    {
        uint32_t dwSize;        ///< Size of this pixel-format structure in bytes.
        uint32_t dwFlags;       ///< Flags describing the pixel format (e.g. presence of a FourCC).
        uint32_t dwFourCC;      ///< Four-character compression code (e.g. "DXT5") when applicable.
        uint32_t dwRGBBitCount; ///< Number of bits per pixel for uncompressed formats.
        uint32_t dwRBitMask;    ///< Bit mask for the red channel.
        uint32_t dwGBitMask;    ///< Bit mask for the green channel.
        uint32_t dwBBitMask;    ///< Bit mask for the blue channel.
        uint32_t dwABitMask;    ///< Bit mask for the alpha channel.
    } ddpf;

    /** @brief Surface capability flags (DDS_CAPS). */
    struct
    {
        uint32_t dwCaps1;    ///< Primary capability flags (texture/mipmap/complex).
        uint32_t dwCaps2;    ///< Additional capabilities (cubemap/volume faces).
        uint32_t dwDDSX;     ///< Reserved capability field; unused.
        uint32_t dwReserved; ///< Reserved; unused.
    } caps;

    uint32_t dwReserved2; ///< Reserved; unused.
};

/**
 * @brief Decode the next UTF-8 codepoint starting at @p it and advance the iterator.
 *
 * Reads one full UTF-8 sequence from the byte range [it, end), advancing @p it
 * past the consumed bytes.
 *
 * @param it  Reference to the current read position; advanced past the decoded sequence.
 * @param end One-past-the-end pointer bounding the input range.
 * @return The decoded Unicode codepoint, or 0xFFFD (replacement character) on malformed input.
 */
uint32_t utf8Next(const char*& it, const char* end);

/**
 * @brief Encode a Unicode codepoint as UTF-8 and append it to a string.
 *
 * @param cp  Unicode codepoint to encode.
 * @param out Destination string; the encoded bytes are appended.
 */
void utf8Encode(uint32_t cp, kString& out);

/**
 * @brief Truncate a UTF-8 string with a trailing "..." if it exceeds a pixel width.
 *
 * Measures the rendered width via the GUI manager and, if the text is wider than
 * @p maxWidth, removes whole codepoints from the end until the text plus the
 * ellipsis fits. Returns the original text unchanged when it already fits.
 *
 * @param gui      GUI manager used to measure rendered text size.
 * @param text     Source UTF-8 text to fit.
 * @param maxWidth Maximum allowed rendered width in pixels.
 * @return The original or ellipsized text that fits within @p maxWidth.
 */
kString fitTextWithEllipsisUtf8(kGuiManager *gui, const kString& text, float maxWidth);

/**
 * @brief Convert a model file to glTF binary (.glb) using Assimp.
 *
 * Imports the mesh (including animations) from @p inputPath and exports it as a
 * .glb file at @p outputPath.
 *
 * @param inputPath  Path to the source model file in any Assimp-supported format.
 * @param outputPath Destination path for the generated .glb file.
 * @return True on successful conversion, false on import or export failure.
 */
bool convertMeshToGlb(const fs::path& inputPath, const fs::path& outputPath);

/**
 * @brief Convert an image file to a DXT5-compressed DDS texture.
 *
 * Loads @p inputPath as RGBA, compresses it block-by-block to DXT5, and writes a
 * .dds file (with a DDSHeader) to @p outputPath.
 *
 * @param inputPath  Path to the source image file.
 * @param outputPath Destination path for the generated DXT5 .dds file.
 * @return True on successful conversion, false if the image could not be loaded or written.
 */
bool convertImageToDxt5(const fs::path& inputPath, const fs::path& outputPath);

#endif // header guard

