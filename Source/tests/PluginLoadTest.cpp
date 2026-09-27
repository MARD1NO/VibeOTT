/*
    Loads the built VST3 bundle through JUCE's own VST3 implementation and checks
    it produces a working plugin instance.

    This replaces an earlier check that grepped a symbol name out of `nm` /
    `dumpbin` output. That check was a bad idea: the exported name differs per
    object format (_GetPluginFactory on Mach-O and ELF, the bare name in a PE
    export table), which tools even exist differs per platform, and none of it
    could be verified without a machine of each kind. It passed on Linux, broke
    on Windows and macOS, and proved very little either way.

    Actually loading the bundle proves the thing that matters -- that a host can
    open this file and get an AudioProcessor out of it -- using one code path on
    every platform, with no shell tooling at all.

    Usage:  VibeOTTPluginLoadTest <path-to-.vst3>
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdio>
#include <cstdlib>

namespace
{

int failures = 0;

void check (bool condition, const juce::String& what)
{
    std::printf ("  %s %s\n", condition ? "ok  " : "FAIL", what.toRawUTF8());

    if (! condition)
        ++failures;
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // A bare invocation is a hard error: silently passing on no argument is how
    // a verification step ends up checking nothing.
    if (argc < 2)
    {
        std::printf ("FAIL no bundle path given\n");
        std::printf ("usage: %s <path-to-.vst3>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const juce::File bundle (juce::String (argv[1]).unquoted());
    const juce::String expectedName (VIBEOTT_PLUGIN_NAME);

    std::printf ("VibeOTT plugin load test\n\n");
    std::printf ("bundle: %s\n", bundle.getFullPathName().toRawUTF8());

    if (! bundle.exists())
    {
        std::printf ("FAIL the bundle does not exist\n");
        return EXIT_FAILURE;
    }

    // The VST3 binary lives at a fixed place inside the bundle, and the shape
    // differs per platform. Report what is actually there so a failure is
    // diagnosable without another CI round trip.
    const juce::StringArray candidates = {
        "Contents/MacOS/" + expectedName,
        "Contents/x86_64-linux/" + expectedName + ".so",
        "Contents/x86_64-win/" + expectedName + ".vst3",
    };

    juce::String foundBinary;

    for (const auto& candidate : candidates)
    {
        const auto file = bundle.getChildFile (candidate);

        if (file.existsAsFile())
        {
            foundBinary = candidate;
            break;
        }
    }

    if (foundBinary.isEmpty())
    {
        std::printf ("FAIL no plugin binary inside the bundle. Contents:\n");

        for (const auto& entry : juce::RangedDirectoryIterator (bundle, true, "*",
                                                                juce::File::findFiles))
            std::printf ("       %s\n",
                         entry.getFile().getRelativePathFrom (bundle).toRawUTF8());

        return EXIT_FAILURE;
    }

    std::printf ("binary: %s\n\n", foundBinary.toRawUTF8());

    // Ask JUCE's own VST3 implementation to open it. JUCE 8 no longer exposes a
    // public VST3PluginFormat type, so go through the format manager.
    juce::AudioPluginFormatManager formatManager;
    juce::addDefaultFormatsToManager (formatManager);

    auto* format = formatManager.getFormat (0);

    for (int i = 0; i < formatManager.getNumFormats(); ++i)
        if (formatManager.getFormat (i)->getName() == "VST3")
            format = formatManager.getFormat (i);

    if (format == nullptr || format->getName() != "VST3")
    {
        std::printf ("FAIL this JUCE build has no VST3 format available\n");
        return EXIT_FAILURE;
    }

    juce::OwnedArray<juce::PluginDescription> descriptions;
    juce::KnownPluginList knownList;

    // findAllTypesForFile wants the file to be in a place it recognises; the
    // bundle path itself is what matters here.
    format->findAllTypesForFile (descriptions, bundle.getFullPathName());

    std::printf ("[discovery]\n");
    std::printf ("  found %d plugin type(s)\n", descriptions.size());

    check (descriptions.size() > 0, "the bundle exposes at least one plugin type");

    if (descriptions.isEmpty())
    {
        std::printf ("\n%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }

    // Prefer the one we actually build, in case the bundle ever gains siblings.
    juce::PluginDescription* description = descriptions[0];

    for (auto* candidate : descriptions)
        if (candidate->name == expectedName)
            description = candidate;

    std::printf ("  name:     %s\n", description->name.toRawUTF8());
    std::printf ("  format:   %s\n", description->pluginFormatName.toRawUTF8());
    std::printf ("  channels: in %d, out %d\n",
                 description->numInputChannels, description->numOutputChannels);

    check (description->name == expectedName,
           "the reported plugin name is " + expectedName);
    check (description->pluginFormatName == "VST3", "the format is reported as VST3");
    check (description->isInstrument == false, "the plugin is not reported as an instrument");

    // The real test: instantiate it, exactly as a host would.
    std::printf ("\n[instantiation]\n");

    juce::String error;
    auto instance = formatManager.createPluginInstance (*description, 48000.0, 512, error);

    if (instance == nullptr)
    {
        std::printf ("  FAIL could not instantiate: %s\n", error.toRawUTF8());
        std::printf ("\n%d failure(s)\n", ++failures);
        return EXIT_FAILURE;
    }

    check (true, "a plugin instance was created");

    instance->prepareToPlay (48000.0, 512);

    // Push some audio through, so a plugin that instantiates but cannot process
    // is still caught.
    juce::AudioBuffer<float> buffer (juce::jmax (1, instance->getTotalNumOutputChannels()), 512);
    buffer.clear();

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample (0, i, 0.25f * std::sin (juce::MathConstants<float>::twoPi * 440.0f
                                                    * (float) i / 48000.0f));

    juce::MidiBuffer midi;
    instance->processBlock (buffer, midi);

    bool finite = true;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            finite = finite && std::isfinite (buffer.getSample (ch, i));

    check (finite, "processBlock produced finite output");

    juce::MemoryBlock state;
    instance->getStateInformation (state);

    check (state.getSize() > 0, "the loaded instance serialises its state");

    instance->releaseResources();

    std::printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
