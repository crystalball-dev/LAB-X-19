#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"

using namespace labx19;

LabX19AudioProcessor::LabX19AudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LABX19", createParameterLayout())
{
    const auto& specs = paramSpecs();
    for (int i = 0; i < P::count; ++i)
    {
        raw[(size_t) i] = apvts.getRawParameterValue (specs[(size_t) i].id);
        jassert (raw[(size_t) i] != nullptr);
        smoothers[(size_t) i].enabled = specs[(size_t) i].smooth != Smooth::none;
        smoothers[(size_t) i].logDomain = specs[(size_t) i].smooth == Smooth::log;
    }

    applyPreset (0);
    startTimer (500);
    specimens.requestChoice ((int) std::lround (rawValue (P::specSource)));
    airSpecimens.requestChoice ((int) std::lround (rawValue (P::airSource)));
}

LabX19AudioProcessor::~LabX19AudioProcessor()
{
    stopTimer();
    flushFaultLog();
}

const char* LabX19AudioProcessor::stageName (int stage) noexcept
{
    static const char* names[] = { "VOICE", "GRIT", "ENSEMBLE", "DELAY", "REVERB", "OUTPUT" };
    return juce::isPositiveAndBelow (stage, 6) ? names[stage] : "?";
}

juce::File LabX19AudioProcessor::getFaultLogFile() const
{
    if (faultLogOverride != juce::File())
        return faultLogOverride;
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile (vendorName).getChildFile (juce::String (pluginName) + " faults.log");
}

int LabX19AudioProcessor::flushFaultLog()
{
    const int written = faultWrite.load (std::memory_order_acquire);
    if (written == faultRead)
        return 0;

    // The ring holds the latest 64 entries; anything older was overwritten.
    const int size = (int) faultRing.size();
    const int from = std::max (faultRead, written - size);
    const int lost = from - faultRead;
    faultRead = written;

    auto file = getFaultLogFile();
    file.getParentDirectory().createDirectory();
    juce::FileOutputStream out (file);
    if (! out.openedOk())
        return 0;
    out.setPosition (out.getFile().getSize());

    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S");
    if (! faultLogHeaderWritten)
    {
        faultLogHeaderWritten = true;
        out << "\n" << stamp << "  " << pluginName << " " << versionString << "  sample rate " << (int) currentSampleRate
            << "  block " << preparedBlockSize << "\n";
    }
    if (lost > 0)
        out << stamp << "  (" << lost << " entries lost: too many in half a second)\n";

    int count = 0;
    bool wantParams = false;
    for (int i = from; i < written; ++i)
    {
        const auto f = faultRing[(size_t) (i % size)];
        out << stamp << "  " << (f.fault ? "FAULT" : "hot  ") << "  " << juce::String (stageName (f.stage)).paddedRight (' ', 8)
            << " peak " << (f.peakDb > 900.0f ? juce::String ("non-finite") : juce::String (f.peakDb, 1) + " dBFS")
            << "  note " << (f.note >= 0 ? juce::String (f.note) : juce::String ("-"))
            << "  at " << juce::String (f.seconds, 2) << " s  voices " << f.activeVoices << "  keys " << f.heldKeys
            << "  bpm " << juce::String (f.bpm, 1) << (f.synced ? " playing" : " stopped")
            << "  preset " << getProgramName (currentProgram) << "\n";
        wantParams = wantParams || f.fault;
        ++count;
    }

    if (wantParams)
    {
        juce::String line ("    params:");
        for (const auto& spec : paramSpecs())
            line << " " << spec.id << "=" << juce::String (apvts.getRawParameterValue (spec.id)->load(), 3);
        line << "  specimen=\"" << specimens.getStatusText() << "\" air=\"" << airSpecimens.getStatusText() << "\"";
        out << line << "\n";
    }
    out.flush();
    return count;
}

void LabX19AudioProcessor::setLibraryRoot (const juce::File& folder, bool persist)
{
    specimens.setLibraryRoot (folder, persist);
    airSpecimens.setLibraryRoot (folder, false);
}

