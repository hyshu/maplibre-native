#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace mln {
namespace command_export {

/// Shader type enum — determines the vertex format and consumer pipeline.
enum class ShaderType : uint32_t {
    Fill = 0,
    FillOutline = 1,
    Line = 2,
    Background = 3,
    FillExtrusion = 4,
    LineSDF = 5,                  // dashed lines (line-dasharray), samples dash atlas
    LineGradient = 6,             // line-gradient, samples color ramp texture
    LinePattern = 7,              // line-pattern, samples tile icon atlas
    Circle = 8,                   // circle layer (POIs, dots)
    Raster = 9,                   // raster tiles (satellite imagery etc.), samples tile texture
    FillOutlineTriangulated = 10, // antialiased fill outline (LineLayoutVertex; optional paint ranges)
    ClippingMask = 11,            // tile clipping quad; writes only the stencil attachment
    BackgroundPattern = 12,       // repeating/crossfaded background pattern atlas
    Heatmap = 13,
    HeatmapTexture = 14,
    RenderTarget = 15,
    HillshadePrepare = 16,
    Hillshade = 17,
    // Future: Symbol, ...
    Unknown = 255
};

/// Primitive draw mode
enum class DrawModeType : uint32_t {
    Triangles = 0,
    Lines = 1,
    LineStrip = 2,
    Points = 3,
};

/// Texture sampler filter exported to the consumer. Values are part of the ABI.
enum class TextureFilterType : uint32_t {
    Nearest = 0,
    Linear = 1,
};

/// Resolved stencil behavior exported to the consumer. Values are part of the ABI.
/// The named modes correspond exactly to the modes produced by
/// PaintParameters::renderTileClippingMasks/stencilModeForClipping/
/// stencilModeFor3D. Clear is an ordered control command with no geometry.
enum class StencilModeType : uint32_t {
    Disabled = 0,
    ClippingMask = 1,  // compare Always, pass Replace, write mask 0xff
    ClippingTest = 2,  // compare Equal, pass Replace, write mask 0x00
    FillExtrusion = 3, // compare NotEqual, pass Replace, write mask 0xff
    Clear = 4,         // clear the stencil attachment to stencilReference
};

/// DrawCommand::flags bits. Values are part of the FFI ABI.
namespace DrawCommandFlags {
constexpr uint32_t CrossTileMerged = 1u << 0;
constexpr uint32_t FillExtrusionDataDriven = 1u << 1;
constexpr uint32_t FillColorDataDriven = 1u << 2;
constexpr uint32_t FillOpacityDataDriven = 1u << 3;
constexpr uint32_t FillExtrusionColorDataDriven = 1u << 4;
constexpr uint32_t CircleColorDataDriven = 1u << 5;
constexpr uint32_t CircleRadiusDataDriven = 1u << 6;
constexpr uint32_t CircleBlurDataDriven = 1u << 7;
constexpr uint32_t CircleOpacityDataDriven = 1u << 8;
constexpr uint32_t CircleStrokeColorDataDriven = 1u << 9;
constexpr uint32_t CircleStrokeWidthDataDriven = 1u << 10;
constexpr uint32_t CircleStrokeOpacityDataDriven = 1u << 11;
constexpr uint32_t LineColorDataDriven = 1u << 12;
constexpr uint32_t LineBlurDataDriven = 1u << 13;
constexpr uint32_t LineOpacityDataDriven = 1u << 14;
constexpr uint32_t LineGapWidthDataDriven = 1u << 15;
constexpr uint32_t LineOffsetDataDriven = 1u << 16;
constexpr uint32_t LineWidthDataDriven = 1u << 17;
constexpr uint32_t LineFloorWidthDataDriven = 1u << 18;
constexpr uint32_t LinePatternDataDriven = 1u << 19;
constexpr uint32_t FillOutlineColorDataDriven = 1u << 20;
constexpr uint32_t FillOutlineOpacityDataDriven = 1u << 21;
// Resolved at draw time through PaintParameters. Exporting the effective
// state (rather than Drawable's requested state) preserves opaquePassCutoff.
constexpr uint32_t DepthTest = 1u << 22;
constexpr uint32_t DepthWrite = 1u << 23;
constexpr uint32_t HeatmapWeightDataDriven = 1u << 26;
constexpr uint32_t HeatmapRadiusDataDriven = 1u << 27;
constexpr uint32_t RenderTargetRGBA8 = 1u << 28;
constexpr uint32_t HeatmapDataDrivenMask = HeatmapWeightDataDriven | HeatmapRadiusDataDriven;
constexpr uint32_t FillDataDrivenMask = FillColorDataDriven | FillOpacityDataDriven;
constexpr uint32_t FillOutlineDataDrivenMask = FillOutlineColorDataDriven | FillOutlineOpacityDataDriven;
constexpr uint32_t CircleDataDrivenMask = CircleColorDataDriven | CircleRadiusDataDriven | CircleBlurDataDriven |
                                          CircleOpacityDataDriven | CircleStrokeColorDataDriven |
                                          CircleStrokeWidthDataDriven | CircleStrokeOpacityDataDriven;
constexpr uint32_t LineDataDrivenMask = LineColorDataDriven | LineBlurDataDriven | LineOpacityDataDriven |
                                        LineGapWidthDataDriven | LineOffsetDataDriven | LineWidthDataDriven |
                                        LineFloorWidthDataDriven | LinePatternDataDriven;
static_assert(CircleDataDrivenMask == 0xFE0u);
static_assert(LineDataDrivenMask == 0xFF000u);
static_assert(FillOutlineDataDrivenMask == 0x300000u);
static_assert((DepthTest & DepthWrite) == 0u);
static_assert((DepthTest | DepthWrite) == 0xC00000u);
} // namespace DrawCommandFlags

/// A fixed-size command header with frame-relative references to optional data.
/// Geometry pointers retain their renderer or bridge ownership.
/// Vertex stride, payload offset, and payload size are measured in bytes.
struct DrawCommand {
    ShaderType shaderType;
    DrawModeType drawMode;
    const void* vertexData;
    uint32_t vertexCount;
    uint32_t vertexStride;
    const uint16_t* indexData;
    uint32_t indexCount;
    uint32_t flags;
    // Resource identities are independent of addresses. Versions track changed geometry.
    uint32_t bufferId;
    uint32_t bufferVersion;
    uint32_t layerIndex;
    int32_t subLayerIndex;
    uint32_t payloadOffset;
    uint32_t payloadSize;
};
static_assert(sizeof(DrawCommand) == 64, "DrawCommand size must be stable for FFI");
static_assert(static_cast<uint32_t>(ShaderType::ClippingMask) == 11);
static_assert(static_cast<uint32_t>(ShaderType::BackgroundPattern) == 12);
static_assert(static_cast<uint32_t>(ShaderType::Heatmap) == 13);
static_assert(static_cast<uint32_t>(ShaderType::HeatmapTexture) == 14);
static_assert(static_cast<uint32_t>(ShaderType::RenderTarget) == 15);
static_assert(static_cast<uint32_t>(ShaderType::HillshadePrepare) == 16);
static_assert(static_cast<uint32_t>(ShaderType::Hillshade) == 17);
static_assert(static_cast<uint32_t>(TextureFilterType::Nearest) == 0);
static_assert(static_cast<uint32_t>(TextureFilterType::Linear) == 1);
static_assert(static_cast<uint32_t>(StencilModeType::Disabled) == 0);
static_assert(static_cast<uint32_t>(StencilModeType::ClippingMask) == 1);
static_assert(static_cast<uint32_t>(StencilModeType::ClippingTest) == 2);
static_assert(static_cast<uint32_t>(StencilModeType::FillExtrusion) == 3);
static_assert(static_cast<uint32_t>(StencilModeType::Clear) == 4);

namespace CommandPayloadSections {
constexpr uint16_t Texture = 1u << 0;
constexpr uint16_t Stencil = 1u << 1;
constexpr uint16_t RenderTarget = 1u << 2;
constexpr uint16_t CameraDistance = 1u << 3;
constexpr uint16_t All = Texture | Stencil | RenderTarget | CameraDistance;
} // namespace CommandPayloadSections

/// Nonempty payloads start with this header and end on an eight-byte boundary.
/// UBOs follow in drawable, evaluated, and tile order at four-byte boundaries.
/// Optional texture, stencil, target, and camera sections follow in that order.
/// Textures align to eight bytes and other sections align to four bytes.
struct CommandPayloadHeader {
    uint16_t drawableUBOSize = 0;
    uint16_t propsUBOSize = 0;
    uint16_t tilePropsUBOSize = 0;
    uint16_t sections = 0;
};
static_assert(sizeof(CommandPayloadHeader) == 8, "CommandPayloadHeader size must be stable for FFI");

/// Texture pixels remain owned by the renderer for the frame lifetime.
struct CommandTexture {
    const void* data = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t id = 0;
    uint32_t version = 0;
    uint32_t channels = 0;
    TextureFilterType filter = TextureFilterType::Nearest;
};
static_assert(sizeof(CommandTexture) == 32, "CommandTexture size must be stable for FFI");

/// Omitted stencil data means disabled stencil testing.
struct CommandStencil {
    uint32_t reference = 0;
    StencilModeType mode = StencilModeType::Disabled;
};
static_assert(sizeof(CommandStencil) == 8, "CommandStencil size must be stable for FFI");

/// Omitted target data selects the main framebuffer.
/// RenderTargetRGBA8 on a clear command selects RGBA8 instead of RGBA16Float.
struct CommandRenderTarget {
    uint32_t id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};
static_assert(sizeof(CommandRenderTarget) == 12, "CommandRenderTarget size must be stable for FFI");

// The binding generator reads these compiler-verified field offsets.
#define COMMAND_EXPORT_ABI_OFFSET(S, field, off) \
    static_assert(offsetof(S, field) == (off), #S "::" #field " ABI offset changed")
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, shaderType, 0);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, drawMode, 4);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, vertexData, 8);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, vertexCount, 16);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, vertexStride, 20);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, indexData, 24);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, indexCount, 32);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, flags, 36);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, bufferId, 40);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, bufferVersion, 44);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, layerIndex, 48);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, subLayerIndex, 52);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, payloadOffset, 56);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, payloadSize, 60);
COMMAND_EXPORT_ABI_OFFSET(CommandPayloadHeader, drawableUBOSize, 0);
COMMAND_EXPORT_ABI_OFFSET(CommandPayloadHeader, propsUBOSize, 2);
COMMAND_EXPORT_ABI_OFFSET(CommandPayloadHeader, tilePropsUBOSize, 4);
COMMAND_EXPORT_ABI_OFFSET(CommandPayloadHeader, sections, 6);
COMMAND_EXPORT_ABI_OFFSET(CommandTexture, data, 0);
COMMAND_EXPORT_ABI_OFFSET(CommandTexture, width, 8);
COMMAND_EXPORT_ABI_OFFSET(CommandTexture, height, 12);
COMMAND_EXPORT_ABI_OFFSET(CommandTexture, id, 16);
COMMAND_EXPORT_ABI_OFFSET(CommandTexture, version, 20);
COMMAND_EXPORT_ABI_OFFSET(CommandTexture, channels, 24);
COMMAND_EXPORT_ABI_OFFSET(CommandTexture, filter, 28);
COMMAND_EXPORT_ABI_OFFSET(CommandStencil, reference, 0);
COMMAND_EXPORT_ABI_OFFSET(CommandStencil, mode, 4);
COMMAND_EXPORT_ABI_OFFSET(CommandRenderTarget, id, 0);
COMMAND_EXPORT_ABI_OFFSET(CommandRenderTarget, width, 4);
COMMAND_EXPORT_ABI_OFFSET(CommandRenderTarget, height, 8);

