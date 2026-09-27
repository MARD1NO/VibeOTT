#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::NormalisableRange<float> linearRange (float minimum, float maximum, float interval)
    {
        return { minimum, maximum, interval };
    }

    /** Frequency controls are logarithmic, which is the only way a 30 Hz to
        18 kHz control is usable with a mouse. */
    juce::NormalisableRange<float> frequencyRange (float minimum, float maximum)
    {
        juce::NormalisableRange<float> range (minimum, maximum);

        // A skew chosen so the mid-point of the control lands on the geometric
        // mean of the range (roughly 735 Hz for 30 Hz - 18 kHz).
        range.setSkewForCentre (std::sqrt (minimum * maximum));
        return range;
    }

    void addFloat (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& parameters,
                   const juce::String& id, const juce::String& name,
                   juce::NormalisableRange<float> range, float defaultValue,
                   const juce::String& suffix = {})
    {
        parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, defaultValue,
            juce::AudioParameterFloatAttributes().withLabel (suffix)));
    }
}

//==============================================================================
VibeOTTProcessor::VibeOTTProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    cacheParameterPointers();

    // Start the engine on the documented defaults so the first block after
    // construction already sounds right.
    applyParameters();
}

VibeOTTProcessor::~VibeOTTProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout VibeOTTProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;

    //== the OTT macros ========================================================
    addFloat (parameters, ParameterIDs::mix, "Mix", linearRange (0.0f, 1.0f, 0.001f), ott::defaultMix, "%");
    addFloat (parameters, ParameterIDs::depth, "Depth", linearRange (0.0f, 1.0f, 0.001f), ott::defaultDepth, "%");
    addFloat (parameters, ParameterIDs::upward, "Upward", linearRange (0.0f, 2.0f, 0.001f), 1.0f, "x");
    addFloat (parameters, ParameterIDs::downward, "Downward", linearRange (0.0f, 2.0f, 0.001f), 1.0f, "x");

    //== timing ================================================================
    addFloat (parameters, ParameterIDs::time, "Time", linearRange (0.0f, 1.0f, 0.001f), ott::defaultTime, "%");

    //== gain staging ==========================================================
    addFloat (parameters, ParameterIDs::inputGain, "In Gain",
              linearRange (ott::trimMinDb, ott::trimMaxDb, 0.1f), 0.0f, "dB");
    addFloat (parameters, ParameterIDs::outputGain, "Out Gain",
              linearRange (ott::trimMinDb, ott::trimMaxDb, 0.1f), 0.0f, "dB");

    addFloat (parameters, ParameterIDs::behavior, "Behavior",
              linearRange (ott::behaviorMinDb, ott::behaviorMaxDb, 0.1f), 0.0f, "dB");

    //== crossovers ============================================================
    addFloat (parameters, ParameterIDs::lowCrossover, "Low / Mid",
              frequencyRange (ott::minCrossoverHz, ott::maxCrossoverHz), ott::defaultLowCrossoverHz, "Hz");
    addFloat (parameters, ParameterIDs::highCrossover, "Mid / High",
              frequencyRange (ott::minCrossoverHz, ott::maxCrossoverHz), ott::defaultHighCrossoverHz, "Hz");

    //== per-band detail =======================================================
    for (int band = 0; band < ott::numBands; ++band)
    {
        const juce::String prefix (ParameterIDs::bandNames[band]);
        const auto& defaults = ott::bandDefaults[band];

        addFloat (parameters, ParameterIDs::upwardThreshold (band), prefix + " Up Thr",
                  linearRange (-80.0f, 0.0f, 0.1f), defaults.upwardThresholdDb, "dB");
        addFloat (parameters, ParameterIDs::upwardRatio (band), prefix + " Up Ratio",
                  linearRange (-1.0f, 1.0f, 0.001f), defaults.upwardRatio);
        addFloat (parameters, ParameterIDs::downwardThreshold (band), prefix + " Dn Thr",
                  linearRange (-80.0f, 0.0f, 0.1f), defaults.downwardThresholdDb, "dB");
        addFloat (parameters, ParameterIDs::downwardRatio (band), prefix + " Dn Ratio",
                  linearRange (0.0f, 1.0f, 0.001f), defaults.downwardRatio);
        addFloat (parameters, ParameterIDs::bandGain (band), prefix + " Gain",
                  linearRange (ott::trimMinDb, ott::trimMaxDb, 0.1f), defaults.gainDb, "dB");
    }

    return { parameters.begin(), parameters.end() };
}

