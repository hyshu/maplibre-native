#include <mbgl/text/placement.hpp>

#include <mbgl/layout/symbol_layout.hpp>
#include <mbgl/renderer/bucket.hpp>
#include <mbgl/renderer/buckets/symbol_bucket.hpp>
#include <mbgl/renderer/render_layer.hpp>
#include <mbgl/renderer/render_tile.hpp>
#include <mbgl/renderer/update_parameters.hpp>
#include <mbgl/tile/geometry_tile.hpp>
#include <mbgl/util/instrumentation.hpp>
#include <mbgl/util/math.hpp>

#include <list>
#include <utility>

namespace mbgl {

OpacityState::OpacityState(bool placed_, bool skipFade)
    : opacity((skipFade && placed_) ? 1.0f : 0.0f),
      placed(placed_) {}

OpacityState::OpacityState(const OpacityState& prevState, float increment, bool placed_)
    : opacity(std::fmax(0.0f, std::fmin(1.0f, prevState.opacity + (prevState.placed ? increment : -increment)))),
      placed(placed_) {}

bool OpacityState::isHidden() const {
    return opacity == 0 && !placed;
}

JointOpacityState::JointOpacityState(bool placedText, bool placedIcon, bool skipFade)
    : icon(OpacityState(placedIcon, skipFade)),
      text(OpacityState(placedText, skipFade)) {}

JointOpacityState::JointOpacityState(const JointOpacityState& prevOpacityState,
                                     float increment,
                                     bool placedText,
                                     bool placedIcon)
    : icon(OpacityState(prevOpacityState.icon, increment, placedIcon)),
      text(OpacityState(prevOpacityState.text, increment, placedText)) {}

bool JointOpacityState::isHidden() const {
    return icon.isHidden() && text.isHidden();
}

const CollisionGroups::CollisionGroup& CollisionGroups::get(const std::string& sourceID) {
    // The predicate/groupID mechanism allows for arbitrary grouping,
    // but the current interface defines one source == one group when
    // crossSourceCollisions == true.
    if (!crossSourceCollisions) {
        if (!collisionGroups.contains(sourceID)) {
            uint16_t nextGroupID = ++maxGroupID;
            collisionGroups.emplace(
                sourceID,
                CollisionGroup(nextGroupID,
                               std::optional<Predicate>([nextGroupID](const RefIndexedSubfeature& feature) -> bool {
                                   return feature.getCollisionGroupId() == nextGroupID;
                               })));
        }
        return collisionGroups[sourceID];
    } else {
        static CollisionGroup nullGroup{0, std::nullopt};
        return nullGroup;
    }
}

using namespace style;

// PlacementContext implementation
class PlacementContext {
    std::reference_wrapper<const SymbolBucket> bucket;
    std::reference_wrapper<const RenderTile> renderTile;
    std::reference_wrapper<const TransformState> state;

public:
    PlacementContext(const SymbolBucket& bucket_,
                     const RenderTile& renderTile_,
                     const TransformState& state_,
                     float placementZoom,
                     CollisionGroups::CollisionGroup collisionGroup_,
                     std::optional<CollisionBoundaries> avoidEdges_ = std::nullopt)
        : bucket(bucket_),
          renderTile(renderTile_),
          state(state_),
          pixelsToTileUnits(renderTile_.id.pixelsToTileUnits(1, placementZoom)),
          scale(static_cast<float>(std::pow(2, placementZoom - getOverscaledID().overscaledZ))),
          pixelRatio(static_cast<float>(util::tileSize_D * getOverscaledID().overscaleFactor() / util::EXTENT)),
          collisionGroup(std::move(collisionGroup_)),
          partiallyEvaluatedTextSize(bucket_.textSizeBinder->evaluateForZoom(placementZoom)),
          partiallyEvaluatedIconSize(bucket_.iconSizeBinder->evaluateForZoom(placementZoom)),
          avoidEdges(std::move(avoidEdges_)) {}

    const SymbolBucket& getBucket() const { return bucket.get(); }
    const style::SymbolLayoutProperties::PossiblyEvaluated& getLayout() const { return *getBucket().layout; }
    const RenderTile& getRenderTile() const { return renderTile.get(); }

    const OverscaledTileID& getOverscaledID() const { return renderTile.get().getOverscaledTileID(); }

    const TransformState& getTransformState() const { return state; }

    float pixelsToTileUnits;
    float scale;
    float pixelRatio;

    bool rotateTextWithMap = getLayout().get<TextRotationAlignment>() == AlignmentType::Map;
    bool pitchTextWithMap = getLayout().get<TextPitchAlignment>() == AlignmentType::Map;
    bool rotateIconWithMap = getLayout().get<IconRotationAlignment>() == AlignmentType::Map;
    bool pitchIconWithMap = getLayout().get<IconPitchAlignment>() == AlignmentType::Map;
    SymbolPlacementType placementType = getLayout().get<SymbolPlacement>();

    mat4 textLabelPlaneMatrix = getLabelPlaneMatrix(
        renderTile.get().matrix, pitchTextWithMap, rotateTextWithMap, state, pixelsToTileUnits);
    mat4 iconLabelPlaneMatrix =
        (rotateTextWithMap == rotateIconWithMap && pitchTextWithMap == pitchIconWithMap)
            ? textLabelPlaneMatrix
            : getLabelPlaneMatrix(
                  renderTile.get().matrix, pitchIconWithMap, rotateIconWithMap, state, pixelsToTileUnits);

    CollisionGroups::CollisionGroup collisionGroup;
    ZoomEvaluatedSize partiallyEvaluatedTextSize;
    ZoomEvaluatedSize partiallyEvaluatedIconSize;

    bool textAllowOverlap = getLayout().get<style::TextAllowOverlap>();
    bool iconAllowOverlap = getLayout().get<style::IconAllowOverlap>();
    // This logic is similar to the "defaultOpacityState" logic below in
    // updateBucketOpacities If we know a symbol is always supposed to show,
    // force it to be marked visible even if it wasn't placed into the collision
    // index (because some or all of it was outside the range of the collision
    // grid). There is a subtle edge case here we're accepting:
    //  Symbol A has text-allow-overlap: true, icon-allow-overlap: true,
    //  icon-optional: false A's icon is outside the grid, so doesn't get placed
    //  A's text would be inside grid, but doesn't get placed because of
    //  icon-optional: false We still show A because of the allow-overlap
    //  settings. Symbol B has allow-overlap: false, and gets placed where A's
    //  text would be On panning in, there is a short period when Symbol B and
    //  Symbol A will overlap This is the reverse of our normal policy of "fade
    //  in on pan", but should look like any other collision and hopefully not
    //  be too noticeable.
    // See https://github.com/mapbox/mapbox-gl-native/issues/12683
    bool alwaysShowText = textAllowOverlap &&
                          (iconAllowOverlap || !(getBucket().hasIconData() || getBucket().hasSdfIconData()) ||
                           getLayout().get<style::IconOptional>());
    bool alwaysShowIcon = iconAllowOverlap &&
                          (textAllowOverlap || !getBucket().hasTextData() || getLayout().get<style::TextOptional>());

    bool hasIconTextFit = getLayout().get<IconTextFit>() != IconTextFitType::None;

    std::optional<CollisionBoundaries> avoidEdges;
};

// PlacementController implementation

PlacementController::PlacementController()
    : placement(makeMutable<Placement>()) {}

void PlacementController::setPlacement(Immutable<Placement> placement_) {
    placement = std::move(placement_);
    stale = false;
}

bool PlacementController::placementIsRecent(TimePoint now,
                                            const float zoom,
                                            std::optional<Duration> periodOverride) const {
    if (!placement->transitionsEnabled()) return false;

    auto updatePeriod = periodOverride ? *periodOverride : placement->getUpdatePeriod(zoom);

    return placement->getCommitTime() + updatePeriod > now;
}

bool PlacementController::hasTransitions(TimePoint now) const {
    if (!placement->transitionsEnabled()) return false;

    if (stale) return true;

    return placement->hasTransitions(now);
}

// Placement implementation

Placement::Placement(std::shared_ptr<const UpdateParameters> updateParameters_,
                     std::optional<Immutable<Placement>> prevPlacement_)
    : updateParameters(std::move(updateParameters_)),
      collisionIndex(updateParameters->transformState, updateParameters->mode),
      transitionOptions(updateParameters->transitionOptions),
      commitTime(updateParameters->timePoint),
      placementZoom(static_cast<float>(updateParameters->transformState.getZoom())),
      collisionGroups(updateParameters->crossSourceCollisions),
      prevPlacement(std::move(prevPlacement_)),
      showCollisionBoxes(updateParameters->debugOptions & MapDebugOptions::Collision) {
    if (prevPlacement) {
        prevPlacement->get()->prevPlacement = std::nullopt; // Only hold on to one placement back
    }
}

Placement::Placement()
    : collisionIndex({}, MapMode::Static),
      collisionGroups(true) {}

Placement::~Placement() = default;

namespace {

SymbolInstanceReferences getPaintOrderedSymbols(const BucketPlacementData& params, double bearing) {
    const auto& bucket = static_cast<const SymbolBucket&>(params.bucket.get());
    const bool canSortByViewportY = bucket.sortFeaturesByY && bucket.text.segments.size() <= 1 &&
                                    bucket.icon.segments.size() <= 1 && bucket.sdfIcon.segments.size() <= 1;
    return canSortByViewportY ? bucket.getSortedSymbols(static_cast<float>(bearing))
                              : bucket.getSymbols(params.sortKeyRange);
}

} // namespace

void Placement::placeLayers(const RenderLayerReferences& layers) {
    placedSymbolsData_.clear();
    for (auto it = layers.crbegin(); it != layers.crend(); ++it) {
        std::set<uint32_t> seenCrossTileIDs;
        placeLayer(*it, seenCrossTileIDs);
    }
    commit();
    if (placedSymbolDataCollected_) {
        const auto& state = collisionIndex.getTransformState();
        placedSymbolRefreshProjection_ = state.getProjectionMatrix();
        placedSymbolRefreshZoom_ = state.getZoom();
        placedSymbolRefreshSize_ = state.getSize();
    }
}

void Placement::placeLayer(const RenderLayer& layer, std::set<uint32_t>& seenCrossTileIDs) {
    symbolRenderOrders.clear();
    std::optional<float> previousSortKey;
    bool hasRenderGroup = false;
    uint32_t renderGroup = 0;
    uint32_t renderOrder = 0;
    const double bearing = collisionIndex.getTransformState().getBearing();
    for (const BucketPlacementData& data : layer.getPlacementData()) {
        const std::optional<float> sortKey = data.sortKeyRange ? std::optional<float>{data.sortKeyRange->sortKey}
                                                               : std::nullopt;
        if (!hasRenderGroup) {
            previousSortKey = sortKey;
            hasRenderGroup = true;
        } else if (sortKey != previousSortKey) {
            previousSortKey = sortKey;
            ++renderGroup;
            renderOrder = 0;
        }
        for (const SymbolInstance& symbol : getPaintOrderedSymbols(data, bearing)) {
            const uint64_t packedOrder = (static_cast<uint64_t>(renderGroup) << 32u) | renderOrder++;
            symbolRenderOrders.insert_or_assign(&symbol, packedOrder);
        }
    }

    for (const BucketPlacementData& data : layer.getPlacementData()) {
        Bucket& bucket = data.bucket;
        bucket.place(*this, data, seenCrossTileIDs);
    }
}

namespace {
Point<float> calculateVariableLayoutOffset(style::SymbolAnchorType anchor,
                                           float width,
                                           float height,
                                           std::array<float, 2> offset,
                                           float textBoxScale,
                                           bool rotateWithMap,
                                           bool pitchWithMap,
                                           float bearing) {
    AnchorAlignment alignment = AnchorAlignment::getAnchorAlignment(anchor);
    float shiftX = -(alignment.horizontalAlign - 0.5f) * width;
    float shiftY = -(alignment.verticalAlign - 0.5f) * height;
    Point<float> shift{shiftX + offset[0] * textBoxScale, shiftY + offset[1] * textBoxScale};
    if (rotateWithMap) {
        shift = util::rotate(shift, pitchWithMap ? bearing : -bearing);
    }
    return shift;
}
} // namespace

void Placement::placeSymbolBucket(const BucketPlacementData& params, std::set<uint32_t>& seenCrossTileIDs) {
    assert(updateParameters);
    const auto& symbolBucket = static_cast<const SymbolBucket&>(params.bucket.get());
    const RenderTile& renderTile = params.tile;
    PlacementContext ctx{symbolBucket,
                         params.tile,
                         collisionIndex.getTransformState(),
                         placementZoom,
                         collisionGroups.get(params.sourceId),
                         getAvoidEdges(symbolBucket, renderTile.matrix)};
    for (const SymbolInstance& symbol : getSortedSymbols(params, ctx.pixelRatio)) {
        if (!symbol.check(SYM_GUARD_LOC)) continue;
        if (seenCrossTileIDs.contains(symbol.getCrossTileID())) continue;
        placeSymbol(symbol, ctx);

        // Prevent a flickering issue while zooming out.
        if (symbol.getCrossTileID() != SymbolInstance::invalidCrossTileID && !ctx.getRenderTile().holdForFade()) {
            seenCrossTileIDs.insert(symbol.getCrossTileID());
        }
    }

    // Prevent a flickering issue when a symbol is moved.
    symbolBucket.justReloaded = false;

    // As long as this placement lives, we have to hold onto this bucket's
    // matching FeatureIndex/data for querying purposes
    retainedQueryData.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(symbolBucket.bucketInstanceId),
        std::forward_as_tuple(symbolBucket.bucketInstanceId, params.featureIndex, ctx.getOverscaledID()));
}