//==============================================================================
void LabX19AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedBlockSize = samplesPerBlock;
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const auto sr = (float) currentSampleRate;

    for (int n = 0; n <= controlBlock; ++n)
    {
        smoothCoef[(size_t) n] = 1.0f - std::exp (-(float) std::max (n, 1) / (0.015f * sr));
        duckCoef[(size_t) n] = 1.0f - std::exp (-(float) std::max (n, 1) / (0.006f * sr));
    }

    for (int i = 0; i < P::count; ++i)
    {
        smoothers[(size_t) i].reset (rawValue (i));
        values[(size_t) i] = rawValue (i);
    }

    for (int v = 0; v < maxVoices; ++v)
        voices[(size_t) v].prepare (sr, (uint32_t) (v + 1) * 7919u);

    engine.prepare (currentSampleRate, 0x19u);
    grit.prepare (sr);
    ensemble.prepare (sr);
    echo.prepare (sr);
    reverb.prepare (sr);
    lfo.prepare (sr, 19u);
    regulator.prepare (sr);

    const int latency = regulator.getLatency();
    setLatencySamples (latency);
    meters.latency.store (latency);

    monoCount = 0;
    monoVoice = 0;
    bend = bendTarget = 0.0f;
    modWheel = modWheelTarget = 0.0f;
    retriggerLfo = false;
    masterGain = dsp::dbToGain (rawValue (P::volume));
    dcInL = dcInR = dcOutL = dcOutR = 0.0f;
    dcCoef = 1.0f - dsp::twoPi * 4.0f / sr;
    lfoLevelGain = lfoLevelTarget = 1.0f;
    midiAccumulator = meterSamples = 0;

    lastSpecimenChoice = lastAirChoice = -1;
    specimenAwait = airAwait = false;
    specimenDuck = airDuck = 1.0f;

    programChangePending.store (false);
    switchState = SwitchState::idle;
    switchGain = 1.0f;
    switchFadeLength = std::max (1, (int) (0.03 * currentSampleRate));
    switchFadeRemaining = 0;
    switchFadeInStep = 1.0f / (0.005f * sr);

    transport = {};
    transport.bpm = 120.0;
    updateControl (controlBlock);
}

void LabX19AudioProcessor::releaseResources()
{
    for (auto& v : voices)
        v.kill();
}

bool LabX19AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet().isDisabled() || layouts.inputBuses.isEmpty();
}

double LabX19AudioProcessor::getTailLengthSeconds() const
{
    const float release = rawValue (P::env1R) * 2.5f;
    const float rt60 = rawValue (P::revMix) > 0.0f ? 0.3f * std::exp2 (rawValue (P::revDecay) * 6.0f) : 0.0f;
    const float fb = rawValue (P::delayFeedback);
    const float echoes = rawValue (P::delayMix) > 0.0f ? 2.0f * std::log (0.001f) / std::log (std::max (fb, 0.05f)) * 0.5f : 0.0f;
    return (double) std::clamp (release + std::max (rt60, echoes), 0.1f, 30.0f);
}

//==============================================================================
dsp::NoteEngine::Settings LabX19AudioProcessor::engineSettings() const noexcept
{
    auto choiceOf = [this] (int index) { return (int) std::lround (rawValue (index)); };
    dsp::NoteEngine::Settings s;
    s.chordType = std::clamp (choiceOf (P::chordType), 0, numChordShapes - 1);
    s.voicing = choiceOf (P::chordVoicing);
    s.strumMs = rawValue (P::strumTime);
    s.strumDir = choiceOf (P::strumDir);
    s.arpMode = choiceOf (P::arpMode);
    s.arpBeats = arpRateBeats[std::clamp (choiceOf (P::arpRate), 0, (int) std::size (arpRateBeats) - 1)];
    s.arpOctaves = choiceOf (P::arpOctaves);
    s.arpGate = rawValue (P::arpGate);
    s.arpSwing = rawValue (P::arpSwing);
    s.arpLatch = choiceOf (P::arpLatch) == 1;
    s.mono = voiceMode() != Choice::poly;
    return s;
}

