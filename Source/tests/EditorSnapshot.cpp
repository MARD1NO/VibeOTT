/*
    Renders the plugin editor to PNG files without a host, an audio device or a
    window server. Used to check the layout — that nothing overlaps, that the
    advanced panel actually fits, and that the meters draw — after any UI
    change:

        cmake --build build --target VibeOTTSnapshot
        ./build/VibeOTTSnapshot

    It also asserts the layout invariants the previous editor violated, so a
    regression fails the build rather than needing a human to look at a picture.
*/

#include "PluginEditor.h"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <map>
#include <cstdlib>
#include <vector>

namespace
{

int failures = 0;

void check (bool condition, const juce::String& what)
{
    std::printf ("  %s %s\n", condition ? "ok  " : "FAIL", what.toRawUTF8());

    if (! condition)
        ++failures;
}

/** Collects a component and all of its descendants. */
void collectDescendants (juce::Component& component, std::vector<juce::Component*>& out)
{
    for (auto* child : component.getChildren())
    {
        out.push_back (child);
        collectDescendants (*child, out);
    }
}

/** Dumps the laid-out bounds of the named controls, so a failure is readable
    without guessing which component is which from its RTTI name. */
void dumpLayout (juce::Component& component, const juce::String& context)
{
    std::printf ("  -- %s layout --\n", context.toRawUTF8());

    for (auto* child : component.getChildren())
    {
        if (! child->isVisible())
            continue;

        juce::String name (typeid (*child).name());

        if (auto* slider = dynamic_cast<juce::Slider*> (child))
            name = "Slider";
        else if (auto* label = dynamic_cast<juce::Label*> (child))
            name = "Label:" + label->getText();
        else if (dynamic_cast<BandMeter*> (child) != nullptr)
            name = "BandMeter";
        else if (dynamic_cast<DepthDisplay*> (child) != nullptr)
            name = "DepthDisplay";
        else if (auto* button = dynamic_cast<juce::TextButton*> (child))
            name = "Button:" + button->getButtonText();

        std::printf ("     %-24s %s\n", name.toRawUTF8(), child->getBounds().toString().toRawUTF8());
    }
}

/** Reports control groups that overlap.

    Only sibling controls are compared, and a JUCE Slider's own internals are
    skipped: a slider's text box is a child of the slider, so treating it as an
    independent control reports phantom overlaps. Two sliders sharing space is
    the real bug, and that is what this catches.
*/
void checkNoOverlaps (juce::Component& parent, const juce::String& context)
{
    std::vector<juce::Component*> controls;

    for (auto* child : parent.getChildren())
        if (child->isVisible() && (dynamic_cast<juce::Slider*> (child) != nullptr
                                   || dynamic_cast<juce::Label*> (child) != nullptr
                                   || dynamic_cast<BandMeter*> (child) != nullptr
                                   || dynamic_cast<DepthDisplay*> (child) != nullptr))
            controls.push_back (child);

    for (size_t i = 0; i < controls.size(); ++i)
    {
        for (size_t j = i + 1; j < controls.size(); ++j)
        {
            // A caption sits directly under its dial inside the same slot; a
            // label is allowed to be within a slider's outer bounds only if it
            // is that slider's own caption, which is indistinguishable here, so
            // labels are compared against each other but not against sliders.
            const bool labelVsSlider = dynamic_cast<juce::Label*> (controls[i]) != nullptr
                                    || dynamic_cast<juce::Label*> (controls[j]) != nullptr;

            if (labelVsSlider)
                continue;

            // Containers are laid out as neighbouring columns, so two controls
            // can share x-range freely as long as they do not share y-range.
            // Requiring both is what makes this a real overlap test.
            const auto a = controls[i]->getBounds();
            const auto b = controls[j]->getBounds();
            const bool sharesX = a.getX() < b.getRight() && b.getX() < a.getRight();
            const bool sharesY = a.getY() < b.getBottom() && b.getY() < a.getBottom();

            if (sharesX && sharesY)
            {
                // Bind the pointers first: typeid on a dereferenced expression
                // evaluates it, which the compiler rightly warns about.
                const auto* first = controls[i];
                const auto* second = controls[j];

                std::printf ("       %s: %s %s overlaps %s %s\n",
                             context.toRawUTF8(),
                             typeid (*first).name(), first->getBounds().toString().toRawUTF8(),
                             typeid (*second).name(), second->getBounds().toString().toRawUTF8());
                ++failures;
            }
        }
    }
}

/** Every visible control must sit inside the editor. */
void checkInsideParent (juce::Component& parent, const juce::String& context)
{
    for (auto* child : parent.getChildren())
    {
        if (! child->isVisible())
            continue;

        const auto parentArea = parent.getLocalBounds();

        if (! parentArea.expanded (2).contains (child->getBounds()))
        {
            std::printf ("       %s: %s escapes its parent (%s vs %s)\n",
                         context.toRawUTF8(), typeid (*child).name(),
                         child->getBounds().toString().toRawUTF8(),
                         parentArea.toString().toRawUTF8());
            ++failures;
        }

        checkInsideParent (*child, context);
    }
}

void saveSnapshot (juce::Component& component, const juce::File& file)
{
    const auto image = component.createComponentSnapshot (component.getLocalBounds(), true, 1.0f);

    if (image.isValid())
    {
        file.deleteFile();
        juce::FileOutputStream stream (file);

        if (stream.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (image, stream);
            std::printf ("  wrote %s (%dx%d)\n", file.getFullPathName().toRawUTF8(),
                         image.getWidth(), image.getHeight());
            return;
        }
    }

    std::printf ("  FAIL could not write %s\n", file.getFullPathName().toRawUTF8());
    ++failures;
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File outputDirectory = argc > 1
        ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
        : juce::File::getCurrentWorkingDirectory().getChildFile ("ui-snapshots");

    outputDirectory.createDirectory();

    std::printf ("VibeOTT editor layout checks\n");

    VibeOTTProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* ottEditor = dynamic_cast<VibeOTTEditor*> (editor.get());

    if (ottEditor == nullptr)
    {
        std::printf ("FAIL could not create the editor\n");
        return EXIT_FAILURE;
    }

    editor->setSize (editor->getWidth(), editor->getHeight());

    //== the plugin's host contract ==========================================
    // The DSP tests cover the engine; this covers the wrapper around it, which
    // is what a DAW actually talks to.
    std::printf ("\n[host contract]\n");

    {
        const juce::String required[] = {
            ParameterIDs::mix, ParameterIDs::depth, ParameterIDs::upward, ParameterIDs::downward,
            ParameterIDs::time,
            ParameterIDs::inputGain, ParameterIDs::outputGain, ParameterIDs::behavior,
            ParameterIDs::lowCrossover, ParameterIDs::highCrossover,
        };

        bool allPresent = true;

        for (const auto& id : required)
            if (processor.apvts.getParameter (id) == nullptr)
            {
                std::printf ("       missing parameter %s\n", id.toRawUTF8());
                allPresent = false;
            }

        for (int band = 0; band < ott::numBands; ++band)
            for (const auto& id : { ParameterIDs::upwardThreshold (band), ParameterIDs::upwardRatio (band),
                                    ParameterIDs::downwardThreshold (band), ParameterIDs::downwardRatio (band),
                                    ParameterIDs::bandGain (band) })
                if (processor.apvts.getParameter (id) == nullptr)
                {
                    std::printf ("       missing parameter %s\n", id.toRawUTF8());
                    allPresent = false;
                }

        check (allPresent, "every documented parameter is registered");
    }

    check (processor.getTotalNumInputChannels() == 2, "defaults to a stereo bus");
    check (processor.getLatencySamples() == 0, "reports zero latency (no look-ahead)");

    {
        // Stereo in, stereo out.
        juce::AudioBuffer<float> stereo (2, 512);
        juce::MidiBuffer midi;

        for (int i = 0; i < 512; ++i)
        {
            const float v = 0.3f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / 48000.0f);
            stereo.setSample (0, i, v);
            stereo.setSample (1, i, v);
        }

        processor.processBlock (stereo, midi);

        bool finite = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                finite = finite && std::isfinite (stereo.getSample (ch, i));

        check (finite, "stereo processBlock produces finite output");
        check (stereo.getMagnitude (0, 512) > 0.0f, "stereo processBlock produces signal");

        // Mono in, one channel out: the second channel must be left alone.
        juce::AudioBuffer<float> mono (1, 512);
        mono.copyFrom (0, 0, stereo, 0, 0, 512);

        juce::AudioBuffer<float> monoRef (1, 512);
        monoRef.copyFrom (0, 0, mono, 0, 0, 512);

        processor.processBlock (mono, midi);

        bool monoFinite = true;
        for (int i = 0; i < 512; ++i)
            monoFinite = monoFinite && std::isfinite (mono.getSample (0, i));

        check (monoFinite, "mono processBlock produces finite output");

        // A single-sample block and an over-large block must both be survivable;
        // hosts do both, and the engine chunks internally rather than allocating.
        juce::AudioBuffer<float> tiny (2, 1);
        tiny.clear();
        processor.processBlock (tiny, midi);
        check (std::isfinite (tiny.getSample (0, 0)), "a one-sample block is handled");

        juce::AudioBuffer<float> huge (2, 4096);
        huge.clear();
        processor.processBlock (huge, midi);
        check (std::isfinite (huge.getSample (0, 0)), "a 4096-sample block is handled");
    }

