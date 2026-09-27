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
    /** "WaveNet", "LSTM", ... */
    const std::string& getArchitecture() const noexcept { return architecture; }

    /** Allocates for blocks of up to maxBlockSize samples and runs the model into its settled state. */
    void reset (int maxBlockSize);
    void process (const float* in, float* out, int numSamples) noexcept;

private:
    NamModel();
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::string architecture;
};
