#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// The tile you drag the finished reverse reverb out of.
class OutputTile : public juce::Component, private juce::ChangeListener
{
public:
    OutputTile();
    ~OutputTile() override;

    void setFile (const juce::File& f);
    const juce::File& getFile() const { return file; }
    void setBusy (bool b) { busy = b; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override { dragStarted = false; }
    void mouseUp (const juce::MouseEvent&) override { dragStarted = false; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    juce::File file;
    bool busy = false;
    std::atomic<bool> dragStarted { false };
    juce::Component::SafePointer<OutputTile> self { this };
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache cache { 4 };
    juce::AudioThumbnail thumb { 512, formats, cache };
};

class ReverseReverbEditor : public juce::AudioProcessorEditor,
                            public juce::FileDragAndDropTarget,
                            public juce::TextDragAndDropTarget,
                            private juce::Timer
{
public:
    explicit ReverseReverbEditor (ReverseReverbProcessor&);
    ~ReverseReverbEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

    // Clips dragged from the Cubase timeline arrive as text (a <vst-xml> description).
    bool isInterestedInTextDrag (const juce::String&) override;
    void textDragEnter (const juce::String&, int, int) override { dragHover = true; repaint(); }
    void textDragExit (const juce::String&) override { dragHover = false; repaint(); }
    void textDropped (const juce::String&, int, int) override;

private:
    void timerCallback() override;
    void loadSource (const juce::File&);
    void startRender();
    juce::uint32 renderDueAt = 0;     // debounce for the length knob (0 = nothing pending)

    ReverseReverbProcessor& proc;

    juce::Slider lengthKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label lengthLabel { {}, "Length" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lengthAttach;

    OutputTile tile;
    juce::TextButton revealButton { "Show in Finder" };
    juce::Label status;

    juce::Rectangle<int> dropArea;
    bool dragHover = false;
    double lastRenderedLength = -1.0;

    std::unique_ptr<juce::FileChooser> chooser;
    std::shared_ptr<std::atomic<int>> renderCounter = std::make_shared<std::atomic<int>> (0);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverseReverbEditor)
};
