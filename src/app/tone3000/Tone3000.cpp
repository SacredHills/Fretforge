#include "Tone3000.h"
#include <juce_cryptography/juce_cryptography.h>

namespace ff::t3k
{
namespace
{
    juce::String str (const juce::var& v, const char* key)
    {
        const auto x = v.getProperty (key, {});
        return x.isVoid() || x.isUndefined() ? juce::String() : x.toString();
    }
    int num (const juce::var& v, const char* key) { return (int) v.getProperty (key, 0); }

    juce::String base64Url (const void* data, size_t size)
    {
        return juce::Base64::toBase64 (data, size).replaceCharacter ('+', '-').replaceCharacter ('/', '_').removeCharacters ("=");
    }

    juce::String randomToken (int bytes)
    {
        juce::Random r (juce::Random::getSystemRandom().nextInt64() ^ juce::Time::currentTimeMillis());
        juce::MemoryBlock mb ((size_t) bytes);
        for (size_t i = 0; i < mb.getSize(); ++i) mb[i] = (char) r.nextInt (256);
        return base64Url (mb.getData(), mb.getSize());
    }

    juce::StringPairArray parseQuery (const juce::String& q)
    {
        juce::StringPairArray out;
        for (auto& pair : juce::StringArray::fromTokens (q.upToFirstOccurrenceOf ("#", false, false), "&", ""))
            out.set (juce::URL::removeEscapeChars (pair.upToFirstOccurrenceOf ("=", false, false)),
                     juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false).replaceCharacter ('+', ' ')));
        return out;
    }
}

// ---------------------------------------------------------------- types

Tone Tone::fromVar (const juce::var& v)
{
    Tone t;
    t.id = num (v, "id");
    t.title = str (v, "title");
    t.gear = str (v, "gear");
    t.format = str (v, "format");
    t.license = str (v, "license");
    t.url = str (v, "url");
    const auto u = v.getProperty ("user", {});
    t.creator = str (u, "display_name").isNotEmpty() ? str (u, "display_name") : str (u, "username");
    if (auto* arr = v.getProperty ("makes", {}).getArray())
        for (auto& m : *arr) t.makes.add (m.isObject() ? str (m, "name") : m.toString());
    t.modelsCount = num (v, "models_count");
    t.irsCount = num (v, "irs_count");
    t.downloadsCount = num (v, "downloads_count");
    return t;
}

juce::String Tone::countLabel() const
{
    if (format == "ir") return juce::String (irsCount > 0 ? irsCount : modelsCount) + " IR" + ((irsCount > 0 ? irsCount : modelsCount) == 1 ? "" : "s");
    return juce::String (modelsCount) + " model" + (modelsCount == 1 ? "" : "s");
}

Model Model::fromVar (const juce::var& v)
{
    Model m;
    m.id = num (v, "id");
    m.toneId = num (v, "tone_id");
    m.name = str (v, "name");
    m.url = str (v, "model_url");
    m.size = str (v, "size");
    m.architecture = str (v, "architecture_version");
    return m;
}

