#include "../src/JPEGdecoder.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

bool diagnosticDebug = false;
SerialStub Serial;

static std::vector<uint8_t> readFixture(const char* name) {
  const std::string path = std::string("tests/fixtures/") + name;
  std::ifstream input(path.c_str(), std::ios::binary);
  assert(input.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
}

static size_t findMarker(const std::vector<uint8_t>& data, uint8_t marker) {
  for (size_t i = 0; i + 1U < data.size(); ++i)
    if (data[i] == 0xFFU && data[i + 1U] == marker) return i;
  return data.size();
}

static size_t findNthMarker(const std::vector<uint8_t>& data, uint8_t marker,
                            unsigned occurrence) {
  for (size_t i = 0; i + 1U < data.size(); ++i) {
    if (data[i] == 0xFFU && data[i + 1U] == marker) {
      if (occurrence == 0U) return i;
      --occurrence;
    }
  }
  return data.size();
}

static void expectBaseline(const char* name, uint8_t components,
                           uint8_t hSampling, uint8_t vSampling,
                           bool expectRestart, uint16_t expectedWidth = 16U,
                           uint16_t expectedHeight = 16U) {
  std::vector<uint8_t> jpeg = readFixture(name);
  JPEGImageInfo info;
  const JPEGPreflightResult preflight =
      JPEGpreflight(jpeg.data(), jpeg.size(), 320, 240, info);
  if (preflight != JPEGPreflightResult::SupportedBaseline)
    std::cerr << name << ": " << JPEGpreflightName(preflight) << "\n";
  assert(preflight == JPEGPreflightResult::SupportedBaseline);
  assert(info.app0 && info.sof0 && !info.sof2);
  assert(info.width == expectedWidth && info.height == expectedHeight);
  assert(info.components == components);
  assert(info.maxHorizontalSampling == hSampling);
  assert(info.maxVerticalSampling == vSampling);
  assert((info.restartInterval != 0U) == expectRestart);

  std::vector<uint8_t> workspace(76800U);
  assert(JPEGvalidate(jpeg.data(), jpeg.size(), 320, 240,
                      workspace.data(), workspace.size(), &info));
  assert(info.lastRenderedMcuRow == -1);

  TFT_eSPI display;
  assert(JPEGdecoder(jpeg.data(), jpeg.size(), display, 320, 240,
                     workspace.data(), workspace.size(), &info));
  assert(display.pushedLines == expectedHeight);
  assert(display.pushedPixels ==
         static_cast<int>(expectedWidth) * static_cast<int>(expectedHeight));
  assert(info.lastRenderedMcuRow >= 0);
  assert(!display.pixels.empty());
  assert(std::adjacent_find(display.pixels.begin(), display.pixels.end(),
                            std::not_equal_to<uint16_t>()) !=
         display.pixels.end());
}

static void expectProgressive(const char* progressiveName,
                              const char* baselineName,
                              uint8_t components,
                              uint8_t hSampling,
                              uint8_t vSampling,
                              bool expectRestart,
                              uint16_t width = 16U,
                              uint16_t height = 16U,
                              uint8_t expectedScale = 1U) {
  std::vector<uint8_t> jpeg = readFixture(progressiveName);
  JPEGImageInfo info;
  assert(jpeg.size() >= 4U && jpeg[0] == 0xFFU && jpeg[1] == 0xD8U &&
         jpeg[2] == 0xFFU && jpeg[3] == 0xE0U);
  assert(JPEGpreflight(jpeg.data(), jpeg.size(), 320, 240, info) ==
         JPEGPreflightResult::SupportedProgressive);
  assert(info.app0 && info.sof2 && info.scans > 1U);
  assert(info.width == width && info.height == height);
  assert(info.components == components);
  assert(info.maxHorizontalSampling == hSampling);
  assert(info.maxVerticalSampling == vSampling);
  assert((info.restartInterval != 0U) == expectRestart);
  assert(info.scaleDivisor == expectedScale);

  std::vector<uint8_t> workspace(76800U);
  assert(JPEGvalidate(jpeg.data(), jpeg.size(), 320, 240,
                      workspace.data(), workspace.size(), &info));
  assert(info.lastRenderedMcuRow == -1);

  TFT_eSPI progressiveDisplay;
  assert(JPEGdecoder(jpeg.data(), jpeg.size(), progressiveDisplay, 320, 240,
                     workspace.data(), workspace.size(), &info));
  const int outputWidth = (width + expectedScale - 1U) / expectedScale;
  const int outputHeight = (height + expectedScale - 1U) / expectedScale;
  assert(progressiveDisplay.pushedLines == outputHeight);
  assert(progressiveDisplay.pushedPixels ==
         outputWidth * outputHeight);
  assert(info.lastRenderedMcuRow >= 0);

  std::vector<uint8_t> baseline = readFixture(baselineName);
  TFT_eSPI baselineDisplay;
  assert(JPEGdecoder(baseline.data(), baseline.size(), baselineDisplay,
                     320, 240, workspace.data(), workspace.size()));
  assert(progressiveDisplay.pixels == baselineDisplay.pixels);
}

static void testAlphaPngFixture() {
  const std::vector<uint8_t> png = readFixture("alpha.png");
  static const uint8_t signature[8] =
      {0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
  assert(png.size() > 33U);
  assert(std::equal(signature, signature + 8, png.begin()));
  assert(png[12] == 'I' && png[13] == 'H' && png[14] == 'D' && png[15] == 'R');
  assert(png[18] == 0U && png[19] == 16U);
  assert(png[22] == 0U && png[23] == 16U);
  assert(png[25] == 6U);  // RGBA
  assert(png[png.size() - 8U] == 'I' && png[png.size() - 7U] == 'E' &&
         png[png.size() - 6U] == 'N' && png[png.size() - 5U] == 'D');
}

static void testCorruptAndUnsupportedInputs() {
  const std::vector<uint8_t> original = readFixture("baseline_444.jpg");
  std::vector<uint8_t> workspace(76800U);
  JPEGImageInfo info;

  std::vector<uint8_t> earlyEoi = original;
  const size_t sos = findMarker(earlyEoi, 0xDAU);
  assert(sos < earlyEoi.size());
  earlyEoi.resize(sos + 24U);
  earlyEoi.push_back(0xFFU);
  earlyEoi.push_back(0xD9U);
  assert(JPEGpreflight(earlyEoi.data(), earlyEoi.size(), 320, 240, info) ==
         JPEGPreflightResult::SupportedBaseline);
  assert(!JPEGvalidate(earlyEoi.data(), earlyEoi.size(), 320, 240,
                       workspace.data(), workspace.size(), &info));

  std::vector<uint8_t> badDht = original;
  const size_t dht = findMarker(badDht, 0xC4U);
  assert(dht + 21U < badDht.size());
  badDht[dht + 5U] = 0xFFU;
  badDht[dht + 6U] = 0xFFU;
  assert(!JPEGvalidate(badDht.data(), badDht.size(), 320, 240,
                       workspace.data(), workspace.size(), &info));

  std::vector<uint8_t> badDqt = original;
  const size_t dqt = findMarker(badDqt, 0xDBU);
  assert(dqt + 4U < badDqt.size());
  badDqt[dqt + 4U] = 0x20U;  // unsupported 2-byte precision selector value
  assert(!JPEGvalidate(badDqt.data(), badDqt.size(), 320, 240,
                       workspace.data(), workspace.size(), &info));

  std::vector<uint8_t> oversized = original;
  const size_t sof = findMarker(oversized, 0xC0U);
  assert(sof + 8U < oversized.size());
  oversized[sof + 7U] = 0x02U;
  oversized[sof + 8U] = 0x81U;  // width 641; still too wide at 50%
  assert(JPEGpreflight(oversized.data(), oversized.size(), 320, 240, info) ==
         JPEGPreflightResult::UnsupportedDimensions);

  std::vector<uint8_t> multiscan = original;
  const size_t multiSos = findMarker(multiscan, 0xDAU);
  assert(multiSos + 14U < multiscan.size());
  multiscan[multiSos + 2U] = 0x00U;
  multiscan[multiSos + 3U] = 0x08U;
  multiscan[multiSos + 4U] = 0x01U;
  multiscan.erase(multiscan.begin() + static_cast<std::ptrdiff_t>(multiSos + 7U),
                  multiscan.begin() + static_cast<std::ptrdiff_t>(multiSos + 11U));
  assert(JPEGpreflight(multiscan.data(), multiscan.size(), 320, 240, info) ==
         JPEGPreflightResult::UnsupportedMultiscan);

  const uint8_t fourComponentBytes[] = {
      0xFF, 0xD8,
      0xFF, 0xC0, 0x00, 0x14, 0x08, 0x00, 0x01, 0x00, 0x01, 0x04,
      0x01, 0x11, 0x00, 0x02, 0x11, 0x00,
      0x03, 0x11, 0x00, 0x04, 0x11, 0x00,
      0xFF, 0xDA, 0x00, 0x0E, 0x04,
      0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00,
      0x00, 0x3F, 0x00, 0x00, 0xFF, 0xD9};
  const std::vector<uint8_t> fourComponents(
      fourComponentBytes,
      fourComponentBytes + sizeof(fourComponentBytes));
  assert(JPEGpreflight(fourComponents.data(), fourComponents.size(),
                       320, 240, info) ==
         JPEGPreflightResult::UnsupportedComponents);

  const std::vector<uint8_t> progressive =
      readFixture("progressive_app0.jpg");
  std::vector<uint8_t> truncatedProgressive = progressive;
  const size_t progressiveSos = findMarker(truncatedProgressive, 0xDAU);
  assert(progressiveSos + 4U < truncatedProgressive.size());
  const uint16_t progressiveSosLength = static_cast<uint16_t>(
      (truncatedProgressive[progressiveSos + 2U] << 8) |
       truncatedProgressive[progressiveSos + 3U]);
  truncatedProgressive.resize(progressiveSos + 2U + progressiveSosLength);
  truncatedProgressive.push_back(0xFFU);
  truncatedProgressive.push_back(0xD9U);
  assert(JPEGpreflight(truncatedProgressive.data(),
                       truncatedProgressive.size(), 320, 240, info) ==
         JPEGPreflightResult::SupportedProgressive);
  assert(!JPEGvalidate(truncatedProgressive.data(),
                       truncatedProgressive.size(), 320, 240,
                       workspace.data(), workspace.size(), &info));

  std::vector<uint8_t> invalidRefinement = progressive;
  const size_t secondProgressiveSos =
      findNthMarker(invalidRefinement, 0xDAU, 1U);
  assert(secondProgressiveSos + 5U < invalidRefinement.size());
  const uint8_t scanComponents = invalidRefinement[secondProgressiveSos + 4U];
  const size_t approximationOffset =
      secondProgressiveSos + 4U + 1U + 2U * scanComponents + 2U;
  assert(approximationOffset < invalidRefinement.size());
  invalidRefinement[approximationOffset] = 0x20U;  // Ah=2, Al=0 skips Al=1
  assert(JPEGpreflight(invalidRefinement.data(), invalidRefinement.size(),
                       320, 240, info) ==
         JPEGPreflightResult::SupportedProgressive);
  assert(!JPEGvalidate(invalidRefinement.data(), invalidRefinement.size(),
                       320, 240, workspace.data(), workspace.size(), &info));

  std::vector<uint8_t> badProgressiveRestart =
      readFixture("progressive_restart.jpg");
  const size_t firstRestart = findMarker(badProgressiveRestart, 0xD0U);
  assert(firstRestart < badProgressiveRestart.size());
  badProgressiveRestart[firstRestart + 1U] = 0xD7U;
  assert(JPEGpreflight(badProgressiveRestart.data(),
                       badProgressiveRestart.size(), 320, 240, info) ==
         JPEGPreflightResult::SupportedProgressive);
  assert(!JPEGvalidate(badProgressiveRestart.data(),
                       badProgressiveRestart.size(), 320, 240,
                       workspace.data(), workspace.size(), &info));
}

int main() {
  expectBaseline("baseline_420.jpg", 3U, 2U, 2U, false);
  expectBaseline("baseline_422.jpg", 3U, 2U, 1U, false);
  expectBaseline("baseline_444.jpg", 3U, 1U, 1U, false);
  expectBaseline("baseline_gray.jpg", 1U, 1U, 1U, false);
  expectBaseline("baseline_restart.jpg", 3U, 1U, 1U, true);
  expectBaseline("baseline_320x240_420.jpg", 3U, 2U, 2U, false, 320U, 240U);
  expectProgressive("progressive_app0.jpg", "baseline_420.jpg",
                    3U, 2U, 2U, false);
  expectProgressive("progressive_422.jpg", "baseline_422.jpg",
                    3U, 2U, 1U, false);
  expectProgressive("progressive_444.jpg", "baseline_444.jpg",
                    3U, 1U, 1U, false);
  expectProgressive("progressive_gray.jpg", "baseline_gray.jpg",
                    1U, 1U, 1U, false);
  expectProgressive("progressive_restart.jpg", "baseline_restart.jpg",
                    3U, 1U, 1U, true);
  expectProgressive("progressive_17x13_420.jpg", "baseline_17x13_420.jpg",
                    3U, 2U, 2U, false, 17U, 13U);
  expectProgressive("progressive_320x240_app0.jpg",
                    "baseline_320x240_420.jpg",
                    3U, 2U, 2U, false, 320U, 240U);
  expectProgressive("progressive_320x320_420.jpg",
                    "baseline_320x320_420.jpg",
                    3U, 2U, 2U, false, 320U, 320U, 2U);
  testCorruptAndUnsupportedInputs();
  testAlphaPngFixture();
  return 0;
}