void LabX19AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    buffer.clear();

    // Both source layers: request on change, duck until the new file is in.
    blockSpecimen = specimens.acquireForBlock();
    blockAir = airSpecimens.acquireForBlock();
    const uint64_t specimenId = blockSpecimen != nullptr ? blockSpecimen->id : 0;
    const uint64_t airId = blockAir != nullptr ? blockAir->id : 0;

    const int choice = (int) std::lround (rawValue (P::specSource));
    if (choice != lastSpecimenChoice)
    {
        specimens.requestChoice (choice);
        if (lastSpecimenChoice >= 0)
        {
            specimenAwait = true;
            specimenAwaitId = specimenId;
        }
        lastSpecimenChoice = choice;
    }
    else if (specimenAwait && (specimenId != specimenAwaitId || specimens.isSettled()))
        specimenAwait = false;

    const int airChoice = (int) std::lround (rawValue (P::airSource));
    if (airChoice != lastAirChoice)
    {
        airSpecimens.requestChoice (airChoice);
        if (lastAirChoice >= 0)
        {
            airAwait = true;
            airAwaitId = airId;
        }
        lastAirChoice = airChoice;
    }
    else if (airAwait && (airId != airAwaitId || airSpecimens.isSettled()))
        airAwait = false;

    if (programChangePending.exchange (false))
    {
        if (switchState != SwitchState::fadingOut)
        {
            // Start from the current gain so rapid preset clicks never jump the level back up.
            switchState = SwitchState::fadingOut;
            switchFadeRemaining = (int) std::ceil (switchGain * (float) switchFadeLength);
        }
    }

    // Host transport: tempo always when available, beat position only while playing.
    bool synced = false;
    double ppqStart = 0.0;
    if (auto* host = getPlayHead())
    {
        if (auto position = host->getPosition())
        {
            if (auto bpm = position->getBpm())
                if (*bpm > 1.0)
                    transport.bpm = *bpm;
            if (position->getIsPlaying())
                if (auto ppq = position->getPpqPosition())
                {
                    synced = true;
                    ppqStart = *ppq;
                }
        }
    }
    transport.synced = synced;

    if (voiceMode() == Choice::poly)
    {
        const int limit = polyLimit();
        for (int v = limit; v < maxVoices; ++v)
            if (voices[(size_t) v].isActive() && voices[(size_t) v].isKeyDown())
                voices[(size_t) v].noteOff();
    }

    auto midiIt = midi.cbegin();
    const auto midiEnd = midi.cend();

    if (numSamples == 0)
    {
        for (; midiIt != midiEnd; ++midiIt)
            handleMidi ((*midiIt).getMessage());
        return;
    }

    std::array<float, controlBlock> blockL {}, blockR {};
    double sumSquares = 0.0;
    float peak = 0.0f;
    int pos = 0;
    int scopePos = scopeWrite.load (std::memory_order_relaxed);
    const double beatsPerSample = transport.bpm / 60.0 / currentSampleRate;
    const auto settings = engineSettings();

    while (pos < numSamples)
    {
        while (midiIt != midiEnd && (*midiIt).samplePosition <= pos)
        {
            const double ppqHere = synced ? ppqStart + (double) pos * beatsPerSample : 0.0;
            engine.begin (settings, synced, ppqHere, transport.bpm, *this);
            handleMidi ((*midiIt).getMessage());
            ++midiIt;
        }

        transport.ppq = synced ? ppqStart + (double) pos * beatsPerSample : 0.0;
        engine.begin (settings, synced, transport.ppq, transport.bpm, *this);
        engine.fire (*this);

        int next = numSamples;
        if (midiIt != midiEnd)
            next = std::min (next, (*midiIt).samplePosition);
        const int n = std::min ({ controlBlock, std::max (1, next - pos), engine.samplesUntilNext() });

        renderControlBlock (blockL.data(), blockR.data(), n);
        engine.advance (n);

        if (numChannels >= 2)
        {
            buffer.copyFrom (0, pos, blockL.data(), n);
            buffer.copyFrom (1, pos, blockR.data(), n);
        }
        else if (numChannels == 1)
        {
            buffer.copyFrom (0, pos, blockL.data(), n, 0.5f);
            buffer.addFrom (0, pos, blockR.data(), n, 0.5f);
        }

        for (int i = 0; i < n; ++i)
        {
            const float l = blockL[(size_t) i], r = blockR[(size_t) i];
            sumSquares += (double) (l * l + r * r) * 0.5;
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
            scope[(size_t) scopePos] = 0.5f * (l + r);
            scopePos = (scopePos + 1) & (scopeSize - 1);
        }

        pos += n;
    }
    scopeWrite.store (scopePos, std::memory_order_release);

    for (; midiIt != midiEnd; ++midiIt)
        handleMidi ((*midiIt).getMessage());

    // Readouts
    int active = 0;
    for (auto& v : voices)
        if (v.isActive() && ! v.isFading())
            ++active;

    meterSamples += numSamples;
    if (meterSamples >= (int) (currentSampleRate * 0.1))
    {
        meters.midiPerSecond.store ((float) midiAccumulator * (float) currentSampleRate / (float) meterSamples);
        midiAccumulator = 0;
        meterSamples = 0;
    }

    const float rms = (float) std::sqrt (sumSquares / (double) numSamples);
    meters.rmsDb.store (juce::Decibels::gainToDecibels (rms, -100.0f));
    meters.peakDb.store (juce::Decibels::gainToDecibels (peak, -100.0f));
    meters.activeVoices.store (active);
    meters.dawn.store (dawnEffective);
    meters.bpm.store ((float) transport.bpm);
    meters.synced.store (synced);
    meters.gainReductionDb.store (regulator.getGainReductionDb());
    meters.heldKeys.store (engine.getHeldKeyCount());
    meters.arpStep.store (engine.getArpStep());
}