juce::String Model::extension (const juce::String& toneFormat) const
{
    const auto file = juce::URL (url).getSubPath().fromLastOccurrenceOf ("/", false, false);
    const auto ext = file.contains (".") ? file.fromLastOccurrenceOf (".", true, false).toLowerCase() : juce::String();
    if (ext == ".nam" || ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac") return ext;
    return toneFormat == "ir" ? ".wav" : ".nam";
}

juce::String Client::Response::message() const
{
    if (error.isNotEmpty()) return error;
    const auto j = json();
    for (auto* k : { "error_description", "message", "error" })
        if (str (j, k).isNotEmpty()) return str (j, k);
    if (status == 401) return "Not signed in to TONE3000 (or the session expired).";
    if (status == 403) return "TONE3000 refused this request (403).";
    if (status == 429) return "Too many requests to TONE3000; wait a minute and try again.";
    return "TONE3000 answered with HTTP " + juce::String (status) + ".";
}

// ---------------------------------------------------------------- loopback server

bool LoopbackServer::start()
{
    if (isThreadRunning()) return true;
    if (! listener.createListener (0, "127.0.0.1")) return false;
    port = listener.getBoundPort();
    if (port <= 0) { listener.close(); return false; }
    startThread();
    return true;
}

void LoopbackServer::stop()
{
    if (! isThreadRunning()) { listener.close(); return; }
    signalThreadShouldExit();
    listener.close();
    stopThread (3000);
}

void LoopbackServer::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<juce::StreamingSocket> c (listener.waitForNextConnection());
        if (c == nullptr || threadShouldExit()) continue;
        juce::MemoryOutputStream head;
        char buf[1024];
        while (! head.toString().contains ("\r\n\r\n") && head.getDataSize() < 16 * 1024)
        {
            if (c->waitUntilReady (true, 2000) != 1) break;
            const int n = c->read (buf, (int) sizeof (buf), false);
            if (n <= 0) break;
            head.write (buf, (size_t) n);
        }
        const auto line = head.toString().upToFirstOccurrenceOf ("\r\n", false, false);
        const auto target = line.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (" ", false, false);
        juce::String response;
        if (line.startsWith ("GET ") && target.upToFirstOccurrenceOf ("?", false, false) == "/callback")
        {
            const juce::String html ("<!doctype html><html><head><meta charset=\"utf-8\"><title>FretForge Studio</title></head>"
                                     "<body style=\"background:#141619;color:#dde1e6;font:16px sans-serif;display:flex;align-items:center;"
                                     "justify-content:center;height:100vh;margin:0\"><div style=\"text-align:center\"><h2>Done</h2>"
                                     "<p>You can close this tab and go back to FretForge Studio.</p></div></body></html>");
            response << "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " << (int) html.getNumBytesAsUTF8()
                     << "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" << html;
            if (onCallback) juce::MessageManager::callAsync ([cb = onCallback, q = target.fromFirstOccurrenceOf ("?", false, false)] { cb (q); });
        }
        else response << "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        c->write (response.toRawUTF8(), (int) response.getNumBytesAsUTF8());
        c->close();
    }
}

// ---------------------------------------------------------------- client

Client::Client (juce::PropertiesFile& s) : settings (s)
{
    id = settings.getValue ("t3kClientId").trim();
    const auto j = juce::JSON::parse (settings.getValue ("t3kTokens"));
    tokens.access = str (j, "access_token");
    tokens.refresh = str (j, "refresh_token");
    tokens.expiresAtMs = (juce::int64) j.getProperty ("expires_at", 0);
    user = settings.getValue ("t3kUser");
}

Client::~Client()
{
    *alive = false;
    server.onCallback = nullptr;
    server.stop();
    pool.removeAllJobs (true, 10000);
}

juce::String Client::clientId() const { return id; }
void Client::setClientId (const juce::String& k)
{
    if (k.trim() == id) return;
    id = k.trim();
    settings.setValue ("t3kClientId", id);
    disconnect();   // tokens belong to the old key
}

Client::Tokens Client::currentTokens() const
{
    const juce::ScopedLock sl (tokenLock);
    return tokens;
}

bool Client::isConnected() const { return currentTokens().valid(); }

void Client::storeTokens (const Tokens& t, bool notify)
{
    {
        const juce::ScopedLock sl (tokenLock);
        tokens = t;
    }
    juce::String json;
    if (t.valid())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("access_token", t.access);
        o->setProperty ("refresh_token", t.refresh);
        o->setProperty ("expires_at", t.expiresAtMs);
        json = juce::JSON::toString (juce::var (o), true);
    }
    std::weak_ptr<bool> token = alive;
    juce::MessageManager::callAsync ([this, token, json, notify] {
        if (token.expired()) return;
        settings.setValue ("t3kTokens", json);
        if (json.isEmpty()) { user = {}; settings.setValue ("t3kUser", juce::String()); }
        if (notify) sendChangeMessage();
    });
}

void Client::disconnect()
{
    storeTokens ({}, true);
}

juce::String Client::formEncode (const juce::StringPairArray& fields)
{
    juce::StringArray parts;
    for (auto& k : fields.getAllKeys())
        parts.add (juce::URL::addEscapeChars (k, true) + "=" + juce::URL::addEscapeChars (fields[k], true));
    return parts.joinIntoString ("&");
}

