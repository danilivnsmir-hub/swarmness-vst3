#include "Tone3000.h"
#include <random>

#ifndef SWARMNESS_TONE3000_KEY
 #define SWARMNESS_TONE3000_KEY ""
#endif

namespace
{
    const juce::String kApi { "https://www.tone3000.com" };
    const juce::String kPublishableKey { SWARMNESS_TONE3000_KEY };
    constexpr int kPort = 43167;
    constexpr int kWaitSeconds = 600;   // how long the browser has to come back

    juce::String randomToken (int bytes)
    {
        std::random_device rd;   // the OS entropy source
        juce::HeapBlock<juce::uint8> buf ((size_t) bytes);
        for (int i = 0; i < bytes; ++i)
            buf[i] = (juce::uint8) (rd() & 0xff);
        return Tone3000::base64Url (buf.getData(), (size_t) bytes);
    }

    juce::String safeName (juce::String s)
    {
        s = s.removeCharacters ("\\/:*?\"<>|").trim();
        return s.isEmpty() ? juce::String ("tone") : s.substring (0, 80);
    }

    void respond (juce::StreamingSocket& socket, int code, const juce::String& title, const juce::String& text)
    {
        const juce::String body = "<!doctype html><meta charset=utf-8><title>Swarmness</title>"
                                  "<body style=\"background:#120d09;color:#f0c060;font-family:sans-serif;text-align:center;padding-top:80px\">"
                                  "<h1>" + title + "</h1><p style=\"color:#d8c8b0\">" + text + "</p></body>";
        const auto bodyUtf8 = body.toUTF8();
        const auto length = (int) std::strlen (bodyUtf8.getAddress());
        const juce::String head = "HTTP/1.1 " + juce::String (code) + (code == 200 ? " OK" : " Not Found")
                                + "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + juce::String (length)
                                + "\r\nConnection: close\r\n\r\n";
        socket.write (head.toRawUTF8(), (int) std::strlen (head.toRawUTF8()));
        socket.write (bodyUtf8.getAddress(), length);
    }
}

//==============================================================================
Tone3000::Tone3000() : juce::Thread ("TONE3000") {}

Tone3000::~Tone3000()
{
    alive->store (false);
    cancel();
}

bool Tone3000::isConfigured()     { return kPublishableKey.startsWith ("t3k_pub_"); }
juce::String Tone3000::redirectUri() { return "http://127.0.0.1:" + juce::String (kPort) + "/callback"; }

juce::File Tone3000::downloadFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Swarmness").getChildFile ("TONE3000");
}

juce::String Tone3000::base64Url (const void* data, size_t size)
{
    auto s = juce::Base64::toBase64 (data, size);
    return s.replaceCharacter ('+', '-').replaceCharacter ('/', '_').removeCharacters ("=");
}

juce::String Tone3000::codeChallengeFor (const juce::String& v)
{
    const juce::SHA256 hash (v.toRawUTF8(), std::strlen (v.toRawUTF8()));
    const auto raw = hash.getRawData();
    return base64Url (raw.getData(), raw.getSize());
}

juce::StringPairArray Tone3000::parseQuery (const juce::String& requestLine)
{
    // "GET /callback?code=...&state=... HTTP/1.1"
    juce::StringPairArray out;
    const auto target = requestLine.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (" ", false, false);
    out.set ("__path", target.upToFirstOccurrenceOf ("?", false, false));
    for (const auto& pair : juce::StringArray::fromTokens (target.fromFirstOccurrenceOf ("?", false, false), "&", ""))
        if (pair.isNotEmpty())
            out.set (juce::URL::removeEscapeChars (pair.upToFirstOccurrenceOf ("=", false, false).replace ("+", " ")),
                     juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false).replace ("+", " ")));
    return out;
}

void Tone3000::setStatus (const juce::String& s)
{
    const juce::ScopedLock sl (statusLock);
    status = s;
}

juce::String Tone3000::getStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return status;
}

