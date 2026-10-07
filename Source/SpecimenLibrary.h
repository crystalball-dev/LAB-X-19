#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include "DSP/Specimen.h"
#include "SpecimenCatalog.h"

namespace labx19
{
    // Loads one source file from the STALKER SPECIMENS library (or the user's own file) on a background thread,
    // cuts a wavetable from it, and hands it to the audio thread without locks. The SPECIMEN and AIR layers each
    // own one. Retired buffers are freed only after the audio thread has started two more blocks, so a buffer is
    // never freed while it is read.
    class SpecimenLibrary : private juce::Thread
    {
    public:
        enum class Status { idle, loading, ready, missingLibrary, missingFile, failed };

        explicit SpecimenLibrary (const juce::String& threadName);
        ~SpecimenLibrary() override;

        // Any thread, lock-free. choice is a value of a specimen source parameter.
        void requestChoice (int choice) noexcept;

        // Audio thread: call once at the start of every block and use the result for that block.
        const dsp::SpecimenData* acquireForBlock() noexcept;

        // Message thread.
        void setLibraryRoot (const juce::File& folder, bool persist);
        juce::File getLibraryRoot() const;
        void setUserFile (const juce::File& file);
        juce::File getUserFile() const;

        Status getStatus() const noexcept { return status.load(); }
        juce::String getStatusText() const;

        // Fundamental the wavetable was cut at (0 when the source is noise-like), and the frame count.
        float getTableHz() const noexcept { return tableHz.load(); }
        int getPeriodicFrames() const noexcept { return periodicFrames.load(); }

        // True once the latest request has been handled. Lock-free, safe on the audio thread.
        bool isSettled() const noexcept;

        // Blocks until the latest request has been handled. Used by the render harness.
        bool waitUntilSettled (int timeoutMs) const;

        static juce::File autodetectLibrary();
        static bool looksLikeLibrary (const juce::File& folder);

        // Cuts 16 single-cycle frames from a mono recording and builds the band-limited mip levels.
        static dsp::WavetableData buildTable (const float* data, int length, double sampleRate);

    private:
        void run() override;
        void load (int choice);
        void publish (std::unique_ptr<dsp::SpecimenData> next);
        void reclaim();
        void setStatus (Status s, const juce::String& text);

        juce::AudioFormatManager formats;
        std::unique_ptr<juce::PropertiesFile> settings;

        mutable juce::CriticalSection lock;   // guards root, userFile, statusText
        juce::File root, userFile;
        juce::String statusText { "NO SPECIMEN" };

        std::atomic<int> requested { -1 }, generation { 0 };
        std::atomic<int> handledChoice { -2 }, handledGeneration { -1 };
        std::atomic<Status> status { Status::idle };
        std::atomic<float> tableHz { 0.0f };
        std::atomic<int> periodicFrames { 0 };

        std::atomic<dsp::SpecimenData*> active { nullptr };
        std::atomic<uint64_t> epoch { 0 };
        std::unique_ptr<dsp::SpecimenData> activeOwner;                                   // loader thread
        uint64_t nextId = 1;                                                              // loader thread
        std::vector<std::pair<std::unique_ptr<dsp::SpecimenData>, uint64_t>> retired;      // loader thread

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpecimenLibrary)
    };
}
