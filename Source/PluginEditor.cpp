#include "PluginEditor.h"
#include "Renderer.h"

namespace colours
{
    const juce::Colour bg       { 0xff17181c };
    const juce::Colour panel    { 0xff23252b };
    const juce::Colour accent   { 0xff7aa2ff };
    const juce::Colour text     { 0xffe6e7ea };
    const juce::Colour dimText  { 0xff8a8d96 };
}

//==============================================================================
OutputTile::OutputTile()
{
    formats.registerBasicFormats();
    thumb.addChangeListener (this);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

OutputTile::~OutputTile() { thumb.removeChangeListener (this); }

void OutputTile::setFile (const juce::File& f)
{
    file = f;
    cache.clear();
    if (file.existsAsFile())
        thumb.setSource (new juce::FileInputSource (file));
    else
        thumb.clear();
    repaint();
}

void OutputTile::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 8.0f);

    auto inner = getLocalBounds().reduced (12);
    auto caption = inner.removeFromBottom (20);

    if (file.existsAsFile() && thumb.getTotalLength() > 0)
    {
        g.setColour (colours::accent.withAlpha (busy ? 0.3f : 0.9f));
        thumb.drawChannels (g, inner, 0.0, thumb.getTotalLength(), 1.0f);

        g.setColour (colours::text);
        g.setFont (13.0f);
        g.drawText (busy ? "Rendering..." : juce::String::fromUTF8 ("Drag me into your project  \xe2\x86\x92  line my end up with the start of the sound"),
                    caption, juce::Justification::centred, true);
    }
    else
    {
        g.setColour (colours::dimText);
        g.setFont (14.0f);
        g.drawText (busy ? "Rendering..." : "Your reverse reverb will appear here",
                    inner, juce::Justification::centred, true);
    }
}

void OutputTile::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || busy || ! file.existsAsFile() || e.getDistanceFromDragStart() < 4)
        return;

    dragStarted = true;

    // macOS runs its own drag loop, so mouseUp never reaches us after a drag. Reset the flag
    // when the drag session ends, otherwise only the first drag per editor window works.
    auto safe = self;
    const bool ok = juce::DragAndDropContainer::performExternalDragDropOfFiles (
        { file.getFullPathName() }, false, this,
        [safe] { if (safe != nullptr) safe->dragStarted = false; });

    if (! ok)
        dragStarted = false;
}

//==============================================================================
ReverseReverbEditor::ReverseReverbEditor (ReverseReverbProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    lengthKnob.setColour (juce::Slider::rotarySliderFillColourId, colours::accent);
    lengthKnob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lengthKnob.setTextValueSuffix (" s");
    lengthKnob.setNumDecimalPlacesToDisplay (1);
    addAndMakeVisible (lengthKnob);
    lengthAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "length", lengthKnob);
    // Re-render shortly after the knob stops moving.
    lengthKnob.onValueChange = [this] { renderDueAt = juce::Time::getMillisecondCounter() + 350; };


    lengthLabel.setJustificationType (juce::Justification::centred);
    lengthLabel.setColour (juce::Label::textColourId, colours::text);
    addAndMakeVisible (lengthLabel);

    addAndMakeVisible (tile);

    revealButton.onClick = [this]
    {
        if (tile.getFile().existsAsFile())
            tile.getFile().revealToUser();
        else
            getDefaultRenderFolder().getParentDirectory().revealToUser();
    };
    addAndMakeVisible (revealButton);

    status.setColour (juce::Label::textColourId, colours::dimText);
    status.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (status);

    setSize (560, 360);
    startTimerHz (30);

    if (proc.sourcePath.isNotEmpty() && juce::File (proc.sourcePath).existsAsFile())
        startRender();
}

ReverseReverbEditor::~ReverseReverbEditor()
{
    renderCounter->fetch_add (1); // invalidate any render in flight
}

void ReverseReverbEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);

    g.setColour (colours::text);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("Reverse Reverb", 16, 10, 300, 24, juce::Justification::centredLeft);

    auto r = dropArea.toFloat();
    g.setColour (dragHover ? colours::accent.withAlpha (0.2f) : colours::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (dragHover ? colours::accent : colours::dimText.withAlpha (0.5f));
    g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, 1.5f);

    const juce::File src (proc.sourcePath);
    g.setFont (14.0f);
    if (src.existsAsFile())
    {
        g.setColour (colours::text);
        g.drawFittedText (src.getFileName(), dropArea.reduced (12).withTrimmedBottom (18), juce::Justification::centred, 2);
        g.setColour (colours::dimText);
        g.setFont (12.0f);
        g.drawText ("Drop another sound to replace", dropArea.reduced (8).removeFromBottom (18), juce::Justification::centred);
    }
    else
    {
        g.setColour (colours::dimText);
        g.drawFittedText ("Drag a clip or audio file here\n(or click to browse)", dropArea.reduced (12), juce::Justification::centred, 2);
    }
}

void ReverseReverbEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (30);

    auto bottom = area.removeFromBottom (28);
    revealButton.setBounds (bottom.removeFromRight (130));
    status.setBounds (bottom);
    area.removeFromBottom (10);

    auto top = area.removeFromTop (130);
    auto knobArea = top.removeFromRight (130);
    lengthLabel.setBounds (knobArea.removeFromTop (18));
    lengthKnob.setBounds (knobArea);
    top.removeFromRight (12);
    dropArea = top;


    area.removeFromTop (12);
    tile.setBounds (area);
}

void ReverseReverbEditor::mouseUp (const juce::MouseEvent& e)
{
    if (! dropArea.contains (e.getPosition()))
        return;

    chooser = std::make_unique<juce::FileChooser> ("Choose a sound", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.caf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  loadSource (fc.getResult());
                          });
}

bool ReverseReverbEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() > 0 && juce::File (files[0]) != tile.getFile() && juce::File (files[0]).existsAsFile();
}

void ReverseReverbEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();
    if (files.size() > 0)
        loadSource (juce::File (files[0]));
}

static juce::File fileFromDragText (const juce::String& text)
{
    auto t = text.trim().upToFirstOccurrenceOf ("\n", false, false).trim();
    if (t.startsWith ("file://"))
        return juce::URL (t).getLocalFile();
    if (t.startsWith ("/"))
        return juce::File (t);
    return {};
}

bool ReverseReverbEditor::isInterestedInTextDrag (const juce::String& text)
{
    return isCubaseClipDrag (text) || fileFromDragText (text).existsAsFile();
}

void ReverseReverbEditor::textDropped (const juce::String& text, int, int)
{
    dragHover = false;
    repaint();

    if (! isCubaseClipDrag (text))
    {
        auto f = fileFromDragText (text);
        if (f.existsAsFile())
            loadSource (f);
        return;
    }

    status.setText ("Reading clip from Cubase...", juce::dontSendNotification);
    juce::Component::SafePointer<ReverseReverbEditor> safe (this);
    juce::Thread::launch ([text, safe]
    {
        auto result = extractCubaseClip (text);
        juce::MessageManager::callAsync ([result, safe]
        {
            if (safe == nullptr)
                return;
            if (result.error.isNotEmpty())
                safe->status.setText (result.error, juce::dontSendNotification);
            else
                safe->loadSource (result.file);
        });
    });
}

void ReverseReverbEditor::loadSource (const juce::File& f)
{
    proc.sourcePath = f.getFullPathName();
    repaint();
    startRender();
}

void ReverseReverbEditor::timerCallback()
{
    if (renderDueAt != 0 && juce::Time::getMillisecondCounter() >= renderDueAt)
    {
        renderDueAt = 0;
        if (std::abs (lengthKnob.getValue() - lastRenderedLength) > 0.01)
            startRender();
    }
}

void ReverseReverbEditor::startRender()
{
    const juce::File src (proc.sourcePath);
    if (! src.existsAsFile())
        return;

    const double length = lengthKnob.getValue();
    lastRenderedLength = length;
    const int id = ++(*renderCounter);
    auto counter = renderCounter;
    juce::Component::SafePointer<ReverseReverbEditor> safe (this);

    tile.setBusy (true);
    status.setText ("Rendering " + juce::String (length, 1) + " s reverse reverb...", juce::dontSendNotification);

    juce::Thread::launch ([src, length, id, counter, safe]
    {
        auto result = renderReverseReverb (src, length, getDefaultRenderFolder());

        juce::MessageManager::callAsync ([result, id, counter, safe]
        {
            if (safe == nullptr || counter->load() != id)
                return;

            safe->tile.setBusy (false);
            if (result.error.isNotEmpty())
            {
                safe->status.setText (result.error, juce::dontSendNotification);
                return;
            }
            safe->tile.setFile (result.file);
            safe->status.setText ("Saved: " + result.file.getFileName(), juce::dontSendNotification);
        });
    });
}
