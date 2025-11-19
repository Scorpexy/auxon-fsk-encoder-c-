#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <string>

// ----------------------------------
// THIS GENERATES SIN WAVES
// ----------------------------------
std::vector<double> generateSineWave(double freq, double duration, 
                                     int sampleRate = 44100, double amplitude = 0.5)
{
    int totalSamples = static_cast<int>(duration * sampleRate);
    std::vector<double> samples(totalSamples);

    for (int i = 0; i < totalSamples; i++)
    {
        double t = static_cast<double>(i) / sampleRate;
        samples[i] = amplitude * sin(2.0 * 3.14159265358979323846 * freq * t);
    }

    return samples;
}

// ----------------------------------
// CONVERTS TEXT TO A BINARY STRING
// ----------------------------------
std::string textToBits(const std::string& text)
{
    std::string bits = "";
    for (unsigned char c : text)
    {
        for (int i = 7; i >= 0; i--)
        {
            bits += ((c >> i) & 1) ? '1' : '0';
        }
    }
    return bits;
}

// ----------------------------------
// WRITES TO THE .WAV FILE
// ----------------------------------
void saveWav(const std::string& filename, const std::vector<double>& samples, int sampleRate)
{
    std::ofstream file(filename, std::ios::binary);

    int dataSize = samples.size() * 2; // 16-bit samples
    int fileSize = 36 + dataSize;

    // WAV HEADER
    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&fileSize), 4);
    file.write("WAVE", 4);

    file.write("fmt ", 4);
    int subChunk1Size = 16;
    short audioFormat = 1;
    short numChannels = 1;
    short bitsPerSample = 16;
    int byteRate = sampleRate * numChannels * bitsPerSample / 8;
    short blockAlign = numChannels * bitsPerSample / 8;

    file.write(reinterpret_cast<const char*>(&subChunk1Size), 4);
    file.write(reinterpret_cast<const char*>(&audioFormat), 2);
    file.write(reinterpret_cast<const char*>(&numChannels), 2);
    file.write(reinterpret_cast<const char*>(&sampleRate), 4);
    file.write(reinterpret_cast<const char*>(&byteRate), 4);
    file.write(reinterpret_cast<const char*>(&blockAlign), 2);
    file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&dataSize), 4);

    // Write samples
    for (double s : samples)
    {
        short sampleValue = static_cast<short>(s * 32767);
        file.write(reinterpret_cast<const char*>(&sampleValue), 2);
    }

    file.close();
}

// ----------------------------------
// FSK ENCODER
// ----------------------------------
std::vector<double> fskEncode(const std::string& text,
                              double f0, double f1,
                              double bitDuration,
                              int sampleRate)
{
    std::string bits = textToBits(text);
    std::vector<double> waveData;

    for (char bit : bits)
    {
        double freq = (bit == '1') ? f1 : f0;
        std::vector<double> tone = generateSineWave(freq, bitDuration, sampleRate);

        waveData.insert(waveData.end(), tone.begin(), tone.end());
    }

    return waveData;
}

// ----------------------------------
// MAIN
// ----------------------------------
int main()
{
    std::string message;
    std::cout << "Convert following to audio: ";
    std::getline(std::cin, message);

    std::cout << "Encoding: " << message << "\n";

    double f0 = 35000;      // FREQUENCY FOR 0
    double f1 = 45000;      // FREQUENCY FOR 1
    double bitDuration = 0.005;  // 200 BITS / SEC (DECREASE FOR FASTER BUT LESS RELIABLE)
    int sampleRate = 44100;      // SAMPLE RATE (CHANGE THIS IF NEEDED)

    std::vector<double> samples = fskEncode(message, f0, f1, bitDuration, sampleRate);

    saveWav("auxon_fsk.wav", samples, sampleRate);

    std::cout << "Saved as auxon_fsk.wav\n";
    return 0;
}
