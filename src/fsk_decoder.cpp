#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <string>
#include <cstdint>

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
// GOERTZEL
// ----------------------
double goertzelPower(const std::vector<double>& data, int start, int len,
                     double targetFreq, int sampleRate)
{
    if (len <= 0) return 0.0;

    double kf = 0.5 + (len * targetFreq) / sampleRate;
    int k = static_cast<int>(kf);
    double omega = (2.0 * 3.14159265358979323846 * k) / len;
    double coeff = 2.0 * std::cos(omega);

    double s0 = 0, s1 = 0, s2 = 0;

    for (int n = 0; n < len; ++n) {
        double x = data[start + n];
        s0 = x + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
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
    const double f0 = 35000.0;
    const double f1 = 45000.0;
    const double bitDuration = 0.005;

    const std::string SYNC = "1111000011110000";

    WavData wav;
    if (!loadWav(inputFile, wav)) return 1;

    int sampleRate = wav.sampleRate;
    int samplesPerBit = static_cast<int>(bitDuration * sampleRate);

    std::cout << "Loaded WAV with " << wav.samples.size() << " samples.\n";

    // Extract raw bitstream
    std::string bitstream;
    int numBits = wav.samples.size() / samplesPerBit;
    bitstream.reserve(numBits);

    for (int b = 0; b < numBits; b++) {
        int start = b * samplesPerBit;
        if (start + samplesPerBit > wav.samples.size()) break;

        double p0 = goertzelPower(wav.samples, start, samplesPerBit, f0, sampleRate);
        double p1 = goertzelPower(wav.samples, start, samplesPerBit, f1, sampleRate);

        bitstream.push_back((p1 > p0) ? '1' : '0');
    }

    std::cout << "Total bits recovered: " << bitstream.size() << "\n";

    // Find sync word
    std::size_t pos = bitstream.find(SYNC);
    if (pos == std::string::npos) {
        std::cerr << "SYNC WORD NOT FOUND – noise or bad signal.\n";
        return 1;
    }
    std::cout << "Sync word found at bit index: " << pos << "\n";

    // Extract length field (next 16 bits)
    if (pos + SYNC.size() + 16 > bitstream.size()) {
        std::cerr << "Bitstream too short for length.\n";
        return 1;
    }

    std::string lenBits = bitstream.substr(pos + SYNC.size(), 16);
    uint16_t msgLen = static_cast<uint16_t>(std::stoi(lenBits, nullptr, 2));

    std::cout << "Payload length = " << msgLen << " bytes\n";

    // Extract payload bits
    std::size_t payloadStart = pos + SYNC.size() + 16;
    std::size_t payloadBitsNeeded = msgLen * 8;

    if (payloadStart + payloadBitsNeeded > bitstream.size()) {
        std::cerr << "Bitstream too short for expected payload.\n";
        return 1;
    }

    std::string payloadBits = bitstream.substr(payloadStart, payloadBitsNeeded);
    std::string message = bitsToText(payloadBits);

    std::cout << "\nDECODED MESSAGE:\n" << message << "\n";

    std::ofstream out("decoded.txt");
    out << message;
    out.close();

    return 0;
}