void LabX19AudioProcessor::renderControlBlock (float* outL, float* outR, int n) noexcept
{
    updateControl (n);

    std::fill_n (outL, n, 0.0f);
    std::fill_n (outR, n, 0.0f);

    voiceParams.specimen = blockSpecimen;
    voiceParams.air = blockAir;

    // Each voice renders on its own first, so a voice that runs away can be stopped alone and named in the log.
    std::array<float, controlBlock> vl {}, vr {};
    for (auto& v : voices)
    {
        if (! v.isActive())
            continue;
        std::fill_n (vl.data(), n, 0.0f);
        std::fill_n (vr.data(), n, 0.0f);
        v.render (vl.data(), vr.data(), n, voiceParams);
       #if LABX19_HARNESS
        if (injectStage == 6 && injectAtSample >= samplesRendered && injectAtSample < samplesRendered + n)
            vl[0] = std::numeric_limits<float>::infinity();
       #endif
        const float pk = blockPeak (vl.data(), vr.data(), n);
        if (! (pk <= voiceLimit))
        {
            noteFault (0, v.getNote(), pk, true);
            v.kill();
            continue;
        }
        for (int i = 0; i < n; ++i)
        {
            outL[i] += vl[(size_t) i];
            outR[i] += vr[(size_t) i];
        }
    }

   #if LABX19_HARNESS
    if (muteVoices)
    {
        std::fill_n (outL, n, 0.0f);
        std::fill_n (outR, n, 0.0f);
    }
   #endif

    // LFO level modulation sits before the effects, so echoes and tails keep their own shape.
    const float levelStep = (lfoLevelTarget - lfoLevelGain) / (float) n;
    for (int i = 0; i < n; ++i)
    {
        lfoLevelGain += levelStep;
        outL[i] *= lfoLevelGain;
        outR[i] *= lfoLevelGain;
    }

    // Runaway guard: after every stage the block's peak is checked. Above +18 dBFS the stage is logged as hot;
    // non-finite or above +42 dBFS it is a fault: the block is silenced and every effect starts again from zero.
    bool runaway = false;
    auto check = [&] (int stage)
    {
        if (runaway)
            return;
       #if LABX19_HARNESS
        if (stage == injectStage && injectAtSample >= samplesRendered && injectAtSample < samplesRendered + n)
            outL[0] = 1.0e9f;
       #endif
        const float pk = blockPeak (outL, outR, n);
       #if LABX19_HARNESS
        stagePeak[(size_t) stage] = std::max (stagePeak[(size_t) stage], std::isfinite (pk) ? pk : 1.0e30f);
        if (! (pk <= 4.0f) && stageHotAt[(size_t) stage] < 0)
            stageHotAt[(size_t) stage] = samplesRendered;
       #endif
        if (! (pk <= stageLimit))
        {
            runaway = true;
            noteFault (stage, -1, pk, true);
        }
        else if (pk > hotLevel)
        {
            noteFault (stage, -1, pk, false);
        }
    };

    check (0);
    grit.process (outL, outR, n, gritModeValue, gritAmountValue, gritDriveValue, gritToneValue, gritDustValue);
    check (1);
    ensemble.process (outL, outR, n, ensMixValue, ensDepthValue, ensRateValue);
    check (2);
    echo.process (outL, outR, n, delayMixValue, delaySecondsValue, delayFeedbackValue, delayToneValue, delayPingPong);
    check (3);
    reverb.process (outL, outR, n);
    check (4);
   #if LABX19_HARNESS
    samplesRendered += n;
   #endif

    if (runaway)
    {
        std::fill_n (outL, n, 0.0f);
        std::fill_n (outR, n, 0.0f);
        recoverFromFault();
    }

    const float gainTarget = dsp::dbToGain (values[P::volume]);
    for (int i = 0; i < n; ++i)
    {
        masterGain += (gainTarget - masterGain) * 0.002f;
        const float mid = 0.5f * (outL[i] + outR[i]);
        const float side = 0.5f * (outL[i] - outR[i]) * widthValue;
        const float l = (mid + side) * masterGain;
        const float r = (mid - side) * masterGain;

        const float dl = l - dcInL + dcCoef * dcOutL;
        const float dr = r - dcInR + dcCoef * dcOutR;
        dcInL = l; dcOutL = dl;
        dcInR = r; dcOutR = dr;
        outL[i] = dl;
        outR[i] = dr;
    }

    regulator.process (outL, outR, n, values[P::regAmount], values[P::regCeiling], 0.0f);

    bool fault = false;
    for (int i = 0; i < n; ++i)
    {
        float l = outL[i];
        float r = outR[i];

        if (! std::isfinite (l) || ! std::isfinite (r))
        {
            // Never pass garbage to the host: silence this sample and reset after the block.
            fault = true;
            l = r = 0.0f;
        }

        if (switchState == SwitchState::fadingOut)
        {
            switchGain = (float) switchFadeRemaining / (float) switchFadeLength;
            if (switchFadeRemaining > 0)
                --switchFadeRemaining;
        }
        else if (switchState == SwitchState::fadingIn)
        {
            switchGain = std::min (1.0f, switchGain + switchFadeInStep);
            if (switchGain >= 1.0f)
                switchState = SwitchState::idle;
        }

        outL[i] = dsp::safetyClip (l) * switchGain;
        outR[i] = dsp::safetyClip (r) * switchGain;
    }

    if (fault)
    {
        noteFault (5, -1, std::numeric_limits<float>::quiet_NaN(), true);
        recoverFromFault();
    }

    engineClock += n;

    if (switchState == SwitchState::fadingOut && switchFadeRemaining == 0)
    {
        flushForProgramChange();
        switchState = SwitchState::fadingIn;
        switchGain = 0.0f;
    }
}