//==============================================================================
void Tone3000::start (Target t, Architecture arch)
{
    if (! isConfigured())
    {
        juce::URL (kApi + (t == Target::cab ? "/search?format=ir" : t == Target::pedal ? "/search?gears=pedal" : "/search?gears=amp")).launchInDefaultBrowser();
        return;
    }
    cancel();
    target = t;
    architecture = arch;
    verifier = randomToken (32);
    state = randomToken (16);

    listener = std::make_unique<juce::StreamingSocket>();
    if (! listener->createListener (kPort, "127.0.0.1"))
    {
        listener.reset();
        setStatus ("TONE3000: port " + juce::String (kPort) + " is busy - close other Swarmness windows waiting for TONE3000");
        return;
    }

    juce::URL url (kApi + "/api/v1/oauth/authorize");
    url = url.withParameter ("client_id", kPublishableKey)
             .withParameter ("redirect_uri", redirectUri())
             .withParameter ("response_type", "code")
             .withParameter ("code_challenge", codeChallengeFor (verifier))
             .withParameter ("code_challenge_method", "S256")
             .withParameter ("state", state)
             .withParameter ("prompt", "select_tone")
             .withParameter ("preview", "true");
    if (t == Target::cab)
        url = url.withParameter ("format", "ir");
    else
        url = url.withParameter ("gears", t == Target::pedal ? "pedal" : "amp_full-rig").withParameter ("format", "nam");
    if (t != Target::cab && arch == Architecture::a2)
        url = url.withParameter ("architecture", "2");

    setStatus ("Pick a tone on TONE3000 in your browser...");
    startThread (juce::Thread::Priority::low);
    url.launchInDefaultBrowser();
}

void Tone3000::cancel()
{
    signalThreadShouldExit();
    if (listener != nullptr)
        listener->close();
    stopThread (5000);
    listener.reset();
}

bool Tone3000::receiveCallback (juce::StringPairArray& params)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) kWaitSeconds * 1000u;
    while (! threadShouldExit() && juce::Time::getMillisecondCounter() < deadline)
    {
        if (listener == nullptr || listener->waitUntilReady (true, 300) <= 0)
            continue;
        std::unique_ptr<juce::StreamingSocket> client (listener->waitForNextConnection());
        if (client == nullptr)
            continue;

        // the request line and headers (small)
        juce::MemoryBlock data;
        char buf[2048];
        for (int tries = 0; tries < 50 && data.getSize() < 16384; ++tries)
        {
            if (client->waitUntilReady (true, 100) <= 0)
                continue;
            const int n = client->read (buf, (int) sizeof (buf), false);
            if (n <= 0) break;
            data.append (buf, (size_t) n);
            if (data.toString().contains ("\r\n\r\n")) break;
        }
        const auto requestLine = data.toString().upToFirstOccurrenceOf ("\r\n", false, false);
        params = parseQuery (requestLine);
        if (params["__path"] != "/callback")
        {
            respond (*client, 404, "Not here", "");
            continue;
        }
        const bool ok = params["code"].isNotEmpty() && params["state"] == state;
        respond (*client, 200, ok ? "Got it" : "TONE3000",
                 ok ? "Loading your tone into Swarmness - you can close this tab." : "Nothing selected. You can close this tab.");
        return true;
    }
    return false;
}

juce::var Tone3000::apiGet (const juce::String& path, const juce::String& token, int& httpStatus)
{
    httpStatus = 0;
    auto stream = juce::URL (kApi + path).createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withExtraHeaders ("Authorization: Bearer " + token + "\r\nAccept: application/json")
            .withConnectionTimeoutMs (15000)
            .withProgressCallback ([this] (int, int) { return ! threadShouldExit(); })
            .withStatusCode (&httpStatus));
    if (stream == nullptr)
        return {};
    return juce::JSON::parse (stream->readEntireStreamAsString());
}

bool Tone3000::downloadTo (const juce::String& url, const juce::String& token, const juce::File& dest)
{
    int httpStatus = 0;
    auto stream = juce::URL (url).createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withExtraHeaders ("Authorization: Bearer " + token)
            .withConnectionTimeoutMs (30000)
            .withProgressCallback ([this] (int, int) { return ! threadShouldExit(); })
            .withStatusCode (&httpStatus));
    if (stream == nullptr || httpStatus >= 400)
        return false;
    // in small chunks, so closing the plug-in (or a new browse) never waits on a big download
    juce::MemoryOutputStream data;
    char chunk[16384];
    while (! threadShouldExit() && ! stream->isExhausted() && data.getDataSize() < (size_t) 64 * 1024 * 1024)
    {
        const int n = stream->read (chunk, (int) sizeof (chunk));
        if (n <= 0) break;
        data.write (chunk, (size_t) n);
    }
    if (threadShouldExit() || data.getDataSize() < 16)
        return false;
    dest.getParentDirectory().createDirectory();
    return dest.replaceWithData (data.getData(), data.getDataSize());
}

