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
    outputGainParam = apvts.getRawParameterValue("OUTPUT_GAIN");

    lowThreshParam = apvts.getRawParameterValue("LOW_THRESH");
    midThreshParam = apvts.getRawParameterValue("MID_THRESH");
    highThreshParam = apvts.getRawParameterValue("HIGH_THRESH");
    lowRangeParam = apvts.getRawParameterValue("LOW_RANGE");
    midRangeParam = apvts.getRawParameterValue("MID_RANGE");
    highRangeParam = apvts.getRawParameterValue("HIGH_RANGE");
}

VibeOTTProcessor::~VibeOTTProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout VibeOTTProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DEPTH", "Depth", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "UPWARD_RATIO", "Upward Ratio",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.6f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DOWNWARD_RATIO", "Downward Ratio",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.7f));

    auto threshRange = juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f, 2.0f);
    auto rangeParam = juce::NormalisableRange<float>(0.0f, 48.0f, 0.1f);

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "LOW_THRESH", "Low Threshold", threshRange, -36.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MID_THRESH", "Mid Threshold", threshRange, -36.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "HIGH_THRESH", "High Threshold", threshRange, -36.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "LOW_RANGE", "Low Range", rangeParam, 18.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MID_RANGE", "Mid Range", rangeParam, 18.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "HIGH_RANGE", "High Range", rangeParam, 18.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "LOW_GAIN", "Low Gain", juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MID_GAIN", "Mid Gain", juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "HIGH_GAIN", "High Gain", juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "OUTPUT_GAIN", "Output Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));

    return { params.begin(), params.end() };
}

void VibeOTTProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    compressor.prepare(sampleRate, samplesPerBlock);
    setLatencySamples(compressor.getLatencySamples());
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
    compressor.setOutputGain(outputGainParam->load());

    compressor.setUpwardThreshold(0, lowThreshParam->load());
    compressor.setUpwardThreshold(1, midThreshParam->load());
    compressor.setUpwardThreshold(2, highThreshParam->load());
    compressor.setDownwardThreshold(0, lowThreshParam->load());
    compressor.setDownwardThreshold(1, midThreshParam->load());
    compressor.setDownwardThreshold(2, highThreshParam->load());
    compressor.setThresholdRange(0, lowRangeParam->load());
    compressor.setThresholdRange(1, midRangeParam->load());
    compressor.setThresholdRange(2, highRangeParam->load());

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