JointPlacement Placement::placeSymbol(const SymbolInstance& symbolInstance, const PlacementContext& ctx) {
    static const JointPlacement kUnplaced(false, false, false);
    const auto renderOrder = symbolRenderOrders.find(&symbolInstance);
    if (renderOrder == symbolRenderOrders.end()) {
        currentRenderGroup = std::numeric_limits<uint32_t>::max();
        currentRenderOrder = std::numeric_limits<uint32_t>::max();
    } else {
        currentRenderGroup = static_cast<uint32_t>(renderOrder->second >> 32u);
        currentRenderOrder = static_cast<uint32_t>(renderOrder->second);
    }
    if (!symbolInstance.check(SYM_GUARD_LOC)) return kUnplaced;
    if (symbolInstance.getCrossTileID() == SymbolInstance::invalidCrossTileID) return kUnplaced;

    if (ctx.getRenderTile().holdForFade()) {
        // Mark all symbols from this tile as "not placed", but don't add to
        // seenCrossTileIDs, because we don't know yet if we have a duplicate in
        // a parent tile that _should_ be placed.
        return kUnplaced;
    }
    const SymbolBucket& bucket = ctx.getBucket();
    const mat4& posMatrix = ctx.getRenderTile().matrix;
    const auto& collisionGroup = ctx.collisionGroup;
    auto variableTextAnchors = symbolInstance.getTextAnchors();
    textBoxes.clear();
    iconBoxes.clear();

    bool placeText = false;
    bool placeIcon = false;
    bool offscreen = true;
    float evaluatedTextSize = style::TextSize::defaultValue();
    float evaluatedIconSize = style::IconSize::defaultValue();
    std::pair<bool, bool> placed{false, false};
    std::pair<bool, bool> placedVerticalText{false, false};
    std::pair<bool, bool> placedVerticalIcon{false, false};
    Point<float> shift{0.0f, 0.0f};
    std::optional<size_t> horizontalTextIndex = symbolInstance.getDefaultHorizontalPlacedTextIndex();
    if (horizontalTextIndex) {
        const PlacedSymbol& placedSymbol = bucket.text.placedSymbols.at(*horizontalTextIndex);
        evaluatedTextSize = evaluateSizeForFeature(ctx.partiallyEvaluatedTextSize, placedSymbol);

        const auto updatePreviousOrientationIfNotPlaced = [&](bool isPlaced) {
            if (bucket.allowVerticalPlacement && !isPlaced && getPrevPlacement()) {
                auto prevOrientation = getPrevPlacement()->placedOrientations.find(symbolInstance.getCrossTileID());
                if (prevOrientation != getPrevPlacement()->placedOrientations.end()) {
                    placedOrientations[symbolInstance.getCrossTileID()] = prevOrientation->second;
                }
            }
        };

        const auto placeTextForPlacementModes = [&](auto& placeHorizontalFn, auto& placeVerticalFn) {
            if (bucket.allowVerticalPlacement && symbolInstance.getWritingModes() & WritingModeType::Vertical) {
                assert(!bucket.placementModes.empty());
                for (auto& placementMode : bucket.placementModes) {
                    if (placementMode == TextWritingModeType::Vertical) {
                        placedVerticalText = placed = placeVerticalFn();
                    } else {
                        placed = placeHorizontalFn();
                    }

                    if (placed.first) {
                        break;
                    }
                }
            } else {
                placed = placeHorizontalFn();
            }
        };

        // Line or point label placement
        if (variableTextAnchors.empty()) {
            const auto placeFeature = [&](const CollisionFeature& collisionFeature,
                                          style::TextWritingModeType orientation) {
                textBoxes.clear();
                auto placedFeature = collisionIndex.placeFeature(collisionFeature,
                                                                 {},
                                                                 posMatrix,
                                                                 ctx.textLabelPlaneMatrix,
                                                                 ctx.pixelRatio,
                                                                 placedSymbol,
                                                                 ctx.scale,
                                                                 evaluatedTextSize,
                                                                 ctx.textAllowOverlap,
                                                                 ctx.pitchTextWithMap,
                                                                 showCollisionBoxes,
                                                                 ctx.avoidEdges,
                                                                 collisionGroup.second,
                                                                 textBoxes);
                if (placedFeature.first) {
                    placedOrientations.emplace(symbolInstance.getCrossTileID(), orientation);
                }
                return placedFeature;
            };

            const auto placeHorizontal = [&] {
                return placeFeature(symbolInstance.getTextCollisionFeature(), style::TextWritingModeType::Horizontal);
            };

            const auto placeVertical = [&] {
                if (bucket.allowVerticalPlacement && symbolInstance.getVerticalTextCollisionFeature()) {
                    return placeFeature(*symbolInstance.getVerticalTextCollisionFeature(),
                                        style::TextWritingModeType::Vertical);
                }
                return std::pair<bool, bool>{false, false};
            };

            placeTextForPlacementModes(placeHorizontal, placeVertical);
            updatePreviousOrientationIfNotPlaced(placed.first);

            placeText = placed.first;
            offscreen &= placed.second;
        } else if (!symbolInstance.getTextCollisionFeature().alongLine &&
                   !symbolInstance.getTextCollisionFeature().boxes.empty()) {
            // If this symbol was in the last placement, shift the previously
            // used anchor to the front of the anchor list, only if the previous
            // anchor is still in the anchor list.
            if (getPrevPlacement()) {
                auto prevOffset = getPrevPlacement()->variableOffsets.find(symbolInstance.getCrossTileID());
                if (prevOffset != getPrevPlacement()->variableOffsets.end()) {
                    const auto prevAnchor = prevOffset->second.anchor;
                    auto found = std::find(variableTextAnchors.begin(), variableTextAnchors.end(), prevAnchor);
                    if (found != variableTextAnchors.begin() && found != variableTextAnchors.end()) {
                        std::vector<style::TextVariableAnchorType> filtered{prevAnchor};
                        if (!isTiltedView()) {
                            for (auto anchor : variableTextAnchors) {
                                if (anchor != prevAnchor) {
                                    filtered.push_back(anchor);
                                }
                            }
                        }
                        variableTextAnchors = std::move(filtered);
                    }
                }
            }

            const bool doVariableIconPlacement = ctx.hasIconTextFit && !ctx.iconAllowOverlap &&
                                                 symbolInstance.getPlacedIconIndex();
            const auto placeFeatureForVariableAnchors = [&](const CollisionFeature& textCollisionFeature,
                                                            style::TextWritingModeType orientation,
                                                            const CollisionFeature& iconCollisionFeature) {
                const CollisionBox& textBox = textCollisionFeature.boxes[0];
                const float width = textBox.x2 - textBox.x1;
                const float height = textBox.y2 - textBox.y1;
                const float textBoxScale = symbolInstance.getTextBoxScale();
                std::pair<bool, bool> placedFeature = {false, false};
                const size_t anchorsSize = variableTextAnchors.size();
                const size_t placementAttempts = ctx.textAllowOverlap ? anchorsSize * 2 : anchorsSize;
                for (size_t i = 0u; i < placementAttempts; ++i) {
                    // when anchorsSize is 0, placementAttempts is also 0,
                    // so this code would not be reached
                    // NOLINTNEXTLINE(clang-analyzer-core.DivideZero)
                    auto anchor = variableTextAnchors[i % anchorsSize];
                    auto variableTextOffset = symbolInstance.getTextVariableAnchorOffset()->getOffsetByAnchor(anchor);
                    const bool allowOverlap = (i >= anchorsSize);
                    shift = calculateVariableLayoutOffset(anchor,
                                                          width,
                                                          height,
                                                          variableTextOffset,
                                                          textBoxScale,
                                                          ctx.rotateTextWithMap,
                                                          ctx.pitchTextWithMap,
                                                          static_cast<float>(ctx.getTransformState().getBearing()));
                    textBoxes.clear();
                    if (!canPlaceAtVariableAnchor(
                            textBox, anchor, shift, variableTextAnchors, posMatrix, ctx.pixelRatio)) {
                        continue;
                    }

                    placedFeature = collisionIndex.placeFeature(textCollisionFeature,
                                                                shift,
                                                                posMatrix,
                                                                mat4(),
                                                                ctx.pixelRatio,
                                                                placedSymbol,
                                                                ctx.scale,
                                                                evaluatedTextSize,
                                                                allowOverlap,
                                                                ctx.pitchTextWithMap,
                                                                showCollisionBoxes,
                                                                ctx.avoidEdges,
                                                                collisionGroup.second,
                                                                textBoxes);

                    if (doVariableIconPlacement) {
                        auto placedIconFeature = collisionIndex.placeFeature(
                            iconCollisionFeature,
                            shift,
                            posMatrix,
                            ctx.iconLabelPlaneMatrix,
                            ctx.pixelRatio,
                            placedSymbol,
                            ctx.scale,
                            evaluatedTextSize,
                            ctx.iconAllowOverlap,
                            ctx.pitchTextWithMap, // TODO: shall it be pitchIconWithMap?
                            showCollisionBoxes,
                            ctx.avoidEdges,
                            collisionGroup.second,
                            iconBoxes);
                        iconBoxes.clear();
                        if (!placedIconFeature.first) continue;
                    }

                    if (placedFeature.first) {
                        assert(symbolInstance.getCrossTileID() != 0u);
                        std::optional<style::TextVariableAnchorType> prevAnchor;

                        // If this label was placed in the previous
                        // placement, record the anchor position to allow us
                        // to animate the transition
                        if (getPrevPlacement()) {
                            auto prevOffset = getPrevPlacement()->variableOffsets.find(symbolInstance.getCrossTileID());
                            auto prevPlacements = getPrevPlacement()->placements.find(symbolInstance.getCrossTileID());
                            if (prevOffset != getPrevPlacement()->variableOffsets.end() &&
                                prevPlacements != getPrevPlacement()->placements.end() && prevPlacements->second.text) {
                                // TODO: The prevAnchor seems to be unused, needs to be fixed.
                                prevAnchor = prevOffset->second.anchor;
                            }
                        }

                        variableOffsets.insert(std::make_pair(symbolInstance.getCrossTileID(),
                                                              VariableOffset{.offset = variableTextOffset,
                                                                             .width = width,
                                                                             .height = height,
                                                                             .anchor = anchor,
                                                                             .textBoxScale = textBoxScale,
                                                                             .prevAnchor = prevAnchor}));

                        if (bucket.allowVerticalPlacement) {
                            placedOrientations.emplace(symbolInstance.getCrossTileID(), orientation);
                        }
                        break;
                    }
                }

                return placedFeature;
            };

            const auto placeHorizontal = [&] {
                return placeFeatureForVariableAnchors(symbolInstance.getTextCollisionFeature(),
                                                      style::TextWritingModeType::Horizontal,
                                                      symbolInstance.getIconCollisionFeature());
            };

            const auto placeVertical = [&] {
                if (bucket.allowVerticalPlacement && !placed.first &&
                    symbolInstance.getVerticalTextCollisionFeature()) {
                    return placeFeatureForVariableAnchors(*symbolInstance.getVerticalTextCollisionFeature(),
                                                          style::TextWritingModeType::Vertical,
                                                          symbolInstance.getVerticalTextCollisionFeature()
                                                              ? *symbolInstance.getVerticalTextCollisionFeature()
                                                              : symbolInstance.getIconCollisionFeature());
                }
                return std::pair<bool, bool>{false, false};
            };

            placeTextForPlacementModes(placeHorizontal, placeVertical);

            placeText = placed.first;
            offscreen &= placed.second;

            updatePreviousOrientationIfNotPlaced(placed.first);

            // If we didn't get placed, we still need to copy our position from
            // the last placement for fade animations
            if (!placeText && getPrevPlacement()) {
                auto prevOffset = getPrevPlacement()->variableOffsets.find(symbolInstance.getCrossTileID());
                if (prevOffset != getPrevPlacement()->variableOffsets.end()) {
                    variableOffsets[symbolInstance.getCrossTileID()] = prevOffset->second;
                }
            }
        }
    }

    if (symbolInstance.getPlacedIconIndex()) {
        if (!ctx.hasIconTextFit || !placeText || variableTextAnchors.empty()) {
            shift = {0.0f, 0.0f};
        }

        const auto& iconBuffer = symbolInstance.hasSdfIcon() ? bucket.sdfIcon : bucket.icon;
        const PlacedSymbol& placedSymbol = iconBuffer.placedSymbols.at(*symbolInstance.getPlacedIconIndex());
        evaluatedIconSize = evaluateSizeForFeature(ctx.partiallyEvaluatedIconSize, placedSymbol);
        const auto& placeIconFeature = [&](const CollisionFeature& collisionFeature) {
            return collisionIndex.placeFeature(collisionFeature,
                                               shift,
                                               posMatrix,
                                               ctx.iconLabelPlaneMatrix,
                                               ctx.pixelRatio,
                                               placedSymbol,
                                               ctx.scale,
                                               evaluatedIconSize,
                                               ctx.iconAllowOverlap,
                                               ctx.pitchTextWithMap,
                                               showCollisionBoxes,
                                               ctx.avoidEdges,
                                               collisionGroup.second,
                                               iconBoxes);
        };

        std::pair<bool, bool> placedIcon;
        if (placedVerticalText.first && symbolInstance.getVerticalIconCollisionFeature()) {
            placedIcon = placedVerticalIcon = placeIconFeature(*symbolInstance.getVerticalIconCollisionFeature());
        } else {
            placedIcon = placeIconFeature(symbolInstance.getIconCollisionFeature());
        }
        placeIcon = placedIcon.first;
        offscreen &= placedIcon.second;
    }

    const bool iconWithoutText = !symbolInstance.hasText() || ctx.getLayout().get<TextOptional>();
    const bool textWithoutIcon = !symbolInstance.hasIcon() || ctx.getLayout().get<IconOptional>();

    // combine placements for icon and text
    if (!iconWithoutText && !textWithoutIcon) {
        placeText = placeIcon = placeText && placeIcon;
    } else if (!textWithoutIcon) {
        placeText = placeText && placeIcon;
    } else if (!iconWithoutText) {
        placeIcon = placeText && placeIcon;
    }

    if (placeText) {
        if (placedVerticalText.first && symbolInstance.getVerticalTextCollisionFeature()) {
            collisionIndex.insertFeature(*symbolInstance.getVerticalTextCollisionFeature(),
                                         textBoxes,
                                         ctx.getLayout().get<TextIgnorePlacement>(),
                                         bucket.bucketInstanceId,
                                         collisionGroup.first);
        } else {
            collisionIndex.insertFeature(symbolInstance.getTextCollisionFeature(),
                                         textBoxes,
                                         ctx.getLayout().get<TextIgnorePlacement>(),
                                         bucket.bucketInstanceId,
                                         collisionGroup.first);
        }
    }

    if (placeIcon) {
        if (placedVerticalIcon.first && symbolInstance.getVerticalIconCollisionFeature()) {
            collisionIndex.insertFeature(*symbolInstance.getVerticalIconCollisionFeature(),
                                         iconBoxes,
                                         ctx.getLayout().get<IconIgnorePlacement>(),
                                         bucket.bucketInstanceId,
                                         collisionGroup.first);
        } else {
            collisionIndex.insertFeature(symbolInstance.getIconCollisionFeature(),
                                         iconBoxes,
                                         ctx.getLayout().get<IconIgnorePlacement>(),
                                         bucket.bucketInstanceId,
                                         collisionGroup.first);
        }
    }

    const bool hasIconCollisionCircleData = bucket.hasIconCollisionCircleData();
    const bool hasTextCollisionCircleData = bucket.hasTextCollisionCircleData();

    if (hasIconCollisionCircleData && symbolInstance.getIconCollisionFeature().alongLine && !iconBoxes.empty()) {
        collisionCircles[&symbolInstance.getIconCollisionFeature()] = iconBoxes;
    }
    if (hasTextCollisionCircleData && symbolInstance.getTextCollisionFeature().alongLine && !textBoxes.empty()) {
        collisionCircles[&symbolInstance.getTextCollisionFeature()] = textBoxes;
    }

    if (!symbolInstance.check(SYM_GUARD_LOC)) {
        return kUnplaced;
    }

    if (symbolInstance.getCrossTileID() != 0) {
        const auto hit = placements.find(symbolInstance.getCrossTileID());
        if (hit != placements.end()) {
            // If there's a previous placement with this ID, it comes from a tile that's fading out
            // Erase it so that the placement result from the non-fading tile supersedes it
            placements.erase(hit);
        }
    } else {
        assert(false);
        // We skipped some setup, don't use this one or we might run into inconsistencies
        symbolInstance.forceFail();
        return kUnplaced;
    }

    JointPlacement result(
        placeText || ctx.alwaysShowText, placeIcon || ctx.alwaysShowIcon, offscreen || bucket.justReloaded);
    placements.emplace(symbolInstance.getCrossTileID(), result);
    newSymbolPlaced(
        symbolInstance, ctx, result, ctx.placementType, evaluatedTextSize, evaluatedIconSize, textBoxes, iconBoxes);
    return result;
}

