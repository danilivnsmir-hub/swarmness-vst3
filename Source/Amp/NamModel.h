#pragma once

#include <memory>
#include <string>

/**
 * A Neural Amp Modeler capture (.nam), behind a plain C++17 interface: the NAM core
 * (NeuralAmpModelerCore, MIT) needs C++20 and Eigen, so it lives in its own library
 * and nothing else in the project sees its headers.
 *
 * Mono in, mono out, at the model's own sample rate (AmpBlock resamples around it).
 * load() allocates and may take a while: message thread only. process() is real-time safe
 * once reset() was called for the block size.
 */
class NamModel
{
public:
    ~NamModel();

    /** Parses a .nam file's JSON text. Returns nullptr and fills `error` on failure. */
    static std::unique_ptr<NamModel> load (const std::string& jsonText, std::string& error);

    /** Training sample rate (48 kHz for almost every capture). */
    double getSampleRate() const noexcept;
    /** Measured output loudness in dB, when the capture carries it. */
    bool hasLoudness() const noexcept;
    double getLoudnessDb() const noexcept;
    /** "A2", "WaveNet", "LSTM", ... (A2 = the slimmable container NAM uses for Architecture 2) */
    const std::string& getArchitecture() const noexcept { return architecture; }
    /** The reamp level the capture was made at (0 dBFS in = this many dBu), when it carries it. */
    bool hasInputLevel() const noexcept;
    double getInputLevelDbu() const noexcept;
    /** A2: one file, several sizes of the same model (Full, Lite). 1 = the biggest, 0 = the smallest.
        Not real-time safe (call off the audio thread); safe while the model is playing. */
    bool isSlimmable() const noexcept;
    void setSize (double size01);

    /** Allocates for blocks of up to maxBlockSize samples and runs the model into its settled state. */
    void reset (int maxBlockSize);
    void process (const float* in, float* out, int numSamples) noexcept;

private:
    NamModel();
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::string architecture;
};
