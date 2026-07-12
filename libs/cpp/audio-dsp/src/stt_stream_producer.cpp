#include "voxmesh/dsp/stt_stream_producer.hpp"

#include <cstddef>
#include <cstring>
#include <span>

namespace voxmesh::dsp {

std::optional<SttStreamProducer> SttStreamProducer::create(const SttStreamConfig& config)
{
    const bool integerPcm =
        config.input.format == audio::SampleFormat::PcmS16Le && config.output.format == audio::SampleFormat::PcmS16Le;
    const bool channelsSupported =
        (config.input.channels.value == 1 || config.input.channels.value == 2) && config.output.channels.value == 1;
    const bool ratesSupported = config.input.sampleRate.hz > 0 && config.output.sampleRate.hz > 0 &&
                                config.input.sampleRate.hz % config.output.sampleRate.hz == 0;
    const std::size_t frameSamples = audio::samplesPerChannel(config.output.sampleRate, config.frameDuration);
    if (!integerPcm || !channelsSupported || !ratesSupported || frameSamples == 0) {
        return std::nullopt;
    }
    return SttStreamProducer(config);
}

SttStreamProducer::SttStreamProducer(const SttStreamConfig& config)
    : config_(config), decimator_(config.input.sampleRate.hz / config.output.sampleRate.hz),
      samplesPerOutputFrame_(audio::samplesPerChannel(config.output.sampleRate, config.frameDuration))
{
    pending_.reserve(samplesPerOutputFrame_ * 2);
}

std::vector<audio::AudioFrame> SttStreamProducer::process(const audio::AudioFrame& frame)
{
    std::vector<audio::AudioFrame> outputs;
    if (frame.format != config_.input.format || frame.sampleRate != config_.input.sampleRate ||
        frame.channels != config_.input.channels) {
        stats_.rejectedFrames += 1;
        return outputs;
    }
    stats_.framesIn += 1;
    if (!anchor_.has_value()) {
        anchor_ = frame.timestamp;
    }
    if (frame.discontinuity) {
        // Unhealed gap upstream: do not smear unrelated audio through the
        // filter, and tell consumers not to interpolate across it.
        decimator_.reset();
        pendingDiscontinuity_ = true;
    }

    const std::size_t samplesPerChannel = frame.sampleCountPerChannel();
    const auto* interleaved = reinterpret_cast<const std::int16_t*>(frame.payload.data());
    std::span<const std::int16_t> mono;
    if (config_.input.channels.value == 2) {
        monoScratch_.resize(samplesPerChannel);
        for (std::size_t i = 0; i < samplesPerChannel; ++i) {
            const int left = interleaved[2 * i];
            const int right = interleaved[2 * i + 1];
            monoScratch_[i] = static_cast<std::int16_t>((left + right) / 2);
        }
        mono = monoScratch_;
    } else {
        mono = std::span<const std::int16_t>(interleaved, samplesPerChannel);
    }

    decimator_.process(mono, pending_);

    while (pending_.size() >= samplesPerOutputFrame_) {
        audio::AudioFrame output;
        output.track = audio::TrackKind::SttStream;
        output.sequence = nextSequence_;
        nextSequence_ = nextSequence_.next();
        output.sampleRate = config_.output.sampleRate;
        output.channels = config_.output.channels;
        output.format = audio::SampleFormat::PcmS16Le;
        output.discontinuity = pendingDiscontinuity_;
        pendingDiscontinuity_ = false;
        const auto elapsed = std::chrono::nanoseconds{
            static_cast<std::int64_t>(samplesEmitted_ * 1'000'000'000ull / config_.output.sampleRate.hz)};
        output.timestamp = anchor_->advancedBy(elapsed);
        output.payload.resize(samplesPerOutputFrame_ * sizeof(std::int16_t));
        std::memcpy(output.payload.data(), pending_.data(), output.payload.size());
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(samplesPerOutputFrame_));
        samplesEmitted_ += samplesPerOutputFrame_;
        stats_.framesOut += 1;
        outputs.push_back(std::move(output));
    }
    return outputs;
}

} // namespace voxmesh::dsp