void LabX19AudioProcessor::updateControl (int n) noexcept
{
    const float coef = smoothCoef[(size_t) std::clamp (n, 0, controlBlock)];

    for (int i = 0; i < P::count; ++i)
    {
        const float target = rawValue (i);
        values[(size_t) i] = smoothers[(size_t) i].enabled ? smoothers[(size_t) i].advance (target, coef) : target;
    }

    bend += (bendTarget - bend) * std::min (1.0f, coef * 4.0f);
    modWheel += (modWheelTarget - modWheel) * coef;
    const float duck = duckCoef[(size_t) std::clamp (n, 0, controlBlock)];
    specimenDuck += ((specimenAwait ? 0.0f : 1.0f) - specimenDuck) * duck;
    airDuck += ((airAwait ? 0.0f : 1.0f) - airDuck) * duck;

    const auto& v = values;
    auto choiceOf = [&v] (int index) { return (int) std::lround (v[(size_t) index]); };

    // DAWN (plus the mod wheel) clears the haze.
    const float dawn = std::clamp (v[P::dawn] + modWheel, 0.0f, 1.0f);
    dawnEffective = dawn;

    // LFO
    if (retriggerLfo)
    {
        lfo.retrigger();
        retriggerLfo = false;
    }
    const int syncIndex = std::clamp (choiceOf (P::lfoSync), 0, (int) std::size (lfoSyncBeats) - 1);
    const float lfoValue = lfo.advance (n, choiceOf (P::lfoShape), v[P::lfoRate], lfoSyncBeats[syncIndex], transport,
                                        choiceOf (P::lfoRetrig) == 1);
    const float depth = v[P::lfoDepth];
    const float mod = lfoValue * depth;
    const int target = choiceOf (P::lfoTarget);

    auto& p = voiceParams;
    p.modCutoffOct = (target == Choice::toCutoff ? mod * 4.0f : 0.0f) + 1.5f * dawn;
    p.modPitch     = target == Choice::toPitch    ? mod * 12.0f : 0.0f;
    p.modShape     = target == Choice::toShape    ? mod         : 0.0f;
    p.modVowel     = (target == Choice::toVowel   ? mod * 0.5f  : 0.0f) + 0.25f * dawn;
    p.modPosition  = target == Choice::toPosition ? mod         : 0.0f;
    if (target == Choice::toLevel)
        lfoLevelTarget = depth >= 0.0f ? 1.0f - depth * 0.5f * (1.0f - lfoValue) : 1.0f + depth * 0.5f * (1.0f + lfoValue);
    else
        lfoLevelTarget = 1.0f;

    // Voices
    p.oscAWave  = choiceOf (P::oscAWave);
    p.oscAPitch = 12.0f * (float) choiceOf (P::oscAOctave) + (float) choiceOf (P::oscASemi) + v[P::oscAFine] * 0.01f;
    p.oscAShape = v[P::oscAShape];
    p.oscALevel = v[P::oscALevel];

    p.uniVoices = choiceOf (P::uniVoices);
    p.uniDetune = v[P::uniDetune];
    p.uniSpread = v[P::uniSpread];

    p.oscBWave  = choiceOf (P::oscBWave);
    p.oscBPitch = 12.0f * (float) choiceOf (P::oscBOctave) + (float) choiceOf (P::oscBSemi) + v[P::oscBFine] * 0.01f;
    p.oscBShape = v[P::oscBShape];
    p.oscBLevel = v[P::oscBLevel];

    p.subLevel    = v[P::subLevel];
    p.noiseLevel  = v[P::noiseLevel];
    p.noiseColour = v[P::noiseColour];

    p.choirLevel        = v[P::choirLevel];
    p.choirOctave       = (float) choiceOf (P::choirOctave);
    p.choir.singers     = choiceOf (P::choirSingers);
    p.choir.detuneCents = v[P::choirDetune];
    p.choir.vibrato     = v[P::choirVibrato];
    p.choir.breath      = v[P::choirBreath];
    p.choir.vowel       = v[P::choirVowel];
    p.choir.size        = v[P::choirSize];

    p.specMode     = choiceOf (P::specMode);
    p.specLevel    = v[P::specLevel] * specimenDuck;
    p.specPosition = v[P::specPosition];
    p.specSize     = v[P::specSize] * 0.001f;
    p.specDensity  = v[P::specDensity];
    p.specSpray    = v[P::specSpray];
    p.specTrack    = v[P::specTrack];
    p.specTune     = v[P::specTune];

    p.airLevel    = std::min (1.0f, v[P::airLevel] * (1.0f + 0.6f * dawn)) * airDuck;
    p.airPosition = v[P::airPosition];
    p.airSize     = v[P::airSize] * 0.001f;
    p.airTrack    = v[P::airTrack];

    p.filterModel = choiceOf (P::filterModel);
    p.cutoff      = v[P::cutoff];
    p.resonance   = v[P::resonance];
    p.driveDb     = v[P::filterDrive];
    p.filterEnv   = v[P::filterEnv];
    p.keytrack    = v[P::keytrack];
    p.filterVel   = v[P::filterVel];

    p.a1 = v[P::env1A]; p.d1 = v[P::env1D]; p.s1 = v[P::env1S]; p.r1 = v[P::env1R];
    p.a2 = v[P::env2A]; p.d2 = v[P::env2D]; p.s2 = v[P::env2S]; p.r2 = v[P::env2R];
    p.ampVel = v[P::ampVel];

    p.glide     = v[P::glide];
    p.pitchBend = bend * (float) choiceOf (P::bendRange);

    // GRIT: DAWN pulls it back.
    gritModeValue   = choiceOf (P::gritMode);
    gritAmountValue = v[P::gritAmount] * (1.0f - 0.6f * dawn);
    gritDriveValue  = v[P::gritDrive];
    gritToneValue   = v[P::gritTone];
    gritDustValue   = v[P::gritDust] * (1.0f - 0.6f * dawn);

    ensMixValue   = std::min (1.0f, v[P::ensMix] + 0.25f * dawn);
    ensDepthValue = v[P::ensDepth];
    ensRateValue  = v[P::ensRate];

    const int delaySync = std::clamp (choiceOf (P::delaySync), 0, (int) std::size (delaySyncBeats) - 1);
    delaySecondsValue  = delaySync == 0 ? v[P::delayTime] * 0.001f
                                        : (float) (delaySyncBeats[delaySync] * 60.0 / std::max (20.0, transport.bpm));
    delaySecondsValue  = std::min (delaySecondsValue, 2.5f);
    delayMixValue      = v[P::delayMix];
    delayFeedbackValue = v[P::delayFeedback];
    delayToneValue     = v[P::delayTone];
    delayPingPong      = choiceOf (P::delayMode) == Choice::pingPong;

    reverb.setParameters (v[P::revSize], v[P::revDecay], v[P::revDamping],
                          std::min (1.0f, v[P::revMix] + 0.15f * dawn),
                          std::min (1.0f, v[P::revShimmer] + 0.4f * dawn),
                          v[P::revPredelay] * 0.001f);

    widthValue = v[P::width];
}