namespace {

SymbolInstanceReferences getBucketSymbols(const SymbolBucket& bucket,
                                          const std::optional<SortKeyRange>& sortKeyRange,
                                          double bearing) {
    if (bucket.layout->get<style::SymbolZOrder>() == style::SymbolZOrderType::ViewportY) {
        auto sortedSymbols = bucket.getSortedSymbols(static_cast<float>(bearing));
        // Place in the reverse order than draw i.e., starting from the foreground elements.
        std::reverse(std::begin(sortedSymbols), std::end(sortedSymbols));
        return sortedSymbols;
    }
    return bucket.getSymbols(sortKeyRange);
}

} // namespace

SymbolInstanceReferences Placement::getSortedSymbols(const BucketPlacementData& params, float) {
    const auto& bucket = static_cast<const SymbolBucket&>(params.bucket.get());
    SymbolInstanceReferences sortedSymbols = getBucketSymbols(
        bucket, params.sortKeyRange, collisionIndex.getTransformState().getBearing());
    auto* previousPlacement = getPrevPlacement();
    if (previousPlacement && isTiltedView()) {
        std::stable_sort(sortedSymbols.begin(),
                         sortedSymbols.end(),
                         [previousPlacement](const SymbolInstance& a, const SymbolInstance& b) noexcept {
                             auto* aPlacement = previousPlacement->getSymbolPlacement(a);
                             auto* bPlacement = previousPlacement->getSymbolPlacement(b);
                             if (!aPlacement) {
                                 // a < b, if 'a' is new and if 'b' was previously hidden.
                                 return bPlacement && !bPlacement->placed();
                             }
                             if (!bPlacement) {
                                 // a < b, if 'b' is new and 'a' was previously shown.
                                 return aPlacement->placed();
                             }
                             // a < b, if 'a' was shown and 'b' was hidden.
                             return aPlacement->placed() && !bPlacement->placed();
                         });
    }
    return sortedSymbols;
}

void Placement::commit() {
    bool placementChanged = false;
    if (!getPrevPlacement()) {
        assert(false);
        return;
    }
    prevZoomAdjustment = getPrevPlacement()->zoomAdjustment(placementZoom);
    const float increment = getPrevPlacement()->symbolFadeChange(commitTime);

    // add the opacities from the current placement, and copy their current
    // values from the previous placement
    for (auto& jointPlacement : placements) {
        auto prevOpacity = getPrevPlacement()->opacities.find(jointPlacement.first);
        if (prevOpacity != getPrevPlacement()->opacities.end()) {
            opacities.emplace(
                jointPlacement.first,
                JointOpacityState(
                    prevOpacity->second, increment, jointPlacement.second.text, jointPlacement.second.icon));
            placementChanged = placementChanged || jointPlacement.second.icon != prevOpacity->second.icon.placed ||
                               jointPlacement.second.text != prevOpacity->second.text.placed;
        } else {
            opacities.emplace(
                jointPlacement.first,
                JointOpacityState(
                    jointPlacement.second.text, jointPlacement.second.icon, jointPlacement.second.skipFade));
            placementChanged = placementChanged || jointPlacement.second.icon || jointPlacement.second.text;
        }
    }

    // copy and update values from the previous placement that aren't in the
    // current placement but haven't finished fading
    for (auto& prevOpacity : getPrevPlacement()->opacities) {
        if (!opacities.contains(prevOpacity.first)) {
            JointOpacityState jointOpacity(prevOpacity.second, increment, false, false);
            if (!jointOpacity.isHidden()) {
                opacities.emplace(prevOpacity.first, jointOpacity);
                placementChanged = placementChanged || prevOpacity.second.icon.placed || prevOpacity.second.text.placed;
            }
        }
    }

    for (auto& prevOffset : getPrevPlacement()->variableOffsets) {
        const uint32_t crossTileID = prevOffset.first;
        auto foundOffset = variableOffsets.find(crossTileID);
        auto foundOpacity = opacities.find(crossTileID);
        if (foundOffset == variableOffsets.end() && foundOpacity != opacities.end() &&
            !foundOpacity->second.isHidden()) {
            variableOffsets[prevOffset.first] = prevOffset.second;
        }
    }

    for (auto& prevOrientation : getPrevPlacement()->placedOrientations) {
        const uint32_t crossTileID = prevOrientation.first;
        auto foundOrientation = placedOrientations.find(crossTileID);
        auto foundOpacity = opacities.find(crossTileID);
        if (foundOrientation == placedOrientations.end() && foundOpacity != opacities.end() &&
            !foundOpacity->second.isHidden()) {
            placedOrientations[prevOrientation.first] = prevOrientation.second;
        }
    }

    fadeStartTime = placementChanged ? commitTime
                                     : (getPrevPlacement() ? getPrevPlacement()->fadeStartTime : TimePoint{});
}

void Placement::updateLayerBuckets(const RenderLayer& layer, const TransformState& state, bool updateOpacities) const {
    std::set<uint32_t> seenCrossTileIDs;
    for (const auto& item : layer.getPlacementData()) {
        if (!item.sortKeyRange || item.sortKeyRange->isFirstRange()) {
            item.bucket.get().updateVertices(*this, updateOpacities, state, item.tile, seenCrossTileIDs);
        }
    }
}

namespace {
Point<float> calculateVariableRenderShift(style::SymbolAnchorType anchor,
                                          float width,
                                          float height,
                                          std::array<float, 2> textOffset,
                                          float textBoxScale,
                                          float renderTextSize) {
    const AnchorAlignment alignment = AnchorAlignment::getAnchorAlignment(anchor);
    const float shiftX = -(alignment.horizontalAlign - 0.5f) * width;
    const float shiftY = -(alignment.verticalAlign - 0.5f) * height;
    return {(shiftX / textBoxScale + textOffset[0]) * renderTextSize,
            (shiftY / textBoxScale + textOffset[1]) * renderTextSize};
}
} // namespace

bool Placement::updateBucketDynamicVertices(SymbolBucket& bucket,
                                            const TransformState& state,
                                            const RenderTile& tile) const {
    using namespace style;
    const auto& layout = *bucket.layout;
    const bool alongLine = layout.get<SymbolPlacement>() != SymbolPlacementType::Point;
    const bool hasVariableAnchors = bucket.hasVariableTextAnchors() && bucket.hasTextData();
    const bool updateTextFitIcon = layout.get<IconTextFit>() != IconTextFitType::None &&
                                   (bucket.allowVerticalPlacement || hasVariableAnchors) &&
                                   (bucket.hasIconData() || bucket.hasSdfIconData());
    bool result = false;

    if (alongLine) {
        if (layout.get<IconRotationAlignment>() == AlignmentType::Map) {
            const bool pitchWithMap = layout.get<style::IconPitchAlignment>() == style::AlignmentType::Map;
            const bool keepUpright = layout.get<style::IconKeepUpright>();
            if (bucket.hasSdfIconData()) {
                reprojectLineLabels(bucket.sdfIcon.dynamicVertices(),
                                    bucket.sdfIcon.placedSymbols,
                                    tile.matrix,
                                    pitchWithMap,
                                    true /*rotateWithMap*/,
                                    keepUpright,
                                    tile,
                                    *bucket.iconSizeBinder,
                                    state);
                result = true;
            }
            if (bucket.hasIconData()) {
                reprojectLineLabels(bucket.icon.dynamicVertices(),
                                    bucket.icon.placedSymbols,
                                    tile.matrix,
                                    pitchWithMap,
                                    true /*rotateWithMap*/,
                                    keepUpright,
                                    tile,
                                    *bucket.iconSizeBinder,
                                    state);
                result = true;
            }
        }

        if (bucket.hasTextData() && layout.get<TextRotationAlignment>() == AlignmentType::Map) {
            const bool pitchWithMap = layout.get<style::TextPitchAlignment>() == style::AlignmentType::Map;
            const bool keepUpright = layout.get<style::TextKeepUpright>();
            reprojectLineLabels(bucket.text.dynamicVertices(),
                                bucket.text.placedSymbols,
                                tile.matrix,
                                pitchWithMap,
                                true /*rotateWithMap*/,
                                keepUpright,
                                tile,
                                *bucket.textSizeBinder,
                                state);
            result = true;
        }
    } else if (hasVariableAnchors) {
        bucket.text.sharedDynamicVertices->clear();
        bucket.hasVariablePlacement = false;

        const auto partiallyEvaluatedSize = bucket.textSizeBinder->evaluateForZoom(static_cast<float>(state.getZoom()));
        const auto tileScale = static_cast<float>(
            std::pow(2, state.getZoom() - tile.getOverscaledTileID().overscaledZ));
        const bool rotateWithMap = layout.get<TextRotationAlignment>() == AlignmentType::Map;
        const bool pitchWithMap = layout.get<TextPitchAlignment>() == AlignmentType::Map;
        const float pixelsToTileUnits = tile.id.pixelsToTileUnits(1.0f, static_cast<float>(state.getZoom()));
        const auto labelPlaneMatrix = getLabelPlaneMatrix(
            tile.matrix, pitchWithMap, rotateWithMap, state, pixelsToTileUnits);
        std::unordered_map<std::size_t, std::pair<std::size_t, Point<float>>> placedTextShifts;

        for (std::size_t i = 0; i < bucket.text.placedSymbols.size(); ++i) {
            const PlacedSymbol& symbol = bucket.text.placedSymbols[i];
            std::optional<VariableOffset> variableOffset;
            const bool skipOrientation = bucket.allowVerticalPlacement && !symbol.placedOrientation;
            if (!symbol.hidden && symbol.crossTileID != 0u && !skipOrientation) {
                auto it = variableOffsets.find(symbol.crossTileID);
                if (it != variableOffsets.end()) {
                    bucket.hasVariablePlacement = true;
                    variableOffset = it->second;
                }
            }

            if (!variableOffset) {
                // These symbols are from a justification that is not being
                // used, or a label that wasn't placed so we don't need to do
                // the extra math to figure out what incremental shift to apply.
                hideGlyphs(symbol.glyphOffsets.size(), bucket.text.dynamicVertices());
            } else {
                const Point<float> tileAnchor = symbol.anchorPoint;
                const auto projectedAnchor = project(tileAnchor, pitchWithMap ? tile.matrix : labelPlaneMatrix);
                const float perspectiveRatio = 0.5f +
                                               0.5f * (state.getCameraToCenterDistance() / projectedAnchor.second);
                float renderTextSize = evaluateSizeForFeature(partiallyEvaluatedSize, symbol) * perspectiveRatio /
                                       util::ONE_EM;
                if (pitchWithMap) {
                    // Go from size in pixels to equivalent size in tile units
                    renderTextSize *= bucket.tilePixelRatio / tileScale;
                }

                auto shift = calculateVariableRenderShift((*variableOffset).anchor,
                                                          (*variableOffset).width,
                                                          (*variableOffset).height,
                                                          (*variableOffset).offset,
                                                          (*variableOffset).textBoxScale,
                                                          renderTextSize);

                // Usual case is that we take the projected anchor and add the
                // pixel-based shift calculated above. In the (somewhat weird)
                // case of pitch-aligned text, we add an equivalent tile-unit
                // based shift to the anchor before projecting to the label
                // plane.
                Point<float> shiftedAnchor;
                if (pitchWithMap) {
                    shiftedAnchor =
                        project(Point<float>(tileAnchor.x + shift.x, tileAnchor.y + shift.y), labelPlaneMatrix).first;
                } else if (rotateWithMap) {
                    auto rotated = util::rotate(shift, -state.getPitch());
                    shiftedAnchor = Point<float>(projectedAnchor.first.x + rotated.x,
                                                 projectedAnchor.first.y + rotated.y);
                } else {
                    shiftedAnchor = Point<float>(projectedAnchor.first.x + shift.x, projectedAnchor.first.y + shift.y);
                }

                if (updateTextFitIcon && symbol.placedIconIndex) {
                    placedTextShifts.emplace(*symbol.placedIconIndex,
                                             std::pair<std::size_t, Point<float>>{i, shiftedAnchor});
                }

                for (std::size_t j = 0; j < symbol.glyphOffsets.size(); ++j) {
                    addDynamicAttributes(shiftedAnchor, symbol.angle, bucket.text.dynamicVertices());
                }
            }
        }

        if (updateTextFitIcon && bucket.hasVariablePlacement) {
            auto updateIcon = [&](SymbolBucket::Buffer& iconBuffer) {
                iconBuffer.sharedDynamicVertices->clear();
                for (std::size_t i = 0; i < iconBuffer.placedSymbols.size(); ++i) {
                    const PlacedSymbol& placedIcon = iconBuffer.placedSymbols[i];
                    if (placedIcon.hidden || (!placedIcon.placedOrientation && bucket.allowVerticalPlacement)) {
                        hideGlyphs(placedIcon.glyphOffsets.size(), iconBuffer.dynamicVertices());
                    } else {
                        const auto& pair = placedTextShifts.find(i);
                        if (pair == placedTextShifts.end()) {
                            hideGlyphs(placedIcon.glyphOffsets.size(), iconBuffer.dynamicVertices());
                        } else {
                            for (std::size_t j = 0; j < placedIcon.glyphOffsets.size(); ++j) {
                                addDynamicAttributes(
                                    pair->second.second, placedIcon.angle, iconBuffer.dynamicVertices());
                            }
                        }
                    }
                }
            };
            updateIcon(bucket.icon);
            updateIcon(bucket.sdfIcon);
        }

        result = true;
    } else if (bucket.allowVerticalPlacement && bucket.hasTextData()) {
        const auto updateDynamicVertices = [](SymbolBucket::Buffer& buffer) {
            buffer.sharedDynamicVertices->clear();
            for (const PlacedSymbol& symbol : buffer.placedSymbols) {
                if (symbol.hidden || !symbol.placedOrientation) {
                    hideGlyphs(symbol.glyphOffsets.size(), buffer.dynamicVertices());
                } else {
                    for (std::size_t j = 0; j < symbol.glyphOffsets.size(); ++j) {
                        addDynamicAttributes(symbol.anchorPoint, symbol.angle, buffer.dynamicVertices());
                    }
                }
            }
        };

        updateDynamicVertices(bucket.text);
        // When text box is rotated, icon-text-fit icon must be rotated as well.
        if (updateTextFitIcon) {
            updateDynamicVertices(bucket.icon);
            updateDynamicVertices(bucket.sdfIcon);
        }

        result = true;
    }

    return result;
}