Client::Response Client::send (const juce::String& method, const juce::URL& url, const juce::String& headers, const juce::String& body)
{
    Response r;
    auto u = body.isNotEmpty() ? url.withPOSTData (body) : url;
    juce::StringPairArray responseHeaders;
    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withHttpRequestCmd (method)
                       .withExtraHeaders (headers)
                       .withConnectionTimeoutMs (20000)
                       .withNumRedirectsToFollow (5)
                       .withStatusCode (&r.status)
                       .withResponseHeaders (&responseHeaders);
    if (auto stream = u.createInputStream (options))
        stream->readIntoMemoryBlock (r.body);
    else if (r.status == 0)
        r.error = "Could not reach tone3000.com (check the internet connection).";
    return r;
}

juce::String Client::accessToken (bool force)
{
    auto t = currentTokens();
    if (! t.valid()) return {};
    if (! force && juce::Time::currentTimeMillis() < t.expiresAtMs - 60000) return t.access;

    std::lock_guard<std::mutex> single (refreshMutex);
    t = currentTokens();
    if (! t.valid()) return {};
    if (! force && juce::Time::currentTimeMillis() < t.expiresAtMs - 60000) return t.access;   // refreshed meanwhile

    juce::StringPairArray f;
    f.set ("grant_type", "refresh_token");
    f.set ("refresh_token", t.refresh);
    f.set ("client_id", id);
    const auto res = send ("POST", juce::URL (juce::String (origin) + "/api/v1/oauth/token"),
                           "Content-Type: application/x-www-form-urlencoded\r\nAccept: application/json\r\n", formEncode (f));
    if (res.ok())
    {
        const auto j = res.json();
        Tokens n { str (j, "access_token"), str (j, "refresh_token"),
                   juce::Time::currentTimeMillis() + (juce::int64) ((double) j.getProperty ("expires_in", 3600) * 1000.0) };
        if (n.refresh.isEmpty()) n.refresh = t.refresh;
        storeTokens (n, false);
        return n.access;
    }
    if (res.status == 400 || res.status == 401) { storeTokens ({}, true); return {}; }   // invalid_grant: session over
    return t.access;   // transient: try with what we have
}

Client::Response Client::authed (const juce::String& method, const juce::URL& url)
{
    auto token = accessToken (false);
    if (token.isEmpty()) { Response r; r.status = 401; r.error = "Not signed in to TONE3000."; return r; }
    auto r = send (method, url, "Authorization: Bearer " + token + "\r\nAccept: application/json\r\n");
    if (r.status == 401)
    {
        token = accessToken (true);
        if (token.isNotEmpty()) r = send (method, url, "Authorization: Bearer " + token + "\r\nAccept: application/json\r\n");
    }
    return r;
}

template <typename T>
void Client::run (std::function<T (juce::String& error)> job, std::function<void (T, juce::String)> done)
{
    std::weak_ptr<bool> token = alive;
    pool.addJob ([job, done, token] {
        juce::String err;
        auto result = std::make_shared<T> (job (err));
        juce::MessageManager::callAsync ([done, result, err, token] { if (! token.expired() && done) done (*result, err); });
    });
}

// ---- flows