void VibeOTTProcessor::cacheParameterPointers()
{
    mixParam           = apvts.getRawParameterValue (ParameterIDs::mix);
    depthParam         = apvts.getRawParameterValue (ParameterIDs::depth);
    upwardParam        = apvts.getRawParameterValue (ParameterIDs::upward);
    downwardParam      = apvts.getRawParameterValue (ParameterIDs::downward);
    inputGainParam     = apvts.getRawParameterValue (ParameterIDs::inputGain);
    outputGainParam    = apvts.getRawParameterValue (ParameterIDs::outputGain);
    behaviorParam      = apvts.getRawParameterValue (ParameterIDs::behavior);
    timeParam          = apvts.getRawParameterValue (ParameterIDs::time);
    lowCrossoverParam  = apvts.getRawParameterValue (ParameterIDs::lowCrossover);
    highCrossoverParam = apvts.getRawParameterValue (ParameterIDs::highCrossover);

    for (int band = 0; band < ott::numBands; ++band)
    {
        upwardThresholdParam[band]   = apvts.getRawParameterValue (ParameterIDs::upwardThreshold (band));
        upwardRatioParam[band]       = apvts.getRawParameterValue (ParameterIDs::upwardRatio (band));
        downwardThresholdParam[band] = apvts.getRawParameterValue (ParameterIDs::downwardThreshold (band));
        downwardRatioParam[band]     = apvts.getRawParameterValue (ParameterIDs::downwardRatio (band));
        bandGainParam[band]          = apvts.getRawParameterValue (ParameterIDs::bandGain (band));
    }
}

ott::Parameters VibeOTTProcessor::readParameters() const
{
    ott::Parameters p;

    p.mix        = mixParam->load();
    p.depth      = depthParam->load();
    p.upward     = upwardParam->load();
    p.downward   = downwardParam->load();
    // One control, both envelope times. The engine keeps them separate because
    // a future version may expose them independently again.
    p.attack     = timeParam->load();
    p.release     = timeParam->load();
    p.inGainDb   = inputGainParam->load();
    p.outGainDb  = outputGainParam->load();
    p.behaviorDb = behaviorParam->load();

    // The engine keeps the low corner below the high one, and the collapse
    // range is deliberately unreachable from here so a sweep never crosses a
    // band-topology change.
    p.lowCrossoverHz  = juce::jlimit (ott::minCrossoverHz, ott::maxCrossoverHz, lowCrossoverParam->load());
    p.highCrossoverHz = juce::jlimit (ott::minCrossoverHz, ott::maxCrossoverHz, highCrossoverParam->load());

    for (int band = 0; band < ott::numBands; ++band)
    {
        const auto index = (std::size_t) band;
        p.upwardThresholdDb[index]   = upwardThresholdParam[band]->load();
        p.upwardRatio[index]         = upwardRatioParam[band]->load();
        p.downwardThresholdDb[index] = downwardThresholdParam[band]->load();
        p.downwardRatio[index]       = downwardRatioParam[band]->load();
        p.bandGainDb[index]          = bandGainParam[band]->load();
    }

    return p;
}

void VibeOTTProcessor::applyParameters()
{
    dsp.setParameters (readParameters());
}

//==============================================================================
void VibeOTTProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    applyParameters();
    dsp.reset();

    // The engine is feed-forward with no look-ahead, so it adds no latency.
    setLatencySamples (dsp.getLatencySamples());
}