void Placement::updateBucketOpacities(SymbolBucket& bucket,
                                      const TransformState& state,
                                      std::set<uint32_t>& seenCrossTileIDs) const {
    if (bucket.hasTextData()) bucket.text.sharedOpacityVertices->clear();
    if (bucket.hasIconData()) bucket.icon.sharedOpacityVertices->clear();
    if (bucket.hasSdfIconData()) bucket.sdfIcon.sharedOpacityVertices->clear();
    if (bucket.hasIconCollisionBoxData()) bucket.iconCollisionBox->dynamicVertices().clear();
    if (bucket.hasIconCollisionCircleData()) bucket.iconCollisionCircle->dynamicVertices().clear();
    if (bucket.hasTextCollisionBoxData()) bucket.textCollisionBox->dynamicVertices().clear();
    if (bucket.hasTextCollisionCircleData()) bucket.textCollisionCircle->dynamicVertices().clear();

    const JointOpacityState duplicateOpacityState(false, false, true);

    const bool textAllowOverlap = bucket.layout->get<style::TextAllowOverlap>();
    const bool iconAllowOverlap = bucket.layout->get<style::IconAllowOverlap>();
    const bool variablePlacement = bucket.hasVariableTextAnchors();
    const bool rotateWithMap = bucket.layout->get<style::TextRotationAlignment>() == style::AlignmentType::Map;
    const bool pitchWithMap = bucket.layout->get<style::TextPitchAlignment>() == style::AlignmentType::Map;
    const bool hasIconTextFit = bucket.layout->get<style::IconTextFit>() != style::IconTextFitType::None;
    const bool screenSpace = bucket.layout->get<style::SymbolScreenSpace>();

    // If allow-overlap is true, we can show symbols before placement runs on them
    // But we have to wait for placement if we potentially depend on a paired icon/text
    // with allow-overlap: false.
    // See https://github.com/mapbox/mapbox-gl-native/issues/12483
    // Prevent a flickering issue when showing a symbol allowing overlap.
    const JointOpacityState defaultOpacityState(
        (screenSpace || bucket.justReloaded) && textAllowOverlap &&
            (iconAllowOverlap || !(bucket.hasIconData() || bucket.hasSdfIconData()) ||
             bucket.layout->get<style::IconOptional>()),
        (screenSpace || bucket.justReloaded) && iconAllowOverlap &&
            (textAllowOverlap || !bucket.hasTextData() || bucket.layout->get<style::TextOptional>()),
        true);

    for (SymbolInstance& symbolInstance : bucket.symbolInstances) {
        if (!symbolInstance.check(SYM_GUARD_LOC)) continue;
        bool isDuplicate = seenCrossTileIDs.contains(symbolInstance.getCrossTileID());

        auto it = opacities.find(symbolInstance.getCrossTileID());
        auto opacityState = defaultOpacityState;
        if (isDuplicate) {
            opacityState = duplicateOpacityState;
        } else if (it != opacities.end()) {
            opacityState = it->second;
        }

        seenCrossTileIDs.insert(symbolInstance.getCrossTileID());

        if (symbolInstance.hasText() || symbolInstance.hasIcon()) {
            if (!symbolInstance.checkIndexes(bucket.text.placedSymbols.size(),
                                             bucket.icon.placedSymbols.size(),
                                             bucket.sdfIcon.placedSymbols.size(),
                                             SYM_GUARD_LOC))
                return;
        }
        if (symbolInstance.hasText()) {
            size_t textOpacityVerticesSize = 0u;
            const auto& opacityVertex = SymbolBucket::opacityVertex(opacityState.text.placed,
                                                                    opacityState.text.opacity);
            if (symbolInstance.getPlacedRightTextIndex()) {
                textOpacityVerticesSize += symbolInstance.getRightJustifiedGlyphQuadsSize() * 4;
                PlacedSymbol& placed = bucket.text.placedSymbols[*symbolInstance.getPlacedRightTextIndex()];
                placed.hidden = opacityState.isHidden();
            }
            if (symbolInstance.getPlacedCenterTextIndex() && !symbolInstance.getSingleLine()) {
                textOpacityVerticesSize += symbolInstance.getCenterJustifiedGlyphQuadsSize() * 4;
                PlacedSymbol& placed = bucket.text.placedSymbols[*symbolInstance.getPlacedCenterTextIndex()];
                placed.hidden = opacityState.isHidden();
            }
            if (symbolInstance.getPlacedLeftTextIndex() && !symbolInstance.getSingleLine()) {
                textOpacityVerticesSize += symbolInstance.getLeftJustifiedGlyphQuadsSize() * 4;
                PlacedSymbol& placed = bucket.text.placedSymbols[*symbolInstance.getPlacedLeftTextIndex()];
                placed.hidden = opacityState.isHidden();
            }
            if (symbolInstance.getPlacedVerticalTextIndex()) {
                textOpacityVerticesSize += symbolInstance.getVerticalGlyphQuadsSize() * 4;
                bucket.text.placedSymbols[*symbolInstance.getPlacedVerticalTextIndex()].hidden =
                    opacityState.isHidden();
            }

            bucket.text.opacityVertices().extend(textOpacityVerticesSize, opacityVertex);

            style::TextWritingModeType previousOrientation = style::TextWritingModeType::Horizontal;
            if (bucket.allowVerticalPlacement) {
                auto prevOrientation = placedOrientations.find(symbolInstance.getCrossTileID());
                if (prevOrientation != placedOrientations.end()) {
                    previousOrientation = prevOrientation->second;
                    markUsedOrientation(bucket, prevOrientation->second, symbolInstance);
                }
            }

            auto prevOffset = variableOffsets.find(symbolInstance.getCrossTileID());
            if (prevOffset != variableOffsets.end()) {
                markUsedJustification(bucket, prevOffset->second.anchor, symbolInstance, previousOrientation);
            }
        }
        if (symbolInstance.hasIcon()) {
            size_t iconOpacityVerticesSize = 0u;
            const auto& opacityVertex = SymbolBucket::opacityVertex(opacityState.icon.placed,
                                                                    opacityState.icon.opacity);
            auto& iconBuffer = symbolInstance.hasSdfIcon() ? bucket.sdfIcon : bucket.icon;

            if (symbolInstance.getPlacedIconIndex()) {
                iconOpacityVerticesSize += symbolInstance.getIconQuadsSize() * 4;
                iconBuffer.placedSymbols[*symbolInstance.getPlacedIconIndex()].hidden = opacityState.isHidden();
            }

            if (symbolInstance.getPlacedVerticalIconIndex()) {
                iconOpacityVerticesSize += symbolInstance.getIconQuadsSize() * 4;
                iconBuffer.placedSymbols[*symbolInstance.getPlacedVerticalIconIndex()].hidden = opacityState.isHidden();
            }

            iconBuffer.opacityVertices().extend(iconOpacityVerticesSize, opacityVertex);
        }

        auto updateIconCollisionBox = [&](const auto& feature, const bool placed, const Point<float>& shift) {
            if (feature.alongLine) {
                return;
            }
            const auto& dynamicVertex = SymbolBucket::collisionDynamicVertex(placed, false, shift);
            bucket.iconCollisionBox->dynamicVertices().extend(feature.boxes.size() * 4, dynamicVertex);
        };

        auto updateTextCollisionBox =
            [this, &bucket, &symbolInstance, &state, variablePlacement, rotateWithMap, pitchWithMap](
                const auto& feature, const bool placed) {
                Point<float> shift{0.0f, 0.0f};
                if (feature.alongLine) {
                    return shift;
                }
                bool used = true;
                if (variablePlacement) {
                    auto foundOffset = variableOffsets.find(symbolInstance.getCrossTileID());
                    if (foundOffset != variableOffsets.end()) {
                        const VariableOffset& variableOffset = foundOffset->second;
                        // This will show either the currently placed position or
                        // the last successfully placed position (so you can
                        // visualize what collision just made the symbol disappear,
                        // and the most likely place for the symbol to come back)
                        shift = calculateVariableLayoutOffset(variableOffset.anchor,
                                                              variableOffset.width,
                                                              variableOffset.height,
                                                              variableOffset.offset,
                                                              variableOffset.textBoxScale,
                                                              rotateWithMap,
                                                              pitchWithMap,
                                                              static_cast<float>(state.getBearing()));
                    } else {
                        // No offset -> this symbol hasn't been placed since coming
                        // on-screen No single box is particularly meaningful and
                        // all of them would be too noisy Use the center box just to
                        // show something's there, but mark it "not used"
                        used = false;
                    }
                }
                const auto& dynamicVertex = SymbolBucket::collisionDynamicVertex(placed, !used, shift);
                bucket.textCollisionBox->dynamicVertices().extend(feature.boxes.size() * 4, dynamicVertex);
                return shift;
            };

        auto updateCollisionCircles = [&](const auto& feature, const bool placed, bool isText) {
            if (!feature.alongLine) {
                return;
            }
            auto circles = collisionCircles.find(&feature);
            if (circles != collisionCircles.end()) {
                for (const auto& circle : circles->second) {
                    const auto& dynamicVertex = SymbolBucket::collisionDynamicVertex(placed, !circle.isCircle(), {});
                    isText ? bucket.textCollisionCircle->dynamicVertices().extend(4, dynamicVertex)
                           : bucket.iconCollisionCircle->dynamicVertices().extend(4, dynamicVertex);
                }
            } else {
                // This feature was not placed, because it was not loaded or
                // from a fading tile. Apply default values.
                static const auto dynamicVertex = SymbolBucket::collisionDynamicVertex(placed, false /*not used*/, {});
                isText ? bucket.textCollisionCircle->dynamicVertices().extend(4 * feature.boxes.size(), dynamicVertex)
                       : bucket.iconCollisionCircle->dynamicVertices().extend(4 * feature.boxes.size(), dynamicVertex);
            }
        };
        Point<float> textShift{0.0f, 0.0f};
        Point<float> verticalTextShift{0.0f, 0.0f};
        if (bucket.hasTextCollisionBoxData()) {
            textShift = updateTextCollisionBox(symbolInstance.getTextCollisionFeature(), opacityState.text.placed);
            if (bucket.allowVerticalPlacement && symbolInstance.getVerticalTextCollisionFeature()) {
                verticalTextShift = updateTextCollisionBox(*symbolInstance.getVerticalTextCollisionFeature(),
                                                           opacityState.text.placed);
            }
        }
        if (bucket.hasIconCollisionBoxData()) {
            updateIconCollisionBox(symbolInstance.getIconCollisionFeature(),
                                   opacityState.icon.placed,
                                   hasIconTextFit ? textShift : Point<float>{0.0f, 0.0f});
            if (bucket.allowVerticalPlacement && symbolInstance.getVerticalIconCollisionFeature()) {
                updateIconCollisionBox(*symbolInstance.getVerticalIconCollisionFeature(),
                                       opacityState.text.placed,
                                       hasIconTextFit ? verticalTextShift : Point<float>{0.0f, 0.0f});
            }
        }

        if (bucket.hasIconCollisionCircleData()) {
            updateCollisionCircles(symbolInstance.getIconCollisionFeature(), opacityState.icon.placed, false);
        }
        if (bucket.hasTextCollisionCircleData()) {
            updateCollisionCircles(symbolInstance.getTextCollisionFeature(), opacityState.text.placed, true);
        }
    }

    bucket.sortFeatures(static_cast<float>(state.getBearing()));
    static_cast<Bucket&>(bucket).check(SYM_GUARD_LOC);

    const auto retainedData = retainedQueryData.find(bucket.bucketInstanceId);
    if (retainedData != retainedQueryData.end()) {
        retainedData->second.featureSortOrder = bucket.featureSortOrder;
    }
}

namespace {
std::optional<size_t> justificationToIndex(style::TextJustifyType justify,
                                           const SymbolInstance& symbolInstance,
                                           style::TextWritingModeType orientation) {
    // Vertical symbol has just one justification, style::TextJustifyType::Left.
    if (orientation == style::TextWritingModeType::Vertical) {
        return symbolInstance.getPlacedVerticalTextIndex();
    }

    switch (justify) {
        case style::TextJustifyType::Right:
            return symbolInstance.getPlacedRightTextIndex();
        case style::TextJustifyType::Center:
            return symbolInstance.getPlacedCenterTextIndex();
        case style::TextJustifyType::Left:
            return symbolInstance.getPlacedLeftTextIndex();
        case style::TextJustifyType::Auto:
            break;
    }
    assert(false);
    return std::nullopt;
}

const style::TextJustifyType justifyTypes[] = {
    style::TextJustifyType::Right, style::TextJustifyType::Center, style::TextJustifyType::Left};

} // namespace

void Placement::markUsedJustification(SymbolBucket& bucket,
                                      style::TextVariableAnchorType placedAnchor,
                                      const SymbolInstance& symbolInstance,
                                      style::TextWritingModeType orientation) const {
    style::TextJustifyType anchorJustify = getAnchorJustification(placedAnchor);
    assert(anchorJustify != style::TextJustifyType::Auto);
    const std::optional<size_t>& autoIndex = justificationToIndex(anchorJustify, symbolInstance, orientation);

    for (auto& justify : justifyTypes) {
        const std::optional<size_t> index = justificationToIndex(justify, symbolInstance, orientation);
        if (index) {
            assert(bucket.text.placedSymbols.size() > *index);
            if (!symbolInstance.checkIndex(index, bucket.text.placedSymbols.size(), SYM_GUARD_LOC)) continue;
            if (autoIndex && *index != *autoIndex) {
                // There are multiple justifications and this one isn't it: shift offscreen
                bucket.text.placedSymbols.at(*index).crossTileID = 0u;
            } else {
                // Either this is the chosen justification or the justification is hardwired: use this one
                bucket.text.placedSymbols.at(*index).crossTileID = symbolInstance.getCrossTileID();
            }
        }
    }
}

void Placement::markUsedOrientation(SymbolBucket& bucket,
                                    style::TextWritingModeType orientation,
                                    const SymbolInstance& symbolInstance) const {
    const auto horizontal = orientation == style::TextWritingModeType::Horizontal
                                ? std::optional<style::TextWritingModeType>(orientation)
                                : std::nullopt;
    const auto vertical = orientation == style::TextWritingModeType::Vertical
                              ? std::optional<style::TextWritingModeType>(orientation)
                              : std::nullopt;

    if (!symbolInstance.checkIndexes(bucket.text.placedSymbols.size(),
                                     bucket.icon.placedSymbols.size(),
                                     bucket.sdfIcon.placedSymbols.size(),
                                     SYM_GUARD_LOC)) {
        return;
    }

    if (symbolInstance.getPlacedRightTextIndex()) {
        bucket.text.placedSymbols.at(*symbolInstance.getPlacedRightTextIndex()).placedOrientation = horizontal;
    }

    if (symbolInstance.getPlacedCenterTextIndex() && !symbolInstance.getSingleLine()) {
        bucket.text.placedSymbols.at(*symbolInstance.getPlacedCenterTextIndex()).placedOrientation = horizontal;
    }

    if (symbolInstance.getPlacedLeftTextIndex() && !symbolInstance.getSingleLine()) {
        bucket.text.placedSymbols.at(*symbolInstance.getPlacedLeftTextIndex()).placedOrientation = horizontal;
    }

    if (symbolInstance.getPlacedVerticalTextIndex()) {
        bucket.text.placedSymbols.at(*symbolInstance.getPlacedVerticalTextIndex()).placedOrientation = vertical;
    }

    auto& iconBuffer = symbolInstance.hasSdfIcon() ? bucket.sdfIcon : bucket.icon;
    if (symbolInstance.getPlacedIconIndex()) {
        iconBuffer.placedSymbols.at(*symbolInstance.getPlacedIconIndex()).placedOrientation = horizontal;
    }

    if (symbolInstance.getPlacedVerticalIconIndex()) {
        iconBuffer.placedSymbols.at(*symbolInstance.getPlacedVerticalIconIndex()).placedOrientation = vertical;
    }
}

