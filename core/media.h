// Media files (photos, videos, audio) on storage, plus a tiny BMP codec used for the SIMULATED camera.
// Listing is by file name (storage has no timestamps yet), newest-first for names like IMG_0007.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "services.h"

namespace fai {

enum class MediaKind { Image, Video, Audio, Other };
MediaKind mediaKind(const std::string& fileName);  // by extension, case-insensitive

struct MediaFile {
  std::string key;   // storage key, e.g. "DCIM/IMG_0003.bmp"
  std::string name;  // file name
  MediaKind kind;
};

constexpr const char* kPhotoDir = "DCIM/";
constexpr const char* kVideoDir = "Videos/";
constexpr const char* kMusicDir = "Music/";
constexpr const char* kRecDir = "Recordings/";

// Images from DCIM/ then videos from Videos/. Within a folder, names sort descending (IMG_0010 before IMG_0009).
std::vector<MediaFile> listGallery(IStorage& s);
std::string nextPhotoName(IStorage& s);  // "IMG_0001.bmp", then the number after the highest existing IMG_nnnn

// Files of one kind in one folder, names sorted descending.
std::vector<MediaFile> listMedia(IStorage& s, const std::string& dir, MediaKind kind);
std::string nextRecordingName(IStorage& s);  // "REC_0001.wav", then highest + 1
// SIMULATED recording: a valid 16-bit mono WAV holding silence (no microphone is assumed). 1..600 s, else "".
std::string makeWav(int seconds, int sampleRate = 8000);  // sampleRate 8000 or 16000, else ""
// SIMULATED video recording: a small text stub (NOT playable video) named VID_nnnn.mp4 so the Gallery and Videos list it.
std::string makeVideoStub(int seconds);
std::string nextVideoName(IStorage& s);  // "VID_0001.mp4", then highest + 1
// Length of a PCM WAV in ms from its header; -1 if it is not a WAV this code understands (other formats: duration unknown).
long long wavDurationMs(const std::string& data);
std::string formatClock(long long ms);  // "m:ss"

// 24-bit uncompressed Windows BMP. makeBmp draws a SIMULATED placeholder image (gradient + disc) from a seed.
std::string makeBmp(int w, int h, uint32_t seed);
// Top-down RGB, 3 bytes per pixel. Rejects anything it does not fully understand (compression, odd sizes, truncation).
bool parseBmp(const std::string& data, int& w, int& h, std::vector<uint8_t>& rgb);

}  // namespace fai
