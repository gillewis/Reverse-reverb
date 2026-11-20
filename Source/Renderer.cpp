#include "Renderer.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

juce::File getDefaultRenderFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("Reverse Reverb Renders");
}

namespace
{
    bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, int start, int numSamples, double sr)
    {
        file.deleteFile();
        auto stream = std::make_unique<juce::FileOutputStream> (file);
        if (! stream->openedOk())
            return false;
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0));
        if (writer == nullptr)
            return false;
        stream.release();
        return writer->writeFromAudioSampleBuffer (audio, start, numSamples);
    }
}

bool isCubaseClipDrag (const juce::String& text)
{
    return text.contains ("<vst-xml") && text.contains ("<filename>");
}

RenderResult extractCubaseClip (const juce::String& xmlText)
{
    auto xml = juce::parseXML (xmlText);
    if (xml == nullptr)
        return { {}, "Couldn't read the clip Cubase sent." };

    std::function<juce::XmlElement* (juce::XmlElement*)> findRegion = [&] (juce::XmlElement* e) -> juce::XmlElement*
    {
        if (e->hasTagName ("region") && e->getChildByName ("filename") != nullptr)
            return e;
        for (auto* c : e->getChildIterator())
            if (auto* r = findRegion (c))
                return r;
        return nullptr;
    };
    auto* region = findRegion (xml.get());
    if (region == nullptr)
        return { {}, "Cubase sent a clip without audio info." };

    const juce::File file (region->getChildElementAllSubText ("filename", {}).trim());
    const auto startText = region->getChildElementAllSubText ("start", {}).trim();
    const auto endText   = region->getChildElementAllSubText ("end", {}).trim();

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return { {}, "Couldn't open " + file.getFileName() };

    const juce::int64 total = reader->lengthInSamples;
    juce::int64 start = startText.isNotEmpty() ? startText.getLargeIntValue() : 0;
    juce::int64 end   = endText.isNotEmpty() ? endText.getLargeIntValue() + 1 : total; // <end> is inclusive
    start = juce::jlimit<juce::int64> (0, total, start);
    end   = juce::jlimit<juce::int64> (start, total, end);

    const juce::int64 maxLen = (juce::int64) (reader->sampleRate * 30.0);
    if (end - start > maxLen)
        return { {}, "That clip is longer than 30 seconds - split off the bit you want reversed." };
    if (end <= start)
        return { {}, "That clip is empty." };

    const int n = (int) (end - start);
    juce::AudioBuffer<float> audio (2, n);
    audio.clear();
    reader->read (&audio, 0, n, start, true, true);
    if (reader->numChannels == 1)
        audio.copyFrom (1, 0, audio, 0, 0, n);

    return saveClip (audio, n, reader->sampleRate, file.getFileNameWithoutExtension());
}

RenderResult saveClip (const juce::AudioBuffer<float>& audio, int numSamples, double sampleRate,
                          const juce::String& name)
{
    const float threshold = juce::Decibels::decibelsToGain (-50.0f);
    auto loud = [&] (int i) { return std::abs (audio.getSample (0, i)) > threshold
                                  || std::abs (audio.getSample (1, i)) > threshold; };

    int first = 0;
    while (first < numSamples && ! loud (first))
        ++first;
    if (first >= numSamples)
        return { {}, "That clip is silent." };

    int last = numSamples - 1;
    while (last > first && ! loud (last))
        --last;

    // Start right at the sound so the reverse reverb lines up with its onset; keep a little tail.
    first = juce::jmax (0, first - (int) (0.002 * sampleRate));
    last = juce::jmin (numSamples - 1, last + (int) (0.05 * sampleRate));

    auto dir = getDefaultRenderFolder().getChildFile ("Clips");
    if (! dir.createDirectory())
        return { {}, "Couldn't create " + dir.getFullPathName() };

    // Drop Cubase's long "-Reverse-<hex id>" style suffixes to keep names readable.
    auto base = name.isNotEmpty() ? name.upToFirstOccurrenceOf ("-", false, false).trim() : juce::String ("Clip");
    if (base.isEmpty())
        base = "Clip";
    auto file = dir.getChildFile (juce::File::createLegalFileName (
        base + " " + juce::Time::getCurrentTime().formatted ("%H-%M-%S") + ".wav"));
    if (! writeWav (file, audio, first, last - first + 1, sampleRate))
        return { {}, "Couldn't save the clip audio." };

    return { file, {} };
}