bool Placement::isTiltedView() const {
    return updateParameters->transformState.getPitch() != 0.0f;
}

float Placement::symbolFadeChange(TimePoint now) const {
    if (transitionsEnabled() &&
        transitionOptions.duration.value_or(util::DEFAULT_TRANSITION_DURATION) > Milliseconds(0)) {
        return std::chrono::duration<float>(now - commitTime) /
                   transitionOptions.duration.value_or(util::DEFAULT_TRANSITION_DURATION) +
               prevZoomAdjustment;
    }
    return 1.0;
}

float Placement::zoomAdjustment(const float zoom) const {
    // When zooming out labels can overlap each other quickly. This
    // adjustment is used to reduce the fade duration for symbols while zooming
    // out quickly. It is also used to reduce the interval between placement
    // calculations. Reducing the interval between placements means collisions
    // are discovered and eliminated sooner.
    return std::max(0.0f, (placementZoom - zoom) / 1.5f);
}

const JointPlacement* Placement::getSymbolPlacement(const SymbolInstance& symbol) const {
    assert(symbol.getCrossTileID() != 0);
    const auto found = placements.find(symbol.getCrossTileID());
    return (found != placements.end()) ? &found->second : nullptr;
}

Duration Placement::getUpdatePeriod(const float zoom) const {
    // Even if transitionOptions.duration is set to a value < 300ms, we still wait
    // for this default transition duration before attempting another placement operation.
    const auto fadeDuration = std::max(util::DEFAULT_TRANSITION_DURATION,
                                       transitionOptions.duration.value_or(util::DEFAULT_TRANSITION_DURATION));
    return std::chrono::duration_cast<Duration>(fadeDuration * (1.0 - zoomAdjustment(zoom)));
}

bool Placement::transitionsEnabled() const {
    return transitionOptions.enablePlacementTransitions;
}

bool Placement::hasTransitions(TimePoint now) const {
    assert(transitionsEnabled());
    return std::chrono::duration<float>(now - fadeStartTime) <
           transitionOptions.duration.value_or(util::DEFAULT_TRANSITION_DURATION);
}

const std::vector<PlacedSymbolData>& Placement::getPlacedSymbolsData() const {
    return placedSymbolsData_;
}

void Placement::newSymbolPlaced(const SymbolInstance& symbol,
                                const PlacementContext& ctx,
                                const JointPlacement& placement,
                                style::SymbolPlacementType placementType,
                                float evaluatedTextSize,
                                float evaluatedIconSize,
                                const std::vector<ProjectedCollisionBox>& textCollisionBoxes,
                                const std::vector<ProjectedCollisionBox>& iconCollisionBoxes) {
    if (!placedSymbolDataCollected_) return;

    struct ExportGeometry {
        std::optional<mapbox::geometry::box<float>> bounds;
        bool hasCircle = false;
        float angle = 0;
        std::vector<Point<float>> path;
    };

    // Projected collision geometry provides line paths and a fallback bounds box.
    // Visual bounds are exported separately because collision padding must not
    // affect Widget centering or size.
    const auto extractGeometry = [](const std::vector<ProjectedCollisionBox>& boxes) {
        ExportGeometry result;
        bool hasGeometry = false;
        float minX = 0, minY = 0, maxX = 0, maxY = 0;
        Point<float> first{0, 0}, last{0, 0};

        const auto expandBounds = [&](float x1, float y1, float x2, float y2) {
            if (!hasGeometry) {
                minX = x1;
                minY = y1;
                maxX = x2;
                maxY = y2;
                hasGeometry = true;
            } else {
                minX = std::min(minX, x1);
                minY = std::min(minY, y1);
                maxX = std::max(maxX, x2);
                maxY = std::max(maxY, y2);
            }
        };

        for (const auto& b : boxes) {
            if (b.isBox()) {
                const auto& box = b.box();
                expandBounds(box.min.x, box.min.y, box.max.x, box.max.y);
            } else if (b.isCircle()) {
                const auto& circle = b.circle();
                const Point<float> center{static_cast<float>(circle.center.x), static_cast<float>(circle.center.y)};
                if (!result.hasCircle) first = center;
                last = center;
                result.hasCircle = true;
                result.path.push_back(center);
                expandBounds(center.x - circle.radius,
                             center.y - circle.radius,
                             center.x + circle.radius,
                             center.y + circle.radius);
            }
        }

        if (hasGeometry) {
            result.bounds = mapbox::geometry::box<float>({minX, minY}, {maxX, maxY});
        }
        if (result.hasCircle && (first.x != last.x || first.y != last.y)) {
            result.angle = std::atan2(last.y - first.y, last.x - first.x);
        }
        return result;
    };

    auto textGeometry = extractGeometry(textCollisionBoxes);
    auto iconGeometry = extractGeometry(iconCollisionBoxes);

    const auto orientation = placedOrientations.find(symbol.getCrossTileID());
    const bool vertical = orientation != placedOrientations.end() &&
                          orientation->second == style::TextWritingModeType::Vertical;
    const auto& exportData = symbol.getExportData();
    const auto variableOffset = variableOffsets.find(symbol.getCrossTileID());
    auto textJustify = exportData.textJustify;
    if (textJustify == style::TextJustifyType::Auto) {
        textJustify = variableOffset == variableOffsets.end() ? style::TextJustifyType::Center
                                                              : getAnchorJustification(variableOffset->second.anchor);
    }
    const bool textAlongLine = placementType != style::SymbolPlacementType::Point &&
                               ctx.getLayout().get<style::TextRotationAlignment>() == style::AlignmentType::Map;
    const bool iconAlongLine = placementType != style::SymbolPlacementType::Point &&
                               ctx.getLayout().get<style::IconRotationAlignment>() == style::AlignmentType::Map;

    struct WidgetTransform {
        std::array<float, 4> matrix;
        Point<float> unitX;
        Point<float> unitY;
        float perspectiveRatio = 1;
    };
    const auto widgetTransform = [&](bool text, float rotation) {
        const auto& state = ctx.getTransformState();
        const bool pitchWithMap = text ? ctx.pitchTextWithMap : ctx.pitchIconWithMap;
        const bool rotateWithMap = text ? ctx.rotateTextWithMap : ctx.rotateIconWithMap;
        const auto& labelPlane = text ? ctx.textLabelPlaneMatrix : ctx.iconLabelPlaneMatrix;
        const auto& tileMatrix = ctx.getRenderTile().matrix;
        const auto glMatrix = getGlCoordMatrix(tileMatrix, pitchWithMap, rotateWithMap, state, ctx.pixelsToTileUnits);
        const auto tileAnchor = convertPoint<float>(symbol.getAnchor().point);
        const auto labelAnchor = project(tileAnchor, labelPlane).first;
        const auto origin = project(labelAnchor, glMatrix).first;
        const auto xPoint = project(labelAnchor + Point<float>{1, 0}, glMatrix).first;
        const auto yPoint = project(labelAnchor + Point<float>{0, 1}, glMatrix).first;
        const float halfWidth = static_cast<float>(state.getSize().width) * 0.5f;
        const float halfHeight = static_cast<float>(state.getSize().height) * 0.5f;
        const float cameraToAnchorDistance = project(tileAnchor, tileMatrix).second;
        const float distanceRatio = pitchWithMap ? cameraToAnchorDistance / state.getCameraToCenterDistance()
                                                 : state.getCameraToCenterDistance() / cameraToAnchorDistance;
        // The symbol shader applies this size scale unless icon-offset is defined.
        const float perspectiveRatio = exportData.iconOffsetDefined
                                           ? 1.0f
                                           : util::clamp(0.5f + 0.5f * distanceRatio, 0.0f, 4.0f);
        const Point<float> xAxis{(xPoint.x - origin.x) * halfWidth, -(xPoint.y - origin.y) * halfHeight};
        const Point<float> yAxis{(yPoint.x - origin.x) * halfWidth, -(yPoint.y - origin.y) * halfHeight};
        float widgetRotation = rotation;
        // The symbol shader applies projected map rotation after viewport-aligned placement.
        const bool rotateInShader = rotateWithMap && !pitchWithMap &&
                                    ctx.getLayout().get<style::SymbolPlacement>() == style::SymbolPlacementType::Point;
        if (rotateInShader) {
            const auto mapAnchor = project(tileAnchor, tileMatrix).first;
            const auto mapEast = project(tileAnchor + Point<float>{1, 0}, tileMatrix).first;
            const Point<float> mapEastAxis{(mapEast.x - mapAnchor.x) * halfWidth,
                                           -(mapEast.y - mapAnchor.y) * halfHeight};
            if (std::hypot(mapEastAxis.x, mapEastAxis.y) > 1.0e-6f) {
                widgetRotation += std::atan2(mapEastAxis.y, mapEastAxis.x);
            }
        }
        const float cosine = std::cos(widgetRotation);
        const float sine = std::sin(widgetRotation);
        const Point<float> unitX{xAxis.x * cosine + yAxis.x * sine, xAxis.y * cosine + yAxis.y * sine};
        const Point<float> unitY{-xAxis.x * sine + yAxis.x * cosine, -xAxis.y * sine + yAxis.y * cosine};
        return WidgetTransform{
            .matrix = {unitX.x * perspectiveRatio,
                       unitX.y * perspectiveRatio,
                       unitY.x * perspectiveRatio,
                       unitY.y * perspectiveRatio},
            .unitX = unitX,
            .unitY = unitY,
            .perspectiveRatio = perspectiveRatio,
        };
    };

    const auto textWidgetTransform = widgetTransform(true, exportData.textRotation);
    const auto iconWidgetTransform = widgetTransform(false, exportData.iconRotation);
    const auto textTransform = textWidgetTransform.matrix;
    auto iconTransform = iconWidgetTransform.matrix;
    const auto firstValidTextBounds = [&]() -> const SymbolVisualBounds& {
        if (vertical && exportData.verticalTextBounds.valid) return exportData.verticalTextBounds;
        const auto* selected = &exportData.centerTextBounds;
        if (textJustify == style::TextJustifyType::Right) selected = &exportData.rightTextBounds;
        if (textJustify == style::TextJustifyType::Left) selected = &exportData.leftTextBounds;
        if (selected->valid) return *selected;
        if (exportData.rightTextBounds.valid) return exportData.rightTextBounds;
        if (exportData.centerTextBounds.valid) return exportData.centerTextBounds;
        return exportData.leftTextBounds;
    };
    struct VisualGeometry {
        Point<float> offset;
        float width = 0;
        float height = 0;
    };
    const auto visualGeometry =
        [](const SymbolVisualBounds& bounds, const std::array<float, 4>& transform, float scale) {
            VisualGeometry result;
            if (!bounds.valid) return result;
            const Point<float> center{(bounds.left + bounds.right) * 0.5f * scale,
                                      (bounds.top + bounds.bottom) * 0.5f * scale};
            result.offset = {transform[0] * center.x + transform[2] * center.y,
                             transform[1] * center.x + transform[3] * center.y};
            result.width = (bounds.right - bounds.left) * scale;
            result.height = (bounds.bottom - bounds.top) * scale;
            return result;
        };
    auto textVisual = visualGeometry(
        firstValidTextBounds(), textTransform, evaluatedTextSize / static_cast<float>(util::ONE_EM));
    const auto& selectedIconBounds = vertical && exportData.verticalIconBounds.valid ? exportData.verticalIconBounds
                                                                                     : exportData.iconBounds;
    auto iconVisual = visualGeometry(selectedIconBounds, iconTransform, evaluatedIconSize);
    struct StretchAxis {
        float shaderScale;
        float extentScale;
    };
    const auto stretchAxis = [evaluatedIconSize, &iconWidgetTransform](float stretchFraction) {
        const float fixedFraction = 1.0f - stretchFraction;
        const float fontScale = evaluatedIconSize * iconWidgetTransform.perspectiveRatio;
        const float shaderScale = std::max(fixedFraction, fontScale);

        return StretchAxis{
            .shaderScale = shaderScale,
            .extentScale = (shaderScale - fixedFraction) / stretchFraction,
        };
    };
    if (selectedIconBounds.valid && evaluatedIconSize > 0 &&
        (exportData.iconStretchFractionX < 1 || exportData.iconStretchFractionY < 1)) {
        // Even without icon-text-fit, stretchable images use the symbol shader's
        // em and fixed-pixel split. This changes both their extent and center.
        const auto scaleX = stretchAxis(exportData.iconStretchFractionX);
        const auto scaleY = stretchAxis(exportData.iconStretchFractionY);
        const float widgetScaleX = scaleX.extentScale / evaluatedIconSize;
        const float widgetScaleY = scaleY.extentScale / evaluatedIconSize;
        iconTransform = {
            iconWidgetTransform.unitX.x * widgetScaleX,
            iconWidgetTransform.unitX.y * widgetScaleX,
            iconWidgetTransform.unitY.x * widgetScaleY,
            iconWidgetTransform.unitY.y * widgetScaleY,
        };
        const float width = selectedIconBounds.right - selectedIconBounds.left;
        const float height = selectedIconBounds.bottom - selectedIconBounds.top;
        const Point<float> center{
            selectedIconBounds.left * scaleX.shaderScale + width * scaleX.extentScale * 0.5f,
            selectedIconBounds.top * scaleY.shaderScale + height * scaleY.extentScale * 0.5f,
        };
        iconVisual.offset = {
            iconWidgetTransform.unitX.x * center.x + iconWidgetTransform.unitY.x * center.y,
            iconWidgetTransform.unitX.y * center.x + iconWidgetTransform.unitY.y * center.y,
        };
        iconVisual.width = width * scaleX.extentScale;
        iconVisual.height = height * scaleY.extentScale;
    }

    if (variableOffset != variableOffsets.end()) {
        const auto& state = ctx.getTransformState();
        const auto& tile = ctx.getRenderTile();
        const auto tileAnchor = convertPoint<float>(symbol.getAnchor().point);
        const auto projectedAnchor = project(tileAnchor, ctx.pitchTextWithMap ? tile.matrix : ctx.textLabelPlaneMatrix);
        const float perspectiveRatio = 0.5f + 0.5f * (state.getCameraToCenterDistance() / projectedAnchor.second);
        float renderTextSize = evaluatedTextSize * perspectiveRatio / util::ONE_EM;
        if (ctx.pitchTextWithMap) {
            renderTextSize *= ctx.getBucket().tilePixelRatio / ctx.scale;
        }
        const auto& variable = variableOffset->second;
        auto shift = calculateVariableRenderShift(
            variable.anchor, variable.width, variable.height, variable.offset, variable.textBoxScale, renderTextSize);
        Point<float> shiftedAnchor;
        if (ctx.pitchTextWithMap) {
            shiftedAnchor = project(tileAnchor + shift, ctx.textLabelPlaneMatrix).first;
        } else if (ctx.rotateTextWithMap) {
            shift = util::rotate(shift, -state.getPitch());
            shiftedAnchor = projectedAnchor.first + shift;
        } else {
            shiftedAnchor = projectedAnchor.first + shift;
        }

        const auto labelAnchor = project(tileAnchor, ctx.textLabelPlaneMatrix).first;
        const auto glMatrix = getGlCoordMatrix(
            tile.matrix, ctx.pitchTextWithMap, ctx.rotateTextWithMap, state, ctx.pixelsToTileUnits);
        const auto baseScreen = project(labelAnchor, glMatrix).first;
        const auto shiftedScreen = project(shiftedAnchor, glMatrix).first;
        const float halfWidth = static_cast<float>(state.getSize().width) * 0.5f;
        const float halfHeight = static_cast<float>(state.getSize().height) * 0.5f;
        const Point<float> variableAnchorScreenShift{
            (shiftedScreen.x - baseScreen.x) * halfWidth,
            -(shiftedScreen.y - baseScreen.y) * halfHeight,
        };
        textVisual.offset += variableAnchorScreenShift;
        if (ctx.hasIconTextFit) iconVisual.offset += variableAnchorScreenShift;
    }

    std::vector<std::string> layers;
    layers.reserve(ctx.getBucket().paintProperties.size());
    for (const auto& pair : ctx.getBucket().paintProperties) {
        layers.push_back(pair.first);
    }
    const auto anchorPoint = collisionIndex.projectPoint(ctx.getRenderTile().matrix, symbol.getAnchor().point);
    const auto exportSourceLineGeometry = [&](ExportGeometry& geometry, bool retainPath) {
        if (exportData.sourceLineSegment) {
            const auto& lineSegment = *exportData.sourceLineSegment;
            std::vector<Point<float>> projectedPath;
            const auto appendProjected = [&](const Point<float>& point) {
                const auto projected = collisionIndex.projectPoint(ctx.getRenderTile().matrix, point);
                if (projectedPath.empty() || projectedPath.back() != projected) {
                    projectedPath.push_back(projected);
                }
            };
            appendProjected(lineSegment[0]);
            appendProjected(convertPoint<float>(symbol.getAnchor().point));
            appendProjected(lineSegment[1]);
            if (projectedPath.size() >= 2) {
                const auto& first = projectedPath.front();
                const auto& last = projectedPath.back();
                geometry.angle = std::atan2(last.y - first.y, last.x - first.x);
                if (retainPath) geometry.path = std::move(projectedPath);
            }
        }
    };
    if (placement.text && textAlongLine && textGeometry.path.size() < 2) {
        exportSourceLineGeometry(textGeometry, false);
    }
    if (placement.icon && iconAlongLine) {
        exportSourceLineGeometry(iconGeometry, true);
    }
    const float viewportPadding = collisionIndex.getViewportPadding();
    const auto anchorLatLng = ctx.getTransformState().screenCoordinateToLatLng(
        ScreenCoordinate{anchorPoint.x - viewportPadding,
                         ctx.getTransformState().getSize().height - (anchorPoint.y - viewportPadding)},
        LatLng::Wrapped);
    auto textPath = textGeometry.path;
    for (auto& point : textPath) point -= anchorPoint;
    auto iconPath = iconGeometry.path;
    for (auto& point : iconPath) point -= anchorPoint;

    PlacedSymbolData symbolData{
        .key = symbol.getKey(),
        .lineBrokenText = symbol.getLineBrokenText(),
        .logicalLineBrokenText = exportData.logicalLineBrokenText,
        .textRTL = exportData.textRTL,
        .crossTileID = symbol.getCrossTileID(),
        .bucketInstanceID = ctx.getBucket().bucketInstanceId,
        .symbolInstanceIndex = static_cast<uint32_t>(&symbol - ctx.getBucket().symbolInstances.data()),
        .textCollisionBox = textGeometry.bounds,
        .iconCollisionBox = iconGeometry.bounds,
        .textPlaced = placement.text,
        .iconPlaced = placement.icon,
        .intersectsTileBorder = false,
        .viewportPadding = viewportPadding,
        .anchorPoint = anchorPoint,
        .tileWrap = ctx.getRenderTile().id.wrap,
        .tileAnchor = convertPoint<float>(symbol.getAnchor().point),
        .anchorLatLng = anchorLatLng,
        .layer = ctx.getBucket().bucketLeaderID,
        .layers = std::move(layers),
        .renderGroup = currentRenderGroup,
        .renderOrder = currentRenderOrder,
        .sourceID = exportData.sourceID,
        .sourceLayer = exportData.sourceLayer,
        .featureProperties = exportData.featureProperties,
        .featureID = exportData.featureID,
        .featureType = exportData.featureType,
        .canonicalZ = exportData.canonicalZ,
        .canonicalX = exportData.canonicalX,
        .canonicalY = exportData.canonicalY,
        .icon = symbol.getIconImageID(),
        .textSize = evaluatedTextSize,
        .iconSize = evaluatedIconSize,
        .textAngle = textGeometry.angle,
        .alongLine = textAlongLine,
        .textPath = std::move(textPath),
        .iconAngle = iconGeometry.angle,
        .iconAlongLine = iconAlongLine,
        .iconPath = std::move(iconPath),
        .visualTextSections = exportData.visualTextSections,
        .textSections = exportData.textSections,
        .textFontStack = exportData.textFontStack,
        .letterSpacing = exportData.letterSpacing,
        .lineHeight = exportData.lineHeight,
        .maxWidth = exportData.maxWidth,
        .textRotation = exportData.textRotation,
        .iconRotation = exportData.iconRotation,
        .textJustify = textJustify,
        .textPitchAlignment = ctx.getLayout().get<style::TextPitchAlignment>(),
        .textRotationAlignment = ctx.getLayout().get<style::TextRotationAlignment>(),
        .iconPitchAlignment = ctx.getLayout().get<style::IconPitchAlignment>(),
        .iconRotationAlignment = ctx.getLayout().get<style::IconRotationAlignment>(),
        .textKeepUpright = ctx.getLayout().get<style::TextKeepUpright>(),
        .iconKeepUpright = ctx.getLayout().get<style::IconKeepUpright>(),
        .vertical = vertical,
        .iconSDF = symbol.hasSdfIcon(),
        .iconFitWidth = exportData.iconFitWidth * evaluatedIconSize,
        .iconFitHeight = exportData.iconFitHeight * evaluatedIconSize,
        .textTransform = textTransform,
        .iconTransform = iconTransform,
        .textVisualOffset = textVisual.offset,
        .iconVisualOffset = iconVisual.offset,
        .textVisualWidth = textVisual.width,
        .textVisualHeight = textVisual.height,
        .iconVisualWidth = iconVisual.width,
        .iconVisualHeight = iconVisual.height};
    placedSymbolsData_.emplace_back(std::move(symbolData));
}

