#pragma once

#include <mbgl/gfx/drawable.hpp>
#include <mbgl/renderer/query.hpp>
#include <mbgl/annotation/annotation.hpp>
#include <mbgl/style/types.hpp>
#include <mbgl/text/glyph.hpp>
#include <mbgl/util/geo.hpp>
#include <mbgl/util/geojson.hpp>
#include <mbgl/util/feature.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mbgl {

class RendererObserver;
class RenderedQueryOptions;
class SourceQueryOptions;
class UpdateParameters;

namespace style {
class LayerProperties;
} // namespace style

namespace gfx {
class RendererBackend;
} // namespace gfx

struct PlacedSymbolData {
    /// Contents of the label
    std::u16string key;
    /// Contents after MapLibre shaping has inserted balanced line breaks
    std::u16string lineBrokenText;
    /// Logical-order contents using the same balanced line breaks
    std::u16string logicalLineBrokenText;
    /// Resolved paragraph direction. Neutral paragraphs use left-to-right.
    bool textRTL = false;
    /// Stable identity assigned by CrossTileSymbolIndex
    uint32_t crossTileID = 0;
    /// If symbol contains text, text collision box in viewport coordinates
    std::optional<mapbox::geometry::box<float>> textCollisionBox;
    /// If symbol contains icon, icon collision box in viewport coordinates
    std::optional<mapbox::geometry::box<float>> iconCollisionBox;
    /// Symbol text was placed
    bool textPlaced;
    /// Symbol icon was placed
    bool iconPlaced;
    /// Symbol text or icon collision box intersects tile borders
    bool intersectsTileBorder;
    /// Viewport padding ({viewportPadding, viewportPadding} is a coordinate of the tile's top-left corner)
    float viewportPadding;
    /// Geographic symbol anchor projected into padded viewport coordinates
    Point<float> anchorPoint;
    /// Tile identity and coordinate retained for projected paint translation.
    int16_t tileWrap = 0;
    Point<float> tileAnchor;
    /// Geographic anchor captured by the placement transform.
    LatLng anchorLatLng;
    /// Layer id whose paint properties render this symbol.
    std::string layer;
    /// All paint layers sharing this symbol layout bucket.
    std::vector<std::string> layers;
    /// Style order of `layer` when placement ran.
    int32_t layerIndex = -1;
    /// Native paint group and back-to-front order within that group.
    /// Icons paint before text inside each group.
    uint32_t renderGroup = 0;
    uint32_t renderOrder = 0;
    /// Source identity needed to resolve current feature state.
    std::string sourceID;
    std::string sourceLayer;
    /// Feature snapshot used to evaluate data-driven paint properties.
    PropertyMap featureProperties;
    FeatureIdentifier featureID = NullValue{};
    FeatureType featureType = FeatureType::Unknown;
    uint8_t canonicalZ = 0;
    uint32_t canonicalX = 0;
    uint32_t canonicalY = 0;
    /// Evaluated icon-image ID (empty when the symbol has no icon)
    std::string icon;
    /// Feature- and zoom-evaluated text-size used for placement
    float textSize = 16;
    /// Feature- and zoom-evaluated icon-size used for placement
    float iconSize = 1;
    /// Label angle in radians (screen space, y-down). Non-zero only for
    /// line-placed symbols, derived from the collision-circle chain.
    float textAngle = 0;
    /// True when the symbol uses line placement (street names)
    bool alongLine = false;
    /// Projected collision-circle centers relative to `anchorPoint`.
    std::vector<Point<float>> textPath;
    float iconAngle = 0;
    bool iconAlongLine = false;
    std::vector<Point<float>> iconPath;
    /// Formatting ranges use UTF-16 offsets into visual-order `lineBrokenText`.
    std::vector<ShapingTextSection> visualTextSections;
    /// Formatting ranges use UTF-16 offsets into `logicalLineBrokenText`.
    std::vector<ShapingTextSection> textSections;
    /// Evaluated base font stack for unformatted text.
    FontStack textFontStack;
    float letterSpacing = 0;
    float lineHeight = 1.2f;
    float maxWidth = 10;
    /// Feature-evaluated layout values retained by the symbol bucket.
    float textRotation = 0;
    float iconRotation = 0;
    style::TextJustifyType textJustify = style::TextJustifyType::Center;
    style::AlignmentType textPitchAlignment = style::AlignmentType::Viewport;
    style::AlignmentType textRotationAlignment = style::AlignmentType::Viewport;
    style::AlignmentType iconPitchAlignment = style::AlignmentType::Viewport;
    style::AlignmentType iconRotationAlignment = style::AlignmentType::Viewport;
    bool textKeepUpright = true;
    bool iconKeepUpright = false;
    bool vertical = false;
    bool iconSDF = false;
    float iconFitWidth = 0;
    float iconFitHeight = 0;
    /// Final screen-space affine basis for point widgets. Explicit style
    /// rotation is already included. Values are [xx, xy, yx, yy].
    std::array<float, 4> textTransform{{1, 0, 0, 1}};
    std::array<float, 4> iconTransform{{1, 0, 0, 1}};
    /// Unpadded visual geometry used to center and size Widget children.
    Point<float> textVisualOffset;
    Point<float> iconVisualOffset;
    float textVisualWidth = 0;
    float textVisualHeight = 0;
    float iconVisualWidth = 0;
    float iconVisualHeight = 0;
};

