#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

//==============================================================================
namespace OttTheme
{
    const juce::Colour background   { 0xff121316 };
    const juce::Colour panel        { 0xff1c1e23 };
    const juce::Colour panelRaised  { 0xff24272e };
    const juce::Colour outline      { 0xff33373f };
    const juce::Colour text         { 0xffb9bfca };
    const juce::Colour textDim      { 0xff6d737e };
    const juce::Colour low          { 0xff4a9bff };
    const juce::Colour mid          { 0xff9a6cff };
    const juce::Colour high         { 0xffffa832 };
    const juce::Colour upward       { 0xffff6f2a };
    const juce::Colour downward     { 0xff2ecf9a };
    const juce::Colour neutral      { 0xffd6dae2 };
    const juce::Colour overload     { 0xffff4d4d };

    /** Band colours in band order, so the meter and its controls always agree. */
    inline juce::Colour bandColour (int band)
    {
        switch (band)
        {
            case 0:  return low;
            case 1:  return mid;
            default: return high;
        }
    }
}

//==============================================================================
/** Renders a knob's numeric readout for the parameter it is attached to.

    JUCE's default prints the raw float, which turns a crossover into
    "2500.0004883". These are the units a user actually reads. Exposed (rather
    than file-local) so the editor snapshot tool can assert on the strings the
    plugin really shows.
*/
juce::String formatParameterValue (const juce::String& parameterId, double value);

/** Slider property carrying the parameter ID a knob was created for. Lets
    tooling — and anyone debugging — tell which control is which without
    guessing from the attached value. */
inline constexpr const char* knobParameterIdProperty = "vibeottParameterId";

//==============================================================================
/** Rotary knob drawing shared by every control in the plugin. */
class OttLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OttLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    /** Every knob in this plugin is a coloured arc, so the colour travels on the
        slider as a property rather than being looked up per parameter. */
    static void setKnobColour (juce::Slider& slider, juce::Colour colour)
    {
        slider.getProperties().set ("knobColour", (juce::int64) colour.getARGB());
    }
};

//==============================================================================
/** One band's level and gain-reduction readout.

    The level bar is dB-scaled over 60 dB and clipped at the top so that +0 dBFS
    is visible rather than off-screen. Gain reduction is drawn to the right of
    the bar, above and below the 0 dB line, so upward lift and downward
    compression are both readable without a second scale.
*/
class BandMeter : public juce::Component
{
public:
    explicit BandMeter (int bandIndex);

    void paint (juce::Graphics&) override;

    /** Called from the editor's timer with a fresh snapshot. */
    void setLevels (const ott::BandLevels& levels);

private:
    static constexpr float minimumDb = -60.0f;
    static constexpr float maximumGainReductionDb = 24.0f;

    static float dbToProportion (float db) noexcept
    {
        return juce::jlimit (0.0f, 1.0f, (db - minimumDb) / (0.0f - minimumDb));
    }

    int band;

    // Smoothed display values: the level falls slowly and snaps up instantly,
    // which is the only way a 30 Hz meter reads as anything but a flicker.
    float levelDb = minimumDb;
    float reductionDb = 0.0f;
    float peakDb = minimumDb;
    int   peakHoldCounter = 0;
};

//==============================================================================
/** The big central visual: a ring that fills to show the compression depth,
    with the wet/dry balance drawn as a second, inner arc. Draggable, so it
    doubles as the Depth control.
*/
class DepthDisplay : public juce::Component
{
public:
    explicit DepthDisplay (juce::Slider& depthSliderToDrive);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Reads the attached slider, so the display never disagrees with the
        parameter it is showing. */
    void refresh();

private:
    void setDepthFromPosition (juce::Point<int> position);

    juce::Slider& depthSlider;
    float depth = ott::defaultDepth;
};

//==============================================================================
class VibeOTTEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit VibeOTTEditor (VibeOTTProcessor&);
    ~VibeOTTEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

public:
    // The layout geometry is public so the snapshot tool can assert against it
    // rather than repeating the numbers.
    static constexpr int windowWidth     = 780;
    static constexpr int collapsedHeight = 348;
    static constexpr int expandedHeight  = 596;

private:
    void timerCallback() override;
    void setAdvancedVisible (bool shouldBeVisible);

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;    // caption under the dial
        juce::Label readout;  // numeric value, formatted from the parameter
        juce::String parameterId;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

        /** Lays the knob out in a column, splitting the area into dial, numeric
            readout and caption. */
        void setBounds (juce::Rectangle<int> area);
    };

    /** Configures a knob and attaches it to a parameter.

        The knob is passed in rather than returned: a static pool would make two
        editor instances silently share state, and the members have to be real
        children of whichever component owns them. */
    void addKnob (Knob& knob, juce::Component& parent,
                  const juce::String& parameterId, const juce::String& labelText,
                  juce::Colour colour);

    VibeOTTProcessor& processorRef;
    OttLookAndFeel lookAndFeel;

    //== layout ===============================================================
    static constexpr int rowLabelHeight = 12;

    //== main controls ========================================================
    Knob depthKnob, upwardKnob, downwardKnob, mixKnob, timeKnob;
    Knob inputKnob, outputKnob, behaviorKnob;
    Knob lowCrossoverKnob, highCrossoverKnob;

    std::unique_ptr<DepthDisplay> depthDisplay;
    std::unique_ptr<BandMeter> meters[ott::numBands];

    //== advanced (per band detail) ===========================================
    juce::TextButton advancedButton { "ADVANCED" };
    std::unique_ptr<juce::Component> advancedPanel;
    Knob upwardThresholdKnob[ott::numBands];
    Knob upwardRatioKnob[ott::numBands];
    Knob downwardThresholdKnob[ott::numBands];
    Knob downwardRatioKnob[ott::numBands];
    Knob bandGainKnob[ott::numBands];

    bool advancedVisible = false;


    // Labels that need to follow a value, so they are looked up once per paint.
    std::unique_ptr<juce::Label> depthCaption;
    std::unique_ptr<juce::Label> mixCaption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VibeOTTEditor)
};
