#include "tagreader.h"
#include "tagwriter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace linuxbee {

namespace {

// R-RATING anchor table: ID3v2 POPM byte and the stars it stands for.
struct Anchor {
    int popm;
    double stars;
};

constexpr std::array<Anchor, 9> kAnchors { {
    { 13, 0.5 },
    { 54, 1.5 },
    { 64, 2.0 },
    { 118, 2.5 },
    { 128, 3.0 },
    { 186, 3.5 },
    { 196, 4.0 },
    { 242, 4.5 },
    { 255, 5.0 },
} };

// Rounds to the nearest half; a quarter rounds up.
double roundToHalf(double stars)
{
    return std::floor(stars * 2.0 + 0.5) / 2.0;
}

}

std::optional<double> ratingFromPopm(int popmValue)
{
    if (popmValue < 1 || popmValue > 255)
        return std::nullopt;
    if (popmValue == 1)
        return 1.0;

    // Nearest anchor. Anchors ascend, so a tie (<=) moves to the higher one.
    const Anchor *best = &kAnchors.front();
    int bestDistance = std::abs(popmValue - best->popm);
    for (const Anchor &anchor : kAnchors) {
        const int distance = std::abs(popmValue - anchor.popm);
        if (distance <= bestDistance) {
            best = &anchor;
            bestDistance = distance;
        }
    }
    return best->stars;
}

std::optional<double> ratingFromText(const QString &text)
{
    bool parsed = false;
    const double number = text.trimmed().toDouble(&parsed);
    if (!parsed || !std::isfinite(number) || number <= 0.0 || number > 100.0)
        return std::nullopt;

    // R-RATING: up to 5 the number is stars; above 5 it is a percentage.
    const double stars = number <= 5.0 ? number : number / 20.0;
    // The least rating is a half star, so a tiny positive number is not "none".
    return std::max(0.5, roundToHalf(stars));
}

QString ratingToString(double stars)
{
    if (std::floor(stars) == stars)
        return QString::number(static_cast<qint64>(stars));
    return QString::number(stars, 'f', 1);
}

// W-RATING: the POPM byte for each half star, 0.5 first. Anything that is not
// a whole number of half stars from 0.5 to 5 gives 0.
int popmFromRating(double stars)
{
    static constexpr std::array<int, 10> kBytes { 13, 1, 54, 64, 118, 128, 186, 196, 242, 255 };
    const double halves = stars * 2.0;
    if (!(halves >= 1.0 && halves <= 10.0) || halves != std::floor(halves))
        return 0;
    return kBytes[static_cast<std::size_t>(halves) - 1];
}

}