void LabX19AudioProcessor::flushForProgramChange() noexcept
{
    // The new preset starts from silence: no voices, held notes or effect tails carrying old settings.
    for (auto& v : voices)
        v.kill();
    monoCount = 0;
    engine.reset();
    grit.reset();
    ensemble.reset();
    echo.reset();
    reverb.flush();
    regulator.reset();
    lfo.reset();
    dcInL = dcInR = dcOutL = dcOutR = 0.0f;
    for (int i = 0; i < P::count; ++i)
        smoothers[(size_t) i].reset (rawValue (i));
    masterGain = dsp::dbToGain (rawValue (P::volume));
}

float LabX19AudioProcessor::blockPeak (const float* l, const float* r, int n) noexcept
{
    // NaN never compares, so a non-finite sample turns the result into NaN, which every guard treats as a fault.
    float pk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float a = std::abs (l[i]), b = std::abs (r[i]);
        if (! (a < 3.0e38f) || ! (b < 3.0e38f))
            return std::numeric_limits<float>::quiet_NaN();
        pk = std::max (pk, std::max (a, b));
    }
    return pk;
}

void LabX19AudioProcessor::noteFault (int stage, int note, float peak, bool fault) noexcept
{
    const int64_t now = engineClock;
    if (! fault)
    {
        // Hot warnings: at most one per stage every two seconds.
        auto& last = lastHotAt[(size_t) stage];
        if (last >= 0 && now - last < (int64_t) (2.0 * currentSampleRate))
            return;
        last = now;
    }

    const int index = faultWrite.load (std::memory_order_relaxed);
    auto& f = faultRing[(size_t) (index % (int) faultRing.size())];
    f.stage = stage;
    f.note = note;
    f.peakDb = std::isfinite (peak) ? juce::Decibels::gainToDecibels (peak, -120.0f) : 999.0f;
    f.fault = fault;
    f.seconds = (double) now / currentSampleRate;
    f.activeVoices = meters.activeVoices.load (std::memory_order_relaxed);
    f.heldKeys = engine.getHeldKeyCount();
    f.bpm = transport.bpm;
    f.synced = transport.synced;
    faultWrite.store (index + 1, std::memory_order_release);

    if (fault)
    {
        meters.faults.fetch_add (1);
        meters.lastFaultStage.store (stage);
    }
}

