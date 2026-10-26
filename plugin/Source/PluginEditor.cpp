#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <memory>

class PianoHorizontalFader : public gin::HorizontalFader
{
public:
    PianoHorizontalFader (gin::Parameter* p, bool c)
        : gin::HorizontalFader (p, c)
    {
    }

    void resized() override
    {
        juce::Rectangle<int> r = getLocalBounds();

        name.setBounds (r.removeFromLeft (100));
        value.setBounds (r.removeFromRight (40));
        fader.setBounds (r.reduced (2));
    }
};

//==============================================================================
PlaybackTimeDisplay::PlaybackTimeDisplay (PianoAudioProcessor& p)
    : proc (p)
{
    // Laid out like the faders' name and value
    name.setText ("Time", juce::dontSendNotification);
    name.setJustificationType (juce::Justification::centredRight);
    value.setJustificationType (juce::Justification::centredLeft);
    value.setText (format (-1.0, false), juce::dontSendNotification);

    addAndMakeVisible (name);
    addAndMakeVisible (value);

    startTimerHz (30);
}

void PlaybackTimeDisplay::resized()
{
    auto r = getLocalBounds();

    name.setBounds (r.removeFromLeft (100));
    value.setBounds (r.reduced (2, 0));
}

juce::String PlaybackTimeDisplay::format (double seconds, bool playing)
{
    if (seconds < 0.0)
        return "No transport";

    auto ms = juce::int64 (seconds * 1000.0 + 0.5);
    auto text = juce::String (ms / 60000) + ":"
              + juce::String ((ms / 1000) % 60).paddedLeft ('0', 2) + "."
              + juce::String (ms % 1000).paddedLeft ('0', 3);

    return playing ? text : text + " stopped";
}

void PlaybackTimeDisplay::timerCallback()
{
    auto text = format (proc.playheadSeconds.load(), proc.playheadPlaying.load());

    if (text != value.getText())
        value.setText (text, juce::dontSendNotification);
}

//==============================================================================
PianoAudioProcessorEditor::PianoAudioProcessorEditor (PianoAudioProcessor& p_)
    : ProcessorEditor (p_), proc (p_)
{
    setName ("main");
    
    keyboard.setName ("keys");
    keyboard.setOpaque (false);
    keyboard.setMidiChannel (1);
    keyboard.setKeyWidth (10);
    keyboard.setAvailableRange (PIANO_MIN_NOTE, PIANO_MAX_NOTE);
    keyboard.setScrollButtonsVisible (false);
    addAndMakeVisible (keyboard);
    addAndMakeVisible (playbackTime);

    for (auto pp : proc.getPluginParameters())
    {
        // These faders can't do anything: the engine derives tension from
        // pitch, length and density, the bridge model only uses impedances,
        // and the damper doesn't change the longitudinal losses. They stay
        // parameters so old sessions load.
        auto uid = pp->getUid();
        if (uid == "StringTension"
             || uid == "BridgeMass" || uid == "BridgeSpring"
             || uid == "LongitudinalGammaDamped" || uid == "LongitudinalGammaQuadDamped")
            continue;

        auto pc = std::make_unique<PianoHorizontalFader> (pp, false);

        addAndMakeVisible (pc.get());
        controls.add (pc.release());
    }

    setGridSize (19, 4);

    // Everything is laid out at the base size and scaled as a whole, so the
    // window keeps the base aspect ratio while it is resized.
    const auto base = getBaseBounds();
    resizeLimits.setFixedAspectRatio (base.getWidth() / double (base.getHeight()));
    makeResizable (base.getWidth() * 3 / 4, base.getHeight() * 3 / 4,
                   base.getWidth() * 3,     base.getHeight() * 3);
    setConstrainer (&resizeLimits);
    setResizable (true, false);
}

PianoAudioProcessorEditor::~PianoAudioProcessorEditor()
{
}

//==============================================================================
juce::Rectangle<int> PianoAudioProcessorEditor::getBaseBounds() const
{
    return { cols * cx + inset * 2 + extraWidthPx,
             rows * cy + inset * 2 + headerHeight + extraHeightPx };
}

void PianoAudioProcessorEditor::applyScale()
{
    const auto base = getBaseBounds();
    const auto scale = getWidth() / float (base.getWidth());
    const auto t = juce::AffineTransform::scale (scale);

    // ProcessorEditor::resized() placed these at the window size, put them
    // back at the base size before scaling
    if (titleBar != nullptr)
    {
        titleBar->setBounds (base.reduced (1).removeFromTop (headerHeight - 1));
        titleBar->setTransform (t);
    }

    if (midiLearnOverlay != nullptr)
    {
        midiLearnOverlay->setBounds (base);
        midiLearnOverlay->setTransform (t);
    }

    patchBrowser.setTransform (t);
    keyboard.setTransform (t);
    playbackTime.setTransform (t);

    for (auto c : controls)
        c->setTransform (t);
}

void PianoAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Same as ProcessorEditor::paint(), with the header lines scaled
    juce::ColourGradient grad (findColour (gin::PluginLookAndFeel::matte1ColourId), 0, 0,
                               findColour (gin::PluginLookAndFeel::matte2ColourId), 0, float (getHeight()), false);

    g.setGradientFill (grad);
    g.fillAll();

    const auto header = juce::roundToInt (headerHeight * getWidth() / float (getBaseBounds().getWidth()));

    g.setColour (findColour (gin::PluginLookAndFeel::blackColourId));
    g.fillRect (0, 0, getWidth(), 1);
    g.fillRect (0, header, getWidth(), 1);

    g.setColour (findColour (gin::PluginLookAndFeel::grey30ColourId));
    g.fillRect (0, header + 1, getWidth(), 1);
}

void PianoAudioProcessorEditor::resized()
{
    ProcessorEditor::resized ();
	
	keyboard.setBounds (getGridArea (0, 3, 19, 1));
    keyboard.setKeyWidth ( 10.0f );
    auto ratio = keyboard.getWidth () / keyboard.getTotalKeyboardWidth ();
    keyboard.setKeyWidth ( 10.0f * ratio );

    auto r = getGridArea (0, 0, 19, 3);

    // faders
    auto h = 20;
    auto w = r.getWidth() / 4;
    auto rc = r.removeFromLeft (w);
    auto perCol = int (std::ceil (controls.size () / 4.0f));

    int i = 0;
    for (auto c : controls)
    {
        c->setBounds (rc.removeFromTop (h));
        i++;

        if (i == perCol)
        {
            i = 0;
            rc = r.removeFromLeft (w);
        }
    }

    // In the space under the last column of faders
    playbackTime.setBounds (rc.removeFromTop (h));

    applyScale();
}