void Placement::refreshPlacedSymbolData(const RenderLayerReferences& layers, const TransformState& state) const {
    if (!placedSymbolDataCollected_ || placedSymbolsData_.empty() || !updateParameters) return;
    const auto& projectionMatrix = state.getProjectionMatrix();
    if (placedSymbolRefreshProjection_ && *placedSymbolRefreshProjection_ == projectionMatrix &&
        placedSymbolRefreshZoom_ == state.getZoom() && placedSymbolRefreshSize_ == state.getSize()) {
        return;
    }

    std::unordered_map<uint32_t, std::vector<PlacedSymbolData*>> symbolsByBucket;
    symbolsByBucket.reserve(placedSymbolsData_.size());
    for (auto& data : placedSymbolsData_) {
        symbolsByBucket[data.bucketInstanceID].push_back(&data);
    }

    CollisionIndex projection(state, updateParameters->mode, false);
    CollisionGroups refreshCollisionGroups(updateParameters->crossSourceCollisions);
    std::unordered_set<PlacedSymbolData*> refreshed;
    refreshed.reserve(placedSymbolsData_.size());

    struct ExportGeometry {
        std::optional<mapbox::geometry::box<float>> bounds;
        bool hasCircle = false;
        float angle = 0;
        std::vector<Point<float>> path;
    };
    const auto extractGeometry = [](const std::vector<ProjectedCollisionBox>& boxes) {
        ExportGeometry result;
        bool hasGeometry = false;
        float minX = 0, minY = 0, maxX = 0, maxY = 0;
        Point<float> first{0, 0}, last{0, 0};

        const auto expandBounds = [&](float x1, float y1, float x2, float y2) {
            if (!hasGeometry) {
                minX = x1;
                minY = y1;
                maxX = x2;
                maxY = y2;
                hasGeometry = true;
            } else {
                minX = std::min(minX, x1);
                minY = std::min(minY, y1);
                maxX = std::max(maxX, x2);
                maxY = std::max(maxY, y2);
            }
        };

        for (const auto& box : boxes) {
            if (box.isBox()) {
                const auto& bounds = box.box();
                expandBounds(bounds.min.x, bounds.min.y, bounds.max.x, bounds.max.y);
            } else if (box.isCircle()) {
                const auto& circle = box.circle();
                const Point<float> center{static_cast<float>(circle.center.x), static_cast<float>(circle.center.y)};
                if (!result.hasCircle) first = center;
                last = center;
                result.hasCircle = true;
                result.path.push_back(center);
                expandBounds(center.x - circle.radius,
                             center.y - circle.radius,
                             center.x + circle.radius,
                             center.y + circle.radius);
            }
        }

        if (hasGeometry) {
            result.bounds = mapbox::geometry::box<float>({minX, minY}, {maxX, maxY});
        }
        if (result.hasCircle && (first.x != last.x || first.y != last.y)) {
            result.angle = std::atan2(last.y - first.y, last.x - first.x);
        }

        return result;
    };

    const auto refreshSymbol = [&](PlacedSymbolData& data, const SymbolInstance& symbol, const PlacementContext& ctx) {
        const auto& bucket = ctx.getBucket();
        const auto& exportData = symbol.getExportData();
        const auto variableOffset = variableOffsets.find(symbol.getCrossTileID());

        const PlacedSymbol* placedText = nullptr;
        const CollisionFeature* textFeature = nullptr;
        auto textIndex = data.vertical ? symbol.getPlacedVerticalTextIndex()
                                       : symbol.getDefaultHorizontalPlacedTextIndex();
        if (!textIndex && data.vertical) textIndex = symbol.getDefaultHorizontalPlacedTextIndex();
        if (textIndex && *textIndex < bucket.text.placedSymbols.size()) {
            placedText = &bucket.text.placedSymbols[*textIndex];
            textFeature = data.vertical && symbol.getVerticalTextCollisionFeature()
                              ? &*symbol.getVerticalTextCollisionFeature()
                              : &symbol.getTextCollisionFeature();
        }

        const PlacedSymbol* placedIcon = nullptr;
        const CollisionFeature* iconFeature = nullptr;
        if (const auto iconIndex = symbol.getPlacedIconIndex()) {
            const auto& iconBuffer = symbol.hasSdfIcon() ? bucket.sdfIcon : bucket.icon;
            if (*iconIndex < iconBuffer.placedSymbols.size()) {
                placedIcon = &iconBuffer.placedSymbols[*iconIndex];
                iconFeature = data.vertical && symbol.getVerticalIconCollisionFeature()
                                  ? &*symbol.getVerticalIconCollisionFeature()
                                  : &symbol.getIconCollisionFeature();
            }
        }

        float evaluatedTextSize = data.textSize;
        if (placedText) {
            evaluatedTextSize = evaluateSizeForFeature(ctx.partiallyEvaluatedTextSize, *placedText);
        }
        float evaluatedIconSize = data.iconSize;
        if (placedIcon) {
            evaluatedIconSize = evaluateSizeForFeature(ctx.partiallyEvaluatedIconSize, *placedIcon);
        }

        Point<float> layoutShift;
        if (variableOffset != variableOffsets.end()) {
            const auto& variable = variableOffset->second;
            layoutShift = calculateVariableLayoutOffset(variable.anchor,
                                                        variable.width,
                                                        variable.height,
                                                        variable.offset,
                                                        variable.textBoxScale,
                                                        ctx.rotateTextWithMap,
                                                        ctx.pitchTextWithMap,
                                                        static_cast<float>(state.getBearing()));
        }

        std::vector<ProjectedCollisionBox> projectedTextBoxes;
        if (data.textPlaced && placedText && textFeature) {
            projection.projectFeature(*textFeature,
                                      layoutShift,
                                      ctx.getRenderTile().matrix,
                                      ctx.textLabelPlaneMatrix,
                                      ctx.pixelRatio,
                                      *placedText,
                                      ctx.scale,
                                      evaluatedTextSize,
                                      ctx.pitchTextWithMap,
                                      projectedTextBoxes);
        }
        std::vector<ProjectedCollisionBox> projectedIconBoxes;
        if (data.iconPlaced && placedIcon && iconFeature) {
            const Point<float> iconShift = ctx.hasIconTextFit && variableOffset != variableOffsets.end() &&
                                                   data.textPlaced
                                               ? layoutShift
                                               : Point<float>{};
            projection.projectFeature(*iconFeature,
                                      iconShift,
                                      ctx.getRenderTile().matrix,
                                      ctx.iconLabelPlaneMatrix,
                                      ctx.pixelRatio,
                                      *placedIcon,
                                      ctx.scale,
                                      evaluatedIconSize,
                                      ctx.pitchTextWithMap,
                                      projectedIconBoxes);
        }

        auto textGeometry = extractGeometry(projectedTextBoxes);
        auto iconGeometry = extractGeometry(projectedIconBoxes);
        const bool textAlongLine = data.alongLine;
        const bool iconAlongLine = data.iconAlongLine;

        struct WidgetTransform {
            std::array<float, 4> matrix;
            Point<float> unitX;
            Point<float> unitY;
            float perspectiveRatio = 1;
        };
        const auto widgetTransform = [&](bool text, float rotation) {
            const bool pitchWithMap = text ? ctx.pitchTextWithMap : ctx.pitchIconWithMap;
            const bool rotateWithMap = text ? ctx.rotateTextWithMap : ctx.rotateIconWithMap;
            const auto& labelPlane = text ? ctx.textLabelPlaneMatrix : ctx.iconLabelPlaneMatrix;
            const auto& tileMatrix = ctx.getRenderTile().matrix;
            const auto glMatrix = getGlCoordMatrix(
                tileMatrix, pitchWithMap, rotateWithMap, state, ctx.pixelsToTileUnits);
            const auto tileAnchor = convertPoint<float>(symbol.getAnchor().point);
            const auto labelAnchor = project(tileAnchor, labelPlane).first;
            const auto origin = project(labelAnchor, glMatrix).first;
            const auto xPoint = project(labelAnchor + Point<float>{1, 0}, glMatrix).first;
            const auto yPoint = project(labelAnchor + Point<float>{0, 1}, glMatrix).first;
            const float halfWidth = static_cast<float>(state.getSize().width) * 0.5f;
            const float halfHeight = static_cast<float>(state.getSize().height) * 0.5f;
            const float cameraToAnchorDistance = project(tileAnchor, tileMatrix).second;
            const float distanceRatio = pitchWithMap ? cameraToAnchorDistance / state.getCameraToCenterDistance()
                                                     : state.getCameraToCenterDistance() / cameraToAnchorDistance;
            const float perspectiveRatio = exportData.iconOffsetDefined
                                               ? 1.0f
                                               : util::clamp(0.5f + 0.5f * distanceRatio, 0.0f, 4.0f);
            const Point<float> xAxis{(xPoint.x - origin.x) * halfWidth, -(xPoint.y - origin.y) * halfHeight};
            const Point<float> yAxis{(yPoint.x - origin.x) * halfWidth, -(yPoint.y - origin.y) * halfHeight};
            float widgetRotation = rotation;
            const bool rotateInShader = rotateWithMap && !pitchWithMap &&
                                        ctx.getLayout().get<style::SymbolPlacement>() ==
                                            style::SymbolPlacementType::Point;
            if (rotateInShader) {
                const auto mapAnchor = project(tileAnchor, tileMatrix).first;
                const auto mapEast = project(tileAnchor + Point<float>{1, 0}, tileMatrix).first;
                const Point<float> mapEastAxis{(mapEast.x - mapAnchor.x) * halfWidth,
                                               -(mapEast.y - mapAnchor.y) * halfHeight};
                if (std::hypot(mapEastAxis.x, mapEastAxis.y) > 1.0e-6f) {
                    widgetRotation += std::atan2(mapEastAxis.y, mapEastAxis.x);
                }
            }
            const float cosine = std::cos(widgetRotation);
            const float sine = std::sin(widgetRotation);
            const Point<float> unitX{xAxis.x * cosine + yAxis.x * sine, xAxis.y * cosine + yAxis.y * sine};
            const Point<float> unitY{-xAxis.x * sine + yAxis.x * cosine, -xAxis.y * sine + yAxis.y * cosine};

            return WidgetTransform{
                .matrix = {unitX.x * perspectiveRatio,
                           unitX.y * perspectiveRatio,
                           unitY.x * perspectiveRatio,
                           unitY.y * perspectiveRatio},
                .unitX = unitX,
                .unitY = unitY,
                .perspectiveRatio = perspectiveRatio,
            };
        };

        const auto textWidgetTransform = widgetTransform(true, exportData.textRotation);
        const auto iconWidgetTransform = widgetTransform(false, exportData.iconRotation);
        const auto textTransform = textWidgetTransform.matrix;
        auto iconTransform = iconWidgetTransform.matrix;
        const auto firstValidTextBounds = [&]() -> const SymbolVisualBounds& {
            if (data.vertical && exportData.verticalTextBounds.valid) return exportData.verticalTextBounds;
            const auto* selected = &exportData.centerTextBounds;
            if (data.textJustify == style::TextJustifyType::Right) selected = &exportData.rightTextBounds;
            if (data.textJustify == style::TextJustifyType::Left) selected = &exportData.leftTextBounds;
            if (selected->valid) return *selected;
            if (exportData.rightTextBounds.valid) return exportData.rightTextBounds;
            if (exportData.centerTextBounds.valid) return exportData.centerTextBounds;

            return exportData.leftTextBounds;
        };
        struct VisualGeometry {
            Point<float> offset;
            float width = 0;
            float height = 0;
        };
        const auto visualGeometry =
            [](const SymbolVisualBounds& bounds, const std::array<float, 4>& transform, float scale) {
                VisualGeometry result;
                if (!bounds.valid) return result;
                const Point<float> center{(bounds.left + bounds.right) * 0.5f * scale,
                                          (bounds.top + bounds.bottom) * 0.5f * scale};
                result.offset = {transform[0] * center.x + transform[2] * center.y,
                                 transform[1] * center.x + transform[3] * center.y};
                result.width = (bounds.right - bounds.left) * scale;
                result.height = (bounds.bottom - bounds.top) * scale;

                return result;
            };
        auto textVisual = visualGeometry(
            firstValidTextBounds(), textTransform, evaluatedTextSize / static_cast<float>(util::ONE_EM));
        const auto& selectedIconBounds = data.vertical && exportData.verticalIconBounds.valid
                                             ? exportData.verticalIconBounds
                                             : exportData.iconBounds;
        auto iconVisual = visualGeometry(selectedIconBounds, iconTransform, evaluatedIconSize);
        struct StretchAxis {
            float shaderScale;
            float extentScale;
        };
        const auto stretchAxis = [evaluatedIconSize, &iconWidgetTransform](float stretchFraction) {
            const float fixedFraction = 1.0f - stretchFraction;
            const float fontScale = evaluatedIconSize * iconWidgetTransform.perspectiveRatio;
            const float shaderScale = std::max(fixedFraction, fontScale);

            return StretchAxis{
                .shaderScale = shaderScale,
                .extentScale = (shaderScale - fixedFraction) / stretchFraction,
            };
        };
        if (selectedIconBounds.valid && evaluatedIconSize > 0 &&
            (exportData.iconStretchFractionX < 1 || exportData.iconStretchFractionY < 1)) {
            const auto scaleX = stretchAxis(exportData.iconStretchFractionX);
            const auto scaleY = stretchAxis(exportData.iconStretchFractionY);
            const float widgetScaleX = scaleX.extentScale / evaluatedIconSize;
            const float widgetScaleY = scaleY.extentScale / evaluatedIconSize;
            iconTransform = {
                iconWidgetTransform.unitX.x * widgetScaleX,
                iconWidgetTransform.unitX.y * widgetScaleX,
                iconWidgetTransform.unitY.x * widgetScaleY,
                iconWidgetTransform.unitY.y * widgetScaleY,
            };
            const float width = selectedIconBounds.right - selectedIconBounds.left;
            const float height = selectedIconBounds.bottom - selectedIconBounds.top;
            const Point<float> center{
                selectedIconBounds.left * scaleX.shaderScale + width * scaleX.extentScale * 0.5f,
                selectedIconBounds.top * scaleY.shaderScale + height * scaleY.extentScale * 0.5f,
            };
            iconVisual.offset = {
                iconWidgetTransform.unitX.x * center.x + iconWidgetTransform.unitY.x * center.y,
                iconWidgetTransform.unitX.y * center.x + iconWidgetTransform.unitY.y * center.y,
            };
            iconVisual.width = width * scaleX.extentScale;
            iconVisual.height = height * scaleY.extentScale;
        }

        if (variableOffset != variableOffsets.end()) {
            const auto& tile = ctx.getRenderTile();
            const auto tileAnchor = convertPoint<float>(symbol.getAnchor().point);
            const auto projectedAnchor = project(tileAnchor,
                                                 ctx.pitchTextWithMap ? tile.matrix : ctx.textLabelPlaneMatrix);
            const float perspectiveRatio = 0.5f + 0.5f * (state.getCameraToCenterDistance() / projectedAnchor.second);
            float renderTextSize = evaluatedTextSize * perspectiveRatio / util::ONE_EM;
            if (ctx.pitchTextWithMap) {
                renderTextSize *= ctx.getBucket().tilePixelRatio / ctx.scale;
            }
            const auto& variable = variableOffset->second;
            auto shift = calculateVariableRenderShift(variable.anchor,
                                                      variable.width,
                                                      variable.height,
                                                      variable.offset,
                                                      variable.textBoxScale,
                                                      renderTextSize);
            Point<float> shiftedAnchor;
            if (ctx.pitchTextWithMap) {
                shiftedAnchor = project(tileAnchor + shift, ctx.textLabelPlaneMatrix).first;
            } else if (ctx.rotateTextWithMap) {
                shift = util::rotate(shift, -state.getPitch());
                shiftedAnchor = projectedAnchor.first + shift;
            } else {
                shiftedAnchor = projectedAnchor.first + shift;
            }

            const auto labelAnchor = project(tileAnchor, ctx.textLabelPlaneMatrix).first;
            const auto glMatrix = getGlCoordMatrix(
                tile.matrix, ctx.pitchTextWithMap, ctx.rotateTextWithMap, state, ctx.pixelsToTileUnits);
            const auto baseScreen = project(labelAnchor, glMatrix).first;
            const auto shiftedScreen = project(shiftedAnchor, glMatrix).first;
            const float halfWidth = static_cast<float>(state.getSize().width) * 0.5f;
            const float halfHeight = static_cast<float>(state.getSize().height) * 0.5f;
            const Point<float> variableAnchorScreenShift{
                (shiftedScreen.x - baseScreen.x) * halfWidth,
                -(shiftedScreen.y - baseScreen.y) * halfHeight,
            };
            textVisual.offset += variableAnchorScreenShift;
            if (ctx.hasIconTextFit) iconVisual.offset += variableAnchorScreenShift;
        }

        const auto anchorPoint = projection.projectPoint(ctx.getRenderTile().matrix, symbol.getAnchor().point);
        const auto exportSourceLineGeometry = [&](ExportGeometry& geometry, bool retainPath) {
            if (!exportData.sourceLineSegment) return;
            const auto& lineSegment = *exportData.sourceLineSegment;
            std::vector<Point<float>> projectedPath;
            const auto appendProjected = [&](const Point<float>& point) {
                const auto projected = projection.projectPoint(ctx.getRenderTile().matrix, point);
                if (projectedPath.empty() || projectedPath.back() != projected) {
                    projectedPath.push_back(projected);
                }
            };
            appendProjected(lineSegment[0]);
            appendProjected(convertPoint<float>(symbol.getAnchor().point));
            appendProjected(lineSegment[1]);
            if (projectedPath.size() < 2) return;
            const auto& first = projectedPath.front();
            const auto& last = projectedPath.back();
            geometry.angle = std::atan2(last.y - first.y, last.x - first.x);
            if (retainPath) geometry.path = std::move(projectedPath);
        };
        if (data.textPlaced && textAlongLine && textGeometry.path.size() < 2) {
            exportSourceLineGeometry(textGeometry, false);
        }
        if (data.iconPlaced && iconAlongLine) {
            exportSourceLineGeometry(iconGeometry, true);
        }
        const float viewportPadding = projection.getViewportPadding();
        const auto anchorLatLng = state.screenCoordinateToLatLng(
            ScreenCoordinate{anchorPoint.x - viewportPadding,
                             state.getSize().height - (anchorPoint.y - viewportPadding)},
            LatLng::Wrapped);
        auto textPath = std::move(textGeometry.path);
        for (auto& point : textPath) point -= anchorPoint;
        auto iconPath = std::move(iconGeometry.path);
        for (auto& point : iconPath) point -= anchorPoint;

        data.textCollisionBox = textGeometry.bounds;
        data.iconCollisionBox = iconGeometry.bounds;
        data.viewportPadding = viewportPadding;
        data.anchorPoint = anchorPoint;
        data.anchorLatLng = anchorLatLng;
        data.textSize = evaluatedTextSize;
        data.iconSize = evaluatedIconSize;
        data.textAngle = textGeometry.angle;
        data.textPath = std::move(textPath);
        data.iconAngle = iconGeometry.angle;
        data.iconPath = std::move(iconPath);
        data.iconFitWidth = exportData.iconFitWidth * evaluatedIconSize;
        data.iconFitHeight = exportData.iconFitHeight * evaluatedIconSize;
        data.textTransform = textTransform;
        data.iconTransform = iconTransform;
        data.textVisualOffset = textVisual.offset;
        data.iconVisualOffset = iconVisual.offset;
        data.textVisualWidth = textVisual.width;
        data.textVisualHeight = textVisual.height;
        data.iconVisualWidth = iconVisual.width;
        data.iconVisualHeight = iconVisual.height;
    };

    const float zoom = static_cast<float>(state.getZoom());
    for (const auto& layerRef : layers) {
        const auto& layer = layerRef.get();
        for (const auto& item : layer.getPlacementData()) {
            const auto& bucket = static_cast<const SymbolBucket&>(item.bucket.get());
            const auto found = symbolsByBucket.find(bucket.bucketInstanceId);
            if (found == symbolsByBucket.end()) continue;
            PlacementContext ctx{bucket, item.tile, state, zoom, refreshCollisionGroups.get(item.sourceId)};
            const auto& tileID = item.tile.get().id;
            for (auto* data : found->second) {
                if (refreshed.contains(data) || data->layer != bucket.bucketLeaderID || data->tileWrap != tileID.wrap ||
                    data->canonicalZ != tileID.canonical.z || data->canonicalX != tileID.canonical.x ||
                    data->canonicalY != tileID.canonical.y ||
                    data->symbolInstanceIndex >= bucket.symbolInstances.size()) {
                    continue;
                }
                const auto& symbol = bucket.symbolInstances[data->symbolInstanceIndex];
                if (symbol.getCrossTileID() != data->crossTileID) continue;
                refreshSymbol(*data, symbol, ctx);
                refreshed.insert(data);
            }
        }
    }
    placedSymbolRefreshProjection_ = projectionMatrix;
    placedSymbolRefreshZoom_ = state.getZoom();
    placedSymbolRefreshSize_ = state.getSize();
}

