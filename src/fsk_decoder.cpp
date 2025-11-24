#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <string>
#include <cstdint>
#include <algorithm>

#include "kiss_fft.h"

constexpr double PI = 3.14159265358979323846;

// ----------------------
// WAV LOADER
// ----------------------
struct WavData {
    int sampleRate = 0;
    std::vector<double> samples;
};

bool loadWav(const std::string& filename, WavData& out) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open WAV file: " << filename << "\n";
        return false;
    }

    char riff[4];
    file.read(riff, 4);
    if (std::string(riff, 4) != "RIFF") {
        std::cerr << "Not a RIFF file\n";
        return false;
    }

    std::uint32_t chunkSize = 0;
    file.read(reinterpret_cast<char*>(&chunkSize), 4);

    char wave[4];
    file.read(wave, 4);
    if (std::string(wave, 4) != "WAVE") {
        std::cerr << "Not a WAVE file\n";
        return false;
    }

    bool fmtFound = false;
    bool dataFound = false;
    std::uint32_t dataSize = 0;
    std::uint16_t audioFormat = 0;
    std::uint16_t numChannels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;

    while (file && (!fmtFound || !dataFound)) {
        char chunkId[4];
        if (!file.read(chunkId, 4)) break;

        std::uint32_t subchunkSize = 0;
        file.read(reinterpret_cast<char*>(&subchunkSize), 4);
        std::string id(chunkId, 4);

        if (id == "fmt ") {
            fmtFound = true;
            file.read(reinterpret_cast<char*>(&audioFormat), 2);
            file.read(reinterpret_cast<char*>(&numChannels), 2);
            file.read(reinterpret_cast<char*>(&sampleRate), 4);

            std::uint32_t byteRate;
            std::uint16_t blockAlign;
            file.read(reinterpret_cast<char*>(&byteRate), 4);
            file.read(reinterpret_cast<char*>(&blockAlign), 2);
            file.read(reinterpret_cast<char*>(&bitsPerSample), 2);

            if (subchunkSize > 16)
                file.seekg(subchunkSize - 16, std::ios::cur);

        } else if (id == "data") {
            dataFound = true;
            dataSize = subchunkSize;
            break;

        } else {
            file.seekg(subchunkSize, std::ios::cur);
        }
    }

    if (!fmtFound || !dataFound) {
        std::cerr << "Invalid WAV: missing fmt or data chunk\n";
        return false;
    }

    if (audioFormat != 1) {
        std::cerr << "Only PCM WAV supported\n";
        return false;
    }
    if (numChannels != 1) {
        std::cerr << "Only mono WAV supported\n";
        return false;
    }
    if (bitsPerSample != 16) {
        std::cerr << "Only 16-bit WAV supported\n";
        return false;
    }

    int numSamples = dataSize / 2;
    out.samples.resize(numSamples);

    for (int i = 0; i < numSamples; ++i) {
        std::int16_t sampleInt;
        file.read(reinterpret_cast<char*>(&sampleInt), sizeof(sampleInt));
        out.samples[i] = static_cast<double>(sampleInt) / 32768.0;
    }

    out.sampleRate = sampleRate;
    return true;
}

// ----------------------
// BITS → TEXT
// ----------------------
std::string bitsToText(const std::string& bits) {
    std::string result;
    for (std::size_t i = 0; i + 7 < bits.size(); i += 8) {
        std::string byteStr = bits.substr(i, 8);
        char c = static_cast<char>(std::stoi(byteStr, nullptr, 2));
        result.push_back(c);
    }
    return result;
}

