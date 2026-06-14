#include "PluginProcessor.h"
#include "PluginEditor.h"

VibeOTTProcessor::VibeOTTProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    depthParam = apvts.getRawParameterValue("DEPTH");
    mixParam = apvts.getRawParameterValue("MIX");
    crossoverLowMidParam = apvts.getRawParameterValue("CROSSOVER_LOW_MID");
    crossoverMidHighParam = apvts.getRawParameterValue("CROSSOVER_MID_HIGH");

    upThresholdParam = apvts.getRawParameterValue("UP_THRESHOLD");
    upRatioParam = apvts.getRawParameterValue("UP_RATIO");
    upAttackParam = apvts.getRawParameterValue("UP_ATTACK");
    upReleaseParam = apvts.getRawParameterValue("UP_RELEASE");

    downThresholdParam = apvts.getRawParameterValue("DOWN_THRESHOLD");
    downRatioParam = apvts.getRawParameterValue("DOWN_RATIO");
    downAttackParam = apvts.getRawParameterValue("DOWN_ATTACK");
    downReleaseParam = apvts.getRawParameterValue("DOWN_RELEASE");

    lowGainParam = apvts.getRawParameterValue("LOW_GAIN");
    midGainParam = apvts.getRawParameterValue("MID_GAIN");
    highGainParam = apvts.getRawParameterValue("HIGH_GAIN");
}

VibeOTTProcessor::~VibeOTTProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout VibeOTTProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DEPTH", "Depth", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MIX", "Mix", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "CROSSOVER_LOW_MID", "Crossover Low/Mid",
        juce::NormalisableRange<float>(20.0f, 2000.0f, 1.0f, 0.4f), 880.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "CROSSOVER_MID_HIGH", "Crossover Mid/High",
        juce::NormalisableRange<float>(200.0f, 16000.0f, 1.0f, 0.4f), 4400.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "UP_THRESHOLD", "Upward Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f, 2.0f), -36.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "UP_RATIO", "Upward Ratio",
        juce::NormalisableRange<float>(1.0f, 10.0f, 0.1f, 0.5f), 3.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "UP_ATTACK", "Upward Attack",
        juce::NormalisableRange<float>(0.1f, 200.0f, 0.1f, 0.5f), 10.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "UP_RELEASE", "Upward Release",
        juce::NormalisableRange<float>(0.1f, 500.0f, 0.1f, 0.5f), 50.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DOWN_THRESHOLD", "Downward Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f, 2.0f), -18.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DOWN_RATIO", "Downward Ratio",
        juce::NormalisableRange<float>(1.0f, 10.0f, 0.1f, 0.5f), 3.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DOWN_ATTACK", "Downward Attack",
        juce::NormalisableRange<float>(0.1f, 200.0f, 0.1f, 0.5f), 10.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DOWN_RELEASE", "Downward Release",
        juce::NormalisableRange<float>(0.1f, 500.0f, 0.1f, 0.5f), 50.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "LOW_GAIN", "Low Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MID_GAIN", "Mid Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "HIGH_GAIN", "High Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

    return { params.begin(), params.end() };
}

void VibeOTTProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(getTotalNumInputChannels());

    compressor.prepare(spec);
    dryBuffer.setSize(getTotalNumInputChannels(), samplesPerBlock);
}

void VibeOTTProcessor::releaseResources()
{
    compressor.reset();
    dryBuffer.setSize(0, 0);
}

bool VibeOTTProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void VibeOTTProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    dryBuffer.makeCopyOf(buffer);

    compressor.setDepth(depthParam->load());
    compressor.setCrossoverLowMid(crossoverLowMidParam->load());
    compressor.setCrossoverMidHigh(crossoverMidHighParam->load());

    for (int b = 0; b < MultibandCompressor::numBands; ++b)
    {
        auto& band = compressor.getBand(b);
        band.upwardThreshold = upThresholdParam->load();
        band.upwardRatio = upRatioParam->load();
        band.upwardAttack = upAttackParam->load();
        band.upwardRelease = upReleaseParam->load();
        band.downwardThreshold = downThresholdParam->load();
        band.downwardRatio = downRatioParam->load();
        band.downwardAttack = downAttackParam->load();
        band.downwardRelease = downReleaseParam->load();
    }

    compressor.getBand(0).gainDb = lowGainParam->load();
    compressor.getBand(1).gainDb = midGainParam->load();
    compressor.getBand(2).gainDb = highGainParam->load();

    compressor.process(buffer);

    const float mix = mixParam->load();
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* wet = buffer.getWritePointer(ch);
        const auto* dry = dryBuffer.getReadPointer(ch);
        for (int s = 0; s < numSamples; ++s)
            wet[s] = dry[s] * (1.0f - mix) + wet[s] * mix;
    }
}

juce::AudioProcessorEditor* VibeOTTProcessor::createEditor()
{
    return new VibeOTTEditor(*this);
}

void VibeOTTProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void VibeOTTProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VibeOTTProcessor();
}