/// Borrowed UBO bytes and optional values for one command.
/// Spans into a frame arena remain valid only until it is appended, compacted, or released.
struct DrawCommandPayload {
    std::span<const uint8_t> drawableUBO;
    std::span<const uint8_t> propsUBO;
    std::span<const uint8_t> tilePropsUBO;
    std::optional<CommandTexture> texture;
    std::optional<CommandStencil> stencil;
    std::optional<CommandRenderTarget> renderTarget;
    std::optional<float> cameraDistance;
};

/// Validates a command's payload bounds and exposes its borrowed UBO spans.
class CommandPayloadView {
public:
    CommandPayloadView(std::span<const uint8_t> arena, const DrawCommand&);
    bool valid() const noexcept { return valid_; }
    /// Invalid records expose an empty payload.
    const DrawCommandPayload& get() const noexcept { return payload; }

private:
    DrawCommandPayload payload;
    bool valid_ = false;
};

/// Commands and their payload arena must be published and retained together.
struct FrameData {
    std::vector<DrawCommand> commands;
    std::vector<uint8_t> payload;
    std::optional<std::array<float, 4>> clearColor;

    void clear() {
        commands.clear();
        payload.clear();
        clearColor.reset();
    }

    /// Appends optional data and replaces the command's frame-relative reference.
    /// Sources may borrow this arena, including when appending reallocates it.
    /// Throws length_error if UBO lengths or the arena exceed their ABI limits.
    /// Throws out_of_range if a source extends beyond this arena's used bytes.
    void setPayload(DrawCommand&, const DrawCommandPayload&);

    /// Removes unreferenced payload blocks while preserving shared references.
    /// Invalidates borrowed spans and throws invalid_argument for malformed references.
    void compactPayload();

    /// Adds geometry whose storage remains owned by the renderer or bridge.
    DrawCommand& addCommand(ShaderType shader,
                            DrawModeType mode,
                            const void* vertices,
                            uint32_t vertexStride,
                            uint32_t vertexCount,
                            const uint16_t* indices,
                            uint32_t indexCount) {
        DrawCommand cmd{};
        cmd.shaderType = shader;
        cmd.drawMode = mode;
        cmd.vertexData = vertices;
        cmd.vertexStride = vertexStride;
        cmd.vertexCount = vertexCount;
        cmd.indexData = indices;
        cmd.indexCount = indexCount;
        commands.push_back(cmd);
        return commands.back();
    }
};

/// Global frame data — singleton, accessed from Drawable::draw() and FFI
FrameData& getFrameData();

/// Current layer index — set by LayerGroup::render(), read while exporting masks and drawables.
void setCurrentLayerIndex(uint32_t idx);
uint32_t getCurrentLayerIndex();

} // namespace command_export
} // namespace mln
