#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/Deck.h"

constexpr int NUM_CUES = 8;

/** Vorberechnete Pegel fuer die Wellenform-Anzeige. */
struct Peaks
{
    static constexpr int STEP = 256;
    std::vector<float> full, bass;
};

class TekkdeckProcessor : public juce::AudioProcessor, private juce::Thread
{
public:
    TekkdeckProcessor();
    ~TekkdeckProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Tekkdeck"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Tekkdeck"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    td::Deck deck;

    // Song
    void loadSong (const juce::File&);
    const td::Song* song() const { return songPtr.load (std::memory_order_acquire); }
    const td::SongGrid* grid() const { return gridPtr.load (std::memory_order_acquire); }
    const Peaks* peaks() const { return peaksPtr.load (std::memory_order_acquire); }
    std::atomic<int> version { 0 };
    std::atomic<bool> busy { false };
    juce::String statusText() const { const juce::ScopedLock l (textLock); return status; }

    // Live
    void triggerCue (int index);                 // leer = setzen, belegt = springen
    void clearCue (int index);
    void seekToSeconds (double seconds);
    std::atomic<double> cues[NUM_CUES];          // Sekunden im Song, < 0 = leer
    int autoKeyShift (double hostBpm) const;

    // Raster korrigieren
    void gridDouble();
    void gridHalve();
    void gridNudge (double ms);
    void gridSetOne();

    std::atomic<float> meterL { 0 }, meterR { 0 };
    std::atomic<double> hostBpm { 170 };
    float pf (const char* id) const { return apvts.getRawParameterValue (id)->load (std::memory_order_relaxed); }

private:
    void run() override;
    void installGrid (std::unique_ptr<td::SongGrid>);
    void keepPositionOn (const td::SongGrid& next);
    void installSong (std::unique_ptr<td::Song>, std::unique_ptr<td::SongGrid>, std::unique_ptr<Peaks>);
    void setStatus (const juce::String& s) { const juce::ScopedLock l (textLock); status = s; }
    double quantBeats() const;
    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();

    std::atomic<const td::Song*> songPtr { nullptr };
    std::atomic<const td::SongGrid*> gridPtr { nullptr };
    std::atomic<const Peaks*> peaksPtr { nullptr };
    std::unique_ptr<td::Song> liveSong; std::unique_ptr<td::SongGrid> liveGrid; std::unique_ptr<Peaks> livePeaks;
    struct Old { std::shared_ptr<void> data; juce::uint32 when; };
    std::vector<Old> retired;                    // erst spaeter loeschen, Audio-Thread und Anzeige koennten noch lesen
    void retire (std::shared_ptr<void>);
    juce::CriticalSection retireLock, textLock;
    juce::String status;

    juce::AudioFormatManager formats;
    juce::File pending; std::unique_ptr<td::SongGrid> pendingGrid; juce::CriticalSection pendingLock;
    double rate = 44100, freePpq = 0;
    bool lastPlay = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TekkdeckProcessor)
};