void LabX19AudioProcessor::recoverFromFault() noexcept
{
    for (auto& v : voices)
        v.kill();
    monoCount = 0;
    grit.reset();
    ensemble.reset();
    echo.reset();
    reverb.flush();
    regulator.reset();
    lfo.reset();
    dcInL = dcInR = dcOutL = dcOutR = 0.0f;
}

//==============================================================================
void LabX19AudioProcessor::handleMidi (const juce::MidiMessage& m) noexcept
{
    ++midiAccumulator;

    if (m.isNoteOn())
    {
        if ((int) std::lround (rawValue (P::lfoRetrig)) == 1)
            retriggerLfo = true;
        engine.keyDown (m.getNoteNumber(), m.getFloatVelocity(), *this);
    }
    else if (m.isNoteOff())
        engine.keyUp (m.getNoteNumber(), *this);
    else if (m.isPitchWheel())
        bendTarget = ((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f;
    else if (m.isAllNotesOff())
        engine.allNotesOff (*this);
    else if (m.isAllSoundOff())
        allSoundOff();
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        const int val = m.getControllerValue();
        if (cc == 1)
            modWheelTarget = (float) val / 127.0f;
        else if (cc == 64)
            engine.setSustain (val >= 64, *this);
    }
}

int LabX19AudioProcessor::voiceMode() const noexcept
{
    return std::clamp ((int) std::lround (rawValue (P::voiceMode)), 0, 2);
}

int LabX19AudioProcessor::polyLimit() const noexcept
{
    return juce::jlimit (1, maxVoices, (int) std::lround (rawValue (P::voices)));
}

int LabX19AudioProcessor::pickFreeVoice (int exclude) const noexcept
{
    for (int v = 0; v < maxVoices; ++v)
        if (v != exclude && ! voices[(size_t) v].isActive())
            return v;

    int best = exclude == 0 ? 1 : 0;
    for (int v = 0; v < maxVoices; ++v)
        if (v != exclude && voices[(size_t) v].getLevel() < voices[(size_t) best].getLevel())
            best = v;
    return best;
}

void LabX19AudioProcessor::removeMonoTag (int tag) noexcept
{
    for (int i = 0; i < monoCount; ++i)
        if (monoStack[(size_t) i].tag == tag)
        {
            for (int j = i; j < monoCount - 1; ++j)
                monoStack[(size_t) j] = monoStack[(size_t) j + 1];
            --monoCount;
            return;
        }
}

void LabX19AudioProcessor::voiceOn (int note, float velocity, int tag) noexcept
{
    dsp::NoteStart s;
    s.note = note;
    s.velocity = velocity;
    s.tag = tag;
    s.stamp = ++voiceClock;

    const bool glideOn = rawValue (P::glide) > 0.0005f;
    const int mode = voiceMode();

    if (mode != Choice::poly)
    {
        const bool keyWasHeld = monoCount > 0;
        removeMonoTag (tag);
        if (monoCount == (int) monoStack.size())
        {
            for (int j = 0; j < monoCount - 1; ++j)
                monoStack[(size_t) j] = monoStack[(size_t) j + 1];
            --monoCount;
        }
        monoStack[(size_t) monoCount++] = { note, tag, velocity };

        // Notes left over from poly playing are released.
        for (int v = 0; v < maxVoices; ++v)
            if (v != monoVoice && voices[(size_t) v].isActive() && voices[(size_t) v].isKeyDown())
                voices[(size_t) v].noteOff();

        auto& current = voices[(size_t) monoVoice];
        if (mode == Choice::legato && keyWasHeld && current.isActive() && ! current.isFading())
        {
            current.legatoTo (note, velocity, tag);   // overlapping notes slide
        }
        else
        {
            s.startPitch = current.isActive() ? current.getPitch() : lastNotePitch;
            s.glideFromStart = glideOn && anyNotePlayed;
            current.noteOn (s);
        }
    }
    else
    {
        const int limit = polyLimit();
        dsp::Voice* target = nullptr;

        for (int v = 0; v < limit && target == nullptr; ++v)
            if (! voices[(size_t) v].isActive())
                target = &voices[(size_t) v];

        if (target == nullptr)
        {
            // Steal the quietest releasing voice, otherwise the oldest one.
            float quietest = 2.0f;
            for (int v = 0; v < limit; ++v)
            {
                auto& candidate = voices[(size_t) v];
                if (candidate.isReleasing() && candidate.getLevel() < quietest)
                {
                    quietest = candidate.getLevel();
                    target = &candidate;
                }
            }
        }

        if (target == nullptr)
        {
            uint64_t oldest = std::numeric_limits<uint64_t>::max();
            for (int v = 0; v < limit; ++v)
                if (voices[(size_t) v].getAge() < oldest)
                {
                    oldest = voices[(size_t) v].getAge();
                    target = &voices[(size_t) v];
                }
        }

        s.startPitch = lastNotePitch;
        s.glideFromStart = glideOn && anyNotePlayed;
        if (target != nullptr)
            target->noteOn (s);
    }

    lastNotePitch = (float) note;
    anyNotePlayed = true;
}

void LabX19AudioProcessor::voiceOff (int tag) noexcept
{
    if (voiceMode() != Choice::poly)
    {
        removeMonoTag (tag);
        auto& voice = voices[(size_t) monoVoice];
        if (voice.isActive() && voice.isKeyDown() && voice.getTag() == tag)
        {
            if (monoCount > 0)
            {
                const auto back = monoStack[(size_t) monoCount - 1];
                voice.returnTo (back.note, back.tag);
                lastNotePitch = (float) back.note;
            }
            else
                voice.noteOff();
        }
        return;
    }

    for (auto& v : voices)
        if (v.isActive() && v.isKeyDown() && v.getTag() == tag)
            v.noteOff();
}

void LabX19AudioProcessor::releaseAllVoices() noexcept
{
    monoCount = 0;
    for (auto& v : voices)
        if (v.isActive())
            v.noteOff();
}

void LabX19AudioProcessor::allSoundOff() noexcept
{
    engine.reset();
    monoCount = 0;
    for (auto& v : voices)
        v.kill();
    echo.reset();
    reverb.flush();
}

//==============================================================================
void LabX19AudioProcessor::copyScope (float* dest, int numSamples) const noexcept
{
    numSamples = std::min (numSamples, scopeSize);
    int r = (scopeWrite.load (std::memory_order_acquire) - numSamples) & (scopeSize - 1);
    for (int i = 0; i < numSamples; ++i)
    {
        dest[i] = scope[(size_t) r];
        r = (r + 1) & (scopeSize - 1);
    }
}

//==============================================================================
int LabX19AudioProcessor::getNumPrograms()
{
    return (int) factoryPresets().size();
}

int LabX19AudioProcessor::getCurrentProgram()
{
    return currentProgram;
}

void LabX19AudioProcessor::setCurrentProgram (int index)
{
    if (juce::isPositiveAndBelow (index, getNumPrograms()))
        applyPreset (index);
}

const juce::String LabX19AudioProcessor::getProgramName (int index)
{
    const auto& presets = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) presets.size()) ? presets[(size_t) index].name : juce::String();
}

