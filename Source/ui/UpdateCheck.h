// Tekkforge und Tekkdeck - prueft beim Oeffnen GitHub und installiert Updates erst nach einem Klick.
// Geladen wird nur der Installer aus dem Release des eigenen Repositorys. Windows fragt danach nach Adminrechten.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace tk
{
/** true, wenn "v1.2.0" neuer ist als "1.1.9". */
inline bool versionIsNewer (juce::String remote, juce::String local)
{
    const auto parts = [] (juce::String s)
    {
        auto a = juce::StringArray::fromTokens (s.trimCharactersAtStart ("vV"), ".", "");
        while (a.size() < 3) a.add ("0");
        return a;
    };
    const auto r = parts (remote), l = parts (local);
    for (int i = 0; i < 3; ++i)
    {
        if (r[i].getIntValue() > l[i].getIntValue()) return true;
        if (r[i].getIntValue() < l[i].getIntValue()) return false;
    }
    return false;
}

class UpdateChecker : private juce::Thread
{
public:
    enum State { NOTHING = 0, AVAILABLE, DOWNLOADING, READY, FAILED };

    UpdateChecker (juce::String ownerAndRepo, juce::String currentVersion)
        : juce::Thread ("Update-Pruefung"), repo (std::move (ownerAndRepo)), current (std::move (currentVersion))
    {
        startThread (juce::Thread::Priority::low);
    }
    ~UpdateChecker() override { stopThread (7000); }

    int state() const { return stateNow.load(); }
    int percent() const { return progress.load(); }
    juce::String version() const { const juce::ScopedLock l (lock); return latest; }
    juce::String link() const { const juce::ScopedLock l (lock); return page; }
    bool hasInstaller() const { const juce::ScopedLock l (lock); return installerUrl.isNotEmpty(); }
    juce::File installer() const { const juce::ScopedLock l (lock); return installerFile; }
    void startDownload() { stateNow.store (DOWNLOADING); notify(); }

private:
    void run() override
    {
        check();
        if (stateNow.load() != AVAILABLE) return;
        while (! threadShouldExit() && stateNow.load() == AVAILABLE) wait (500);
        if (! threadShouldExit() && stateNow.load() == DOWNLOADING) download();
    }
    void check()
    {
        int status = 0;
        auto stream = juce::URL ("https://api.github.com/repos/" + repo + "/releases/latest")
            .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs (5000)
                .withStatusCode (&status)
                .withExtraHeaders ("User-Agent: Tekk-Plugin\r\nAccept: application/vnd.github+json"));
        if (stream == nullptr || status != 200 || threadShouldExit()) return;
        const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
        const auto tag = json.getProperty ("tag_name", {}).toString();
        const auto url = json.getProperty ("html_url", {}).toString();
        if (tag.isEmpty() || ! url.startsWith ("https://github.com/") || ! versionIsNewer (tag, current)) return;

        // Nur den Installer aus genau diesem Repository akzeptieren
        juce::String exe;
        if (auto* assets = json.getProperty ("assets", {}).getArray())
            for (const auto& a : *assets)
            {
                const auto name = a.getProperty ("name", {}).toString();
                const auto link2 = a.getProperty ("browser_download_url", {}).toString();
                if (name.endsWith ("-Setup.exe") && link2.startsWith ("https://github.com/" + repo + "/releases/download/")) exe = link2;
            }
        { const juce::ScopedLock l (lock); latest = tag.trimCharactersAtStart ("vV"); page = url; installerUrl = exe; }
        stateNow.store (AVAILABLE);
    }
    void download()
    {
        juce::String url; { const juce::ScopedLock l (lock); url = installerUrl; }
        const auto target = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Tekk-Update").getChildFile (juce::URL (url).getFileName());
        target.getParentDirectory().createDirectory();
        target.deleteFile();
        int status = 0;
        auto in = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                         .withConnectionTimeoutMs (15000).withStatusCode (&status).withNumRedirectsToFollow (5)
                                                         .withExtraHeaders ("User-Agent: Tekk-Plugin"));
        if (in == nullptr || status != 200) { stateNow.store (FAILED); return; }
        bool ok = false;
        {
            juce::FileOutputStream out (target);
            if (! out.failedToOpen())
            {
                const auto total = in->getTotalLength();
                juce::int64 done = 0; juce::HeapBlock<char> buffer (65536);
                while (! threadShouldExit())
                {
                    const int got = in->read (buffer, 65536);
                    if (got <= 0) break;
                    out.write (buffer, (size_t) got); done += got;
                    if (total > 0) progress.store ((int) (done * 100 / total));
                }
                ok = ! threadShouldExit() && done > 100000 && (total <= 0 || done == total);
            }
        }
        if (! ok) { target.deleteFile(); stateNow.store (FAILED); return; }
        { const juce::ScopedLock l (lock); installerFile = target; }
        stateNow.store (READY);
    }
    juce::String repo, current, latest, page, installerUrl;
    juce::File installerFile;
    juce::CriticalSection lock;
    std::atomic<int> stateNow { NOTHING }, progress { 0 };
};