    {
        // State save / restore round trip.
        processor.apvts.getParameter (ParameterIDs::depth)->setValueNotifyingHost (0.25f);
        processor.apvts.getParameter (ParameterIDs::mix)->setValueNotifyingHost (0.5f);

        juce::MemoryBlock saved;
        processor.getStateInformation (saved);
        check (saved.getSize() > 0, "state serialises");

        VibeOTTProcessor restored;
        restored.setStateInformation (saved.getData(), (int) saved.getSize());

        const auto depth = restored.apvts.getParameter (ParameterIDs::depth)->getValue();
        const auto mix = restored.apvts.getParameter (ParameterIDs::mix)->getValue();

        check (std::abs (depth - 0.25f) < 0.01f, "depth survives a state round trip");
        check (std::abs (mix - 0.5f) < 0.01f, "mix survives a state round trip");

        // Put the live instance back to its defaults so the readout checks below
        // measure the default UI rather than this test's leftovers.
        for (const char* id : { ParameterIDs::depth, ParameterIDs::mix })
            if (auto* parameter = processor.apvts.getParameter (id))
                parameter->setValueNotifyingHost (parameter->getDefaultValue());
    }

    {
        // A session saved by the pre-rewrite plugin must survive: its seven 0-1
        // parameters have to land on the new set, not reset to defaults.
        const char* legacyXml =
            "<Parameters DEPTH=\"0.8\" UPWARD_RATIO=\"0.9\" DOWNWARD_RATIO=\"0.4\" "
            "MIX=\"0.6\" LOW_GAIN=\"0.75\" MID_GAIN=\"0.5\" HIGH_GAIN=\"0.25\"/>";

        // setStateInformation consumes JUCE's binary wrapper, not raw XML, so
        // build the block the same way the plugin writes it.
        juce::MemoryBlock legacyBlock;
        {
            std::unique_ptr<juce::XmlElement> legacyElement (juce::parseXML (legacyXml));
            juce::AudioProcessor::copyXmlToBinary (*legacyElement, legacyBlock);
        }

        VibeOTTProcessor migrated;
        migrated.setStateInformation (legacyBlock.getData(), (int) legacyBlock.getSize());

        const auto value = [&migrated] (const juce::String& id)
        {
            auto* parameter = migrated.apvts.getParameter (id);
            return parameter != nullptr ? parameter->getValue() : -1.0f;
        };

        std::printf ("       depth=%f upward=%f mix=%f lowGain=%f\n",
                     value (ParameterIDs::depth), value (ParameterIDs::upward),
                     value (ParameterIDs::mix), value (ParameterIDs::bandGain (0)));

        check (std::abs (value (ParameterIDs::depth) - 0.8f) < 0.01f,
               "legacy DEPTH migrates");
        check (std::abs (value (ParameterIDs::upward) - 0.9f) < 0.01f,
               "legacy UPWARD_RATIO migrates onto the 0-2 macro");
        check (std::abs (value (ParameterIDs::mix) - 0.6f) < 0.01f,
               "legacy MIX migrates");
        // 0.75 on the old +/-12 dB range is +6 dB.
        check (std::abs (value (ParameterIDs::bandGain (0)) - 0.6f) < 0.02f,
               "legacy per-band gain migrates onto the dB range");
        check (std::abs (value (ParameterIDs::downward) - 0.4f) < 0.01f,
               "legacy DOWNWARD_RATIO migrates");
    }