namespace
{
    // Stereo reverb impulse response: decorrelated noise with an exponential decay
    // (60 dB over the full length) that gets darker as it decays.
    juce::AudioBuffer<float> makeImpulseResponse (int length, double sampleRate)
    {
        juce::AudioBuffer<float> ir (2, length);
        const int fadeIn = juce::jmax (1, (int) (0.003 * sampleRate));

        for (int ch = 0; ch < 2; ++ch)
        {
            juce::Random rng (ch == 0 ? 0x5eed1 : 0x5eed2);
            auto* d = ir.getWritePointer (ch);
            float lp = 0.0f;

            for (int i = 0; i < length; ++i)
            {
                const double t = (double) i / (double) length;
                const double env = std::exp (-6.9078 * t);                 // -60 dB at the end
                const double cutoff = 12000.0 * std::pow (2500.0 / 12000.0, t); // darkens over time
                const float a = (float) std::exp (-juce::MathConstants<double>::twoPi
                                                  * juce::jmin (cutoff, sampleRate * 0.45) / sampleRate);
                const float noise = rng.nextFloat() * 2.0f - 1.0f;
                lp = (1.0f - a) * noise + a * lp;

                float g = (float) env;
                if (i < fadeIn)
                    g *= (float) i / (float) fadeIn;

                d[i] = lp * g;
            }
        }
        return ir;
    }
}

RenderResult renderReverseReverb (const juce::File& source, double lengthSeconds, const juce::File& outputDir)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
    if (reader == nullptr)
        return { {}, "Couldn't read that file. Try a WAV or AIFF." };

    const double sr = reader->sampleRate;
    if (sr <= 0 || reader->lengthInSamples <= 0)
        return { {}, "That file looks empty." };

    if (reader->lengthInSamples > (juce::int64) (sr * 30.0))
        return { {}, "Source is longer than 30 seconds - trim it to the bit you want reversed." };

    const int N = (int) reader->lengthInSamples;
    const int L = juce::jmax (1, (int) std::round (lengthSeconds * sr));

    juce::AudioBuffer<float> in (2, N);
    in.clear();
    reader->read (&in, 0, N, 0, true, true);
    if (reader->numChannels == 1)
        in.copyFrom (1, 0, in, 0, 0, N);

    // reverse(reverb(reverse(x))) -> convolve the reversed source with the IR, then reverse.
    in.reverse (0, N);
    auto ir = makeImpulseResponse (L, sr);

    const int total = N + L - 1;
    int order = 1;
    while ((1 << order) < total)
        ++order;
    const int fftSize = 1 << order;

    juce::dsp::FFT fft (order);
    std::vector<float> a ((size_t) fftSize * 2), b ((size_t) fftSize * 2);

    juce::AudioBuffer<float> out (2, L);

    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (a.begin(), a.end(), 0.0f);
        std::fill (b.begin(), b.end(), 0.0f);
        std::copy (in.getReadPointer (ch), in.getReadPointer (ch) + N, a.begin());
        std::copy (ir.getReadPointer (ch), ir.getReadPointer (ch) + L, b.begin());

        fft.performRealOnlyForwardTransform (a.data());
        fft.performRealOnlyForwardTransform (b.data());

        auto* ca = reinterpret_cast<std::complex<float>*> (a.data());
        auto* cb = reinterpret_cast<std::complex<float>*> (b.data());
        for (int k = 0; k < fftSize; ++k)
            ca[k] *= cb[k];

        fft.performRealOnlyInverseTransform (a.data());

        // w = a[0..total). Output y[j] = w[total-1-j] for j in [0, L):
        // the last output sample lines up with the first sample of the source.
        auto* o = out.getWritePointer (ch);
        for (int j = 0; j < L; ++j)
            o[j] = a[(size_t) (total - 1 - j)];
    }

    // Normalise to -1 dBFS.
    const float peak = out.getMagnitude (0, L);
    if (peak > 0.0f)
        out.applyGain (juce::Decibels::decibelsToGain (-1.0f) / peak);

    if (! outputDir.createDirectory())
        return { {}, "Couldn't create " + outputDir.getFullPathName() };

    auto outFile = outputDir.getChildFile (juce::File::createLegalFileName (
        source.getFileNameWithoutExtension() + " - reverse reverb "
        + juce::String (lengthSeconds, 1) + "s.wav"));
    if (! writeWav (outFile, out, 0, L, sr))
        return { {}, "Couldn't write " + outFile.getFullPathName() };

    return { outFile, {} };
}
