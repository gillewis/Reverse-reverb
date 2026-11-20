#pragma once
#include <juce_audio_formats/juce_audio_formats.h>

struct RenderResult
{
    juce::File file;
    juce::String error;
};

// Renders a reverse reverb of `source`. The output is exactly `lengthSeconds` long
// and is built so that its END lines up with the START of the source sound:
// place the end of the rendered file where the source begins.
RenderResult renderReverseReverb (const juce::File& source, double lengthSeconds, const juce::File& outputDir);

juce::File getDefaultRenderFolder();

// Trims silence off both ends of the audio and saves it as a WAV in the Clips folder.
RenderResult saveClip (const juce::AudioBuffer<float>& audio, int numSamples, double sampleRate,
                          const juce::String& name = {});

// Cubase puts a <vst-xml> description of a dragged clip on the drag (file path + start/end
// sample positions). This cuts that range out of the file and saves it with saveClip.
bool isCubaseClipDrag (const juce::String& text);
RenderResult extractCubaseClip (const juce::String& xmlText);
