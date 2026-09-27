// Compiled with the NAM core (C++20, Eigen) - see CMakeLists.txt
#include "NamModel.h"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/container.h"
#include "NAM/convnet.h"
#include "NAM/linear.h"
#include "NAM/lstm.h"
#include "NAM/sequential.h"
#include "NAM/wavenet/model.h"

#include <algorithm>
#include <exception>
#include <vector>

namespace
{
    // The architectures register themselves from static initialisers in their own object files.
    // A linker working from a static library only pulls in object files something refers to (and
    // dead-strips unused data), so load() refers to each one through volatile reads.
    using CreateConfig = std::unique_ptr<nam::ModelConfig> (*) (const nlohmann::json&, double);
    CreateConfig const architectures[] {
        &nam::container::create_config, &nam::convnet::create_config, &nam::linear::create_config,
        &nam::lstm::create_config, &nam::sequential::create_config, &nam::wavenet::create_config
    };

    bool architecturesLinked() noexcept
    {
        bool all = true;
        for (auto f : architectures)
        {
            volatile CreateConfig p = f;
            all = all && p != nullptr;
        }
        return all;
    }
}

struct NamModel::Impl
{
    std::unique_ptr<nam::DSP> dsp;
    double sampleRate = 48000.0;
    int maxBlock = 0;
};

NamModel::NamModel() : impl (std::make_unique<Impl>()) {}
NamModel::~NamModel() = default;

std::unique_ptr<NamModel> NamModel::load (const std::string& jsonText, std::string& error)
{
    if (! architecturesLinked())
    {
        error = "NAM architectures missing";
        return nullptr;
    }
    try
    {
        const auto j = nlohmann::json::parse (jsonText);
        nam::DspLoadOptions options;
        options.prewarm = false;   // reset() prewarms at the real block size
        auto dsp = nam::get_dsp (j, options);
        if (dsp == nullptr)
        {
            error = "not a NAM model";
            return nullptr;
        }
        if (dsp->NumInputChannels() != 1 || dsp->NumOutputChannels() != 1)
        {
            error = "only mono amp captures are supported";
            return nullptr;
        }
        std::unique_ptr<NamModel> m (new NamModel());
        m->architecture = j.value ("architecture", std::string());
        const double sr = dsp->GetExpectedSampleRate();
        m->impl->sampleRate = sr > 0.0 ? sr : 48000.0;
        m->impl->dsp = std::move (dsp);
        return m;
    }
    catch (const std::exception& e)
    {
        error = e.what();
    }
    catch (...)
    {
        error = "unreadable model";
    }
    return nullptr;
}

double NamModel::getSampleRate() const noexcept   { return impl->sampleRate; }
bool NamModel::hasLoudness() const noexcept       { return impl->dsp->HasLoudness(); }
double NamModel::getLoudnessDb() const noexcept   { return impl->dsp->GetLoudness(); }

void NamModel::reset (int maxBlockSize)
{
    impl->maxBlock = std::max (1, maxBlockSize);
    impl->dsp->ResetAndPrewarm (impl->sampleRate, impl->maxBlock);
}

void NamModel::process (const float* in, float* out, int numSamples) noexcept
{
    while (numSamples > 0)
    {
        const int n = std::min (numSamples, impl->maxBlock);
        float* inputs[1] { const_cast<float*> (in) };
        float* outputs[1] { out };
        impl->dsp->process (inputs, outputs, n);
        in += n;
        out += n;
        numSamples -= n;
    }
}
