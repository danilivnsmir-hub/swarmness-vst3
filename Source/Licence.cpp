#include "Licence.h"

namespace
{
    constexpr const char* kApi = "https://api.lemonsqueezy.com/v1/licenses/";
    constexpr int kValidateEveryDays = 30, kGraceDays = 45;

    // the secret behind the stored token (split so it is not one string in the binary)
    juce::String secret()
    {
        const char a[] = "sw4rm-", b[] = "n3ss:", c[] = "h0ney-", d[] = "wasp!";
        return juce::String (a) + b + c + d;
    }

    juce::String isoNow (const juce::Time& t) { return t.toISO8601 (true); }
    juce::Time fromIso (const juce::String& s) { return juce::Time::fromISO8601 (s); }
}

Licence::Licence() : juce::Thread ("Swarmness licence")
{
    clock = [] { return juce::Time::getCurrentTime(); };
    juce::PropertiesFile::Options o;
    o.applicationName = "Swarmness";
    o.filenameSuffix = "licence";
    o.folderName = "Swarmness";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = 0;
    settings = settingsDirectoryOverride != juce::File() ? std::make_unique<juce::PropertiesFile> (settingsDirectoryOverride.getChildFile ("Swarmness.licence"), o)
                                                         : std::make_unique<juce::PropertiesFile> (o);
    refresh();
}

Licence::~Licence()
{
    cancelPendingUpdate();
    stopThread (8000);
}

void Licence::setSettingsDirectory (const juce::File& dir) { settingsDirectoryOverride = dir; }

juce::String Licence::getKey() const
{
    const juce::ScopedLock sl (lock);
    return settings->getValue ("key");
}

juce::String Licence::getMessage() const
{
    const juce::ScopedLock sl (lock);
    return message;
}

juce::String Licence::machineName()
{
    return juce::SystemStats::getComputerName() + " (" + juce::SystemStats::getOperatingSystemName() + ")";
}

juce::String Licence::tokenFor (const juce::String& key, const juce::String& instance, const juce::String& activatedAt) const
{
    return juce::SHA256 ((key + "|" + instance + "|" + activatedAt + "|" + secret()).toUTF8()).toHexString();
}

void Licence::refresh()
{
    {
        const juce::ScopedLock sl (lock);
        settings->reload();
    }
    compute();
}

void Licence::compute()
{
    const juce::ScopedLock sl (lock);
    const auto now = clock();

    // the trial: from the first run; the clock may not go backwards
    if (settings->getValue ("trialStart").isEmpty())
        settings->setValue ("trialStart", isoNow (now));
    auto lastSeen = settings->containsKey ("lastSeen") ? fromIso (settings->getValue ("lastSeen")) : now;
    if (now >= lastSeen)
    {
        lastSeen = now;
        settings->setValue ("lastSeen", isoNow (now));
    }
    const auto start = fromIso (settings->getValue ("trialStart"));
    const double usedDays = (lastSeen - start).inDays();
    const int left = (int) std::ceil ((double) kTrialDays - usedDays);
    daysLeft = juce::jlimit (0, kTrialDays, left);

    // the licence: key + instance + token, validated within the grace period
    const auto key = settings->getValue ("key"), instance = settings->getValue ("instance"), at = settings->getValue ("activatedAt");
    bool activated = key.isNotEmpty() && settings->getValue ("token") == tokenFor (key, instance, at);
    if (activated)
    {
        const auto lastValid = fromIso (settings->getValue ("lastValidated", at));
        if ((lastSeen - lastValid).inDays() > kGraceDays)
            activated = false;   // not seen the store for too long: the key must be checked again
    }
    settings->saveIfNeeded();

   #if SWARMNESS_NO_LICENCE
    state = State::activated;   // a developer build: no trial, no key
   #else
    state = activated ? State::activated : (daysLeft > 0 ? State::trial : State::expired);
   #endif
}

void Licence::activate (const juce::String& key, std::function<void()> onDone)
{
    if (isThreadRunning() || key.trim().isEmpty())
        return;
    job = Job::activate;
    jobKey = key.trim();
    jobDone = std::move (onDone);
    busy = true;
    startThread (juce::Thread::Priority::low);
}

void Licence::deactivate (std::function<void()> onDone)
{
    if (isThreadRunning())
        return;
    job = Job::deactivate;
    jobDone = std::move (onDone);
    busy = true;
    startThread (juce::Thread::Priority::low);
}

void Licence::validateIfDue()
{
    refresh();
    if (state != State::activated || isThreadRunning())
        return;
    juce::Time lastValid;
    {
        const juce::ScopedLock sl (lock);
        lastValid = fromIso (settings->getValue ("lastValidated", settings->getValue ("activatedAt")));
    }
    if ((clock() - lastValid).inDays() < kValidateEveryDays)
        return;
    job = Job::validate;
    busy = true;
    startThread (juce::Thread::Priority::low);
}

