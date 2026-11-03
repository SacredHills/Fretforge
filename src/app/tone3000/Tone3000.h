#pragma once
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>
#include <mutex>

/** TONE3000 (tone3000.com) API v1 client: sign-in, tone lists, models, downloads.

    Sign-in uses OAuth 2 with PKCE in the user's own browser and a loopback redirect
    (http://127.0.0.1:<port>/callback, which TONE3000 accepts without registering it). The "Select" flow lets the user
    pick a tone in TONE3000's own catalogue page and hands its id back. Every request runs on a worker thread; callbacks
    arrive on the message thread. Tokens are kept in the app settings and refreshed before they expire.

    Based on the structure of TONE3000's MIT-licensed JUCE example (github.com/tone-3000/api, juce/Source/T3K). */
namespace ff::t3k
{
struct Tone
{
    int id = 0;
    juce::String title, gear, format, license, url, creator;
    juce::StringArray makes;
    int modelsCount = 0, irsCount = 0, downloadsCount = 0;
    static Tone fromVar (const juce::var&);
    juce::String countLabel() const;
};

struct Model
{
    int id = 0, toneId = 0;
    juce::String name, url, size, architecture;
    static Model fromVar (const juce::var&);
    juce::String extension (const juce::String& toneFormat) const;
};

struct TonePage
{
    juce::Array<Tone> tones;
    int page = 1, totalPages = 1, total = 0;
};

/** Small HTTP server on 127.0.0.1 that receives the OAuth redirect. */
class LoopbackServer : private juce::Thread
{
public:
    LoopbackServer() : juce::Thread ("TONE3000 sign-in") {}
    ~LoopbackServer() override { stop(); }
    bool start();
    void stop();
    juce::String redirectUri() const { return "http://127.0.0.1:" + juce::String (port.load()) + "/callback"; }
    std::function<void (juce::String query)> onCallback;   // message thread
private:
    juce::StreamingSocket listener;
    std::atomic<int> port { 0 };
    void run() override;
};

class Client : public juce::ChangeBroadcaster
{
public:
    explicit Client (juce::PropertiesFile&);
    ~Client() override;

    static constexpr const char* origin = "https://www.tone3000.com";

    // ---- configuration / session ----
    juce::String clientId() const;
    void setClientId (const juce::String&);
    bool isConnected() const;
    juce::String userName() const { return user; }
    void disconnect();

    // ---- browser flows ----
    /** Signs in (no tone picked). */
    void signIn (std::function<void (juce::String error)> done);
    /** Opens TONE3000's catalogue limited to `format` ("nam" / "ir") and `gears`; returns the picked tone id. */
    void selectTone (const juce::String& format, const juce::String& gears, std::function<void (int toneId, juce::String error)> done);
    bool flowRunning() const { return flowActive; }
    void cancelFlow();

    // ---- API ----
    enum class Feed { favorited, downloaded, created, trending, latest, search };
    void listTones (Feed, const juce::String& query, int page, std::function<void (TonePage, juce::String error)> done);
    void getTone (int id, std::function<void (Tone, juce::String error)> done);
    void listModels (int toneId, std::function<void (juce::Array<Model>, juce::String error)> done);
    /** Downloads into libraryFolder()/<tone>/; returns the file (an existing download is reused). */
    void download (const Tone&, const Model&, std::function<void (juce::File, juce::String error)> done);

    /** Where downloads go: <Documents>/FretForge/TONE3000. */
    static juce::File libraryFolder();
    static juce::String safeName (const juce::String&);

    struct Response
    {
        int status = 0;
        juce::MemoryBlock body;
        juce::String error;    // transport error (no response)
        bool ok() const { return status >= 200 && status < 300; }
        juce::var json() const { return juce::JSON::parse (body.toString()); }
        juce::String message() const;
    };
    static juce::String formEncode (const juce::StringPairArray&);

private:
    juce::PropertiesFile& settings;
    juce::String id, user;
    struct Tokens { juce::String access, refresh; juce::int64 expiresAtMs = 0; bool valid() const { return access.isNotEmpty() && refresh.isNotEmpty(); } };
    Tokens tokens;
    mutable juce::CriticalSection tokenLock;
    std::mutex refreshMutex;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("TONE3000").withNumberOfThreads (3) };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    // flow state (message thread)
    LoopbackServer server;
    bool flowActive = false;
    juce::String verifier, state, redirect;
    std::function<void (int toneId, juce::String error)> flowDone;
    void startFlow (const juce::StringPairArray& extra, std::function<void (int, juce::String)> done);
    void finishFlow (int toneId, const juce::String& error);

    static Response send (const juce::String& method, const juce::URL& url, const juce::String& headers, const juce::String& body = {});
    Response authed (const juce::String& method, const juce::URL& url);   // worker thread
    juce::String accessToken (bool force);                               // worker thread
    void storeTokens (const Tokens&, bool notify);
    Tokens currentTokens() const;
    void fetchUser();
    template <typename T>
    void run (std::function<T (juce::String& error)> job, std::function<void (T, juce::String)> done);
};
} // namespace ff::t3k