/** Knopf im Fenster. Erst sichtbar, wenn ein Update bereitsteht. Installiert wird nur nach Klick und Rueckfrage. */
class UpdateBanner : public juce::Component, private juce::Timer
{
public:
    UpdateBanner (juce::String ownerAndRepo, juce::String currentVersion) : checker (std::move (ownerAndRepo), std::move (currentVersion))
    {
        setVisible (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        startTimerHz (2);
    }
    std::function<juce::Colour()> fill, text;

    void paint (juce::Graphics& g) override
    {
        g.setColour (fill != nullptr ? fill() : juce::Colours::orange);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        g.setColour (text != nullptr ? text() : juce::Colours::black);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        juce::String t;
        switch (checker.state())
        {
            case UpdateChecker::DOWNLOADING: t = juce::String::fromUTF8 ("Lädt ") + juce::String (checker.percent()) + " %"; break;
            case UpdateChecker::READY:       t = juce::String::fromUTF8 ("Installer läuft"); break;
            case UpdateChecker::FAILED:      t = juce::String::fromUTF8 ("Download fehlgeschlagen"); break;
            default:                         t = juce::String::fromUTF8 ("Update ") + checker.version() + juce::String::fromUTF8 (" installieren"); break;
        }
        g.drawFittedText (t, getLocalBounds().reduced (6, 0), juce::Justification::centred, 1, 0.8f);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        const int st = checker.state();
        if (st == UpdateChecker::FAILED || (st == UpdateChecker::AVAILABLE && ! checker.hasInstaller()))
        {
            juce::URL (checker.link()).launchInDefaultBrowser();
            return;
        }
        if (st != UpdateChecker::AVAILABLE) return;
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::InfoIcon, juce::String::fromUTF8 ("Update installieren"),
            juce::String::fromUTF8 ("Version ") + checker.version() + juce::String::fromUTF8 (" wird geladen und installiert. Speichere vorher dein Projekt: "
                                    "Der Installer bittet dich, FL Studio zu schließen."),
            juce::String::fromUTF8 ("Installieren"), juce::String::fromUTF8 ("Später"), this,
            juce::ModalCallbackFunction::create ([sp = juce::Component::SafePointer<UpdateBanner> (this)] (int r)
            {
                if (sp != nullptr && r == 1) { sp->checker.startDownload(); sp->repaint(); }
            }));
    }
private:
    void timerCallback() override
    {
        const int st = checker.state();
        if (st == UpdateChecker::NOTHING) return;
        setVisible (true);
        if (st == UpdateChecker::READY && ! launched) { launched = true; checker.installer().startAsProcess(); stopTimer(); }
        repaint();
    }
    UpdateChecker checker;
    bool launched = false;
};
} // namespace tk