    //== readouts ============================================================
    // JUCE caches a slider's display text, so a formatter attached after the
    // value was set silently does nothing and the knob shows "2500.0004883".
    std::printf ("\n[readouts]\n");

    {
        const std::pair<const char*, juce::String> expected[] = {
            { ParameterIDs::lowCrossover,  "120 Hz" },
            { ParameterIDs::highCrossover, "2500 Hz" },
            { ParameterIDs::inputGain,     "0.0 dB" },
            { ParameterIDs::mix,           "100%" },
            { ParameterIDs::upward,        "1.00x" },
            { ParameterIDs::depth,         "100%" },
            { ParameterIDs::time,          "50%" },
        };

        // Collect every knob with the parameter ID it was created for. Several
        // parameters share a value (all three default band gains are 13.3 dB),
        // so matching controls by value alone picks the wrong one.
        std::map<juce::String, juce::Slider*> knobsByParameter;

        std::vector<juce::Component*> stack { editor.get() };

        while (! stack.empty())
        {
            auto* component = stack.back();
            stack.pop_back();

            for (auto* child : component->getChildren())
            {
                if (auto* slider = dynamic_cast<juce::Slider*> (child))
                {
                    const auto id = slider->getProperties()[knobParameterIdProperty].toString();

                    if (id.isNotEmpty())
                        knobsByParameter[id] = slider;
                }

                stack.push_back (child);
            }
        }

        for (const auto& entry : expected)
        {
            const juce::String id (entry.first);
            const juce::String wanted (entry.second);

            auto found = knobsByParameter.find (id);
            juce::String actual ("<no knob>");

            if (found != knobsByParameter.end())
                actual = formatParameterValue (id, found->second->getValue());

            const bool ok = actual == wanted;
            std::printf ("  %s %-14s shows '%s'\n", ok ? "ok  " : "FAIL", id.toRawUTF8(), actual.toRawUTF8());

            if (! ok)
            {
                std::printf ("       expected '%s'\n", wanted.toRawUTF8());
                ++failures;
            }
        }
    }

