/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"



//==============================================================================
SimpMbCompAudioProcessor::SimpMbCompAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    using namespace params;
    const auto& params = GetParams();
    auto floatHelper = [&apvts = this->apvts, &params](auto& param, const auto& paramName)
    {
        param =
            dynamic_cast<juce::AudioParameterFloat*>(
                apvts
                .getParameter(params.at(paramName)));
        jassert(param != nullptr);
    };

    floatHelper(comperessor.attack, Name::Attack_Low_Band);
    floatHelper(comperessor.release, Name::Release_Low_Band);
    floatHelper(comperessor.threshold, Name::Threshold_Low_Band);

    auto ChoiceHelper = [&apvts = this->apvts, &params](auto& param, const auto& paramName)
    {
        param = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(params.at(paramName)));
        jassert(param != nullptr);
    };
    ChoiceHelper(comperessor.ratio, Name::Ratio_Low_Band);

    auto boolHelper = [&apvts = this->apvts, &params](auto& param, const auto& paramName)
    {
        param = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(params.at(paramName)));
        jassert(param != nullptr);
    };
    boolHelper(comperessor.bypassed, Name::Bypassed_Low_Band);
    floatHelper(lowCrossover, Name::Low_Mid_Crossover_Freq);
    LP.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    HP.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
    AP.setType(juce::dsp::LinkwitzRileyFilterType::allpass);
}

SimpMbCompAudioProcessor::~SimpMbCompAudioProcessor()
{
}

//==============================================================================
const juce::String SimpMbCompAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SimpMbCompAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SimpMbCompAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SimpMbCompAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SimpMbCompAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SimpMbCompAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SimpMbCompAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SimpMbCompAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String SimpMbCompAudioProcessor::getProgramName (int index)
{
    return {};
}

void SimpMbCompAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void SimpMbCompAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    juce::dsp::ProcessSpec spec;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = getTotalNumOutputChannels();
    spec.sampleRate = sampleRate;
    comperessor.prepare(spec);
    LP.prepare(spec);
    HP.prepare(spec);

    AP.prepare(spec);
    apBuffer.setSize(spec.numChannels, samplesPerBlock);

    for (auto& buffer: filterBuffers)
    {
        buffer.setSize(spec.numChannels, samplesPerBlock);
    }
  
}

void SimpMbCompAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SimpMbCompAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void SimpMbCompAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
   

    /*comperessor.updateCompressorSettings();
    comperessor.process(buffer);*/

    for(auto& fb : filterBuffers)
    {
        fb = buffer;
    }

    auto cutoff = lowCrossover->get();
    LP.setCutoffFrequency(cutoff);
    HP.setCutoffFrequency(cutoff);
    auto fb0Block = juce::dsp::AudioBlock<float>(filterBuffers[0]);
    auto fb1Block = juce::dsp::AudioBlock<float>(filterBuffers[1]);

    auto fb0Ctx = juce::dsp::ProcessContextReplacing<float>(fb0Block);
    auto fb1Ctx = juce::dsp::ProcessContextReplacing<float>(fb1Block);

	LP.process(fb0Ctx);
    HP.process(fb1Ctx);

    auto numSamples = buffer.getNumChannels();
    auto numChannels = buffer.getNumChannels();
    /*if (comperessor.bypassed->get())
        return;*/
    apBuffer = buffer;
    auto apBlock = juce::dsp::AudioBlock<float>(apBuffer);

    auto apContext = juce::dsp::ProcessContextReplacing<float>(apBlock);
    AP.process(apContext);
    buffer.clear();



    auto addFilterBand = [nc = numChannels, ns = numChannels](auto& inputBuffer, const auto& source)
    {
        for (auto i = 0; i < nc; ++i)
        {
            inputBuffer.addFrom(i, 0, source, i, 0, ns);
        }
    };

    if( !comperessor.bypassed->get())
    {
        addFilterBand(buffer, filterBuffers[0]);
    //    addFilterBand(buffer, filterBuffers[1]);
    }
    else
    {
        addFilterBand(buffer, apBuffer);
    }
}

//==============================================================================
bool SimpMbCompAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SimpMbCompAudioProcessor::createEditor()
{
   // return new SimpMbCompAudioProcessorEditor (*this);
    return new juce::GenericAudioProcessorEditor(*this);
}

//==============================================================================
void SimpMbCompAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream mos(destData, true);
    apvts.state.writeToStream(mos);
}

void SimpMbCompAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    auto tree = juce::ValueTree::readFromData(data, sizeInBytes);
        if(tree.isValid())
    {
            apvts.replaceState(tree);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout SimpMbCompAudioProcessor::createParameterLayout()
{

    APVTS::ParameterLayout layout;

    using namespace juce;
    using namespace  params;
    const auto& params = GetParams();
    layout.add(std::make_unique<AudioParameterFloat>(params.at(Name::Threshold_Low_Band),
        params.at(Name::Threshold_Low_Band),
        NormalisableRange<float>(-60, 12, 1, 1),
        0));

    auto attackReleaseRange = NormalisableRange<float>(5, 500, 1, 1);

    layout.add(std::make_unique<AudioParameterFloat>(params.at(Name::Attack_Low_Band),
        params.at(Name::Attack_Low_Band),
        attackReleaseRange,
        50));

    layout.add(std::make_unique<AudioParameterFloat>(params.at(Name::Release_Low_Band),
        params.at(Name::Release_Low_Band),
        attackReleaseRange,
        250));

    auto choices = std::vector<double>{ 1, 1.5, 2, 3, 4, 5, 6, 7, 8, 10, 15, 20, 50, 100 };
    juce::StringArray sa;
    for (auto choice : choices)
    {
        sa.add(juce::String(choice, 1));
    }

    layout.add(std::make_unique<AudioParameterChoice>(params.at(Name::Ratio_Low_Band), 
        params.at(Name::Ratio_Low_Band),
        sa, 3));

    layout.add(std::make_unique<AudioParameterBool>(params.at(Name::Bypassed_Low_Band),
        params.at(Name::Bypassed_Low_Band),
        false));
    layout.add(std::make_unique<AudioParameterFloat>(params.at(Name::Low_Mid_Crossover_Freq),
        params.at(Name::Low_Mid_Crossover_Freq), NormalisableRange<float>(20,
            20000, 1, 1), 500));
        
    return layout;
}




//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpMbCompAudioProcessor();
}
