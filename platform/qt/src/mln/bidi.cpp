#include <mln/text/bidi.hpp>

#include <QtCore/QChar>

#include <memory>

namespace mln {

// This stub implementation is stateless and doesn't implement the private
// methods used by the ICU BiDi
class BiDiImpl {
    // Used by the ICU implementation to hold onto internal BiDi state
};

std::u16string applyArabicShaping(const std::u16string& input) {
    // Qt does not support raw Arabic shaping, so we just return the input
    return input;
}

BiDi::BiDi()
    : impl(std::make_unique<BiDiImpl>()) {}

BiDi::~BiDi() = default;

std::vector<std::u16string> BiDi::processText(const std::u16string& input,
                                              std::set<std::size_t> lineBreakPoints,
                                              std::vector<std::u16string>* logicalLines) {
    lineBreakPoints.insert(input.length());

    std::vector<std::u16string> transformedLines;
    std::size_t start = 0;
    for (std::size_t lineBreakPoint : lineBreakPoints) {
        transformedLines.push_back(input.substr(start, lineBreakPoint - start));
        start = lineBreakPoint;
    }

    if (logicalLines) *logicalLines = transformedLines;

    return transformedLines;
}

std::vector<StyledText> BiDi::processStyledText(const StyledText& input,
                                                std::set<std::size_t> lineBreakPoints,
                                                std::vector<StyledText>* logicalLines) {
    lineBreakPoints.insert(input.first.length());

    std::vector<StyledText> transformedLines;
    std::size_t start = 0;
    for (std::size_t lineBreakPoint : lineBreakPoints) {
        if (lineBreakPoint <= input.second.size()) {
            transformedLines.emplace_back(
                input.first.substr(start, lineBreakPoint - start),
                std::vector<uint8_t>(input.second.begin() + start, input.second.begin() + lineBreakPoint));
            start = lineBreakPoint;
        }
    }

    if (logicalLines) *logicalLines = transformedLines;

    return transformedLines;
}

bool BiDi::isRTL(const std::u16string& input) const {
    for (std::size_t i = 0; i < input.size(); ++i) {
        char32_t codePoint = input[i];
        if (codePoint >= 0xd800 && codePoint <= 0xdbff && i + 1 < input.size()) {
            const auto low = input[i + 1];
            if (low >= 0xdc00 && low <= 0xdfff) {
                codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (low - 0xdc00);
                ++i;
            }
        }
        const auto direction = QChar::direction(static_cast<uint>(codePoint));
        if (direction == QChar::DirR || direction == QChar::DirAL) {
            return true;
        }
        if (direction == QChar::DirL) {
            return false;
        }
    }

    return false;
}

} // end namespace mln
