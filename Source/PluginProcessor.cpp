#include "PluginProcessor.h"
#include "PluginEditor.h"

VibeOTTProcessor::VibeOTTProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    depthParam = apvts.getRawParameterValue("DEPTH");
    upwardRatioParam = apvts.getRawParameterValue("UPWARD_RATIO");
    downwardRatioParam = apvts.getRawParameterValue("DOWNWARD_RATIO");
    lowGainParam = apvts.getRawParameterValue("LOW_GAIN");
    midGainParam = apvts.getRawParameterValue("MID_GAIN");
    highGainParam = apvts.getRawParameterValue("HIGH_GAIN");

    lowUpThreshParam = apvts.getRawParameterValue("LOW_UP_THRESH");
    midUpThreshParam = apvts.getRawParameterValue("MID_UP_THRESH");
    highUpThreshParam = apvts.getRawParameterValue("HIGH_UP_THRESH");
    lowDownThreshParam = apvts.getRawParameterValue("LOW_DOWN_THRESH");
    midDownThreshParam = apvts.getRawParameterValue("MID_DOWN_THRESH");
    highDownThreshParam = apvts.getRawParameterValue("HIGH_DOWN_THRESH");
}

VibeOTTProcessor::~VibeOTTProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout VibeOTTProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DEPTH", "Depth", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "UPWARD_RATIO", "Upward Ratio",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.75f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DOWNWARD_RATIO", "Downward Ratio",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.75f));

    auto threshRange = juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f, 2.0f);

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "LOW_UP_THRESH", "Low Up Threshold", threshRange, -36.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MID_UP_THRESH", "Mid Up Threshold", threshRange, -36.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "HIGH_UP_THRESH", "High Up Threshold", threshRange, -36.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "LOW_DOWN_THRESH", "Low Down Threshold", threshRange, -18.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MID_DOWN_THRESH", "Mid Down Threshold", threshRange, -18.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "HIGH_DOWN_THRESH", "High Down Threshold", threshRange, -18.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "LOW_GAIN", "Low Gain", juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MID_GAIN", "Mid Gain", juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "HIGH_GAIN", "High Gain", juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

    return { params.begin(), params.end() };
}

void VibeOTTProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    compressor.prepare(sampleRate, samplesPerBlock);
}

void VibeOTTProcessor::releaseResources()
{
    compressor.reset();
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

    compressor.setDepth(depthParam->load());
    compressor.setUpwardRatio(upwardRatioParam->load());
    compressor.setDownwardRatio(downwardRatioParam->load());
    compressor.setBandGain(0, lowGainParam->load());
    compressor.setBandGain(1, midGainParam->load());
    compressor.setBandGain(2, highGainParam->load());

    compressor.setUpwardThreshold(0, lowUpThreshParam->load());
    compressor.setUpwardThreshold(1, midUpThreshParam->load());
    compressor.setUpwardThreshold(2, highUpThreshParam->load());
    compressor.setDownwardThreshold(0, lowDownThreshParam->load());
    compressor.setDownwardThreshold(1, midDownThreshParam->load());
    compressor.setDownwardThreshold(2, highDownThreshParam->load());

    compressor.process(buffer);
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
