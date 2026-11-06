#pragma once
#include "PluginProcessor.h"
#include "ui/UpdateCheck.h"

class DeckLook;
class Ticking;

class TekkdeckEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit TekkdeckEditor (TekkdeckProcessor&);
    ~TekkdeckEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& f, int, int) override;
    void mouseDown (const juce::MouseEvent&) override;

    TekkdeckProcessor& proc;
    void chooseSong();

private:
    void timerCallback() override;
    void rebuild();
    std::unique_ptr<DeckLook> look;
    std::vector<std::unique_ptr<juce::Component>> parts;
    std::vector<Ticking*> ticking;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<tk::UpdateBanner> update;
    juce::Rectangle<int> themeBtn { 1168, 16, 100, 32 }, loadBtn { 132, 16, 110, 32 };
    int lastTheme = -1;
    float shownL = 0, shownR = 0;
    juce::String shownInfo;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TekkdeckEditor)
};
