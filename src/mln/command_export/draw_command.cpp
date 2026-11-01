#include <mln/command_export/draw_command.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace mln {
namespace command_export {
namespace {

constexpr size_t alignTo(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

struct PayloadLayout {
    size_t drawable;
    size_t props;
    size_t tile;
    size_t texture = 0;
    size_t stencil = 0;
    size_t target = 0;
    size_t camera = 0;
    size_t size;

    explicit PayloadLayout(const CommandPayloadHeader& header) {
        size_t cursor = sizeof(CommandPayloadHeader);
        drawable = alignTo(cursor, 4);
        cursor = drawable + header.drawableUBOSize;
        props = alignTo(cursor, 4);
        cursor = props + header.propsUBOSize;
        tile = alignTo(cursor, 4);
        cursor = tile + header.tilePropsUBOSize;
        if (header.sections & CommandPayloadSections::Texture) {
            texture = alignTo(cursor, 8);
            cursor = texture + sizeof(CommandTexture);
        }
        if (header.sections & CommandPayloadSections::Stencil) {
            stencil = alignTo(cursor, 4);
            cursor = stencil + sizeof(CommandStencil);
        }
        if (header.sections & CommandPayloadSections::RenderTarget) {
            target = alignTo(cursor, 4);
            cursor = target + sizeof(CommandRenderTarget);
        }
        if (header.sections & CommandPayloadSections::CameraDistance) {
            camera = alignTo(cursor, 4);
            cursor = camera + sizeof(float);
        }
        size = alignTo(cursor, 8);
    }
};

uint16_t uniformSize(std::span<const uint8_t> bytes) {
    if (bytes.size() > std::numeric_limits<uint16_t>::max()) {
        throw std::length_error("Command UBO exceeds the payload ABI limit");
    }
    return static_cast<uint16_t>(bytes.size());
}

struct PayloadSource {
    std::span<const uint8_t> bytes;
    std::optional<size_t> arenaOffset;

    PayloadSource(std::span<const uint8_t> bytes_, const std::vector<uint8_t>& arena)
        : bytes(bytes_) {
        if (bytes.empty() || arena.empty()) return;
        const auto address = reinterpret_cast<uintptr_t>(bytes.data());
        const auto begin = reinterpret_cast<uintptr_t>(arena.data());
        if (address >= begin && address - begin < arena.size()) {
            const auto offset = address - begin;
            if (bytes.size() > arena.size() - offset) {
                throw std::out_of_range("Command UBO exceeds its source arena");
            }
            arenaOffset = offset;
        }
    }

    const uint8_t* data(const std::vector<uint8_t>& arena) const {
        return arenaOffset ? arena.data() + *arenaOffset : bytes.data();
    }
};

} // namespace

CommandPayloadView::CommandPayloadView(std::span<const uint8_t> arena, const DrawCommand& command) {
    if (command.payloadSize == 0) {
        valid_ = command.payloadOffset == 0;
        return;
    }
    const size_t offset = command.payloadOffset;
    const size_t size = command.payloadSize;
    if (offset % 8 != 0 || size < sizeof(CommandPayloadHeader) || size % 8 != 0 ||
        offset > arena.size() || size > arena.size() - offset) {
        return;
    }
    const auto bytes = arena.subspan(offset, size);
    CommandPayloadHeader header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    if ((header.sections & ~CommandPayloadSections::All) != 0) return;
    const PayloadLayout layout(header);
    if (layout.size != size) return;

    payload.drawableUBO = bytes.subspan(layout.drawable, header.drawableUBOSize);
    payload.propsUBO = bytes.subspan(layout.props, header.propsUBOSize);
    payload.tilePropsUBO = bytes.subspan(layout.tile, header.tilePropsUBOSize);
    if (layout.texture) {
        CommandTexture value;
        std::memcpy(&value, bytes.data() + layout.texture, sizeof(value));
        payload.texture = value;
    }
    if (layout.stencil) {
        CommandStencil value;
        std::memcpy(&value, bytes.data() + layout.stencil, sizeof(value));
        payload.stencil = value;
    }
    if (layout.target) {
        CommandRenderTarget value;
        std::memcpy(&value, bytes.data() + layout.target, sizeof(value));
        payload.renderTarget = value;
    }
    if (layout.camera) {
        float value;
        std::memcpy(&value, bytes.data() + layout.camera, sizeof(value));
        payload.cameraDistance = value;
    }
    valid_ = true;
}

void FrameData::setPayload(DrawCommand& command, const DrawCommandPayload& data) {
    CommandPayloadHeader header{
        uniformSize(data.drawableUBO), uniformSize(data.propsUBO), uniformSize(data.tilePropsUBO), 0};
    if (data.texture) header.sections |= CommandPayloadSections::Texture;
    if (data.stencil) header.sections |= CommandPayloadSections::Stencil;
    if (data.renderTarget) header.sections |= CommandPayloadSections::RenderTarget;
    if (data.cameraDistance) header.sections |= CommandPayloadSections::CameraDistance;
    if (header.drawableUBOSize == 0 && header.propsUBOSize == 0 && header.tilePropsUBOSize == 0 && header.sections == 0) {
        command.payloadOffset = 0;
        command.payloadSize = 0;
        return;
    }

    const PayloadLayout layout(header);
    constexpr size_t maximumSize = std::numeric_limits<uint32_t>::max();
    if (payload.size() > maximumSize - 7) {
        throw std::length_error("Command payload arena exceeds the ABI limit");
    }
    const size_t offset = alignTo(payload.size(), 8);
    if (layout.size > maximumSize - offset) {
        throw std::length_error("Command payload arena exceeds the ABI limit");
    }
    const PayloadSource drawable(data.drawableUBO, payload);
    const PayloadSource props(data.propsUBO, payload);
    const PayloadSource tile(data.tilePropsUBO, payload);
    payload.resize(offset + layout.size, 0);
    auto* destination = payload.data() + offset;
    std::memcpy(destination, &header, sizeof(header));
    const auto copy = [&](size_t position, const PayloadSource& source) {
        if (!source.bytes.empty()) {
            std::memcpy(destination + position, source.data(payload), source.bytes.size());
        }
    };
    copy(layout.drawable, drawable);
    copy(layout.props, props);
    copy(layout.tile, tile);
    if (data.texture) std::memcpy(destination + layout.texture, &*data.texture, sizeof(CommandTexture));
    if (data.stencil) std::memcpy(destination + layout.stencil, &*data.stencil, sizeof(CommandStencil));
    if (data.renderTarget) std::memcpy(destination + layout.target, &*data.renderTarget, sizeof(CommandRenderTarget));
    if (data.cameraDistance) std::memcpy(destination + layout.camera, &*data.cameraDistance, sizeof(float));
    command.payloadOffset = static_cast<uint32_t>(offset);
    command.payloadSize = static_cast<uint32_t>(layout.size);
}

void FrameData::compactPayload() {
    std::vector<DrawCommand*> references;
    references.reserve(commands.size());
    for (auto& command : commands) {
        if (!CommandPayloadView(payload, command).valid()) {
            throw std::invalid_argument("Cannot compact an invalid command payload");
        }
        if (command.payloadSize != 0) references.push_back(&command);
    }
    std::sort(references.begin(), references.end(), [](const DrawCommand* left, const DrawCommand* right) {
        return left->payloadOffset < right->payloadOffset;
    });

    const DrawCommand* previous = nullptr;
    for (const auto* command : references) {
        if (previous) {
            const bool shared = command->payloadOffset == previous->payloadOffset;
            if ((shared && command->payloadSize != previous->payloadSize) ||
                (!shared && command->payloadOffset < size_t{previous->payloadOffset} + previous->payloadSize)) {
                throw std::invalid_argument("Command payload references overlap");
            }
        }
        previous = command;
    }

    size_t destination = 0;
    for (size_t index = 0; index < references.size();) {
        const uint32_t source = references[index]->payloadOffset;
        const uint32_t size = references[index]->payloadSize;
        if (source != destination) {
            std::memmove(payload.data() + destination, payload.data() + source, size);
        }
        do {
            references[index++]->payloadOffset = static_cast<uint32_t>(destination);
        } while (index < references.size() && references[index]->payloadOffset == source);
        destination += size;
    }
    payload.resize(destination);
}

static FrameData g_frameData;
static uint32_t g_currentLayerIndex = 0;

FrameData& getFrameData() {
    return g_frameData;
}

void setCurrentLayerIndex(uint32_t idx) {
    g_currentLayerIndex = idx;
}

uint32_t getCurrentLayerIndex() {
    return g_currentLayerIndex;
}

} // namespace command_export
} // namespace mln
