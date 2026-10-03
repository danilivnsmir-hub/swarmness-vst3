#pragma once

#include <JuceHeader.h>
#include <functional>

/**
 * The licence: a 7-day trial from the first run, then activation with a licence key from the store
 * (Lemon Squeezy's License API: activate / validate / deactivate, an activation limit per key).
 *
 *  - The trial start and the activation live in one settings file per user (shared by every
 *    instance of the plug-in). Reinstalling does not reset the trial; a clock moved backwards
 *    does not extend it (the last date seen is remembered).
 *  - After the trial, without a licence, the plug-in passes the dry signal (see isAuthorised()).
 *  - Activation stores the key, the instance id and a token (a hash over them with a secret in the
 *    binary); it is re-validated online once a month, with a 45-day grace when the network is away.
 *  - The network calls run on a background thread; results come back on the message thread. The
 *    transport is a function, so tests run without a network.
 */
class Licence : private juce::Thread,
                private juce::AsyncUpdater
{
public:
    enum class State { trial, expired, activated };

    struct Response { bool ok = false; int status = 0; juce::String body; };
    using Transport = std::function<Response (const juce::String& endpoint, const juce::StringPairArray& form)>;

    static constexpr int kTrialDays = 7;
    static constexpr const char* kStoreUrl = "https://swarmness.lemonsqueezy.com";
    /** The store's id at Lemon Squeezy: a key from another store is refused (0 = not checked yet). */
    static constexpr int kStoreId = 0;

    Licence();
    ~Licence() override;

    /** Tests: where the settings file lives (before any Licence is created). Empty = the user's app data. */
    static void setSettingsDirectory (const juce::File&);
    /** Tests: the network, replaced. */
    void setTransport (Transport t) { transport = std::move (t); }
    /** Tests: "now", replaced. */
    void setClock (std::function<juce::Time()> c) { clock = std::move (c); refresh(); }
    /** Tests / screenshots: behave like a build without licensing (SWARMNESS_NO_LICENCE): always activated. */
    void setDeveloperBuildForTesting (bool b) { developerBuild = b; refresh(); }

    State getState() const noexcept { return state.load(); }
    bool isAuthorised() const noexcept { return state.load() != State::expired; }
    int trialDaysLeft() const noexcept { return daysLeft.load(); }
    juce::String getKey() const;
    juce::String getMessage() const;          // the last activation / validation message (for the UI)
    bool isBusy() const noexcept { return busy.load(); }

    /** Activates a key (background); onDone is called on the message thread. */
    void activate (const juce::String& key, std::function<void()> onDone = nullptr);
    void deactivate (std::function<void()> onDone = nullptr);
    /** Re-validates the stored licence when it is due (called on startup / editor open). */
    void validateIfDue();

    /** Re-reads the settings file (another instance may have activated) and recomputes the state. */
    void refresh();
    /** Tests (no message loop): waits for the background job and delivers its result. */
    void finishForTesting();

    std::function<void()> onChange;

private:
    void run() override;
    void handleAsyncUpdate() override;
    Response request (const juce::String& endpoint, const juce::StringPairArray& form);
    static juce::File settingsFile();
    static juce::String machineName();
    juce::String tokenFor (const juce::String& key, const juce::String& instance, const juce::String& activatedAt) const;
    void compute();

    enum class Job { none, activate, deactivate, validate };
    Job job = Job::none;
    juce::String jobKey;
    std::function<void()> jobDone;

    std::unique_ptr<juce::PropertiesFile> settings;
    juce::CriticalSection lock;
    Transport transport;
    std::function<juce::Time()> clock;
    std::atomic<State> state { State::trial };
    bool developerBuild = false;   // set from SWARMNESS_NO_LICENCE in the constructor
    std::atomic<int> daysLeft { kTrialDays };
    std::atomic<bool> busy { false };
    juce::String message;

    static inline juce::File settingsDirectoryOverride;
};