void Client::startFlow (const juce::StringPairArray& extra, std::function<void (int, juce::String)> done)
{
    if (id.isEmpty()) { done (0, "Enter your TONE3000 publishable key (client ID) first."); return; }
    if (flowActive) cancelFlow();
    if (! server.start()) { done (0, "Could not open a local port for the TONE3000 sign-in."); return; }

    flowActive = true;
    flowDone = std::move (done);
    verifier = randomToken (32);
    juce::SHA256 hash (verifier.toRawUTF8(), verifier.getNumBytesAsUTF8());
    const auto raw = hash.getRawData();
    const auto challenge = base64Url (raw.getData(), raw.getSize());
    state = randomToken (16);
    redirect = server.redirectUri();

    auto url = juce::URL (juce::String (origin) + "/api/v1/oauth/authorize")
                   .withParameter ("client_id", id)
                   .withParameter ("redirect_uri", redirect)
                   .withParameter ("response_type", "code")
                   .withParameter ("code_challenge", challenge)
                   .withParameter ("code_challenge_method", "S256")
                   .withParameter ("state", state);
    for (auto& k : extra.getAllKeys()) url = url.withParameter (k, extra[k]);

    server.onCallback = [this] (juce::String query) {
        server.stop();
        const auto q = parseQuery (query);
        if (q["state"] != state) { finishFlow (0, "The sign-in answer did not match (state mismatch). Try again."); return; }
        if (q["canceled"] == "true" && q["code"].isEmpty()) { finishFlow (0, "cancelled"); return; }
        if (q["error"].isNotEmpty()) { finishFlow (0, "TONE3000: " + q["error"]); return; }
        if (q["code"].isEmpty()) { finishFlow (0, "TONE3000 did not return a sign-in code."); return; }
        const int tone = q["tone_id"].getIntValue();
        juce::StringPairArray f;
        f.set ("grant_type", "authorization_code");
        f.set ("code", q["code"]);
        f.set ("code_verifier", verifier);
        f.set ("redirect_uri", redirect);
        f.set ("client_id", id);
        const auto body = formEncode (f);
        run<Response> ([body] (juce::String&) {
                           return send ("POST", juce::URL (juce::String (origin) + "/api/v1/oauth/token"),
                                        "Content-Type: application/x-www-form-urlencoded\r\nAccept: application/json\r\n", body);
                       },
                       [this, tone] (Response r, juce::String) {
                           if (! r.ok()) { finishFlow (0, "Sign-in failed: " + r.message()); return; }
                           const auto j = r.json();
                           storeTokens ({ str (j, "access_token"), str (j, "refresh_token"),
                                          juce::Time::currentTimeMillis() + (juce::int64) ((double) j.getProperty ("expires_in", 3600) * 1000.0) },
                                        true);
                           fetchUser();
                           finishFlow (tone, {});
                       });
    };
    if (! url.launchInDefaultBrowser())
    {
        server.stop();
        finishFlow (0, "Could not open your web browser.");
    }
}

void Client::finishFlow (int toneId, const juce::String& error)
{
    if (! flowActive) return;
    flowActive = false;
    verifier = state = {};
    if (auto cb = std::exchange (flowDone, nullptr)) cb (toneId, error);
}

void Client::cancelFlow()
{
    server.stop();
    finishFlow (0, "cancelled");
}

void Client::signIn (std::function<void (juce::String)> done)
{
    startFlow ({}, [done] (int, juce::String e) { if (done) done (e); });
}

void Client::selectTone (const juce::String& format, const juce::String& gears, std::function<void (int, juce::String)> done)
{
    juce::StringPairArray extra;
    extra.set ("prompt", "select_tone");
    extra.set ("menubar", "true");
    if (format.isNotEmpty()) extra.set ("format", format);
    if (gears.isNotEmpty()) extra.set ("gears", gears);
    startFlow (extra, std::move (done));
}

void Client::fetchUser()
{
    run<juce::String> ([this] (juce::String& err) {
                           const auto r = authed ("GET", juce::URL (juce::String (origin) + "/api/v1/user"));
                           if (! r.ok()) { err = r.message(); return juce::String(); }
                           const auto j = r.json();
                           return str (j, "display_name").isNotEmpty() ? str (j, "display_name") : str (j, "username");
                       },
                       [this] (juce::String name, juce::String err) {
                           if (err.isNotEmpty()) return;
                           user = name;
                           settings.setValue ("t3kUser", name);
                           sendChangeMessage();
                       });
}

// ---- API

void Client::listTones (Feed feed, const juce::String& query, int page, std::function<void (TonePage, juce::String)> done)
{
    juce::String path;
    switch (feed)
    {
        case Feed::favorited:  path = "/api/v1/tones/favorited"; break;
        case Feed::downloaded: path = "/api/v1/tones/downloaded"; break;
        case Feed::created:    path = "/api/v1/tones/created"; break;
        case Feed::trending:   path = "/api/v1/tones/trending"; break;
        case Feed::latest:     path = "/api/v1/tones/latest"; break;
        case Feed::search:     path = "/api/v1/tones/search"; break;
    }
    auto url = juce::URL (juce::String (origin) + path);
    const bool paged = feed != Feed::trending && feed != Feed::latest;
    if (paged) url = url.withParameter ("page", juce::String (juce::jmax (1, page))).withParameter ("page_size", feed == Feed::search ? "25" : "50");
    if (query.isNotEmpty() && feed != Feed::trending && feed != Feed::latest) url = url.withParameter ("query", query);
    run<TonePage> ([this, url, feed] (juce::String& err) {
                       TonePage p;
                       const auto r = authed ("GET", url);
                       if (! r.ok())
                       {
                           err = r.message();
                           if (feed == Feed::search && r.status == 403)
                               err = "Searching needs TONE3000's commercial API access. Use \"Browse TONE3000...\" to search their catalogue "
                                     "in your browser, or favourite tones on tone3000.com and pick them from \"My favourites\".";
                           return p;
                       }
                       const auto j = r.json();
                       const auto data = j.isArray() ? j : j.getProperty ("data", {});
                       if (auto* arr = data.getArray())
                           for (auto& t : *arr) p.tones.add (Tone::fromVar (t));
                       p.page = juce::jmax (1, (int) j.getProperty ("page", 1));
                       p.totalPages = juce::jmax (1, (int) j.getProperty ("total_pages", 1));
                       p.total = (int) j.getProperty ("total", p.tones.size());
                       return p;
                   },
                   std::move (done));
}

