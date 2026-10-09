#pragma once

#include <array>
#include <atomic>
#include <limits>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Config.h"
#include "Params.h"
#include "SpecimenLibrary.h"
#include "DSP/Effects.h"
#include "DSP/Modulation.h"
#include "DSP/NoteEngine.h"
#include "DSP/Regulator.h"
#include "DSP/Voice.h"

class LabX19AudioProcessor final : public juce::AudioProcessor,
                                   private labx19::dsp::NoteEngine::Sink,
                                   private juce::Timer
{
public:
    LabX19AudioProcessor();
    ~LabX19AudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return labx19::pluginName; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    labx19::SpecimenLibrary& getSpecimenLibrary() noexcept { return specimens; }
    labx19::SpecimenLibrary& getAirLibrary() noexcept      { return airSpecimens; }

    // Sets the STALKER SPECIMENS folder for both layers.
    void setLibraryRoot (const juce::File& folder, bool persist);

    // Editor-facing readouts, written on the audio thread.
    struct Meters
    {
        std::atomic<float> rmsDb { -100.0f }, peakDb { -100.0f }, dawn { 0.0f }, midiPerSecond { 0.0f };
        std::atomic<float> bpm { 120.0f }, gainReductionDb { 0.0f };
        std::atomic<int> activeVoices { 0 }, faults { 0 }, latency { 0 }, heldKeys { 0 }, arpStep { -1 };
        std::atomic<int> lastFaultStage { -1 };
        std::atomic<bool> synced { false };
    };
    Meters meters;

    // Runaway guard log: one line per fault or hot stage, with the parameters at the time of a fault.
    // Written on the message thread to <user app data>\OPERATION FAIRWAY, LLC\LAB X-19 faults.log.
    static const char* stageName (int stage) noexcept;
    juce::File getFaultLogFile() const;
    void setFaultLogFile (const juce::File& file) { faultLogOverride = file; }
    int flushFaultLog();   // returns the number of entries written

    static constexpr int scopeSize = 2048;
    void copyScope (float* dest, int numSamples) const noexcept;

    juce::AudioProcessorValueTreeState apvts;

   #if LABX19_HARNESS
    bool muteVoices = false;
    // Peak level after each stage (voices, grit, ensemble, delay, reverb) and when a stage first passed +12 dBFS.
    std::array<float, 5> stagePeak {};
    std::array<int64_t, 5> stageHotAt { -1, -1, -1, -1, -1 };
    int64_t samplesRendered = 0;
    int injectStage = -1;          // test hook: a 1e9 sample appears after this stage...
    int64_t injectAtSample = -1;   // ...in the control block containing this sample
   #endif

private:
    struct Smoother
    {
        bool enabled = false, logDomain = false;
        float current = 0.0f;

        void reset (float v) noexcept { current = logDomain ? std::log2 (std::max (v, 1.0e-4f)) : v; }

        float advance (float target, float coef) noexcept
        {
            const float t = logDomain ? std::log2 (std::max (target, 1.0e-4f)) : target;
            current += (t - current) * coef;
            return logDomain ? std::exp2 (current) : current;
        }
    };

    // NoteEngine::Sink
    void voiceOn (int note, float velocity, int tag) noexcept override;
    void voiceOff (int tag) noexcept override;
    void releaseAllVoices() noexcept override;

    void timerCallback() override { flushFaultLog(); }

    static float blockPeak (const float* l, const float* r, int n) noexcept;
    void noteFault (int stage, int note, float peak, bool fault) noexcept;

    void renderControlBlock (float* outL, float* outR, int n) noexcept;
    void updateControl (int n) noexcept;
    labx19::dsp::NoteEngine::Settings engineSettings() const noexcept;
    void flushForProgramChange() noexcept;
    void recoverFromFault() noexcept;

    void handleMidi (const juce::MidiMessage& m) noexcept;
    void allSoundOff() noexcept;
    int voiceMode() const noexcept;
    int polyLimit() const noexcept;
    int pickFreeVoice (int exclude) const noexcept;
    void removeMonoTag (int tag) noexcept;

    void applyPreset (int index);
    float rawValue (int index) const noexcept { return raw[(size_t) index]->load (std::memory_order_relaxed); }

    labx19::SpecimenLibrary specimens { "LAB X-19 specimen loader" };
    labx19::SpecimenLibrary airSpecimens { "LAB X-19 air loader" };
    const labx19::dsp::SpecimenData* blockSpecimen = nullptr;
    const labx19::dsp::SpecimenData* blockAir = nullptr;

    std::array<std::atomic<float>*, labx19::P::count> raw {};
    std::array<Smoother, labx19::P::count> smoothers {};
    std::array<float, labx19::P::count> values {};
    std::array<float, labx19::controlBlock + 1> smoothCoef {}, duckCoef {};

    std::array<labx19::dsp::Voice, labx19::maxVoices> voices;
    labx19::dsp::VoiceParams voiceParams;
    labx19::dsp::NoteEngine engine;
    labx19::dsp::Grit grit;
    labx19::dsp::Ensemble ensemble;
    labx19::dsp::EchoDelay echo;
    labx19::dsp::ShimmerReverb reverb;
    labx19::dsp::Lfo lfo;
    labx19::dsp::Regulator regulator;
    labx19::dsp::Transport transport;
    double currentSampleRate = 48000.0;

    // Global settings for the current control block
    float dawnEffective = 0.0f, lfoLevelTarget = 1.0f, lfoLevelGain = 1.0f, widthValue = 1.0f;
    int gritModeValue = 0;
    float gritAmountValue = 0.0f, gritDriveValue = 12.0f, gritToneValue = 600.0f, gritDustValue = 0.0f;
    float ensMixValue = 0.0f, ensDepthValue = 0.5f, ensRateValue = 0.6f;
    float delayMixValue = 0.0f, delaySecondsValue = 0.375f, delayFeedbackValue = 0.35f, delayToneValue = 5000.0f;
    bool delayPingPong = true;

    // MIDI and voice state (audio thread)
    struct MonoEntry { int note; int tag; float velocity; };
    std::array<MonoEntry, 64> monoStack {};
    int monoCount = 0, monoVoice = 0;
    float bendTarget = 0.0f, bend = 0.0f, modWheelTarget = 0.0f, modWheel = 0.0f;
    float lastNotePitch = 60.0f;
    bool anyNotePlayed = false;
    uint64_t voiceClock = 0;
    int lastSpecimenChoice = -1, lastAirChoice = -1;
    bool retriggerLfo = false;

    // Source changes: each layer ducks out until its new file has been published.
    bool specimenAwait = false, airAwait = false;
    uint64_t specimenAwaitId = 0, airAwaitId = 0;
    float specimenDuck = 1.0f, airDuck = 1.0f;

    // Preset changes: fade out, switch, fade back in.
    enum class SwitchState { idle, fadingOut, fadingIn };
    std::atomic<bool> programChangePending { false };
    SwitchState switchState = SwitchState::idle;
    int switchFadeRemaining = 0, switchFadeLength = 1440;
    float switchGain = 1.0f, switchFadeInStep = 0.005f;

    // Output stage
    float masterGain = 0.5f, dcInL = 0.0f, dcInR = 0.0f, dcOutL = 0.0f, dcOutR = 0.0f, dcCoef = 0.9995f;

    // Metering
    std::array<float, scopeSize> scope {};
    std::atomic<int> scopeWrite { 0 };
    int midiAccumulator = 0, meterSamples = 0;

    int currentProgram = 0;

    // Runaway guard
    static constexpr float voiceLimit = 64.0f;    // +36 dBFS for one voice
    static constexpr float stageLimit = 128.0f;   // +42 dBFS anywhere in the effect chain
    static constexpr float hotLevel = 8.0f;       // +18 dBFS: logged, not reset
    struct FaultEntry
    {
        int stage = 0, note = -1, activeVoices = 0, heldKeys = 0;
        float peakDb = 0.0f;
        bool fault = false, synced = false;
        double seconds = 0.0, bpm = 120.0;
    };
    std::array<FaultEntry, 64> faultRing {};
    std::atomic<int> faultWrite { 0 };
    int faultRead = 0;
    std::array<int64_t, 6> lastHotAt { -1, -1, -1, -1, -1, -1 };
    int64_t engineClock = 0;
    juce::File faultLogOverride;
    bool faultLogHeaderWritten = false;
    int preparedBlockSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabX19AudioProcessor)
};