const CollisionIndex& Placement::getCollisionIndex() const {
    return collisionIndex;
}

const RetainedQueryData& Placement::getQueryData(uint32_t bucketInstanceId) const {
    auto it = retainedQueryData.find(bucketInstanceId);
    if (it == retainedQueryData.end()) {
        throw std::runtime_error("Placement::getQueryData with unrecognized bucketInstanceId");
    }
    return it->second;
}

/// Placement for Static map mode.
class StaticPlacement : public Placement {
public:
    explicit StaticPlacement(std::shared_ptr<const UpdateParameters> updateParameters_)
        : Placement(std::move(updateParameters_), std::nullopt) {}

protected:
    void commit() override;
    float symbolFadeChange(TimePoint) const override { return 1.0f; }
    bool hasTransitions(TimePoint) const override { return false; }
    bool transitionsEnabled() const override { return false; }
};

void StaticPlacement::commit() {
    fadeStartTime = commitTime;
    for (auto& jointPlacement : placements) {
        opacities.emplace(
            jointPlacement.first,
            JointOpacityState(jointPlacement.second.text, jointPlacement.second.icon, jointPlacement.second.skipFade));
    }
}

/// Placement for Tile map mode.

struct Intersection {
    Intersection(const SymbolInstance& symbol_, PlacementContext ctx_, IntersectStatus status_, std::size_t priority_)
        : symbol(symbol_),
          ctx(std::move(ctx_)),
          status(status_),
          priority(priority_) {}
    std::reference_wrapper<const SymbolInstance> symbol;
    PlacementContext ctx;
    IntersectStatus status;
    std::size_t priority; // less means more important
};

class TilePlacement : public StaticPlacement {
public:
    explicit TilePlacement(std::shared_ptr<const UpdateParameters> updateParameters_)
        : StaticPlacement(std::move(updateParameters_)) {}

private:
    void placeLayers(const RenderLayerReferences&) override;
    void placeSymbolBucket(const BucketPlacementData&, std::set<uint32_t>&) override;
    void collectPlacedSymbolData(bool enable) override { collectData = enable; }
    const std::vector<PlacedSymbolData>& getPlacedSymbolsData() const override { return placedSymbolsData; }

