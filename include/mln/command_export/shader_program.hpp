#pragma once

#include <mln/shaders/shader_program_base.hpp>
#include <mln/gfx/vertex_attribute.hpp>

#include <string>

namespace mln {
namespace command_export {

/// Stub shader program for the Command Export backend.
/// No GPU compilation; the external consumer selects shaders.
class ShaderProgram final : public gfx::ShaderProgramBase {
public:
    ShaderProgram(std::string name)
        : shaderName(std::move(name)) {}

    ~ShaderProgram() override = default;

    const std::string_view typeName() const noexcept override { return shaderName; }

    std::optional<size_t> getSamplerLocation(const size_t) const override { return std::nullopt; }

    const gfx::VertexAttributeArray& getVertexAttributes() const override { return vertexAttributes; }

    const gfx::VertexAttributeArray& getInstanceAttributes() const override { return instanceAttributes; }

private:
    std::string shaderName;
    gfx::VertexAttributeArray vertexAttributes;
    gfx::VertexAttributeArray instanceAttributes;
};

} // namespace command_export
} // namespace mln