void Client::getTone (int toneId, std::function<void (Tone, juce::String)> done)
{
    run<Tone> ([this, toneId] (juce::String& err) {
                   const auto r = authed ("GET", juce::URL (juce::String (origin) + "/api/v1/tones/" + juce::String (toneId)));
                   if (! r.ok()) { err = r.message(); return Tone(); }
                   return Tone::fromVar (r.json());
               },
               std::move (done));
}

void Client::listModels (int toneId, std::function<void (juce::Array<Model>, juce::String)> done)
{
    run<juce::Array<Model>> ([this, toneId] (juce::String& err) {
                                 juce::Array<Model> out;
                                 // all architectures FretForge can run: A1 / custom (default listing) and A2
                                 for (auto arch : { juce::String(), juce::String ("2") })
                                 {
                                     auto url = juce::URL (juce::String (origin) + "/api/v1/models")
                                                    .withParameter ("tone_id", juce::String (toneId))
                                                    .withParameter ("page_size", "300");
                                     if (arch.isNotEmpty()) url = url.withParameter ("architecture", arch);
                                     const auto r = authed ("GET", url);
                                     if (! r.ok()) { if (out.isEmpty() && arch.isEmpty()) err = r.message(); continue; }
                                     const auto j = r.json();
                                     const auto data = j.isArray() ? j : j.getProperty ("data", {});
                                     if (auto* arr = data.getArray())
                                         for (auto& m : *arr)
                                         {
                                             auto model = Model::fromVar (m);
                                             bool dup = false;
                                             for (auto& o : out) dup = dup || o.id == model.id;
                                             if (! dup && model.url.isNotEmpty()) out.add (model);
                                         }
                                 }
                                 if (! out.isEmpty()) err = {};
                                 return out;
                             },
                             std::move (done));
}

juce::File Client::libraryFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("FretForge").getChildFile ("TONE3000");
}

juce::String Client::safeName (const juce::String& s)
{
    auto n = juce::File::createLegalFileName (s.trim()).replaceCharacters ("/\\", "--");
    return n.isEmpty() ? juce::String ("untitled") : n.substring (0, 80).trim();
}

void Client::download (const Tone& tone, const Model& model, std::function<void (juce::File, juce::String)> done)
{
    const auto dir = libraryFolder().getChildFile (safeName (tone.title + (tone.creator.isNotEmpty() ? " - " + tone.creator : juce::String())));
    const auto target = dir.getChildFile (safeName (model.name.isNotEmpty() ? model.name : "model " + juce::String (model.id)) + model.extension (tone.format));
    if (target.existsAsFile() && target.getSize() > 0)
    {
        juce::MessageManager::callAsync ([done, target] { done (target, {}); });
        return;
    }
    const juce::URL url (model.url);
    run<juce::File> ([this, url, target] (juce::String& err) {
                         const auto r = authed ("GET", url);
                         if (! r.ok()) { err = "Download failed: " + r.message(); return juce::File(); }
                         if (r.body.getSize() == 0) { err = "Download failed: the file is empty."; return juce::File(); }
                         if (! target.getParentDirectory().createDirectory() || ! target.replaceWithData (r.body.getData(), r.body.getSize()))
                         {
                             err = "Could not write " + target.getFullPathName();
                             return juce::File();
                         }
                         return target;
                     },
                     std::move (done));
}
} // namespace ff::t3k