class Renderer {
public:
    Renderer(gfx::RendererBackend&,
             float pixelRatio_,
             const std::optional<std::string>& localFontFamily = std::nullopt);
    ~Renderer();

    void markContextLost();

    void setObserver(RendererObserver*);

    void render(const std::shared_ptr<UpdateParameters>&);

    /// Feature queries
    std::vector<Feature> queryRenderedFeatures(const ScreenLineString&, const RenderedQueryOptions& options = {}) const;
    std::vector<Feature> queryRenderedFeatures(const ScreenCoordinate& point,
                                               const RenderedQueryOptions& options = {}) const;
    std::vector<Feature> queryRenderedFeatures(const ScreenBox& box, const RenderedQueryOptions& options = {}) const;
    std::vector<Feature> querySourceFeatures(const std::string& sourceID, const SourceQueryOptions& options = {}) const;
    AnnotationIDs queryPointAnnotations(const ScreenBox& box) const;
    AnnotationIDs queryShapeAnnotations(const ScreenBox& box) const;
    AnnotationIDs getAnnotationIDs(const std::vector<Feature>&) const;

    /// Feature extension query
    FeatureExtensionValue queryFeatureExtensions(
        const std::string& sourceID,
        const Feature& feature,
        const std::string& extension,
        const std::string& extensionField,
        const std::optional<std::map<std::string, Value>>& args = std::nullopt) const;

    void setFeatureState(const std::string& sourceID,
                         const std::optional<std::string>& sourceLayerID,
                         const std::string& featureID,
                         const FeatureState& state);

    void getFeatureState(FeatureState& state,
                         const std::string& sourceID,
                         const std::optional<std::string>& sourceLayerID,
                         const std::string& featureID) const;

    FeatureState getFeatureState(const std::string& sourceID,
                                 const std::optional<std::string>& sourceLayerID,
                                 const std::string& featureID) const;

    void removeFeatureState(const std::string& sourceID,
                            const std::optional<std::string>& sourceLayerID,
                            const std::optional<std::string>& featureID,
                            const std::optional<std::string>& stateKey);

    // Debug
    void dumpDebugLogs();

    /**
     * @brief In Tile map mode, enables or disables collecting of the placed
     * symbols data, which can be obtained with `getPlacedSymbolsData()`.
     *
     * The placed symbols data collecting is disabled by default.
     */
    void collectPlacedSymbolData(bool enable);

    /**
     * @brief If collecting of the placed symbols data is enabled, returns the
     * reference to the `PlacedSymbolData` vector holding the collected data.
     *
     * Note: the returned vector gets re-populated at every `render()` call.
     *
     * @return collected placed symbols data
     */
    const std::vector<PlacedSymbolData>& getPlacedSymbolsData() const;

    /**
     * Returns the paint properties evaluated for the latest rendered frame.
     *
     * The pointer remains valid until the next render or style update. A null
     * result means the layer is not present in the current render tree.
     */
    const style::LayerProperties* getEvaluatedLayerProperties(const std::string& layerID) const;

    // Memory
    void setTileCacheEnabled(bool);
    bool getTileCacheEnabled() const;
    void reduceMemoryUse();
    void clearData();

#if MLN_RENDER_BACKEND_OPENGL
    void enableAndroidEmulatorGoldfishMitigation(bool enable);
#endif

    /// Walk all drawables with exported data.
    using DrawableVisitor = std::function<void(const std::string& shaderName, const gfx::Drawable::ExportedData& data)>;
    void visitDrawables(const DrawableVisitor& visitor) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace mbgl