// ----------------------
// MAIN DECODER
// ----------------------
int main() {
    const std::string inputFile = "auxon_fsk.wav";

    // TRANSMIT freqs (what encoder thinks it’s using)
    const double txF0 = 35000.0;
    const double txF1 = 45000.0;
    const double bitDuration = 0.005;

    const std::string SYNC = "1111000011110000";

    WavData wav;
    if (!loadWav(inputFile, wav)) return 1;

    int sampleRate = wav.sampleRate;

    // Compute effective (aliased) frequencies inside the WAV
    auto aliasFreq = [&](double f) -> double {
        double ratio = f / sampleRate;
        double k = std::round(ratio);
        double fa = std::fabs(f - k * sampleRate);
        return fa;
    };

    double f0_dec = aliasFreq(txF0);
    double f1_dec = aliasFreq(txF1);

    int samplesPerBit = static_cast<int>(bitDuration * sampleRate);
    if (samplesPerBit <= 0) {
        std::cerr << "Invalid samplesPerBit\n";
        return 1;
    }

    int FFT_N = samplesPerBit;

    kiss_fft_cfg cfg = kiss_fft_alloc(FFT_N, 0, nullptr, nullptr);
    if (!cfg) {
        std::cerr << "Failed to allocate KissFFT config\n";
        return 1;
    }

    std::cout << "Loaded WAV with " << wav.samples.size() << " samples.\n";
    std::cout << "Sample rate: " << sampleRate << " Hz\n";
    std::cout << "Samples per bit: " << samplesPerBit << "\n";
    std::cout << "FFT size per bit: " << FFT_N << "\n";
    std::cout << "Decoded carrier freqs (alias): f0=" << f0_dec
              << " Hz, f1=" << f1_dec << " Hz\n";

    // ----------------------
    // FSK BIT DETECTION USING FFT
    // ----------------------
    auto detectBitFFT = [&](int start) -> char {
        std::vector<kiss_fft_cpx> fft_in(FFT_N);
        std::vector<kiss_fft_cpx> fft_out(FFT_N);

        int remaining = static_cast<int>(wav.samples.size()) - start;
        int copySize = std::min(remaining, FFT_N);

        for (int i = 0; i < copySize; ++i) {
            double w = 0.5 * (1.0 - std::cos(2.0 * PI * i / (copySize - 1)));
            fft_in[i].r = wav.samples[start + i] * w;
            fft_in[i].i = 0.0;
        }
        for (int i = copySize; i < FFT_N; ++i) {
            fft_in[i].r = 0.0;
            fft_in[i].i = 0.0;
        }

        kiss_fft(cfg, fft_in.data(), fft_out.data());

        int bin0 = static_cast<int>(std::round(f0_dec * FFT_N / sampleRate));
        int bin1 = static_cast<int>(std::round(f1_dec * FFT_N / sampleRate));

        if (bin0 < 0 || bin0 >= FFT_N || bin1 < 0 || bin1 >= FFT_N) {
            return '0';
        }

        double mag0 = std::hypot(fft_out[bin0].r, fft_out[bin0].i);
        double mag1 = std::hypot(fft_out[bin1].r, fft_out[bin1].i);

        return (mag1 > mag0) ? '1' : '0';
    };

    // Extract raw bitstream
    std::string bitstream;
    int numBits = static_cast<int>(wav.samples.size()) / samplesPerBit;
    bitstream.reserve(numBits);

    for (int b = 0; b < numBits; b++) {
        int start = b * samplesPerBit;
        if (start >= static_cast<int>(wav.samples.size())) break;

        char bit = detectBitFFT(start);
        bitstream.push_back(bit);
    }

    std::cout << "Total bits recovered: " << bitstream.size() << "\n";

    // Find sync word
    std::size_t pos = bitstream.find(SYNC);
    if (pos == std::string::npos) {
        std::cerr << "SYNC WORD NOT FOUND – noise or bad signal.\n";
        kiss_fft_free(cfg);
        return 1;
    }
    std::cout << "Sync word found at bit index: " << pos << "\n";

    if (pos + SYNC.size() + 16 > bitstream.size()) {
        std::cerr << "Bitstream too short for length.\n";
        kiss_fft_free(cfg);
        return 1;
    }

    std::string lenBits = bitstream.substr(pos + SYNC.size(), 16);
    uint16_t msgLen = static_cast<uint16_t>(std::stoi(lenBits, nullptr, 2));

    std::cout << "Payload length = " << msgLen << " bytes\n";

    std::size_t payloadStart = pos + SYNC.size() + 16;
    std::size_t payloadBitsNeeded = msgLen * 8;

    if (payloadStart + payloadBitsNeeded > bitstream.size()) {
        std::cerr << "Bitstream too short for expected payload.\n";
        kiss_fft_free(cfg);
        return 1;
    }

    std::string payloadBits = bitstream.substr(payloadStart, payloadBitsNeeded);
    std::string message = bitsToText(payloadBits);

    std::cout << "\nDECODED MESSAGE:\n" << message << "\n";

    std::ofstream out("decoded.txt");
    out << message;
    out.close();

    kiss_fft_free(cfg);
    return 0;
}