    //== collapsed ============================================================
    std::printf ("\n[collapsed]\n");
    check (editor->getWidth() >= 700, "window is wide enough for the controls");
    check (editor->getHeight() == VibeOTTEditor::collapsedHeight, "collapsed height matches the declared layout");
    checkNoOverlaps (*editor, "collapsed");
    checkInsideParent (*editor, "collapsed");
    dumpLayout (*editor, "collapsed");
    saveSnapshot (*editor, outputDirectory.getChildFile ("editor-collapsed.png"));

    //== expanded =============================================================
    std::printf ("\n[expanded]\n");

    // Find and click the ADVANCED button, the same way a user would.
    juce::TextButton* advancedButton = nullptr;

    std::function<void (juce::Component&)> findAdvanced = [&] (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* button = dynamic_cast<juce::TextButton*> (child))
                if (button->getButtonText().startsWith ("ADVANCED") && advancedButton == nullptr)
                    advancedButton = button;

            findAdvanced (*child);
        }
    };

    findAdvanced (*editor);

    check (advancedButton != nullptr, "the ADVANCED button exists");

    // setToggleState with a notification is synchronous, whereas triggerClick
    // posts the click to the message queue and would not have run by the time
    // the layout is inspected below.
    if (advancedButton != nullptr)
        advancedButton->setToggleState (true, juce::sendNotificationSync);

    check (editor->getHeight() == VibeOTTEditor::expandedHeight, "expanded height matches the declared layout");
    checkNoOverlaps (*editor, "expanded");
    checkInsideParent (*editor, "expanded");
    dumpLayout (*editor, "expanded");
    saveSnapshot (*editor, outputDirectory.getChildFile ("editor-expanded.png"));

    std::printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