void LabX19AudioProcessor::applyPreset (int index)
{
    const auto& presets = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    // Every parameter is set once, straight to its final value, so the audio thread never
    // hears an intermediate all-defaults patch.
    const auto& preset = presets[(size_t) index];
    for (const auto& spec : paramSpecs())
    {
        auto* param = apvts.getParameter (spec.id);
        if (param == nullptr)
            continue;

        float target = param->convertFrom0to1 (param->getDefaultValue());
        for (const auto& [id, value] : preset.values)
            if (id == spec.id)
            {
                target = value;
                break;
            }
        param->setValueNotifyingHost (param->convertTo0to1 (target));
    }

    currentProgram = index;
    programChangePending.store (true);
}

//==============================================================================
void LabX19AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("userFile", specimens.getUserFile().getFullPathName(), nullptr);
    state.setProperty ("airUserFile", airSpecimens.getUserFile().getFullPathName(), nullptr);
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("version", versionString, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void LabX19AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto userPath = tree.getProperty ("userFile").toString();
    const auto airPath = tree.getProperty ("airUserFile").toString();
    currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) tree.getProperty ("program", 0));

    apvts.replaceState (tree);
    specimens.setUserFile (juce::File::isAbsolutePath (userPath) ? juce::File (userPath) : juce::File());
    airSpecimens.setUserFile (juce::File::isAbsolutePath (airPath) ? juce::File (airPath) : juce::File());
}

//==============================================================================
juce::AudioProcessorEditor* LabX19AudioProcessor::createEditor()
{
    return new LabX19AudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LabX19AudioProcessor();
}