Licence::Response Licence::request (const juce::String& endpoint, const juce::StringPairArray& form)
{
    if (transport != nullptr)
        return transport (endpoint, form);

    Response r;
    juce::String body;
    for (const auto& k : form.getAllKeys())
        body += (body.isEmpty() ? "" : "&") + juce::URL::addEscapeChars (k, true) + "=" + juce::URL::addEscapeChars (form[k], true);
    juce::WebInputStream stream (juce::URL (kApi + endpoint).withPOSTData (body), true);
    stream.withExtraHeaders ("Accept: application/json\r\nContent-Type: application/x-www-form-urlencoded\r\n").withConnectionTimeout (12000);
    if (! stream.connect (nullptr))
        return r;
    r.status = stream.getStatusCode();
    r.body = stream.readEntireStreamAsString();
    r.ok = r.status >= 200 && r.status < 300;
    return r;
}

void Licence::run()
{
    juce::String msg;
    const auto now = clock();
    if (job == Job::activate)
    {
        juce::StringPairArray form;
        form.set ("license_key", jobKey);
        form.set ("instance_name", machineName());
        const auto r = request ("activate", form);
        const auto json = juce::JSON::parse (r.body);
        bool activated = (bool) json.getProperty ("activated", false);
        const int storeId = (int) json.getProperty ("meta", {}).getProperty ("store_id", 0);
        if (activated && kStoreId != 0 && storeId != kStoreId)
        {
            activated = false;
            msg = "This key belongs to another store";
        }
        if (activated)
        {
            const auto instance = json.getProperty ("instance", {}).getProperty ("id", {}).toString();
            const juce::ScopedLock sl (lock);
            settings->setValue ("key", jobKey);
            settings->setValue ("instance", instance);
            settings->setValue ("activatedAt", isoNow (now));
            settings->setValue ("lastValidated", isoNow (now));
            settings->setValue ("token", tokenFor (jobKey, instance, isoNow (now)));
            settings->saveIfNeeded();
            msg = "Activated on this machine";
        }
        else if (msg.isEmpty())
        {
            const auto error = json.getProperty ("error", {}).toString();
            msg = error.isNotEmpty() ? error : (r.status == 0 ? "No connection - check the network and try again" : "The key was not accepted");
        }
    }
    else if (job == Job::deactivate)
    {
        juce::StringPairArray form;
        juce::String key, instance;
        {
            const juce::ScopedLock sl (lock);
            key = settings->getValue ("key");
            instance = settings->getValue ("instance");
        }
        form.set ("license_key", key);
        form.set ("instance_id", instance);
        const auto r = request ("deactivate", form);
        const auto json = juce::JSON::parse (r.body);
        const bool gone = (bool) json.getProperty ("deactivated", false) || r.status == 404;
        if (gone)
        {
            const juce::ScopedLock sl (lock);
            for (auto k : { "key", "instance", "activatedAt", "lastValidated", "token" })
                settings->removeValue (k);
            settings->saveIfNeeded();
            msg = "Deactivated - the key is free for another machine";
        }
        else
            msg = r.status == 0 ? "No connection - the licence stays on this machine" : json.getProperty ("error", {}).toString();
    }
    else if (job == Job::validate)
    {
        juce::StringPairArray form;
        juce::String key, instance;
        {
            const juce::ScopedLock sl (lock);
            key = settings->getValue ("key");
            instance = settings->getValue ("instance");
        }
        form.set ("license_key", key);
        form.set ("instance_id", instance);
        const auto r = request ("validate", form);
        const auto json = juce::JSON::parse (r.body);
        if (r.status != 0 && r.body.isNotEmpty())
        {
            const bool valid = (bool) json.getProperty ("valid", false);
            const juce::ScopedLock sl (lock);
            if (valid)
                settings->setValue ("lastValidated", isoNow (now));
            else
            {
                for (auto k : { "key", "instance", "activatedAt", "lastValidated", "token" })
                    settings->removeValue (k);
                msg = "The licence is no longer valid: " + json.getProperty ("error", {}).toString();
            }
            settings->saveIfNeeded();
        }
        // no answer: keep the licence, the grace period covers it
    }
    {
        const juce::ScopedLock sl (lock);
        if (msg.isNotEmpty())
            message = msg;
    }
    compute();
    triggerAsyncUpdate();
}

void Licence::finishForTesting()
{
    waitForThreadToExit (-1);
    cancelPendingUpdate();
    handleAsyncUpdate();
}

void Licence::handleAsyncUpdate()
{
    busy = false;
    job = Job::none;
    if (jobDone != nullptr)
    {
        auto done = std::move (jobDone);
        jobDone = nullptr;
        done();
    }
    if (onChange != nullptr)
        onChange();
}