    std::optional<CollisionBoundaries> getAvoidEdges(const SymbolBucket&, const mat4&) override;
    bool canPlaceAtVariableAnchor(const CollisionBox& box,
                                  TextVariableAnchorType anchor,
                                  Point<float> shift,
                                  std::vector<style::TextVariableAnchorType>& anchors,
                                  const mat4& posMatrix,
                                  float textPixelRatio) override;
    void newSymbolPlaced(const SymbolInstance&,
                         const PlacementContext&,
                         const JointPlacement&,
                         style::SymbolPlacementType,
                         float,
                         float,
                         const std::vector<ProjectedCollisionBox>&,
                         const std::vector<ProjectedCollisionBox>&) override;

    bool shouldRetryPlacement(const JointPlacement&, const PlacementContext&);

    std::unordered_map<uint32_t, bool> locationCache;
    std::optional<CollisionBoundaries> tileBorders;
    std::set<uint32_t> seenCrossTileIDs;
    std::vector<PlacedSymbolData> placedSymbolsData;
    std::vector<Intersection> intersections;
    bool populateIntersections = false;
    std::size_t currentIntersectionPriority{};
    bool collectData = false;
};

void TilePlacement::placeLayers(const RenderLayerReferences& layers) {
    placedSymbolsData.clear();
    seenCrossTileIDs.clear();
    intersections.clear();
    currentIntersectionPriority = 0u;
    // Populate intersections.
    populateIntersections = true;
    for (auto it = layers.crbegin(); it != layers.crend(); ++it) {
        placeLayer(*it, seenCrossTileIDs);
    }

    std::sort(intersections.begin(), intersections.end(), [](const Intersection& a, const Intersection& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        uint8_t flagsA = a.status.flags;
        uint8_t flagsB = b.status.flags;
        // Items arranged as: VerticalBorders & HorizontalBorders (3) ->
        // VerticalBorders (2) -> HorizontalBorders (1)
        if (flagsA != flagsB) return flagsA > flagsB;
        // If both intersects the same border(s), look for a more noticeable cut-off.
        if (a.status.minSectionLength != b.status.minSectionLength) {
            return a.status.minSectionLength > b.status.minSectionLength;
        }
        // Look at the anchor coordinates
        if (a.symbol.get().getAnchor().point.y != b.symbol.get().getAnchor().point.y) {
            return a.symbol.get().getAnchor().point.y < b.symbol.get().getAnchor().point.y;
        }
        if (a.symbol.get().getAnchor().point.x != b.symbol.get().getAnchor().point.x) {
            return a.symbol.get().getAnchor().point.x < b.symbol.get().getAnchor().point.x;
        }
        // Finally, looking at the key hashes.
        return std::hash<std::u16string>()(a.symbol.get().getKey()) <
               std::hash<std::u16string>()(b.symbol.get().getKey());
    });
    // Place intersections.
    for (const auto& intersection : intersections) {
        const SymbolInstance& symbol = intersection.symbol;
        const PlacementContext& ctx = intersection.ctx;
        currentIntersectionPriority = intersection.priority;
        if (seenCrossTileIDs.contains(symbol.getCrossTileID())) continue;
        JointPlacement placement = placeSymbol(symbol, ctx);
        if (shouldRetryPlacement(placement, ctx)) continue;
        seenCrossTileIDs.insert(symbol.getCrossTileID());
    }
    // Place the rest labels.
    populateIntersections = false;
    for (auto it = layers.crbegin(); it != layers.crend(); ++it) {
        placeLayer(*it, seenCrossTileIDs);
    }
    commit();
}

std::optional<CollisionBoundaries> TilePlacement::getAvoidEdges(const SymbolBucket& bucket, const mat4& posMatrix) {
    tileBorders = collisionIndex.projectTileBoundaries(posMatrix);
    const auto& layout = *bucket.layout;
    if (layout.get<style::SymbolAvoidEdges>() ||
        layout.get<style::SymbolPlacement>() == style::SymbolPlacementType::Line) {
        return tileBorders;
    }
    return std::nullopt;
}

void TilePlacement::placeSymbolBucket(const BucketPlacementData& params, std::set<uint32_t>& seen) {
    assert(updateParameters);
    const auto& bucket = static_cast<const SymbolBucket&>(params.bucket.get());
    const auto& layout = *bucket.layout;
    if (!populateIntersections) {
        Placement::placeSymbolBucket(params, seen);
        return;
    }
    if (layout.get<SymbolPlacement>() != SymbolPlacementType::Point || layout.get<SymbolAvoidEdges>()) {
        // Collect intersection only for point placement.
        return;
    }
    const RenderTile& renderTile = params.tile;
    PlacementContext ctx{bucket,
                         params.tile,
                         collisionIndex.getTransformState(),
                         placementZoom,
                         collisionGroups.get(params.sourceId),
                         getAvoidEdges(bucket, renderTile.matrix)};

    // In this case we first try to place symbols, which intersects the tile
    // borders, so that those symbols will remain even if each tile is handled
    // independently.
    SymbolInstanceReferences symbolInstances = getBucketSymbols(
        bucket, params.sortKeyRange, collisionIndex.getTransformState().getBearing());

    // Keeps the data necessary to find a feature location according to a tile.
    struct NeighborTileData {
        NeighborTileData(const CollisionIndex& collisionIndex, UnwrappedTileID id_, Point<float> shift_)
            : id(id_),
              shift(shift_),
              matrix() {
            collisionIndex.getTransformState().matrixFor(matrix, id);
            matrix::multiply(matrix, collisionIndex.getTransformState().getProjectionMatrix(), matrix);
            borders = collisionIndex.projectTileBoundaries(matrix);
        }

        UnwrappedTileID id;
        Point<float> shift;
        mat4 matrix;
        CollisionBoundaries borders;
    };

    uint8_t z = renderTile.id.canonical.z;
    uint32_t x = renderTile.id.canonical.x;
    uint32_t y = renderTile.id.canonical.y;
    const std::array<NeighborTileData, 4> neighbours{{
        {collisionIndex, UnwrappedTileID(z, x, y - 1), {0.0f, util::EXTENT}},  // top
        {collisionIndex, UnwrappedTileID(z, x, y + 1), {0.0f, -util::EXTENT}}, // bottom
        {collisionIndex, UnwrappedTileID(z, x - 1, y), {util::EXTENT, 0.0f}},  // left
        {collisionIndex, UnwrappedTileID(z, x + 1, y), {-util::EXTENT, 0.0f}}  // right
    }};

    auto collisionBoxIntersectsTileEdges = [&](const CollisionBox& collisionBox,
                                               Point<float> shift) noexcept -> IntersectStatus {
        IntersectStatus intersects = collisionIndex.intersectsTileEdges(
            collisionBox, shift, renderTile.matrix, ctx.pixelRatio, *tileBorders);
        // Check if this symbol intersects the neighbor tile borders. If so, it
        // also shall be placed with priority.
        for (const auto& neighbor : neighbours) {
            if (intersects.flags != IntersectStatus::None) break;
            intersects = collisionIndex.intersectsTileEdges(
                collisionBox, shift + neighbor.shift, neighbor.matrix, ctx.pixelRatio, neighbor.borders);
        }
        return intersects;
    };

    auto symbolIntersectsTileEdges = [&collisionBoxIntersectsTileEdges,
                                      pitchTextWithMap = ctx.pitchTextWithMap,
                                      rotateTextWithMap = ctx.rotateTextWithMap,
                                      variableIconPlacement = ctx.hasIconTextFit && !ctx.iconAllowOverlap,
                                      bearing = static_cast<float>(ctx.getTransformState().getBearing())](
                                         const SymbolInstance& symbol) noexcept -> IntersectStatus {
        IntersectStatus result;
        std::optional<style::TextVariableAnchorType> variableAnchor;
        auto textVariableAnchorOffset = symbol.getTextVariableAnchorOffset();
        if (textVariableAnchorOffset && !textVariableAnchorOffset->empty()) {
            variableAnchor = textVariableAnchorOffset->begin()->anchorType;
        }

        if (!symbol.getTextCollisionFeature().boxes.empty()) {
            const auto& textCollisionBox = symbol.getTextCollisionFeature().boxes.front();

            Point<float> offset{};
            if (variableAnchor) {
                float width = textCollisionBox.x2 - textCollisionBox.x1;
                float height = textCollisionBox.y2 - textCollisionBox.y1;

                auto variableTextOffset = textVariableAnchorOffset->getOffsetByAnchor(*variableAnchor);

                offset = calculateVariableLayoutOffset(*variableAnchor,
                                                       width,
                                                       height,
                                                       variableTextOffset,
                                                       symbol.getTextBoxScale(),
                                                       rotateTextWithMap,
                                                       pitchTextWithMap,
                                                       bearing);
            }
            result = collisionBoxIntersectsTileEdges(textCollisionBox, offset);
        }

        if (!symbol.getIconCollisionFeature().boxes.empty()) {
            const auto& iconCollisionBox = symbol.getIconCollisionFeature().boxes.front();
            Point<float> offset{};
            if (variableAnchor && variableIconPlacement) {
                float width = iconCollisionBox.x2 - iconCollisionBox.x1;
                float height = iconCollisionBox.y2 - iconCollisionBox.y1;

                auto variableTextOffset = textVariableAnchorOffset->getOffsetByAnchor(*variableAnchor);

                offset = calculateVariableLayoutOffset(*variableAnchor,
                                                       width,
                                                       height,
                                                       variableTextOffset,
                                                       symbol.getTextBoxScale(),
                                                       rotateTextWithMap,
                                                       pitchTextWithMap,
                                                       bearing);
            }
            auto iconIntersects = collisionBoxIntersectsTileEdges(iconCollisionBox, offset);
            result.flags |= iconIntersects.flags;
            result.minSectionLength = std::max(result.minSectionLength, iconIntersects.minSectionLength);
        }

        return result;
    };

    for (const SymbolInstance& symbol : symbolInstances) {
        if (!symbol.check(SYM_GUARD_LOC)) {
            continue;
        }
        const auto intersectStatus = symbolIntersectsTileEdges(symbol);
        if (intersectStatus.flags == IntersectStatus::None) {
            continue;
        }
        intersections.emplace_back(symbol, ctx, intersectStatus, currentIntersectionPriority);
    }

    ++currentIntersectionPriority;
}

bool TilePlacement::canPlaceAtVariableAnchor(const CollisionBox& box,
                                             TextVariableAnchorType anchor,
                                             Point<float> shift,
                                             std::vector<style::TextVariableAnchorType>& anchors,
                                             const mat4& posMatrix,
                                             float textPixelRatio) {
    assert(tileBorders);
    if (populateIntersections) {
        // A variable label is only allowed to intersect tile border with the first anchor.
        if (anchor == anchors.front()) {
            // Check, that the label would intersect the tile borders even
            // without shift, otherwise intersection is not allowed (preventing
            // cut-offs in case the shift is lager than the buffer size).
            auto status = collisionIndex.intersectsTileEdges(box, {}, posMatrix, textPixelRatio, *tileBorders);
            if (status.flags != IntersectStatus::None) return true;
        }
        // The most important labels shall be placed first anyway, so we continue trying
        // the following variable anchors for them; less priority labels
        // will wait for the second round (when `populateIntersections` is `false`).
        if (currentIntersectionPriority > 0u) return false;
    }
    // Can be placed, if it does not intersect tile borders.
    auto status = collisionIndex.intersectsTileEdges(box, shift, posMatrix, textPixelRatio, *tileBorders);
    return (status.flags == IntersectStatus::None);
}

void TilePlacement::newSymbolPlaced(const SymbolInstance& symbol,
                                    const PlacementContext& ctx,
                                    const JointPlacement& placement,
                                    style::SymbolPlacementType placementType,
                                    float evaluatedTextSize,
                                    float evaluatedIconSize,
                                    const std::vector<ProjectedCollisionBox>& textCollisionBoxes,
                                    const std::vector<ProjectedCollisionBox>& iconCollisionBoxes) {
    if (!collectData || placementType != style::SymbolPlacementType::Point || shouldRetryPlacement(placement, ctx))
        return;

    std::optional<mapbox::geometry::box<float>> textCollisionBox;
    if (!textCollisionBoxes.empty()) {
        assert(textCollisionBoxes.size() == 1u);
        auto& box = textCollisionBoxes.front();
        assert(box.isBox());
        textCollisionBox = box.box();
    }
    std::optional<mapbox::geometry::box<float>> iconCollisionBox;
    if (!iconCollisionBoxes.empty()) {
        assert(iconCollisionBoxes.size() == 1u);
        auto& box = iconCollisionBoxes.front();
        assert(box.isBox());
        iconCollisionBox = box.box();
    }
    const float viewportPadding = collisionIndex.getViewportPadding();
    const auto anchorPoint = collisionIndex.projectPoint(ctx.getRenderTile().matrix, symbol.getAnchor().point);
    const auto anchorLatLng = ctx.getTransformState().screenCoordinateToLatLng(
        ScreenCoordinate{anchorPoint.x - viewportPadding,
                         ctx.getTransformState().getSize().height - (anchorPoint.y - viewportPadding)},
        LatLng::Wrapped);
    PlacedSymbolData symbolData{.key = symbol.getKey(),
                                .lineBrokenText = symbol.getLineBrokenText(),
                                .crossTileID = symbol.getCrossTileID(),
                                .textCollisionBox = textCollisionBox,
                                .iconCollisionBox = iconCollisionBox,
                                .textPlaced = placement.text,
                                .iconPlaced = placement.icon,
                                .intersectsTileBorder = !placement.skipFade && populateIntersections,
                                .viewportPadding = viewportPadding,
                                .anchorPoint = anchorPoint,
                                .anchorLatLng = anchorLatLng,
                                .layer = ctx.getBucket().bucketLeaderID,
                                .renderGroup = currentRenderGroup,
                                .renderOrder = currentRenderOrder,
                                .icon = symbol.getIconImageID(),
                                .textSize = evaluatedTextSize,
                                .iconSize = evaluatedIconSize};
    placedSymbolsData.emplace_back(std::move(symbolData));
}

bool TilePlacement::shouldRetryPlacement(const JointPlacement& placement, const PlacementContext& ctx) {
    // We re-try the placement to try out remaining variable anchors.
    return populateIntersections && !placement.placed() && ctx.getBucket().hasVariableTextAnchors();
}

// static
Mutable<Placement> Placement::create(std::shared_ptr<const UpdateParameters> updateParameters_,
                                     std::optional<Immutable<Placement>> prevPlacement) {
    MLN_TRACE_FUNC();
    assert(updateParameters_);
    switch (updateParameters_->mode) {
        case MapMode::Continuous:
            assert(prevPlacement);
            return makeMutable<Placement>(std::move(updateParameters_), std::move(prevPlacement));
        case MapMode::Static:
            return staticMutableCast<Placement>(makeMutable<StaticPlacement>(std::move(updateParameters_)));
        case MapMode::Tile:
            return staticMutableCast<Placement>(makeMutable<TilePlacement>(std::move(updateParameters_)));
    }
    assert(false);
    return makeMutable<Placement>();
}

} // namespace mbgl