void VibeOTTProcessor::releaseResources()
{
    dsp.reset();
}

bool VibeOTTProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return output == layouts.getMainInputChannelSet();
}

void VibeOTTProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0)
        return;

    // Read every parameter once, before touching the audio.
    applyParameters();

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);

    float* channels[2] = { buffer.getWritePointer (0),
                           numChannels > 1 ? buffer.getWritePointer (1) : nullptr };

    dsp.process (channels, numChannels, numSamples);

    {
        const juce::SpinLock::ScopedLockType lock (meteringLock);
        meteringSnapshot = dsp.getBandLevels();
    }
}

ott::BandLevels VibeOTTProcessor::getBandLevels() const
{
    const juce::SpinLock::ScopedLockType lock (meteringLock);
    return meteringSnapshot;
}

//==============================================================================
juce::AudioProcessorEditor* VibeOTTProcessor::createEditor()
{
    return new VibeOTTEditor (*this);
}

void VibeOTTProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void VibeOTTProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    const bool wasLegacy = state.hasProperty ("DEPTH");

    apvts.replaceState (state);

    // Sessions saved by the pre-rewrite plugin used a completely different,
    // 0-1 parameter set. The migration runs AFTER replaceState so it cannot be
    // overwritten by the old session's parameters; the APVTS defaults fill in
    // everything the old set did not have.
    if (wasLegacy)
        migrateLegacyState (state);

    applyParameters();
}

void VibeOTTProcessor::migrateLegacyState (const juce::ValueTree& state)
{
    const auto legacy = [&state] (const char* id, float fallback) -> float
    {
        const juce::var value = state.getProperty (juce::Identifier (id));
        return value.isVoid() ? fallback : (float) value;
    };

    // Writing to the APVTS parameters directly is deliberate. Setting properties
    // on the tree before replaceState does not work: replaceState syncs a
    // parameter only when the tree's value differs from the parameter's current
    // value, so a migrated value that happens to equal the default is silently
    // dropped.
    const auto apply = [this] (const juce::String& id, float plainValue)
    {
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
                                                            parameter->convertTo0to1 (plainValue)));
    };

    // Old ranges were 0-1 for everything. Depth and Mix carry over directly; the
    // old Up/Down ratios were 0-1 over a 2x range, so double them onto the new
    // 0-2 macros. The old per-band gains were 0-1 over a +/-12 dB range.
    apply (ParameterIDs::depth,    legacy ("DEPTH", ott::defaultDepth));
    apply (ParameterIDs::upward,   legacy ("UPWARD_RATIO", 0.5f) * 2.0f);
    apply (ParameterIDs::downward, legacy ("DOWNWARD_RATIO", 0.5f) * 2.0f);
    apply (ParameterIDs::mix,      legacy ("MIX", ott::defaultMix));

    // Timing used to be an attack/release pair; fold whichever is present into
    // the single Time macro so the setting survives.
    {
        const juce::var legacyAttack = state.getProperty (juce::Identifier ("ATTACK"));
        const juce::var legacyRelease = state.getProperty (juce::Identifier ("RELEASE"));

        if (! legacyAttack.isVoid() || ! legacyRelease.isVoid())
        {
            const float attack = legacyAttack.isVoid() ? ott::defaultTime : (float) legacyAttack;
            const float release = legacyRelease.isVoid() ? attack : (float) legacyRelease;

            apply (ParameterIDs::time, 0.5f * (attack + release));
        }
    }

    const char* legacyGains[ott::numBands] = { "LOW_GAIN", "MID_GAIN", "HIGH_GAIN" };

    for (int band = 0; band < ott::numBands; ++band)
        apply (ParameterIDs::bandGain (band), legacy (legacyGains[band], 0.5f) * 24.0f - 12.0f);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VibeOTTProcessor();
}
