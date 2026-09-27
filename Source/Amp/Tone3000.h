#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>

/**
 * TONE3000 in the plug-in: "browse on TONE3000 -> pick a tone -> it loads".
 *
 * The documented Select flow (OAuth 2.0 + PKCE, public client):
 *   1. the plug-in opens the system browser at /api/v1/oauth/authorize?prompt=select_tone, with our
 *      publishable key as client_id and a loopback redirect (http://127.0.0.1:<port>/callback)
 *   2. the user signs in / browses / picks a tone on tone3000.com
 *   3. TONE3000 redirects the browser to the loopback URL with ?code=...&state=...&tone_id=...;
 *      a tiny HTTP listener in the plug-in receives it
 *   4. the code is exchanged for an access token (POST /api/v1/oauth/token, with the PKCE verifier)
 *   5. the tone's models are listed (GET /api/v1/models?tone_id=...) and downloaded with the Bearer
 *      token into Documents/Swarmness/TONE3000/<tone>/, and the first one is loaded
 *
 * Only the publishable key (t3k_pub_...) is built in: per TONE3000 it is an OAuth client id, not a
 * secret. The network work runs on this object's own thread; results come back on the message thread.
 */
class Tone3000 : private juce::Thread
{
public:
    enum class Target { amp, cab };

    struct Download
    {
        Target target = Target::amp;
        juce::String toneTitle;
        juce::Array<juce::File> files;   // every model / IR of the tone, the first is the one to load
    };

    Tone3000();
    ~Tone3000() override;

    /** True when the build has a publishable key (otherwise the button just opens the website). */
    static bool isConfigured();
    static juce::String redirectUri();

    /** Starts a Select flow (message thread). A flow already running is cancelled. */
    void start (Target);
    void cancel();

    /** "Waiting for TONE3000 in your browser...", "Downloading 3 models...", errors (message thread). */
    juce::String getStatus() const;
    bool isBusy() const noexcept { return isThreadRunning(); }

    /** Called on the message thread when a tone has been downloaded. */
    std::function<void (const Download&)> onDownloaded;

    /** Folder the captures of a tone are saved to. */
    static juce::File downloadFolder();

    // Exposed for tests
    static juce::String base64Url (const void* data, size_t size);
    static juce::String codeChallengeFor (const juce::String& verifier);
    static juce::StringPairArray parseQuery (const juce::String& requestLine);

private:
    void run() override;
    void setStatus (const juce::String&);
    bool receiveCallback (juce::StringPairArray& params);
    juce::var apiGet (const juce::String& path, const juce::String& token, int& status);
    bool downloadTo (const juce::String& url, const juce::String& token, const juce::File& dest);

    Target target = Target::amp;
    juce::String verifier, state;
    std::unique_ptr<juce::StreamingSocket> listener;
    mutable juce::CriticalSection statusLock;
    juce::String status;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Tone3000)
};