void Tone3000::run()
{
    juce::StringPairArray params;
    const bool got = receiveCallback (params);
    if (listener != nullptr)
        listener->close();
    if (threadShouldExit())
        return;
    if (! got)
    {
        setStatus ("TONE3000: no answer from the browser - try again");
        return;
    }
    if (params["canceled"].isNotEmpty() || params["error"].isNotEmpty() || params["code"].isEmpty())
    {
        setStatus (params["error"].isNotEmpty() ? "TONE3000: " + params["error"] : juce::String ("TONE3000: nothing selected"));
        return;
    }
    if (params["state"] != state)
    {
        setStatus ("TONE3000: the answer did not match this request - try again");
        return;
    }

    // code -> access token (PKCE)
    setStatus ("TONE3000: signing in...");
    const juce::String form = "grant_type=authorization_code&code=" + juce::URL::addEscapeChars (params["code"], true)
                            + "&code_verifier=" + juce::URL::addEscapeChars (verifier, true)
                            + "&redirect_uri=" + juce::URL::addEscapeChars (redirectUri(), true)
                            + "&client_id=" + juce::URL::addEscapeChars (kPublishableKey, true);
    int httpStatus = 0;
    juce::String token;
    {
        auto stream = juce::URL (kApi + "/api/v1/oauth/token").withPOSTData (form).createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                .withExtraHeaders ("Content-Type: application/x-www-form-urlencoded\r\nAccept: application/json")
                .withConnectionTimeoutMs (15000)
                .withStatusCode (&httpStatus));
        if (stream != nullptr)
            token = juce::JSON::parse (stream->readEntireStreamAsString())["access_token"].toString();
    }
    if (token.isEmpty())
    {
        setStatus ("TONE3000: sign-in failed (" + juce::String (httpStatus) + ")");
        return;
    }

    const auto toneId = params["tone_id"];
    if (toneId.isEmpty())
    {
        setStatus ("TONE3000: signed in, but no tone was picked");
        return;
    }

    const auto tone = apiGet ("/api/v1/tones/" + juce::URL::addEscapeChars (toneId, true), token, httpStatus);
    const juce::String title = tone["title"].toString().isNotEmpty() ? tone["title"].toString() : "Tone " + toneId;
    const bool isIr = tone["format"].toString() == "ir" || target == Target::cab;

    // the model list comes one architecture at a time (default: A1): the chosen one first, then the other
    juce::Array<juce::var> all;
    juce::StringArray seen;
    juce::StringArray archs { "", "2" };
    if (isIr)
        archs = { "" };
    else if (architecture == Architecture::a2)
        archs = { "2", "" };
    for (const auto& a : archs)
    {
        const auto models = apiGet ("/api/v1/models?tone_id=" + juce::URL::addEscapeChars (toneId, true) + "&page_size=50"
                                        + (a.isNotEmpty() ? "&architecture=" + a : juce::String()), token, httpStatus);
        if (const auto* page = models["data"].getArray())
            for (const auto& m : *page)
                if (! seen.contains (m["id"].toString()))
                {
                    seen.add (m["id"].toString());
                    all.add (m);
                }
    }
    const auto* list = &all;
    if (list->isEmpty())
    {
        setStatus ("TONE3000: \"" + title + "\" has no files to download (" + juce::String (httpStatus) + ")");
        return;
    }

    const auto folder = downloadFolder().getChildFile (safeName (title) + " (" + toneId + ")");
    Download result;
    result.target = isIr ? Target::cab : (target == Target::pedal ? Target::pedal : Target::amp);
    result.toneTitle = title;
    int n = 0;
    for (const auto& m : *list)
    {
        if (threadShouldExit())
            return;
        setStatus ("TONE3000: downloading \"" + title + "\" (" + juce::String (++n) + " / " + juce::String (list->size()) + ")...");
        auto name = safeName (m["name"].toString().isNotEmpty() ? m["name"].toString() : "model " + juce::String (n));
        if (m["architecture_version"].toString() == "2" && ! name.containsIgnoreCase ("A2"))
            name << " A2";
        const auto dest = folder.getChildFile (name + (isIr ? ".wav" : ".nam"));
        if (downloadTo (m["model_url"].toString(), token, dest))
            result.files.add (dest);
    }
    if (result.files.isEmpty())
    {
        setStatus ("TONE3000: the download failed - try again");
        return;
    }

    setStatus ({});
    auto flag = alive;
    auto callback = onDownloaded;
    juce::MessageManager::callAsync ([flag, callback, result]
    {
        if (flag->load() && callback != nullptr)
            callback (result);
    });
}
